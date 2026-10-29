#include "YbxAccountActivity.h"

#include <ArduinoJson.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "util/PluginHttp.h"
#include "util/PluginLocations.h"
#include "util/YbxAccountLayout.h"
#include "util/QrUtils.h"
#include "network/TlsTransport.h"
#include "activities/reader/QrDisplayActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include <cctype>

namespace fui = freeink::ui;
namespace {
constexpr size_t kMaxManifestBytes = 8192;
constexpr size_t kMaxListsBytes = 8192;
constexpr uint32_t kRequestTimeoutMs = 12000;
// A plugin manifest must not be allowed to send the device bearer token to
// an arbitrary host merely because it declares a custom browse URL.
constexpr const char* kListsUrl = "https://my.ybx.app/api/v1/crosspoint/catalog/lists";
constexpr const char* kStartUrl = "https://my.ybx.app/api/v1/crosspoint/auth/device";
constexpr const char* kPollUrl = "https://my.ybx.app/api/v1/crosspoint/auth/token";
constexpr const char* kGrant = "urn:ietf:params:oauth:grant-type:device_code";
constexpr size_t kMaxPairResponse = 3072;
constexpr const char* kTokenFilename = "token.json";
constexpr const char* kTokenPath = "access_token";

bool loadPairTarget(std::string& file) {
  file.clear();
  if (!SETTINGS.pluginsEnabled) return false;
  const std::string dir = PluginLocations::findPluginDir("ybx");
  if (dir.empty()) return false;
  std::string raw;
  if (!Storage.readFileToString("YBX", dir + "/device.json", kMaxManifestBytes, raw)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, raw) != DeserializationError::Ok ||
      std::string(doc["token"]["file"] | "") != kTokenFilename ||
      std::string(doc["token"]["path"] | "") != kTokenPath ||
      std::string(doc["auth"]["type"] | "") != "device_code" ||
      std::string(doc["auth"]["request"]["url"] | "") != kStartUrl ||
      std::string(doc["auth"]["poll"]["url"] | "") != kPollUrl ||
      std::string(doc["browse"]["lists_url"] | "") != kListsUrl)
    return false;
  file = pluginhttp::inPluginDir(dir, kTokenFilename);
  return true;
}

bool deviceRemovedHeader(const pluginhttp::Headers& headers) {
  for (const auto& h : headers) {
    std::string name = h.first;
    for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (name == "x-ybx-device-removed" && ybxaccount::headerIsTrue(h.second)) return true;
  }
  return false;
}

struct ScopedCacheStorage {
  bool exists(const std::string& path) { return Storage.exists(path.c_str()); }
  bool remove(const std::string& path) { return Storage.remove(path.c_str()); }
};

pluginhttp::Headers jsonHeaders() {
  return {{"Content-Type", "application/json"}};
}

int postPairRequest(const char* url, const std::string& body, String& response) {
  const unsigned long deadline = millis() + kRequestTimeoutMs;
  return pluginhttp::request(nullptr, url, "POST", body, jsonHeaders(), response, kMaxPairResponse,
                             nullptr, [deadline] { return ybxaccount::deadlineReached(millis(), deadline); },
                             kRequestTimeoutMs);
}


struct Connection {
  std::string token;
  pluginhttp::Headers headers;
};

enum class ConfigResult { Ready, Missing, Unsupported };
ConfigResult loadConnection(Connection& out) {
  if (!SETTINGS.pluginsEnabled) return ConfigResult::Missing;
  const std::string dir = PluginLocations::findPluginDir("ybx");
  if (dir.empty()) return ConfigResult::Missing;
  std::string raw;
  if (!Storage.readFileToString("YBX", dir + "/device.json", kMaxManifestBytes, raw))
    return ConfigResult::Unsupported;
  JsonDocument filter;
  filter["token"] = true;
  filter["browse"]["lists_url"] = true;
  filter["browse"]["lists_headers"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, raw, DeserializationOption::Filter(filter)))
    return ConfigResult::Unsupported;
  if (std::string(doc["browse"]["lists_url"] | "") != kListsUrl)
    return ConfigResult::Unsupported;
  const std::string file = pluginhttp::inPluginDir(dir, doc["token"]["file"] | "");
  if (file.empty() ||
      !pluginhttp::loadTokenFromFile(file, doc["token"]["path"] | "access_token", out.token) ||
      out.token.empty())
    return ConfigResult::Missing;
  pluginhttp::readHeaders(doc["browse"]["lists_headers"], out.headers);
  bool hasAuth = false;
  for (auto& header : out.headers) {
    if (header.first == "Authorization") {
      hasAuth = true;
      if (header.second != "Bearer {token}") return ConfigResult::Unsupported;
      header.second = "Bearer " + out.token;
    }
  }
  return hasAuth ? ConfigResult::Ready : ConfigResult::Unsupported;
}
}  // namespace

void YbxAccountActivity::onEnter() {
  Activity::onEnter();
  sources_.clear();
  fetched_ = false;
  pairing_.clear();
  pairPending_ = false;
  pairExpired_ = false;
  switchingAccount_ = false;
  Connection info;
  const ConfigResult ready = loadConnection(info);
  state_ = ready == ConfigResult::Ready ? State::Ready
           : ready == ConfigResult::Missing ? State::NotConnected
                                            : State::Unsupported;
  resetUi();
  app.on(1, &YbxAccountActivity::action, this);
  app.setScreen(&YbxAccountActivity::screen, this);
  refreshPending_ = ready == ConfigResult::Ready && WiFi.status() == WL_CONNECTED;
  requestUpdate();
}
void YbxAccountActivity::onExit() {
  cancelPairing();
  closeRouting();
  if (ownsWifi_) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
  Activity::onExit();
}
void YbxAccountActivity::screen(UiScreen& ui, void* user) {
  static_cast<YbxAccountActivity*>(user)->draw(ui);
}
void YbxAccountActivity::action(const fui::ActionEvent& event, void* user) {
  auto& self = *static_cast<YbxAccountActivity*>(user);
  self.pending_ = event.value;
  self.app.clearTapFlash();
}
void YbxAccountActivity::refresh() {
  Connection info;
  const auto ready = loadConnection(info);
  if (ready != ConfigResult::Ready) {
    state_ = ready == ConfigResult::Missing ? State::NotConnected : State::Unsupported;
    fetched_ = false;
    sources_.clear();
    requestUpdate();
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    ownsWifi_ = true;
    startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                           [this](const ActivityResult& result) {
                             returnedFromChild_ = true;
                             refreshPending_ = !result.isCancelled;
                             waitConfirmRelease_ = mappedInput.isPressed(MappedInputManager::Button::Confirm);
                             waitBackRelease_ = mappedInput.isPressed(MappedInputManager::Button::Back);
                           });
    return;
  }
  {
    RenderLock lock(*this);
    state_ = State::Loading;
  }
  requestUpdateAndWait();
  String response;
  const unsigned long deadline = millis() + kRequestTimeoutMs;
  pluginhttp::Headers responseHeaders;
  const int code = pluginhttp::request(
      nullptr, kListsUrl, "GET", "", info.headers, response, kMaxListsBytes,
      &responseHeaders, [&] { return ybxaccount::deadlineReached(millis(), deadline); }, kRequestTimeoutMs);
  State next = State::Failed;
  ybxaccount::Summary incoming;
  std::string detailCode;
  if (code == 409) {
    JsonDocument error;
    if (!deserializeJson(error, response)) detailCode = error["detail"]["code"] | "";
  }
  switch (ybxaccount::checkedListsOutcome(code, deviceRemovedHeader(responseHeaders), detailCode)) {
    case ybxaccount::ListsOutcome::DeviceRemoved:
      next = State::DeviceRemoved;
      break;
    case ybxaccount::ListsOutcome::InvalidToken:
      next = State::InvalidToken;
      break;
    case ybxaccount::ListsOutcome::ProviderSession:
      next = State::ProviderSession;
      break;
    case ybxaccount::ListsOutcome::Ok: {
    JsonDocument doc;
    if (!deserializeJson(doc, response)) {
      JsonArrayConst lists = doc["lists"].as<JsonArrayConst>();
      if (!lists.isNull() && lists.size() <= 32) {
        for (JsonObjectConst row : lists) {
          incoming.addList(ybxaccount::classifyListUrl(row["url"] | ""));
        }
        next = State::Ready;
      }
    }
    break;
    }
    default:
      break;
  }
  // Once the token changes or is rejected, never show another account's
  // remembered source list as if it were current.
  {
    RenderLock lock(*this);
    state_ = next;
    if (next == State::Ready) {
      sources_ = incoming;
      fetched_ = true;
    } else {
      // Do not display stale source states after any unverified refresh,
      // especially a revoked device token or a failed network request.
      sources_.clear();
      fetched_ = false;
    }
  }
  requestUpdate();
}
void YbxAccountActivity::cancelPairing() {
  // Device code is a short-lived credential and must not survive cancel,
  // sleep, activity replacement or expiry. The existing token is untouched.
  pairing_.clear();
  pairPending_ = false;
  pairExpired_ = false;
  if (state_ == State::Pairing || state_ == State::PairFailed)
    state_ = switchingAccount_ ? State::Ready : State::NotConnected;
  switchingAccount_ = false;
}

void YbxAccountActivity::beginPairing() {
  // A valid existing account must not be silently replaced. The optional
  // account-switch/revoke workflow needs its own explicit confirmation.
  if ((state_ == State::Ready && !switchingAccount_) ||
      state_ == State::Loading || state_ == State::Pairing) return;
  std::string tokenFile;
  if (!loadPairTarget(tokenFile)) {
    state_ = State::Unsupported;
    requestUpdate();
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    ownsWifi_ = true;
    pairPending_ = true;
    startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                           [this](const ActivityResult& result) {
                             returnedFromChild_ = true;
                             pairPending_ = !result.isCancelled;
                             if (result.isCancelled) switchingAccount_ = false;
                             waitConfirmRelease_ = mappedInput.isPressed(MappedInputManager::Button::Confirm);
                             waitBackRelease_ = mappedInput.isPressed(MappedInputManager::Button::Back);
                           });
    return;
  }
  pairing_.clear();
  pairExpired_ = false;
  pairPending_ = false;
  {
    RenderLock lock(*this);
    state_ = State::Loading;
  }
  requestUpdateAndWait();
  String response;
  const int status = postPairRequest(kStartUrl, R"({"client_id":"crosspoint"})", response);
  JsonDocument doc;
  if (status != 200 || deserializeJson(doc, response) != DeserializationError::Ok) {
    state_ = State::PairFailed;
    requestUpdate();
    return;
  }
  const std::string user = doc["user_code"] | "";
  const std::string device = doc["device_code"] | "";
  const std::string uri = doc["verification_uri_complete"] | "";
  if (!ybxaccount::validDeviceChallenge(user, device, uri)) {
    state_ = State::PairFailed;
    requestUpdate();
    return;
  }
  pairing_.userCode = user;
  pairing_.deviceCode = device;
  pairing_.verifyUrl = uri;
  pairing_.intervalMs = ybxaccount::clampPollIntervalMs(doc["interval"] | 5L);
  const auto now = millis();
  pairing_.nextPollMs = now + pairing_.intervalMs;
  pairing_.deadlineMs = now + ybxaccount::clampCodeLifetimeMs(doc["expires_in"] | 900L);
  state_ = State::Pairing;
  selection_ = 1;
  requestUpdate();
}

void YbxAccountActivity::pollPairing() {
  if (state_ != State::Pairing) return;
  if (ybxaccount::deadlineReached(millis(), pairing_.deadlineMs)) {
    pairing_.clear();
    pairExpired_ = true;
    state_ = State::PairFailed;
    requestUpdate();
    return;
  }
  if (!ybxaccount::deadlineReached(millis(), pairing_.nextPollMs)) return;
  pairing_.nextPollMs = millis() + pairing_.intervalMs;
  if (WiFi.status() != WL_CONNECTED) return;

  JsonDocument request;
  request["grant_type"] = kGrant;
  request["device_code"] = pairing_.deviceCode;
  std::string body;
  serializeJson(request, body);  // Never template-interpolate a secret into JSON.
  String response;
  const int status = postPairRequest(kPollUrl, body, response);
  if (state_ != State::Pairing) return;
  if (status < 0) {
    // A network timeout can recover, but clock/CA/hostname trust must fail
    // closed instead of retrying a bad certificate throughout the full TTL.
    if (tlspolicy::isTrustFailure(tlspolicy::lastFailure())) {
      pairing_.clear();
      state_ = State::PairFailed;
      requestUpdate();
    }
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, response) != DeserializationError::Ok) {
    state_ = State::PairFailed;
  } else if (status == 200) {
    const std::string token = doc["access_token"] | "";
    if (std::string(doc["token_type"] | "") != "Bearer" ||
        !ybxaccount::validAccessToken(token)) {
      state_ = State::PairFailed;
    } else {
      std::string file;
      if (!loadPairTarget(file)) {
        state_ = State::PairFailed;
      } else {
        const std::string dir = PluginLocations::findPluginDir("ybx");
        // Only rebuildable token-scoped material is removed. Never delete
        // books, user-local reading sessions, bookmarks or progress.
        ScopedCacheStorage cacheStorage;
        const bool clean = ybxaccount::clearScopedCaches(cacheStorage, dir);
        if (!clean || !pluginhttp::saveTokenToFileAtomic(file, kTokenPath, token))
          state_ = State::PairFailed;
        else {
          switchingAccount_ = false;
          pairing_.clear();
          sources_.clear();
          fetched_ = false;
          state_ = State::Ready;
          refreshPending_ = true;
        }
      }
    }
  } else {
    const std::string error = doc["error"] | "";
    switch (ybxaccount::pollOutcomeFromErrorCode(error)) {
      case ybxaccount::PollOutcome::Pending:
        if (error != "authorization_pending") state_ = State::PairFailed;
        break;
      case ybxaccount::PollOutcome::SlowDown:
        pairing_.intervalMs = ybxaccount::slowedDownIntervalMs(pairing_.intervalMs);
        pairing_.nextPollMs = millis() + pairing_.intervalMs;
        break;
      case ybxaccount::PollOutcome::Expired:
        pairExpired_ = true;
        state_ = State::PairFailed;
        break;
      case ybxaccount::PollOutcome::Denied:
        state_ = State::PairFailed;
        break;
    }
  }
  if (state_ != State::Pairing) pairing_.clear();
  requestUpdate();
}

void YbxAccountActivity::showPairQr() {
  if (state_ != State::Pairing || pairing_.verifyUrl.empty()) return;
  startActivityForResult(std::make_unique<QrDisplayActivity>(renderer, mappedInput, pairing_.verifyUrl),
                         [this](const ActivityResult&) {
                           returnedFromChild_ = true;
                           waitConfirmRelease_ = mappedInput.isPressed(MappedInputManager::Button::Confirm);
                           waitBackRelease_ = mappedInput.isPressed(MappedInputManager::Button::Back);
                           requestUpdate();
                         });
}

void YbxAccountActivity::loop() {
  if (std::exchange(returnedFromChild_, false)) return;
  const bool allowBack = !waitBackRelease_;
  const bool allowConfirm = !waitConfirmRelease_;
  if (!mappedInput.isPressed(MappedInputManager::Button::Back)) waitBackRelease_ = false;
  if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) waitConfirmRelease_ = false;
  if (pairPending_) {
    pairPending_ = false;
    beginPairing();
    return;
  }
  if (refreshPending_) {
    refreshPending_ = false;
    refresh();
    return;
  }
  if (state_ == State::Pairing) {
    pollPairing();
    if (state_ != State::Pairing) return;  // Do not process stale taps after token exchange/expiry.
  }
  if (managePopup_.handleInput(mappedInput, [this] { requestUpdate(); })) return;
  if (allowBack && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (state_ == State::Pairing) {
      cancelPairing();
      requestUpdate();
    } else {
      finish();
    }
    return;
  }
  const auto touch = routeTouch(mappedInput);
  if (touch.routed && app.invalidated()) requestUpdate();
  navigator_.onNext([this] { selection_ = (selection_ + 1) % 3; requestUpdate(); });
  navigator_.onPrevious([this] { selection_ = (selection_ + 2) % 3; requestUpdate(); });
  const int chosen = std::exchange(pending_, -1);
  if (chosen >= 0)
    activate(chosen);
  else if (allowConfirm && mappedInput.wasReleased(MappedInputManager::Button::Confirm))
    activate(selection_);
}
void YbxAccountActivity::activate(int value) {
  if (state_ == State::Loading) return;
  if (state_ == State::Pairing) {
    if (value == 0) {
      cancelPairing();
      requestUpdate();
    } else if (value == 1) {
      // A fresh one-time code invalidates the previous challenge, but keeps
      // the already-approved account and explicit switch confirmation.
      const bool switching = switchingAccount_;
      pairing_.clear();
      state_ = switching ? State::Ready : State::NotConnected;
      switchingAccount_ = switching;
      beginPairing();
    } else if (value == 2) {
      showPairQr();
    }
    return;
  }
  if (value == 0) finish();
  if (value == 1) {
    if (state_ == State::ProviderSession) {
      showCabinetSourcesQr();
    } else if (state_ == State::NotConnected || state_ == State::InvalidToken ||
               state_ == State::DeviceRemoved || state_ == State::PairFailed) {
      beginPairing();
    } else {
      refresh();
    }
  }
  if (value == 2) showManageOptions();
  if (value == 3) showCabinetSourcesQr();
}
void YbxAccountActivity::openPluginCatalog() { activityManager.goToPlugins(false); }

void YbxAccountActivity::showCabinetSourcesQr() {
  startActivityForResult(
      std::make_unique<QrDisplayActivity>(renderer, mappedInput,
                                          std::string(ybxaccount::CABINET_SOURCES_URL)),
      [this](const ActivityResult&) {
        returnedFromChild_ = true;
        waitConfirmRelease_ = mappedInput.isPressed(MappedInputManager::Button::Confirm);
        waitBackRelease_ = mappedInput.isPressed(MappedInputManager::Button::Back);
        requestUpdate();
      });
}

void YbxAccountActivity::confirmSwitchAccount() {
  if (state_ != State::Ready) return;
  auto dialog = makeUniqueNoThrow<ConfirmationActivity>(
      renderer, mappedInput, tr(STR_YBX_ACCOUNT_SWITCH),
      tr(STR_YBX_ACCOUNT_SWITCH_CONFIRM), tr(STR_CANCEL), tr(STR_CONFIRM));
  if (!dialog) return;
  startActivityForResult(std::move(dialog), [this](const ActivityResult& result) {
    returnedFromChild_ = true;
    waitConfirmRelease_ = mappedInput.isPressed(MappedInputManager::Button::Confirm);
    waitBackRelease_ = mappedInput.isPressed(MappedInputManager::Button::Back);
    if (result.isCancelled) return;
    switchingAccount_ = true;
    pairPending_ = true;  // Defer network activity out of child completion.
  });
}

void YbxAccountActivity::showManageOptions() {
  if (state_ == State::Loading || state_ == State::Pairing) return;
  static constexpr StrId READY[] = {
      StrId::STR_YBX_ACCOUNT_MANAGE, StrId::STR_YBX_ACCOUNT_SWITCH,
      StrId::STR_YBX_HOME_CATALOGS, StrId::STR_CANCEL};
  static constexpr StrId LIMITED[] = {
      StrId::STR_YBX_ACCOUNT_MANAGE, StrId::STR_YBX_HOME_CATALOGS, StrId::STR_CANCEL};
  const bool canSwitch = state_ == State::Ready;
  managePopup_.show(StrId::STR_OPDS_MY_ACCOUNT, canSwitch ? READY : LIMITED,
                    canSwitch ? 4 : 3, 0, [this, canSwitch](int choice) {
    if (choice == 0) showCabinetSourcesQr();
    else if (canSwitch && choice == 1) confirmSwitchAccount();
    else if (choice == (canSwitch ? 2 : 1)) openPluginCatalog();
  });
  requestUpdate();
}
void YbxAccountActivity::button(UiScreen& ui, fui::Rect bounds, int value, const char* label) {
  const auto ink = fui::Paint::solid(fui::Color::Black);
  const auto paper = fui::Paint::solid(fui::Color::White);
  // Explicit white/black styles match YBX Home on four orientations.
  props_ = {};
  props_.label = label;
  props_.action = 1;
  props_.value = value;
  props_.inputMask = state_ == State::Loading ? 0 : fui::InputTouch;
  props_.minTouchSize = 0;
  props_.text = ui.theme().smallText;
  props_.text.bold = value == 1;
  props_.text.maxLines = 2;
  props_.state = state_ == State::Loading ? fui::StateDisabled
                 : selection_ == value ? fui::StateSelected : fui::StateNormal;
  props_.styles.explicitlySet = true;
  props_.styles.normal.background = value == 1 ? ink : paper;
  props_.styles.normal.foreground = value == 1 ? paper : ink;
  props_.styles.normal.border = ink;
  props_.styles.normal.borderWidth = value == 1 ? 2 : 1;
  props_.styles.selected = props_.styles.normal;
  props_.styles.selected.background = value == 1 ? paper : ink;
  props_.styles.selected.foreground = value == 1 ? ink : paper;
  props_.styles.selected.borderWidth = 2;
  props_.styles.active = props_.styles.selected;
  fui::button(ui.frame(), bounds, props_);
}

void YbxAccountActivity::draw(UiScreen& ui) {
  const bool buttonHints = !uiTarget.deviceContext().hasTouch;
  const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, buttonHints);
  const int smallLine = ui.target().lineHeight(ui.theme().smallText.font);
  const int bodyLine = ui.target().lineHeight(ui.theme().bodyText.font);
  const bool pairing = state_ == State::Pairing;
  const auto l = ybxaccountui::layout(
      ybxaccountui::rect(safe.x, safe.y, safe.width, safe.height),
      smallLine, bodyLine, pairing);
  const auto ink = fui::Paint::solid(fui::Color::Black);
  const auto paper = fui::Paint::solid(fui::Color::White);
  const auto grey = fui::Paint::solid(fui::Color::LightGray);
  // Keep the real system clock/battery band unobstructed. All screen content
  // below it uses the same e-ink paper tone as the approved YBX Reader art.
  const int statusBand = UITheme::getInstance().getMetrics().batteryBarHeight +
                         UITheme::getInstance().getMetrics().topPadding;
  const auto background = ybxaccountui::rect(
      safe.x, safe.y + statusBand, safe.width,
      std::max(0, safe.height - statusBand));
  if (!background.empty()) ui.target().fill(background, grey);

  ui.target().fill(l.brandMark, ink, 5);
  for (int i = 0; i < 3; ++i)
    ui.target().fill(ybxaccountui::rect(l.brandMark.x + 6, l.brandMark.y + 6 + i * 6,
                                       i == 2 ? 9 : 14, 2), paper);
  auto brand = ui.theme().titleText;
  brand.bold = true;
  brand.maxLines = 1;
  ui.target().text(l.eyebrow, tr(STR_YBX), brand);
  ui.target().line({l.headerLine.x, l.headerLine.y},
                   {l.headerLine.right(), l.headerLine.y}, 2, ink);

  auto heading = ui.theme().titleText;
  heading.bold = true;
  heading.maxLines = 1;
  ui.target().text(l.title, pairing ? tr(STR_YBX_ACCOUNT_PAIR_TITLE)
                                  : tr(STR_YBX_ACCOUNT_SOURCES), heading);
  auto intro = ui.theme().bodyText;
  intro.maxLines = l.compact ? 1 : 2;
  ui.target().text(l.intro, pairing ? tr(STR_YBX_ACCOUNT_PAIR_OPEN)
                                  : tr(STR_YBX_ACCOUNT_INTRO), intro);

  pairingQrInline_ = pairing && l.qrInline;
  pairingQrBounds_ = l.inlineQr;
  if (pairing) {
    if (l.qrInline) {
      ui.target().fill(l.inlineQr, paper);
      ui.target().stroke(l.inlineQr, ink, selection_ == 2 ? 3 : 2);
      ui.frame().hit(l.inlineQr, 1, 2, fui::InputTouch);
    }
    auto caption = ui.theme().smallText;
    caption.align = l.compact ? fui::TextAlign::Left : fui::TextAlign::Center;
    caption.maxLines = 1;
    ui.target().text(l.codeCaption, tr(STR_YBX_ACCOUNT_PAIR_CODE), caption);
    auto code = ui.theme().titleText;
    code.bold = true;
    code.align = l.compact ? fui::TextAlign::Left : fui::TextAlign::Center;
    code.maxLines = 1;
    ui.target().text(l.code, pairing_.userCode.c_str(), code);
    auto description = ui.theme().smallText;
    description.align = l.compact ? fui::TextAlign::Left : fui::TextAlign::Center;
    description.maxLines = 3;
    ui.target().text(l.pairHelp, tr(STR_YBX_ACCOUNT_PAIR_HINT), description);
    const unsigned long remaining =
        ybxaccount::deadlineReached(millis(), pairing_.deadlineMs)
        ? 0UL : pairing_.deadlineMs - millis();
    const unsigned long minutes = (remaining + 59999UL) / 60000UL;
    snprintf(pairTimeText_.data(), pairTimeText_.size(),
             tr(STR_YBX_ACCOUNT_WAIT_MINUTES), minutes);
    auto wait = ui.theme().smallText;
    wait.align = fui::TextAlign::Center;
    wait.maxLines = 2;
    if (l.pairWait.height >= smallLine)
      ui.target().text(l.pairWait, pairTimeText_.data(), wait);
    button(ui, l.actions[0], 0, tr(STR_CANCEL));
    button(ui, l.actions[1], 1, tr(STR_YBX_ACCOUNT_RENEW_CODE));
    if (l.actionCount == 3)
      button(ui, l.actions[2], 2, tr(STR_YBX_ACCOUNT_SHOW_QR));
    return;
  }

  const char* names[] = {tr(STR_YBX_ACCOUNT_YANDEX),
                         tr(STR_YBX_ACCOUNT_WB), tr(STR_YBX_ACCOUNT_LITRES)};
  const char* badges[] = {"Я", "WB", "Л"};
  const auto listed = static_cast<int>(sources_.connectedCount());
  for (int i = 0; i < 3; ++i) {
    if (l.sources[i].empty()) continue;
    const bool connected = fetched_ && sources_.connected(static_cast<ybxaccount::SourceKind>(i));
    ui.target().stroke(l.sources[i], ink, 2);
    const auto markPaint = connected ? ink : grey;
    ui.target().fill(l.sourceMarks[i], markPaint);
    ui.target().stroke(l.sourceMarks[i], ink, 1);
    auto badgeText = ui.theme().bodyText;
    badgeText.bold = true;
    badgeText.align = fui::TextAlign::Center;
    badgeText.maxLines = 1;
    if (connected) badgeText.color = fui::Color::White;
    ui.target().text(l.sourceMarks[i], badges[i], badgeText);
    auto name = ui.theme().bodyText;
    name.bold = true;
    name.maxLines = 1;
    ui.target().text(l.sourceTitles[i], names[i], name);
    auto stateText = ui.theme().smallText;
    stateText.maxLines = 1;
    ui.target().text(l.sourceDetails[i],
                     !fetched_ ? tr(STR_YBX_ACCOUNT_NOT_CHECKED)
                     : connected ? tr(STR_YBX_ACCOUNT_LISTED)
                                 : tr(STR_YBX_ACCOUNT_NOT_LISTED), stateText);
    // The source strip resembles 06-sources.png, but the only real actions
    // supported by the device are refresh-all and opening the web cabinet.
    if (l.sourceBands) {
      ui.target().line({l.sourceActionLine[i].x, l.sourceActionLine[i].y},
                       {l.sourceActionLine[i].right(), l.sourceActionLine[i].y}, 1, ink);
      auto action = ui.theme().smallText;
      action.maxLines = 1;
      ui.target().text(l.sourceActionLeft[i].inset({10, 9, 4, 5}),
                       tr(STR_YBX_ACCOUNT_REFRESH_ALL), action);
      ui.target().text(l.sourceActionRight[i].inset({10, 9, 4, 5}),
                       tr(STR_YBX_ACCOUNT_MANAGE), action);
      ui.frame().hit(l.sourceActionLeft[i], 1, 1, fui::InputTouch);
      ui.frame().hit(l.sourceActionRight[i], 1, 3, fui::InputTouch);
    } else {
      ui.frame().hit(l.sources[i], 1, 3, fui::InputTouch);
    }
  }

  const char* status = tr(STR_YBX_ACCOUNT_UNAVAILABLE);
  if (state_ == State::Ready)
    status = fetched_ ? (listed ? tr(STR_YBX_ACCOUNT_UPDATED) : tr(STR_YBX_ACCOUNT_NO_SOURCES))
                      : tr(STR_YBX_ACCOUNT_REFRESH);
  if (state_ == State::Loading) status = tr(STR_LOADING_POPUP);
  if (state_ == State::NotConnected) status = tr(STR_YBX_SYNC_NOT_PAIRED);
  if (state_ == State::InvalidToken) status = tr(STR_YBX_ACCOUNT_TOKEN_INVALID);
  if (state_ == State::DeviceRemoved) status = tr(STR_YBX_ACCOUNT_DEVICE_REMOVED);
  if (state_ == State::ProviderSession) status = tr(STR_YBX_ACCOUNT_PROVIDER_REPAIR);
  if (state_ == State::Unsupported) status = tr(STR_YBX_PLUGIN_UPDATE);
  if (state_ == State::PairFailed)
    status = pairExpired_ ? tr(STR_YBX_ACCOUNT_EXPIRED) : tr(STR_PLUGIN_AUTH_FAILED);
  auto note = ui.theme().smallText;
  note.maxLines = l.compact ? 1 : 2;
  ui.target().text(l.status, status, note);

  const bool needsPair = state_ == State::NotConnected || state_ == State::InvalidToken ||
                         state_ == State::DeviceRemoved || state_ == State::PairFailed;
  const char* primary = state_ == State::ProviderSession ? tr(STR_YBX_ACCOUNT_MANAGE) :
                        needsPair ? (state_ == State::PairFailed ? tr(STR_RETRY) : tr(STR_PLUGIN_SIGN_IN))
                                  : tr(STR_UPDATE);
  button(ui, l.actions[0], 0, tr(STR_BACK));
  button(ui, l.actions[1], 1, primary);
  button(ui, l.actions[2], 2, tr(STR_YBX_ACCOUNT_OPTIONS));
}
void YbxAccountActivity::render(RenderLock&&) {
  if (managePopup_.processRender(renderer, mappedInput)) return;
  renderer.clearScreen();
  auto header = UITheme::getInstance().getHeaderRect(renderer, !uiTarget.deviceContext().hasTouch);
  header.height = UITheme::getInstance().getMetrics().batteryBarHeight;
  GUI.drawHeader(renderer, header, nullptr, nullptr, false);
  renderUi();
  if (state_ == State::Pairing && pairingQrInline_ && !pairing_.verifyUrl.empty()) {
    // Guarantee at least four quiet modules at the common version-4
    // QR density; do not let the light-gray panel touch black modules.
    const auto bounds = pairingQrBounds_.inset({24, 24, 24, 24});
    QrUtils::drawQrCode(renderer, Rect{bounds.x, bounds.y, bounds.width, bounds.height},
                        pairing_.verifyUrl);
  }
  if (!uiTarget.deviceContext().hasTouch) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_YBX_ACCOUNT_UP),
                                              tr(STR_YBX_ACCOUNT_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}