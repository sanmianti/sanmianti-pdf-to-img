#include "worker/png_encoder.h"

#include <windows.h>
#include <wincodec.h>

#include <limits>

namespace pdfimg {
namespace {

template <typename T>
void Release(T*& value) {
  if (value) {
    value->Release();
    value = nullptr;
  }
}

bool IsDiskFull(HRESULT result) {
  return result == HRESULT_FROM_WIN32(ERROR_DISK_FULL) ||
         result == HRESULT_FROM_WIN32(ERROR_HANDLE_DISK_FULL) || result == STG_E_MEDIUMFULL;
}

}  // namespace

PngWriteResult WritePngAtomically(const std::wstring& output_path,
                                  const std::uint8_t* pixels,
                                  unsigned width,
                                  unsigned height,
                                  unsigned stride) {
  if (!pixels || width == 0 || height == 0 ||
      static_cast<unsigned long long>(stride) * height > std::numeric_limits<UINT>::max()) {
    return PngWriteResult::kWriteFailed;
  }

  const std::wstring temporary_path = output_path + L".tmp";
  DeleteFileW(temporary_path.c_str());
  HRESULT result = S_OK;
  IWICImagingFactory* factory = nullptr;
  IWICStream* stream = nullptr;
  IWICBitmapEncoder* encoder = nullptr;
  IWICBitmapFrameEncode* frame = nullptr;
  IPropertyBag2* options = nullptr;

  result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                            IID_PPV_ARGS(&factory));
  if (SUCCEEDED(result)) result = factory->CreateStream(&stream);
  if (SUCCEEDED(result)) {
    result = stream->InitializeFromFilename(temporary_path.c_str(), GENERIC_WRITE);
  }
  if (SUCCEEDED(result)) {
    result = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
  }
  if (SUCCEEDED(result)) result = encoder->Initialize(stream, WICBitmapEncoderNoCache);
  if (SUCCEEDED(result)) result = encoder->CreateNewFrame(&frame, &options);
  if (SUCCEEDED(result)) result = frame->Initialize(options);
  if (SUCCEEDED(result)) result = frame->SetSize(width, height);
  if (SUCCEEDED(result)) result = frame->SetResolution(144.0, 144.0);
  WICPixelFormatGUID pixel_format = GUID_WICPixelFormat24bppBGR;
  if (SUCCEEDED(result)) result = frame->SetPixelFormat(&pixel_format);
  if (SUCCEEDED(result) && !IsEqualGUID(pixel_format, GUID_WICPixelFormat24bppBGR)) {
    result = WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
  }
  if (SUCCEEDED(result)) {
    result = frame->WritePixels(height, stride, stride * height,
                                const_cast<BYTE*>(pixels));
  }
  if (SUCCEEDED(result)) result = frame->Commit();
  if (SUCCEEDED(result)) result = encoder->Commit();

  Release(options);
  Release(frame);
  Release(encoder);
  Release(stream);
  Release(factory);

  if (SUCCEEDED(result)) {
    if (!MoveFileExW(temporary_path.c_str(), output_path.c_str(), MOVEFILE_WRITE_THROUGH)) {
      const DWORD code = GetLastError();
      DeleteFileW(temporary_path.c_str());
      return (code == ERROR_DISK_FULL || code == ERROR_HANDLE_DISK_FULL)
                 ? PngWriteResult::kDiskFull
                 : PngWriteResult::kWriteFailed;
    }
    return PngWriteResult::kSuccess;
  }

  DeleteFileW(temporary_path.c_str());
  return IsDiskFull(result) ? PngWriteResult::kDiskFull : PngWriteResult::kWriteFailed;
}

}  // namespace pdfimg

