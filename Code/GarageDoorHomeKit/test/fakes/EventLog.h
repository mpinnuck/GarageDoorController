#pragma once

#include <cstdarg>
#include <cstdio>

struct EventLog {
  char last[256] = {};
  void log(const char *format, ...) {
    va_list args;
    va_start(args, format);
    std::vsnprintf(last, sizeof(last), format, args);
    va_end(args);
  }
};

extern EventLog Log;
