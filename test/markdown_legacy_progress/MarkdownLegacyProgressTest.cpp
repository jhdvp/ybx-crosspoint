#include <gtest/gtest.h>

#include "HalStorage.h"
#include "activities/reader/MarkdownLegacyProgress.h"

namespace ml = markdownlegacy;

class MarkdownLegacyProgress : public ::testing::Test {
 protected:
  void SetUp() override { Storage.reset(); }
  const std::string dir = "/.crosspoint/epub_12345";
  const std::string originalPath = dir + "/progress.bin";
  const std::string snapshotPath = dir + ml::kBackupSuffix;
  const std::string indexPath = dir + "/book.bin";
  const std::string markerPath = dir + "/markdown-index-v2.fnv";

  void oldIndex(const uint16_t page = 14, const uint16_t total = 40, bool withOffset = true) {
    Storage.files[indexPath] = "old-bin";
    std::string raw;
    raw.push_back(0); raw.push_back(0); // spine 0
    raw.push_back(static_cast<char>(page & 0xff)); raw.push_back(static_cast<char>(page >> 8));
    raw.push_back(static_cast<char>(total & 0xff)); raw.push_back(static_cast<char>(total >> 8));
    if (withOffset) raw += std::string(4, '\x12');
    Storage.files[originalPath] = raw;
  }
};

TEST_F(MarkdownLegacyProgress, NoPreviousCacheOrProgressDoesNothing) {
  EXPECT_EQ(ml::prepare(dir, 4096).action, ml::Action::None);
  Storage.files[indexPath] = "cache";
  EXPECT_EQ(ml::prepare(dir, 4096).action, ml::Action::None);
  EXPECT_FALSE(Storage.exists(snapshotPath.c_str()));
}
TEST_F(MarkdownLegacyProgress, ExistingV2IndexDoesNotModifyProgress) {
  oldIndex();
  Storage.files[markerPath] = "md2:abcdefgh12345678";
  EXPECT_EQ(ml::prepare(dir, 4096).action, ml::Action::None);
  EXPECT_FALSE(Storage.exists(snapshotPath.c_str()));
}
TEST_F(MarkdownLegacyProgress, V1SixByteProgressGetsVerifiedSnapshotAndRatio) {
  oldIndex(12, 40, false);
  const auto action = ml::prepare(dir, 8000);
  ASSERT_EQ(action.action, ml::Action::Approximate);
  EXPECT_EQ(action.savedPage, 12);
  EXPECT_EQ(action.savedTotal, 40);
  EXPECT_FLOAT_EQ(action.fraction(), 0.3f);
  EXPECT_EQ(Storage.files.at(originalPath), Storage.files.at(snapshotPath));
}
TEST_F(MarkdownLegacyProgress, V1TenByteProgressSnapshotPreservesOffsetExactly) {
  oldIndex(19, 40);
  const auto plan = ml::prepare(dir, 8000);
  EXPECT_EQ(plan.action, ml::Action::Approximate);
  EXPECT_EQ(Storage.files.at(snapshotPath).size(), 10u);
  EXPECT_EQ(Storage.files.at(snapshotPath), Storage.files.at(originalPath));
}
TEST_F(MarkdownLegacyProgress, FourByteOldProgressSafelyRestartsWithSnapshot) {
  oldIndex(30, 50);
  Storage.files[originalPath].resize(4);
  const auto plan = ml::prepare(dir, 1024);
  EXPECT_EQ(plan.action, ml::Action::Restart);
  EXPECT_EQ(Storage.files.at(snapshotPath), Storage.files.at(originalPath));
}
TEST_F(MarkdownLegacyProgress, VeryLargeMarkdownRestartsWithoutForcedFullPagination) {
  oldIndex();
  const auto plan = ml::prepare(dir, ml::kApproximateMaxSourceBytes + 1);
  EXPECT_EQ(plan.action, ml::Action::Restart);
  EXPECT_TRUE(Storage.exists(snapshotPath.c_str()));
}
TEST_F(MarkdownLegacyProgress, InvalidPageAndUnusableTotalRestart) {
  oldIndex(UINT16_MAX, 10);
  EXPECT_EQ(ml::prepare(dir, 100).action, ml::Action::Restart);
  Storage.reset();
  oldIndex(30, 0);
  EXPECT_EQ(ml::prepare(dir, 100).action, ml::Action::Restart);
}
TEST_F(MarkdownLegacyProgress, ClampsOutOfRangeButNeverUsesStaleTextOffset) {
  oldIndex(600, 40);
  const auto plan = ml::prepare(dir, 4000);
  EXPECT_EQ(plan.action, ml::Action::Approximate);
  EXPECT_LT(plan.fraction(), 1.0f);
  EXPECT_GE(plan.fraction(), 0.0f);
}
TEST_F(MarkdownLegacyProgress, ReattemptWithIdenticalSnapshotIsIdempotent) {
  oldIndex();
  EXPECT_EQ(ml::prepare(dir, 100).action, ml::Action::Approximate);
  const std::string backup = Storage.files.at(snapshotPath);
  EXPECT_EQ(ml::prepare(dir, 100).action, ml::Action::Approximate);
  EXPECT_EQ(Storage.files.at(snapshotPath), backup);
}
TEST_F(MarkdownLegacyProgress, ConflictingBackupNeverGetsOverwritten) {
  oldIndex();
  Storage.files[snapshotPath] = std::string(10, 'x');
  EXPECT_EQ(ml::prepare(dir, 100).action, ml::Action::Failed);
  EXPECT_EQ(Storage.files.at(snapshotPath), std::string(10, 'x'));
  EXPECT_EQ(Storage.files.at(originalPath).size(), 10u);
}
TEST_F(MarkdownLegacyProgress, ShortWriteDoesNotModifyCanonicalOrLeaveUsableBackup) {
  oldIndex();
  const auto original = Storage.files.at(originalPath);
  Storage.shortWrite = true;
  EXPECT_EQ(ml::prepare(dir, 100).action, ml::Action::Failed);
  EXPECT_EQ(Storage.files.at(originalPath), original);
  EXPECT_FALSE(Storage.exists(snapshotPath.c_str()));
}
TEST_F(MarkdownLegacyProgress, CloseFailureNeverCommitsSnapshot) {
  oldIndex();
  Storage.closeFailed = true;
  EXPECT_EQ(ml::prepare(dir, 100).action, ml::Action::Failed);
  EXPECT_FALSE(Storage.exists(snapshotPath.c_str()));
}
TEST_F(MarkdownLegacyProgress, ReadFailureNeverCommitsSnapshot) {
  oldIndex();
  Storage.shortRead = true;
  EXPECT_EQ(ml::prepare(dir, 100).action, ml::Action::Failed);
  EXPECT_FALSE(Storage.exists(snapshotPath.c_str()));
}
TEST_F(MarkdownLegacyProgress, RenameFailureDoesNotChangeCanonicalProgress) {
  oldIndex();
  Storage.blockFinalRename = true;
  EXPECT_EQ(ml::prepare(dir, 100).action, ml::Action::Failed);
  EXPECT_FALSE(Storage.exists(snapshotPath.c_str()));
  EXPECT_TRUE(Storage.exists(originalPath.c_str()));
}
TEST_F(MarkdownLegacyProgress, OfflineSdFailsClosedWithExistingFile) {
  oldIndex();
  Storage.online = false;
  EXPECT_EQ(ml::prepare(dir, 100).action, ml::Action::Failed);
  EXPECT_FALSE(Storage.exists(snapshotPath.c_str()));
}
TEST_F(MarkdownLegacyProgress, BadProgressLengthCannotBeSilentlyMigrated) {
  oldIndex();
  Storage.files[originalPath] = std::string(7, 'x');
  EXPECT_EQ(ml::prepare(dir, 100).action, ml::Action::Failed);
  EXPECT_FALSE(Storage.exists(snapshotPath.c_str()));
}
