#pragma once

#include <cstdint>
#include <string>

namespace pdfimg {

enum class PngWriteResult { kSuccess, kWriteFailed, kDiskFull };

PngWriteResult WritePngAtomically(const std::wstring& output_path,
                                  const std::uint8_t* pixels,
                                  unsigned width,
                                  unsigned height,
                                  unsigned stride);

}  // namespace pdfimg

