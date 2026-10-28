#include "KOReaderSettingsActivity.h"

#include <GfxRenderer.h>
#include <HttpHeader.h>
#include <I18n.h>
#include <TrustedTime.h>

#include <memory>
#include <string>
#include <utility>

#include "KOReaderAuthActivity.h"
#include "KOReaderCredentialStore.h"
#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "util/YbxKosync.h"

namespace fui = freeink::ui;

namespace {
// Manual rows, addressed by their ordinal within the list tail.
constexpr int IDX_USERNAME = 0;
constexpr int IDX_PASSWORD = 1;
constexpr int IDX_SERVER_URL = 2;
constexpr int IDX_SERVER_TYPE = 3;
constexpr int IDX_CUSTOM_HEADER_1 = 4;
constexpr int IDX_CUSTOM_HEADER_2 = 5;
constexpr int IDX_MATCH_METHOD = 6;
constexpr int IDX_SEND_METADATA = 7;
constexpr int IDX_SYNC_BEHAVIOR = 8;
constexpr int IDX_SYNC_CLIPPINGS = 9;
constexpr int IDX_SIGN_UP = 10;
constexpr int IDX_AUTHENTICATE = 11;

// YBX rows, addressed by their slot at the head of the list.
constexpr int IDX_YBX_TOGGLE = 0;
constexpr int IDX_YBX_STATUS = 1;
constexpr int IDX_YBX_SWITCH = 2;

constexpr StrId menuNames[KOReaderSettingsActivity::MENU_ITEMS] = {
    StrId::STR_USERNAME,        StrId::STR_PASSWORD,        StrId::STR_SYNC_SERVER_URL,   StrId::STR_SERVER_TYPE,
    StrId::STR_CUSTOM_HEADER_1, StrId::STR_CUSTOM_HEADER_2, StrId::STR_DOCUMENT_MATCHING, StrId::STR_SEND_METADATA,
    StrId::STR_SYNC_BEHAVIOR,   StrId::STR_SYNC_CLIPPINGS,  StrId::STR_SIGN_UP,           StrId::STR_AUTHENTICATE};

constexpr int SERVER_TYPE_ITEMS = 3;
constexpr StrId serverTypeNames[SERVER_TYPE_ITEMS] = {StrId::STR_CROSSPOINT, StrId::STR_KOSYNC_SERVER,
                                                      StrId::STR_OTHER};
}  // namespace

KOReaderSettingsActivity::KOReaderSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("KOReaderSettings", renderer, mappedInput) {}

void KOReaderSettingsActivity::onEnter() {
  UiListActivity::onEnter();
  // Reconcile the pairing before the first paint: a reader signed in to YBX since
  // the last visit gets its sync configured here, without a URL or a password.
  ybxkosync::refresh();
}

int KOReaderSettingsActivity::ybxRowCount() const {
  if (!ybxkosync::isVisible()) return 0;
  return ybxkosync::needsTakeOver() ? YBX_MAX_ROWS : YBX_MAX_ROWS - 1;
}

void KOReaderSettingsActivity::stampRows(const int ybxRows) {
  // Labels never change (unlike the values, which track live store state), so they
  // are re-stamped only when the YBX prefix moves the manual rows to other slots.
  // Toggles are cleared with the slots they lived on.
  for (int i = 0; i < ROW_CAPACITY; i++) {
    rowItems_[i].toggle = false;
    rowItems_[i].toggleChecked = false;
  }
  if (ybxRows > IDX_YBX_TOGGLE) {
    rowItems_[IDX_YBX_TOGGLE].label = I18N.get(StrId::STR_YBX_SYNC);
    rowItems_[IDX_YBX_TOGGLE].actionValue = IDX_YBX_TOGGLE;
  }
  if (ybxRows > IDX_YBX_STATUS) {
    rowItems_[IDX_YBX_STATUS].label = I18N.get(StrId::STR_YBX_SYNC_STATUS);
    rowItems_[IDX_YBX_STATUS].actionValue = IDX_YBX_STATUS;
  }
  if (ybxRows > IDX_YBX_SWITCH) {
    rowItems_[IDX_YBX_SWITCH].label = I18N.get(StrId::STR_YBX_SYNC_SWITCH);
    rowItems_[IDX_YBX_SWITCH].actionValue = IDX_YBX_SWITCH;
  }
  for (int i = 0; i < MENU_ITEMS; i++) {
    const int slot = ybxRows + i;
    rowItems_[slot].label = I18N.get(menuNames[i]);
    rowItems_[slot].actionValue = static_cast<int16_t>(slot);
  }
  stampedYbxRows = ybxRows;
}

int KOReaderSettingsActivity::listCount() const { return ybxRowCount() + MENU_ITEMS; }

const char* KOReaderSettingsActivity::headerTitle() const { return tr(STR_KOREADER_SYNC); }

bool KOReaderSettingsActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

void KOReaderSettingsActivity::activateIndex(const int index) {
  if (optionPopup.isActive()) return;
  // Activation opens a keyboard/sub-activity or repaints a new value; a
  // lingering flash would gray an unrelated row.
  app.clearTapFlash();
  const int ybxRows = ybxRowCount();
  if (index < ybxRows) {
    if (index == IDX_YBX_TOGGLE) {
      ybxkosync::setEnabled(!ybxkosync::isEnabled());
    } else if (index == IDX_YBX_SWITCH) {
      ybxkosync::takeOver();
    } else {
      return;  // the status row only reports
    }
    requestUpdate();
    return;
  }
  const int row = index - ybxRows;
  if (row == IDX_USERNAME) {
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_KOREADER_USERNAME),
                                                                   KOREADER_STORE.getUsername(), 64, InputType::Text),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& kb = std::get<KeyboardResult>(result.data);
                               KOREADER_STORE.setCredentials(kb.text, KOREADER_STORE.getPassword());
                               KOREADER_STORE.saveToFile();
                             }
                           });
  } else if (row == IDX_PASSWORD) {
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_KOREADER_PASSWORD),
                                                KOREADER_STORE.getPassword(), 64, InputType::Password),
        [this](const ActivityResult& result) {
          if (!result.isCancelled) {
            const auto& kb = std::get<KeyboardResult>(result.data);
            KOREADER_STORE.setCredentials(KOREADER_STORE.getUsername(), kb.text);
            KOREADER_STORE.saveToFile();
          }
        });
  } else if (row == IDX_SERVER_URL) {
    const std::string currentUrl = KOREADER_STORE.getServerUrl();
    const std::string prefillUrl = currentUrl.empty() ? "https://" : currentUrl;
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SYNC_SERVER_URL),
                                                                   prefillUrl, 128, InputType::Url),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& kb = std::get<KeyboardResult>(result.data);
                               const std::string urlToSave =
                                   (kb.text == "https://" || kb.text == "http://") ? "" : kb.text;
                               KOREADER_STORE.setServerUrl(urlToSave);
                               KOREADER_STORE.saveToFile();
                             }
                           });
  } else if (row == IDX_SERVER_TYPE) {
    const auto current = KOREADER_STORE.getServerType();
    optionPopup.show(StrId::STR_SERVER_TYPE, serverTypeNames, SERVER_TYPE_ITEMS, static_cast<int>(current),
                     [this](const int selected) {
                       if (selected == static_cast<int>(KOREADER_STORE.getServerType())) return;
                       KOREADER_STORE.setServerType(static_cast<KOReaderServerType>(selected));
                       KOREADER_STORE.saveToFile();
                     });
    requestUpdate();
  } else if (row == IDX_CUSTOM_HEADER_1 || row == IDX_CUSTOM_HEADER_2) {
    // Single "Name: Value" line, parsed on save.
    const size_t slot = static_cast<size_t>(row == IDX_CUSTOM_HEADER_1 ? 0 : 1);
    const StrId label = row == IDX_CUSTOM_HEADER_1 ? StrId::STR_CUSTOM_HEADER_1 : StrId::STR_CUSTOM_HEADER_2;
    const std::string prefill = formatHttpHeaderLine(KOREADER_STORE.getCustomHeaders()[slot]);
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, I18N.get(label), prefill, 160, InputType::Text),
        [this, slot](const ActivityResult& result) {
          if (!result.isCancelled) {
            const auto& kb = std::get<KeyboardResult>(result.data);
            HttpHeader header;
            parseHttpHeaderLine(kb.text, header);
            KOREADER_STORE.setCustomHeader(slot, header.name, header.value);
            KOREADER_STORE.saveToFile();
          }
        });
  } else if (row == IDX_MATCH_METHOD) {
    const auto current = KOREADER_STORE.getMatchMethod();
    const auto newMethod =
        (current == DocumentMatchMethod::FILENAME) ? DocumentMatchMethod::BINARY : DocumentMatchMethod::FILENAME;
    KOREADER_STORE.setMatchMethod(newMethod);
    KOREADER_STORE.saveToFile();
    requestUpdate();
  } else if (row == IDX_SEND_METADATA) {
    KOREADER_STORE.setSendMetadata(!KOREADER_STORE.getSendMetadata());
    KOREADER_STORE.saveToFile();
    requestUpdate();
  } else if (row == IDX_SYNC_BEHAVIOR) {
    const auto current = KOREADER_STORE.getSyncBehavior();
    const auto newBehavior = (current == KOReaderSyncBehavior::ASK_EVERY_TIME) ? KOReaderSyncBehavior::SMART
                                                                               : KOReaderSyncBehavior::ASK_EVERY_TIME;
    KOREADER_STORE.setSyncBehavior(newBehavior);
    KOREADER_STORE.saveToFile();
    requestUpdate();
  } else if (row == IDX_SYNC_CLIPPINGS) {
    KOREADER_STORE.setSyncClippings(!KOREADER_STORE.getSyncClippings());
    KOREADER_STORE.saveToFile();
    requestUpdate();
  } else if (row == IDX_SIGN_UP) {
    // The YBX reader key is not an account password: signing up through it would
    // post md5(key) to an endpoint the bridge has no use for.
    if (!KOREADER_STORE.hasCredentials() || ybxkosync::isActive()) return;
    startActivityForResult(
        std::make_unique<KOReaderAuthActivity>(renderer, mappedInput, KOReaderAuthActivity::Mode::SIGN_UP),
        [](const ActivityResult&) {});
  } else if (row == IDX_AUTHENTICATE) {
    if (!KOREADER_STORE.hasCredentials()) return;
    startActivityForResult(std::make_unique<KOReaderAuthActivity>(renderer, mappedInput), [](const ActivityResult&) {});
  }
}

void KOReaderSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // Labels/actionValue are re-stamped only when the YBX prefix changes; each pass
  // refreshes the live value text by assigning into the existing rowValues_ strings
  // (no array growth) rather than building a new items/values vector.
  const int ybxRows = ybxRowCount();
  if (ybxRows != stampedYbxRows) stampRows(ybxRows);

  for (int i = 0; i < MENU_ITEMS; i++) {
    if (i == IDX_USERNAME) {
      const auto username = KOREADER_STORE.getUsername();
      rowValues_[i] = username.empty() ? tr(STR_NOT_SET) : username;
    } else if (i == IDX_PASSWORD) {
      rowValues_[i] = KOREADER_STORE.getPassword().empty() ? tr(STR_NOT_SET) : "******";
    } else if (i == IDX_SERVER_URL) {
      rowValues_[i] = KOREADER_STORE.getServerUrl();
      if (rowValues_[i].empty()) {
        std::string defaultUrl = KOREADER_STORE.getBaseUrl();
        const auto schemeEnd = defaultUrl.find("://");
        if (schemeEnd != std::string::npos) defaultUrl.erase(0, schemeEnd + 3);
        rowValues_[i] = std::string(tr(STR_DEFAULT_VALUE)) + ": " + defaultUrl;
      }
    } else if (i == IDX_SERVER_TYPE) {
      const auto type = KOREADER_STORE.getServerType();
      rowValues_[i] = type == KOReaderServerType::CROSSPOINT ? tr(STR_CROSSPOINT)
                      : type == KOReaderServerType::KOSYNC   ? tr(STR_KOSYNC_SERVER)
                                                             : tr(STR_OTHER);
    } else if (i == IDX_CUSTOM_HEADER_1 || i == IDX_CUSTOM_HEADER_2) {
      const size_t slot = static_cast<size_t>(i == IDX_CUSTOM_HEADER_1 ? 0 : 1);
      rowValues_[i] = formatHttpHeaderLine(KOREADER_STORE.getCustomHeaders()[slot], true);
      if (rowValues_[i].empty()) rowValues_[i] = tr(STR_NOT_SET);
    } else if (i == IDX_MATCH_METHOD) {
      rowValues_[i] =
          KOREADER_STORE.getMatchMethod() == DocumentMatchMethod::FILENAME ? tr(STR_FILENAME) : tr(STR_BINARY);
    } else if (i == IDX_SEND_METADATA || i == IDX_SYNC_CLIPPINGS) {
      rowValues_[i].clear();
    } else if (i == IDX_SYNC_BEHAVIOR) {
      rowValues_[i] =
          KOREADER_STORE.getSyncBehavior() == KOReaderSyncBehavior::SMART ? tr(STR_SMART_SYNC) : tr(STR_ASK_EVERY_TIME);
    } else {
      rowValues_[i] = KOREADER_STORE.hasCredentials() ? "" : std::string("[") + tr(STR_SET_CREDENTIALS_FIRST) + "]";
    }
    rowItems_[ybxRows + i].value = rowValues_[i].empty() ? nullptr : rowValues_[i].c_str();
  }
  GUI.setCheckboxRow(rowItems_[ybxRows + IDX_SEND_METADATA], KOREADER_STORE.getSendMetadata());
  GUI.setCheckboxRow(rowItems_[ybxRows + IDX_SYNC_CLIPPINGS], KOREADER_STORE.getSyncClippings());
  rowItems_[ybxRows + IDX_SIGN_UP].enabled = !ybxkosync::isActive();

  if (ybxRows > IDX_YBX_TOGGLE) GUI.setCheckboxRow(rowItems_[IDX_YBX_TOGGLE], ybxkosync::isEnabled());
  if (ybxRows > IDX_YBX_STATUS) rowItems_[IDX_YBX_STATUS].value = ybxStatusText().c_str();

  fui::ListProps props;
  props.items = rowItems_;
  props.count = static_cast<uint16_t>(ybxRows + MENU_ITEMS);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;               // air between the value and the row edge
  // Label at the value's font size: both sides of the row read as one unit.
  // maxLines=2 also marks the style caller-owned (see textStyleUnset).
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

const std::string& KOReaderSettingsActivity::ybxStatusText() {
  const auto status = ybxkosync::getStatus();
  const char* text;
  bool dated;
  switch (status) {
    case ybxkosync::Status::Disabled:
      text = tr(STR_YBX_SYNC_DISABLED);
      dated = false;
      break;
    case ybxkosync::Status::NotPaired:
      text = tr(STR_YBX_SYNC_NOT_PAIRED);
      dated = false;
      break;
    case ybxkosync::Status::ManualServer:
      text = tr(STR_YBX_SYNC_MANUAL);
      dated = false;
      break;
    case ybxkosync::Status::Mismatch:
      text = tr(STR_YBX_SYNC_MISMATCH);
      dated = false;  // waiting on the reader, not on a transaction
      break;
    case ybxkosync::Status::NeverSynced:
      text = tr(STR_YBX_SYNC_NEVER);
      dated = false;
      break;
    case ybxkosync::Status::AuthRejected:
      text = tr(STR_YBX_SYNC_REJECTED);
      dated = true;
      break;
    case ybxkosync::Status::Refused:
      text = tr(STR_YBX_SYNC_REFUSED);
      dated = true;
      break;
    case ybxkosync::Status::Offline:
      text = tr(STR_YBX_SYNC_OFFLINE);
      dated = true;
      break;
    case ybxkosync::Status::Failed:
      text = tr(STR_SYNC_FAILED_MSG);
      dated = true;
      break;
    case ybxkosync::Status::Pushed:
      text = tr(STR_YBX_SYNC_PUSHED);
      dated = true;
      break;
    case ybxkosync::Status::Pulled:
      text = tr(STR_YBX_SYNC_PULLED);
      dated = true;
      break;
    case ybxkosync::Status::Already:
      text = tr(STR_YBX_SYNC_ALREADY);
      dated = true;
      break;
    default:
      text = tr(STR_YBX_SYNC_NEVER);
      dated = false;
      break;
  }
  ybxStatusValue_ = text;
  int value;
  int unit;
  if (dated && ybxkosync::describeAge(ybxkosync::getLastOutcomeAt(), trustedtime::trustedNow(), value, unit)) {
    char age[32];
    const StrId ageKey = unit == 0   ? StrId::STR_YBX_SYNC_MINUTES
                         : unit == 1 ? StrId::STR_YBX_SYNC_HOURS
                                     : StrId::STR_YBX_SYNC_DAYS;
    snprintf(age, sizeof(age), I18N.get(ageKey), value);
    ybxStatusValue_ += ", ";
    ybxStatusValue_ += age;
  }
  return ybxStatusValue_;
}

void KOReaderSettingsActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
}
