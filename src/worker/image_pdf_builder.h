#pragma once

#include "worker/pdf_renderer.h"

#include <string>
#include <vector>

namespace pdfimg {

ConversionResult ConvertImagesToPdf(const std::vector<std::wstring>& image_paths,
                                    const std::wstring& output_path,
                                    const ProgressCallback& progress);

}  // namespace pdfimg
