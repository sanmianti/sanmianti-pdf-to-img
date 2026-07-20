#include "app/drop_handler.h"

namespace pdfimg {

std::vector<std::wstring> ReadDroppedFiles(HDROP drop) {
  std::vector<std::wstring> files;
  const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
  files.reserve(count);
  for (UINT index = 0; index < count; ++index) {
    const UINT length = DragQueryFileW(drop, index, nullptr, 0);
    std::wstring path(static_cast<size_t>(length) + 1, L'\0');
    DragQueryFileW(drop, index, &path[0], length + 1);
    path.resize(length);
    files.push_back(std::move(path));
  }
  DragFinish(drop);
  return files;
}

}  // namespace pdfimg

