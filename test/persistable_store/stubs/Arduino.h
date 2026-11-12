#pragma once

// Minimal native stand-in for Arduino's String, covering what
// PersistableStore.cpp and ArduinoJson's String writer/reader need:
// null-safe const char* assignment, concat, c_str/length, isEmpty.
#include <string>

class String {
 public:
  String() = default;
  String(const char* s) : value_(s ? s : "") {}

  String& operator=(const char* s) {
    value_ = s ? s : "";
    return *this;
  }

  bool concat(const char* s) {
    if (s) value_ += s;
    return true;
  }

  const char* c_str() const { return value_.c_str(); }
  size_t length() const { return value_.length(); }
  bool isEmpty() const { return value_.empty(); }

 private:
  std::string value_;
};
