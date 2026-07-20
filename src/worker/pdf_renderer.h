#pragma once

#include "common/error_code.h"

#include <functional>
#include <string>

namespace pdfimg {

struct ConversionResult {
  WorkerExit exit_code = WorkerExit::kUnknown;
  std::string error_token = "UNKNOWN";
  int page_count = 0;
};

using ProgressCallback = std::function<void(int current, int total, const std::string& file_name)>;

ConversionResult ConvertPdf(const std::wstring& input_path,
                            const std::wstring& output_directory,
                            const ProgressCallback& progress);

}  // namespace pdfimg

