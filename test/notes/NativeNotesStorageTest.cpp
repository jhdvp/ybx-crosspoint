#include <gtest/gtest.h>

#include "HalStorage.h"
#include "nativeapps/notes/NotesDocument.h"
#include "nativeapps/notes/NotesStorage.h"

using namespace nativeapps::notes;

class NativeNotesStorage : public ::testing::Test {
 protected:
  void SetUp() override { Storage.reset(); }
  const std::string name = "note-1234567890abcdef.md";
  std::string path() const { return pathFor(name); }
  Document original{"First", "Body"};
};

TEST_F(NativeNotesStorage, CreateAndReadRoundTripWithoutTitleBasedFileName) {
  std::string newName;
  ASSERT_TRUE(create(original, newName));
  ASSERT_TRUE(validFileName(newName));
  EXPECT_NE(newName, original.title + ".md");
  Document loaded;
  ASSERT_TRUE(load(newName, loaded));
  EXPECT_EQ(loaded.title, "First");
  EXPECT_EQ(loaded.body, "Body");
}
TEST_F(NativeNotesStorage, EditKeepsBackupAndCanonicalValid) {
  ASSERT_TRUE(save(name, original));
  ASSERT_TRUE(save(name, Document{"Updated", "New text"}));
  Document backup, current;
  ASSERT_TRUE(decode(Storage.files.at(path() + ".bak"), backup));
  EXPECT_EQ(backup.title, "First");
  ASSERT_TRUE(load(name, current));
  EXPECT_EQ(current.title, "Updated");
}
TEST_F(NativeNotesStorage, ShortWritePreservesCommittedFile) {
  ASSERT_TRUE(save(name, original));
  Storage.shortWrite = true;
  EXPECT_FALSE(save(name, Document{"New", "Second"}));
  EXPECT_TRUE(Storage.exists(path().c_str()));
  Document current;
  ASSERT_TRUE(load(name, current));
  EXPECT_EQ(current.title, "First");
}
TEST_F(NativeNotesStorage, InvalidExistingFilePreservesPriorBackupOnUpdate) {
  Document doc{"Old", "Saved"};
  std::string saved;
  ASSERT_TRUE(encode(doc, saved));
  Storage.files[path() + ".bak"] = saved;
  Storage.files[path()] = "corrupt final";
  EXPECT_FALSE(save(name, Document{"New", "X"}));
  EXPECT_EQ(Storage.files.at(path() + ".bak"), saved);
  EXPECT_EQ(Storage.files.at(path()), "corrupt final");
}
TEST_F(NativeNotesStorage, FailedFinalRenameRollsBackPreviousRevision) {
  ASSERT_TRUE(save(name, original));
  Storage.blockFinalRename = true;
  EXPECT_FALSE(save(name, Document{"Updated", "Body2"}));
  Storage.blockFinalRename = false;
  Document current;
  ASSERT_TRUE(load(name, current));
  EXPECT_EQ(current.title, "First");
}
TEST_F(NativeNotesStorage, MissingCanonicalRecoversBackupOnBoot) {
  std::string encoded;
  ASSERT_TRUE(encode(original, encoded));
  Storage.files[path() + ".bak"] = encoded;
  Document current;
  ASSERT_TRUE(load(name, current));
  EXPECT_EQ(current.title, "First");
  EXPECT_TRUE(Storage.exists(path().c_str()));
  EXPECT_FALSE(Storage.exists((path() + ".bak").c_str()));
}
TEST_F(NativeNotesStorage, TempFileAloneIsNeverPromotedOnBoot) {
  std::string encoded;
  ASSERT_TRUE(encode(original, encoded));
  Storage.files[path() + ".tmp"] = encoded;
  Document current;
  EXPECT_FALSE(load(name, current));
  EXPECT_FALSE(Storage.exists(path().c_str()));
}
TEST_F(NativeNotesStorage, DeleteCannotResurrectFromBackup) {
  ASSERT_TRUE(save(name, original));
  ASSERT_TRUE(save(name, Document{"New", "Changed"}));
  ASSERT_TRUE(erase(name));
  EXPECT_FALSE(Storage.exists(path().c_str()));
  EXPECT_FALSE(Storage.exists((path() + ".bak").c_str()));
  Document current;
  EXPECT_FALSE(load(name, current));
}
TEST_F(NativeNotesStorage, AbsentOrOfflineSDDoesNotCreateNote) {
  Storage.online = false;
  std::string nameOut;
  EXPECT_FALSE(create(original, nameOut));
  EXPECT_TRUE(nameOut.empty());
  EXPECT_TRUE(list().failed);
}
TEST_F(NativeNotesStorage, FileScanBoundedWithHundredsOfEntries) {
  std::string encoded;
  ASSERT_TRUE(encode(original, encoded));
  for (unsigned n = 0; n < 250; ++n) Storage.files[pathFor(fileName(n, n))] = encoded;
  auto result = list();
  EXPECT_FALSE(result.failed);
  EXPECT_TRUE(result.truncated);
  EXPECT_LE(result.rows.size(), kMaxNotes);
}
TEST_F(NativeNotesStorage, BackupRenameFailurePreservesCanonical) {
  ASSERT_TRUE(save(name, original));
  Storage.blockBackupRename = true;
  EXPECT_FALSE(save(name, Document{"New", "X"}));
  Storage.blockBackupRename = false;
  Document current;
  ASSERT_TRUE(load(name, current));
  EXPECT_EQ(current.title, "First");
}
TEST_F(NativeNotesStorage, FailedDeleteKeepsCanonical) {
  ASSERT_TRUE(save(name, original));
  Storage.blockDelete = true;
  EXPECT_FALSE(erase(name));
  EXPECT_TRUE(Storage.exists(path().c_str()));
}

TEST_F(NativeNotesStorage, FailedFlushCloseCannotRotateCanonical) {
  ASSERT_TRUE(save(name, original));
  Storage.closeFailed = true;
  EXPECT_FALSE(save(name, Document{"Updated", "Text"}));
  Storage.closeFailed = false;
  Document current;
  ASSERT_TRUE(load(name, current));
  EXPECT_EQ(current.title, "First");
}
TEST_F(NativeNotesStorage, FailedVerificationReadCannotRotateCanonical) {
  ASSERT_TRUE(save(name, original));
  Storage.shortRead = true;
  EXPECT_FALSE(save(name, Document{"Updated", "Text"}));
  Storage.shortRead = false;
  Document current;
  ASSERT_TRUE(load(name, current));
  EXPECT_EQ(current.title, "First");
}
