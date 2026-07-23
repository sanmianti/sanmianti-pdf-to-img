#include "app/worker_client.h"

#include <limits>
#include <cstdint>
#include <vector>

namespace pdfimg {
namespace {

std::wstring Quote(const std::wstring& value) { return L"\"" + value + L"\""; }

void CloseHandleIfSet(HANDLE* handle) {
  if (*handle && *handle != INVALID_HANDLE_VALUE) {
    CloseHandle(*handle);
    *handle = nullptr;
  }
}

bool WriteAll(HANDLE file, const void* data, DWORD bytes) {
  const auto* cursor = static_cast<const std::uint8_t*>(data);
  DWORD remaining = bytes;
  while (remaining > 0) {
    DWORD written = 0;
    if (!WriteFile(file, cursor, remaining, &written, nullptr) || written == 0) return false;
    cursor += written;
    remaining -= written;
  }
  return true;
}

}  // namespace

WorkerClient::~WorkerClient() { Cancel(); }

bool WorkerClient::Start(HWND notify_window,
                         const std::wstring& worker_path,
                         const std::wstring& input_path,
                         const std::wstring& output_directory,
                         std::wstring* error) {
  Cancel();
  return StartInternal(notify_window, worker_path, input_path, output_directory, false, error);
}

bool WorkerClient::StartImages(HWND notify_window,
                               const std::wstring& worker_path,
                               const std::vector<std::wstring>& image_paths,
                               const std::wstring& output_path,
                               std::wstring* error) {
  Cancel();
  if (image_paths.empty() || image_paths.size() > 10000) return false;
  wchar_t temporary[MAX_PATH]{};
  if (!GetTempPathW(static_cast<DWORD>(std::size(temporary)), temporary)) return false;
  HANDLE manifest = INVALID_HANDLE_VALUE;
  for (unsigned attempt = 0; attempt < 32 && manifest == INVALID_HANDLE_VALUE; ++attempt) {
    manifest_path_ = std::wstring(temporary) + L"PdfToImage-" +
                     std::to_wstring(GetCurrentProcessId()) + L"-" +
                     std::to_wstring(GetTickCount64() + attempt) + L".manifest";
    manifest = CreateFileW(manifest_path_.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_TEMPORARY, nullptr);
  }
  if (manifest == INVALID_HANDLE_VALUE) {
    manifest_path_.clear();
    if (error) *error = L"无法创建图片清单";
    return false;
  }
  const std::uint32_t count = static_cast<std::uint32_t>(image_paths.size());
  bool written = WriteAll(manifest, &count, sizeof(count));
  for (const auto& path : image_paths) {
    if (path.empty() || path.size() > 32767) {
      written = false;
      break;
    }
    const std::uint32_t length = static_cast<std::uint32_t>(path.size());
    written = written && WriteAll(manifest, &length, sizeof(length));
    if (written && length > 0) {
      written = WriteAll(manifest, path.data(), length * sizeof(wchar_t));
    }
  }
  CloseHandle(manifest);
  if (!written) {
    DeleteFileW(manifest_path_.c_str());
    manifest_path_.clear();
    if (error) *error = L"无法写入图片清单";
    return false;
  }
  if (!StartInternal(notify_window, worker_path, manifest_path_, output_path, true, error)) {
    DeleteFileW(manifest_path_.c_str());
    manifest_path_.clear();
    return false;
  }
  return true;
}

bool WorkerClient::StartInternal(HWND notify_window,
                                 const std::wstring& worker_path,
                                 const std::wstring& input_path,
                                 const std::wstring& output_directory,
                                 bool images_to_pdf,
                                 std::wstring* error) {
  notify_window_ = notify_window;

  SECURITY_ATTRIBUTES security{};
  security.nLength = sizeof(security);
  security.bInheritHandle = TRUE;
  HANDLE pipe_write = nullptr;
  if (!CreatePipe(&pipe_read_, &pipe_write, &security, 0) ||
      !SetHandleInformation(pipe_read_, HANDLE_FLAG_INHERIT, 0)) {
    if (error) *error = L"无法创建 Worker 通信通道";
    CloseHandleIfSet(&pipe_write);
    CloseHandleIfSet(&pipe_read_);
    return false;
  }

  job_ = CreateJobObjectW(nullptr, nullptr);
  if (!job_) {
    if (error) *error = L"无法创建 Worker 作业";
    CloseHandleIfSet(&pipe_write);
    CloseHandleIfSet(&pipe_read_);
    return false;
  }
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags =
      JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_PROCESS_MEMORY;
  limits.ProcessMemoryLimit = static_cast<SIZE_T>(768) * 1024 * 1024;
  if (!SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits,
                               sizeof(limits))) {
    if (error) *error = L"无法设置 Worker 资源限制";
    CloseHandleIfSet(&pipe_write);
    CloseHandleIfSet(&pipe_read_);
    CloseHandleIfSet(&job_);
    return false;
  }

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  startup.wShowWindow = SW_HIDE;
  startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  startup.hStdOutput = pipe_write;
  startup.hStdError = pipe_write;

  PROCESS_INFORMATION process_info{};
  std::wstring command = Quote(worker_path) +
                         (images_to_pdf ? L" --images " : L" --input ") + Quote(input_path) +
                         L" --output " + Quote(output_directory);
  const size_t separator = worker_path.find_last_of(L"\\/");
  const std::wstring working_directory =
      separator == std::wstring::npos ? L"." : worker_path.substr(0, separator);
  const DWORD flags = CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT;
  const BOOL created = CreateProcessW(worker_path.c_str(), &command[0], nullptr, nullptr, TRUE, flags,
                                      nullptr, working_directory.c_str(), &startup, &process_info);
  CloseHandleIfSet(&pipe_write);
  if (!created) {
    if (error) *error = L"无法启动转换进程";
    CloseHandleIfSet(&pipe_read_);
    CloseHandleIfSet(&job_);
    return false;
  }

  process_ = process_info.hProcess;
  if (!AssignProcessToJobObject(job_, process_)) {
    TerminateProcess(process_, static_cast<UINT>(-1));
    if (error) *error = L"无法约束转换进程";
    CloseHandle(process_info.hThread);
    CloseHandleIfSet(&process_);
    CloseHandleIfSet(&pipe_read_);
    CloseHandleIfSet(&job_);
    return false;
  }

  if (ResumeThread(process_info.hThread) == static_cast<DWORD>(-1)) {
    TerminateProcess(process_, static_cast<UINT>(-1));
    if (error) *error = L"无法启动转换进程";
    CloseHandle(process_info.hThread);
    CloseHandleIfSet(&process_);
    CloseHandleIfSet(&pipe_read_);
    CloseHandleIfSet(&job_);
    return false;
  }
  CloseHandle(process_info.hThread);
  thread_ = std::thread(&WorkerClient::ReadLoop, this);
  return true;
}

void WorkerClient::ReadLoop() {
  std::string pending;
  char buffer[4096];
  while (true) {
    DWORD read = 0;
    if (!ReadFile(pipe_read_, buffer, sizeof(buffer), &read, nullptr) || read == 0) break;
    pending.append(buffer, buffer + read);
    size_t newline = std::string::npos;
    while ((newline = pending.find('\n')) != std::string::npos) {
      std::string line = pending.substr(0, newline);
      pending.erase(0, newline + 1);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      WorkerEvent event;
      if (ParseWorkerEvent(line, &event)) {
        auto* notification = new WorkerNotification;
        notification->event = event;
        Post(notification);
      }
    }
  }
  WaitForSingleObject(process_, INFINITE);
  DWORD exit_code = static_cast<DWORD>(-1);
  GetExitCodeProcess(process_, &exit_code);
  auto* notification = new WorkerNotification;
  notification->process_exited = true;
  notification->exit_code = exit_code;
  Post(notification);
}

void WorkerClient::Post(WorkerNotification* notification) {
  if (!notify_window_ || !PostMessageW(notify_window_, kWorkerNotificationMessage, 0,
                                       reinterpret_cast<LPARAM>(notification))) {
    delete notification;
  }
}

void WorkerClient::Cancel() {
  CloseHandleIfSet(&job_);
  if (thread_.joinable()) thread_.join();
  CloseHandleIfSet(&pipe_read_);
  CloseHandleIfSet(&process_);
  if (!manifest_path_.empty()) {
    DeleteFileW(manifest_path_.c_str());
    manifest_path_.clear();
  }
  notify_window_ = nullptr;
}

}  // namespace pdfimg
