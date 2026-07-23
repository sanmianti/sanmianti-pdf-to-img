#include "common/error_code.h"
#include "common/output_path.h"
#include "common/protocol.h"
#include "worker/image_pdf_builder.h"
#include "worker/pdf_renderer.h"

#include <windows.h>
#include <objbase.h>

#include <string>
#include <cstdint>
#include <vector>

namespace {

bool WriteProtocolLine(const std::string& line) {
  HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
  if (!output || output == INVALID_HANDLE_VALUE) return false;
  const char* cursor = line.data();
  DWORD remaining = static_cast<DWORD>(line.size());
  while (remaining) {
    DWORD written = 0;
    if (!WriteFile(output, cursor, remaining, &written, nullptr) || written == 0) return false;
    cursor += written;
    remaining -= written;
  }
  return true;
}

int Fail(pdfimg::WorkerExit code, const char* token) {
  WriteProtocolLine(pdfimg::FormatErrorEvent(token));
  return static_cast<int>(code);
}

bool ReadExact(HANDLE file, void* data, DWORD bytes) {
  auto* cursor = static_cast<std::uint8_t*>(data);
  DWORD remaining = bytes;
  while (remaining > 0) {
    DWORD read = 0;
    if (!ReadFile(file, cursor, remaining, &read, nullptr) || read == 0) return false;
    cursor += read;
    remaining -= read;
  }
  return true;
}

bool ReadManifest(const std::wstring& path, std::vector<std::wstring>* images) {
  if (!images) return false;
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  std::uint32_t count = 0;
  bool ok = ReadExact(file, &count, sizeof(count)) && count > 0 && count <= 10000;
  std::vector<std::wstring> parsed;
  if (ok) parsed.reserve(count);
  for (std::uint32_t index = 0; ok && index < count; ++index) {
    std::uint32_t length = 0;
    ok = ReadExact(file, &length, sizeof(length)) && length > 0 && length <= 32767;
    if (!ok) break;
    std::wstring value(length, L'\0');
    ok = ReadExact(file, &value[0], length * sizeof(wchar_t));
    if (ok) parsed.push_back(std::move(value));
  }
  CloseHandle(file);
  if (!ok) return false;
  *images = std::move(parsed);
  return true;
}

}  // namespace

int wmain(int argc, wchar_t* argv[]) {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
  if (argc != 5 || (std::wstring(argv[1]) != L"--input" &&
                    std::wstring(argv[1]) != L"--images") ||
      std::wstring(argv[3]) != L"--output") {
    return Fail(pdfimg::WorkerExit::kBadArguments, "BAD_ARGUMENTS");
  }

  const std::wstring input_path = argv[2];
  const std::wstring output_directory = argv[4];
  const bool images_to_pdf = std::wstring(argv[1]) == L"--images";
  std::vector<std::wstring> images;
  if (images_to_pdf) {
    if (!ReadManifest(input_path, &images)) {
      return Fail(pdfimg::WorkerExit::kImageReadFailed, "IMAGE_READ_FAILED");
    }
  } else {
    if (!pdfimg::IsRegularPdf(input_path)) {
      return Fail(pdfimg::WorkerExit::kPdfOpenFailed, "CANNOT_READ");
    }
    const DWORD output_attributes = GetFileAttributesW(output_directory.c_str());
    if (output_attributes == INVALID_FILE_ATTRIBUTES ||
        !(output_attributes & FILE_ATTRIBUTE_DIRECTORY)) {
      return Fail(pdfimg::WorkerExit::kPngWriteFailed, "CANNOT_WRITE");
    }
  }

  const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(com_result)) return Fail(pdfimg::WorkerExit::kUnknown, "UNKNOWN");

  const auto progress = [](int current, int total, const std::string& file_name) {
        if (current == 0) {
          WriteProtocolLine(pdfimg::FormatStartEvent(total));
        } else {
          WriteProtocolLine(pdfimg::FormatProgressEvent(current, total, file_name));
        }
      };
  const pdfimg::ConversionResult result =
      images_to_pdf ? pdfimg::ConvertImagesToPdf(images, output_directory, progress)
                    : pdfimg::ConvertPdf(input_path, output_directory, progress);
  CoUninitialize();

  if (result.exit_code == pdfimg::WorkerExit::kSuccess) {
    WriteProtocolLine(pdfimg::FormatDoneEvent(result.page_count));
    return 0;
  }
  WriteProtocolLine(pdfimg::FormatErrorEvent(result.error_token));
  return static_cast<int>(result.exit_code);
}
