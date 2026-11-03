#include <gtest/gtest.h>
#include <expat.h>
#include <cstdint>
#include <string>
#include <string_view>
#include "Print.h"
#include "TxtToHtml.h"
#include "util/YbxNoteReanchor.h"

namespace {
class StringPrint : public Print {
 public:
  std::string bytes;
  size_t write(uint8_t byte) override { bytes.push_back(static_cast<char>(byte)); return 1; }
  size_t write(const uint8_t* buf, size_t size) override {
    bytes.append(reinterpret_cast<const char*>(buf), size); return size;
  }
};
struct ExpatBody { bool inBody = false; std::string visible; };
void XMLCALL startElement(void* user, const XML_Char* name, const XML_Char**) {
  if (std::string_view(name) == "body") static_cast<ExpatBody*>(user)->inBody = true;
}
void XMLCALL endElement(void* user, const XML_Char* name) {
  if (std::string_view(name) == "body") static_cast<ExpatBody*>(user)->inBody = false;
}
void XMLCALL textNode(void* user, const XML_Char* bytes, int length) {
  auto* body = static_cast<ExpatBody*>(user);
  if (body->inBody) body->visible.append(bytes, static_cast<size_t>(length));
}
std::string xmlVisible(std::string_view source) {
  StringPrint html;
  if (!TxtToHtml::stream("note-0123456789abcdef.md", source, html)) return {};
  XML_Parser parser = XML_ParserCreate("UTF-8");
  if (!parser) return {};
  ExpatBody body;
  XML_SetUserData(parser, &body);
  XML_SetElementHandler(parser, startElement, endElement);
  XML_SetCharacterDataHandler(parser, textNode);
  const bool ok = XML_Parse(parser, html.bytes.data(), static_cast<int>(html.bytes.size()), XML_TRUE) == XML_STATUS_OK;
  XML_ParserFree(parser);
  return ok ? body.visible : std::string{};
}
}

TEST(NativeNoteReanchor, VisibleTextAgreesWithRealXhtmlExpatCharacterData) {
  for (const std::string source : {
         "# Topic\n\nA **unique phrase** in text & <tag>.\n",
         "# Café ☕\n\nЛев *мост* right & left.\n",
         "# Head\n\n- first item\n- second item\n\n> quoted text\n",
         "# Head\n\n~~~\nhello <world> & code\n~~~\n",
         "# Head\n\n[Visit](https://example.com) literal <script>.\n",
       }) {
    std::string actual;
    ASSERT_TRUE(ybxnote::renderVisibleText(source, actual));
    const auto expat = xmlVisible(source);
    ASSERT_FALSE(expat.empty());
    EXPECT_EQ(actual, expat);
  }
}
TEST(NativeNoteReanchor, OnlyOneExactOccurrenceRelocatesToVisibleCodepointSpan) {
  const std::string source = "# Topic\n\nSome **unique phrase** is here.\n";
  std::string visible;
  ASSERT_TRUE(ybxnote::renderVisibleText(source, visible));
  const auto candidate = ybxnote::findUnique(visible, "unique phrase");
  ASSERT_EQ(candidate.status, ybxnote::Match::Found);
  EXPECT_EQ(candidate.end - candidate.start, 13u);
  EXPECT_EQ(candidate.start, ybxnote::codepointCount(visible.substr(0, visible.find("unique phrase"))));
  const ybxnote::Relocation relocation{4, candidate.start, candidate.end};
  EXPECT_TRUE(ybxnote::contains(relocation, 4, candidate.start + 2, candidate.end - 2));
  EXPECT_FALSE(ybxnote::contains(relocation, 5, candidate.start + 2, candidate.end - 2));
  EXPECT_FALSE(ybxnote::contains(relocation, 4, UINT32_MAX, UINT32_MAX));
  EXPECT_FALSE(ybxnote::contains(relocation, 4, candidate.end, candidate.end + 2));
}
TEST(NativeNoteReanchor, RepeatedQuotesRemainAmbiguousEvenWhenSeparated) {
  const std::string source = "# Title\n\nA repeated phrase is here.\n\nHere is repeated phrase again.\n";
  std::string visible;
  ASSERT_TRUE(ybxnote::renderVisibleText(source, visible));
  EXPECT_EQ(ybxnote::findUnique(visible, "repeated phrase").status, ybxnote::Match::Ambiguous);
}
TEST(NativeNoteReanchor, NeverMatchesInsideLongerWords) {
  const std::string source = "# Title\n\nThe superexampleword has a preexamplewordsuffix middle.\n";
  std::string visible;
  ASSERT_TRUE(ybxnote::renderVisibleText(source, visible));
  EXPECT_EQ(ybxnote::findUnique(visible, "exampleword").status, ybxnote::Match::Missing);
}
TEST(NativeNoteReanchor, Utf8CodepointCoordinatesAndEscapedXmlMatch) {
  const std::string source = "# Письмо\n\n🍀 Café & crème <bon> unique.\n";
  std::string visible;
  ASSERT_TRUE(ybxnote::renderVisibleText(source, visible));
  EXPECT_EQ(visible, xmlVisible(source));
  const auto match = ybxnote::findUnique(visible, "Café & crème");
  ASSERT_EQ(match.status, ybxnote::Match::Found);
  EXPECT_EQ(match.end - match.start, 12u);
  EXPECT_EQ(match.start, ybxnote::codepointCount(visible.substr(0, visible.find("Café & crème"))));
}
TEST(NativeNoteReanchor, WhitespaceAndChangedTextDoNotInventLocation) {
  const std::string source = "# Title\n\nOne   unique phrase in this document.\n";
  std::string visible;
  ASSERT_TRUE(ybxnote::renderVisibleText(source, visible));
  EXPECT_EQ(ybxnote::findUnique(visible, "unique phrase x").status, ybxnote::Match::Missing);
  EXPECT_EQ(ybxnote::findUnique(visible, "unique  phrase").status, ybxnote::Match::Missing);
  EXPECT_EQ(ybxnote::findUnique(visible, "unique phrase").status, ybxnote::Match::Found);
}
TEST(NativeNoteReanchor, RejectsShortAndOversizeAndInvalidQuotes) {
  std::string visible;
  ASSERT_TRUE(ybxnote::renderVisibleText("# Title\n\nabcdefghijk quote.\n", visible));
  EXPECT_EQ(ybxnote::findUnique(visible, "abc").status, ybxnote::Match::Unsupported);
  EXPECT_EQ(ybxnote::findUnique(visible, "").status, ybxnote::Match::Unsupported);
  EXPECT_EQ(ybxnote::findUnique(visible, std::string(513, 'x')).status, ybxnote::Match::Unsupported);
  EXPECT_EQ(ybxnote::findUnique(visible, std::string("\xff\xfe", 2)).status, ybxnote::Match::Unsupported);
}
TEST(NativeNoteReanchor, InvalidOrOversizedNativeNotesFailClosed) {
  std::string visible;
  EXPECT_FALSE(ybxnote::renderVisibleText("not a native note", visible));
  EXPECT_FALSE(ybxnote::renderVisibleText("# Valid\n\n" + std::string(4097, 'a') + "\n", visible));
  EXPECT_FALSE(ybxnote::renderVisibleText("# Bad\n\n\xff\n", visible));
}
TEST(NativeNoteReanchor, AChangedTitleAltersMatchingCoordinatesWithoutMutation) {
  std::string oldVisible, newVisible;
  ASSERT_TRUE(ybxnote::renderVisibleText("# Short\n\nA unique example sentence.\n", oldVisible));
  ASSERT_TRUE(ybxnote::renderVisibleText("# A much longer heading\n\nA unique example sentence.\n", newVisible));
  const auto before = ybxnote::findUnique(oldVisible, "unique example sentence");
  const auto after = ybxnote::findUnique(newVisible, "unique example sentence");
  ASSERT_EQ(before.status, ybxnote::Match::Found);
  ASSERT_EQ(after.status, ybxnote::Match::Found);
  EXPECT_NE(before.start, after.start);
  EXPECT_EQ(before.end - before.start, after.end - after.start);
}
