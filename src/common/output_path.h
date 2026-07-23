#pragma once

#include <string>

namespace pdfimg {

struct OutputPlan {
  std::wstring final_directory;
  std::wstring temporary_directory;
};

bool IsRegularPdf(const std::wstring& path);
std::wstring FileNameFromPath(const std::wstring& path);
std::wstring BaseOutputDirectory(const std::wstring& pdf_path);
std::wstring BaseOutputDirectory(const std::wstring& pdf_path,
                                 const std::wstring& output_root);
bool PrepareOutputPlan(const std::wstring& pdf_path, OutputPlan* plan, std::wstring* error);
bool PrepareOutputPlan(const std::wstring& pdf_path, const std::wstring& output_root,
                       OutputPlan* plan, std::wstring* error);
bool CommitOutputPlan(const OutputPlan& plan, std::wstring* error);
bool CleanupTemporaryDirectory(const std::wstring& temporary_directory);

}  // namespace pdfimg
