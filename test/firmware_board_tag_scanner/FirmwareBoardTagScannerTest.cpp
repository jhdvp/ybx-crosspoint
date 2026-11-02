// Host-side regression fixture for the OTA update-packaging path's board-tag
// scanner (src/network/FirmwareBoardTag.h). The scanner streams over a
// downloaded firmware image and must reject an image built for a different
// board before esp_ota_end() promotes it to the boot target. This suite pins
// the contract without touching ESP-IDF or TLS: chunk boundaries, partial
// magic collisions, overlong/non-printable names, multiple tags, and the
// "untagged image is allowed" rule are all exercised here so a future refactor
// of FirmwareBoardTag.cpp cannot silently weaken the guard.

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "src/network/FirmwareBoardTag.h"

namespace {

// Build a synthetic image body carrying exactly one board tag at `offset`.
// Everything else is zero-filled noise; the scanner only looks for the magic.
std::string imageWithTag(const char* boardName, size_t tagOffset) {
  const std::string tag =
      std::string("CROSSPOINT-BOARD-V1:") + boardName + ";";
  std::string body(std::max<size_t>(tagOffset + tag.size() + 64, 256), '\0');
  std::memcpy(&body[tagOffset], tag.data(), tag.size());
  return body;
}

// Feed `body` to the scanner in chunks of `chunkSize`; returns true when the
// scanner reports a mismatch at any point during the stream.
bool scanInChunks(const std::string& body, size_t chunkSize) {
  board_tag::Scanner s;
  for (size_t off = 0; off < body.size(); off += chunkSize) {
    const size_t n = std::min(chunkSize, body.size() - off);
    s.feed(reinterpret_cast<const uint8_t*>(body.data() + off), n);
    if (s.mismatch()) return true;
  }
  return s.mismatch();
}

}  // namespace

// --- Rule: an untagged image is accepted ------------------------------------

TEST(FirmwareBoardTagScanner, UntaggedImageIsAccepted) {
  const std::string body(512, '\xFF');
  EXPECT_FALSE(scanInChunks(body, 1));
  EXPECT_FALSE(scanInChunks(body, 7));
  EXPECT_FALSE(scanInChunks(body, 64));
}

// --- Rule: a tag naming this board passes -----------------------------------

TEST(FirmwareBoardTagScanner, MatchingTagIsAcceptedAtEveryChunkSize) {
  const std::string thisBoard(board_tag::boardName(), board_tag::boardNameLen());
  ASSERT_FALSE(thisBoard.empty()) << "board_tag::boardName() must be set by the build";
  const std::string body = imageWithTag(thisBoard.c_str(), 37);
  for (size_t chunk : {1u, 2u, 3u, 5u, 7u, 11u, 13u, 31u, 64u, 128u}) {
    EXPECT_FALSE(scanInChunks(body, chunk)) << "chunk=" << chunk;
  }
}

// --- Rule: a tag naming another board is rejected ---------------------------

TEST(FirmwareBoardTagScanner, MismatchedTagIsRejectedAndNamesTheOffendingBoard) {
  // Pick a name that differs from the running board and fits MAX_NAME.
  std::string other = "otherboard";
  const std::string thisBoard(board_tag::boardName(), board_tag::boardNameLen());
  if (other == thisBoard) other = "notthisone";
  const std::string body = imageWithTag(other.c_str(), 19);

  board_tag::Scanner s;
  s.feed(reinterpret_cast<const uint8_t*>(body.data()), body.size());
  ASSERT_TRUE(s.mismatch());
  EXPECT_STREQ(s.foundName(), other.c_str());
}

TEST(FirmwareBoardTagScanner, MismatchIsStickyAcrossFurtherFeeds) {
  const std::string body = imageWithTag("wrongboard", 0);
  board_tag::Scanner s;
  s.feed(reinterpret_cast<const uint8_t*>(body.data()), body.size());
  ASSERT_TRUE(s.mismatch());
  // Further feeds must not clear the flag or change the captured name.
  const std::string tail(256, '\0');
  s.feed(reinterpret_cast<const uint8_t*>(tail.data()), tail.size());
  EXPECT_TRUE(s.mismatch());
  EXPECT_STREQ(s.foundName(), "wrongboard");
}

// --- Rule: chunk boundaries splitting the magic or name still detect --------

TEST(FirmwareBoardTagScanner, ChunkBoundaryInsideMagicStillDetectsMismatch) {
  // Place the tag so every possible split of the 20-byte magic lands on a
  // chunk boundary when fed with chunkSize == magicLen - 1.
  const std::string body = imageWithTag("splitmagic", 5);
  EXPECT_TRUE(scanInChunks(body, 19));  // splits inside "CROSSPOINT-BOARD-V1:"
}

TEST(FirmwareBoardTagScanner, ChunkBoundaryInsideBoardNameStillDetectsMismatch) {
  const std::string body = imageWithTag("splitname", 0);
  // Magic is 20 bytes; splitting at 23 puts the boundary inside the name.
  EXPECT_TRUE(scanInChunks(body, 23));
}

// --- Rule: partial magic collisions do not false-positive -------------------

TEST(FirmwareBoardTagScanner, PartialMagicPrefixDoesNotTriggerCapture) {
  // Body contains everything but the final ':' of the magic; no tag should form.
  std::string body(256, '\0');
  const char partial[] = "CROSSPOINT-BOARD-V1";  // missing ':'
  std::memcpy(&body[10], partial, sizeof(partial) - 1);
  EXPECT_FALSE(scanInChunks(body, 1));
  EXPECT_FALSE(scanInChunks(body, 4));
}

TEST(FirmwareBoardTagScanner, MagicRestartAfterSingleByteCollision) {
  // "CROSSPOINT-BOARD-V1XCROSSPOINT-BOARD-V1:x4pro;" — the 'X' breaks the first
  // attempt; the scanner must restart and still find the valid tag.
  const std::string thisBoard(board_tag::boardName(), board_tag::boardNameLen());
  std::string body = "CROSSPOINT-BOARD-V1XCROSSPOINT-BOARD-V1:" + thisBoard + ";";
  body.resize(body.size() + 64, '\0');
  EXPECT_FALSE(scanInChunks(body, 1));
}

// --- Rule: overlong or non-printable names are ignored ----------------------

TEST(FirmwareBoardTagScanner, OverlongNameIsTreatedAsNoise) {
  // 30 printable chars between ':' and ';' exceeds MAX_NAME (23).
  std::string body(256, '\0');
  const std::string longName(30, 'A');
  const std::string tag = "CROSSPOINT-BOARD-V1:" + longName + ";";
  std::memcpy(&body[0], tag.data(), tag.size());
  EXPECT_FALSE(scanInChunks(body, 1));
}

TEST(FirmwareBoardTagScanner, NonPrintableByteInNameResetsCapture) {
  std::string body(256, '\0');
  std::string tag = "CROSSPOINT-BOARD-V1:abc\x01def;";
  std::memcpy(&body[0], tag.data(), tag.size());
  EXPECT_FALSE(scanInChunks(body, 1));
}

// --- Rule: only the first mismatch matters ----------------------------------

TEST(FirmwareBoardTagScanner, FirstMismatchWinsWhenTwoTagsArePresent) {
  // Two tags back-to-back: the first names a wrong board, the second names the
  // running board. The scanner must report the first and stop.
  const std::string thisBoard(board_tag::boardName(), board_tag::boardNameLen());
  std::string body = "CROSSPOINT-BOARD-V1:firstwrong;CROSSPOINT-BOARD-V1:" + thisBoard + ";";
  body.resize(body.size() + 64, '\0');

  board_tag::Scanner s;
  s.feed(reinterpret_cast<const uint8_t*>(body.data()), body.size());
  ASSERT_TRUE(s.mismatch());
  EXPECT_STREQ(s.foundName(), "firstwrong");
}

// --- Rule: empty / null feeds are safe no-ops -------------------------------

TEST(FirmwareBoardTagScanner, EmptyFeedIsSafe) {
  board_tag::Scanner s;
  s.feed(nullptr, 0);
  EXPECT_FALSE(s.mismatch());
  const uint8_t dummy = 0;
  s.feed(&dummy, 0);
  EXPECT_FALSE(s.mismatch());
}