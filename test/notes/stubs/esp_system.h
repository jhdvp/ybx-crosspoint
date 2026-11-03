#pragma once
#include <cstdint>
inline uint32_t esp_random() {
  static uint32_t value = 0x10203040;
  return ++value;
}
