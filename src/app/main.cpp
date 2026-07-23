#include "app/app_window.h"
#include "common/logger.h"

#include <windows.h>

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, wchar_t*, int show_command) {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
  SetProcessDPIAware();
  const HRESULT com_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(com_result)) return 1;
  pdfimg::InitializeLog();
  pdfimg::LogEvent("APP_START");

  pdfimg::AppWindow window;
  if (!window.Create(instance, show_command)) {
    MessageBoxW(nullptr, L"无法启动 PDF 图片转换。", L"PDF 图片转换", MB_OK | MB_ICONERROR);
    CoUninitialize();
    return 1;
  }

  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  CoUninitialize();
  return static_cast<int>(message.wParam);
}
