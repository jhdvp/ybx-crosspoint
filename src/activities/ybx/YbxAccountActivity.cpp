#include "YbxAccountActivity.h"

#include <ArduinoJson.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <cstdio>
#include <algorithm>
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
#include "util/YbxNativeAuthPolicy.h"
#include "util/YbxNativeSession.h"
#include "util/YbxKosync.h"

namespace fui = freeink::ui;
namespace {
constexpr size_t kMaxListsBytes = 8192;
constexpr size_t kMaxAuthBytes = 4096;
constexpr uint32_t kRequestTimeoutMs = 12000;

struct Connection {
  std::string token;
  pluginhttp::Headers headers;
};

enum class ConfigResult { Ready, Missing };
ConfigResult loadConnection(Connection& out) {
  if (!ybxnative::loadToken(out.token)) return ConfigResult::Missing;
  out.headers.emplace_back("Authorization", "Bearer " + out.token);
  return ConfigResult::Ready;
}
}  // namespace

void YbxAccountActivity::onEnter() {
  Activity::onEnter();
  sources_.clear();
  fetched_ = false;
  Connection info;
  const ConfigResult ready = loadConnection(info);
  state_ = ready == ConfigResult::Ready ? State::Ready : State::NotConnected;
  authUserCode_.clear();
  authVerifyUrl_.clear();
  authDeviceCode_.clear();
  authQrRect_ = {};
  resetUi();
  app.on(1, &YbxAccountActivity::action, this);
  app.setScreen(&YbxAccountActivity::screen, this);
  refreshPending_ = ready == ConfigResult::Ready && WiFi.status() == WL_CONNECTED;
  pairPending_ = ready == ConfigResult::Missing && WiFi.status() == WL_CONNECTED;
  requestUpdate();
}
void YbxAccountActivity::onExit() {
  closeRouting();
  authDeviceCode_.clear();
  authVerifyUrl_.clear();
  authUserCode_.clear();
  if (ownsWifi_) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
  Activity::onExit();
}
void YbxAccountActivity::screen(UiScreen& ui, void* user) { static_cast<YbxAccountActivity*>(user)->draw(ui); }
void YbxAccountActivity::action(const fui::ActionEvent& event, void* user) {
  auto& self = *static_cast<YbxAccountActivity*>(user);
  self.pending_ = event.value;
  self.app.clearTapFlash();
}
void YbxAccountActivity::refresh() {
  Connection info;
  const auto ready = loadConnection(info);
  if (ready != ConfigResult::Ready) {
    state_ = State::NotConnected;
    fetched_ = false;
    sources_.clear();
    requestUpdate();
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    ownsWifi_ = true;
    auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
    if (!wifi) {
      state_ = State::Failed;
      requestUpdate();
      return;
    }
    startActivityForResult(std::move(wifi), [this](const ActivityResult& result) {
      returnedFromChild_ = true;
      refreshPending_ = !result.isCancelled;
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
  bool cancelled = false;
  uint32_t polledAt = millis() - 50;
  const int code = pluginhttp::request(
      nullptr, ybxnative::kListsUrl, "GET", "", info.headers, response, kMaxListsBytes, nullptr,
      [&] {
        const uint32_t now = millis();
        if (now - polledAt >= 50) {
          polledAt = now;
          mappedInput.update(true);
          cancelled =
              cancelled || mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasHomeGesture();
        }
        return cancelled || ybxaccount::deadlineReached(now, deadline);
      },
      kRequestTimeoutMs);
  if (cancelled) {
    activityManager.goHome();
    return;
  }
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
    } else if (next == State::InvalidToken) {
      sources_.clear();
      fetched_ = false;
    }
  }
  requestUpdate();
}
void YbxAccountActivity::beginPairing() {
  authUserCode_.clear();
  authVerifyUrl_.clear();
  authDeviceCode_.clear();
  if (WiFi.status() != WL_CONNECTED) {
    ownsWifi_ = true;
    auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
    if (!wifi) {
      state_ = State::PairingFailed;
      requestUpdate();
      return;
    }
    startActivityForResult(std::move(wifi), [this](const ActivityResult& result) {
      returnedFromChild_ = true;
      pairPending_ = !result.isCancelled;
    });
    return;
  }
  {
    RenderLock lock(*this);
    state_ = State::PairingStarting;
  }
  requestUpdateAndWait();
  String response;
  const pluginhttp::Headers headers{{"Content-Type", "application/json"}};
  const int status = pluginhttp::request(
      nullptr, ybxnative::kDeviceUrl, "POST", "{\"client_id\":\"crosspoint\"}",
      headers, response, kMaxAuthBytes, nullptr, nullptr, kRequestTimeoutMs);
  JsonDocument doc;
  const bool good = status >= 200 && status < 300 &&
                    !deserializeJson(doc, response);
  if (!good) {
    // Log status only. Response body, verification URL and device code are
    // short-lived credentials and must never be printed.
    LOG_ERR("YBX", "Native device-code request failed: status=%d", status);
    state_ = State::PairingFailed;
    requestUpdate();
    return;
  }
  authUserCode_ = doc["user_code"] | "";
  authDeviceCode_ = doc["device_code"] | "";
  authVerifyUrl_ = doc["verification_uri_complete"] | "";
  const long interval = doc["interval"] | 5L;
  const long expires = doc["expires_in"] | 900L;
  if (authUserCode_.empty() || authUserCode_.size() > 64 ||
      authDeviceCode_.empty() || authDeviceCode_.size() > 1024 ||
      !ybxnative::trustedVerifyUrl(authVerifyUrl_)) {
    LOG_ERR("YBX", "Native device-code response rejected: invalid fields");
    authDeviceCode_.clear();
    authVerifyUrl_.clear();
    state_ = State::PairingFailed;
    requestUpdate();
    return;
  }
  authPollIntervalMs_ = ybxnative::pollSeconds(interval) * 1000UL;
  authNextPollMs_ = millis() + authPollIntervalMs_;
  authDeadlineMs_ = millis() + ybxnative::expiresSeconds(expires) * 1000UL;
  state_ = State::Pairing;
  requestUpdate();
}

void YbxAccountActivity::pollPairing() {
  if (ybxaccount::deadlineReached(millis(), authDeadlineMs_)) {
    authDeviceCode_.clear();
    state_ = State::PairingFailed;
    requestUpdate();
    return;
  }
  authNextPollMs_ = millis() + authPollIntervalMs_;
  JsonDocument payload;
  payload["grant_type"] = "urn:ietf:params:oauth:grant-type:device_code";
  payload["device_code"] = authDeviceCode_;
  std::string body;
  serializeJson(payload, body);
  const pluginhttp::Headers headers{{"Content-Type", "application/json"}};
  String response;
  const int status = pluginhttp::request(
      nullptr, ybxnative::kTokenUrl, "POST", body, headers,
      response, kMaxAuthBytes, nullptr, nullptr, kRequestTimeoutMs);
  if (status < 0) return;  // offline; retry until expiry, never downgrade TLS
  JsonDocument doc;
  if (deserializeJson(doc, response)) {
    if (status >= 500) return;
    state_ = State::PairingFailed;
    requestUpdate();
    return;
  }
  if (status >= 200 && status < 300) {
    const std::string accessToken = doc["access_token"] | "";
    if (accessToken.empty() || !ybxnative::saveToken(accessToken)) {
      state_ = State::PairingFailed;
      requestUpdate();
      return;
    }
    authDeviceCode_.clear();
    authUserCode_.clear();
    authVerifyUrl_.clear();
    state_ = State::Ready;
    ybxkosync::refresh();
    refreshPending_ = true;
    requestUpdate();
    return;
  }
  const std::string error = doc["error"] | "";
  if (error == "slow_down") {
    authPollIntervalMs_ = std::min<uint32_t>(60000, authPollIntervalMs_ + 5000);
  } else if (!ybxnative::pendingGrantError(error) &&
             (ybxnative::terminalGrantError(error) || status < 500)) {
    authDeviceCode_.clear();
    state_ = State::PairingFailed;
    requestUpdate();
  }
}

void YbxAccountActivity::loop() {
  if (std::exchange(returnedFromChild_, false)) return;
  if (pairPending_) {
    pairPending_ = false;
    beginPairing();
    return;
  }
  if (state_ == State::Pairing && ybxaccount::deadlineReached(millis(), authNextPollMs_)) {
    pollPairing();
    return;
  }
  if (refreshPending_) {
    refreshPending_ = false;
    refresh();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goHome();
    return;
  }
  const auto touch = routeTouch(mappedInput);
  if (touch.routed && app.invalidated()) requestUpdate();
  navigator_.onNext([this] {
    selection_ = (selection_ + 1) % 3;
    requestUpdate();
  });
  navigator_.onPrevious([this] {
    selection_ = (selection_ + 2) % 3;
    requestUpdate();
  });
  const int chosen = std::exchange(pending_, -1);
  if (chosen >= 0)
    activate(chosen);
  else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm))
    activate(selection_);
}
void YbxAccountActivity::activate(int value) {
  if (state_ == State::Loading) return;
  if (value == 0) activityManager.goHome();
  if (value == 1) {
    if (state_ == State::NotConnected || state_ == State::InvalidToken ||
        state_ == State::Unsupported || state_ == State::PairingFailed ||
        state_ == State::Pairing) {
      beginPairing();
    } else {
      refresh();
    }
  }
  if (value == 2) openPluginCatalog();
}
void YbxAccountActivity::openPluginCatalog() { activityManager.goToYbxCatalog(); }
void YbxAccountActivity::button(UiScreen& ui, fui::Rect bounds, int value, const char* label) {
  props_.label = label;
  props_.action = 1;
  props_.value = value;
  props_.inputMask = state_ == State::Loading ? 0 : fui::InputTouch;
  props_.minTouchSize = 0;
  props_.text = ui.theme().smallText;
  props_.text.maxLines = 2;
  props_.state = state_ == State::Loading ? fui::StateDisabled
                 : selection_ == value    ? fui::StateSelected
                                          : fui::StateNormal;
  fui::button(ui.frame(), bounds, props_);
}
void YbxAccountActivity::draw(UiScreen& ui) {
  const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, false);
  const int pad = UITheme::getInstance().getMetrics().contentSidePadding;
  const int x = safe.x + pad, width = safe.width - pad * 2;
  auto heading = ui.theme().bodyText;
  heading.bold = true;
  ui.target().text({static_cast<int16_t>(x), static_cast<int16_t>(safe.y + 8), static_cast<int16_t>(width), 42},
                   tr(STR_OPDS_MY_ACCOUNT), heading);
  authQrRect_ = {};
  if (state_ == State::Pairing) {
    auto codeStyle = ui.theme().titleText;
    codeStyle.align = fui::TextAlign::Center;
    codeStyle.bold = true;
    auto noteStyle = ui.theme().smallText;
    noteStyle.align = fui::TextAlign::Center;
    noteStyle.maxLines = 2;
    ui.target().text({static_cast<int16_t>(x), static_cast<int16_t>(safe.y + 65),
                      static_cast<int16_t>(width), 48},
                     tr(STR_PLUGIN_AUTH_WAITING), noteStyle);
    ui.target().text({static_cast<int16_t>(x), static_cast<int16_t>(safe.y + 112),
                      static_cast<int16_t>(width), 48},
                     authUserCode_.c_str(), codeStyle);
    const int qrSize = std::min(220, width - 32);
    authQrRect_ = fui::Rect{static_cast<int16_t>(safe.x + (safe.width - qrSize) / 2),
                           static_cast<int16_t>(safe.y + 185),
                           static_cast<int16_t>(qrSize), static_cast<int16_t>(qrSize)};
    ui.target().text({static_cast<int16_t>(x), static_cast<int16_t>(safe.y + 430),
                      static_cast<int16_t>(width), 58},
                     authVerifyUrl_.c_str(), noteStyle);
  } else {
    const char* names[] = {tr(STR_YBX_ACCOUNT_YANDEX), tr(STR_YBX_ACCOUNT_WB), tr(STR_YBX_ACCOUNT_LITRES)};
    auto text = ui.theme().smallText;
    text.maxLines = 2;
    for (size_t i = 0; i < 3; i++) {
      const char* value = !fetched_                                                    ? "—"
                          : sources_.connected(static_cast<ybxaccount::SourceKind>(i)) ? tr(STR_CONNECTED)
                                                                                       : tr(STR_YBX_ACCOUNT_NOT_LISTED);
      snprintf(rows_[i].data(), rows_[i].size(), "%s: %s", names[i], value);
      ui.target().text(
          {static_cast<int16_t>(x), static_cast<int16_t>(safe.y + 65 + i * 48), static_cast<int16_t>(width), 40},
          rows_[i].data(), text);
    }
    const char* status = tr(STR_YBX_ACCOUNT_UNAVAILABLE);
    if (state_ == State::Ready) status = fetched_ ? tr(STR_YBX_ACCOUNT_UPDATED) : tr(STR_YBX_ACCOUNT_REFRESH);
    if (state_ == State::Loading || state_ == State::PairingStarting) status = tr(STR_LOADING_POPUP);
    if (state_ == State::NotConnected || state_ == State::InvalidToken) status = tr(STR_YBX_SYNC_NOT_PAIRED);
    if (state_ == State::Unsupported) status = tr(STR_YBX_PLUGIN_UPDATE);
    if (state_ == State::PairingFailed) status = tr(STR_PLUGIN_AUTH_FAILED);
    ui.target().text({static_cast<int16_t>(x), static_cast<int16_t>(safe.y + 225),
                      static_cast<int16_t>(width), 45},
                     status, text);
  }
  const int spacing = 8, cell = (width - spacing * 2) / 3;
  const int y = safe.y + safe.height - 64;
  button(ui, {static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(cell), 48}, 0, tr(STR_BACK));
  button(ui, {static_cast<int16_t>(x + cell + spacing), static_cast<int16_t>(y), static_cast<int16_t>(cell), 48}, 1,
         state_ == State::Ready || state_ == State::Loading ? tr(STR_UPDATE) : tr(STR_PLUGIN_SIGN_IN));
  button(ui, {static_cast<int16_t>(x + (cell + spacing) * 2), static_cast<int16_t>(y), static_cast<int16_t>(cell), 48},
         2, tr(STR_YBX_HOME_CATALOGS));
}
void YbxAccountActivity::render(RenderLock&&) {
  renderer.clearScreen();
  renderUi();
  if (state_ == State::Pairing && authQrRect_.width > 0) {
    QrUtils::drawQrCode(renderer, Rect{authQrRect_.x, authQrRect_.y, authQrRect_.width, authQrRect_.height},
                        authVerifyUrl_);
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}