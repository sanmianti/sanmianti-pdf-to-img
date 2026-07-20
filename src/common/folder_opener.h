#pragma once

#include <string>

namespace pdfimg {

std::wstring BuildExplorerCommandLine(const std::wstring& directory);
bool OpenFolderExact(const std::wstring& directory);

}  // namespace pdfimg

