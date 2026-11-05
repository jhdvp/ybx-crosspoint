#include <gtest/gtest.h>

#include <string>

#include "util/YbxDocumentIdentity.h"
#include "util/YbxNoteRevision.h"

namespace {
constexpr const char* kNote = "/apps/notes/note-0123456789abcdef.md";
constexpr const char* kOther = "/apps/notes/note-fedcba9876543210.md";
}  // namespace

TEST(YbxDocumentIdentity, ManagedNotesUseStableClippingFilenameHash) {
  EXPECT_TRUE(ybxdocument::isManagedNote(kNote));
  EXPECT_TRUE(ybxdocument::useStableClippingIdentity(kNote));
}

TEST(YbxDocumentIdentity, NativeNoteIdIndependentOfTitleAndBody) {
  const std::string notePath = kNote;
  const std::string original = "# Title\n\nText v1\n";
  const std::string edited = "# Completely Different Title\n\nEdited body v2\n";
  ASSERT_NE(original, edited);
  // Both revisions retain the same note filename: the configured binary
  // content hash may change, but this decision is stable across both.
  EXPECT_TRUE(ybxdocument::useStableClippingIdentity(notePath));
  EXPECT_TRUE(ybxdocument::isManagedNote(notePath));
}

TEST(YbxDocumentIdentity, IndependentNoteIdsAreDifferent) {
  EXPECT_NE(std::string(kNote), std::string(kOther));
  EXPECT_TRUE(ybxdocument::isManagedNote(kOther));
  EXPECT_TRUE(ybxdocument::useStableClippingIdentity(kOther));
}

TEST(YbxDocumentIdentity, OtherMarkdownAndEpubRetainKoSyncPreference) {
  for (const char* path :
       {"/books/report.md", "/books/book.epub", "/apps/notes/draft.md", "/apps/notes/book.epub", "/docs/ordinary.md"}) {
    EXPECT_FALSE(ybxdocument::isManagedNote(path)) << path;
    EXPECT_FALSE(ybxdocument::useStableClippingIdentity(path)) << path;
  }
}

TEST(YbxDocumentIdentity, BadNamesAndPathTraversalNeverForceIdentity) {
  for (const char* path : {"", "note-0123456789abcdef.md", "/apps/notes/../note-0123456789abcdef.md",
                           "/apps/notes/note-0123456789abcdef.md/extra", "/apps/notes/note-0123456789ABCDEf.md",
                           "/apps/notes/note-0123456789abcde.md", "/apps/notes/note-0123456789abcdef.MD",
                           "/apps/notes/note-0123456789abcdef.md.bak"}) {
    EXPECT_FALSE(ybxdocument::isManagedNote(path)) << path;
  }
}

TEST(YbxDocumentIdentity, NativeNoteContentRevisionChangesOnSameLengthEdit) {
  constexpr std::string_view a = "# Title\n\nred apple\n";
  constexpr std::string_view b = "# Title\n\ngrn apple\n";
  ASSERT_EQ(a.size(), b.size());
  EXPECT_NE(ybxnote::revisionSignature(a), ybxnote::revisionSignature(b));
  EXPECT_EQ(ybxnote::revisionSignature(a), ybxnote::revisionSignature(a));
  EXPECT_NE(ybxnote::revisionSignature(a), 0u);
}
TEST(YbxDocumentIdentity, NativeNoteRevisionIncludesBodyAndTitle) {
  const auto original = ybxnote::revisionSignature("# Title\n\nText\n");
  EXPECT_NE(original, ybxnote::revisionSignature("# Other\n\nText\n"));
  EXPECT_NE(original, ybxnote::revisionSignature("# Title\n\nElse\n"));
}

TEST(YbxDocumentIdentity, NativeNoteUsesFilenameKeyAcrossContentEdits) {
  const auto a = ybxdocument::clippingDocumentKey(kNote, "binary-hash-v1", [] { return "note-filename-hash"; });
  const auto b = ybxdocument::clippingDocumentKey(kNote, "binary-hash-v2", [] { return "note-filename-hash"; });
  EXPECT_EQ(a, "note-filename-hash");
  EXPECT_EQ(a, b);
}
TEST(YbxDocumentIdentity, OrdinaryEpubDoesNotCallExtraFilenameHash) {
  bool called = false;
  const auto result = ybxdocument::clippingDocumentKey("/books/novel.epub", "original-kosync-key", [&] {
    called = true;
    return "wrong-key";
  });
  EXPECT_FALSE(called);
  EXPECT_EQ(result, "original-kosync-key");
}
TEST(YbxDocumentIdentity, UnavailableNativeNoteHashRefusesUnrelatedFallback) {
  const auto result = ybxdocument::clippingDocumentKey(kNote, "binary-changes-per-edit", [] { return ""; });
  EXPECT_TRUE(result.empty());
}

TEST(YbxDocumentIdentity, PreservesNoteIdentityAcrossDirectoryMove) {
  for (const char* p : {"/apps/notes/note-0123456789abcdef.md", "/Read/note-0123456789abcdef.md",
                        "/Transferred/SD 1/note-0123456789abcdef.md"}) {
    ASSERT_TRUE(ybxdocument::isManagedNote(p));
    EXPECT_EQ(ybxdocument::clippingDocumentKey(p, "binary-hash", [] { return "stable-name-hash"; }),
              "stable-name-hash");
  }
  EXPECT_FALSE(ybxdocument::isManagedNote("/Read/../note-0123456789abcdef.md"));
  EXPECT_FALSE(ybxdocument::isManagedNote("/Read//note-0123456789abcdef.md"));
}
