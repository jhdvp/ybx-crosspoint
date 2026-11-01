#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include "util/YbxAchievements.h"

namespace {

constexpr int64_t OCT_20_2026 = 1792454400;  // 2026-10-20 00:00:00 UTC
constexpr int64_t OCT_19_2026 = 1792368000;  // 2026-10-19 00:00:00 UTC

ybxach::Snapshot parseOk(const std::string& json) {
  ybxach::Snapshot out;
  EXPECT_TRUE(ybxach::parse(json.data(), json.size(), out));
  return out;
}

bool validUtf8(const char* text) {
  const auto* p = reinterpret_cast<const unsigned char*>(text);
  while (*p) {
    int extra = 0;
    if (*p < 0x80) {
      extra = 0;
    } else if ((*p & 0xE0) == 0xC0) {
      extra = 1;
    } else if ((*p & 0xF0) == 0xE0) {
      extra = 2;
    } else if ((*p & 0xF8) == 0xF0) {
      extra = 3;
    } else {
      return false;
    }
    ++p;
    for (int i = 0; i < extra; ++i, ++p) {
      if (*p == '\0' || (*p & 0xC0) != 0x80) return false;
    }
  }
  return true;
}

}  // namespace

TEST(YbxAchievements, ParsesDocumentedDeviceShape) {
  const std::string json = R"({
    "yandex": {
      "received": [
        {"id": "reward-uuid", "title": "Reward title", "description": "10 minutes of reading",
         "earned_at": null, "first_observed_at": 1792454400,
         "image_url": "/api/v1/devices/D/achievements/reward-uuid/image"},
        {"id": "reward-2", "title": "Second", "description": null,
         "earned_at": null, "first_observed_at": null, "image_url": null}
      ],
      "pending": [
        {"id": "pending-1", "title": "Pending one", "description": "Read 5 of 10 books",
         "earned_at": null, "first_observed_at": 1792454400, "image_url": "/x"}
      ]
    },
    "ybx_reader": [],
    "has_more": {"received": true, "pending": false},
    "checked_at": 1792454400
  })";
  const auto snapshot = parseOk(json);
  ASSERT_EQ(snapshot.receivedCount, 2);
  ASSERT_EQ(snapshot.pendingCount, 1);
  EXPECT_TRUE(snapshot.hasMoreReceived);
  EXPECT_FALSE(snapshot.hasMorePending);
  EXPECT_EQ(snapshot.checkedAt, OCT_20_2026);
  EXPECT_FALSE(snapshot.truncated);

  const auto& first = snapshot.received[0];
  EXPECT_STREQ(first.id, "reward-uuid");
  EXPECT_STREQ(first.title, "Reward title");
  EXPECT_STREQ(first.description, "10 minutes of reading");
  EXPECT_FALSE(first.earnedKnown);
  EXPECT_EQ(first.earnedAt, 0);
  EXPECT_EQ(first.observedAt, OCT_20_2026);
  EXPECT_EQ(first.percent, -1);
  EXPECT_EQ(first.progress, -1);
  EXPECT_EQ(first.target, -1);

  const auto& second = snapshot.received[1];
  EXPECT_STREQ(second.description, "");
  EXPECT_EQ(second.observedAt, 0);

  EXPECT_STREQ(snapshot.pending[0].title, "Pending one");
  EXPECT_STREQ(snapshot.pending[0].description, "Read 5 of 10 books");
}

TEST(YbxAchievements, ParsesFlatShapeWithPercentAndProgressObjects) {
  const std::string json = R"({
    "received": [{
      "id": "a", "title": "Титул", "description": "d",
      "earned_at": "2026-10-20T12:00:00Z",
      "first_observed_at": "2026-10-19T00:00:00Z",
      "percent": {"value": 40, "sources": ["cabinet"]},
      "progress": {"current": {"value": 4, "sources": ["cabinet"]},
                   "target": {"value": 10, "sources": ["cabinet"]}},
      "image_url": null
    }],
    "pending": [],
    "has_more": {"received": false, "pending": true},
    "checked_at": "2026-10-20T12:00:00.750Z"
  })";
  const auto snapshot = parseOk(json);
  ASSERT_EQ(snapshot.receivedCount, 1);
  EXPECT_EQ(snapshot.pendingCount, 0);
  EXPECT_FALSE(snapshot.hasMoreReceived);
  EXPECT_TRUE(snapshot.hasMorePending);
  EXPECT_EQ(snapshot.checkedAt, OCT_20_2026 + 12 * 3600);

  const auto& award = snapshot.received[0];
  EXPECT_STREQ(award.title, "Титул");
  EXPECT_TRUE(award.earnedKnown);
  EXPECT_EQ(award.earnedAt, OCT_20_2026 + 12 * 3600);
  EXPECT_EQ(award.observedAt, OCT_19_2026);
  EXPECT_EQ(award.percent, 40);
  EXPECT_EQ(award.progress, 4);
  EXPECT_EQ(award.target, 10);
}

TEST(YbxAchievements, AcceptsScalarAndNumericStringMetrics) {
  const std::string json = R"({
    "received": [],
    "pending": [{
      "id": "p", "title": "P", "percent": 55,
      "progress": 3, "description": "d"
    }],
    "has_more": {"received": false, "pending": false},
    "checked_at": 0
  })";
  const auto snapshot = parseOk(json);
  ASSERT_EQ(snapshot.pendingCount, 1);
  EXPECT_EQ(snapshot.pending[0].percent, 55);
  EXPECT_EQ(snapshot.pending[0].progress, 3);
  EXPECT_EQ(snapshot.pending[0].target, -1);
  EXPECT_EQ(snapshot.checkedAt, 0);
}

TEST(YbxAchievements, EmptySectionsAreAValidSnapshot) {
  const auto snapshot = parseOk(R"({"yandex":{"received":[],"pending":[]},
                                     "has_more":{"received":false,"pending":false},
                                     "checked_at":1792454400})");
  EXPECT_EQ(snapshot.receivedCount, 0);
  EXPECT_EQ(snapshot.pendingCount, 0);
  EXPECT_FALSE(ybxach::hasAwards(snapshot));
}

TEST(YbxAchievements, CapsEachSectionAndFlagsTruncation) {
  std::string json = R"({"received":[)";
  for (int i = 0; i < ybxach::MAX_RECEIVED + 5; ++i) {
    if (i) json += ',';
    json += "{\"id\":\"r" + std::to_string(i) + "\",\"title\":\"T" + std::to_string(i) + "\"}";
  }
  json += R"(],"pending":[],"has_more":{"received":true,"pending":false},"checked_at":0})";
  const auto snapshot = parseOk(json);
  EXPECT_EQ(snapshot.receivedCount, ybxach::MAX_RECEIVED);
  EXPECT_TRUE(snapshot.truncated);
  EXPECT_TRUE(snapshot.hasMoreReceived);
  EXPECT_STREQ(snapshot.received[ybxach::MAX_RECEIVED - 1].id, "r19");
}

TEST(YbxAchievements, RejectsMalformedOrUnbalancedDocuments) {
  const char* cases[] = {
      R"([])",
      R"({"received":[{"id":"x")",
      R"({"received":[{"id":"x"}],)",
      R"({"received":[{"id":"x"}], "pending":[})",
      R"(not json at all)",
  };
  for (const char* json : cases) {
    ybxach::Snapshot out;
    out.receivedCount = 7;  // must be reset on failure
    EXPECT_FALSE(ybxach::parse(json, strlen(json), out)) << json;
    EXPECT_EQ(out.receivedCount, 0) << json;
  }
}

TEST(YbxAchievements, TruncatesLongTextOnUtf8Boundaries) {
  std::string title;
  for (int i = 0; i < 200; ++i) title += "Ж";  // two bytes per character
  const std::string json = "{\"received\":[{\"id\":\"x\",\"title\":\"" + title +
                           "\",\"description\":\"line1\\nline2\\ttab\"}],\"pending\":[]}";
  const auto snapshot = parseOk(json);
  ASSERT_EQ(snapshot.receivedCount, 1);
  const auto& award = snapshot.received[0];
  EXPECT_LT(strlen(award.title), ybxach::TITLE_LEN);
  EXPECT_TRUE(validUtf8(award.title));
  EXPECT_STREQ(award.description, "line1 line2 tab");
}

TEST(YbxAchievements, IgnoresUnknownFieldsAndNestedObjects) {
  const std::string json = R"({
    "received": [{
      "id": "x", "title": "T", "description": "D",
      "color": "#ff0000", "weird": {"nested": [1, 2, {"deep": true}]},
      "image_url": "/x", "percent": {"value": 1, "sources": [{"name": "a"}]}
    }],
    "pending": [], "extra": {"received": 5}, "checked_at": 1792454400
  })";
  const auto snapshot = parseOk(json);
  ASSERT_EQ(snapshot.receivedCount, 1);
  EXPECT_EQ(snapshot.pendingCount, 0);
  EXPECT_EQ(snapshot.received[0].percent, 1);
  EXPECT_EQ(snapshot.checkedAt, OCT_20_2026);
}

TEST(YbxAchievements, ParsesTimestampsAndFormatsLocalDates) {
  EXPECT_EQ(ybxach::parseTimestamp("1792454400", 10), OCT_20_2026);
  EXPECT_EQ(ybxach::parseTimestamp("2026-10-20T00:00:00Z", 20), OCT_20_2026);
  EXPECT_EQ(ybxach::parseTimestamp("2026-10-20T03:00:00+03:00", 25), OCT_20_2026);
  EXPECT_EQ(ybxach::parseTimestamp("2026-10-19", 10), OCT_19_2026);
  EXPECT_EQ(ybxach::parseTimestamp("null", 4), 0);
  EXPECT_EQ(ybxach::parseTimestamp("0", 1), 0);
  EXPECT_EQ(ybxach::parseTimestamp("2026-13-40T00:00:00Z", 20), 0);

  setenv("TZ", "UTC0", 1);
  tzset();
  char date[16];
  char stamp[24];
  ASSERT_TRUE(ybxach::formatDate(OCT_20_2026, date, sizeof(date)));
  EXPECT_STREQ(date, "2026-10-20");
  ASSERT_TRUE(ybxach::formatDateTime(OCT_20_2026 + 14 * 3600 + 5 * 60, stamp, sizeof(stamp)));
  EXPECT_STREQ(stamp, "2026-10-20 14:05");
  EXPECT_FALSE(ybxach::formatDate(0, date, sizeof(date)));
  EXPECT_FALSE(ybxach::formatDate(OCT_20_2026, date, 4));
}
