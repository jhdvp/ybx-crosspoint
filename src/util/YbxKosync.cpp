#include "util/YbxKosync.h"

#include <ArduinoJson.h>
#include <Logging.h>
#include <PersistableStore.h>
#include <TrustedTime.h>

#include <string>

#include "KOReaderCredentialStore.h"
#include "util/PluginHttp.h"
#include "util/PluginLocations.h"

namespace {

using ybxkosync::Action;
using ybxkosync::decideAction;
using ybxkosync::Outcome;
using ybxkosync::ProvisionState;
using ybxkosync::ybxDerivedCredentialsRemain;

constexpr char YBX_PLUGIN[] = "ybx";
// The reader key is the credential the YBX bridge checks as the KOSync password, so
// no other account secret ever reaches the reader.
constexpr char YBX_SYNC_URL[] = "https://my.ybx.app/api/v1/crosspoint/kosync";
constexpr char YBX_SYNC_USER[] = "ybx";
// The key's file name is part of the plugin's published contract: the browser page
// and the installer keep it across updates.
constexpr char TOKEN_FILE[] = "token.json";
constexpr char TOKEN_PATH[] = "access_token";
// Installs that predate the plugin folder kept the key here.
constexpr char LEGACY_TOKEN_FILE[] = "/.crosspoint/plugin-data/ybx/token.json";

// Switch, history and a fingerprint of the key written last. The key itself is
// stored once only: obfuscated as the KOSync password in koreader.json.
class YbxKosyncStore : public PersistableStore<YbxKosyncStore> {
 private:
  YbxKosyncStore() = default;
  friend class PersistableStore<YbxKosyncStore>;

 public:
  bool enabled = true;
  bool provisioned = false;
  bool mismatch = false;
  uint32_t keyFp = 0;
  uint8_t outcome = 0;
  int64_t outcomeAt = 0;

  static const char* getFilePath() { return "/.crosspoint/ybx-kosync.json"; }
  void toJson(JsonDocument& doc) const {
    doc["enabled"] = enabled;
    doc["provisioned"] = provisioned;
    doc["mismatch"] = mismatch;
    doc["keyFp"] = keyFp;
    doc["outcome"] = outcome;
    doc["outcomeAt"] = outcomeAt;
  }
  bool fromJson(JsonVariantConst doc) {
    enabled = doc["enabled"] | true;
    provisioned = doc["provisioned"] | false;
    mismatch = doc["mismatch"] | false;
    keyFp = doc["keyFp"] | static_cast<uint32_t>(0);
    outcome = doc["outcome"] | static_cast<uint8_t>(0);
    outcomeAt = doc["outcomeAt"] | static_cast<int64_t>(0);
    return true;
  }
};

#define YBX_SYNC YbxKosyncStore::getInstance()

// Facts from the last pass, cached so a repaint reads no storage.
bool loaded = false;
bool stateTokenPresent = false;
bool stateManualConflict = false;

// FNV-1a over the reader key: rotation is visible without a second copy of it.
// Never 0, which means "nothing written yet".
uint32_t keyFingerprint(const std::string& key) {
  uint32_t hash = 2166136261u;
  for (const char c : key) hash = (hash ^ static_cast<uint8_t>(c)) * 16777619u;
  return hash == 0 ? 1 : hash;
}

bool readReaderKey(const std::string& dir, std::string& key) {
  if (!pluginhttp::loadTokenFromFile(pluginhttp::inPluginDir(dir, TOKEN_FILE), TOKEN_PATH, key)) key.clear();
  if (key.empty() && !pluginhttp::loadTokenFromFile(LEGACY_TOKEN_FILE, TOKEN_PATH, key)) key.clear();
  return !key.empty();
}

// YBX matches books by content hash like the other YBX clients, and the bridge
// translates positions between copies of the same work using the extended position
// and the book id the catalog recorded next to the downloaded file.
bool writeConfiguration(const std::string& key) {
  KOREADER_STORE.setCredentials(YBX_SYNC_USER, key);
  KOREADER_STORE.setServerUrl(YBX_SYNC_URL);
  KOREADER_STORE.setMatchMethod(DocumentMatchMethod::BINARY);
  KOREADER_STORE.setSyncBehavior(KOReaderSyncBehavior::SMART);
  KOREADER_STORE.setSendMetadata(true);
  KOREADER_STORE.setServerType(KOReaderServerType::CROSSPOINT);
  if (!KOREADER_STORE.saveToFile()) {
    LOG_ERR("YBXK", "Failed to write the sync configuration");
    return false;
  }
  return true;
}

void resetHistory() {
  YBX_SYNC.outcome = static_cast<uint8_t>(Outcome::None);
  YBX_SYNC.outcomeAt = 0;
  YBX_SYNC.mismatch = false;
}

Action run(const bool forced) {
  if (!loaded) ybxkosync::begin();

  // findPluginDir honours the global Plugin System switch, so a disabled plugin is
  // not paired; the disk probe only separates that from an uninstalled one, because
  // turning Plugins off must not discard a configuration the reader still holds.
  const std::string dir = PluginLocations::findPluginDir(YBX_PLUGIN);
  const bool installed = !dir.empty() || !PluginLocations::findPluginDirOnDisk(YBX_PLUGIN).empty();

  std::string key;
  stateTokenPresent = !dir.empty() && readReaderKey(dir, key);
  const uint32_t fp = stateTokenPresent ? keyFingerprint(key) : 0;

  const bool pointsAtYbx = KOREADER_STORE.hasCredentials() && KOREADER_STORE.getServerUrl() == YBX_SYNC_URL &&
                           KOREADER_STORE.getUsername() == YBX_SYNC_USER;
  // Whether the credential on file is the key read just now: a rotated key differs
  // in the fingerprint, a hand-typed password in the text itself.
  const bool liveCarriesKey = pointsAtYbx && KOREADER_STORE.getPassword() == key;

  ProvisionState state;
  state.enabled = YBX_SYNC.enabled;
  state.pluginInstalled = installed;
  state.tokenPresent = stateTokenPresent;
  state.provisioned = YBX_SYNC.provisioned;
  state.credentialsPresent = KOREADER_STORE.hasCredentials();
  state.pointsAtYbx = pointsAtYbx;
  state.keyFpMatches = YBX_SYNC.keyFp == fp;
  state.credentialCarriesKey = liveCarriesKey;

  Action action = decideAction(state);
  if (action == Action::KeepManual && forced) action = Action::Apply;
  // A hand-typed server while a key is on the card: the status row offers YBX.
  stateManualConflict = state.enabled && stateTokenPresent && state.credentialsPresent && !pointsAtYbx;

  switch (action) {
    case Action::None:
      break;
    case Action::Adopt:
      // A configuration the browser page wrote becomes ours, so an opt-out or a
      // rotated key is handled here from then on.
      YBX_SYNC.provisioned = true;
      YBX_SYNC.keyFp = fp;
      YBX_SYNC.saveToFile();
      break;
    case Action::Apply:
      if (!writeConfiguration(key)) return Action::None;
      YBX_SYNC.provisioned = true;
      YBX_SYNC.keyFp = fp;
      resetHistory();
      YBX_SYNC.saveToFile();
      LOG_INF("YBXK", "Sync pointed at YBX for the paired reader");
      break;
    case Action::Clear:
    case Action::Forget:
      // Opting out drops only what this module (or the browser page it replaced)
      // wrote: the credentials and the server URL set with them.
      if (KOREADER_STORE.hasCredentials()) KOREADER_STORE.clearCredentials();
      if (KOREADER_STORE.getServerUrl() == YBX_SYNC_URL) {
        KOREADER_STORE.setServerUrl("");
        KOREADER_STORE.saveToFile();
      }
      YBX_SYNC.provisioned = false;
      YBX_SYNC.keyFp = 0;
      resetHistory();
      YBX_SYNC.saveToFile();
      break;
    case Action::Abandon:
      // The reader's own sync settings again: stop writing over them, keep the
      // pairing visible in the list so "use YBX" stays one tap away.
      // A hand-typed server must never carry the reader key: drop the YBX-derived
      // credentials too, keeping only the URL the user entered.
      if (ybxDerivedCredentialsRemain(KOREADER_STORE.getUsername(), KOREADER_STORE.getPassword(), YBX_SYNC_USER, key)) {
        KOREADER_STORE.clearCredentials();
        KOREADER_STORE.saveToFile();
      }
      YBX_SYNC.provisioned = false;
      // Kept, unlike Clear/Forget: it lets the next pass recognise the stored
      // password as the user's hand-edit rather than a stale key to rewrite.
      YBX_SYNC.keyFp = fp;
      resetHistory();
      YBX_SYNC.saveToFile();
      break;
    case Action::KeepManual:
      LOG_DBG("YBXK", "Another sync server is configured; YBX setup left alone");
      break;
  }
  return action;
}

}  // namespace

namespace ybxkosync {

void begin() {
  loaded = true;
  if (!YBX_SYNC.loadFromFile()) LOG_DBG("YBXK", "No saved sync state yet");
}

Action refresh() { return run(false); }

Action takeOver() { return run(true); }

bool isEnabled() { return YBX_SYNC.enabled; }

void setEnabled(const bool enabled) {
  if (YBX_SYNC.enabled == enabled) return;
  YBX_SYNC.enabled = enabled;
  YBX_SYNC.saveToFile();
  run(false);
}

bool isVisible() { return stateTokenPresent || YBX_SYNC.provisioned || stateManualConflict; }

bool needsTakeOver() { return stateManualConflict; }

bool isActive() { return YBX_SYNC.enabled && YBX_SYNC.provisioned; }

Status getStatus() {
  return deriveStatus(YBX_SYNC.enabled, stateTokenPresent, stateManualConflict, YBX_SYNC.mismatch,
                      static_cast<Outcome>(YBX_SYNC.outcome));
}

Outcome getLastOutcome() { return static_cast<Outcome>(YBX_SYNC.outcome); }

int64_t getLastOutcomeAt() { return YBX_SYNC.outcomeAt; }

bool isMismatchPending() { return YBX_SYNC.mismatch; }

void setMismatchPending(const bool pending) {
  if (YBX_SYNC.mismatch == pending) return;
  YBX_SYNC.mismatch = pending;
  YBX_SYNC.saveToFile();
}

void noteTransaction(const Outcome outcome) {
  if (!isActive()) return;
  YBX_SYNC.outcome = static_cast<uint8_t>(outcome);
  YBX_SYNC.outcomeAt = trustedtime::trustedNow();
  if (outcome == Outcome::Pushed || outcome == Outcome::Pulled || outcome == Outcome::Already) {
    YBX_SYNC.mismatch = false;
  }
  YBX_SYNC.saveToFile();
}

}  // namespace ybxkosync
