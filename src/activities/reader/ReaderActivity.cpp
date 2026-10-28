#include "ReaderActivity.h"

#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Memory.h>
#include <TrustedTime.h>

#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "EpubReaderActivity.h"
#include "ReaderUtils.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "XtcReaderActivity.h"
#include "util/PluginEvents.h"
#include "util/ReaderSessionJournal.h"

namespace {
// Wall time between crash checkpoints while reading. A power cut can lose at
// most this much of the current page; the rest of the session is on SD.
constexpr uint32_t SESSION_CHECKPOINT_INTERVAL_MS = 60 * 1000;
}  // namespace

ReaderActivity::ReaderActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput,
                               std::string bookPath, const bool allowFastInitialRefresh)
    : Activity(name, renderer, mappedInput), bookPath(std::move(bookPath)) {
  if (allowFastInitialRefresh) {
    const int refreshFrequency = SETTINGS.getRefreshFrequency();
    pagesUntilFullRefresh = refreshFrequency > 1 ? refreshFrequency : 2;
  }
}

std::unique_ptr<ReaderActivity> ReaderActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                       std::string path, const bool allowFastInitialRefresh) {
  // ActivityManager requires heap ownership; each branch allocates exactly one screen-lifetime object.
  std::unique_ptr<ReaderActivity> activity;
  if (FsHelpers::hasXtcExtension(path)) {
    activity = makeUniqueNoThrow<XtcReaderActivity>(renderer, mappedInput, std::move(path), allowFastInitialRefresh);
  } else {
    activity = makeUniqueNoThrow<EpubReaderActivity>(renderer, mappedInput, std::move(path), allowFastInitialRefresh);
  }

  if (!activity) {
    LOG_ERR("READER", "OOM: reader activity");
  }
  return activity;
}

void ReaderActivity::applyInitialOrientation() { ReaderUtils::applyOrientation(renderer, SETTINGS.orientation); }

void ReaderActivity::disableFastInitialRefresh() { pagesUntilFullRefresh = 0; }

void ReaderActivity::notePageTurn(const bool succeeded) {
  RenderLock lock(*this);
  readerSession.noteTurn(succeeded);
}

void ReaderActivity::ensureSessionId() {
  if (sessionId[0] == '\0') pluginevents::allocateEventId(sessionId, sizeof(sessionId));
}

void ReaderActivity::onEnter() {
  Activity::onEnter();

  // Heap ledger for field crash reports: free vs largest block distinguishes a
  // leak (free falls) from fragmentation (free stable, largest collapses).
  LOG_INF("MEM", "reader enter: free=%u max_block=%u", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

  if (!Storage.exists(bookPath.c_str())) {
    LOG_ERR("READER", "File does not exist: %s", bookPath.c_str());
    finish();
    return;
  }

  // Clear remembered book after opening it
  if (!APP_STATE.openEpubPath.empty()) {
    APP_STATE.openEpubPath.clear();
    APP_STATE.saveToFile();
  }

  sdFontSystem.ensureLoaded(renderer);
  applyInitialOrientation();

  if (!loadBook()) {
    if (!handleLoadFailure()) finish();
    return;
  }

  requestUpdate();
}

void ReaderActivity::rememberBookOnceRendered() {
  if (bookRemembered || !pageRendered.load(std::memory_order_acquire)) return;
  bookRemembered = true;
  APP_STATE.openEpubPath = bookPath;
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(bookPath, getBookTitle(), getBookAuthor(), getBookThumbBmpPath());
  const pluginevents::Var openVars[] = {{"book", bookPath.c_str()}};
  pluginevents::emit(pluginevents::Event::ReaderOpen, openVars, 1);
}

void ReaderActivity::onExit() {
  Activity::onExit();

  // Keep rebuildable font buffers from pinning the heap between reading sessions.
  if (auto* fontCache = renderer.getFontCacheManager()) {
    fontCache->releaseSdFontCaches();
  }

  LOG_INF("MEM", "reader exit: free=%u max_block=%u", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

  // Flush BEFORE the ReaderExit event: the session's final progress must be
  // durable before a subscriber can act on the exit notification.
  flushReaderSession();

  if (pluginevents::anySubscriber(pluginevents::Event::ReaderExit)) {
    char percent[8];
    snprintf(percent, sizeof(percent), "%d", getScreenshotInfo().progressPercent);
    const pluginevents::Var vars[] = {{"book", bookPath.c_str()}, {"percent", percent}};
    pluginevents::emit(pluginevents::Event::ReaderExit, vars, 2);
  }

  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  APP_STATE.readerActivityLoadCount = 0;
  APP_STATE.saveToFile();

  endOfBookOptions.reset();
  endOfBookOptionsReady.store(false, std::memory_order_release);
}

void ReaderActivity::prepareForSleep() { flushReaderSession(); }

void ReaderActivity::onScreenCovered() { readerSession.pause(millis()); }

void ReaderActivity::checkpointReaderSession(const uint32_t monotonicMs) {
  if (!pluginevents::anySubscriber(pluginevents::Event::ReaderSession)) return;
  if (lastSessionCheckpointMs != 0 && monotonicMs - lastSessionCheckpointMs < SESSION_CHECKPOINT_INTERVAL_MS) return;

  ReaderSessionSummary summary;
  {
    RenderLock lock(*this);
    summary = readerSession.projected(monotonicMs, getProgressBasisPoints());
  }
  if (summary.durationSeconds < ReaderSession::MIN_SESSION_SECONDS) return;

  ensureSessionId();
  // An earlier session whose flush could not queue its event still owns the
  // checkpoint. Queue it under its own id before this session's record can
  // replace it; if that fails, keep the old record and retry in a minute.
  readersession::Record stale;
  if (readersession::load(stale) && strcmp(stale.id, sessionId) != 0) {
    lastSessionCheckpointMs = monotonicMs;
    if (!readersession::recoverPending()) return;
  }
  readersession::Record record;
  snprintf(record.id, sizeof(record.id), "%s", sessionId);
  record.startEpoch = summary.startEpoch;
  record.durationSeconds = summary.durationSeconds;
  record.startProgressBp = summary.startProgressBp;
  record.endProgressBp = summary.endProgressBp;
  record.book = bookPath;
  readersession::save(record);
  // A failed write advances the timer too: a full or write-protected card must
  // not turn every loop pass into a journal read and rewrite.
  lastSessionCheckpointMs = monotonicMs;
}

// Emits one reader.session for the interval that just ended. The id was
// allocated by an earlier checkpoint or the first flush, so a retry from the
// crash checkpoint or a duplicate delivery carries the same id and the
// server's (device, event id) key makes it idempotent.
void ReaderActivity::flushReaderSession() {
  readerSession.finish(millis(), trustedtime::trustedNow(), getProgressBasisPoints());
  if (readerSession.isEmitWorthy() && pluginevents::anySubscriber(pluginevents::Event::ReaderSession)) {
    ensureSessionId();
    // A previous session's flush may have failed and still own the checkpoint.
    readersession::Record stale;
    if (readersession::load(stale) && strcmp(stale.id, sessionId) != 0) {
      // That record holds another session's reading time: it may be replaced
      // only once it is queued. If queuing fails, nothing here is rewritten and
      // the checkpoint stays recoverable at the next boot.
      if (!readersession::recoverPending()) return;
    }
    const ReaderSessionSummary summary = readerSession.summary();
    readersession::Record record;
    snprintf(record.id, sizeof(record.id), "%s", sessionId);
    record.startEpoch = summary.startEpoch;
    record.durationSeconds = summary.durationSeconds;
    record.startProgressBp = summary.startProgressBp;
    record.endProgressBp = summary.endProgressBp;
    record.book = bookPath;
    if (readersession::queueEvent(record)) {
      readersession::discard();
    } else {
      // Keep the checkpoint current so the next boot retries this final
      // summary (same id); a delivery that did go through is a duplicate.
      readersession::save(record);
      LOG_ERR("READER", "reader session kept for retry after reboot");
    }
  } else if (sessionId[0] != '\0') {
    // A session that allocated an id may have checkpointed under it, so the
    // journal is this one's to clear. Without an id the record belongs to an
    // earlier session, or to the retry the branch above just saved: leave it
    // for the next boot.
    readersession::discard();
  }
  readerSession.reset();
  sessionId[0] = '\0';
  lastSessionCheckpointMs = 0;
}

bool ReaderActivity::handleBackNavigation() {
  return ReaderUtils::handleBackNavigation(mappedInput, activityManager, bookPath.c_str(),
                                           {this, [](void* ctx) { static_cast<ReaderActivity*>(ctx)->onGoHome(); }});
}

void ReaderActivity::clearEndOfBookOptionsIfNeeded() {
  if (isAtEndOfBook() || !endOfBookOptionsReady.load(std::memory_order_acquire)) return;

  RenderLock lock(*this);
  endOfBookOptionsReady.store(false, std::memory_order_release);
  endOfBookOptions.reset();
}

bool ReaderActivity::endOfBookMenuActive() const {
  return isAtEndOfBook() && endOfBookOptionsReady.load(std::memory_order_acquire) && endOfBookOptions->menuActive();
}

bool ReaderActivity::handleEndOfBookMenu(const bool suppressConfirmRelease) {
  if (suppressConfirmRelease || !endOfBookMenuActive()) {
    return false;
  }

  std::string openPath;
  switch (endOfBookOptions->handleMenuInput(mappedInput, &openPath)) {
    case EndOfBookOptions::Action::OpenBook:
      activityManager.goToReader(openPath);
      return true;
    case EndOfBookOptions::Action::GoHome:
      onGoHome();
      return true;
    case EndOfBookOptions::Action::LastPage:
      onReturnFromEndOfBook();
      requestUpdate();
      return true;
    case EndOfBookOptions::Action::Redraw:
      requestUpdate();
      return true;
    case EndOfBookOptions::Action::None:
      return false;
  }

  return false;
}

bool ReaderActivity::handleEndOfBookPageTurn(const bool prevTriggered, const bool nextTriggered) {
  if (!isAtEndOfBook()) return false;

  if (endOfBookOptionsReady.load(std::memory_order_acquire) && endOfBookOptions->menuActive()) {
    return true;
  }
  if (nextTriggered) {
    onGoHome();
  } else if (prevTriggered) {
    onReturnFromEndOfBook();
    requestUpdate();
  }
  return true;
}

void ReaderActivity::loop() {
  rememberBookOnceRendered();
  checkpointReaderSession(millis());
  clearEndOfBookOptionsIfNeeded();
  if (handleEndOfBookMenu()) return;
  if (handleFormatInput()) return;
  if (handleBackNavigation()) return;

  const auto touch = ReaderUtils::detectTouchPageTurn(renderer, mappedInput);
  auto [prevTriggered, nextTriggered, fromTilt] = ReaderUtils::detectPageTurn(mappedInput);
  prevTriggered = prevTriggered || touch.prev;
  nextTriggered = nextTriggered || touch.next;
  if (!prevTriggered && !nextTriggered) return;
  if (handleEndOfBookPageTurn(prevTriggered, nextTriggered)) return;

  const unsigned long heldMs = (touch.prev || touch.next) ? touch.heldMs : mappedInput.getHeldTime();
  const bool skip =
      !fromTilt && SETTINGS.longPressButtonBehavior == SETTINGS.CHAPTER_SKIP && heldMs >= ReaderUtils::SKIP_HOLD_MS;

  const bool changed = skip ? skipPages(prevTriggered ? -10 : 10) : pageTurn(!prevTriggered);
  // A skip closes no interval: the dwell on the page before it continues.
  notePageTurn(!skip && changed);
  if (changed && (touch.prev || touch.next)) haptic_feedback::touchAction(skip);
  requestUpdate();
}

void ReaderActivity::render(RenderLock&&) {
  if (isAtEndOfBook()) {
    if (!endOfBookOptions) {
      endOfBookOptions = makeUniqueNoThrow<EndOfBookOptions>(renderer);
      if (!endOfBookOptions) LOG_ERR("READER", "OOM: EndOfBookOptions");
    }
    renderer.clearScreen();
    if (endOfBookOptions) {
      endOfBookOptions->loadOnce(bookPath);
      // Release-publish AFTER loadOnce() so the main task's acquire load can't
      // observe an object whose names/selector are still being populated.
      endOfBookOptionsReady.store(true, std::memory_order_release);
      endOfBookOptions->render(renderer, mappedInput);
    }
    renderer.displayBuffer();
    onEndOfBookRendered();
    markPageRendered();
    readerSession.onRenderComplete(millis(), trustedtime::trustedNow(), getProgressBasisPoints());
    return;
  }

  renderBook();
  readerSession.onRenderComplete(millis(), trustedtime::trustedNow(), getProgressBasisPoints());
}

bool ReaderActivity::handleForcedRefresh() {
  {
    RenderLock lock(*this);
    pagesUntilFullRefresh = 1;
    forcedRefreshPending = true;
  }
  requestUpdate();
  return true;
}
