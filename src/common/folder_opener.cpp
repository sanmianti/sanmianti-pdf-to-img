#include "common/folder_opener.h"

#include <windows.h>

#include <vector>

namespace pdfimg {

std::wstring BuildExplorerCommandLine(const std::wstring& directory) {
  if (directory.empty() || directory.find(L'"') != std::wstring::npos) return {};
  return L"explorer.exe /e,\"" + directory + L"\"";
}

bool OpenFolderExact(const std::wstring& directory) {
  const DWORD attributes = GetFileAttributesW(directory.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) return false;

  std::vector<wchar_t> windows_directory(MAX_PATH + 1, L'\0');
  const UINT length =
      GetWindowsDirectoryW(windows_directory.data(), static_cast<UINT>(windows_directory.size()));
  if (length == 0 || length >= windows_directory.size()) return false;
  const std::wstring explorer_path =
      std::wstring(windows_directory.data(), length) + L"\\explorer.exe";
  std::wstring command = BuildExplorerCommandLine(directory);
  if (command.empty()) return false;

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  const BOOL created = CreateProcessW(explorer_path.c_str(), &command[0], nullptr, nullptr, FALSE,
                                      CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr, &startup,
                                      &process);
  if (!created) return false;
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return true;
}

}  // namespace pdfimg

