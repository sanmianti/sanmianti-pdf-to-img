#pragma once

#include <string>

namespace pdfimg {

enum class WorkerEventType { kInvalid, kStart, kProgress, kDone, kError };

struct WorkerEvent {
  WorkerEventType type = WorkerEventType::kInvalid;
  int current = 0;
  int total = 0;
  std::string value;
};

bool ParseWorkerEvent(const std::string& line, WorkerEvent* event);
std::string FormatStartEvent(int total);
std::string FormatProgressEvent(int current, int total, const std::string& file_name);
std::string FormatDoneEvent(int total);
std::string FormatErrorEvent(const std::string& token);

}  // namespace pdfimg

