#include "YbxAccountActivity.h"

#include <ArduinoJson.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <WiFi.h>

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

namespace fui = freeink::ui;
namespace {
constexpr size_t kMaxManifestBytes = 8192;
constexpr size_t kMaxListsBytes = 8192;
constexpr uint32_t kRequestTimeoutMs = 12000;
// A plugin manifest must not be allowed to send the device bearer token to
// an arbitrary host merely because it declares a custom browse URL.
constexpr const char* kListsUrl = "https://my.ybx.app/api/v1/crosspoint/catalog/lists";

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
void YbxAccountActivity::loop() {
  if (std::exchange(returnedFromChild_, false)) return;
  const bool allowBack = !waitBackRelease_;
  const bool allowConfirm = !waitConfirmRelease_;
  if (!mappedInput.isPressed(MappedInputManager::Button::Back)) waitBackRelease_ = false;
  if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) waitConfirmRelease_ = false;
  if (refreshPending_) {
    refreshPending_ = false;
    refresh();
    return;
  }
  if (allowBack && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goHome();
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
  if (value == 0) activityManager.goHome();
  if (value == 1) refresh();
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
  ui.target().text({static_cast<int16_t>(x), static_cast<int16_t>(safe.y + 225),
                    static_cast<int16_t>(width), 45}, status, text);
  const int spacing = 8, cell = (width - spacing * 2) / 3;
  const int y = safe.y + safe.height - 64;
  button(ui, {static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(cell), 48}, 0, tr(STR_BACK));
  button(ui, {static_cast<int16_t>(x + cell + spacing), static_cast<int16_t>(y),
              static_cast<int16_t>(cell), 48}, 1, tr(STR_UPDATE));
  button(ui, {static_cast<int16_t>(x + (cell + spacing) * 2), static_cast<int16_t>(y),
              static_cast<int16_t>(cell), 48}, 2, tr(STR_YBX_HOME_CATALOGS));
}
void YbxAccountActivity::render(RenderLock&&) {
  renderer.clearScreen();
  renderUi();
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}