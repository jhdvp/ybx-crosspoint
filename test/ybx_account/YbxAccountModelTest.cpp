#include <gtest/gtest.h>
#include <algorithm>
#include <vector>
#include <string>

#include "util/YbxAccountModel.h"

using namespace ybxaccount;

TEST(YbxAccountModel, ClassifiesOnlyConnectedProviderCatalogs) {
  EXPECT_EQ(classifyListUrl("https://my.ybx.app/api/v1/crosspoint/catalog?shelf=reading&page=1"), ListKind::Yandex);
  EXPECT_EQ(classifyListUrl("https://my.ybx.app/api/v1/crosspoint/wb/catalog?page=1"), ListKind::Wb);
  EXPECT_EQ(classifyListUrl("https://my.ybx.app/api/v1/crosspoint/litres/catalog?page=1"), ListKind::Litres);
  EXPECT_EQ(classifyListUrl("https://example.test/unrelated"), ListKind::None);
  EXPECT_EQ(classifyListUrl("https://evil.example/crosspoint/wb/catalog"), ListKind::None);
  EXPECT_EQ(classifyListUrl("https://my.ybx.app.evil.example/api/v1/crosspoint/catalog"), ListKind::None);
}
TEST(YbxAccountModel, CountsProviderShelvesWithoutPretendingTheOthersAreLinked) {
  Summary s;
  for (int i = 0; i < 4; ++i) s.addList(ListKind::Yandex);
  s.addList(ListKind::Wb);
  EXPECT_TRUE(s.connected(SourceKind::Yandex));
  EXPECT_TRUE(s.connected(SourceKind::Wb));
  EXPECT_FALSE(s.connected(SourceKind::Litres));
  EXPECT_EQ(s.connectedCount(), 2);
  EXPECT_EQ(s.shelfCount(SourceKind::Yandex), 4);
  EXPECT_EQ(s.shelfCount(SourceKind::Wb), 1);
  EXPECT_EQ(s.totalLists, 5);
  s.clear();
  EXPECT_FALSE(s.connected(SourceKind::Yandex));
  EXPECT_EQ(s.totalLists, 0);
}
TEST(YbxAccountModel, ExpiredReaderTokenIsNotAProviderLoginFailure) {
  EXPECT_EQ(listsOutcomeFromStatus(200, false), ListsOutcome::Ok);
  EXPECT_EQ(listsOutcomeFromStatus(401, false), ListsOutcome::InvalidToken);
  EXPECT_EQ(listsOutcomeFromStatus(403, true), ListsOutcome::DeviceRemoved);
  EXPECT_EQ(listsOutcomeFromStatus(409, false), ListsOutcome::ProviderSession);
  EXPECT_EQ(listsOutcomeFromStatus(-1, false), ListsOutcome::Offline);
  EXPECT_TRUE(isProviderSessionBody("books_consent_required"));
  EXPECT_FALSE(isProviderSessionBody("invalid_token"));
}
TEST(YbxAccountModel, PollTimingIsBoundedAndHandlesMillisRollover) {
  EXPECT_EQ(clampPollIntervalMs(0), 3000UL);
  EXPECT_EQ(clampPollIntervalMs(5), 5000UL);
  EXPECT_EQ(clampPollIntervalMs(999999999L), 3600000UL);
  EXPECT_EQ(clampCodeLifetimeMs(0), 60000UL);
  EXPECT_EQ(clampCodeLifetimeMs(999999999L), 3600000UL);
  EXPECT_FALSE(deadlineReached(0xfffffff0UL, 0x00000010UL));
  EXPECT_TRUE(deadlineReached(0x00000010UL, 0x00000010UL));
  EXPECT_EQ(pollOutcomeFromErrorCode("slow_down"), PollOutcome::SlowDown);
  EXPECT_EQ(pollOutcomeFromErrorCode("access_denied"), PollOutcome::Denied);
}

TEST(YbxAccountModel, ValidPairingChallengeBindsUriAndSecretToFirstParty) {
  EXPECT_TRUE(validDeviceChallenge("ABCD-1234", "abc.def0123", "https://my.ybx.app/pair?code=ABCD-1234"));
  EXPECT_FALSE(validDeviceChallenge("ABCD-1234", "abc.def", "https://evil.example/pair?code=ABCD-1234"));
  EXPECT_FALSE(validDeviceChallenge("ABCD-1234", "abc.def", "https://my.ybx.app.evil/pair?code=ABCD-1234"));
  EXPECT_FALSE(validDeviceChallenge("ABCD-1234", "abc.def", "https://my.ybx.app/pair?code=WRONG"));
  EXPECT_FALSE(validDeviceChallenge("A;DROP", "abc.def", "https://my.ybx.app/pair?code=A;DROP"));
  EXPECT_FALSE(validDeviceChallenge("ABCD-1234", "abc\\\"def", "https://my.ybx.app/pair?code=ABCD-1234"));
}
TEST(YbxAccountModel, TokenValidationAndBackoffAreBounded) {
  EXPECT_FALSE(validAccessToken(""));
  EXPECT_FALSE(validAccessToken("bad\\ntoken"));
  EXPECT_FALSE(validAccessToken(std::string(2049,'x')));
  EXPECT_TRUE(validAccessToken("secure.encoded_token-12"));
  EXPECT_EQ(slowedDownIntervalMs(3000UL), 8000UL);
  EXPECT_EQ(slowedDownIntervalMs(60000UL), 60000UL);
  EXPECT_EQ(slowedDownIntervalMs(0xffffffffUL), 60000UL);
}

TEST(YbxAccountModel, DiagnosesProviderSessionOnlyForKnown409Error) {
  EXPECT_EQ(checkedListsOutcome(409, false, "books_consent_required"), ListsOutcome::ProviderSession);
  EXPECT_EQ(checkedListsOutcome(409, false, "conflict"), ListsOutcome::Failed);
  EXPECT_EQ(checkedListsOutcome(403, true, ""), ListsOutcome::DeviceRemoved);
  EXPECT_EQ(checkedListsOutcome(401, false, ""), ListsOutcome::InvalidToken);
  EXPECT_EQ(checkedListsOutcome(-1, false, ""), ListsOutcome::Offline);
  EXPECT_EQ(checkedListsOutcome(200, false, ""), ListsOutcome::Ok);
}
TEST(YbxAccountModel, ClearsOnlyTokenScopedCacheWithFailClosedErrors) {
  struct Files {
    std::vector<std::string> current = {
      "/plugins/ybx/events.jsonl", "/plugins/ybx/events.jsonl.bak",
      "/plugins/ybx/reading-summary.json.pending",
      "/plugins/ybx/achievements.json.response",
      "/plugins/ybx/books/novel.epub", "/plugins/ybx/reading-positions.json"
    };
    bool exists(const std::string& path) {
      return std::find(current.begin(),current.end(),path)!=current.end();
    }
    bool remove(const std::string& path) {
      auto it=std::find(current.begin(),current.end(),path);
      if(it==current.end()) return false;
      current.erase(it); return true;
    }
  } files;
  EXPECT_FALSE(clearScopedCaches(files, ""));
  EXPECT_TRUE(clearScopedCaches(files, "/plugins/ybx"));
  EXPECT_EQ(files.current.size(), 2u);
  EXPECT_TRUE(files.exists("/plugins/ybx/books/novel.epub"));
  EXPECT_TRUE(files.exists("/plugins/ybx/reading-positions.json"));
  struct BrokenFiles {
    bool exists(const std::string&) { return true; }
    bool remove(const std::string&) { return false; }
  } broken;
  EXPECT_FALSE(clearScopedCaches(broken, "/plugins/ybx"));
}
TEST(YbxAccountModel, CabinetSourcesDeepLinkIsFixedTrustedOrigin) {
  EXPECT_EQ(CABINET_SOURCES_URL, "https://my.ybx.app/cabinet#sources");
}

#include "util/PluginTokenAtomic.h"
#include <map>
#include <utility>

// Fake SD surface: tests the sequence and failure recovery, not a filesystem.
struct TokenFilesFake {
  std::map<std::string,std::string> items;
  bool failStaging = false;
  bool failPromotion = false;
  bool exists(const std::string& p) { return items.count(p) != 0; }
  bool remove(const std::string& p) { return items.erase(p) != 0; }
  bool rename(const std::string& a,const std::string& b) {
    if (failPromotion && a.find(".pending") != std::string::npos) return false;
    const auto it=items.find(a);
    if(it==items.end() || exists(b)) return false;
    items.emplace(b, it->second);
    items.erase(it);
    return true;
  }
  bool stage(const std::string& path) {
    if(failStaging) return false;
    items[path]="new-token";
    return true;
  }
};
TEST(YbxAccountModel, AtomicTokenReplacementPreservesOldOnWriteFailure) {
  TokenFilesFake f;
  f.items["/plugins/ybx/token.json"]="old-token";
  f.failStaging=true;
  EXPECT_FALSE(plugintoken::replace(f,"/plugins/ybx/token.json",[&](const auto& p){return f.stage(p);}));
  EXPECT_EQ(f.items.at("/plugins/ybx/token.json"),"old-token");
  EXPECT_EQ(f.items.size(),1u);
}
TEST(YbxAccountModel, AtomicTokenReplacementRollsBackOnRenameError) {
  TokenFilesFake f;
  const std::string p="/plugins/ybx/token.json";
  f.items[p]="old-token";
  f.failPromotion=true;
  EXPECT_FALSE(plugintoken::replace(f,p,[&](const auto& stage){return f.stage(stage);}));
  EXPECT_TRUE(plugintoken::recover(f,p));
  EXPECT_EQ(f.items.at(p),"old-token");
  EXPECT_EQ(f.items.size(),1u);
}
TEST(YbxAccountModel, AtomicTokenReplacementPromotesNewAndPrunesBackup) {
  TokenFilesFake f;
  const std::string p="/plugins/ybx/token.json";
  f.items[p]="old-token";
  EXPECT_TRUE(plugintoken::replace(f,p,[&](const auto& stage){return f.stage(stage);}));
  EXPECT_EQ(f.items.at(p),"new-token");
  EXPECT_EQ(f.items.size(),1u);
}
TEST(YbxAccountModel, AtomicTokenRecoveryRestoresOldOnInterruptedPromotion) {
  TokenFilesFake f;
  const std::string p="/plugins/ybx/token.json";
  f.items[p+".bak"]="old-token";
  f.items[p+".pending"]="uncommitted-new";
  EXPECT_TRUE(plugintoken::recover(f,p));
  EXPECT_EQ(f.items.at(p),"old-token");
  EXPECT_EQ(f.items.size(),1u);
}
TEST(YbxAccountModel, AtomicTokenRecoveryNeverOverwritesCommittedNewToken) {
  TokenFilesFake f;
  const std::string p="/plugins/ybx/token.json";
  f.items[p]="new-token";
  f.items[p+".bak"]="old-token";
  EXPECT_TRUE(plugintoken::recover(f,p));
  EXPECT_EQ(f.items.at(p),"new-token");
  EXPECT_EQ(f.items.size(),1u);
}

// Layout tests cover both shipped panel sizes and all 4 rotations. This
// verifies geometry before device flashing; visual acceptance remains physical.
#include "util/YbxAccountLayout.h"

namespace {
bool accountContains(freeink::ui::Rect outer, freeink::ui::Rect inner) {
  return inner.width > 0 && inner.height > 0 &&
         inner.x >= outer.x && inner.y >= outer.y &&
         inner.right() <= outer.right() && inner.bottom() <= outer.bottom();
}
bool accountOverlaps(freeink::ui::Rect a, freeink::ui::Rect b) {
  return a.x < b.right() && a.right() > b.x &&
         a.y < b.bottom() && a.bottom() > b.y;
}
}  // namespace

TEST(YbxAccountUiLayout, AllStatesFitFourOrientationsOnX3X4AndX4Pro) {
  for (auto panel : {freeink::ui::Size{480, 800}, freeink::ui::Size{528, 792}}) {
    for (int orientation = 0; orientation < 4; ++orientation) {
      const int w = orientation % 2 ? panel.height : panel.width;
      const int h = orientation % 2 ? panel.width : panel.height;
      for (bool pairing : {false, true}) {
        for (int line : {18, 22, 26}) {
          const auto safe = ybxaccountui::rect(20, 8, w - 40, h - 16);
          const auto ui = ybxaccountui::layout(safe, line, line + 4, pairing);
          ASSERT_TRUE(accountContains(safe, ui.brandMark));
          ASSERT_TRUE(accountContains(safe, ui.eyebrow));
          ASSERT_TRUE(accountContains(safe, ui.title));
          ASSERT_TRUE(accountContains(safe, ui.headerLine));
          EXPECT_FALSE(accountOverlaps(ui.headerLine, ui.actions[0]));
          EXPECT_EQ(ui.actionCount, pairing && !ui.compact ? 2 : 3);
          for (int i = 0; i < ui.actionCount; ++i) {
            EXPECT_TRUE(accountContains(safe, ui.actions[i]));
            EXPECT_GE(ui.actions[i].height, 48);
            EXPECT_FALSE(accountOverlaps(ui.headerLine, ui.actions[i]));
            for (int j = i + 1; j < ui.actionCount; ++j)
              EXPECT_FALSE(accountOverlaps(ui.actions[i], ui.actions[j]));
          }
          if (pairing) {
            EXPECT_TRUE(accountContains(safe, ui.code));
            EXPECT_TRUE(accountContains(safe, ui.codeCaption));
            EXPECT_FALSE(accountOverlaps(ui.code, ui.actions[0]));
            EXPECT_TRUE(accountContains(safe, ui.pairHelp));
            EXPECT_FALSE(accountOverlaps(ui.pairHelp, ui.actions[0]));
            if (ui.qrInline) {
              EXPECT_TRUE(accountContains(safe, ui.inlineQr));
              EXPECT_GE(ui.inlineQr.width, 160);
              EXPECT_FALSE(accountOverlaps(ui.inlineQr, ui.code));
              EXPECT_FALSE(accountOverlaps(ui.inlineQr, ui.pairWait));
              EXPECT_FALSE(accountOverlaps(ui.inlineQr, ui.actions[0]));
              EXPECT_FALSE(accountOverlaps(ui.inlineQr, ui.pairHelp));
            }
          } else {
            EXPECT_TRUE(accountContains(safe, ui.sectionTitle));
            EXPECT_TRUE(accountContains(safe, ui.status));
            EXPECT_TRUE(accountContains(ui.status, ui.statusCaption));
            EXPECT_TRUE(accountContains(ui.status, ui.statusDetail));
            for (int i = 0; i < 3; ++i) {
              ASSERT_TRUE(accountContains(safe, ui.sources[i]));
              EXPECT_GE(ui.sources[i].height, 45);
              EXPECT_TRUE(accountContains(ui.sources[i], ui.sourceMarks[i]));
              EXPECT_TRUE(accountContains(ui.sources[i], ui.sourceTitles[i]));
              EXPECT_TRUE(accountContains(ui.sources[i], ui.sourceDetails[i]));
              EXPECT_FALSE(accountOverlaps(ui.sources[i], ui.status));
              EXPECT_FALSE(accountOverlaps(ui.sources[i], ui.actions[0]));
              if (ui.sourceBands) {
                EXPECT_TRUE(accountContains(ui.sources[i], ui.sourceActionLeft[i]));
                EXPECT_TRUE(accountContains(ui.sources[i], ui.sourceActionRight[i]));
              }
              if (i > 0) EXPECT_FALSE(accountOverlaps(ui.sources[i-1], ui.sources[i]));
            }
            EXPECT_FALSE(accountOverlaps(ui.status, ui.actions[0]));
          }
        }
      }
    }
  }
}
TEST(YbxAccountUiLayout, QRIsVisibleAndResponsiveInBothOrientations) {
  const auto portrait = ybxaccountui::layout(ybxaccountui::rect(20, 8, 440, 760), 22, 27, true);
  const auto landscape = ybxaccountui::layout(ybxaccountui::rect(20, 8, 760, 440), 22, 27, true);
  EXPECT_TRUE(portrait.qrInline);
  EXPECT_TRUE(landscape.qrInline);
  EXPECT_GE(portrait.inlineQr.width, 220);
  EXPECT_GE(landscape.inlineQr.width, 96);
  EXPECT_FALSE(accountOverlaps(portrait.inlineQr, portrait.code));
  EXPECT_FALSE(accountOverlaps(landscape.inlineQr, landscape.code));
}
