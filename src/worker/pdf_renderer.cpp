#include "worker/pdf_renderer.h"

#include "common/scope_guard.h"
#include "common/utf.h"
#include "worker/png_encoder.h"

#include <windows.h>

#include <fpdf_edit.h>
#include <fpdfview.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <vector>

namespace pdfimg {
namespace {

constexpr std::uint64_t kMaximumBitmapBytes = 256ull * 1024ull * 1024ull;
constexpr double kRenderScale = 2.0;

ConversionResult Failure(WorkerExit code, const char* token) {
  ConversionResult result;
  result.exit_code = code;
  result.error_token = token;
  return result;
}

ConversionResult MapLoadError(unsigned long error) {
  switch (error) {
    case FPDF_ERR_FILE:
      return Failure(WorkerExit::kPdfOpenFailed, "CANNOT_READ");
    case FPDF_ERR_FORMAT:
      return Failure(WorkerExit::kPdfDamaged, "PDF_DAMAGED");
    case FPDF_ERR_PASSWORD:
      return Failure(WorkerExit::kPasswordRequired, "PASSWORD_REQUIRED");
    case FPDF_ERR_SECURITY:
      return Failure(WorkerExit::kSecurityUnsupported, "SECURITY_UNSUPPORTED");
    default:
      return Failure(WorkerExit::kPdfOpenFailed, "CANNOT_READ");
  }
}

std::string PageFileName(int page_number, int page_count) {
  int digits = 1;
  for (int value = page_count; value >= 10; value /= 10) ++digits;
  digits = (std::max)(3, digits);
  std::ostringstream stream;
  stream << "page_" << std::setw(digits) << std::setfill('0') << page_number << ".png";
  return stream.str();
}

}  // namespace

ConversionResult ConvertPdf(const std::wstring& input_path,
                            const std::wstring& output_directory,
                            const ProgressCallback& progress) {
  FPDF_InitLibrary();
  auto library_guard = MakeScopeGuard([] { FPDF_DestroyLibrary(); });

  std::string utf8_path;
  try {
    utf8_path = WideToUtf8(input_path);
  } catch (...) {
    return Failure(WorkerExit::kPdfOpenFailed, "CANNOT_READ");
  }

  FPDF_DOCUMENT document = FPDF_LoadDocument(utf8_path.c_str(), nullptr);
  if (!document) return MapLoadError(FPDF_GetLastError());
  auto document_guard = MakeScopeGuard([&] { FPDF_CloseDocument(document); });

  const int page_count = FPDF_GetPageCount(document);
  if (page_count <= 0) return Failure(WorkerExit::kPdfDamaged, "PDF_DAMAGED");
  if (progress) progress(0, page_count, std::string());

  for (int page_index = 0; page_index < page_count; ++page_index) {
    FPDF_PAGE page = FPDF_LoadPage(document, page_index);
    if (!page) return Failure(WorkerExit::kRenderFailed, "RENDER_FAILED");
    auto page_guard = MakeScopeGuard([&] { FPDF_ClosePage(page); });

    const double width_points = FPDF_GetPageWidthF(page);
    const double height_points = FPDF_GetPageHeightF(page);
    const int rotation = FPDFPage_GetRotation(page);
    if (!std::isfinite(width_points) || !std::isfinite(height_points) || width_points <= 0.0 ||
        height_points <= 0.0 || rotation < 0 || rotation > 3) {
      return Failure(WorkerExit::kPageTooLarge, "PAGE_TOO_LARGE");
    }

    double width_pixels_value = std::ceil(width_points * kRenderScale);
    double height_pixels_value = std::ceil(height_points * kRenderScale);
    // PDFium 109 reports page width/height after the page's intrinsic rotation. Passing the
    // rotation to the renderer is still required, but swapping these dimensions again would
    // produce a portrait bitmap for an intrinsically rotated landscape page.
    if (width_pixels_value > static_cast<double>(INT_MAX) ||
        height_pixels_value > static_cast<double>(INT_MAX)) {
      return Failure(WorkerExit::kPageTooLarge, "PAGE_TOO_LARGE");
    }
    const int width = static_cast<int>(width_pixels_value);
    const int height = static_cast<int>(height_pixels_value);
    if (width <= 0 || height <= 0 || width > INT_MAX / 3) {
      return Failure(WorkerExit::kPageTooLarge, "PAGE_TOO_LARGE");
    }
    const int stride = (width * 3 + 3) & ~3;
    const std::uint64_t bitmap_bytes = static_cast<std::uint64_t>(stride) * height;
    if (bitmap_bytes == 0 || bitmap_bytes > kMaximumBitmapBytes ||
        bitmap_bytes > std::numeric_limits<size_t>::max()) {
      return Failure(WorkerExit::kPageTooLarge, "PAGE_TOO_LARGE");
    }

    std::vector<std::uint8_t> pixels;
    try {
      pixels.assign(static_cast<size_t>(bitmap_bytes), 0xFF);
    } catch (...) {
      return Failure(WorkerExit::kPageTooLarge, "PAGE_TOO_LARGE");
    }
    FPDF_BITMAP bitmap =
        FPDFBitmap_CreateEx(width, height, FPDFBitmap_BGR, pixels.data(), stride);
    if (!bitmap) return Failure(WorkerExit::kRenderFailed, "RENDER_FAILED");
    auto bitmap_guard = MakeScopeGuard([&] { FPDFBitmap_Destroy(bitmap); });

    FPDFBitmap_FillRect(bitmap, 0, 0, width, height, 0xFFFFFFFF);
    FPDF_RenderPageBitmap(bitmap, page, 0, 0, width, height, rotation,
                          FPDF_ANNOT | FPDF_RENDER_LIMITEDIMAGECACHE);

    const std::string file_name = PageFileName(page_index + 1, page_count);
    std::wstring wide_file_name;
    try {
      wide_file_name = Utf8ToWide(file_name);
    } catch (...) {
      return Failure(WorkerExit::kPngWriteFailed, "CANNOT_WRITE");
    }
    const std::wstring output_path = output_directory + L"\\" + wide_file_name;
    const PngWriteResult write_result = WritePngAtomically(
        output_path, pixels.data(), static_cast<unsigned>(width), static_cast<unsigned>(height),
        static_cast<unsigned>(stride));
    if (write_result == PngWriteResult::kDiskFull) {
      return Failure(WorkerExit::kPngWriteFailed, "DISK_FULL");
    }
    if (write_result != PngWriteResult::kSuccess) {
      return Failure(WorkerExit::kPngWriteFailed, "CANNOT_WRITE");
    }
    if (progress) progress(page_index + 1, page_count, file_name);
  }

  ConversionResult success;
  success.exit_code = WorkerExit::kSuccess;
  success.error_token.clear();
  success.page_count = page_count;
  return success;
}

}  // namespace pdfimg
