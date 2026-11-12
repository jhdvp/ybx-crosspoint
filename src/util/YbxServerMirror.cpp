#include "util/YbxServerMirror.h"

#include <Arduino.h>
#include <Logging.h>
#include <PersistableStore.h>

namespace {

// The human's server choice. Kept in its own file so the shared settings format
// and the web settings table stay untouched.
class YbxServerStore : public PersistableStore<YbxServerStore> {
 private:
  YbxServerStore() = default;
  friend class PersistableStore<YbxServerStore>;

 public:
  uint8_t mode = 0;

  static const char* getFilePath() { return "/.crosspoint/ybx-server.json"; }
  void toJson(JsonDocument& doc) const { doc["mode"] = mode; }
  bool fromJson(JsonVariantConst doc) {
    mode = doc["mode"] | static_cast<uint8_t>(0);
    if (mode >= ybxmirror::MODE_COUNT) mode = static_cast<uint8_t>(ybxmirror::Mode::Auto);
    return true;
  }
};

#define YBX_SERVER YbxServerStore::getInstance()

// What Auto remembers, per family and scheme: the reader's TLS reachability is
// what decides, and http and https can differ (the sync host answers on http).
ybxmirror::Preference prefs[ybxmirror::FAMILY_COUNT][2];
bool loaded = false;

void ensureLoaded() {
  if (loaded) return;
  YBX_SERVER.loadFromFile();
  loaded = true;
}

}  // namespace

namespace ybxmirror {

Mode currentMode() {
  ensureLoaded();
  const uint8_t raw = YBX_SERVER.mode;
  return raw < MODE_COUNT ? static_cast<Mode>(raw) : Mode::Auto;
}

void setMode(const Mode mode) {
  ensureLoaded();
  YBX_SERVER.mode = static_cast<uint8_t>(mode);
  // A mode change forgets the remembered hosts so Auto starts from main again.
  for (auto& family : prefs) {
    for (auto& scheme : family) scheme = Preference{};
  }
  if (!YBX_SERVER.saveToFile()) LOG_ERR("YBXS", "Failed to write the server choice");
}

const char* modeName(const Mode mode) {
  switch (mode) {
    case Mode::Main:
      return "main";
    case Mode::Eu:
      return "eu";
    case Mode::Pt:
      return "pt";
    case Mode::Auto:
      break;
  }
  return "auto";
}

Plan::Plan(const std::string& url) : url_(url), parsed_(parseUrl(url_)) {
  if (!parsed_.mirrorable) return;
  family_ = familyForHost(parsed_.host);
  schemeIndex_ = parsed_.scheme == "http" ? 0 : 1;
  ensureLoaded();
  count_ = candidates(currentMode(), prefs[family_][schemeIndex_], millis(), slots_);
  first_ = slots_[0];
}

std::string Plan::urlAt(const uint8_t index) const {
  if (family_ < 0 || index >= count_) return url_;
  return withHost(url_, parsed_, FAMILY_HOSTS[family_][slots_[index]]);
}

void Plan::answered(const uint8_t index) {
  if (family_ < 0 || index >= count_) return;
  prefs[family_][schemeIndex_] = afterSuccess(slots_[index], prefs[family_][schemeIndex_], millis());
}

void Plan::allFailed() {
  if (family_ < 0) return;
  prefs[family_][schemeIndex_] = afterAllFailed(first_, millis());
}

}  // namespace ybxmirror
