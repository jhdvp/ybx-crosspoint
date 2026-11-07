#include <gtest/gtest.h>

#include "nativeapps/notes/NotesDocument.h"

using namespace nativeapps::notes;

TEST(NativeNotes, PortableMarkdownRoundTrip) {
  Document doc{"Планы YBX", "## Сегодня\n* Сделать заметки\nКод `test`"};
  std::string encoded;
  ASSERT_TRUE(encode(doc, encoded));
  EXPECT_EQ(encoded.substr(0, 2), "# ");
  Document output;
  ASSERT_TRUE(decode(encoded, output));
  EXPECT_EQ(output.title, doc.title);
  EXPECT_EQ(output.body, doc.body);
}
TEST(NativeNotes, CanEditAndPreserveEndingNewlines) {
  Document doc{"Title", "Hello\n\n"};
  std::string saved;
  ASSERT_TRUE(encode(doc, saved));
  Document restored;
  ASSERT_TRUE(decode(saved, restored));
  EXPECT_EQ(restored.body, doc.body);
}
TEST(NativeNotes, RejectsUnboundedTitleAndBodyWithoutMutatingTarget) {
  Document out{"Original", "Content"};
  EXPECT_FALSE(decode("# invalid\nno separator\n", out));
  EXPECT_EQ(out.title, "Original");
  EXPECT_FALSE(validDocument(Document{std::string(97, 'a'), ""}));
  EXPECT_FALSE(validDocument(Document{"Title", std::string(4097, 'b')}));
  EXPECT_FALSE(validDocument(Document{" \t", "body"}));
  EXPECT_FALSE(validDocument(Document{"", "body"}));
}
TEST(NativeNotes, RejectsMalformedUtf8AndControlCharacters) {
  EXPECT_FALSE(validDocument(Document{"bad\nheader", "body"}));
  EXPECT_FALSE(validDocument(Document{"bad\rheader", "body"}));
  EXPECT_FALSE(validDocument(Document{std::string("bad\0x", 5), "body"}));
  EXPECT_FALSE(validDocument(Document{"Title", std::string("\xC0\xAF", 2)}));
  EXPECT_FALSE(validDocument(Document{"Title", std::string("\xED\xA0\x80", 3)}));
  EXPECT_FALSE(validDocument(Document{"Title", std::string("\xF4\x90\x80\x80", 4)}));
  EXPECT_FALSE(validDocument(Document{"Title", std::string("\xE2\x82", 2)}));
  EXPECT_TRUE(validDocument(Document{"Title", "line\nnew\ttext"}));
}
TEST(NativeNotes, RejectsMissingOrBrokenHeader) {
  Document doc;
  EXPECT_FALSE(decode("Title\n\nBody\n", doc));
  EXPECT_FALSE(decode("# Title\r\n\nBody\n", doc));
  EXPECT_FALSE(decode("# Title\nBody\n", doc));
  EXPECT_FALSE(decode("# Title\n\nBody", doc));
  EXPECT_FALSE(decode("# Title\n\nBody\0\n", doc));
}
TEST(NativeNotes, OpaqueStableIdNotDerivedFromTitle) {
  const std::string name = fileName(0x00112233u, 0x44556677u);
  EXPECT_EQ(name, "note-0011223344556677.md");
  EXPECT_TRUE(validFileName(name));
  EXPECT_EQ(pathFor(name), "/apps/notes/note-0011223344556677.md");
  EXPECT_NE(fileName(1, 2), fileName(1, 3));
}
TEST(NativeNotes, FilenameCannotTraverseOrUseTemporarySuffix) {
  for (const char* name : {"../../bad.md", "note-0011223344556677.md.tmp", "note-0011223344556677.md.bak",
                           "note-0011223344556677.txt", "note-001122334455667g.md", "note-0011223344556677.MD", ""}) {
    EXPECT_FALSE(validFileName(name)) << name;
    EXPECT_TRUE(pathFor(name).empty());
  }
}
TEST(NativeNotes, MaximumBodyRoundTrip) {
  Document doc{"T", std::string(kMaxBodyBytes, 'a')};
  std::string bytes;
  ASSERT_TRUE(encode(doc, bytes));
  EXPECT_LE(bytes.size(), kMaxFileBytes);
  Document again;
  ASSERT_TRUE(decode(bytes, again));
  EXPECT_EQ(again.body.size(), kMaxBodyBytes);
}
