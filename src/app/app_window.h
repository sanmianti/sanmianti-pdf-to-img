#pragma once

#include "app/app_settings.h"
#include "app/worker_client.h"
#include "common/output_path.h"

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>

#include <string>
#include <vector>

namespace pdfimg {

class AppWindow {
 public:
  AppWindow() = default;
  ~AppWindow();
  bool Create(HINSTANCE instance, int show_command);
  HWND hwnd() const { return hwnd_; }

 private:
  enum class State { kIdle, kConverting, kSuccess, kError };
  enum class View { kMain, kSettings, kImageOrder };
  enum class TaskKind { kPdfToImages, kImagesToPdf };

  static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
  LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);
  bool CreateGraphicsResources();
  void DiscardGraphicsResources();
  void Paint();
  void ChooseFiles();
  void HandleFiles(std::vector<std::wstring> files);
  void ChooseOutputDirectory();
  void OpenFeedback();
  void SetLanguage(InterfaceLanguage language);
  void EnsureSettingsHeight();
  void StartPdfConversion(const std::wstring& path);
  void StartImageConversion();
  void MoveSelectedImage(int direction);
  void SortPendingImages(bool by_time);
  void SetError(const std::string& token);
  void FinishWorker(DWORD exit_code);
  void OpenResult();
  D2D1_RECT_F PrimaryActionRect() const;
  D2D1_RECT_F SecondaryActionRect() const;
  D2D1_RECT_F SettingsButtonRect(float width) const;
  D2D1_RECT_F BackButtonRect() const;
  D2D1_RECT_F OutputButtonRect(float width) const;
  D2D1_RECT_F LanguageButtonRect(float width) const;
  D2D1_RECT_F LanguageOptionRect(float width, size_t index) const;
  D2D1_RECT_F FeedbackRect(float height) const;
  D2D1_RECT_F OrderUpRect(float width) const;
  D2D1_RECT_F OrderDownRect(float width) const;
  D2D1_RECT_F OrderStartRect(float width, float height) const;
  D2D1_RECT_F OrderScrollbarRect(float width) const;
  D2D1_RECT_F OrderSortNameRect(float width) const;
  D2D1_RECT_F OrderSortTimeRect(float width) const;
  std::wstring WorkerPath() const;
  const wchar_t* Text(const wchar_t* chinese, const wchar_t* english) const;

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

  View view_ = View::kMain;
  State state_ = State::kIdle;
  TaskKind task_kind_ = TaskKind::kPdfToImages;
  AppSettings settings_;
  WorkerClient worker_;
  OutputPlan output_plan_;
  FileOutputPlan pdf_output_plan_;
  std::vector<std::wstring> pending_images_;
  size_t selected_image_ = 0;
  size_t image_list_offset_ = 0;
  bool dragging_image_ = false;
  bool dragging_scrollbar_ = false;
  bool language_dropdown_open_ = false;
  std::wstring input_file_name_;
  std::wstring current_output_file_;
  std::wstring completed_output_directory_;
  std::wstring completed_output_file_;
  std::wstring error_message_;
  std::string worker_error_token_;
  int current_page_ = 0;
  int total_pages_ = 0;
  bool worker_reported_done_ = false;
};

}  // namespace pdfimg
