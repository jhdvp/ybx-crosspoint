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
#include "util/QrUtils.h"
#include "network/TlsTransport.h"
#include "activities/reader/QrDisplayActivity.h"

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
  const int code = pluginhttp::request(
      nullptr, kListsUrl, "GET", "", info.headers, response, kMaxListsBytes,
      nullptr, [&] { return ybxaccount::deadlineReached(millis(), deadline); }, kRequestTimeoutMs);
  State next = State::Failed;
  ybxaccount::Summary incoming;
  if (code == 401 || code == 403) {
    next = State::InvalidToken;
  } else if (code >= 200 && code < 300) {
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
    state_ = State::NotConnected;
}

void YbxAccountActivity::beginPairing() {
  // A valid existing account must not be silently replaced. The optional
  // account-switch/revoke workflow needs its own explicit confirmation.
  if (state_ == State::Ready || state_ == State::Loading || state_ == State::Pairing) return;
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
                             waitConfirmRelease_ = mappedInput.isPressed(MappedInputManager::Button::Confirm);
                             waitBackRelease_ = mappedInput.isPressed(MappedInputManager::Button::Back);
                           });
    return;
  }
  cancelPairing();
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
        const std::string pending = dir + "/events.jsonl";
        // Never replay a prior account's queued reading events under a new
        // token. Fail closed if the stale outbox cannot be cleared.
        const bool clean = !Storage.exists(pending.c_str()) || Storage.remove(pending.c_str());
        if (!clean || !pluginhttp::saveTokenToFile(file, kTokenPath, token))
          state_ = State::PairFailed;
        else {
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
  if (allowBack && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (state_ == State::Pairing) {
      cancelPairing();
      requestUpdate();
    } else {
      activityManager.goHome();
    }
    return;
  }
  const auto touch = routeTouch(mappedInput);
  if (touch.routed && app.invalidated()) requestUpdate();
  navigator_.onNext([this] {
    selection_ = (selection_ + 1) % (state_ == State::Pairing ? 2 : 3);
    requestUpdate();
  });
  navigator_.onPrevious([this] {
    selection_ = (selection_ + (state_ == State::Pairing ? 1 : 2)) % (state_ == State::Pairing ? 2 : 3);
    requestUpdate();
  });
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
      showPairQr();
    }
    return;
  }
  if (value == 0) activityManager.goHome();
  if (value == 1) {
    if (state_ == State::NotConnected || state_ == State::InvalidToken || state_ == State::PairFailed)
      beginPairing();
    else
      refresh();
  }
  if (value == 2) openPluginCatalog();
}
void YbxAccountActivity::openPluginCatalog() { activityManager.goToPlugins(false); }
void YbxAccountActivity::button(UiScreen& ui, fui::Rect bounds, int value, const char* label) {
  props_.label = label;
  props_.action = 1;
  props_.value = value;
  props_.inputMask = state_ == State::Loading ? 0 : fui::InputTouch;
  props_.minTouchSize = 0;
  props_.text = ui.theme().smallText;
  props_.text.maxLines = 2;
  props_.state = state_ == State::Loading ? fui::StateDisabled
                 : selection_ == value ? fui::StateSelected : fui::StateNormal;
  fui::button(ui.frame(), bounds, props_);
}
void YbxAccountActivity::draw(UiScreen& ui) {
  const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, false);
  const int pad = UITheme::getInstance().getMetrics().contentSidePadding;
  const int x = safe.x + pad, width = safe.width - pad * 2;
  auto heading = ui.theme().bodyText;
  heading.bold = true;
  ui.target().text({static_cast<int16_t>(x), static_cast<int16_t>(safe.y + 8),
                    static_cast<int16_t>(width), 42}, tr(STR_OPDS_MY_ACCOUNT), heading);
  if (state_ == State::Pairing) {
    auto codeStyle = ui.theme().titleText;
    codeStyle.bold = true;
    codeStyle.align = fui::TextAlign::Center;
    const bool compact = safe.height < 180;
    ui.target().text({static_cast<int16_t>(x), static_cast<int16_t>(safe.y + (compact ? 26 : 43)),
                      static_cast<int16_t>(width), static_cast<int16_t>(compact ? 26 : 35)},
                     pairing_.userCode.c_str(), codeStyle);
    auto hint = ui.theme().smallText;
    hint.maxLines = 2;
    if (!compact) {
      ui.target().text({static_cast<int16_t>(x), static_cast<int16_t>(safe.y + 81),
                        static_cast<int16_t>(width), 35}, tr(STR_YBX_ACCOUNT_PAIR_HINT), hint);
    }
    if (safe.height >= 230) {
      ui.target().text({static_cast<int16_t>(x), static_cast<int16_t>(safe.y + safe.height - 105),
                        static_cast<int16_t>(width), 30}, tr(STR_PLUGIN_AUTH_WAITING), hint);
    }
    const int y = safe.y + safe.height - 55;
    const int spacing = 8, cell = (width - spacing) / 2;
    button(ui, {static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(cell), 45},
           0, tr(STR_CANCEL));
    button(ui, {static_cast<int16_t>(x + cell + spacing), static_cast<int16_t>(y),
                static_cast<int16_t>(cell), 45}, 1, tr(STR_YBX_ACCOUNT_SHOW_QR));
    return;
  }
  const char* names[] = {tr(STR_YBX_ACCOUNT_YANDEX), tr(STR_YBX_ACCOUNT_WB), tr(STR_YBX_ACCOUNT_LITRES)};
  auto text = ui.theme().smallText;
  text.maxLines = 2;
  for (size_t i = 0; i < 3; i++) {
    const char* value = !fetched_ ? "—"
        : sources_.connected(static_cast<ybxaccount::SourceKind>(i))
            ? tr(STR_CONNECTED) : tr(STR_YBX_SYNC_NOT_PAIRED);
    snprintf(rows_[i].data(), rows_[i].size(), "%s: %s", names[i], value);
    ui.target().text({static_cast<int16_t>(x), static_cast<int16_t>(safe.y + 65 + i * 48),
                      static_cast<int16_t>(width), 40}, rows_[i].data(), text);
  }
  const char* status = tr(STR_YBX_ACCOUNT_UNAVAILABLE);
  if (state_ == State::Ready) status = fetched_ ? tr(STR_YBX_ACCOUNT_UPDATED) : tr(STR_YBX_ACCOUNT_REFRESH);
  if (state_ == State::Loading) status = tr(STR_LOADING_POPUP);
  if (state_ == State::NotConnected || state_ == State::InvalidToken) status = tr(STR_YBX_SYNC_NOT_PAIRED);
  if (state_ == State::Unsupported) status = tr(STR_YBX_PLUGIN_UPDATE);
  if (state_ == State::PairFailed)
    status = pairExpired_ ? tr(STR_YBX_ACCOUNT_EXPIRED) : tr(STR_PLUGIN_AUTH_FAILED);
  ui.target().text({static_cast<int16_t>(x), static_cast<int16_t>(safe.y + 225),
                    static_cast<int16_t>(width), 45}, status, text);
  const int spacing = 8, cell = (width - spacing * 2) / 3;
  const int y = safe.y + safe.height - 64;
  button(ui, {static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(cell), 48}, 0, tr(STR_BACK));
  const bool needsPair = state_ == State::NotConnected || state_ == State::InvalidToken ||
                         state_ == State::PairFailed;
  button(ui, {static_cast<int16_t>(x + cell + spacing), static_cast<int16_t>(y),
              static_cast<int16_t>(cell), 48}, 1,
         needsPair ? (state_ == State::PairFailed ? tr(STR_RETRY) : tr(STR_PLUGIN_SIGN_IN)) : tr(STR_UPDATE));
  button(ui, {static_cast<int16_t>(x + (cell + spacing) * 2), static_cast<int16_t>(y),
              static_cast<int16_t>(cell), 48}, 2, tr(STR_YBX_HOME_CATALOGS));
}
void YbxAccountActivity::render(RenderLock&&) {
  renderer.clearScreen();
  renderUi();
  if (state_ == State::Pairing && !pairing_.verifyUrl.empty()) {
    const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, false);
    if (safe.height >= 265) {
      // Leave a gap before the waiting text and bottom touch controls.
      const int size = std::min(110, std::min(safe.width - 28, safe.height - 226));
      if (size >= 60) {
        const Rect bounds{static_cast<int16_t>(safe.x + (safe.width - size) / 2),
                          static_cast<int16_t>(safe.y + 119),
                          static_cast<int16_t>(size), static_cast<int16_t>(size)};
        QrUtils::drawQrCode(renderer, bounds, pairing_.verifyUrl);
      }
    }
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}