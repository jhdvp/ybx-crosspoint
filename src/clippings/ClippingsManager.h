#pragma once

#include <string>

// Appends a saved quote to /My Clippings.txt on the SD card in Kindle format,
// so the file can be imported by Readwise, Obsidian, etc. over USB.
//
// Adapted from uxjulia/CrossInk.
//
// Copyright (c) 2025 Dave Allie
// Copyright (c) 2025 CrossPoint Reader organization
// SPDX-License-Identifier: MIT
//
// The export is append-only: a quote deleted in the reader is removed from the
// binary clipping store and queued for sync, but its entry stays in /My Clippings.txt.
class ClippingsManager {
 public:
  /**
   * Appends a highlighted passage to /My Clippings.txt on the SD card in
   * Kindle-compatible format.
   *
   * @param bookTitle Title of the book.
   * @param author Author of the book.
   * @param chapterTitle Current chapter title (omitted if empty).
   * @param pageNumber 1-based page number within the section.
   * @param selectedText Clipped text content.
   * @return true if successfully appended to the file, false otherwise.
   */
  static bool saveClipping(const std::string& bookTitle, const std::string& author, const std::string& chapterTitle,
                           int pageNumber, const std::string& selectedText);

  static constexpr const char* CLIPPINGS_PATH = "/My Clippings.txt";
};
