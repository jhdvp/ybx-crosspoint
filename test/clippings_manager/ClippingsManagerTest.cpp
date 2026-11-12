#include <HalStorage.h>
#include <clippings/ClippingsManager.h>
#include <gtest/gtest.h>

#include <string>

#include "HalClock.h"

namespace {
struct Fixture : testing::Test {
  void SetUp() override {
    fake::reset();
    fake_clock::available = true;
    fake_clock::requested = false;
    // A stable "Added on Saturday, November 7, 2026, 02:03 PM" timestamp.
    fake_clock::local = {};
    fake_clock::local.tm_year = 126;  // 2026
    fake_clock::local.tm_mon = 10;    // November
    fake_clock::local.tm_mday = 7;
    fake_clock::local.tm_hour = 14;
    fake_clock::local.tm_min = 3;
    fake_clock::local.tm_wday = 6;  // Saturday
  }
  std::string text() {
    const auto& bytes = fake::files.at(ClippingsManager::CLIPPINGS_PATH)->bytes;
    return {bytes.begin(), bytes.end()};
  }
};

const std::string KINDLE_HEADER =
    "Book Title (Author Name)\n"
    "- Your Highlight on Page 12 | Chapter One | Added on Saturday, November 7, 2026, 02:03 PM\n";

struct tm midnightClock() {
  struct tm local{};
  local.tm_year = 126;
  local.tm_mon = 10;
  local.tm_mday = 7;
  local.tm_hour = 0;
  local.tm_min = 0;
  local.tm_wday = 6;
  return local;
}

std::string longTextWithUtf8Tail(const size_t boundary) {
  std::string text(3000, 'a');
  text[boundary] = char(0xC3);  // two-byte lead for é
  text[boundary + 1] = char(0xA9);
  return text;
}

}  // namespace

TEST_F(Fixture, FormatsKindleEntryWithSeparator) {
  EXPECT_TRUE(ClippingsManager::saveClipping("Book Title", "Author Name", "Chapter One", 12, "quoted text"));
  EXPECT_EQ(text(), KINDLE_HEADER + "\nquoted text\n==========\n");
}

TEST_F(Fixture, MidnightClockAppendsAmAndEmptyChapterOmitsChapter) {
  fake_clock::local = midnightClock();
  EXPECT_TRUE(ClippingsManager::saveClipping("Book", "Author", "", 3, "quote"));
  EXPECT_EQ(text(),
            "Book (Author)\n- Your Highlight on Page 3 | Added on Saturday, November 7, 2026, 12:00 AM\n"
            "\nquote\n==========\n");
}

TEST_F(Fixture, MissingClockOmitsAddedOnSuffix) {
  fake_clock::available = false;
  EXPECT_TRUE(ClippingsManager::saveClipping("Book", "Author", "Chapter", 3, "quote"));
  EXPECT_EQ(text(), "Book (Author)\n- Your Highlight on Page 3 | Chapter\n\nquote\n==========\n");
}

TEST_F(Fixture, AppendsWithoutTouchingEarlierEntries) {
  fake::add(ClippingsManager::CLIPPINGS_PATH, "old entry\n==========\n");
  EXPECT_TRUE(ClippingsManager::saveClipping("Next", "Book", "Chapter", 4, "new"));
  EXPECT_TRUE(text().starts_with("old entry\n==========\n"));
  EXPECT_TRUE(
      text().ends_with("Next (Book)\n- Your Highlight on Page 4 | Chapter | Added on Saturday, November 7, "
                       "2026, 02:03 PM\n\nnew\n==========\n"));
}

TEST_F(Fixture, TruncatesLongTextKeepingWholeUtf8Sequences) {
  // The export keeps a bounded prefix of the selection; a charset split exactly
  // at the boundary must not produce a broken codepoint.
  // A probe entry measures this entry's header length (same metadata).
  ASSERT_TRUE(ClippingsManager::saveClipping("Book", "Author", "Chapter", 1, ""));
  const size_t headerBytes = text().size() - 12;  // minus "\n==========\n"
  const size_t probeBytes = text().size();
  EXPECT_TRUE(ClippingsManager::saveClipping("Book", "Author", "Chapter", 1, longTextWithUtf8Tail(1998)));
  const std::string body = text();
  // The 1998 letters plus the complete two-byte sequence survive.
  EXPECT_EQ(body.find(std::string(1998, 'a') + std::string("\xC3\xA9")), probeBytes + headerBytes);
  EXPECT_EQ(body.find(std::string(3000, 'a')), std::string::npos);
}

TEST_F(Fixture, TruncatesLongTextDroppingSplitSequence) {
  // A lead byte at the final position is incomplete: it is dropped instead of
  // written as mojibake.
  ASSERT_TRUE(ClippingsManager::saveClipping("Book", "Author", "Chapter", 1, ""));
  const size_t headerBytes = text().size() - 12;
  const size_t probeBytes = text().size();
  EXPECT_TRUE(ClippingsManager::saveClipping("Book", "Author", "Chapter", 1, longTextWithUtf8Tail(1999)));
  const std::string body = text();
  EXPECT_EQ(body.find(std::string(1999, 'a')), probeBytes + headerBytes);
  EXPECT_EQ(body.substr(probeBytes + headerBytes + 1999, 1), std::string("\n"));
}

TEST_F(Fixture, FailedWriteRollsBackToOriginalSize) {
  fake::add(ClippingsManager::CLIPPINGS_PATH, "old entry\n==========\n");
  fake::failWritePath = ClippingsManager::CLIPPINGS_PATH;
  EXPECT_FALSE(ClippingsManager::saveClipping("Book", "Author", "Chapter", 1, "text"));
  EXPECT_EQ(text(), "old entry\n==========\n");
}

TEST_F(Fixture, ShortWriteAlsoRollsBack) {
  fake::add(ClippingsManager::CLIPPINGS_PATH, "old entry\n==========\n");
  fake::shortWrite = 10;
  EXPECT_FALSE(ClippingsManager::saveClipping("Book", "Author", "Chapter", 1, "text"));
  EXPECT_EQ(text(), "old entry\n==========\n");
}

TEST_F(Fixture, FailedRollbackStillReportsFailure) {
  fake::add(ClippingsManager::CLIPPINGS_PATH, "old entry\n==========\n");
  // A short write leaves partial bytes behind, so the failed rollback is
  // observable; a zero-byte write alone could not detect a missing truncate.
  fake::shortWrite = 10;
  fake::failTruncate = 0;
  EXPECT_FALSE(ClippingsManager::saveClipping("Book", "Author", "Chapter", 1, "text"));
  EXPECT_TRUE(fake::failureTriggered);
  EXPECT_EQ(text(), "old entry\n==========\nBook (Auth");
}
