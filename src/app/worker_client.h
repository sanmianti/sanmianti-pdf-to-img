#pragma once

#include "common/protocol.h"

#include <windows.h>

#include <string>
#include <thread>
#include <vector>

namespace pdfimg {

constexpr UINT kWorkerNotificationMessage = WM_APP + 42;

struct WorkerNotification {
  WorkerEvent event;
  bool process_exited = false;
  DWORD exit_code = 0;
};

class WorkerClient {
 public:
  WorkerClient() = default;
  ~WorkerClient();
  WorkerClient(const WorkerClient&) = delete;
  WorkerClient& operator=(const WorkerClient&) = delete;

  bool Start(HWND notify_window,
             const std::wstring& worker_path,
             const std::wstring& input_path,
             const std::wstring& output_directory,
             std::wstring* error);
  bool StartImages(HWND notify_window,
                   const std::wstring& worker_path,
                   const std::vector<std::wstring>& image_paths,
                   const std::wstring& output_path,
                   std::wstring* error);
  void Cancel();
  bool running() const { return thread_.joinable(); }

 private:
  bool StartInternal(HWND notify_window,
                     const std::wstring& worker_path,
                     const std::wstring& input_path,
                     const std::wstring& output_path,
                     bool images_to_pdf,
                     std::wstring* error);
  void ReadLoop();
  void Post(WorkerNotification* notification);

  HWND notify_window_ = nullptr;
  HANDLE job_ = nullptr;
  HANDLE process_ = nullptr;
  HANDLE pipe_read_ = nullptr;
  std::thread thread_;
  std::wstring manifest_path_;
};

}  // namespace pdfimg
