#pragma once

#include <atomic>
#include <memory>

#include "activities/UiListActivity.h"
#include "util/YbxAchievements.h"
#include "util/YbxAchievementsStore.h"

// Native view of the YBX account's Yandex rewards: received and pending, with
// the cached snapshot shown while a refresh is unavailable. The service sends
// no award image that a cp_ token can fetch, so every row is text.
class YbxAchievementsActivity final : public UiListActivity {
 public:
  YbxAchievementsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("YbxAchievements", renderer, mappedInput) {}
  ~YbxAchievementsActivity() override = default;

  void onEnter() override;
  void onExit() override;
  bool preventAutoSleep() override { return refreshing_.load(); }

 protected:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

 private:
  enum class Notice : uint8_t {
    None,
    NoData,       // signed in, nothing cached yet
    NoToken,      // YBX plugin missing or signed out
    Expired,      // token rejected
    NoYandex,     // device not linked to a Yandex account
    Offline,      // refresh failed, cache is shown
    OfflineNoData,
    RateLimit,
    Error,        // HTTP/parse failure, or no endpoint
  };

  enum class RowKind : uint8_t { None, Memory, Refresh, Notice, Empty, Award, Note };

  struct RowRef {
    RowKind kind = RowKind::None;
    bool received = false;
    int award = -1;
    bool noteIsMore = false;
  };

  struct Rows {
    int refresh = 0;
    int notice = -1;
    int emptyRow = -1;
    int receivedFirst = -1;
    int receivedRows = 0;
    int receivedNote = -1;
    int pendingFirst = -1;
    int pendingRows = 0;
    int pendingNote = -1;
    int total = 0;
  };

  static void provideRow(void* ctx, uint16_t index, freeink::ui::ListItem& item);

  Rows layout() const;
  RowRef rowAt(int index) const;
  void startRefresh();
  void fetchNow();
  void openSignIn();
  static Notice noticeFor(ybxach::FetchStatus status, bool hasData);
  bool noticeNeedsSignIn() const { return notice_ == Notice::NoToken || notice_ == Notice::Expired || notice_ == Notice::NoYandex; }
  const char* noticeTitle() const;
  const char* noticeHint() const;
  void formatFreshness(char* out, size_t size) const;

  std::unique_ptr<ybxach::Snapshot> snapshot_;
  ybxach::FetchStatus lastStatus_ = ybxach::FetchStatus::Offline;
  Notice notice_ = Notice::NoData;
  bool hasData_ = false;
  bool wifiBroughtUp_ = false;
  std::atomic<bool> refreshing_{false};

  // Row provider scratch: one row is formatted at a time, and these pointers
  // stay valid until the next provider call.
  char labelBuf_[ybxach::TITLE_LEN + 48] = {};
  char valueBuf_[48] = {};
  char headingBuf_[48] = {};
};
