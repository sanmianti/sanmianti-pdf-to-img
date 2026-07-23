#pragma once

#include <string>

namespace pdfimg {

enum class InterfaceLanguage { kChinese, kEnglish };

struct AppSettings {
  std::wstring output_directory;
  InterfaceLanguage language = InterfaceLanguage::kChinese;
};

std::wstring DefaultOutputDirectory();
AppSettings LoadAppSettings();
bool SaveAppSettings(const AppSettings& settings);

}  // namespace pdfimg
