#pragma once

#include <string>

namespace pdfimg {

enum class WorkerExit : int {
  kSuccess = 0,
  kBadArguments = 10,
  kPdfOpenFailed = 20,
  kPdfDamaged = 21,
  kPasswordRequired = 22,
  kSecurityUnsupported = 23,
  kRenderFailed = 30,
  kPngWriteFailed = 31,
  kPageTooLarge = 32,
  kImageReadFailed = 33,
  kPdfWriteFailed = 34,
  kCancelled = 40,
  kUnknown = 50,
};

inline const wchar_t* UserMessageForToken(const std::string& token) {
  if (token == "NOT_PDF") return L"请选择 PDF 文件";
  if (token == "FILE_NOT_FOUND") return L"文件不存在或已被移动";
  if (token == "CANNOT_READ") return L"无法读取该 PDF，请检查文件权限";
  if (token == "PDF_DAMAGED") return L"PDF 文件已损坏或格式不受支持";
  if (token == "PASSWORD_REQUIRED") return L"PDF 已加密，暂不支持转换";
  if (token == "SECURITY_UNSUPPORTED") return L"PDF 的安全方案暂不支持";
  if (token == "CANNOT_WRITE") return L"无法将转换结果写入所选保存位置";
  if (token == "DISK_FULL") return L"磁盘空间不足，无法继续转换";
  if (token == "PAGE_TOO_LARGE") return L"PDF 页面尺寸过大，无法生成图片";
  if (token == "MULTIPLE_FILES") return L"一次只能转换一个 PDF 文件";
  if (token == "MIXED_FILES") return L"PDF 和图片不能同时转换";
  if (token == "UNSUPPORTED_FILE") return L"请选择 PDF 或支持的图片文件";
  if (token == "IMAGE_READ_FAILED") return L"其中一张图片无法读取或已经损坏";
  if (token == "IMAGE_TOO_LARGE") return L"其中一张图片尺寸过大";
  if (token == "PDF_CREATE_FAILED") return L"无法生成 PDF 文件";
  if (token == "BUSY") return L"正在转换，请稍候";
  return L"转换失败，请更换 PDF 后重试";
}

}  // namespace pdfimg
