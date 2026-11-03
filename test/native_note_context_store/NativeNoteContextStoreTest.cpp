#include <gtest/gtest.h>
#include "util/YbxNoteContextStore.h"
#include "HalStorage.h"

namespace ctx = ybxnote::contextstore;

class NativeNoteContextStore : public ::testing::Test {
 protected:
  void SetUp() override { Storage.reset(); }
  std::string path = "/.crosspoint/clippings/epub_100.bin.anchors";
  const std::string id1 = "0123456789abcdef0123456789abcdef";
  const std::string id2 = "abcdef0123456789abcdef0123456789";
  const std::string quote1 = "original quotation";
  const std::string quote2 = "changed second quotation";
  ybxnote::Context a{"prefix: left", "suffix: right"};
  ybxnote::Context b{"старое 🍀", "будущее дерево"};
};

TEST_F(NativeNoteContextStore, NewFileReadAndIdempotentRetry) {
  ybxnote::Context read;
  EXPECT_EQ(ctx::read(path, id1, quote1, read), ctx::Status::Missing);
  ASSERT_TRUE(ctx::save(path, id1, quote1, a));
  EXPECT_EQ(ctx::read(path, id1, quote1, read), ctx::Status::Found);
  EXPECT_EQ(read.prefix, a.prefix);
  EXPECT_EQ(read.suffix, a.suffix);
  const auto original = Storage.files.at(path);
  EXPECT_TRUE(ctx::save(path, id1, quote1, a));
  EXPECT_EQ(Storage.files.at(path), original);
  EXPECT_FALSE(ctx::save(path, id1, quote1, b));  // immutable ID -> context
  EXPECT_EQ(Storage.files.at(path), original);
}

TEST_F(NativeNoteContextStore, AppendingPreservesPreviousAndUnicode) {
  ASSERT_TRUE(ctx::save(path, id1, quote1, a));
  ASSERT_TRUE(ctx::save(path, id2, quote2, b));
  ybxnote::Context read;
  EXPECT_EQ(ctx::read(path, id1, quote1, read), ctx::Status::Found);
  EXPECT_EQ(read.prefix, a.prefix);
  EXPECT_EQ(ctx::read(path, id2, quote2, read), ctx::Status::Found);
  EXPECT_EQ(read.prefix, b.prefix);
  EXPECT_EQ(read.suffix, b.suffix);
  EXPECT_EQ(ctx::read(path, "fedcba9876543210fedcba9876543210", quote1, read), ctx::Status::Missing);
}

TEST_F(NativeNoteContextStore, RefuseInvalidIDsAndMissingContext) {
  EXPECT_FALSE(ctx::save(path, "abc", quote1, a));
  EXPECT_FALSE(ctx::save(path, std::string(32, 'A'), quote1, a));
  EXPECT_FALSE(ctx::save(path, id1, quote1, {"", ""}));
  EXPECT_FALSE(ctx::save(path, id1, quote1, {std::string(65, 'x'), "end"}));
  EXPECT_FALSE(ctx::save(path, id1, quote1, {"\xff\xfe", "end"}));
  ybxnote::Context read;
  EXPECT_EQ(ctx::read(path, "invalid", quote1, read), ctx::Status::Missing);
  EXPECT_FALSE(Storage.exists(path.c_str()));
}

TEST_F(NativeNoteContextStore, ShortWriteCannotReplaceExistingAnchorFile) {
  ASSERT_TRUE(ctx::save(path, id1, quote1, a));
  const auto canonical = Storage.files.at(path);
  Storage.shortWrite = true;
  EXPECT_FALSE(ctx::save(path, id2, quote2, b));
  EXPECT_EQ(Storage.files.at(path), canonical);
  Storage.shortWrite = false;
  ybxnote::Context restored;
  EXPECT_EQ(ctx::read(path, id1, quote1, restored), ctx::Status::Found);
}

TEST_F(NativeNoteContextStore, ReadAndCloseErrorFailClosed) {
  ASSERT_TRUE(ctx::save(path, id1, quote1, a));
  Storage.shortRead = true;
  ybxnote::Context out;
  EXPECT_EQ(ctx::read(path, id1, quote1, out), ctx::Status::Invalid);
  EXPECT_FALSE(ctx::save(path, id2, quote2, b));
  Storage.shortRead = false;
  Storage.closeFailed = true;
  EXPECT_EQ(ctx::read(path, id1, quote1, out), ctx::Status::Invalid);
  EXPECT_FALSE(ctx::save(path, id2, quote2, b));
}

TEST_F(NativeNoteContextStore, RenameFailuresNeverOverwriteOldCanonical) {
  ASSERT_TRUE(ctx::save(path, id1, quote1, a));
  const auto canonical = Storage.files.at(path);
  Storage.blockBackupRename = true;
  EXPECT_FALSE(ctx::save(path, id2, quote2, b));
  EXPECT_EQ(Storage.files.at(path), canonical);
  Storage.blockBackupRename = false;
  Storage.blockFinalRename = true;
  EXPECT_FALSE(ctx::save(path, id2, quote2, b));
  EXPECT_EQ(Storage.files.at(path), canonical);
  EXPECT_FALSE(Storage.exists((path + ".bak").c_str()));
}

TEST_F(NativeNoteContextStore, RecoversOnlyValidBackupWhenCanonicalMissing) {
  ASSERT_TRUE(ctx::save(path, id1, quote1, a));
  Storage.files[path + ".bak"] = Storage.files.at(path);
  Storage.files.erase(path);
  ybxnote::Context c;
  EXPECT_EQ(ctx::read(path, id1, quote1, c), ctx::Status::Found);
  EXPECT_EQ(c.prefix, a.prefix);
  EXPECT_TRUE(Storage.exists(path.c_str()));
  EXPECT_FALSE(Storage.exists((path + ".bak").c_str()));
}

TEST_F(NativeNoteContextStore, InvalidOrPartialBackupMustNotRelocate) {
  Storage.files[path + ".bak"] = "corrupt";
  ybxnote::Context c;
  EXPECT_EQ(ctx::read(path, id1, quote1, c), ctx::Status::Invalid);
  EXPECT_FALSE(ctx::save(path, id2, quote2, b));
  EXPECT_FALSE(Storage.exists(path.c_str()));
}

TEST_F(NativeNoteContextStore, CorruptHeaderOrChecksumDoesNotFallBack) {
  ASSERT_TRUE(ctx::save(path, id1, quote1, a));
  ybxnote::Context c;
  Storage.files[path][0] ^= 0x01;
  EXPECT_EQ(ctx::read(path, id1, quote1, c), ctx::Status::Invalid);
  Storage.reset();
  ASSERT_TRUE(ctx::save(path, id1, quote1, a));
  Storage.files[path].back() ^= 0x01;
  EXPECT_EQ(ctx::read(path, id1, quote1, c), ctx::Status::Invalid);
}

TEST_F(NativeNoteContextStore, TruncatedRecordAndInterruptedTempAreInvalid) {
  ASSERT_TRUE(ctx::save(path, id1, quote1, a));
  Storage.files[path].pop_back();
  ybxnote::Context c;
  EXPECT_EQ(ctx::read(path, id1, quote1, c), ctx::Status::Invalid);
  Storage.reset();
  Storage.files[path + ".tmp"] = "partially written";
  EXPECT_EQ(ctx::read(path, id1, quote1, c), ctx::Status::Invalid);
}

TEST_F(NativeNoteContextStore, MaximumCountIsBoundedAndNeverCorruptsExisting) {
  for (unsigned i = 0; i < ctx::kMaxRecords; ++i) {
    char id[33];
    std::snprintf(id, sizeof(id), "%032x", i);
    ASSERT_TRUE(ctx::save(path, id, quote1, a)) << i;
  }
  const auto canonical = Storage.files.at(path);
  EXPECT_FALSE(ctx::save(path, "ffffffffffffffffffffffffffffffff", quote1, b));
  EXPECT_EQ(Storage.files.at(path), canonical);
}

TEST_F(NativeNoteContextStore, CorruptAnchorNeverDowngradesToTextOnlyMatch) {
  std::string visible;
  ASSERT_TRUE(ybxnote::renderVisibleText("# T\n\nOnly unique phrase is here.\n", visible));
  const std::string quote = "unique phrase";
  const ybxnote::Context unrelated{"different preceding", "different following"};
  EXPECT_EQ(ctx::resolve(visible, quote, ctx::Status::Invalid, unrelated).status, ybxnote::Match::Unsupported);
  EXPECT_EQ(ctx::resolve(visible, quote, ctx::Status::Found, unrelated).status, ybxnote::Match::Missing);
  EXPECT_EQ(ctx::resolve(visible, quote, ctx::Status::Missing, unrelated).status, ybxnote::Match::Found);
}


TEST_F(NativeNoteContextStore, RemoteQuoteUpdateWithSameIdCannotUseOldContext) {
  ASSERT_TRUE(ctx::save(path, id1, quote1, a));
  ybxnote::Context c;
  EXPECT_EQ(ctx::read(path, id1, "edited quotation", c), ctx::Status::Invalid);
  EXPECT_FALSE(ctx::save(path, id1, "edited quotation", a));
  EXPECT_EQ(ctx::read(path, id1, quote1, c), ctx::Status::Found);
  EXPECT_EQ(c.prefix, a.prefix);
}


TEST_F(NativeNoteContextStore, LegacyNonLocalIdsKeepPreviousUniqueQuoteFallback) {
  ybxnote::Context c;
  EXPECT_EQ(ctx::read(path, std::string(64, 'a'), quote1, c), ctx::Status::Missing);
  EXPECT_EQ(ctx::read(path, "", quote1, c), ctx::Status::Missing);
  ASSERT_TRUE(ctx::save(path, id1, quote1, a));
  EXPECT_EQ(ctx::read(path, std::string(64, 'a'), quote1, c), ctx::Status::Missing);
  EXPECT_EQ(ctx::read(path, "", quote1, c), ctx::Status::Missing);
  Storage.files[path][0] ^= 0x01;
  EXPECT_EQ(ctx::read(path, std::string(64, 'a'), quote1, c), ctx::Status::Invalid);
}
