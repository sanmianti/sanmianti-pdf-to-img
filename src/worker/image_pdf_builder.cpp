#include "worker/image_pdf_builder.h"

#include "common/scope_guard.h"
#include "common/utf.h"

#include <windows.h>
#include <wincodec.h>
#include <propvarutil.h>

#include <fpdf_edit.h>
#include <fpdf_save.h>
#include <fpdfview.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace pdfimg {
namespace {

constexpr std::uint64_t kMaximumImageBytes = 256ull * 1024ull * 1024ull;

template <typename T>
void Release(T*& value) {
  if (value) {
    value->Release();
    value = nullptr;
  }
}

ConversionResult Failure(WorkerExit code, const char* token) {
  ConversionResult result;
  result.exit_code = code;
  result.error_token = token;
  return result;
}

struct DecodedImage {
  UINT width = 0;
  UINT height = 0;
  UINT stride = 0;
  double dpi_x = 96.0;
  double dpi_y = 96.0;
  std::vector<std::uint8_t> pixels;
};

WICBitmapTransformOptions OrientationTransform(IWICBitmapFrameDecode* frame) {
  IWICMetadataQueryReader* reader = nullptr;
  PROPVARIANT value{};
  PropVariantInit(&value);
  unsigned orientation = 1;
  if (frame && SUCCEEDED(frame->GetMetadataQueryReader(&reader)) &&
      SUCCEEDED(reader->GetMetadataByName(L"/app1/ifd/{ushort=274}", &value))) {
    if (value.vt == VT_UI2) orientation = value.uiVal;
    if (value.vt == VT_UI4) orientation = value.ulVal;
  }
  PropVariantClear(&value);
  Release(reader);
  switch (orientation) {
    case 2:
      return WICBitmapTransformFlipHorizontal;
    case 3:
      return WICBitmapTransformRotate180;
    case 4:
      return WICBitmapTransformFlipVertical;
    case 5:
      return static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate90 |
                                                    WICBitmapTransformFlipHorizontal);
    case 6:
      return WICBitmapTransformRotate90;
    case 7:
      return static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate270 |
                                                    WICBitmapTransformFlipHorizontal);
    case 8:
      return WICBitmapTransformRotate270;
    default:
      return WICBitmapTransformRotate0;
  }
}

bool DecodeImage(IWICImagingFactory* factory, const std::wstring& path, DecodedImage* image) {
  if (!factory || !image) return false;
  IWICBitmapDecoder* decoder = nullptr;
  IWICBitmapFrameDecode* frame = nullptr;
  IWICBitmapFlipRotator* rotator = nullptr;
  IWICFormatConverter* converter = nullptr;
  HRESULT result = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                       WICDecodeMetadataCacheOnLoad, &decoder);
  if (SUCCEEDED(result)) result = decoder->GetFrame(0, &frame);
  WICBitmapTransformOptions transform = WICBitmapTransformRotate0;
  IWICBitmapSource* source = frame;
  if (SUCCEEDED(result)) {
    transform = OrientationTransform(frame);
    if (transform != WICBitmapTransformRotate0) {
      result = factory->CreateBitmapFlipRotator(&rotator);
      if (SUCCEEDED(result)) result = rotator->Initialize(frame, transform);
      if (SUCCEEDED(result)) source = rotator;
    }
  }
  if (SUCCEEDED(result)) result = source->GetSize(&image->width, &image->height);
  if (SUCCEEDED(result)) {
    double dpi_x = 0.0;
    double dpi_y = 0.0;
    if (SUCCEEDED(frame->GetResolution(&dpi_x, &dpi_y)) && std::isfinite(dpi_x) &&
        std::isfinite(dpi_y) && dpi_x >= 10.0 && dpi_y >= 10.0) {
      image->dpi_x = dpi_x;
      image->dpi_y = dpi_y;
    }
    if ((transform & WICBitmapTransformRotate90) == WICBitmapTransformRotate90 ||
        (transform & WICBitmapTransformRotate270) == WICBitmapTransformRotate270) {
      std::swap(image->dpi_x, image->dpi_y);
    }
  }
  if (SUCCEEDED(result)) result = factory->CreateFormatConverter(&converter);
  if (SUCCEEDED(result)) {
    result = converter->Initialize(source, GUID_WICPixelFormat32bppBGRA,
                                   WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom);
  }
  const std::uint64_t source_stride = static_cast<std::uint64_t>(image->width) * 4;
  const std::uint64_t source_bytes = source_stride * image->height;
  if (SUCCEEDED(result) &&
      (image->width == 0 || image->height == 0 || source_stride > UINT_MAX ||
       source_bytes > kMaximumImageBytes || source_bytes > UINT_MAX)) {
    result = E_OUTOFMEMORY;
  }
  std::vector<std::uint8_t> source_pixels;
  if (SUCCEEDED(result)) {
    try {
      source_pixels.resize(static_cast<size_t>(source_bytes));
    } catch (...) {
      result = E_OUTOFMEMORY;
    }
  }
  if (SUCCEEDED(result)) {
    result = converter->CopyPixels(nullptr, static_cast<UINT>(source_stride),
                                   static_cast<UINT>(source_bytes), source_pixels.data());
  }
  if (SUCCEEDED(result)) {
    image->stride = (image->width * 3 + 3) & ~3u;
    const std::uint64_t target_bytes = static_cast<std::uint64_t>(image->stride) * image->height;
    if (target_bytes > kMaximumImageBytes || target_bytes > std::numeric_limits<size_t>::max()) {
      result = E_OUTOFMEMORY;
    } else {
      try {
        image->pixels.assign(static_cast<size_t>(target_bytes), 0xFF);
      } catch (...) {
        result = E_OUTOFMEMORY;
      }
    }
  }
  if (SUCCEEDED(result)) {
    for (UINT y = 0; y < image->height; ++y) {
      const std::uint8_t* source_row =
          source_pixels.data() + static_cast<size_t>(source_stride) * y;
      std::uint8_t* target_row = image->pixels.data() + static_cast<size_t>(image->stride) * y;
      for (UINT x = 0; x < image->width; ++x) {
        const unsigned alpha = source_row[x * 4 + 3];
        for (unsigned channel = 0; channel < 3; ++channel) {
          const unsigned value = source_row[x * 4 + channel];
          target_row[x * 3 + channel] =
              static_cast<std::uint8_t>((value * alpha + 255u * (255u - alpha) + 127u) / 255u);
        }
      }
    }
  }
  Release(converter);
  Release(rotator);
  Release(frame);
  Release(decoder);
  return SUCCEEDED(result);
}

struct FileWriter : FPDF_FILEWRITE {
  HANDLE file = INVALID_HANDLE_VALUE;
  DWORD last_error = ERROR_SUCCESS;
};

int WriteBlock(FPDF_FILEWRITE* base, const void* data, unsigned long size) {
  auto* writer = static_cast<FileWriter*>(base);
  const std::uint8_t* cursor = static_cast<const std::uint8_t*>(data);
  unsigned long remaining = size;
  while (remaining > 0) {
    DWORD written = 0;
    if (!WriteFile(writer->file, cursor, remaining, &written, nullptr) || written == 0) {
      writer->last_error = GetLastError();
      return 0;
    }
    cursor += written;
    remaining -= written;
  }
  return 1;
}

}  // namespace

ConversionResult ConvertImagesToPdf(const std::vector<std::wstring>& image_paths,
                                    const std::wstring& output_path,
                                    const ProgressCallback& progress) {
  if (image_paths.empty()) return Failure(WorkerExit::kImageReadFailed, "IMAGE_READ_FAILED");
  FPDF_InitLibrary();
  auto library_guard = MakeScopeGuard([] { FPDF_DestroyLibrary(); });

  IWICImagingFactory* factory = nullptr;
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&factory)))) {
    return Failure(WorkerExit::kImageReadFailed, "IMAGE_READ_FAILED");
  }
  auto factory_guard = MakeScopeGuard([&] { Release(factory); });

  FPDF_DOCUMENT document = FPDF_CreateNewDocument();
  if (!document) return Failure(WorkerExit::kPdfWriteFailed, "PDF_CREATE_FAILED");
  auto document_guard = MakeScopeGuard([&] { FPDF_CloseDocument(document); });
  if (progress) progress(0, static_cast<int>(image_paths.size()), std::string());

  for (size_t index = 0; index < image_paths.size(); ++index) {
    DecodedImage image;
    if (!DecodeImage(factory, image_paths[index], &image)) {
      return Failure(WorkerExit::kImageReadFailed, "IMAGE_READ_FAILED");
    }
    const double width_points = image.width * 72.0 / image.dpi_x;
    const double height_points = image.height * 72.0 / image.dpi_y;
    if (!std::isfinite(width_points) || !std::isfinite(height_points) || width_points <= 0.0 ||
        height_points <= 0.0 || width_points > 14400.0 || height_points > 14400.0) {
      return Failure(WorkerExit::kPageTooLarge, "IMAGE_TOO_LARGE");
    }

    FPDF_PAGE page = FPDFPage_New(document, static_cast<int>(index), width_points, height_points);
    if (!page) return Failure(WorkerExit::kPdfWriteFailed, "PDF_CREATE_FAILED");
    auto page_guard = MakeScopeGuard([&] { FPDF_ClosePage(page); });
    FPDF_PAGEOBJECT image_object = FPDFPageObj_NewImageObj(document);
    if (!image_object) return Failure(WorkerExit::kPdfWriteFailed, "PDF_CREATE_FAILED");
    auto object_guard = MakeScopeGuard([&] { FPDFPageObj_Destroy(image_object); });
    FPDF_BITMAP bitmap = FPDFBitmap_CreateEx(static_cast<int>(image.width),
                                             static_cast<int>(image.height), FPDFBitmap_BGR,
                                             image.pixels.data(), static_cast<int>(image.stride));
    if (!bitmap) return Failure(WorkerExit::kImageReadFailed, "IMAGE_TOO_LARGE");
    auto bitmap_guard = MakeScopeGuard([&] { FPDFBitmap_Destroy(bitmap); });
    if (!FPDFImageObj_SetBitmap(nullptr, 0, image_object, bitmap) ||
        !FPDFImageObj_SetMatrix(image_object, width_points, 0, 0, height_points, 0, 0)) {
      return Failure(WorkerExit::kPdfWriteFailed, "PDF_CREATE_FAILED");
    }
    FPDFPage_InsertObject(page, image_object);
    object_guard.Dismiss();
    if (!FPDFPage_GenerateContent(page)) {
      return Failure(WorkerExit::kPdfWriteFailed, "PDF_CREATE_FAILED");
    }
    std::string file_name;
    try {
      file_name = WideToUtf8(image_paths[index].substr(image_paths[index].find_last_of(L"\\/") + 1));
    } catch (...) {
      file_name = "image";
    }
    if (progress) {
      progress(static_cast<int>(index + 1), static_cast<int>(image_paths.size()), file_name);
    }
  }

  DeleteFileW(output_path.c_str());
  FileWriter writer{};
  writer.version = 1;
  writer.WriteBlock = WriteBlock;
  writer.file = CreateFileW(output_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (writer.file == INVALID_HANDLE_VALUE) {
    return Failure(WorkerExit::kPdfWriteFailed, "CANNOT_WRITE");
  }
  const bool saved = FPDF_SaveAsCopy(document, &writer, FPDF_NO_INCREMENTAL) != 0;
  FlushFileBuffers(writer.file);
  CloseHandle(writer.file);
  if (!saved) {
    DeleteFileW(output_path.c_str());
    if (writer.last_error == ERROR_DISK_FULL || writer.last_error == ERROR_HANDLE_DISK_FULL) {
      return Failure(WorkerExit::kPdfWriteFailed, "DISK_FULL");
    }
    return Failure(WorkerExit::kPdfWriteFailed, "CANNOT_WRITE");
  }

  ConversionResult success;
  success.exit_code = WorkerExit::kSuccess;
  success.error_token.clear();
  success.page_count = static_cast<int>(image_paths.size());
  return success;
}

}  // namespace pdfimg
