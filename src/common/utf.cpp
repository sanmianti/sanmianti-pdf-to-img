#include "common/utf.h"

#include <windows.h>

#include <stdexcept>

namespace pdfimg {
namespace {

template <typename InputChar, typename OutputChar, typename Convert>
std::basic_string<OutputChar> ConvertString(const std::basic_string<InputChar>& input,
                                            Convert convert) {
  if (input.empty()) return {};
  const int size = convert(nullptr, 0);
  if (size <= 0) throw std::runtime_error("character conversion failed");
  std::basic_string<OutputChar> output(static_cast<size_t>(size), OutputChar{});
  if (convert(&output[0], size) != size) {
    throw std::runtime_error("character conversion failed");
  }
  return output;
}

}  // namespace

std::string WideToUtf8(const std::wstring& value) {
  return ConvertString<wchar_t, char>(value, [&](char* output, int size) {
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                               static_cast<int>(value.size()), output, size, nullptr, nullptr);
  });
}

std::wstring Utf8ToWide(const std::string& value) {
  return ConvertString<char, wchar_t>(value, [&](wchar_t* output, int size) {
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                               static_cast<int>(value.size()), output, size);
  });
}

}  // namespace pdfimg

