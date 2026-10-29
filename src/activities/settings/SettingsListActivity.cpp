#include "SettingsListActivity.h"

#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "SettingsActivity.h"
#include "AboutActivity.h"
#include "LanguageSelectActivity.h"
#include "OtaUpdateActivity.h"
#include "KOReaderSettingsActivity.h"
#include "activities/ybx/YbxAccountActivity.h"
#include "util/YbxReadingData.h"
#include "util/YbxKosync.h"
#include "CrossPointSettings.h"
#include "I18n.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;

namespace {
constexpr int16_t ICON_SIZE = 32;
// Row icons, indexed by SettingsSection.
constexpr const freeink::Icon* SECTION_ICONS[SETTINGS_SECTION_COUNT] = {
    &icon_settings_32, &icon_smartphone_32, &icon_book_open_32, &icon_pointer_32, &icon_library_32, &icon_wifi_32,
};
}  // namespace

SettingsListActivity::SettingsListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           const SettingsSection openSection)
    : UiListActivity("SettingsList", renderer, mappedInput), pendingSection(openSection) {
#if FREEINK_DEVICE_X4 || FREEINK_DEVICE_X3 || FREEINK_DEVICE_X4PRO
  // This is the YBX Reader shell; only its default Settings root gets
  // the YBX dashboard. Direct jumps to a system section remain unchanged.
  ybxRootEntry_ = openSection == SettingsSection::End;
  ybxDashboard_ = ybxRootEntry_;
#endif
}

void SettingsListActivity::onEnter() {
  UiListActivity::onEnter();
  rebuildRows();
}

void SettingsListActivity::onExit() {
  UiListActivity::onExit();
  UITheme::getInstance().reload();  // Re-apply theme in case it was changed
}

void SettingsListActivity::loop() {
  if (pendingSection != SettingsSection::End) {
    const SettingsSection section = pendingSection;
    pendingSection = SettingsSection::End;
    openSection(section);
    return;
  }
  UiListActivity::loop();
}

// Rebuilds row labels; call when entering and after a section returns (the
// UI language may have changed).
void SettingsListActivity::rebuildRows() {
  constexpr StrId ybxLabels[] = {
      StrId::STR_OPDS_MY_ACCOUNT, StrId::STR_YBX_ACCOUNT_SOURCES,
      StrId::STR_YBX_DASH_SYNC, StrId::STR_YBX_DASH_APPEARANCE,
      StrId::STR_LANGUAGE, StrId::STR_SLEEP_SCREEN,
      StrId::STR_CHECK_UPDATES, StrId::STR_YBX_DASH_SUPPORT,
      StrId::STR_YBX_DASH_SYSTEM,
  };
  static_assert(std::size(ybxLabels) == ybxsettings::ORDER.size());
  for (size_t i = 0; i < ybxRows_.size(); ++i) {
    ybxRows_[i].label = I18N.get(ybxLabels[i]);
    ybxRows_[i].actionValue = static_cast<int16_t>(i);
    ybxRows_[i].bold = false;
  }
  for (size_t i = 0; i < SETTINGS_SECTION_COUNT; ++i) {
    auto& item = rowItems_[i];
    item.label = I18N.get(SETTINGS_SECTION_TITLES[i]);
    item.icon = fui::bitmapFromIcon(*SECTION_ICONS[i]);
    item.actionValue = static_cast<int16_t>(i);
  }
  // Gather token state on the main task, never during render.
  populateYbxValues();
}

void SettingsListActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  if (ybxDashboard_) {
    // Keep a separate title row, like 08-settings.png. This screen does
    // not erase the clock/battery band or replace the standard system menu.
    const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const int contentTop = safe.y + metrics.topPadding + metrics.headerHeight;
    screen.setContentMarginFromScreen(fui::Insets{
        static_cast<int16_t>(contentTop),
        static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
        static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)),
        static_cast<int16_t>(safe.x)});
    const int side = std::max(12, metrics.contentSidePadding);
    auto heading = screen.theme().titleText;
    heading.bold = true;
    heading.maxLines = 1;
    screen.target().text(fui::Rect{static_cast<int16_t>(safe.x + side),
                                    static_cast<int16_t>(contentTop + 6),
                                    static_cast<int16_t>(std::max(0, safe.width - 2 * side)), 34},
                          tr(STR_SETTINGS_TITLE), heading);
    screen.spacer(49);

    fui::ListProps props;
    props.items = ybxRows_.data();
    props.count = static_cast<uint16_t>(ybxRows_.size());
    props.action = ACTION_ROW;
    props.inputMask = fui::InputTouch;
    props.rowHeight = static_cast<int16_t>(safe.height >= 620 ? 61 : 52);
    props.rowGap = 1;
    props.rowRadius = 0;
    props.rowInset = 0;
    props.separatorPaint = fui::Paint::solid(fui::Color::DarkGray);
    props.valueInset = 15;
    props.labelText = screen.theme().bodyText;
    props.labelText.maxLines = 1;
    props.valueText = screen.theme().smallText;
    props.valueText.maxLines = 1;
    syncListViewport(screen, props);
    screen.list(props);
    return;
  }
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  // Rows are as tall as a label plus a small second line, giving the icons air.
  const auto& theme = screen.theme();
  const int16_t textHeight = static_cast<int16_t>(screen.target().lineHeight(theme.bodyText.font) +
                                                  screen.target().lineHeight(theme.smallText.font));
  const int16_t paddingY = mappedInput.hasTouch() ? theme.listTouchRowPaddingY : theme.listRowPaddingY;
  props.rowHeight = static_cast<int16_t>(std::max<int16_t>(textHeight, ICON_SIZE) + 2 * paddingY);
  syncListViewport(screen, props);
  screen.list(props);
}

void SettingsListActivity::activateIndex(const int index) {
  app.clearTapFlash();
  if (ybxDashboard_) {
    if (ybxsettings::selectable(index)) openYbxRow(index);
    return;
  }
  if (index >= 0 && index < static_cast<int>(SETTINGS_SECTION_COUNT))
    openSection(static_cast<SettingsSection>(index));
}

void SettingsListActivity::openSection(const SettingsSection section) {
  nav.selected = static_cast<int>(section);
  auto activity = makeUniqueNoThrow<SettingsActivity>(renderer, mappedInput, section);
  if (!activity) {
    LOG_ERR("SETTINGS", "OOM: SettingsActivity");
    return;
  }
  startActivityForResult(std::move(activity), [this](const ActivityResult&) {
    rebuildRows();
    // A theme change inside the section must restyle this screen too.
    resetUi();
    requestUpdate();
  });
}

void SettingsListActivity::onBackButton() {
  if (!ybxDashboard_) {
#if FREEINK_DEVICE_X4 || FREEINK_DEVICE_X3 || FREEINK_DEVICE_X4PRO
    if (!ybxRootEntry_) {
      onGoHome();  // Direct section launches retain original back behavior.
      return;
    }
    ybxDashboard_ = true;
    nav.reset();
    rebuildRows();
    resetUi();
    requestUpdate();
    return;
#endif
  }
  onGoHome();
}

void SettingsListActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Version rides in the header's trailing label slot: the footer position
  // conflicts with button hints on non-touch devices.
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                 ybxDashboard_ ? tr(STR_YBX) : tr(STR_SETTINGS_TITLE),
                 ybxDashboard_ ? nullptr : CROSSPOINT_VERSION);
}

// Live values come only from persisted firmware state. A source list that was
// never fetched is not reported as "2 connected", unlike the illustrative PNG.
void SettingsListActivity::populateYbxValues() {
  const auto account = ybx::readingConnectionState();
  const bool paired = account == ybx::ReadingResult::Ok;
  ybxValues_[0] = paired ? tr(STR_YBX_DASH_CONFIGURED) : tr(STR_YBX_SYNC_NOT_PAIRED);
  ybxValues_[1] = tr(STR_YBX_DASH_OPEN);
  ybxValues_[2] = !paired ? tr(STR_YBX_SYNC_NOT_PAIRED)
                          : (ybxkosync::isEnabled() ? tr(STR_ENABLED) : tr(STR_DISABLED));
  switch (SETTINGS.uiTheme) {
    case CrossPointSettings::CLASSIC: ybxValues_[3] = tr(STR_THEME_CLASSIC); break;
    case CrossPointSettings::LYRA: ybxValues_[3] = tr(STR_THEME_LYRA); break;
    case CrossPointSettings::LYRA_3_COVERS: ybxValues_[3] = tr(STR_THEME_LYRA_EXTENDED); break;
    case CrossPointSettings::ROUNDEDRAFF: ybxValues_[3] = tr(STR_THEME_ROUNDEDRAFF); break;
    case CrossPointSettings::COVER_GRID: ybxValues_[3] = tr(STR_THEME_COVER_GRID); break;
    default: ybxValues_[3] = tr(STR_CONFIGURE); break;
  }
  ybxValues_[4] = I18N.getLanguageName(I18N.getLanguage());
  ybxValues_[5] = tr(STR_CONFIGURE);
  ybxValues_[6] = tr(STR_CHECK_UPDATES);
  // There is no implemented report-submission endpoint. The Support row
  // therefore opens real diagnostics instead of claiming to send a report.
  ybxValues_[7] = tr(STR_YBX_DASH_INFO);
  ybxValues_[8] = tr(STR_YBX_DASH_ALL);
  for (size_t i = 0; i < ybxRows_.size(); ++i)
    ybxRows_[i].value = ybxValues_[i].c_str();
}

void SettingsListActivity::showSystemSettings() {
  ybxDashboard_ = false;
  nav.reset();
  rebuildRows();
  resetUi();
  requestUpdate();
}

void SettingsListActivity::openYbxRow(const int index) {
  const auto destination = ybxsettings::destination(ybxsettings::ORDER[index]);
  if (destination == ybxsettings::Destination::AllSettings) {
    showSystemSettings();
    return;
  }
  std::unique_ptr<Activity> activity;
  switch (destination) {
    case ybxsettings::Destination::Account:
      activity = makeUniqueNoThrow<YbxAccountActivity>(renderer, mappedInput);
      break;
    case ybxsettings::Destination::Sync:
      if (ybx::readingConnectionState() == ybx::ReadingResult::Ok)
        activity = makeUniqueNoThrow<KOReaderSettingsActivity>(renderer, mappedInput);
      else
        activity = makeUniqueNoThrow<YbxAccountActivity>(renderer, mappedInput);
      break;
    case ybxsettings::Destination::DisplayTheme:
      activity = makeUniqueNoThrow<SettingsActivity>(
          renderer, mappedInput, SettingsSection::Display, StrId::STR_UI_THEME);
      break;
    case ybxsettings::Destination::Language:
      activity = makeUniqueNoThrow<LanguageSelectActivity>(renderer, mappedInput);
      break;
    case ybxsettings::Destination::DisplaySleep:
      activity = makeUniqueNoThrow<SettingsActivity>(
          renderer, mappedInput, SettingsSection::Display, StrId::STR_SLEEP_SCREEN);
      break;
    case ybxsettings::Destination::Updates:
      activity = makeUniqueNoThrow<OtaUpdateActivity>(renderer, mappedInput);
      break;
    case ybxsettings::Destination::About:
      activity = makeUniqueNoThrow<AboutActivity>(renderer, mappedInput);
      break;
    case ybxsettings::Destination::AllSettings:
      return;
  }
  if (!activity) {
    LOG_ERR("YBXSettings", "OOM opening settings destination %d", static_cast<int>(destination));
    return;
  }
  startActivityForResult(std::move(activity), [this](const ActivityResult&) {
    // Language/theme may have changed; refresh all labels and FreeInk tokens.
    rebuildRows();
    resetUi();
    requestUpdate();
  });
}
