#pragma once

#include "common/protocol.h"

#include <windows.h>

#include <string>
#include <thread>

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
  void Cancel();
  bool running() const { return thread_.joinable(); }

 private:
  void ReadLoop();
  void Post(WorkerNotification* notification);

  HWND notify_window_ = nullptr;
  HANDLE job_ = nullptr;
  HANDLE process_ = nullptr;
  HANDLE pipe_read_ = nullptr;
  std::thread thread_;
};

}  // namespace pdfimg

