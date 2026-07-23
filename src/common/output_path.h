#pragma once

#include <string>

namespace pdfimg {

struct OutputPlan {
  std::wstring final_directory;
  std::wstring temporary_directory;
};

struct FileOutputPlan {
  std::wstring final_path;
  std::wstring temporary_path;
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
bool PreparePdfOutputPlan(const std::wstring& first_image_path, const std::wstring& output_root,
                          FileOutputPlan* plan, std::wstring* error);
bool CommitPdfOutputPlan(const FileOutputPlan& plan, std::wstring* error);
bool CleanupTemporaryFile(const std::wstring& temporary_path);

}  // namespace pdfimg
