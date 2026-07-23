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
#include <shlobj.h>
#include <shlwapi.h>

#include <algorithm>
#include <iterator>
#include <memory>

namespace pdfimg {
namespace {

constexpr wchar_t kWindowClass[] = L"PdfToImageWindowClass";
constexpr size_t kOrderVisibleRows = 4;
constexpr float kOrderListTop = 130.0f;
constexpr float kOrderRowStep = 42.0f;
constexpr float kOrderRowHeight = 36.0f;
constexpr float kOrderListBottom =
    kOrderListTop + (kOrderVisibleRows - 1) * kOrderRowStep + kOrderRowHeight;

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

bool HasImageExtension(const std::wstring& path) {
  const size_t dot = path.find_last_of(L'.');
  if (dot == std::wstring::npos) return false;
  std::wstring extension = path.substr(dot);
  std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);
  return extension == L".jpg" || extension == L".jpeg" || extension == L".png" ||
         extension == L".bmp" || extension == L".tif" || extension == L".tiff";
}

ULONGLONG FileModifiedTime(const std::wstring& path) {
  WIN32_FILE_ATTRIBUTE_DATA data{};
  if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return 0;
  ULARGE_INTEGER value{};
  value.LowPart = data.ftLastWriteTime.dwLowDateTime;
  value.HighPart = data.ftLastWriteTime.dwHighDateTime;
  return value.QuadPart;
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
    case WorkerExit::kImageReadFailed:
      return "IMAGE_READ_FAILED";
    case WorkerExit::kPdfWriteFailed:
      return "PDF_CREATE_FAILED";
    default:
      return "WORKER_CRASHED";
  }
}

const wchar_t* EnglishMessageForToken(const std::string& token) {
  if (token == "NOT_PDF") return L"Please choose a PDF file.";
  if (token == "FILE_NOT_FOUND") return L"The file does not exist or has been moved.";
  if (token == "CANNOT_READ") return L"The PDF cannot be read. Check its permissions.";
  if (token == "PDF_DAMAGED") return L"The PDF is damaged or unsupported.";
  if (token == "PASSWORD_REQUIRED") return L"Password-protected PDFs are not supported yet.";
  if (token == "SECURITY_UNSUPPORTED") return L"This PDF security scheme is unsupported.";
  if (token == "CANNOT_WRITE") return L"The result cannot be written to the selected folder.";
  if (token == "DISK_FULL") return L"There is not enough disk space to finish the conversion.";
  if (token == "PAGE_TOO_LARGE") return L"A PDF page is too large to convert.";
  if (token == "MULTIPLE_FILES") return L"Only one PDF can be converted at a time.";
  if (token == "MIXED_FILES") return L"PDF and image files cannot be converted together.";
  if (token == "UNSUPPORTED_FILE") return L"Choose a PDF or a supported image file.";
  if (token == "IMAGE_READ_FAILED") return L"One of the images could not be read.";
  if (token == "IMAGE_TOO_LARGE") return L"One of the images is too large.";
  if (token == "PDF_CREATE_FAILED") return L"The PDF could not be created.";
  if (token == "BUSY") return L"A conversion is already running.";
  return L"Conversion failed. Please try another PDF.";
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
  settings_ = LoadAppSettings();
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

  hwnd_ = CreateWindowExW(0, kWindowClass,
                          Text(L"PDF 图片转换", L"PDF & Image Converter"), WS_OVERLAPPEDWINDOW,
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
      info->ptMinTrackSize.y = static_cast<LONG>(380.0f * dpi_scale_ + 0.5f);
      return 0;
    }
    case WM_LBUTTONDOWN:
      if (view_ == View::kImageOrder && state_ != State::kConverting) {
        RECT client{};
        GetClientRect(hwnd_, &client);
        const float width = static_cast<float>(client.right) / dpi_scale_;
        const float x = static_cast<float>(GET_X_LPARAM(lparam)) / dpi_scale_;
        const float y = static_cast<float>(GET_Y_LPARAM(lparam)) / dpi_scale_;
        if (pending_images_.size() > kOrderVisibleRows && x >= width - 56.0f &&
            x < width - 40.0f && y >= kOrderListTop && y < kOrderListBottom) {
          dragging_scrollbar_ = true;
          const float ratio = (std::max)(
              0.0f, (std::min)(1.0f, (y - kOrderListTop) /
                                          (kOrderListBottom - kOrderListTop)));
          image_list_offset_ = static_cast<size_t>(
              ratio * static_cast<float>(pending_images_.size() - kOrderVisibleRows) + 0.5f);
          selected_image_ = (std::max)(image_list_offset_,
                                       (std::min)(selected_image_, image_list_offset_ +
                                                                      kOrderVisibleRows - 1));
          SetCapture(hwnd_);
          InvalidateRect(hwnd_, nullptr, FALSE);
        } else if (x >= 42.0f && x < width - 58.0f && y >= kOrderListTop &&
                   y < kOrderListBottom) {
          const size_t row = static_cast<size_t>((y - kOrderListTop) / kOrderRowStep);
          if (image_list_offset_ + row < pending_images_.size()) {
            selected_image_ = image_list_offset_ + row;
            dragging_image_ = true;
            SetCapture(hwnd_);
            SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
            InvalidateRect(hwnd_, nullptr, FALSE);
          }
        }
      }
      return 0;
    case WM_MOUSEMOVE:
      if (dragging_scrollbar_ && view_ == View::kImageOrder &&
          pending_images_.size() > kOrderVisibleRows) {
        const float y = static_cast<float>(GET_Y_LPARAM(lparam)) / dpi_scale_;
        const float ratio = (std::max)(
            0.0f, (std::min)(1.0f, (y - kOrderListTop) /
                                        (kOrderListBottom - kOrderListTop)));
        image_list_offset_ = static_cast<size_t>(
            ratio * static_cast<float>(pending_images_.size() - kOrderVisibleRows) + 0.5f);
        selected_image_ = (std::max)(image_list_offset_,
                                     (std::min)(selected_image_, image_list_offset_ +
                                                                    kOrderVisibleRows - 1));
        InvalidateRect(hwnd_, nullptr, FALSE);
      } else if (dragging_image_ && view_ == View::kImageOrder && !pending_images_.empty()) {
        SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
        const float y = static_cast<float>(GET_Y_LPARAM(lparam)) / dpi_scale_;
        size_t target = selected_image_;
        if (y < kOrderListTop) {
          if (image_list_offset_ > 0) --image_list_offset_;
          target = image_list_offset_;
        } else if (y >= kOrderListBottom) {
          if (image_list_offset_ + kOrderVisibleRows < pending_images_.size()) {
            ++image_list_offset_;
          }
          target = (std::min)(image_list_offset_ + kOrderVisibleRows - 1,
                              pending_images_.size() - 1);
        } else {
          const size_t row = static_cast<size_t>((y - kOrderListTop) / kOrderRowStep);
          target = (std::min)(image_list_offset_ + row, pending_images_.size() - 1);
        }
        if (target != selected_image_) {
          std::wstring moved = std::move(pending_images_[selected_image_]);
          pending_images_.erase(pending_images_.begin() + selected_image_);
          pending_images_.insert(pending_images_.begin() + target, std::move(moved));
          selected_image_ = target;
          InvalidateRect(hwnd_, nullptr, FALSE);
        }
      }
      return 0;
    case WM_MOUSEWHEEL:
      if (view_ == View::kImageOrder && pending_images_.size() > kOrderVisibleRows) {
        const int delta = GET_WHEEL_DELTA_WPARAM(wparam);
        const size_t maximum = pending_images_.size() - kOrderVisibleRows;
        if (delta > 0 && image_list_offset_ > 0) {
          --image_list_offset_;
        } else if (delta < 0 && image_list_offset_ < maximum) {
          ++image_list_offset_;
        }
        selected_image_ = (std::max)(image_list_offset_,
                                     (std::min)(selected_image_, image_list_offset_ +
                                                                    kOrderVisibleRows - 1));
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
      }
      return DefWindowProcW(hwnd_, message, wparam, lparam);
    case WM_LBUTTONUP:
      if (state_ == State::kConverting) return 0;
      if (dragging_image_ || dragging_scrollbar_) {
        dragging_image_ = false;
        dragging_scrollbar_ = false;
        if (GetCapture() == hwnd_) ReleaseCapture();
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
      }
      {
        RECT client{};
        GetClientRect(hwnd_, &client);
        const float width = static_cast<float>(client.right) / dpi_scale_;
        const float height = static_cast<float>(client.bottom) / dpi_scale_;
        if (view_ == View::kImageOrder) {
          if (PointIn(BackButtonRect(), lparam, dpi_scale_)) {
            pending_images_.clear();
            view_ = View::kMain;
          } else if (PointIn(OrderUpRect(width), lparam, dpi_scale_)) {
            MoveSelectedImage(-1);
          } else if (PointIn(OrderDownRect(width), lparam, dpi_scale_)) {
            MoveSelectedImage(1);
          } else if (PointIn(OrderSortNameRect(width), lparam, dpi_scale_)) {
            SortPendingImages(false);
          } else if (PointIn(OrderSortTimeRect(width), lparam, dpi_scale_)) {
            SortPendingImages(true);
          } else if (PointIn(OrderStartRect(width, height), lparam, dpi_scale_)) {
            StartImageConversion();
          } else {
            const float y = static_cast<float>(GET_Y_LPARAM(lparam)) / dpi_scale_;
            if (y >= kOrderListTop && y < kOrderListBottom) {
              const size_t row =
                  static_cast<size_t>((y - kOrderListTop) / kOrderRowStep);
              if (image_list_offset_ + row < pending_images_.size()) {
                selected_image_ = image_list_offset_ + row;
              }
            }
          }
          InvalidateRect(hwnd_, nullptr, FALSE);
        } else if (view_ == View::kSettings) {
          if (PointIn(BackButtonRect(), lparam, dpi_scale_)) {
            language_dropdown_open_ = false;
            view_ = View::kMain;
          } else if (language_dropdown_open_) {
            if (PointIn(LanguageOptionRect(width, 0), lparam, dpi_scale_)) {
              SetLanguage(InterfaceLanguage::kChinese);
            } else if (PointIn(LanguageOptionRect(width, 1), lparam, dpi_scale_)) {
              SetLanguage(InterfaceLanguage::kEnglish);
            } else {
              language_dropdown_open_ = false;
            }
          } else if (PointIn(OutputButtonRect(width), lparam, dpi_scale_)) {
            ChooseOutputDirectory();
          } else if (PointIn(LanguageButtonRect(width), lparam, dpi_scale_)) {
            language_dropdown_open_ = true;
          } else if (PointIn(FeedbackRect(height), lparam, dpi_scale_)) {
            OpenFeedback();
          }
          InvalidateRect(hwnd_, nullptr, FALSE);
        } else if (PointIn(SettingsButtonRect(width), lparam, dpi_scale_)) {
          view_ = View::kSettings;
          EnsureSettingsHeight();
          InvalidateRect(hwnd_, nullptr, FALSE);
        } else if (state_ == State::kSuccess &&
                   PointIn(SecondaryActionRect(), lparam, dpi_scale_)) {
          ChooseFiles();
        } else if (PointIn(PrimaryActionRect(), lparam, dpi_scale_)) {
          if (state_ == State::kSuccess) {
            OpenResult();
          } else {
            ChooseFiles();
          }
        }
      }
      return 0;
    case WM_CAPTURECHANGED:
      dragging_image_ = false;
      dragging_scrollbar_ = false;
      return 0;
    case WM_DROPFILES: {
      auto files = ReadDroppedFiles(reinterpret_cast<HDROP>(wparam));
      if (state_ == State::kConverting) {
        MessageBoxW(hwnd_, Text(UserMessageForToken("BUSY"), L"A conversion is already running."),
                    Text(L"PDF 图片转换", L"PDF & Image Converter"),
                    MB_OK | MB_ICONINFORMATION);
      } else {
        HandleFiles(std::move(files));
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
      if (!pdf_output_plan_.temporary_path.empty()) {
        CleanupTemporaryFile(pdf_output_plan_.temporary_path);
        pdf_output_plan_ = {};
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
    const float secondary_bottom = (std::min)(300.0f, client_height - 38.0f);
    const float primary_bottom = secondary_bottom - 52.0f;
    return D2D1::RectF(left, primary_bottom - 49.0f, left + width, primary_bottom);
  }
  return D2D1::RectF(70.0f, 142.0f, client_width - 70.0f, client_height - 50.0f);
}

D2D1_RECT_F AppWindow::SecondaryActionRect() const {
  RECT client{};
  GetClientRect(hwnd_, &client);
  const float client_width = static_cast<float>(client.right) / dpi_scale_;
  const float client_height = static_cast<float>(client.bottom) / dpi_scale_;
  const float width = (std::min)(260.0f, client_width - 80.0f);
  const float left = (client_width - width) / 2.0f;
  const float bottom = (std::min)(300.0f, client_height - 38.0f);
  return D2D1::RectF(left, bottom - 42.0f, left + width, bottom);
}

D2D1_RECT_F AppWindow::SettingsButtonRect(float width) const {
  return D2D1::RectF(width - 55.0f, 15.0f, width - 19.0f, 51.0f);
}

D2D1_RECT_F AppWindow::BackButtonRect() const { return D2D1::RectF(18, 16, 58, 52); }

D2D1_RECT_F AppWindow::OutputButtonRect(float width) const {
  return D2D1::RectF(width - 132.0f, 128.0f, width - 42.0f, 170.0f);
}

D2D1_RECT_F AppWindow::LanguageButtonRect(float width) const {
  return D2D1::RectF(width - 190.0f, 194.0f, width - 42.0f, 236.0f);
}

D2D1_RECT_F AppWindow::LanguageOptionRect(float width, size_t index) const {
  const float top = 240.0f + static_cast<float>(index) * 34.0f;
  return D2D1::RectF(width - 190.0f, top, width - 42.0f, top + 33.0f);
}

D2D1_RECT_F AppWindow::FeedbackRect(float height) const {
  return D2D1::RectF(34.0f, height - 45.0f, 155.0f, height - 14.0f);
}

D2D1_RECT_F AppWindow::OrderUpRect(float width) const {
  return D2D1::RectF(width - 202.0f, 86.0f, width - 126.0f, 120.0f);
}

D2D1_RECT_F AppWindow::OrderDownRect(float width) const {
  return D2D1::RectF(width - 118.0f, 86.0f, width - 42.0f, 120.0f);
}

D2D1_RECT_F AppWindow::OrderStartRect(float width, float height) const {
  const float bottom = height - 14.0f;
  return D2D1::RectF(90.0f, (std::max)(kOrderListBottom + 10.0f, bottom - 42.0f),
                     width - 90.0f, bottom);
}

D2D1_RECT_F AppWindow::OrderScrollbarRect(float width) const {
  return D2D1::RectF(width - 52.0f, kOrderListTop, width - 44.0f, kOrderListBottom);
}

D2D1_RECT_F AppWindow::OrderSortNameRect(float) const {
  return D2D1::RectF(120.0f, 86.0f, 200.0f, 120.0f);
}

D2D1_RECT_F AppWindow::OrderSortTimeRect(float) const {
  return D2D1::RectF(206.0f, 86.0f, 286.0f, 120.0f);
}

const wchar_t* AppWindow::Text(const wchar_t* chinese, const wchar_t* english) const {
  return settings_.language == InterfaceLanguage::kEnglish ? english : chinese;
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

  auto draw_left_text = [&](const std::wstring& text, IDWriteTextFormat* format,
                            const D2D1_RECT_F& rect, D2D1_COLOR_F color) {
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    draw_text(text, format, rect, color);
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
  };

  if (view_ == View::kImageOrder) {
    draw_text(Text(L"调整图片顺序", L"Arrange image order"), title_format_,
              D2D1::RectF(70, 18, width - 70, 58), D2D1::ColorF(0x172033));
    draw_text(L"‹", title_format_, BackButtonRect(), D2D1::ColorF(0x687386));
    draw_text(Text(L"拖动调整顺序；可继续拖入图片，或直接转换",
                   L"Drag to reorder; add more images or convert immediately"),
              small_format_, D2D1::RectF(35, 56, width - 35, 78), D2D1::ColorF(0x687386));

    const size_t first = image_list_offset_;
    const size_t visible =
        (std::min)(kOrderVisibleRows, pending_images_.size() - first);
    for (size_t row = 0; row < visible; ++row) {
      const size_t index = first + row;
      const float top = kOrderListTop + row * kOrderRowStep;
      const D2D1_RECT_F item =
          D2D1::RectF(42, top, width - 58.0f, top + kOrderRowHeight);
      brush_->SetColor(D2D1::ColorF(index == selected_image_ ? 0xEAF1FF : 0xFFFFFF));
      render_target_->FillRoundedRectangle(D2D1::RoundedRect(item, 6, 6), brush_);
      brush_->SetColor(D2D1::ColorF(index == selected_image_ ? 0x3478F6 : 0xD8DFEA));
      render_target_->DrawRoundedRectangle(
          D2D1::RoundedRect(item, 6, 6), brush_,
          index == selected_image_ && dragging_image_ ? 2.0f : 1.0f);
      draw_left_text(std::to_wstring(index + 1) + L".  " + FileNameFromPath(pending_images_[index]),
                     small_format_, D2D1::RectF(item.left + 12, item.top, item.right - 34, item.bottom),
                     D2D1::ColorF(0x354052));
      draw_text(L"≡", body_format_, D2D1::RectF(item.right - 34, item.top, item.right - 6, item.bottom),
                D2D1::ColorF(0x7D8798));
    }
    const D2D1_RECT_F up = OrderUpRect(width);
    const D2D1_RECT_F down = OrderDownRect(width);
    brush_->SetColor(D2D1::ColorF(0xFFFFFF));
    render_target_->FillRoundedRectangle(D2D1::RoundedRect(up, 6, 6), brush_);
    render_target_->FillRoundedRectangle(D2D1::RoundedRect(down, 6, 6), brush_);
    brush_->SetColor(D2D1::ColorF(0xBFC9D8));
    render_target_->DrawRoundedRectangle(D2D1::RoundedRect(up, 6, 6), brush_, 1.0f);
    render_target_->DrawRoundedRectangle(D2D1::RoundedRect(down, 6, 6), brush_, 1.0f);
    draw_text(Text(L"↑ 上移", L"↑ Up"), small_format_, up, D2D1::ColorF(0x285FCA));
    draw_text(Text(L"↓ 下移", L"↓ Down"), small_format_, down, D2D1::ColorF(0x285FCA));
    draw_text(std::to_wstring(selected_image_ + 1) + L" / " +
                  std::to_wstring(pending_images_.size()),
              small_format_, D2D1::RectF(42, 86, 122, 120),
              D2D1::ColorF(0x7D8798));
    const D2D1_RECT_F sort_name = OrderSortNameRect(width);
    const D2D1_RECT_F sort_time = OrderSortTimeRect(width);
    brush_->SetColor(D2D1::ColorF(0xFFFFFF));
    render_target_->FillRoundedRectangle(D2D1::RoundedRect(sort_name, 6, 6), brush_);
    render_target_->FillRoundedRectangle(D2D1::RoundedRect(sort_time, 6, 6), brush_);
    brush_->SetColor(D2D1::ColorF(0xBFC9D8));
    render_target_->DrawRoundedRectangle(D2D1::RoundedRect(sort_name, 6, 6), brush_, 1.0f);
    render_target_->DrawRoundedRectangle(D2D1::RoundedRect(sort_time, 6, 6), brush_, 1.0f);
    draw_text(Text(L"按名称", L"Name"), small_format_, sort_name, D2D1::ColorF(0x285FCA));
    draw_text(Text(L"按时间", L"Time"), small_format_, sort_time, D2D1::ColorF(0x285FCA));
    if (pending_images_.size() > kOrderVisibleRows) {
      const D2D1_RECT_F scroll = OrderScrollbarRect(width);
      brush_->SetColor(D2D1::ColorF(0xE0E6EF));
      render_target_->FillRoundedRectangle(D2D1::RoundedRect(scroll, 4, 4), brush_);
      const float track_height = scroll.bottom - scroll.top;
      const float thumb_height =
          (std::max)(24.0f, track_height * static_cast<float>(kOrderVisibleRows) /
                                  static_cast<float>(pending_images_.size()));
      const float progress = static_cast<float>(image_list_offset_) /
                             static_cast<float>(pending_images_.size() - kOrderVisibleRows);
      const D2D1_RECT_F thumb = D2D1::RectF(scroll.left, scroll.top +
                                                            (track_height - thumb_height) * progress,
                                            scroll.right, scroll.top +
                                                              (track_height - thumb_height) * progress +
                                                              thumb_height);
      brush_->SetColor(D2D1::ColorF(dragging_scrollbar_ ? 0x3478F6 : 0xAAB5C5));
      render_target_->FillRoundedRectangle(D2D1::RoundedRect(thumb, 4, 4), brush_);
    }
    const D2D1_RECT_F start = OrderStartRect(width, height);
    brush_->SetColor(D2D1::ColorF(0x3478F6));
    render_target_->FillRoundedRectangle(D2D1::RoundedRect(start, 7, 7), brush_);
    draw_text(Text(L"直接转换", L"Convert now"), body_format_, start,
              D2D1::ColorF(0xFFFFFF));

    const HRESULT result = render_target_->EndDraw();
    if (result == D2DERR_RECREATE_TARGET) DiscardGraphicsResources();
    EndPaint(hwnd_, &paint);
    return;
  }

  if (view_ == View::kSettings) {
    draw_text(Text(L"设置", L"Settings"), title_format_, D2D1::RectF(70, 22, width - 70, 66),
              D2D1::ColorF(0x172033));
    draw_text(L"‹", title_format_, BackButtonRect(), D2D1::ColorF(0x687386));

    draw_left_text(Text(L"保存位置", L"Save location"), heading_format_,
                   D2D1::RectF(42, 76, width - 42, 106), D2D1::ColorF(0x172033));
    draw_left_text(Text(L"生成的图片和 PDF 将保存在此目录", L"Converted files are saved here"),
                   small_format_, D2D1::RectF(42, 102, width - 42, 124),
                   D2D1::ColorF(0x687386));
    const D2D1_RECT_F path_box = D2D1::RectF(42, 128, width - 142, 170);
    brush_->SetColor(D2D1::ColorF(0xFFFFFF));
    render_target_->FillRoundedRectangle(D2D1::RoundedRect(path_box, 7, 7), brush_);
    brush_->SetColor(D2D1::ColorF(0xD8DFEA));
    render_target_->DrawRoundedRectangle(D2D1::RoundedRect(path_box, 7, 7), brush_, 1.0f);
    draw_left_text(settings_.output_directory, small_format_,
                   D2D1::RectF(55, 128, width - 154, 170), D2D1::ColorF(0x354052));
    const D2D1_RECT_F output_button = OutputButtonRect(width);
    brush_->SetColor(D2D1::ColorF(0xFFFFFF));
    render_target_->FillRoundedRectangle(D2D1::RoundedRect(output_button, 7, 7), brush_);
    brush_->SetColor(D2D1::ColorF(0xBFC9D8));
    render_target_->DrawRoundedRectangle(D2D1::RoundedRect(output_button, 7, 7), brush_, 1.0f);
    draw_text(Text(L"选择", L"Browse"), small_format_, output_button, D2D1::ColorF(0x285FCA));

    brush_->SetColor(D2D1::ColorF(0xE3E8F0));
    render_target_->DrawLine(D2D1::Point2F(42, 183), D2D1::Point2F(width - 42, 183), brush_,
                             1.0f);

    draw_left_text(Text(L"界面语言", L"Interface language"), heading_format_,
                   D2D1::RectF(42, 193, width - 215, 222), D2D1::ColorF(0x172033));
    draw_left_text(Text(L"修改后立即生效", L"Changes apply immediately"), small_format_,
                   D2D1::RectF(42, 219, width - 215, 243), D2D1::ColorF(0x687386));
    const D2D1_RECT_F language_button = LanguageButtonRect(width);
    brush_->SetColor(D2D1::ColorF(0xFFFFFF));
    render_target_->FillRoundedRectangle(D2D1::RoundedRect(language_button, 7, 7), brush_);
    brush_->SetColor(D2D1::ColorF(0xBFC9D8));
    render_target_->DrawRoundedRectangle(D2D1::RoundedRect(language_button, 7, 7), brush_, 1.0f);
    draw_text(settings_.language == InterfaceLanguage::kChinese
                  ? (language_dropdown_open_ ? L"简体中文  ⌃" : L"简体中文  ⌄")
                  : (language_dropdown_open_ ? L"English  ⌃" : L"English  ⌄"),
              small_format_, language_button, D2D1::ColorF(0x354052));

    draw_left_text(Text(L"问题反馈", L"Feedback"), small_format_, FeedbackRect(height),
                   D2D1::ColorF(0x3478F6));
    draw_left_text(L"v1.0.0", small_format_,
                   D2D1::RectF(width - 94.0f, height - 45.0f, width - 34.0f, height - 14.0f),
                   D2D1::ColorF(0x7D8798));

    if (language_dropdown_open_) {
      for (size_t index = 0; index < 2; ++index) {
        const D2D1_RECT_F option = LanguageOptionRect(width, index);
        const bool selected =
            (index == 0 && settings_.language == InterfaceLanguage::kChinese) ||
            (index == 1 && settings_.language == InterfaceLanguage::kEnglish);
        brush_->SetColor(D2D1::ColorF(selected ? 0xEAF1FF : 0xFFFFFF));
        render_target_->FillRoundedRectangle(D2D1::RoundedRect(option, 5, 5), brush_);
        brush_->SetColor(D2D1::ColorF(selected ? 0x3478F6 : 0xD5DDE9));
        render_target_->DrawRoundedRectangle(D2D1::RoundedRect(option, 5, 5), brush_, 1.0f);
        draw_text(index == 0 ? L"简体中文" : L"English", small_format_, option,
                  D2D1::ColorF(0x354052));
      }
    }

    const HRESULT result = render_target_->EndDraw();
    if (result == D2DERR_RECREATE_TARGET) DiscardGraphicsResources();
    EndPaint(hwnd_, &paint);
    return;
  }

  draw_text(L"⚙", body_format_, SettingsButtonRect(width), D2D1::ColorF(0x687386));

  if (state_ == State::kIdle) {
    draw_text(Text(L"PDF 与图片互转", L"PDF & Image Converter"), title_format_,
              D2D1::RectF(20, 42, width - 20, 88),
              D2D1::ColorF(0x172033));
    draw_text(Text(L"自动识别文件类型并选择转换方向",
                   L"The file type determines the conversion direction"),
              small_format_, D2D1::RectF(20, 91, width - 20, 122),
              D2D1::ColorF(0x687386));
    const D2D1_RECT_F action = PrimaryActionRect();
    brush_->SetColor(D2D1::ColorF(0x3478F6));
    render_target_->DrawRoundedRectangle(D2D1::RoundedRect(action, 10, 10), brush_, 2.0f);
    draw_text(Text(L"将 PDF 或图片拖到这里\n或单击选择文件",
                   L"Drop a PDF or images here\nor click to choose files"),
              heading_format_, action,
              D2D1::ColorF(0x285FCA));
    draw_text(Text(L"支持 PDF、JPG、PNG、BMP、TIFF",
                   L"Supports PDF, JPG, PNG, BMP and TIFF"), small_format_,
              D2D1::RectF(20, height - 44, width - 20, height - 16), D2D1::ColorF(0x7D8798));
  } else if (state_ == State::kConverting) {
    draw_text(std::wstring(Text(L"正在转换：", L"Converting: ")) + input_file_name_,
              heading_format_, D2D1::RectF(30, 62, width - 30, 112),
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
                                         std::to_wstring(total_pages_) +
                                         (task_kind_ == TaskKind::kImagesToPdf
                                              ? Text(L" 张图片", L" images")
                                              : Text(L" 页", L" pages"))
                                   : (task_kind_ == TaskKind::kImagesToPdf
                                          ? Text(L"正在读取图片…", L"Reading images...")
                                          : Text(L"正在读取 PDF…", L"Reading PDF..."));
    draw_text(count, body_format_, D2D1::RectF(20, 190, width - 20, 226), D2D1::ColorF(0x354052));
    const std::wstring detail = current_output_file_.empty()
                                    ? (task_kind_ == TaskKind::kImagesToPdf
                                           ? Text(L"正在创建 PDF", L"Creating PDF")
                                           : Text(L"正在准备图片", L"Preparing images"))
                                    : std::wstring(Text(L"正在生成 ", L"Creating ")) +
                                          current_output_file_;
    draw_text(detail, small_format_, D2D1::RectF(20, 230, width - 20, 268), D2D1::ColorF(0x7D8798));
  } else if (state_ == State::kSuccess) {
    draw_text(Text(L"✓  转换完成", L"✓  Conversion complete"), title_format_,
              D2D1::RectF(20, 65, width - 20, 118),
              D2D1::ColorF(0x168A55));
    const std::wstring success_detail =
        task_kind_ == TaskKind::kImagesToPdf
            ? std::wstring(Text(L"已将 ", L"Combined ")) + std::to_wstring(total_pages_) +
                  Text(L" 张图片合成为 PDF", L" images into one PDF")
            : std::wstring(Text(L"已生成 ", L"Created ")) + std::to_wstring(total_pages_) +
                  Text(L" 张 PNG 图片", L" PNG images");
    draw_text(success_detail, body_format_,
              D2D1::RectF(20, 126, width - 20, 172), D2D1::ColorF(0x4D596B));
    const D2D1_RECT_F button = PrimaryActionRect();
    brush_->SetColor(D2D1::ColorF(0x3478F6));
    render_target_->FillRoundedRectangle(D2D1::RoundedRect(button, 7, 7), brush_);
    draw_text(task_kind_ == TaskKind::kImagesToPdf ? Text(L"打开 PDF", L"Open PDF")
                                                    : Text(L"打开图片文件夹", L"Open image folder"),
              body_format_, button,
              D2D1::ColorF(0xFFFFFF));
    const D2D1_RECT_F choose_again = SecondaryActionRect();
    brush_->SetColor(D2D1::ColorF(0xFFFFFF));
    render_target_->FillRoundedRectangle(D2D1::RoundedRect(choose_again, 7, 7), brush_);
    brush_->SetColor(D2D1::ColorF(0xB9C9E5));
    render_target_->DrawRoundedRectangle(D2D1::RoundedRect(choose_again, 7, 7), brush_, 1.0f);
    draw_text(Text(L"选择其他文件", L"Choose other files"), small_format_, choose_again,
              D2D1::ColorF(0x285FCA));
    draw_text(Text(L"也可以继续拖入 PDF 或图片", L"You can also keep dropping files"),
              small_format_,
              D2D1::RectF(20, choose_again.bottom + 3.0f, width - 20,
                          (std::min)(height - 4.0f, choose_again.bottom + 31.0f)),
              D2D1::ColorF(0x7D8798));
  } else {
    draw_text(Text(L"未能完成转换", L"Conversion failed"), title_format_,
              D2D1::RectF(20, 55, width - 20, 108),
              D2D1::ColorF(0xC04444));
    draw_text(error_message_, body_format_, D2D1::RectF(35, 116, width - 35, 177),
              D2D1::ColorF(0x4D596B));
    const D2D1_RECT_F action = PrimaryActionRect();
    brush_->SetColor(D2D1::ColorF(0x3478F6));
    render_target_->DrawRoundedRectangle(D2D1::RoundedRect(action, 10, 10), brush_, 2.0f);
    draw_text(Text(L"重新选择文件", L"Choose other files"), heading_format_, action,
              D2D1::ColorF(0x285FCA));
  }

  const HRESULT result = render_target_->EndDraw();
  if (result == D2DERR_RECREATE_TARGET) DiscardGraphicsResources();
  EndPaint(hwnd_, &paint);
}

void AppWindow::ChooseFiles() {
  IFileOpenDialog* dialog = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&dialog)))) {
    return;
  }
  DWORD options = 0;
  dialog->GetOptions(&options);
  dialog->SetOptions(options | FOS_ALLOWMULTISELECT | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
  const COMDLG_FILTERSPEC filters_zh[] = {
      {L"PDF 和图片", L"*.pdf;*.jpg;*.jpeg;*.png;*.bmp;*.tif;*.tiff"},
      {L"所有文件", L"*.*"}};
  const COMDLG_FILTERSPEC filters_en[] = {
      {L"PDF and images", L"*.pdf;*.jpg;*.jpeg;*.png;*.bmp;*.tif;*.tiff"},
      {L"All files", L"*.*"}};
  if (settings_.language == InterfaceLanguage::kEnglish) {
    dialog->SetFileTypes(static_cast<UINT>(std::size(filters_en)), filters_en);
    dialog->SetTitle(L"Choose a PDF or images");
  } else {
    dialog->SetFileTypes(static_cast<UINT>(std::size(filters_zh)), filters_zh);
    dialog->SetTitle(L"选择 PDF 或图片");
  }
  if (SUCCEEDED(dialog->Show(hwnd_))) {
    IShellItemArray* items = nullptr;
    if (SUCCEEDED(dialog->GetResults(&items))) {
      DWORD count = 0;
      items->GetCount(&count);
      std::vector<std::wstring> files;
      files.reserve(count);
      for (DWORD index = 0; index < count; ++index) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(items->GetItemAt(index, &item))) {
          PWSTR path = nullptr;
          if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
            files.emplace_back(path);
            CoTaskMemFree(path);
          }
          item->Release();
        }
      }
      items->Release();
      HandleFiles(std::move(files));
    }
  }
  dialog->Release();
}

void AppWindow::HandleFiles(std::vector<std::wstring> files) {
  if (files.empty()) return;
  if (view_ == View::kImageOrder) {
    for (const auto& path : files) {
      const DWORD attributes = GetFileAttributesW(path.c_str());
      if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) ||
          !HasImageExtension(path)) {
        MessageBoxW(hwnd_, Text(L"调整顺序时只能继续添加图片。",
                                L"Only image files can be added while arranging pages."),
                    Text(L"PDF 图片转换", L"PDF & Image Converter"),
                    MB_OK | MB_ICONINFORMATION);
        return;
      }
    }
    pending_images_.insert(pending_images_.end(), std::make_move_iterator(files.begin()),
                           std::make_move_iterator(files.end()));
    selected_image_ = pending_images_.size() - 1;
    image_list_offset_ = selected_image_ >= kOrderVisibleRows - 1
                             ? selected_image_ - (kOrderVisibleRows - 1)
                             : 0;
    const size_t maximum = pending_images_.size() > kOrderVisibleRows
                               ? pending_images_.size() - kOrderVisibleRows
                               : 0;
    image_list_offset_ = (std::min)(image_list_offset_, maximum);
    InvalidateRect(hwnd_, nullptr, FALSE);
    return;
  }
  size_t pdf_count = 0;
  size_t image_count = 0;
  for (const auto& path : files) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
      SetError("FILE_NOT_FOUND");
      return;
    }
    if (HasPdfExtension(path)) {
      ++pdf_count;
    } else if (HasImageExtension(path)) {
      ++image_count;
    } else {
      SetError("UNSUPPORTED_FILE");
      return;
    }
  }
  view_ = View::kMain;
  if (pdf_count > 0 && image_count > 0) {
    SetError("MIXED_FILES");
  } else if (pdf_count > 1) {
    SetError("MULTIPLE_FILES");
  } else if (pdf_count == 1) {
    StartPdfConversion(files.front());
  } else {
    std::sort(files.begin(), files.end(), [](const std::wstring& left, const std::wstring& right) {
      return StrCmpLogicalW(FileNameFromPath(left).c_str(), FileNameFromPath(right).c_str()) < 0;
    });
    pending_images_ = std::move(files);
    selected_image_ = 0;
    image_list_offset_ = 0;
    if (pending_images_.size() == 1) {
      StartImageConversion();
    } else {
      view_ = View::kImageOrder;
      EnsureSettingsHeight();
      InvalidateRect(hwnd_, nullptr, FALSE);
    }
  }
}

void AppWindow::MoveSelectedImage(int direction) {
  if (pending_images_.empty()) return;
  if (direction < 0 && selected_image_ > 0) {
    std::swap(pending_images_[selected_image_], pending_images_[selected_image_ - 1]);
    --selected_image_;
  } else if (direction > 0 && selected_image_ + 1 < pending_images_.size()) {
    std::swap(pending_images_[selected_image_], pending_images_[selected_image_ + 1]);
    ++selected_image_;
  }
  if (selected_image_ < image_list_offset_) {
    image_list_offset_ = selected_image_;
  } else if (selected_image_ >= image_list_offset_ + kOrderVisibleRows) {
    image_list_offset_ = selected_image_ - (kOrderVisibleRows - 1);
  }
}

void AppWindow::SortPendingImages(bool by_time) {
  if (pending_images_.size() < 2) return;
  const std::wstring selected_path = pending_images_[selected_image_];
  std::stable_sort(pending_images_.begin(), pending_images_.end(),
                   [by_time](const std::wstring& left, const std::wstring& right) {
                     if (by_time) {
                       const ULONGLONG left_time = FileModifiedTime(left);
                       const ULONGLONG right_time = FileModifiedTime(right);
                       if (left_time != right_time) return left_time < right_time;
                     }
                     return StrCmpLogicalW(FileNameFromPath(left).c_str(),
                                           FileNameFromPath(right).c_str()) < 0;
                   });
  const auto selected = std::find(pending_images_.begin(), pending_images_.end(), selected_path);
  selected_image_ = selected == pending_images_.end()
                        ? 0
                        : static_cast<size_t>(selected - pending_images_.begin());
  image_list_offset_ = selected_image_ >= kOrderVisibleRows - 1
                           ? selected_image_ - (kOrderVisibleRows - 1)
                           : 0;
  const size_t maximum = pending_images_.size() > kOrderVisibleRows
                             ? pending_images_.size() - kOrderVisibleRows
                             : 0;
  image_list_offset_ = (std::min)(image_list_offset_, maximum);
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void AppWindow::ChooseOutputDirectory() {
  IFileOpenDialog* dialog = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&dialog)))) {
    return;
  }
  DWORD options = 0;
  dialog->GetOptions(&options);
  dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
  dialog->SetTitle(Text(L"选择转换结果保存位置", L"Choose where converted files are saved"));

  IShellItem* initial = nullptr;
  if (SUCCEEDED(SHCreateItemFromParsingName(settings_.output_directory.c_str(), nullptr,
                                             IID_PPV_ARGS(&initial)))) {
    dialog->SetFolder(initial);
    initial->Release();
  }

  if (SUCCEEDED(dialog->Show(hwnd_))) {
    IShellItem* selected = nullptr;
    if (SUCCEEDED(dialog->GetResult(&selected))) {
      PWSTR path = nullptr;
      if (SUCCEEDED(selected->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
        settings_.output_directory = path;
        SaveAppSettings(settings_);
        CoTaskMemFree(path);
      }
      selected->Release();
    }
  }
  dialog->Release();
}

void AppWindow::SetLanguage(InterfaceLanguage language) {
  settings_.language = language;
  language_dropdown_open_ = false;
  SaveAppSettings(settings_);
  SetWindowTextW(hwnd_, Text(L"PDF 图片转换", L"PDF & Image Converter"));
  if (state_ == State::kError && !worker_error_token_.empty()) {
    error_message_ = Text(UserMessageForToken(worker_error_token_),
                          EnglishMessageForToken(worker_error_token_));
  }
}

void AppWindow::EnsureSettingsHeight() {
  RECT client{};
  RECT window{};
  GetClientRect(hwnd_, &client);
  GetWindowRect(hwnd_, &window);
  const float logical_height = static_cast<float>(client.bottom) / dpi_scale_;
  constexpr float kMinimumSettingsHeight = 370.0f;
  if (logical_height >= kMinimumSettingsHeight) return;

  const int extra = static_cast<int>((kMinimumSettingsHeight - logical_height) * dpi_scale_ + 0.5f);
  const int new_height = window.bottom - window.top + extra;
  int new_top = window.top;
  MONITORINFO monitor{};
  monitor.cbSize = sizeof(monitor);
  if (GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &monitor) &&
      new_top + new_height > monitor.rcWork.bottom) {
    new_top = (std::max)(monitor.rcWork.top, monitor.rcWork.bottom - new_height);
  }
  SetWindowPos(hwnd_, nullptr, window.left, new_top, window.right - window.left, new_height,
               SWP_NOACTIVATE | SWP_NOZORDER);
}

void AppWindow::OpenFeedback() {
  const HINSTANCE result = ShellExecuteW(hwnd_, L"open", L"https://wj.qq.com/s2/27384492/508f/",
                                        nullptr, nullptr, SW_SHOWNORMAL);
  if (reinterpret_cast<INT_PTR>(result) <= 32) {
    MessageBoxW(hwnd_, Text(L"无法打开反馈页面。", L"The feedback page could not be opened."),
                Text(L"PDF 图片转换", L"PDF & Image Converter"), MB_OK | MB_ICONWARNING);
  }
}

void AppWindow::StartPdfConversion(const std::wstring& path) {
  if (state_ == State::kConverting) {
    MessageBoxW(hwnd_, Text(UserMessageForToken("BUSY"), EnglishMessageForToken("BUSY")),
                Text(L"PDF 图片转换", L"PDF & Image Converter"), MB_OK | MB_ICONINFORMATION);
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
  if (!PrepareOutputPlan(path, settings_.output_directory, &plan, &prepare_error)) {
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
  task_kind_ = TaskKind::kPdfToImages;
  view_ = View::kMain;
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

void AppWindow::StartImageConversion() {
  if (pending_images_.empty()) return;
  view_ = View::kMain;
  LogEvent("TASK_PREPARE", "IMAGES_TO_PDF", static_cast<int>(pending_images_.size()));
  FileOutputPlan plan;
  std::wstring prepare_error;
  if (!PreparePdfOutputPlan(pending_images_.front(), settings_.output_directory, &plan,
                            &prepare_error)) {
    SetError("CANNOT_WRITE");
    return;
  }
  std::wstring start_error;
  if (!worker_.StartImages(hwnd_, WorkerPath(), pending_images_, plan.temporary_path,
                           &start_error)) {
    CleanupTemporaryFile(plan.temporary_path);
    SetError("WORKER_CRASHED");
    return;
  }
  pdf_output_plan_ = plan;
  task_kind_ = TaskKind::kImagesToPdf;
  input_file_name_ = std::to_wstring(pending_images_.size()) +
                     Text(L" 张图片", L" images");
  current_output_file_.clear();
  worker_error_token_.clear();
  current_page_ = 0;
  total_pages_ = 0;
  worker_reported_done_ = false;
  state_ = State::kConverting;
  pending_images_.clear();
  LogEvent("WORKER_STARTED", "IMAGES_TO_PDF");
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void AppWindow::SetError(const std::string& token) {
  worker_error_token_ = token;
  error_message_ = Text(UserMessageForToken(token), EnglishMessageForToken(token));
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
    const bool committed = task_kind_ == TaskKind::kImagesToPdf
                               ? CommitPdfOutputPlan(pdf_output_plan_, &commit_error)
                               : CommitOutputPlan(output_plan_, &commit_error);
    if (committed) {
      if (task_kind_ == TaskKind::kImagesToPdf) {
        completed_output_file_ = pdf_output_plan_.final_path;
        completed_output_directory_ = settings_.output_directory;
        pdf_output_plan_ = {};
      } else {
        completed_output_directory_ = output_plan_.final_directory;
        completed_output_file_.clear();
        output_plan_ = {};
      }
      state_ = State::kSuccess;
      LogEvent("TASK_COMPLETE", "OK", total_pages_);
    } else {
      if (task_kind_ == TaskKind::kImagesToPdf) {
        CleanupTemporaryFile(pdf_output_plan_.temporary_path);
        pdf_output_plan_ = {};
      } else {
        CleanupTemporaryDirectory(output_plan_.temporary_directory);
        output_plan_ = {};
      }
      SetError("CANNOT_WRITE");
    }
  } else {
    if (task_kind_ == TaskKind::kImagesToPdf) {
      CleanupTemporaryFile(pdf_output_plan_.temporary_path);
      pdf_output_plan_ = {};
    } else {
      CleanupTemporaryDirectory(output_plan_.temporary_directory);
      output_plan_ = {};
    }
    const std::string token = worker_error_token_.empty() ? ExitCodeToken(exit_code) : worker_error_token_;
    LogEvent("TASK_FAILED", token.c_str(), current_page_);
    SetError(token);
  }
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void AppWindow::OpenResult() {
  bool opened = false;
  if (task_kind_ == TaskKind::kImagesToPdf && !completed_output_file_.empty()) {
    opened = reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd_, L"open", completed_output_file_.c_str(),
                                                     nullptr, nullptr, SW_SHOWNORMAL)) > 32;
  } else {
    opened = OpenFolderExact(completed_output_directory_);
  }
  if (!opened) {
    MessageBoxW(hwnd_, Text(L"无法打开转换结果，请从保存位置手动打开。",
                            L"The result could not be opened. Open it from the save location."),
                Text(L"PDF 图片转换", L"PDF & Image Converter"),
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
