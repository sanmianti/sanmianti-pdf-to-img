#include "common/output_path.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <sstream>

namespace pdfimg {
namespace {

constexpr wchar_t kTemporaryMarker[] = L".tmp-";

std::wstring ParentDirectory(const std::wstring& path) {
  const size_t separator = path.find_last_of(L"\\/");
  if (separator == std::wstring::npos) return L".";
  if (separator == 2 && path.size() >= 3 && path[1] == L':') return path.substr(0, 3);
  return path.substr(0, separator);
}

bool Exists(const std::wstring& path) {
  return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::wstring LastComponent(const std::wstring& path) {
  const size_t separator = path.find_last_of(L"\\/");
  return separator == std::wstring::npos ? path : path.substr(separator + 1);
}

bool DeleteTree(const std::wstring& directory) {
  WIN32_FIND_DATAW data{};
  const std::wstring pattern = directory + L"\\*";
  HANDLE find = FindFirstFileW(pattern.c_str(), &data);
  if (find != INVALID_HANDLE_VALUE) {
    bool ok = true;
    do {
      if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) continue;
      const std::wstring child = directory + L"\\" + data.cFileName;
      if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        ok = DeleteTree(child) && ok;
      } else {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_READONLY) {
          SetFileAttributesW(child.c_str(), data.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
        }
        ok = (DeleteFileW(child.c_str()) != FALSE) && ok;
      }
    } while (FindNextFileW(find, &data));
    FindClose(find);
    if (!ok) return false;
  } else if (GetLastError() != ERROR_FILE_NOT_FOUND) {
    return false;
  }
  return RemoveDirectoryW(directory.c_str()) != FALSE || GetLastError() == ERROR_PATH_NOT_FOUND;
}

}  // namespace

bool IsRegularPdf(const std::wstring& path) {
  const DWORD attributes = GetFileAttributesW(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
  const size_t separator = path.find_last_of(L"\\/");
  const size_t dot = path.find_last_of(L'.');
  if (dot == std::wstring::npos || (separator != std::wstring::npos && dot < separator)) return false;
  std::wstring extension = path.substr(dot);
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
  return extension == L".pdf";
}

std::wstring FileNameFromPath(const std::wstring& path) { return LastComponent(path); }

std::wstring BaseOutputDirectory(const std::wstring& pdf_path) {
  const std::wstring parent = ParentDirectory(pdf_path);
  return BaseOutputDirectory(pdf_path, parent);
}

std::wstring BaseOutputDirectory(const std::wstring& pdf_path, const std::wstring& output_root) {
  std::wstring name = LastComponent(pdf_path);
  const size_t dot = name.find_last_of(L'.');
  if (dot != std::wstring::npos) name.resize(dot);
  std::wstring root = output_root;
  while (root.size() > 3 && (root.back() == L'\\' || root.back() == L'/')) root.pop_back();
  return root + L"\\" + name + L"_图片";
}

bool PrepareOutputPlan(const std::wstring& pdf_path, OutputPlan* plan, std::wstring* error) {
  return PrepareOutputPlan(pdf_path, ParentDirectory(pdf_path), plan, error);
}

bool PrepareOutputPlan(const std::wstring& pdf_path, const std::wstring& output_root,
                       OutputPlan* plan, std::wstring* error) {
  if (!plan) return false;
  const DWORD root_attributes = GetFileAttributesW(output_root.c_str());
  if (root_attributes == INVALID_FILE_ATTRIBUTES ||
      !(root_attributes & FILE_ATTRIBUTE_DIRECTORY)) {
    if (error) *error = L"输出目录不存在";
    return false;
  }
  OutputPlan candidate;
  const std::wstring base = BaseOutputDirectory(pdf_path, output_root);
  candidate.final_directory = base;
  for (unsigned suffix = 2; Exists(candidate.final_directory); ++suffix) {
    if (suffix > 100000) {
      if (error) *error = L"无法分配输出目录";
      return false;
    }
    candidate.final_directory = base + L" (" + std::to_wstring(suffix) + L")";
  }

  const DWORD pid = GetCurrentProcessId();
  const ULONGLONG seed = GetTickCount64();
  for (unsigned attempt = 0; attempt < 128; ++attempt) {
    std::wostringstream stream;
    stream << base << kTemporaryMarker << pid << L"-" << std::hex << (seed + attempt);
    candidate.temporary_directory = stream.str();
    if (CreateDirectoryW(candidate.temporary_directory.c_str(), nullptr)) {
      *plan = candidate;
      return true;
    }
    const DWORD code = GetLastError();
    if (code != ERROR_ALREADY_EXISTS && code != ERROR_FILE_EXISTS) {
      if (error) *error = L"无法创建临时输出目录";
      return false;
    }
  }
  if (error) *error = L"无法分配临时输出目录";
  return false;
}

bool CommitOutputPlan(const OutputPlan& plan, std::wstring* error) {
  if (plan.temporary_directory.empty() || plan.final_directory.empty() ||
      plan.temporary_directory.find(kTemporaryMarker) == std::wstring::npos ||
      ParentDirectory(plan.temporary_directory) != ParentDirectory(plan.final_directory)) {
    if (error) *error = L"输出路径校验失败";
    return false;
  }
  if (Exists(plan.final_directory)) {
    if (error) *error = L"目标目录已存在";
    return false;
  }
  if (!MoveFileExW(plan.temporary_directory.c_str(), plan.final_directory.c_str(),
                   MOVEFILE_WRITE_THROUGH)) {
    if (error) *error = L"无法完成输出目录";
    return false;
  }
  return true;
}

bool CleanupTemporaryDirectory(const std::wstring& temporary_directory) {
  if (temporary_directory.empty() || temporary_directory.find(kTemporaryMarker) == std::wstring::npos) {
    return false;
  }
  const std::wstring leaf = LastComponent(temporary_directory);
  if (leaf.find(kTemporaryMarker) == std::wstring::npos || leaf == L"." || leaf == L"..") return false;
  return DeleteTree(temporary_directory);
}

}  // namespace pdfimg
