#pragma once
#include <cstddef>
#include <cstdint>

// The parser is a Print sink so the client can stream feed bytes into it; the
// host tests feed it directly and need only the methods it overrides.
class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t*, size_t) = 0;
  virtual void flush() {}
};
