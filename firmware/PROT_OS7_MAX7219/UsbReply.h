#pragma once
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>

// snprintf returns the length it wanted to write, not the length stored. Once
// formatting fails, keep the cursor in bounds and prohibit subsequent writes.
class BoundedReply {
 public:
  BoundedReply(char *buffer, size_t capacity) : data(buffer), size(capacity) {
    good = data && size;
    if (good) data[0] = '\0';
  }
  bool append(const char *format, ...) {
    if (!good) return false;
    va_list args;
    va_start(args, format);
    const int count = vsnprintf(data + used, size - used, format, args);
    va_end(args);
    if (count < 0 || static_cast<size_t>(count) >= size - used) {
      good = false;
      return false;
    }
    used += static_cast<size_t>(count);
    return true;
  }
  bool jsonString(const char *value) {
    if (!append("\"")) return false;
    if (!value) { good = false; return false; }
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(value); *p; ++p) {
      if (*p == '"' || *p == '\\') { if (!append("\\%c", *p)) return false; }
      else if (*p < 0x20 || *p >= 0x7f) { if (!append("\\u%04x", *p)) return false; }
      else if (!append("%c", *p)) return false;
    }
    return append("\"");
  }
  bool okay() const { return good; }
 private:
  char *data;
  size_t size, used = 0;
  bool good = false;
};
