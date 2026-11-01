#include "YbxAchievementsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <utility>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/ActivityManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;
using ybxach::FetchStatus;

void YbxAchievementsActivity::onEnter() {
  UiListActivity::onEnter();
  snapshot_ = makeUniqueNoThrow<ybxach::Snapshot>();
  if (!snapshot_) {
    LOG_ERR("YBXA", "OOM: achievements snapshot");
    return;
  }

  std::string token;
  if (!ybxach::loadToken(token)) {
    notice_ = Notice::NoToken;
    return;
  }
  if (ybxach::loadCached(*snapshot_)) {
    hasData_ = true;
    notice_ = Notice::None;
    lastStatus_ = FetchStatus::Ok;
    return;
  }
  notice_ = Notice::NoData;
}

void YbxAchievementsActivity::onExit() {
  UiListActivity::onExit();
  snapshot_.reset();
  // The platform tears a Wi-Fi session down with a reboot so the next screen
  // starts on an unfragmented heap. A sign-in handoff keeps the flag clear:
  // the plugin screen owns the session from there.
  if (wifiBroughtUp_) {
    wifiBroughtUp_ = false;
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

YbxAchievementsActivity::Rows YbxAchievementsActivity::layout() const {
  Rows rows;
  int index = 0;
  rows.refresh = index++;
  if (notice_ != Notice::None) rows.notice = index++;
  if (!hasData_ || !snapshot_) {
    rows.total = index;
    return rows;
  }

  const int received = snapshot_->receivedCount;
  const int pending = snapshot_->pendingCount;
  const bool anyAwards =
      received > 0 || pending > 0 || snapshot_->hasMoreReceived || snapshot_->hasMorePending || snapshot_->truncated;
  if (!anyAwards) {
    rows.emptyRow = index++;
    rows.total = index;
    return rows;
  }

  if (received > 0) {
    rows.receivedFirst = index;
    rows.receivedRows = received;
    index += received;
  }
  if (received == 0 || snapshot_->hasMoreReceived || snapshot_->truncated) rows.receivedNote = index++;
  if (pending > 0) {
    rows.pendingFirst = index;
    rows.pendingRows = pending;
    index += pending;
  }
  if (pending == 0 || snapshot_->hasMorePending || snapshot_->truncated) rows.pendingNote = index++;
  rows.total = index;
  return rows;
}

YbxAchievementsActivity::RowRef YbxAchievementsActivity::rowAt(const int index) const {
  RowRef ref;
  if (!snapshot_) {
    ref.kind = RowKind::Memory;
    return ref;
  }
  const Rows rows = layout();
  if (index == rows.refresh) {
    ref.kind = RowKind::Refresh;
  } else if (index == rows.notice) {
    ref.kind = RowKind::Notice;
  } else if (index == rows.emptyRow) {
    ref.kind = RowKind::Empty;
  } else if (rows.receivedFirst >= 0 && index >= rows.receivedFirst && index < rows.receivedFirst + rows.receivedRows) {
    ref.kind = RowKind::Award;
    ref.received = true;
    ref.award = index - rows.receivedFirst;
  } else if (index == rows.receivedNote) {
    ref.kind = RowKind::Note;
    ref.received = true;
    ref.noteIsMore = snapshot_->receivedCount > 0;
  } else if (rows.pendingFirst >= 0 && index >= rows.pendingFirst && index < rows.pendingFirst + rows.pendingRows) {
    ref.kind = RowKind::Award;
    ref.award = index - rows.pendingFirst;
  } else if (index == rows.pendingNote) {
    ref.kind = RowKind::Note;
    ref.noteIsMore = snapshot_->pendingCount > 0;
  }
  return ref;
}

int YbxAchievementsActivity::listCount() const { return !snapshot_ ? 1 : layout().total; }

const char* YbxAchievementsActivity::headerTitle() const { return tr(STR_YBX_ACHIEVEMENTS); }

void YbxAchievementsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.rowProvider = &YbxAchievementsActivity::provideRow;
  props.rowProviderCtx = this;
  props.count = static_cast<uint16_t>(listCount());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.valueInset = 8;
  props.labelText = screen.theme().bodyText;
  props.labelText.maxLines = 2;
  props.subtitleText = screen.theme().smallText;
  props.subtitleText.maxLines = 2;
  props.valueText = screen.theme().smallText;
  // Dates and percentages are short; wrapped titles may use the full width.
  props.balanceWrappedLabelWithValue = false;
  syncListViewport(screen, props);
  screen.list(props);
}

void YbxAchievementsActivity::formatFreshness(char* out, const size_t size) const {
  if (refreshing_.load()) {
    snprintf(out, size, "%s", tr(STR_YBX_ACH_REFRESHING));
    return;
  }
  if (!hasData_) {
    snprintf(out, size, "%s", tr(STR_YBX_ACH_NEVER));
    return;
  }
  char stamp[24];
  if (!ybxach::formatDateTime(snapshot_->fetchedAt, stamp, sizeof(stamp))) {
    snprintf(out, size, "%s", tr(STR_YBX_ACH_SAVED_SHORT));
    return;
  }
  const char* format = lastStatus_ == FetchStatus::Ok ? tr(STR_YBX_ACH_UPDATED) : tr(STR_YBX_ACH_SAVED);
  snprintf(out, size, format, stamp);
}

const char* YbxAchievementsActivity::noticeTitle() const {
  switch (notice_) {
    case Notice::NoToken:
      return tr(STR_YBX_ACH_SIGN_IN);
    case Notice::Expired:
      return tr(STR_YBX_ACH_EXPIRED);
    case Notice::NoYandex:
      return tr(STR_YBX_ACH_NO_YANDEX);
    case Notice::Offline:
    case Notice::OfflineNoData:
      return tr(STR_YBX_ACH_OFFLINE);
    case Notice::RateLimit:
      return tr(STR_YBX_ACH_RATE_LIMIT);
    case Notice::Error:
      return tr(STR_YBX_ACH_ERROR);
    case Notice::NoData:
      return tr(STR_YBX_ACH_NO_DATA);
    case Notice::None:
      break;
  }
  return "";
}

const char* YbxAchievementsActivity::noticeHint() const {
  switch (notice_) {
    case Notice::NoToken:
      return tr(STR_YBX_ACH_SIGN_IN_HINT);
    case Notice::Expired:
      return tr(STR_YBX_ACH_EXPIRED_HINT);
    case Notice::NoYandex:
      return tr(STR_YBX_ACH_NO_YANDEX_HINT);
    case Notice::Offline:
      return tr(STR_YBX_ACH_OFFLINE_HINT);
    case Notice::OfflineNoData:
      return tr(STR_YBX_ACH_OFFLINE_NONE_HINT);
    case Notice::RateLimit:
      return tr(STR_YBX_ACH_RATE_LIMIT_HINT);
    case Notice::Error:
      return hasData_ ? tr(STR_YBX_ACH_OFFLINE_HINT) : tr(STR_YBX_ACH_ERROR_HINT);
    case Notice::NoData:
      return tr(STR_YBX_ACH_NO_DATA_HINT);
    case Notice::None:
      break;
  }
  return "";
}

void YbxAchievementsActivity::provideRow(void* ctx, const uint16_t index, fui::ListItem& item) {
  auto* self = static_cast<YbxAchievementsActivity*>(ctx);
  const RowRef row = self->rowAt(index);
  switch (row.kind) {
    case RowKind::Memory:
      item.label = tr(STR_MEMORY_ERROR);
      item.enabled = false;
      return;
    case RowKind::Refresh:
      item.label = tr(STR_YBX_ACH_REFRESH);
      self->formatFreshness(self->valueBuf_, sizeof(self->valueBuf_));
      item.value = self->valueBuf_;
      item.bold = true;
      return;
    case RowKind::Notice:
      item.label = self->noticeTitle();
      item.subtitle = self->noticeHint();
      item.bold = true;
      return;
    case RowKind::Empty:
      item.label = tr(STR_YBX_ACH_EMPTY);
      item.subtitle = tr(STR_YBX_ACH_EMPTY_HINT);
      item.enabled = false;
      return;
    case RowKind::Note:
      if (row.noteIsMore) {
        const int shown = row.received ? self->snapshot_->receivedCount : self->snapshot_->pendingCount;
        snprintf(self->labelBuf_, sizeof(self->labelBuf_), tr(STR_YBX_ACH_MORE), shown);
        item.label = self->labelBuf_;
      } else {
        item.label = row.received ? tr(STR_YBX_ACH_NO_RECEIVED) : tr(STR_YBX_ACH_NO_PENDING);
      }
      item.enabled = false;
      break;
    case RowKind::Award: {
      const ybxach::Award& award =
          row.received ? self->snapshot_->received[row.award] : self->snapshot_->pending[row.award];
      item.label = award.title[0] ? award.title : tr(STR_YBX_ACH_UNTITLED);
      if (award.description[0]) item.subtitle = award.description;
      self->valueBuf_[0] = '\0';
      if (row.received) {
        char date[16];
        if (award.earnedKnown && ybxach::formatDate(award.earnedAt, date, sizeof(date))) {
          snprintf(self->valueBuf_, sizeof(self->valueBuf_), tr(STR_YBX_ACH_EARNED), date);
        } else if (ybxach::formatDate(award.observedAt, date, sizeof(date))) {
          snprintf(self->valueBuf_, sizeof(self->valueBuf_), tr(STR_YBX_ACH_SEEN), date);
        }
      } else if (award.percent >= 0) {
        snprintf(self->valueBuf_, sizeof(self->valueBuf_), tr(STR_YBX_ACH_PERCENT), award.percent);
      } else if (award.progress >= 0 && award.target > 0) {
        snprintf(self->valueBuf_, sizeof(self->valueBuf_), tr(STR_YBX_ACH_PROGRESS), award.progress, award.target);
      }
      if (self->valueBuf_[0]) item.value = self->valueBuf_;
      break;
    }
    case RowKind::None:
      return;
  }

  // The heading shares the first row's index so the selection never lands on
  // a non-selectable header.
  if (row.kind != RowKind::Award && row.kind != RowKind::Note) return;
  const Rows rows = self->layout();
  const int headingRow = row.received ? (rows.receivedFirst >= 0 ? rows.receivedFirst : rows.receivedNote)
                                      : (rows.pendingFirst >= 0 ? rows.pendingFirst : rows.pendingNote);
  if (index != headingRow) return;
  const int count = row.received ? self->snapshot_->receivedCount : self->snapshot_->pendingCount;
  const char* format = row.received ? tr(STR_YBX_ACH_RECEIVED) : tr(STR_YBX_ACH_PENDING);
  snprintf(self->headingBuf_, sizeof(self->headingBuf_), format, count);
  item.sectionHeading = self->headingBuf_;
}

void YbxAchievementsActivity::activateIndex(const int index) {
  app.clearTapFlash();
  if (!snapshot_ || refreshing_.load()) return;
  const RowRef row = rowAt(index);
  switch (row.kind) {
    case RowKind::Refresh:
      startRefresh();
      break;
    case RowKind::Notice:
      if (noticeNeedsSignIn()) {
        openSignIn();
      } else {
        startRefresh();
      }
      break;
    default:
      break;
  }
}

void YbxAchievementsActivity::openSignIn() {
  // Signing in happens in the plugin catalog's device-code flow. Its Wi-Fi
  // session owns teardown from here, so onExit must not reboot on top of it.
  wifiBroughtUp_ = false;
  activityManager.goToPlugins(false);
}

void YbxAchievementsActivity::startRefresh() {
  if (WiFi.status() == WL_CONNECTED) {
    fetchNow();
    return;
  }
  auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
  if (!wifi) {
    LOG_ERR("YBXA", "OOM: Wi-Fi selection");
    notice_ = Notice::Error;
    requestUpdate();
    return;
  }
  startActivityForResult(std::move(wifi), [this](const ActivityResult& result) {
    if (result.isCancelled) {
      requestUpdate();
      return;
    }
    wifiBroughtUp_ = true;
    fetchNow();
  });
}

void YbxAchievementsActivity::fetchNow() {
  if (!snapshot_ || refreshing_.exchange(true)) return;
  // Paint the refreshing state and let the render finish before the request
  // writes the shared snapshot: no render may read it mid-parse.
  requestUpdateAndWait();
  bool dataKept = true;
  const FetchStatus status = ybxach::refresh(*snapshot_, &dataKept);
  refreshing_.store(false);
  lastStatus_ = status;
  if (status == FetchStatus::Ok) {
    hasData_ = true;
  } else if (status == FetchStatus::BadResponse) {
    hasData_ = dataKept;
  }
  notice_ = noticeFor(status, hasData_);
  requestUpdate();
}

YbxAchievementsActivity::Notice YbxAchievementsActivity::noticeFor(const FetchStatus status, const bool hasData) {
  switch (status) {
    case FetchStatus::Ok:
      return Notice::None;
    case FetchStatus::NoToken:
      return Notice::NoToken;
    case FetchStatus::Expired:
      return Notice::Expired;
    case FetchStatus::NotLinked:
      return Notice::NoYandex;
    case FetchStatus::RateLimited:
      return Notice::RateLimit;
    case FetchStatus::Offline:
      return hasData ? Notice::Offline : Notice::OfflineNoData;
    case FetchStatus::Disabled:
    case FetchStatus::HttpError:
    case FetchStatus::BadResponse:
      return Notice::Error;
  }
  return Notice::Error;
}
