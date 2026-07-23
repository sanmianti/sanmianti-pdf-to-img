#include "common/protocol.h"

#include <windows.h>

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

std::wstring Quote(const std::wstring& value) { return L"\"" + value + L"\""; }

std::wstring TestRoot() {
  wchar_t temporary[MAX_PATH]{};
  GetTempPathW(MAX_PATH, temporary);
  return std::wstring(temporary) + L"PdfToImage-worker-" + std::to_wstring(GetCurrentProcessId());
}

bool WriteOnePagePdf(const std::wstring& path) {
  const std::vector<std::string> objects = {
      "<< /Type /Catalog /Pages 2 0 R >>",
      "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 72 72] /Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>",
      "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
      "<< /Length 40 >>\nstream\nBT /F1 10 Tf 8 36 Td (Worker) Tj ET\nendstream",
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

struct ProcessResult {
  DWORD exit_code = static_cast<DWORD>(-1);
  std::string output;
};

ProcessResult RunWorker(const std::wstring& worker,
                        const std::wstring& input,
                        const std::wstring& output,
                        bool images_to_pdf = false) {
  ProcessResult result;
  SECURITY_ATTRIBUTES security{};
  security.nLength = sizeof(security);
  security.bInheritHandle = TRUE;
  HANDLE read_pipe = nullptr;
  HANDLE write_pipe = nullptr;
  if (!CreatePipe(&read_pipe, &write_pipe, &security, 0)) return result;
  SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  startup.hStdOutput = write_pipe;
  startup.hStdError = write_pipe;
  PROCESS_INFORMATION process{};
  std::wstring command = Quote(worker) + (images_to_pdf ? L" --images " : L" --input ") +
                         Quote(input) + L" --output " + Quote(output);
  const BOOL created = CreateProcessW(worker.c_str(), &command[0], nullptr, nullptr, TRUE,
                                      CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
  CloseHandle(write_pipe);
  if (!created) {
    CloseHandle(read_pipe);
    return result;
  }

  char buffer[1024];
  while (true) {
    DWORD read = 0;
    if (!ReadFile(read_pipe, buffer, sizeof(buffer), &read, nullptr) || read == 0) break;
    result.output.append(buffer, buffer + read);
  }
  WaitForSingleObject(process.hProcess, 30000);
  GetExitCodeProcess(process.hProcess, &result.exit_code);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  CloseHandle(read_pipe);
  return result;
}

bool WriteManifest(const std::wstring& path, const std::vector<std::wstring>& images) {
  std::ofstream output(path.c_str(), std::ios::binary);
  const std::uint32_t count = static_cast<std::uint32_t>(images.size());
  output.write(reinterpret_cast<const char*>(&count), sizeof(count));
  for (const auto& image : images) {
    const std::uint32_t length = static_cast<std::uint32_t>(image.size());
    output.write(reinterpret_cast<const char*>(&length), sizeof(length));
    output.write(reinterpret_cast<const char*>(image.data()),
                 static_cast<std::streamsize>(length * sizeof(wchar_t)));
  }
  return output.good();
}

std::vector<pdfimg::WorkerEvent> ParseLines(const std::string& output) {
  std::vector<pdfimg::WorkerEvent> events;
  size_t begin = 0;
  while (begin < output.size()) {
    const size_t end = output.find('\n', begin);
    std::string line = output.substr(begin, end == std::string::npos ? end : end - begin);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    pdfimg::WorkerEvent event;
    if (pdfimg::ParseWorkerEvent(line, &event)) events.push_back(event);
    if (end == std::string::npos) break;
    begin = end + 1;
  }
  return events;
}

}  // namespace

int wmain(int argc, wchar_t* argv[]) {
  Check(argc == 2, "worker path argument");
  if (argc != 2) return 1;
  const std::wstring root = TestRoot();
  const std::wstring output = root + L"\\输出";
  CreateDirectoryW(root.c_str(), nullptr);
  CreateDirectoryW(output.c_str(), nullptr);
  const std::wstring valid = root + L"\\进程 协议.pdf";
  Check(WriteOnePagePdf(valid), "write worker PDF fixture");

  const ProcessResult valid_result = RunWorker(argv[1], valid, output);
  Check(valid_result.exit_code == 0, "worker successful exit code");
  const auto valid_events = ParseLines(valid_result.output);
  Check(valid_events.size() == 3, "worker emits exactly three events");
  if (valid_events.size() == 3) {
    Check(valid_events[0].type == pdfimg::WorkerEventType::kStart && valid_events[0].total == 1,
          "worker START event");
    Check(valid_events[1].type == pdfimg::WorkerEventType::kProgress &&
              valid_events[1].current == 1 && valid_events[1].value == "page_001.png",
          "worker PROGRESS event");
    Check(valid_events[2].type == pdfimg::WorkerEventType::kDone && valid_events[2].total == 1,
          "worker DONE event");
  }
  Check(GetFileAttributesW((output + L"\\page_001.png").c_str()) != INVALID_FILE_ATTRIBUTES,
        "worker creates PNG");

  const std::wstring manifest = root + L"\\图片清单.bin";
  const std::wstring combined = root + L"\\合成结果.tmp";
  Check(WriteManifest(manifest, {output + L"\\page_001.png"}), "write image manifest");
  const ProcessResult image_result = RunWorker(argv[1], manifest, combined, true);
  Check(image_result.exit_code == 0, "image-to-PDF worker successful exit code");
  const auto image_events = ParseLines(image_result.output);
  Check(image_events.size() == 3 && image_events.front().type == pdfimg::WorkerEventType::kStart &&
            image_events.back().type == pdfimg::WorkerEventType::kDone,
        "image-to-PDF worker protocol events");
  Check(GetFileAttributesW(combined.c_str()) != INVALID_FILE_ATTRIBUTES,
        "image-to-PDF worker creates output");

  const std::wstring damaged = root + L"\\损坏.pdf";
  {
    std::ofstream invalid(damaged.c_str(), std::ios::binary);
    invalid << "%PDF-1.4\ntruncated";
  }
  const ProcessResult damaged_result = RunWorker(argv[1], damaged, output);
  Check(damaged_result.exit_code == 21, "damaged worker exit code");
  const auto damaged_events = ParseLines(damaged_result.output);
  Check(damaged_events.size() == 1 &&
            damaged_events[0].type == pdfimg::WorkerEventType::kError &&
            damaged_events[0].value == "PDF_DAMAGED",
        "damaged worker protocol error");

  DeleteFileW((output + L"\\page_001.png").c_str());
  DeleteFileW(manifest.c_str());
  DeleteFileW(combined.c_str());
  DeleteFileW(valid.c_str());
  DeleteFileW(damaged.c_str());
  RemoveDirectoryW(output.c_str());
  RemoveDirectoryW(root.c_str());
  if (failures == 0) std::cout << "worker_process_tests: all checks passed\n";
  return failures == 0 ? 0 : 1;
}
