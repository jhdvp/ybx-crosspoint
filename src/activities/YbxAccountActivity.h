#pragma once

#include <I18n.h>

#include <array>
#include <memory>
#include <string>
#include <string_view>

#include "activities/CatalogActivity.h"
#include "util/PluginHttp.h"
#include "util/YbxAccountModel.h"

namespace freeink {
class SecureHttpClient;
}

// Native YBX account screen: device-code sign-in, the connected sources the
// account really has, and the cabinet QR that manages them. It reuses the YBX
// plugin's own auth endpoints and token file (one login stack) and works
// whether or not the plugin's web page was ever opened.
class YbxAccountActivity final : public CatalogActivity {
 public:
  // Out-of-line construction/destruction needs the complete SecureHttpClient type.
  YbxAccountActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~YbxAccountActivity() override;

  void onEnter() override;
  void onExit() override;

 protected:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void drawFooter() override;
  bool handleCustomInput() override;
  void onBackButton() override;
  bool preventAutoSleep() override;

  void startBrowse() override;
  void wifiSelectionCancelled() override;
  bool hasSearch() const override { return false; }
  void performSearch(const std::string&) override {}
  void downloadFinished(bool) override {}

 private:
  enum class View : uint8_t { Account, Sources, Source, Cabinet, ConfirmSignOut };
  enum class RowAction : uint8_t {
    SignIn,
    Refresh,
    Sources,
    SourceYandex,
    SourceWb,
    SourceLitres,
    Cabinet,
    SignOut,
    ConfirmSignOut,
    CancelSignOut,
  };

  struct Manifest {
    std::string tokenFile, tokenPath, configFile;
    std::string authType;
    pluginhttp::RequestSpec authReq, pollReq;
    std::string authCodePath, authVerifyPath, authDeviceCodePath;
    std::string authIntervalPath, authExpiresPath, authTokenPath, authErrorPath;
    std::string listsUrl;
    pluginhttp::Headers listsHeaders;
    bool hasDeviceCode() const {
      return authType == "device_code" && !authReq.url.empty() && !pollReq.url.empty();
    }
  };

  static constexpr int MAX_ROWS = 6;
  static constexpr size_t MAX_MANIFEST_SIZE = 8 * 1024;
  // The account screen fetches the device's own browse.lists_url; the plugin
  // catalog reads the same endpoint with a 48 KB cap, so a response that fits
  // there must not be truncated here and shown as "Offline".
  static constexpr size_t MAX_API_RESPONSE = 48 * 1024;
  // Pairing fields are copied into fixed slots; the server's own limits are
  // 160 characters for the device code and a short formatted user code.
  static constexpr size_t AUTH_CODE_SIZE = 32;
  static constexpr size_t AUTH_VERIFY_SIZE = 192;
  static constexpr size_t AUTH_DEVICE_CODE_SIZE = 176;
  static constexpr size_t CABINET_URL_SIZE = 96;
  static constexpr int16_t QR_SIZE = 180;

  void loadAccountFiles();
  bool loadManifest();
  std::string substituted(const std::string& value) const;
  pluginhttp::Headers substitutedHeaders(const pluginhttp::Headers& headers) const;
  void ensureSession();

  void rebuildRows();
  void releaseRows();
  void setView(View view);
  void goAccount();
  void openSource(ybxaccount::SourceKind kind);

  int apiRequest(const pluginhttp::RequestSpec& req, String& out,
                 pluginhttp::Headers* responseHeaders = nullptr);
  void runRefresh();
  void beginPairing();
  void pollPairing();
  void finishPairing();
  void performSignOut();
  void clearForeignCaches();

  const char* statusLabel() const;
  const char* sourceName(ybxaccount::SourceKind kind) const;
  void statusSubtitle(char* buffer, size_t size) const;
  // Reads a JSON string field into a fixed slot, truncating over-long values.
  static void copyField(std::string_view value, char* out, size_t size);

  std::string pluginDir_;
  Manifest manifest_;
  bool manifestValid_ = false;
  std::string token_;
  pluginhttp::Headers config_;  // {cfg.KEY} values from the plugin config file

  // Written on the loop task under the render lock, read on the render task.
  std::array<freeink::ui::ListItem, MAX_ROWS> rowItems_{};
  std::array<RowAction, MAX_ROWS> rowActions_{};
  char rowNumber_[MAX_ROWS][8] = {};
  // Joined source names shown under the status row.
  char detailText_[128] = {};
  uint8_t rowCount_ = 0;

  View view_ = View::Account;
  ybxaccount::SourceKind sourceKind_ = ybxaccount::SourceKind::Yandex;
  ybxaccount::Summary summary_;
  ybxaccount::ListsOutcome outcome_ = ybxaccount::ListsOutcome::Failed;
  // Wi-Fi was acquired to start a pairing that the token check deferred.
  bool pendingPairing_ = false;

  char authCode_[AUTH_CODE_SIZE] = {};
  char authVerify_[AUTH_VERIFY_SIZE] = {};
  char authDeviceCode_[AUTH_DEVICE_CODE_SIZE] = {};
  unsigned long authIntervalMs_ = 0;
  unsigned long authDeadlineMs_ = 0;
  unsigned long authNextPollMs_ = 0;

  char cabinetUrl_[CABINET_URL_SIZE] = {};
  freeink::ui::Rect qrRect_{};

  std::unique_ptr<freeink::SecureHttpClient> session_;
};
