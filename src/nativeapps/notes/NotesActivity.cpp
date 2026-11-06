#include "NotesActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <utility>

#include "NoteActionsActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

void NotesActivity::reload() {
  notes_ = nativeapps::notes::list();
  rowsUsed_ = 1 + notes_.rows.size();
  rows_[0] = {};
  rows_[0].label = tr(STR_YBX_NOTE_NEW);
  rows_[0].actionValue = 0;

  for (size_t i = 0; i < notes_.rows.size(); ++i) {
    auto& row = rows_[i + 1];
    row = {};
    row.label = notes_.rows[i].readable ? notes_.rows[i].title.c_str() : tr(STR_YBX_NOTE_INVALID);
    row.subtitle = tr(STR_YBX_NOTE_MARKDOWN);
    row.actionValue = static_cast<int16_t>(i + 1);
  }
  needsReload_ = false;
}

void NotesActivity::releaseRows() {
  notes_.rows.clear();
  notes_.rows.shrink_to_fit();
  rowsUsed_ = 1;
  needsReload_ = true;
}

void NotesActivity::onEnter() {
  reload();
  UiListActivity::onEnter();
}

void NotesActivity::onExit() {
  releaseRows();
  UiListActivity::onExit();
}

void NotesActivity::onScreenCovered() { releaseRows(); }

void NotesActivity::loop() {
  if (needsReload_) {
    RenderLock lock(RenderLock::Mode::Try);
    if (lock.ownsLock()) {
      reload();
      resetUi();
      requestUpdate();
    }
    return;
  }
  UiListActivity::loop();
}

const char* NotesActivity::headerTitle() const { return tr(STR_YBX_NOTES); }

void NotesActivity::buildScreen(UiScreen& screen) {
  if (needsReload_) {
    reload();
  }
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (notes_.failed || notes_.truncated) {
    const char* message = notes_.failed ? tr(STR_YBX_NOTE_SD_ERROR) : tr(STR_YBX_NOTE_LIMIT);
    screen.target().text(
        screen.takeTop(static_cast<int16_t>(2 * screen.target().lineHeight(screen.theme().smallText.font)),
                       screen.theme().spaceMd),
        message, screen.theme().smallText);
  }
  fui::ListProps props;
  props.items = rows_.data();
  props.count = static_cast<uint16_t>(rowsUsed_);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  screen.list(props);
}

void NotesActivity::activateIndex(int index) {
  if (index < 0 || static_cast<size_t>(index) >= rowsUsed_) return;
  app.clearTapFlash();
  if (index == 0) {
    startNewNote();
    return;
  }
  const std::string fileName = notes_.rows[static_cast<size_t>(index - 1)].fileName;
  releaseRows();
  auto details = makeUniqueNoThrow<NoteActionsActivity>(renderer, mappedInput, fileName);
  if (!details) {
    GUI.drawPopup(renderer, tr(STR_MEMORY_ERROR));
    return;
  }
  startActivityForResult(std::move(details), [this](const ActivityResult&) {
    needsReload_ = true;
    requestUpdate();
  });
}

void NotesActivity::startNewNote() {
  if (notes_.failed) {
    GUI.drawPopup(renderer, tr(STR_YBX_NOTE_SD_ERROR));
    return;
  }
  if (notes_.truncated || notes_.rows.size() >= nativeapps::notes::kMaxNotes) {
    GUI.drawPopup(renderer, tr(STR_YBX_NOTE_LIMIT));
    return;
  }
  releaseRows();
  auto titleKeyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_YBX_NOTE_TITLE), "",
                                                                nativeapps::notes::kMaxTitleBytes, InputType::Text);
  if (!titleKeyboard) {
    GUI.drawPopup(renderer, tr(STR_MEMORY_ERROR));
    return;
  }

  startActivityForResult(std::move(titleKeyboard), [this](const ActivityResult& result) {
    if (result.isCancelled) {
      needsReload_ = true;
      requestUpdate();
      return;
    }
    const std::string title = std::get<KeyboardResult>(result.data).text;
    if (!nativeapps::notes::validDocument({title, ""})) {
      GUI.drawPopup(renderer, tr(STR_YBX_NOTE_INVALID));
      needsReload_ = true;
      requestUpdate();
      return;
    }
    auto bodyKeyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_YBX_NOTE_BODY), "",
                                                                 nativeapps::notes::kMaxBodyBytes, InputType::Text);
    if (!bodyKeyboard) {
      GUI.drawPopup(renderer, tr(STR_MEMORY_ERROR));
      needsReload_ = true;
      requestUpdate();
      return;
    }
    startActivityForResult(std::move(bodyKeyboard), [this, title](const ActivityResult& bodyResult) {
      if (bodyResult.isCancelled) {
        needsReload_ = true;
        requestUpdate();
        return;
      }
      nativeapps::notes::Document document{title, std::get<KeyboardResult>(bodyResult.data).text};
      std::string newFileName;
      if (!nativeapps::notes::create(document, newFileName)) {
        GUI.drawPopup(renderer, tr(STR_YBX_NOTE_SAVE_FAILED));
        needsReload_ = true;
        requestUpdate();
        return;
      }
      needsReload_ = true;
      requestUpdate();
    });
  });
}
