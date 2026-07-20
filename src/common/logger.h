#pragma once

namespace pdfimg {

void InitializeLog();
void LogEvent(const char* stage, const char* code = "OK", int page = 0);

}  // namespace pdfimg

