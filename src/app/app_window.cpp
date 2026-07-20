#include "app/app_window.h"

#include "app/drop_handler.h"
#include "common/error_code.h"
#include "common/folder_opener.h"
#include "common/logger.h"
#include "common/utf.h"

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <d2d1helper.h>
#include <shellapi.h>

#include <algorithm>
#include <memory>

namespace pdfimg {
namespace {

constexpr wchar_t kWindowClass[] = L"PdfToImageWindowClass";

template <typename T>
void Release(T*& value) {
  if (value) {
    value->Release();
    value = nullptr;
  }
}

bool PointIn(const D2D1_RECT_F& rectangle, LPARAM lparam, float dpi_scale) {
  const float x = static_cast<float>(GET_X_LPARAM(lparam)) / dpi_scale;
  const float y = static_cast<float>(GET_Y_LPARAM(lparam)) / dpi_scale;
  return x >= rectangle.left && x < rectangle.right && y >= rectangle.top &&
         y < rectangle.bottom;
}

bool HasPdfExtension(const std::wstring& path) {
  if (path.size() < 4) return false;
  std::wstring extension = path.substr(path.size() - 4);
  std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);
  return extension == L".pdf";
}

std::string ExitCodeToken(DWORD code) {
  switch (static_cast<WorkerExit>(code)) {
    case WorkerExit::kPdfOpenFailed:
      return "CANNOT_READ";
    case WorkerExit::kPdfDamaged:
      return "PDF_DAMAGED";
    case WorkerExit::kPasswordRequired:
      return "PASSWORD_REQUIRED";
    case WorkerExit::kSecurityUnsupported:
      return "SECURITY_UNSUPPORTED";
    case WorkerExit::kPngWriteFailed:
      return "CANNOT_WRITE";
    case WorkerExit::kPageTooLarge:
      return "PAGE_TOO_LARGE";
    default:
      return "WORKER_CRASHED";
  }
}

}  // namespace

AppWindow::~AppWindow() {
  worker_.Cancel();
  DiscardGraphicsResources();
  Release(small_format_);
  Release(body_format_);
  Release(heading_format_);
  Release(title_format_);
  Release(write_factory_);
  Release(d2d_factory_);
}

bool AppWindow::Create(HINSTANCE instance, int show_command) {
  instance_ = instance;
  if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d_factory_)) ||
      FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                 reinterpret_cast<IUnknown**>(&write_factory_)))) {
    return false;
  }
  float dpi_x = 96.0f;
  float dpi_y = 96.0f;
  d2d_factory_->GetDesktopDpi(&dpi_x, &dpi_y);
  dpi_scale_ = dpi_x > 0.0f ? dpi_x / 96.0f : 1.0f;

  auto create_format = [&](float size, DWRITE_FONT_WEIGHT weight, IDWriteTextFormat** format) {
    HRESULT result = write_factory_->CreateTextFormat(
        L"Microsoft YaHei", nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, size, L"zh-CN", format);
    if (SUCCEEDED(result)) {
      (*format)->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
      (*format)->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    return result;
  };
  if (FAILED(create_format(28.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &title_format_)) ||
      FAILED(create_format(19.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &heading_format_)) ||
      FAILED(create_format(16.0f, DWRITE_FONT_WEIGHT_NORMAL, &body_format_)) ||
      FAILED(create_format(13.0f, DWRITE_FONT_WEIGHT_NORMAL, &small_format_))) {
    return false;
  }

  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  window_class.style = CS_HREDRAW | CS_VREDRAW;
  window_class.lpfnWndProc = WindowProc;
  window_class.hInstance = instance;
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
  if (!window_class.hIcon) window_class.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  window_class.hIconSm = window_class.hIcon;
  window_class.hbrBackground = nullptr;
  window_class.lpszClassName = kWindowClass;
  if (!RegisterClassExW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

  hwnd_ = CreateWindowExW(0, kWindowClass, L"PDF 转图片", WS_OVERLAPPEDWINDOW,
                          CW_USEDEFAULT, CW_USEDEFAULT,
                          static_cast<int>(600.0f * dpi_scale_ + 0.5f),
                          static_cast<int>(380.0f * dpi_scale_ + 0.5f), nullptr, nullptr, instance,
                          this);
  if (!hwnd_) return false;
  DragAcceptFiles(hwnd_, TRUE);
  ShowWindow(hwnd_, show_command);
  UpdateWindow(hwnd_);
  return true;
}

LRESULT CALLBACK AppWindow::WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  AppWindow* self = reinterpret_cast<AppWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    self = static_cast<AppWindow*>(create->lpCreateParams);
    self->hwnd_ = window;
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  return self ? self->HandleMessage(message, wparam, lparam)
              : DefWindowProcW(window, message, wparam, lparam);
}

LRESULT AppWindow::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_PAINT:
      Paint();
      return 0;
    case WM_SIZE:
      if (render_target_) render_target_->Resize(D2D1::SizeU(LOWORD(lparam), HIWORD(lparam)));
      InvalidateRect(hwnd_, nullptr, FALSE);
      return 0;
    case WM_GETMINMAXINFO: {
      auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
      info->ptMinTrackSize.x = static_cast<LONG>(520.0f * dpi_scale_ + 0.5f);
      info->ptMinTrackSize.y = static_cast<LONG>(340.0f * dpi_scale_ + 0.5f);
      return 0;
    }
    case WM_LBUTTONUP:
      if (state_ == State::kConverting) return 0;
      if (state_ == State::kSuccess && PointIn(PrimaryActionRect(), lparam, dpi_scale_)) {
        OpenOutputDirectory();
      } else {
        ChoosePdf();
      }
      return 0;
    case WM_DROPFILES: {
      auto files = ReadDroppedFiles(reinterpret_cast<HDROP>(wparam));
      if (state_ == State::kConverting) {
        MessageBoxW(hwnd_, UserMessageForToken("BUSY"), L"PDF 转图片", MB_OK | MB_ICONINFORMATION);
      } else if (files.size() != 1) {
        SetError("MULTIPLE_FILES");
      } else {
        StartConversion(files.front());
      }
      return 0;
    }
    case kWorkerNotificationMessage: {
      std::unique_ptr<WorkerNotification> notification(
          reinterpret_cast<WorkerNotification*>(lparam));
      if (notification->process_exited) {
        FinishWorker(notification->exit_code);
      } else {
        const WorkerEvent& event = notification->event;
        if (event.type == WorkerEventType::kStart) {
          total_pages_ = event.total;
        } else if (event.type == WorkerEventType::kProgress) {
          current_page_ = event.current;
          total_pages_ = event.total;
          LogEvent("PAGE_COMPLETE", "OK", current_page_);
          try {
            current_output_file_ = Utf8ToWide(event.value);
          } catch (...) {
            current_output_file_.clear();
          }
        } else if (event.type == WorkerEventType::kDone) {
          worker_reported_done_ = true;
          total_pages_ = event.total;
        } else if (event.type == WorkerEventType::kError) {
          worker_error_token_ = event.value;
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
      return 0;
    }
    case WM_CLOSE:
      if (state_ == State::kConverting) LogEvent("TASK_CANCELLED", "USER_CLOSE", current_page_);
      worker_.Cancel();
      if (!output_plan_.temporary_directory.empty()) {
        CleanupTemporaryDirectory(output_plan_.temporary_directory);
        output_plan_ = {};
      }
      DestroyWindow(hwnd_);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(hwnd_, message, wparam, lparam);
  }
}

bool AppWindow::CreateGraphicsResources() {
  if (render_target_) return true;
  RECT client{};
  GetClientRect(hwnd_, &client);
  const D2D1_SIZE_U size =
      D2D1::SizeU(static_cast<UINT32>(client.right), static_cast<UINT32>(client.bottom));
  D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties();
  properties.dpiX = 96.0f * dpi_scale_;
  properties.dpiY = 96.0f * dpi_scale_;
  if (FAILED(d2d_factory_->CreateHwndRenderTarget(
          properties, D2D1::HwndRenderTargetProperties(hwnd_, size), &render_target_))) {
    return false;
  }
  return SUCCEEDED(render_target_->CreateSolidColorBrush(D2D1::ColorF(0x202A3A), &brush_));
}

void AppWindow::DiscardGraphicsResources() {
  Release(brush_);
  Release(render_target_);
}

D2D1_RECT_F AppWindow::PrimaryActionRect() const {
  RECT client{};
  GetClientRect(hwnd_, &client);
  const float client_width = static_cast<float>(client.right) / dpi_scale_;
  const float client_height = static_cast<float>(client.bottom) / dpi_scale_;
  if (state_ == State::kSuccess) {
    const float width = (std::min)(260.0f, client_width - 80.0f);
    const float left = (client_width - width) / 2.0f;
    return D2D1::RectF(left, 225.0f, left + width, 274.0f);
  }
  return D2D1::RectF(70.0f, 142.0f, client_width - 70.0f, client_height - 50.0f);
}

void AppWindow::Paint() {
  PAINTSTRUCT paint{};
  BeginPaint(hwnd_, &paint);
  if (!CreateGraphicsResources()) {
    EndPaint(hwnd_, &paint);
    return;
  }

  RECT client{};
  GetClientRect(hwnd_, &client);
  const float width = static_cast<float>(client.right) / dpi_scale_;
  const float height = static_cast<float>(client.bottom) / dpi_scale_;
  render_target_->BeginDraw();
  render_target_->Clear(D2D1::ColorF(0xF7F9FC));

  auto draw_text = [&](const std::wstring& text, IDWriteTextFormat* format, const D2D1_RECT_F& rect,
                       D2D1_COLOR_F color) {
    brush_->SetColor(color);
    render_target_->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), format, rect, brush_,
                              D2D1_DRAW_TEXT_OPTIONS_CLIP);
  };

  if (state_ == State::kIdle) {
    draw_text(L"PDF 转图片", title_format_, D2D1::RectF(20, 42, width - 20, 88),
              D2D1::ColorF(0x172033));
    draw_text(L"拖入 PDF，自动按页生成图片", small_format_, D2D1::RectF(20, 91, width - 20, 122),
              D2D1::ColorF(0x687386));
    const D2D1_RECT_F action = PrimaryActionRect();
    brush_->SetColor(D2D1::ColorF(0x3478F6));
    render_target_->DrawRoundedRectangle(D2D1::RoundedRect(action, 10, 10), brush_, 2.0f);
    draw_text(L"将 PDF 拖到这里\n或单击选择文件", heading_format_, action,
              D2D1::ColorF(0x285FCA));
    draw_text(L"图片将保存到 PDF 文件所在目录", small_format_,
              D2D1::RectF(20, height - 44, width - 20, height - 16), D2D1::ColorF(0x7D8798));
  } else if (state_ == State::kConverting) {
    draw_text(L"正在转换：" + input_file_name_, heading_format_, D2D1::RectF(30, 62, width - 30, 112),
              D2D1::ColorF(0x172033));
    const float left = 65.0f;
    const float right = width - 65.0f;
    const D2D1_RECT_F track = D2D1::RectF(left, 160, right, 178);
    brush_->SetColor(D2D1::ColorF(0xDEE5EF));
    render_target_->FillRoundedRectangle(D2D1::RoundedRect(track, 9, 9), brush_);
    const float ratio = total_pages_ > 0
                            ? (std::min)(1.0f, static_cast<float>(current_page_) / total_pages_)
                            : 0.0f;
    if (ratio > 0.0f) {
      const D2D1_RECT_F fill = D2D1::RectF(left, 160, left + (right - left) * ratio, 178);
      brush_->SetColor(D2D1::ColorF(0x3478F6));
      render_target_->FillRoundedRectangle(D2D1::RoundedRect(fill, 9, 9), brush_);
    }
    const std::wstring count = total_pages_ > 0
                                   ? std::to_wstring(current_page_) + L" / " +
                                         std::to_wstring(total_pages_) + L" 页"
                                   : L"正在读取 PDF…";
    draw_text(count, body_format_, D2D1::RectF(20, 190, width - 20, 226), D2D1::ColorF(0x354052));
    const std::wstring detail = current_output_file_.empty()
                                    ? L"正在准备图片"
                                    : L"正在生成 " + current_output_file_;
    draw_text(detail, small_format_, D2D1::RectF(20, 230, width - 20, 268), D2D1::ColorF(0x7D8798));
  } else if (state_ == State::kSuccess) {
    draw_text(L"✓  转换完成", title_format_, D2D1::RectF(20, 65, width - 20, 118),
              D2D1::ColorF(0x168A55));
    draw_text(L"已生成 " + std::to_wstring(total_pages_) + L" 张 PNG 图片", body_format_,
              D2D1::RectF(20, 126, width - 20, 172), D2D1::ColorF(0x4D596B));
    const D2D1_RECT_F button = PrimaryActionRect();
    brush_->SetColor(D2D1::ColorF(0x3478F6));
    render_target_->FillRoundedRectangle(D2D1::RoundedRect(button, 7, 7), brush_);
    draw_text(L"打开图片文件夹", body_format_, button, D2D1::ColorF(0xFFFFFF));
    draw_text(L"可继续拖入新的 PDF", small_format_, D2D1::RectF(20, 290, width - 20, 326),
              D2D1::ColorF(0x7D8798));
  } else {
    draw_text(L"未能完成转换", title_format_, D2D1::RectF(20, 55, width - 20, 108),
              D2D1::ColorF(0xC04444));
    draw_text(error_message_, body_format_, D2D1::RectF(35, 116, width - 35, 177),
              D2D1::ColorF(0x4D596B));
    const D2D1_RECT_F action = PrimaryActionRect();
    brush_->SetColor(D2D1::ColorF(0x3478F6));
    render_target_->DrawRoundedRectangle(D2D1::RoundedRect(action, 10, 10), brush_, 2.0f);
    draw_text(L"重新选择 PDF", heading_format_, action, D2D1::ColorF(0x285FCA));
  }

  const HRESULT result = render_target_->EndDraw();
  if (result == D2DERR_RECREATE_TARGET) DiscardGraphicsResources();
  EndPaint(hwnd_, &paint);
}

void AppWindow::ChoosePdf() {
  wchar_t path[32768]{};
  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = hwnd_;
  dialog.lpstrFilter = L"PDF 文件 (*.pdf)\0*.pdf\0所有文件 (*.*)\0*.*\0\0";
  dialog.lpstrFile = path;
  dialog.nMaxFile = static_cast<DWORD>(std::size(path));
  dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  dialog.lpstrDefExt = L"pdf";
  if (GetOpenFileNameW(&dialog)) StartConversion(path);
}

void AppWindow::StartConversion(const std::wstring& path) {
  if (state_ == State::kConverting) {
    MessageBoxW(hwnd_, UserMessageForToken("BUSY"), L"PDF 转图片", MB_OK | MB_ICONINFORMATION);
    return;
  }
  const DWORD attributes = GetFileAttributesW(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    SetError("FILE_NOT_FOUND");
    return;
  }
  if (!HasPdfExtension(path) || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
    SetError("NOT_PDF");
    return;
  }

  LogEvent("TASK_PREPARE");
  OutputPlan plan;
  std::wstring prepare_error;
  if (!PrepareOutputPlan(path, &plan, &prepare_error)) {
    SetError("CANNOT_WRITE");
    return;
  }

  std::wstring start_error;
  if (!worker_.Start(hwnd_, WorkerPath(), path, plan.temporary_directory, &start_error)) {
    CleanupTemporaryDirectory(plan.temporary_directory);
    SetError("WORKER_CRASHED");
    return;
  }

  output_plan_ = plan;
  input_file_name_ = FileNameFromPath(path);
  current_output_file_.clear();
  worker_error_token_.clear();
  current_page_ = 0;
  total_pages_ = 0;
  worker_reported_done_ = false;
  state_ = State::kConverting;
  LogEvent("WORKER_STARTED");
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void AppWindow::SetError(const std::string& token) {
  error_message_ = UserMessageForToken(token);
  state_ = State::kError;
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void AppWindow::FinishWorker(DWORD exit_code) {
  if (state_ != State::kConverting) return;
  const bool succeeded = exit_code == 0 && worker_reported_done_ && total_pages_ > 0 &&
                         current_page_ == total_pages_;
  worker_.Cancel();
  if (succeeded) {
    std::wstring commit_error;
    if (CommitOutputPlan(output_plan_, &commit_error)) {
      completed_output_directory_ = output_plan_.final_directory;
      output_plan_ = {};
      state_ = State::kSuccess;
      LogEvent("TASK_COMPLETE", "OK", total_pages_);
    } else {
      CleanupTemporaryDirectory(output_plan_.temporary_directory);
      output_plan_ = {};
      SetError("CANNOT_WRITE");
    }
  } else {
    CleanupTemporaryDirectory(output_plan_.temporary_directory);
    output_plan_ = {};
    const std::string token = worker_error_token_.empty() ? ExitCodeToken(exit_code) : worker_error_token_;
    LogEvent("TASK_FAILED", token.c_str(), current_page_);
    SetError(token);
  }
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void AppWindow::OpenOutputDirectory() {
  if (!OpenFolderExact(completed_output_directory_)) {
    MessageBoxW(hwnd_, L"无法打开图片文件夹，请从 PDF 所在目录手动打开。", L"PDF 转图片",
                MB_OK | MB_ICONWARNING);
    LogEvent("OPEN_OUTPUT_FAILED", "SHELL_ERROR");
    return;
  }
  LogEvent("OPEN_OUTPUT_COMPLETE");
}

std::wstring AppWindow::WorkerPath() const {
  std::wstring path(32768, L'\0');
  const DWORD length = GetModuleFileNameW(nullptr, &path[0], static_cast<DWORD>(path.size()));
  path.resize(length);
  const size_t separator = path.find_last_of(L"\\/");
  return (separator == std::wstring::npos ? L"" : path.substr(0, separator + 1)) + L"PdfWorker.exe";
}

}  // namespace pdfimg
