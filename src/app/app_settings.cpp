#include "app/app_settings.h"

#include <windows.h>
#include <shlobj.h>

#include <iterator>

namespace pdfimg {
namespace {

std::wstring SettingsPath() {
  wchar_t local_app_data[MAX_PATH]{};
  if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, nullptr,
                              SHGFP_TYPE_CURRENT, local_app_data))) {
    return L"PdfToImage.ini";
  }
  const std::wstring directory = std::wstring(local_app_data) + L"\\PdfToImage";
  SHCreateDirectoryExW(nullptr, directory.c_str(), nullptr);
  return directory + L"\\settings.ini";
}

}  // namespace

std::wstring DefaultOutputDirectory() {
  wchar_t profile[32768]{};
  const DWORD length = GetEnvironmentVariableW(L"USERPROFILE", profile, std::size(profile));
  if (length > 0 && length < std::size(profile)) {
    const std::wstring downloads = std::wstring(profile) + L"\\Downloads";
    const DWORD attributes = GetFileAttributesW(downloads.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
      return downloads;
    }
  }
  wchar_t documents[MAX_PATH]{};
  if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT,
                                 documents))) {
    return documents;
  }
  return L".";
}

AppSettings LoadAppSettings() {
  AppSettings settings;
  const std::wstring path = SettingsPath();
  wchar_t output[32768]{};
  GetPrivateProfileStringW(L"settings", L"output_directory", L"", output,
                           static_cast<DWORD>(std::size(output)), path.c_str());
  settings.output_directory = output[0] ? output : DefaultOutputDirectory();

  wchar_t language[16]{};
  GetPrivateProfileStringW(L"settings", L"language", L"zh-CN", language,
                           static_cast<DWORD>(std::size(language)), path.c_str());
  settings.language = _wcsicmp(language, L"en-US") == 0 ? InterfaceLanguage::kEnglish
                                                          : InterfaceLanguage::kChinese;
  return settings;
}

bool SaveAppSettings(const AppSettings& settings) {
  const std::wstring path = SettingsPath();
  const wchar_t* language =
      settings.language == InterfaceLanguage::kEnglish ? L"en-US" : L"zh-CN";
  return WritePrivateProfileStringW(L"settings", L"output_directory",
                                    settings.output_directory.c_str(), path.c_str()) &&
         WritePrivateProfileStringW(L"settings", L"language", language, path.c_str());
}

}  // namespace pdfimg
