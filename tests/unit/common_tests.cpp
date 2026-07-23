#include "common/folder_opener.h"
#include "common/output_path.h"
#include "common/protocol.h"
#include "common/utf.h"

#include <windows.h>

#include <cstdio>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
  }
}

std::wstring TestRoot() {
  wchar_t temporary[MAX_PATH]{};
  GetTempPathW(MAX_PATH, temporary);
  return std::wstring(temporary) + L"PdfToImage-tests-" + std::to_wstring(GetCurrentProcessId());
}

}  // namespace

int wmain(int argc, wchar_t* argv[]) {
  if (argc == 3 && std::wstring(argv[1]) == L"--open-folder") {
    return pdfimg::OpenFolderExact(argv[2]) ? 0 : 1;
  }
  const std::wstring unicode = L"中文 路径（テスト）.pdf";
  Check(pdfimg::Utf8ToWide(pdfimg::WideToUtf8(unicode)) == unicode, "UTF-8 round trip");

  pdfimg::WorkerEvent event;
  Check(pdfimg::ParseWorkerEvent("START\t20", &event) &&
            event.type == pdfimg::WorkerEventType::kStart && event.total == 20,
        "parse START");
  Check(pdfimg::ParseWorkerEvent("PROGRESS\t12\t20\tpage_012.png", &event) &&
            event.type == pdfimg::WorkerEventType::kProgress && event.current == 12 &&
            event.total == 20 && event.value == "page_012.png",
        "parse PROGRESS");
  Check(pdfimg::ParseWorkerEvent("DONE\t20", &event) &&
            event.type == pdfimg::WorkerEventType::kDone,
        "parse DONE");
  Check(pdfimg::ParseWorkerEvent("ERROR\tPASSWORD_REQUIRED", &event) &&
            event.type == pdfimg::WorkerEventType::kError,
        "parse ERROR");
  Check(!pdfimg::ParseWorkerEvent("PROGRESS\t21\t20\tpage.png", &event),
        "reject inconsistent progress");
  Check(!pdfimg::ParseWorkerEvent("START\t0", &event), "reject zero page count");
  Check(pdfimg::BuildExplorerCommandLine(L"D:\\桌面 文件\\方案（2）_图片") ==
            L"explorer.exe /e,\"D:\\桌面 文件\\方案（2）_图片\"",
        "preserve exact Unicode output folder in Explorer command");
  Check(pdfimg::BuildExplorerCommandLine(L"").empty(), "reject empty Explorer folder");

  const std::wstring root = TestRoot();
  CreateDirectoryW(root.c_str(), nullptr);
  const std::wstring pdf = root + L"\\项目 方案（测试）.PDF";
  FILE* file = nullptr;
  _wfopen_s(&file, pdf.c_str(), L"wb");
  Check(file != nullptr, "create test PDF placeholder");
  if (file) {
    fputs("%PDF-1.4\n", file);
    fclose(file);
  }
  Check(pdfimg::IsRegularPdf(pdf), "case-insensitive PDF extension");
  Check(pdfimg::BaseOutputDirectory(pdf) == root + L"\\项目 方案（测试）_图片",
        "base output naming");

  const std::wstring custom_output = root + L"\\自定义保存位置";
  CreateDirectoryW(custom_output.c_str(), nullptr);
  Check(pdfimg::BaseOutputDirectory(pdf, custom_output) ==
            custom_output + L"\\项目 方案（测试）_图片",
        "base output naming in configured folder");
  pdfimg::OutputPlan configured;
  std::wstring configured_error;
  Check(pdfimg::PrepareOutputPlan(pdf, custom_output, &configured, &configured_error),
        "prepare output plan in configured folder");
  Check(pdfimg::CleanupTemporaryDirectory(configured.temporary_directory),
        "clean configured output temporary directory");
  RemoveDirectoryW(custom_output.c_str());

  pdfimg::OutputPlan first;
  std::wstring error;
  Check(pdfimg::PrepareOutputPlan(pdf, &first, &error), "prepare output plan");
  Check(GetFileAttributesW(first.temporary_directory.c_str()) != INVALID_FILE_ATTRIBUTES,
        "temporary directory exists");
  Check(pdfimg::CommitOutputPlan(first, &error), "commit output plan");
  Check(GetFileAttributesW(first.final_directory.c_str()) != INVALID_FILE_ATTRIBUTES,
        "final directory exists");

  pdfimg::OutputPlan second;
  Check(pdfimg::PrepareOutputPlan(pdf, &second, &error), "prepare non-overwriting plan");
  Check(second.final_directory == first.final_directory + L" (2)", "suffix existing output");
  Check(pdfimg::CleanupTemporaryDirectory(second.temporary_directory), "clean owned temp");
  Check(!pdfimg::CleanupTemporaryDirectory(root), "refuse unsafe cleanup");

  RemoveDirectoryW(first.final_directory.c_str());
  DeleteFileW(pdf.c_str());
  RemoveDirectoryW(root.c_str());

  if (failures == 0) std::cout << "common_tests: all checks passed\n";
  return failures == 0 ? 0 : 1;
}
