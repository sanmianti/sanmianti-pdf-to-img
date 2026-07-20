#include "worker/pdf_renderer.h"

#include <windows.h>
#include <wincodec.h>

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
  }
}

template <typename T>
void Release(T*& value) {
  if (value) {
    value->Release();
    value = nullptr;
  }
}

std::wstring TestRoot() {
  wchar_t temporary[MAX_PATH]{};
  GetTempPathW(MAX_PATH, temporary);
  return std::wstring(temporary) + L"PdfToImage-renderer-" + std::to_wstring(GetCurrentProcessId());
}

bool WriteTwoPagePdf(const std::wstring& path) {
  const std::vector<std::string> objects = {
      "<< /Type /Catalog /Pages 2 0 R >>",
      "<< /Type /Pages /Kids [3 0 R 5 0 R] /Count 2 >>",
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 72 144] /Resources << /Font << /F1 4 0 R >> >> /Contents 6 0 R >>",
      "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 72 144] /Rotate 90 /Resources << /Font << /F1 4 0 R >> >> /Contents 7 0 R >>",
      "<< /Length 43 >>\nstream\nBT /F1 12 Tf 8 72 Td (Portrait) Tj ET\nendstream",
      "<< /Length 41 >>\nstream\nBT /F1 12 Tf 8 72 Td (Rotated) Tj ET\nendstream",
  };

  std::string pdf = "%PDF-1.4\n";
  std::vector<size_t> offsets(1, 0);
  for (size_t index = 0; index < objects.size(); ++index) {
    offsets.push_back(pdf.size());
    pdf += std::to_string(index + 1) + " 0 obj\n" + objects[index] + "\nendobj\n";
  }
  const size_t xref = pdf.size();
  std::ostringstream trailer;
  trailer << "xref\n0 " << offsets.size() << "\n0000000000 65535 f \n";
  for (size_t index = 1; index < offsets.size(); ++index) {
    trailer.width(10);
    trailer.fill('0');
    trailer << offsets[index] << " 00000 n \n";
  }
  trailer << "trailer\n<< /Size " << offsets.size() << " /Root 1 0 R >>\nstartxref\n"
          << xref << "\n%%EOF\n";
  pdf += trailer.str();

  std::ofstream output(path.c_str(), std::ios::binary);
  output.write(pdf.data(), static_cast<std::streamsize>(pdf.size()));
  return output.good();
}

bool WriteUniformPdf(const std::wstring& path, int page_count, int width, int height) {
  std::vector<std::string> objects;
  objects.push_back("<< /Type /Catalog /Pages 2 0 R >>");
  std::ostringstream pages;
  pages << "<< /Type /Pages /Kids [";
  for (int page = 0; page < page_count; ++page) pages << (page + 3) << " 0 R ";
  pages << "] /Count " << page_count << " >>";
  objects.push_back(pages.str());
  const int content_object = page_count + 3;
  for (int page = 0; page < page_count; ++page) {
    std::ostringstream object;
    object << "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " << width << ' ' << height
           << "] /Resources << >> /Contents " << content_object << " 0 R >>";
    objects.push_back(object.str());
  }
  objects.push_back("<< /Length 0 >>\nstream\n\nendstream");

  std::string pdf = "%PDF-1.4\n";
  std::vector<size_t> offsets(1, 0);
  for (size_t index = 0; index < objects.size(); ++index) {
    offsets.push_back(pdf.size());
    pdf += std::to_string(index + 1) + " 0 obj\n" + objects[index] + "\nendobj\n";
  }
  const size_t xref = pdf.size();
  std::ostringstream trailer;
  trailer << "xref\n0 " << offsets.size() << "\n0000000000 65535 f \n";
  for (size_t index = 1; index < offsets.size(); ++index) {
    trailer.width(10);
    trailer.fill('0');
    trailer << offsets[index] << " 00000 n \n";
  }
  trailer << "trailer\n<< /Size " << offsets.size() << " /Root 1 0 R >>\nstartxref\n"
          << xref << "\n%%EOF\n";
  pdf += trailer.str();
  std::ofstream output(path.c_str(), std::ios::binary);
  output.write(pdf.data(), static_cast<std::streamsize>(pdf.size()));
  return output.good();
}

bool InspectPng(const std::wstring& path, UINT expected_width, UINT expected_height) {
  IWICImagingFactory* factory = nullptr;
  IWICBitmapDecoder* decoder = nullptr;
  IWICBitmapFrameDecode* frame = nullptr;
  HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&factory));
  if (SUCCEEDED(result)) {
    result = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                WICDecodeMetadataCacheOnLoad, &decoder);
  }
  if (SUCCEEDED(result)) result = decoder->GetFrame(0, &frame);
  UINT width = 0;
  UINT height = 0;
  if (SUCCEEDED(result)) result = frame->GetSize(&width, &height);
  double dpi_x = 0.0;
  double dpi_y = 0.0;
  if (SUCCEEDED(result)) result = frame->GetResolution(&dpi_x, &dpi_y);
  WICPixelFormatGUID format{};
  if (SUCCEEDED(result)) result = frame->GetPixelFormat(&format);
  WICRect corner{0, 0, 1, 1};
  BYTE pixel[3]{};
  if (SUCCEEDED(result)) result = frame->CopyPixels(&corner, 3, 3, pixel);
  const bool ok = SUCCEEDED(result) && width == expected_width && height == expected_height &&
                  std::fabs(dpi_x - 144.0) < 0.2 && std::fabs(dpi_y - 144.0) < 0.2 &&
                  IsEqualGUID(format, GUID_WICPixelFormat24bppBGR) && pixel[0] == 255 &&
                  pixel[1] == 255 && pixel[2] == 255;
  Release(frame);
  Release(decoder);
  Release(factory);
  return ok;
}

}  // namespace

int wmain() {
  const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  Check(SUCCEEDED(com_result), "initialize COM");
  const std::wstring root = TestRoot();
  const std::wstring output = root + L"\\输出 图片";
  CreateDirectoryW(root.c_str(), nullptr);
  CreateDirectoryW(output.c_str(), nullptr);
  const std::wstring pdf = root + L"\\中 文（テスト）.pdf";
  Check(WriteTwoPagePdf(pdf), "write two-page PDF fixture");

  std::vector<int> progress;
  const auto result = pdfimg::ConvertPdf(
      pdf, output, [&](int current, int total, const std::string&) {
        Check(total == 2, "reported total page count");
        progress.push_back(current);
      });
  Check(result.exit_code == pdfimg::WorkerExit::kSuccess, "convert valid PDF");
  Check(result.page_count == 2, "return two pages");
  Check(progress == std::vector<int>({0, 1, 2}), "start and per-page progress callbacks");
  Check(InspectPng(output + L"\\page_001.png", 144, 288),
        "portrait PNG dimensions and 144 DPI");
  Check(InspectPng(output + L"\\page_002.png", 288, 144),
        "rotated PNG dimensions and 144 DPI");

  const std::wstring many_output = root + L"\\20页输出";
  const std::wstring many_pdf = root + L"\\20页.pdf";
  CreateDirectoryW(many_output.c_str(), nullptr);
  Check(WriteUniformPdf(many_pdf, 20, 18, 18), "write 20-page PDF fixture");
  std::vector<int> many_progress;
  const auto many_result = pdfimg::ConvertPdf(
      many_pdf, many_output,
      [&](int current, int total, const std::string&) {
        Check(total == 20, "20-page total count");
        many_progress.push_back(current);
      });
  Check(many_result.exit_code == pdfimg::WorkerExit::kSuccess && many_result.page_count == 20,
        "convert 20-page PDF");
  Check(many_progress.size() == 21 && many_progress.front() == 0 && many_progress.back() == 20,
        "20-page progress sequence");
  Check(GetFileAttributesW((many_output + L"\\page_001.png").c_str()) != INVALID_FILE_ATTRIBUTES &&
            GetFileAttributesW((many_output + L"\\page_020.png").c_str()) != INVALID_FILE_ATTRIBUTES,
        "20-page zero-padded filenames");

  const std::wstring huge_pdf = root + L"\\超大页面.pdf";
  Check(WriteUniformPdf(huge_pdf, 1, 100000, 100000), "write huge-page PDF fixture");
  const auto huge_result = pdfimg::ConvertPdf(huge_pdf, many_output, {});
  Check(huge_result.exit_code == pdfimg::WorkerExit::kPageTooLarge &&
            huge_result.error_token == "PAGE_TOO_LARGE",
        "reject page above 256 MiB before allocation");

  const std::wstring damaged = root + L"\\损坏.pdf";
  {
    std::ofstream invalid(damaged.c_str(), std::ios::binary);
    invalid << "%PDF-1.4\nthis is truncated";
  }
  const auto damaged_result = pdfimg::ConvertPdf(damaged, output, {});
  Check(damaged_result.exit_code == pdfimg::WorkerExit::kPdfDamaged,
        "map damaged PDF error");

  DeleteFileW((output + L"\\page_001.png").c_str());
  DeleteFileW((output + L"\\page_002.png").c_str());
  DeleteFileW(pdf.c_str());
  DeleteFileW(damaged.c_str());
  DeleteFileW(many_pdf.c_str());
  DeleteFileW(huge_pdf.c_str());
  for (int page = 1; page <= 20; ++page) {
    wchar_t name[32]{};
    swprintf_s(name, L"\\page_%03d.png", page);
    DeleteFileW((many_output + name).c_str());
  }
  RemoveDirectoryW(many_output.c_str());
  RemoveDirectoryW(output.c_str());
  RemoveDirectoryW(root.c_str());
  if (SUCCEEDED(com_result)) CoUninitialize();

  if (failures == 0) std::cout << "renderer_tests: all checks passed\n";
  return failures == 0 ? 0 : 1;
}
