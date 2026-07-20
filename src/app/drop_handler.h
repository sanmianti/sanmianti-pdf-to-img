#pragma once

#include <windows.h>
#include <shellapi.h>

#include <string>
#include <vector>

namespace pdfimg {

std::vector<std::wstring> ReadDroppedFiles(HDROP drop);

}  // namespace pdfimg

