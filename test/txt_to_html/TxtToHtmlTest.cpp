#include <expat.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "MarkdownSourceFingerprint.h"
#include "MarkdownStream.h"
#include "Print.h"
#include "TxtToHtml.h"

namespace {

class StringPrint : public Print {
 public:
  std::string str;
  size_t write(uint8_t b) override {
    str.push_back(static_cast<char>(b));
    return 1;
  }
  size_t write(const uint8_t* buffer, size_t size) override {
    str.append(reinterpret_cast<const char*>(buffer), size);
    return size;
  }
};

const std::string kHeader =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<!-- TXT_CACHE_VERSION: 1 -->\n"
    "<!DOCTYPE html>\n<html>\n<head><title>test</title></head>\n<body>\n";
const std::string kFooter = "\n</body>\n</html>\n";

std::string convert(std::string_view content) {
  StringPrint out;
  EXPECT_TRUE(TxtToHtml::stream("test.txt", content, out));
  return out.str;
}

TEST(TxtToHtmlTest, LeadingSpacesIndentation) {
  EXPECT_EQ(convert("  two spaces"), kHeader + "&#160;&#160;two spaces" + kFooter);
  EXPECT_EQ(convert("    four spaces"), kHeader + "&#160;&#160;&#160;&#160;four spaces" + kFooter);
  EXPECT_EQ(convert("Line 1\n   three spaces"), kHeader + "Line 1<br />&#160;&#160;&#160;three spaces" + kFooter);
}

TEST(TxtToHtmlTest, MidLineConsecutiveSpaces) {
  EXPECT_EQ(convert("One space"), kHeader + "One space" + kFooter);
  EXPECT_EQ(convert("Two  spaces"), kHeader + "Two&#160; spaces" + kFooter);
  EXPECT_EQ(convert("Three   spaces"), kHeader + "Three&#160;&#160; spaces" + kFooter);
  EXPECT_EQ(convert("Four    spaces"), kHeader + "Four&#160;&#160;&#160; spaces" + kFooter);
}

TEST(TxtToHtmlTest, PreservesCacheVersionTagsForBothFormats) {
  StringPrint out;
  ASSERT_TRUE(TxtToHtml::stream("test.MD", "text", out));
  EXPECT_EQ(out.str,
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<!-- MD_CACHE_VERSION: 2 -->\n"
            "<!DOCTYPE html>\n<html>\n<head><title>test</title></head>\n<body>\n<p>text</p>\n" +
                kFooter);
  EXPECT_NE(out.str.find(TxtToHtml::cacheVersionTag("test.MD")), std::string::npos);
  EXPECT_NE(convert("text").find(TxtToHtml::cacheVersionTag("test.txt")), std::string::npos);
}

}  // namespace

namespace {

std::string markdown(std::string_view text) {
  StringPrint result;
  EXPECT_TRUE(TxtToHtml::stream("note.MD", text, result));
  return result.str;
}

TEST(MarkdownToHtmlTest, HeadingsHaveDeterministicAnchors) {
  const auto html = markdown("# Heading\n\n## Chapter *Two*\n\n### Third");
  EXPECT_NE(html.find("<h1 id=\"md-h0001\">Heading</h1>"), std::string::npos);
  EXPECT_NE(html.find("<h2 id=\"md-h0002\">Chapter <em>Two</em></h2>"), std::string::npos);
  EXPECT_NE(html.find("<h3 id=\"md-h0003\">Third</h3>"), std::string::npos);
  EXPECT_EQ(html.find("MD_CACHE_VERSION: 1"), std::string::npos);
}

TEST(MarkdownToHtmlTest, FormattingListsAndCodeEscapeRawHtml) {
  const auto html = markdown(
      "Text **bold** and *italic* and `code` <script>\n\n- first\n- second\n\n1. step\n2. step\n\n```cpp\n<b>&\n```");
  EXPECT_NE(html.find("<strong>bold</strong>"), std::string::npos);
  EXPECT_NE(html.find("<em>italic</em>"), std::string::npos);
  EXPECT_NE(html.find("<code>code</code>"), std::string::npos);
  EXPECT_NE(html.find("&lt;script&gt;"), std::string::npos);
  EXPECT_EQ(html.find("<script>"), std::string::npos);
  EXPECT_NE(html.find("<ul>\n<li>first</li>"), std::string::npos);
  EXPECT_NE(html.find("<ol>\n<li>step</li>"), std::string::npos);
  EXPECT_NE(html.find("<pre><code>&lt;b&gt;&amp;\n</code></pre>"), std::string::npos);
}

TEST(MarkdownToHtmlTest, UnclosedCodeFenceClosesAtEndOfDocument) {
  const auto html = markdown("```\nExample <tag>");
  EXPECT_NE(html.find("<pre><code>Example &lt;tag&gt;\n</code></pre>"), std::string::npos);
}

TEST(MarkdownToHtmlTest, ExternalLinksRemainInertTextWithoutRemoteRequests) {
  const auto html = markdown("[Visit](javascript:alert(1)) & [Site](https://example.org?q=1&x=2)");
  EXPECT_EQ(html.find("<a "), std::string::npos);
  EXPECT_EQ(html.find("<script>"), std::string::npos);
  EXPECT_NE(html.find("Visit (javascript:alert(1)"), std::string::npos);
  EXPECT_NE(html.find("https://example.org?q=1&amp;x=2"), std::string::npos);
}

TEST(MarkdownToHtmlTest, QuotedTextAndCrlfAreSafe) {
  const auto html = markdown("> Quote & <tag>\r\n\r\n# Title\r\n");
  EXPECT_NE(html.find("<blockquote><p>Quote &amp; &lt;tag&gt;</p></blockquote>"), std::string::npos);
  EXPECT_NE(html.find("<h1 id=\"md-h0001\">Title</h1>"), std::string::npos);
}

TEST(MarkdownToHtmlTest, LongLineDoesNotAllocateFullLineOrEmitRawHtml) {
  std::string line(32768, 'x');
  line += "<script>\n# After";
  const auto html = markdown(line);
  EXPECT_NE(html.find("<h1 id=\"md-h0001\">After</h1>"), std::string::npos);
  EXPECT_EQ(html.find("<script>"), std::string::npos);
  EXPECT_NE(html.find("&lt;script&gt;"), std::string::npos);
  EXPECT_EQ(html.size() > 32768, true);
}

TEST(MarkdownToHtmlTest, HeadingScannerMatchesHtmlIdsAcrossChunkBoundaries) {
  const std::string input = "\xEF\xBB\xBF# Привет\n\n```\n# no-heading\n```\n## Next **chapter**";
  struct Reader {
    std::string_view data;
    size_t index = 0;
  } reader{input};
  auto readOne = [](void* p, uint8_t* buffer, size_t) {
    auto& r = *static_cast<Reader*>(p);
    if (r.index == r.data.size()) return 0;
    buffer[0] = static_cast<uint8_t>(r.data[r.index++]);
    return 1;
  };
  std::string invisible;
  auto noop = [&](std::string_view value) { invisible.append(value); };
  std::vector<std::string> titles, anchors;
  auto heading = [&](unsigned ordinal, uint8_t level, const std::string& title) {
    titles.push_back(title);
    anchors.push_back(MarkdownStream::anchorFor(ordinal));
    EXPECT_LE(level, 6u);
  };
  EXPECT_TRUE(MarkdownStream::process(&reader, readOne, noop, heading, false));
  EXPECT_TRUE(invisible.empty());
  ASSERT_EQ(titles.size(), 2u);
  EXPECT_EQ(titles[0], "Привет");
  EXPECT_EQ(titles[1], "Next chapter");
  EXPECT_EQ(anchors[0], "md-h0001");
  EXPECT_EQ(anchors[1], "md-h0002");
  const auto html = markdown(input);
  EXPECT_NE(html.find("id=\"md-h0001\""), std::string::npos);
  EXPECT_NE(html.find("id=\"md-h0002\""), std::string::npos);
}

TEST(MarkdownToHtmlTest, ReadChunksFitEmbeddedStackBudget) {
  const std::string input = std::string(126, 'x') + "Привет\n\n# Heading";
  struct Reader {
    std::string_view data;
    size_t offset = 0;
    size_t largestRequest = 0;
  } reader{input};
  auto read = [](void* context, uint8_t* buffer, size_t capacity) {
    auto& reader = *static_cast<Reader*>(context);
    reader.largestRequest = std::max(reader.largestRequest, capacity);
    const size_t count = std::min(capacity, reader.data.size() - reader.offset);
    std::memcpy(buffer, reader.data.data() + reader.offset, count);
    reader.offset += count;
    return static_cast<int>(count);
  };
  StringPrint out;
  ASSERT_TRUE(TxtToHtml::stream("note.MD", &reader, read, out));
  EXPECT_EQ(out.str, markdown(input));
  // MarkdownStream::process feeds the parser a fixed 512-byte stack chunk.
  EXPECT_LE(reader.largestRequest, 512u);
}

TEST(MarkdownToHtmlTest, ReaderErrorIsReported) {
  struct Reader {
    int readCount = 0;
  } reader;
  auto fail = [](void* p, uint8_t* buffer, size_t) {
    auto& r = *static_cast<Reader*>(p);
    if (++r.readCount == 1) {
      buffer[0] = '#';
      return 1;
    }
    return -1;
  };
  StringPrint out;
  EXPECT_FALSE(TxtToHtml::stream("test.md", &reader, fail, out));
}

TEST(MarkdownToHtmlTest, PlainTextRendererUnchanged) {
  EXPECT_EQ(convert("# Not a heading\n- no list"), kHeader + "# Not a heading<br />- no list" + kFooter);
}

}  // namespace

namespace {
TEST(MarkdownToHtmlTest, InvalidUtf8IsReplacedWithoutDestroyingFollowingText) {
  std::string text = "# ";
  text += std::string("\xED\xA0\x80", 3);
  text += " heading\nNext ";
  text += std::string("\xC0\xAF", 2);
  text += " good";
  const std::string html = markdown(text);
  EXPECT_NE(html.find("id=\"md-h0001\""), std::string::npos);
  EXPECT_NE(html.find("? heading"), std::string::npos);
  EXPECT_NE(html.find("Next ?? good"), std::string::npos);
}
TEST(MarkdownToHtmlTest, TruncatedMultibyteEndGetsReplacement) {
  std::string bytes = "Body ";
  bytes += std::string("\xE2\x82", 2);
  const auto html = markdown(bytes);
  EXPECT_NE(html.find("<p>Body ?</p>"), std::string::npos);
}
TEST(MarkdownToHtmlTest, NullAndXmlControlBytesAreSafe) {
  std::string bytes = "# Header\nbody";
  bytes.push_back(0);
  bytes.push_back('\x01');
  const auto html = markdown(bytes);
  EXPECT_NE(html.find("body??"), std::string::npos);
  EXPECT_EQ(html.find('\0'), std::string::npos);
}
}  // namespace

namespace {
TEST(MarkdownToHtmlTest, GeneratedDocumentParsesAsStrictXml) {
  const std::string content =
      "# Заголовок & <x>\n\nText **strong** and [link](https://example.org/?x=1&y=2)\n\n"
      "- a\n- b\n\n```\n<a id=\"unsafe\">\n```\n\n> quote\n";
  const auto result = markdown(content);
  XML_Parser parser = XML_ParserCreate(nullptr);
  ASSERT_NE(parser, nullptr);
  const auto parsed = XML_Parse(parser, result.data(), static_cast<int>(result.size()), XML_TRUE);
  EXPECT_EQ(parsed, XML_STATUS_OK) << XML_ErrorString(XML_GetErrorCode(parser));
  XML_ParserFree(parser);
}
TEST(MarkdownToHtmlTest, TOCIsBoundedTo128Headings) {
  std::string input;
  for (unsigned i = 0; i < 1000; ++i) input += "# Chapter\n";
  struct Reader {
    std::string_view bytes;
    size_t offset = 0;
  } reader{input};
  auto read = [](void* data, uint8_t* buf, size_t len) {
    auto& r = *static_cast<Reader*>(data);
    if (r.offset == r.bytes.size()) return 0;
    const auto n = std::min(len, r.bytes.size() - r.offset);
    std::memcpy(buf, r.bytes.data() + r.offset, n);
    r.offset += n;
    return static_cast<int>(n);
  };
  auto noop = [](std::string_view) {};
  unsigned headings = 0;
  auto onHeading = [&](unsigned ordinal, uint8_t, const std::string&) { EXPECT_EQ(++headings, ordinal); };
  EXPECT_TRUE(MarkdownStream::process(&reader, read, noop, onHeading, false));
  EXPECT_EQ(headings, MarkdownStream::kMaxTocHeadings);
  EXPECT_EQ(MarkdownStream::anchorFor(128), "md-h0128");
}
}  // namespace

namespace {
TEST(MarkdownToHtmlTest, SameLengthEditsInvalidateMarkdownIndexFingerprint) {
  MarkdownSourceFingerprint::Hasher a, b, chunked;
  a.add("# Title\nalpha");
  b.add("# Title\nomega");
  chunked.add("# Title\n");
  chunked.add("alpha");
  EXPECT_EQ(a.value(), chunked.value());
  EXPECT_NE(a.value(), b.value());
  EXPECT_EQ(MarkdownSourceFingerprint::marker(a.value()).substr(0, 4), "md2:");
  EXPECT_EQ(MarkdownSourceFingerprint::marker(a.value()).size(), 20u);
}
}  // namespace
