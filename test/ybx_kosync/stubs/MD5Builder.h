#pragma once
#include <string>

class MD5Builder {
 public:
  void begin() {}
  void add(const char*) {}
  void add(const std::string&) {}
  void calculate() {}
  std::string toString() const { return "mock_md5"; }
};
