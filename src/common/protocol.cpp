#include "common/protocol.h"

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <vector>

namespace pdfimg {
namespace {

std::vector<std::string> SplitTabs(const std::string& line) {
  std::vector<std::string> fields;
  size_t begin = 0;
  while (true) {
    const size_t tab = line.find('\t', begin);
    fields.push_back(line.substr(begin, tab == std::string::npos ? tab : tab - begin));
    if (tab == std::string::npos) return fields;
    begin = tab + 1;
  }
}

bool ParsePositive(const std::string& value, int* result) {
  if (value.empty()) return false;
  char* end = nullptr;
  errno = 0;
  const long parsed = std::strtol(value.c_str(), &end, 10);
  if (errno || end != value.c_str() + value.size() || parsed <= 0 || parsed > INT_MAX) {
    return false;
  }
  *result = static_cast<int>(parsed);
  return true;
}

}  // namespace


bool ParseWorkerEvent(const std::string& line, WorkerEvent* event) {
  if (!event) return false;
  WorkerEvent parsed;
  const auto fields = SplitTabs(line);
  if (fields.size() == 2 && fields[0] == "START" && ParsePositive(fields[1], &parsed.total)) {
    parsed.type = WorkerEventType::kStart;
  } else if (fields.size() == 4 && fields[0] == "PROGRESS" &&
             ParsePositive(fields[1], &parsed.current) &&
             ParsePositive(fields[2], &parsed.total) && parsed.current <= parsed.total &&
             !fields[3].empty()) {
    parsed.type = WorkerEventType::kProgress;
    parsed.value = fields[3];
  } else if (fields.size() == 2 && fields[0] == "DONE" &&
             ParsePositive(fields[1], &parsed.total)) {
    parsed.type = WorkerEventType::kDone;
  } else if (fields.size() == 2 && fields[0] == "ERROR" && !fields[1].empty()) {
    parsed.type = WorkerEventType::kError;
    parsed.value = fields[1];
  } else {
    return false;
  }
  *event = parsed;
  return true;
}

std::string FormatStartEvent(int total) { return "START\t" + std::to_string(total) + "\n"; }

std::string FormatProgressEvent(int current, int total, const std::string& file_name) {
  return "PROGRESS\t" + std::to_string(current) + "\t" + std::to_string(total) +
         "\t" + file_name + "\n";
}

std::string FormatDoneEvent(int total) { return "DONE\t" + std::to_string(total) + "\n"; }

std::string FormatErrorEvent(const std::string& token) { return "ERROR\t" + token + "\n"; }

}  // namespace pdfimg

