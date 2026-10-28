#pragma once
#include <cstddef>

// Only what the SDK's OPDS Atom parser asks of the board: the largest free
// block sizes its entry vector. Host tests keep it above the 64-entry cap so
// the parser reserves its full capacity.
struct HostEspClass {
  size_t getMaxAllocHeap() const { return 80000; }
  size_t getFreeHeap() const { return 80000; }
};

inline HostEspClass ESP;
