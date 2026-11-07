#pragma once

#include <string>

// Appends a saved quote to /My Clippings.txt on the SD card in Kindle format,
// so the file can be imported by Readwise, Obsidian, etc. over USB.
//
// Ported from uxjulia/CrossInk (MIT license). The export is append-only: a
// quote deleted in the reader is removed from the binary clipping store and
// queued for sync, but its entry stays in /My Clippings.txt.
class ClippingsManager {
 public:
  static bool saveClipping(const std::string& bookTitle, const std::string& author, const std::string& chapterTitle,
                           int pageNumber, const std::string& selectedText);

  static constexpr const char* CLIPPINGS_PATH = "/My Clippings.txt";
};
