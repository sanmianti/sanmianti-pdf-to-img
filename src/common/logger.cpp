#include "common/logger.h"

#include <windows.h>
#include <shlobj.h>

#include <cstdio>
#include <mutex>
#include <string>

namespace pdfimg {
namespace {

constexpr DWORD kMaximumLogBytes = 256 * 1024;
std::mutex g_log_mutex;
std::wstring g_log_directory;

std::wstring LogPath(int index) {
  return g_log_directory + L"\\app." + std::to_wstring(index) + L".log";
}

void RotateIfNeeded(DWORD incoming) {
  WIN32_FILE_ATTRIBUTE_DATA data{};
  const std::wstring current = LogPath(0);
  if (!GetFileAttributesExW(current.c_str(), GetFileExInfoStandard, &data)) return;
  const unsigned long long size =
      (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
  if (size + incoming <= kMaximumLogBytes) return;
  DeleteFileW(LogPath(2).c_str());
  MoveFileExW(LogPath(1).c_str(), LogPath(2).c_str(), MOVEFILE_REPLACE_EXISTING);
  MoveFileExW(LogPath(0).c_str(), LogPath(1).c_str(), MOVEFILE_REPLACE_EXISTING);
}

}  // namespace

void InitializeLog() {
  std::lock_guard<std::mutex> lock(g_log_mutex);
  wchar_t local_app_data[MAX_PATH]{};
  if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, nullptr,
                              SHGFP_TYPE_CURRENT, local_app_data))) {
    return;
  }
  const std::wstring app_directory = std::wstring(local_app_data) + L"\\PdfToImage";
  g_log_directory = app_directory + L"\\logs";
  CreateDirectoryW(app_directory.c_str(), nullptr);
  CreateDirectoryW(g_log_directory.c_str(), nullptr);
}

void LogEvent(const char* stage, const char* code, int page) {
  if (!stage || !code) return;
  std::lock_guard<std::mutex> lock(g_log_mutex);
  if (g_log_directory.empty()) return;
  SYSTEMTIME time{};
  GetLocalTime(&time);
  char line[256]{};
  const int length = std::snprintf(
      line, sizeof(line), "%04u-%02u-%02uT%02u:%02u:%02u version=1.0.0 stage=%s code=%s page=%d\r\n",
      time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, stage, code,
      page);
  if (length <= 0 || length >= static_cast<int>(sizeof(line))) return;
  RotateIfNeeded(static_cast<DWORD>(length));
  HANDLE file = CreateFileW(LogPath(0).c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return;
  DWORD written = 0;
  WriteFile(file, line, static_cast<DWORD>(length), &written, nullptr);
  CloseHandle(file);
}

}  // namespace pdfimg

