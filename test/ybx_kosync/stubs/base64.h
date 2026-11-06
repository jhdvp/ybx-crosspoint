#pragma once
#include <string>

#ifndef ARDUINO_STRING_DEFINED
#define ARDUINO_STRING_DEFINED
class String {
 public:
  String() = default;
  String(const char* s) : value_(s ? s : "") {}
  String(const std::string& s) : value_(s) {}
  const char* c_str() const { return value_.c_str(); }
  size_t length() const { return value_.length(); }

 private:
  std::string value_;
};
#endif

namespace base64 {
inline String encode(const char* s) { return String(s ? s : ""); }
}  // namespace base64
