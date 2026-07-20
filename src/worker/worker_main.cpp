#include "common/error_code.h"
#include "common/output_path.h"
#include "common/protocol.h"
#include "worker/pdf_renderer.h"

#include <windows.h>
#include <objbase.h>

#include <string>

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

}  // namespace

int wmain(int argc, wchar_t* argv[]) {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
  if (argc != 5 || std::wstring(argv[1]) != L"--input" ||
      std::wstring(argv[3]) != L"--output") {
    return Fail(pdfimg::WorkerExit::kBadArguments, "BAD_ARGUMENTS");
  }

  const std::wstring input_path = argv[2];
  const std::wstring output_directory = argv[4];
  if (!pdfimg::IsRegularPdf(input_path)) {
    return Fail(pdfimg::WorkerExit::kPdfOpenFailed, "CANNOT_READ");
  }
  const DWORD output_attributes = GetFileAttributesW(output_directory.c_str());
  if (output_attributes == INVALID_FILE_ATTRIBUTES ||
      !(output_attributes & FILE_ATTRIBUTE_DIRECTORY)) {
    return Fail(pdfimg::WorkerExit::kPngWriteFailed, "CANNOT_WRITE");
  }

  const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(com_result)) return Fail(pdfimg::WorkerExit::kUnknown, "UNKNOWN");

  const pdfimg::ConversionResult result = pdfimg::ConvertPdf(
      input_path, output_directory, [](int current, int total, const std::string& file_name) {
        if (current == 0) {
          WriteProtocolLine(pdfimg::FormatStartEvent(total));
        } else {
          WriteProtocolLine(pdfimg::FormatProgressEvent(current, total, file_name));
        }
      });
  CoUninitialize();

  if (result.exit_code == pdfimg::WorkerExit::kSuccess) {
    WriteProtocolLine(pdfimg::FormatDoneEvent(result.page_count));
    return 0;
  }
  WriteProtocolLine(pdfimg::FormatErrorEvent(result.error_token));
  return static_cast<int>(result.exit_code);
}
