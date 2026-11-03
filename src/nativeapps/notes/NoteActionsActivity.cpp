#include "NoteActionsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <utility>

#include "activities/ActivityManager.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

void NoteActionsActivity::onEnter() {
  loaded_ = nativeapps::notes::load(fileName_, doc_);
  constexpr StrId names[] = {StrId::STR_YBX_NOTE_OPEN, StrId::STR_YBX_NOTE_EDIT_TITLE,
                             StrId::STR_YBX_NOTE_EDIT_BODY, StrId::STR_DELETE};
  for (size_t i = 0; i < rows_.size(); ++i) {
    rows_[i].label = I18N.get(names[i]);
    rows_[i].actionValue = static_cast<int16_t>(i);
  }
  UiListActivity::onEnter();
}

void NoteActionsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  if (!loaded_) {
    screen.target().text(
        screen.takeTop(static_cast<int16_t>(3 * screen.target().lineHeight(screen.theme().bodyText.font)),
                       screen.theme().spaceMd),
        tr(STR_YBX_NOTE_INVALID), screen.theme().bodyText);
    return;
  }

  screen.target().text(
      screen.takeTop(static_cast<int16_t>(2 * screen.target().lineHeight(screen.theme().bodyText.font)),
                     screen.theme().spaceMd),
      doc_.title.c_str(), screen.theme().bodyText);
  fui::ListProps props;
  props.items = rows_.data();
  props.count = static_cast<uint16_t>(rows_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  screen.list(props);
}

void NoteActionsActivity::activateIndex(const int index) {
  if (!loaded_) return;
  app.clearTapFlash();
  if (index == 0) {
    // Reuse canonical Markdown pagination, bookmarks and progress tracking.
    activityManager.goToReader(nativeapps::notes::pathFor(fileName_));
  } else if (index == 1 || index == 2) {
    startEdit(index == 1);
  } else if (index == 3) {
    confirmDelete();
  }
}

void NoteActionsActivity::startEdit(const bool title) {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(
      renderer, mappedInput, title ? tr(STR_YBX_NOTE_TITLE) : tr(STR_YBX_NOTE_BODY),
      title ? doc_.title : doc_.body,
      title ? nativeapps::notes::kMaxTitleBytes : nativeapps::notes::kMaxBodyBytes, InputType::Text);
  if (!keyboard) {
    GUI.drawPopup(renderer, tr(STR_MEMORY_ERROR));
    return;
  }
  startActivityForResult(std::move(keyboard), [this, title](const ActivityResult& result) {
    if (result.isCancelled) return;
    nativeapps::notes::Document update = doc_;
    if (title)
      update.title = std::get<KeyboardResult>(result.data).text;
    else
      update.body = std::get<KeyboardResult>(result.data).text;

    if (!nativeapps::notes::validDocument(update) || !nativeapps::notes::save(fileName_, update)) {
      GUI.drawPopup(renderer, tr(STR_YBX_NOTE_SAVE_FAILED));
      requestUpdate();
      return;
    }
    doc_ = std::move(update);
    requestUpdate();
  });
}

void NoteActionsActivity::confirmDelete() {
  auto confirm = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE),
                                                         doc_.title, tr(STR_CANCEL), tr(STR_DELETE));
  if (!confirm) {
    GUI.drawPopup(renderer, tr(STR_MEMORY_ERROR));
    return;
  }
  startActivityForResult(std::move(confirm), [this](const ActivityResult& result) {
    if (result.isCancelled) return;
    if (!nativeapps::notes::erase(fileName_)) {
      GUI.drawPopup(renderer, tr(STR_YBX_NOTE_SAVE_FAILED));
      requestUpdate();
      return;
    }
    finish();
  });
}
