#pragma once

#include "app/worker_client.h"
#include "common/output_path.h"

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>

#include <string>

namespace pdfimg {

class AppWindow {
 public:
  AppWindow() = default;
  ~AppWindow();
  bool Create(HINSTANCE instance, int show_command);
  HWND hwnd() const { return hwnd_; }

 private:
  enum class State { kIdle, kConverting, kSuccess, kError };

  static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
  LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);
  bool CreateGraphicsResources();
  void DiscardGraphicsResources();
  void Paint();
  void ChoosePdf();
  void StartConversion(const std::wstring& path);
  void SetError(const std::string& token);
  void FinishWorker(DWORD exit_code);
  void OpenOutputDirectory();
  D2D1_RECT_F PrimaryActionRect() const;
  std::wstring WorkerPath() const;

  HWND hwnd_ = nullptr;
  HINSTANCE instance_ = nullptr;
  ID2D1Factory* d2d_factory_ = nullptr;
  IDWriteFactory* write_factory_ = nullptr;
  ID2D1HwndRenderTarget* render_target_ = nullptr;
  ID2D1SolidColorBrush* brush_ = nullptr;
  IDWriteTextFormat* title_format_ = nullptr;
  IDWriteTextFormat* heading_format_ = nullptr;
  IDWriteTextFormat* body_format_ = nullptr;
  IDWriteTextFormat* small_format_ = nullptr;
  float dpi_scale_ = 1.0f;

  State state_ = State::kIdle;
  WorkerClient worker_;
  OutputPlan output_plan_;
  std::wstring input_file_name_;
  std::wstring current_output_file_;
  std::wstring completed_output_directory_;
  std::wstring error_message_;
  std::string worker_error_token_;
  int current_page_ = 0;
  int total_pages_ = 0;
  bool worker_reported_done_ = false;
};

}  // namespace pdfimg
