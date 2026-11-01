#include "YbxAccountActivity.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <SecureHttpClient.h>
#include <strings.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "MappedInputManager.h"
#include "RenderLock.h"
#include "components/CatalogScreens.h"
#include "components/UITheme.h"
#include "util/PluginEvents.h"
#include "util/PluginHttp.h"
#include "util/PluginLocations.h"
#include "util/QrUtils.h"

namespace fui = freeink::ui;

using pluginhttp::resolvePath;
using pluginhttp::substituteAll;
using pluginhttp::variantToString;

YbxAccountActivity::YbxAccountActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : CatalogActivity("YbxAccount", renderer, mappedInput) {}

YbxAccountActivity::~YbxAccountActivity() = default;

void YbxAccountActivity::onEnter() {
  CatalogActivity::onEnter();
  loadAccountFiles();
  view_ = View::Account;
  sourceKind_ = ybxaccount::SourceKind::Yandex;
  outcome_ = ybxaccount::ListsOutcome::Failed;
  pendingPairing_ = false;
  qrRect_ = {};
  state = State::BROWSING;
  rebuildRows();
  // A stored token gets a live status on entry; a transport failure renders
  // as "Offline" in the status row instead of a Wi-Fi setup detour.
  if (manifestValid_ && !token_.empty())
    startBrowse();
  else
    requestUpdate();
}

void YbxAccountActivity::onExit() {
  session_.reset();
  CatalogActivity::onExit();
}

void YbxAccountActivity::loadAccountFiles() {
  pluginDir_ = PluginLocations::findPluginDir("ybx");
  manifest_ = Manifest{};
  manifestValid_ = !pluginDir_.empty() && loadManifest();
  token_.clear();
  config_.clear();
  cabinetUrl_[0] = '\0';
  if (manifestValid_) {
    // A missing token file is the normal "not signed in yet" state; a file
    // that fails to read is treated the same way (the next sign-in rewrites it).
    pluginhttp::loadTokenFromFile(manifest_.tokenFile, manifest_.tokenPath, token_);
    pluginhttp::loadConfigFile(manifest_.configFile, config_);
    ybxaccount::cabinetUrlFromListsUrl(manifest_.listsUrl, cabinetUrl_, sizeof(cabinetUrl_));
  }
  summary_.clear();
  outcome_ = ybxaccount::ListsOutcome::Failed;
  detailText_[0] = '\0';
}

bool YbxAccountActivity::loadManifest() {
  std::string raw;
  const std::string path = pluginDir_ + "/device.json";
  if (!Storage.readFileToString("YBXACC", path, MAX_MANIFEST_SIZE, raw)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, raw) != DeserializationError::Ok) return false;

  manifest_.tokenFile = pluginhttp::inPluginDir(pluginDir_, doc["token"]["file"] | "");
  manifest_.tokenPath = doc["token"]["path"] | "access_token";
  manifest_.configFile = pluginhttp::inPluginDir(pluginDir_, doc["config"]["file"] | "");
  // Same default as the plugin catalog: a manifest may omit "device_code".
  manifest_.authType = doc["auth"]["type"] | "device_code";
  pluginhttp::readRequest(doc["auth"]["request"], "POST", manifest_.authReq);
  pluginhttp::readRequest(doc["auth"]["poll"], "POST", manifest_.pollReq);
  manifest_.authCodePath = doc["auth"]["code_path"] | "user_code";
  manifest_.authVerifyPath = doc["auth"]["verify_url_path"] | "verification_uri";
  manifest_.authDeviceCodePath = doc["auth"]["device_code_path"] | "device_code";
  manifest_.authIntervalPath = doc["auth"]["interval_path"] | "interval";
  manifest_.authExpiresPath = doc["auth"]["expires_path"] | "expires_in";
  manifest_.authTokenPath = doc["auth"]["token_path"] | "access_token";
  manifest_.authErrorPath = doc["auth"]["error_path"] | "error";
  manifest_.listsUrl = doc["browse"]["lists_url"] | "";
  pluginhttp::readHeaders(doc["browse"]["lists_headers"], manifest_.listsHeaders);
  if (manifest_.listsUrl.empty()) {
    LOG_ERR("YBXACC", "manifest has no browse.lists_url");
    return false;
  }
  return true;
}

std::string YbxAccountActivity::substituted(const std::string& value) const {
  std::string out = value;
  substituteAll(out, "{token}", token_);
  for (const auto& kv : config_) {
    const std::string key = "{cfg." + kv.first + "}";
    substituteAll(out, key.c_str(), kv.second);
  }
  return out;
}

pluginhttp::Headers YbxAccountActivity::substitutedHeaders(const pluginhttp::Headers& headers) const {
  pluginhttp::Headers out;
  out.reserve(headers.size());
  for (const auto& header : headers) out.emplace_back(header.first, substituted(header.second));
  return out;
}

void YbxAccountActivity::ensureSession() {
  if (session_) return;
  session_ = makeUniqueNoThrow<freeink::SecureHttpClient>();
  if (!session_) LOG_INF("YBXACC", "OOM: TLS client; using per-request connections");
}

int YbxAccountActivity::apiRequest(const pluginhttp::RequestSpec& req, String& out,
                                   pluginhttp::Headers* responseHeaders) {
  ensureSession();
  return pluginhttp::request(session_.get(), req.url, req.method, req.body, req.headers, out, MAX_API_RESPONSE,
                             responseHeaders);
}

void YbxAccountActivity::startBrowse() {
  if (pendingPairing_) {
    beginPairing();
    return;
  }
  runRefresh();
}

void YbxAccountActivity::wifiSelectionCancelled() {
  pendingPairing_ = false;
  fail(StrId::STR_WIFI_CONN_FAILED);
}

void YbxAccountActivity::runRefresh() {
  if (!manifestValid_) {
    fail(StrId::STR_PLUGIN_MANIFEST_INVALID);
    return;
  }
  if (token_.empty()) {
    beginPairing();
    return;
  }

  beginLoading();
  String response;
  pluginhttp::Headers responseHeaders;
  const pluginhttp::RequestSpec req{substituted(manifest_.listsUrl), "GET", "",
                                    substitutedHeaders(manifest_.listsHeaders)};
  const int status = apiRequest(req, response, &responseHeaders);

  bool deviceRemoved = false;
  for (const auto& header : responseHeaders) {
    if (strcasecmp(header.first.c_str(), "X-YBX-Device-Removed") == 0 && ybxaccount::headerIsTrue(header.second)) {
      deviceRemoved = true;
      break;
    }
  }
  ybxaccount::ListsOutcome outcome = ybxaccount::listsOutcomeFromStatus(status, deviceRemoved);

  JsonDocument doc;
  const DeserializationError parsed = deserializeJson(doc, response);
  if (outcome == ybxaccount::ListsOutcome::ProviderSession && parsed == DeserializationError::Ok) {
    // Only the consent marker distinguishes a provider-session failure from a
    // generic 409; anything else stays an unexplained failure.
    std::string_view code;
    if (doc["detail"].is<const char*>())
      code = doc["detail"].as<const char*>();
    else
      code = variantToString(resolvePath(doc, "detail.code"));
    if (!ybxaccount::isProviderSessionBody(code)) outcome = ybxaccount::ListsOutcome::Failed;
  }

  summary_.clear();
  if (outcome == ybxaccount::ListsOutcome::Ok && parsed == DeserializationError::Ok) {
    // The lists themselves are the truth about connected sources: a provider
    // that stopped publishing lists is reported disconnected even if an old
    // cache said otherwise.
    for (JsonVariantConst list : doc["lists"].as<JsonArrayConst>()) {
      summary_.addList(ybxaccount::classifyListUrl(std::string_view(list["url"] | "")));
    }
  } else if (outcome == ybxaccount::ListsOutcome::Ok) {
    outcome = ybxaccount::ListsOutcome::Failed;
  }

  outcome_ = outcome;
  state = State::BROWSING;
  pendingPairing_ = false;
  setView(View::Account);
}

void YbxAccountActivity::beginPairing() {
  if (!manifestValid_ || !manifest_.hasDeviceCode()) {
    pendingPairing_ = false;
    fail(StrId::STR_PLUGIN_MANIFEST_INVALID);
    return;
  }

  beginLoading();
  String response;
  const pluginhttp::RequestSpec req{substituted(manifest_.authReq.url), manifest_.authReq.method,
                                    substituted(manifest_.authReq.body), substitutedHeaders(manifest_.authReq.headers)};
  const int status = apiRequest(req, response);
  JsonDocument doc;
  if (status < 200 || status >= 300 || deserializeJson(doc, response) != DeserializationError::Ok) {
    pendingPairing_ = false;
    fail(StrId::STR_PLUGIN_AUTH_FAILED);
    return;
  }

  copyField(variantToString(resolvePath(doc, manifest_.authCodePath)), authCode_, sizeof(authCode_));
  copyField(variantToString(resolvePath(doc, manifest_.authVerifyPath)), authVerify_, sizeof(authVerify_));
  copyField(variantToString(resolvePath(doc, manifest_.authDeviceCodePath)), authDeviceCode_, sizeof(authDeviceCode_));
  if (authCode_[0] == '\0' || authDeviceCode_[0] == '\0') {
    pendingPairing_ = false;
    fail(StrId::STR_PLUGIN_AUTH_FAILED);
    return;
  }

  const long interval = resolvePath(doc, manifest_.authIntervalPath) | 5L;
  const long lifetime = resolvePath(doc, manifest_.authExpiresPath) | 900L;
  authIntervalMs_ = ybxaccount::clampPollIntervalMs(interval);
  authDeadlineMs_ = millis() + ybxaccount::clampCodeLifetimeMs(lifetime);
  authNextPollMs_ = millis() + authIntervalMs_;
  state = State::AUTH;
  qrRect_ = {};
  requestUpdate();
}

void YbxAccountActivity::pollPairing() {
  // Expired first: being offline must not let a code outlive its lifetime and
  // then be accepted on reconnect.
  if (ybxaccount::deadlineReached(millis(), authDeadlineMs_)) {
    pendingPairing_ = false;
    fail(StrId::STR_PLUGIN_AUTH_FAILED);
    return;
  }
  // Schedule the next attempt before the request so a slow or failing poll
  // cannot start a second one back-to-back.
  authNextPollMs_ = millis() + authIntervalMs_;

  pluginhttp::RequestSpec req{substituted(manifest_.pollReq.url), manifest_.pollReq.method,
                              substituted(manifest_.pollReq.body), substitutedHeaders(manifest_.pollReq.headers)};
  const std::string_view deviceCode(authDeviceCode_);
  substituteAll(req.url, "{device_code}", std::string(deviceCode));
  substituteAll(req.body, "{device_code}", std::string(deviceCode));

  String response;
  const int status = apiRequest(req, response);
  if (status < 0) return;  // transport blip: keep polling within the deadline
  JsonDocument doc;
  if (deserializeJson(doc, response) != DeserializationError::Ok) return;

  std::string newToken = variantToString(resolvePath(doc, manifest_.authTokenPath));
  if (!newToken.empty()) {
    if (!pluginhttp::saveTokenToFile(manifest_.tokenFile, manifest_.tokenPath, newToken)) {
      pendingPairing_ = false;
      fail(StrId::STR_PLUGIN_AUTH_FAILED);
      return;
    }
    token_ = std::move(newToken);
    finishPairing();
    return;
  }

  switch (ybxaccount::pollOutcomeFromErrorCode(variantToString(resolvePath(doc, manifest_.authErrorPath)))) {
    case ybxaccount::PollOutcome::SlowDown:
      authIntervalMs_ = ybxaccount::slowedDownIntervalMs(authIntervalMs_);
      break;
    case ybxaccount::PollOutcome::Expired:
    case ybxaccount::PollOutcome::Denied:
      pendingPairing_ = false;
      fail(StrId::STR_PLUGIN_AUTH_FAILED);
      break;
    case ybxaccount::PollOutcome::Pending:
      break;
  }
}

void YbxAccountActivity::finishPairing() {
  pendingPairing_ = false;
  runRefresh();
}

void YbxAccountActivity::performSignOut() {
  if (!manifest_.tokenFile.empty()) Storage.remove(manifest_.tokenFile.c_str());
  token_.clear();
  // Queued plugin events belong to the signed-out key; replaying them after a
  // re-pair would attribute old activity to the new binding.
  if (!pluginDir_.empty()) pluginevents::clearOutbox(pluginDir_);
  clearForeignCaches();
  summary_.clear();
  outcome_ = ybxaccount::ListsOutcome::Failed;
  detailText_[0] = '\0';
  session_.reset();
  goAccount();
}

void YbxAccountActivity::clearForeignCaches() {
  // Only caches derived from the previous account's content. Books and reading
  // positions are the owner's local data and survive sign-out. /sleep.bmp is a
  // plugin-event download target (see util/PluginEvents.h): the cabinet pushed
  // it under the binding being removed.
  Storage.remove("/sleep.bmp");
}

void YbxAccountActivity::setView(View view) {
  view_ = view;
  if (view != View::Cabinet) qrRect_ = {};
  rebuildRows();
}

void YbxAccountActivity::goAccount() { setView(View::Account); }

void YbxAccountActivity::openSource(ybxaccount::SourceKind kind) {
  sourceKind_ = kind;
  std::snprintf(detailText_, sizeof(detailText_), tr(STR_YBX_ACCOUNT_LISTS_COUNT),
                static_cast<unsigned>(summary_.shelfCount(kind)));
  setView(View::Source);
}

void YbxAccountActivity::rebuildRows() {
  std::array<fui::ListItem, MAX_ROWS> items{};
  std::array<RowAction, MAX_ROWS> actions{};
  uint8_t n = 0;
  const auto addRow = [&](const char* label, const char* subtitle, const char* value, RowAction action) {
    if (n >= MAX_ROWS) return;
    items[n].label = label;
    items[n].subtitle = subtitle && subtitle[0] ? subtitle : nullptr;
    items[n].value = value;
    items[n].actionValue = static_cast<int16_t>(n);
    actions[n] = action;
    ++n;
  };

  switch (view_) {
    case View::Account: {
      statusSubtitle(detailText_, sizeof(detailText_));
      addRow(tr(STR_YBX_ACCOUNT_STATUS), detailText_, statusLabel(), RowAction::Refresh);
      if (!manifestValid_) break;
      const bool tokenStale =
          outcome_ == ybxaccount::ListsOutcome::InvalidToken || outcome_ == ybxaccount::ListsOutcome::DeviceRemoved;
      if (token_.empty() || tokenStale) addRow(tr(STR_PLUGIN_SIGN_IN), nullptr, ">", RowAction::SignIn);
      if (!token_.empty()) {
        if (outcome_ != ybxaccount::ListsOutcome::ProviderSession)
          addRow(tr(STR_YBX_ACCOUNT_SOURCES), nullptr, ">", RowAction::Sources);
        addRow(tr(STR_YBX_ACCOUNT_CABINET), nullptr, ">", RowAction::Cabinet);
        addRow(tr(STR_YBX_ACCOUNT_SIGN_OUT), nullptr, nullptr, RowAction::SignOut);
      }
      break;
    }
    case View::Sources: {
      using ybxaccount::SourceKind;
      const SourceKind kinds[3] = {SourceKind::Yandex, SourceKind::Wb, SourceKind::Litres};
      const RowAction actionsByKind[3] = {RowAction::SourceYandex, RowAction::SourceWb, RowAction::SourceLitres};
      for (uint8_t i = 0; i < 3; ++i) {
        const bool connected = summary_.connected(kinds[i]);
        const char* value;
        if (connected) {
          std::snprintf(rowNumber_[i], sizeof(rowNumber_[i]), "%u",
                        static_cast<unsigned>(summary_.shelfCount(kinds[i])));
          value = rowNumber_[i];
        } else {
          value = ">";
        }
        addRow(sourceName(kinds[i]), connected ? tr(STR_YBX_ACCOUNT_CONNECTED) : tr(STR_YBX_ACCOUNT_NOT_CONNECTED),
               value, actionsByKind[i]);
      }
      addRow(tr(STR_YBX_ACCOUNT_CABINET), nullptr, ">", RowAction::Cabinet);
      break;
    }
    case View::Source:
    case View::Cabinet:
      break;  // centered detail screens
    case View::ConfirmSignOut:
      addRow(tr(STR_CONFIRM), tr(STR_YBX_ACCOUNT_SIGN_OUT_CONFIRM), nullptr, RowAction::ConfirmSignOut);
      addRow(tr(STR_CANCEL), nullptr, nullptr, RowAction::CancelSignOut);
      break;
  }

  {
    const RenderLock lock;  // the render task reads these arrays under the mutex
    rowItems_ = items;
    rowActions_ = actions;
    rowCount_ = n;  // published last: the render task reads rows through count_
  }
  releaseRows();
  requestUpdate();
}

void YbxAccountActivity::releaseRows() {
  // The app's interaction table still holds indices into the old rows; stop
  // routing touches against it until the next render.
  closeRouting();
}

int YbxAccountActivity::listCount() const { return state == State::BROWSING ? rowCount_ : 0; }

void YbxAccountActivity::buildScreen(UiScreen& screen) {
  const char* title;
  switch (view_) {
    case View::Account:
      title = tr(STR_YBX_ACCOUNT);
      break;
    case View::Sources:
      title = tr(STR_YBX_ACCOUNT_SOURCES);
      break;
    case View::Source:
      title = sourceName(sourceKind_);
      break;
    case View::Cabinet:
      title = tr(STR_YBX_ACCOUNT_CABINET);
      break;
    case View::ConfirmSignOut:
      title = tr(STR_YBX_ACCOUNT_SIGN_OUT);
      break;
  }
  screenHeader(screen, state == State::AUTH ? tr(STR_PLUGIN_SIGN_IN) : title);
  if (buildStatusScreen(screen)) return;

  if (state == State::AUTH) {
    // Device-code pairing: URL + user code + QR + waiting notice, mirroring
    // the plugin catalog's auth screen. The QR bitmap is painted in drawFooter.
    fui::TextStyle centered = screen.theme().bodyText;
    centered.align = fui::TextAlign::Center;
    fui::TextStyle code = screen.theme().titleText;
    code.align = fui::TextAlign::Center;
    code.bold = true;
    const int16_t lh = screen.target().lineHeight(centered.font);
    const int16_t gap = screen.theme().spaceMd;

    screen.target().text(screen.takeTop(lh, gap), authVerify_, centered);
    screen.target().text(screen.takeTop(screen.target().lineHeight(code.font), gap), authCode_, code);
    const fui::Rect band = screen.takeTop(QR_SIZE, gap);
    qrRect_ = fui::Rect{static_cast<int16_t>(band.x + (band.width - QR_SIZE) / 2), band.y, QR_SIZE, QR_SIZE};
    screen.target().text(screen.takeTop(lh), tr(STR_PLUGIN_AUTH_WAITING), centered);
    return;
  }

  if (state != State::BROWSING) return;

  switch (view_) {
    case View::Account:
    case View::Sources:
    case View::ConfirmSignOut: {
      if (rowCount_ == 0) {
        screen.centeredText(tr(STR_NO_ENTRIES), screen.theme().bodyText);
        return;
      }
      fui::ListProps props;
      props.items = rowItems_.data();
      props.count = rowCount_;
      props.action = ACTION_ROW;
      props.inputMask = fui::InputTouch;
      props.valueInset = 8;
      props.subtitleText = screen.theme().smallText;
      props.subtitleText.maxLines = 3;
      syncListViewport(screen, props);
      screen.list(props);
      return;
    }
    case View::Source: {
      const bool connected = summary_.connected(sourceKind_);
      catalogCenteredBlock(screen, {{sourceName(sourceKind_), true},
                                    {connected ? tr(STR_YBX_ACCOUNT_CONNECTED) : tr(STR_YBX_ACCOUNT_NOT_CONNECTED)},
                                    {connected ? detailText_ : tr(STR_YBX_ACCOUNT_CONNECT_HINT)}});
      return;
    }
    case View::Cabinet: {
      if (cabinetUrl_[0] == '\0') {
        qrRect_ = {};
        screen.centeredText(tr(STR_YBX_ACCOUNT_OFFLINE), screen.theme().bodyText);
        return;
      }
      fui::TextStyle centered = screen.theme().bodyText;
      centered.align = fui::TextAlign::Center;
      const int16_t lh = screen.target().lineHeight(centered.font);
      const int16_t gap = screen.theme().spaceMd;
      screen.target().text(screen.takeTop(lh, gap), tr(STR_YBX_ACCOUNT_SCAN_QR), centered);
      const fui::Rect band = screen.takeTop(QR_SIZE, gap);
      qrRect_ = fui::Rect{static_cast<int16_t>(band.x + (band.width - QR_SIZE) / 2), band.y, QR_SIZE, QR_SIZE};
      screen.target().text(screen.takeTop(lh), cabinetUrl_, centered);
      return;
    }
  }
}

void YbxAccountActivity::drawFooter() {
  // The QR is a raw-renderer overlay (FreeInkUI has no QR component); the base
  // render() calls drawFooter() after the app has painted.
  if ((state == State::AUTH || view_ == View::Cabinet) && qrRect_.width > 0) {
    QrUtils::drawQrCode(renderer, Rect{qrRect_.x, qrRect_.y, qrRect_.width, qrRect_.height},
                        std::string(state == State::AUTH ? authVerify_ : cabinetUrl_));
  }
  MappedInputManager::Labels labels;
  switch (state) {
    case State::AUTH:
      labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      break;
    case State::ERROR:
      labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
      break;
    case State::BROWSING: {
      const char* confirm = rowCount_ > 0 ? tr(STR_OPEN) : "";
      const char* up = rowCount_ > 1 ? tr(STR_DIR_UP) : "";
      const char* down = rowCount_ > 1 ? tr(STR_DIR_DOWN) : "";
      labels = mappedInput.mapLabels(tr(STR_BACK), confirm, up, down);
      break;
    }
    default:  // CHECK_WIFI / LOADING and child-activity handoffs
      labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      break;
  }
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

bool YbxAccountActivity::handleCustomInput() {
  if (state == State::AUTH) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      pendingPairing_ = false;
      state = State::BROWSING;
      goAccount();
      return true;
    }
    if (ybxaccount::deadlineReached(millis(), authDeadlineMs_)) {
      pendingPairing_ = false;
      fail(StrId::STR_PLUGIN_AUTH_FAILED);
      return true;
    }
    if (ybxaccount::deadlineReached(millis(), authNextPollMs_)) pollPairing();
    return true;
  }
  return CatalogActivity::handleCustomInput();
}

void YbxAccountActivity::activateIndex(const int index) {
  if (index < 0 || index >= static_cast<int>(rowCount_)) return;
  app.clearTapFlash();
  switch (rowActions_[index]) {
    case RowAction::SignIn:
      pendingPairing_ = true;
      checkAndConnectWifi();
      break;
    case RowAction::Refresh:
      pendingPairing_ = false;
      checkAndConnectWifi();
      break;
    case RowAction::Sources:
      setView(View::Sources);
      break;
    case RowAction::SourceYandex:
      openSource(ybxaccount::SourceKind::Yandex);
      break;
    case RowAction::SourceWb:
      openSource(ybxaccount::SourceKind::Wb);
      break;
    case RowAction::SourceLitres:
      openSource(ybxaccount::SourceKind::Litres);
      break;
    case RowAction::Cabinet:
      setView(View::Cabinet);
      break;
    case RowAction::SignOut:
      setView(View::ConfirmSignOut);
      break;
    case RowAction::ConfirmSignOut:
      performSignOut();
      break;
    case RowAction::CancelSignOut:
      goAccount();
      break;
  }
}

void YbxAccountActivity::onBackButton() {
  if (state == State::AUTH) {
    pendingPairing_ = false;
    state = State::BROWSING;
    goAccount();
    return;
  }
  if (state == State::ERROR) {
    state = State::BROWSING;
    errorMessage.clear();
    goAccount();
    return;
  }
  if (view_ == View::Account) {
    finish();
    return;
  }
  goAccount();
}

bool YbxAccountActivity::preventAutoSleep() {
  // The cabinet QR stays lit while a phone is aimed at it.
  return view_ == View::Cabinet || CatalogActivity::preventAutoSleep();
}

const char* YbxAccountActivity::statusLabel() const {
  if (!manifestValid_) return tr(STR_PLUGIN_MANIFEST_INVALID);
  if (token_.empty()) return tr(STR_PLUGIN_SIGN_IN_HINT);
  switch (outcome_) {
    case ybxaccount::ListsOutcome::Ok:
      return tr(STR_YBX_ACCOUNT_SIGNED_IN);
    case ybxaccount::ListsOutcome::InvalidToken:
      return tr(STR_YBX_ACCOUNT_REPAIR);
    case ybxaccount::ListsOutcome::DeviceRemoved:
      return tr(STR_YBX_ACCOUNT_DEVICE_REMOVED);
    case ybxaccount::ListsOutcome::ProviderSession:
      return tr(STR_YBX_ACCOUNT_PROVIDER_CONSENT);
    case ybxaccount::ListsOutcome::Offline:
      return tr(STR_YBX_ACCOUNT_OFFLINE);
    case ybxaccount::ListsOutcome::Failed:
      return tr(STR_YBX_ACCOUNT_UNKNOWN);
  }
  return tr(STR_YBX_ACCOUNT_UNKNOWN);
}

const char* YbxAccountActivity::sourceName(const ybxaccount::SourceKind kind) const {
  switch (kind) {
    case ybxaccount::SourceKind::Yandex:
      return tr(STR_YBX_ACCOUNT_SOURCE_YANDEX);
    case ybxaccount::SourceKind::Wb:
      return tr(STR_YBX_ACCOUNT_SOURCE_WB);
    case ybxaccount::SourceKind::Litres:
      return tr(STR_YBX_ACCOUNT_SOURCE_LITRES);
  }
  return tr(STR_YBX_ACCOUNT_SOURCES);
}

void YbxAccountActivity::statusSubtitle(char* buffer, const size_t size) const {
  buffer[0] = '\0';
  if (!manifestValid_ || token_.empty() || outcome_ != ybxaccount::ListsOutcome::Ok) return;
  if (summary_.totalLists == 0) {
    std::snprintf(buffer, size, "%s", tr(STR_YBX_ACCOUNT_NO_SOURCES));
    return;
  }
  size_t used = 0;
  const auto append = [&](const char* text) {
    const size_t room = size - used;
    const int written = std::snprintf(buffer + used, room, "%s", text);
    if (written < 0 || static_cast<size_t>(written) >= room) return false;
    used += static_cast<size_t>(written);
    return true;
  };
  for (size_t i = 0; i < ybxaccount::SOURCE_COUNT; ++i) {
    const auto kind = static_cast<ybxaccount::SourceKind>(i);
    if (!summary_.connected(kind)) continue;
    if (used > 0 && !append(" \xC2\xB7 ")) break;
    if (!append(sourceName(kind))) break;
  }
}

void YbxAccountActivity::copyField(const std::string_view value, char* out, const size_t size) {
  if (size == 0) return;
  const size_t n = std::min(value.size(), size - 1);
  std::memcpy(out, value.data(), n);
  out[n] = '\0';
}
