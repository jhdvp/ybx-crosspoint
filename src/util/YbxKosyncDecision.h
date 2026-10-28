#pragma once

#include <cmath>
#include <cstdint>

// Pure decisions behind the YBX sync setup. Kept free of storage, HTTP and UI so
// the host suite can exercise every branch.
namespace ybxkosync {

// What the last sync transaction on this reader did.
enum class Outcome : uint8_t {
  None = 0,      // nothing synced since the pairing
  Pushed,        // the server accepted our position
  Pulled,        // a server position was applied here
  Already,       // both sides already matched
  Offline,       // transport failed: the position never left the device
  AuthRejected,  // the reader key was refused
  Refused,       // the server declined the account/device (409)
  Failed,        // any other server or parse failure
};

// Verdict shown to the user, ordered by what has to be fixed first.
enum class Status : uint8_t {
  Disabled,
  NotPaired,
  ManualServer,
  NeverSynced,
  AuthRejected,
  Refused,
  Offline,
  Failed,
  Mismatch,
  Pushed,
  Pulled,
  Already,
};

// What one provisioning pass should do with the built-in KOSync configuration.
enum class Action : uint8_t {
  None,        // nothing to write
  Apply,       // point KOSync at the YBX bridge with the current reader key
  Adopt,       // KOSync already points there; only record the pairing as ours
  Clear,       // drop the YBX-derived credentials, keep anything else untouched
  Forget,      // the plugin is gone: drop the derived credentials and the state
  Abandon,     // the user replaced our configuration by hand: stop managing sync
  KeepManual,  // the user configured another sync server; never overwrite it
};

// Divergence still accepted as "the same place": the reference YBX clients apply
// a server position silently within one percentage point and ask above it.
constexpr float MISMATCH_CONSENT_SHARE = 0.01f;

// Everything the decision below needs, as facts about the card and the settings
// on file. Hashing the reader key and comparing it with what was written last is
// the caller's job: this only says what a given combination of facts means.
struct ProvisionState {
  bool enabled = false;             // the reader's own YBX sync switch
  bool pluginInstalled = false;     // the YBX plugin folder is on the card
  bool tokenPresent = false;        // it carries a readable reader key
  bool provisioned = false;         // KOSync settings were written by this module
  bool credentialsPresent = false;  // KOSync has any credentials at all
  bool pointsAtYbx = false;         // those credentials target the YBX bridge
  bool keyMatches = false;          // and already carry the key read just now
};

inline Action decideAction(const ProvisionState& s) {
  // Opting out may only forget what this module wrote: a server the user typed
  // by hand stays configured.
  if (!s.enabled) return (s.provisioned || s.pointsAtYbx) ? Action::Clear : Action::None;
  if (!s.pluginInstalled) return s.provisioned ? Action::Forget : Action::None;
  // A missing key says nothing about the pairing (the plugin may be mid-write), so
  // keep the last configuration and let a refused request report it.
  if (!s.tokenPresent) return Action::None;
  if (!s.provisioned) {
    if (!s.credentialsPresent) return Action::Apply;
    if (!s.pointsAtYbx) return Action::KeepManual;
    return s.keyMatches ? Action::Adopt : Action::Apply;
  }
  // Editing the sync settings by hand after we wrote them takes the server back:
  // keep what the reader now holds instead of overwriting it again.
  if (!s.pointsAtYbx) return Action::Abandon;
  return s.keyMatches ? Action::None : Action::Apply;
}

// True when jumping to the server position is far enough that the reader must
// ask first. Unknown shares keep the caller's existing behavior.
inline bool remoteJumpNeedsConsent(const float localShare, const float remoteShare) {
  if (!std::isfinite(localShare) || !std::isfinite(remoteShare)) return false;
  return remoteShare - localShare > MISMATCH_CONSENT_SHARE;
}

inline Status deriveStatus(const bool enabled, const bool tokenPresent, const bool manualServer,
                           const bool mismatchPending, const Outcome last) {
  if (!enabled) return Status::Disabled;
  if (!tokenPresent) return Status::NotPaired;
  if (manualServer) return Status::ManualServer;
  switch (last) {
    case Outcome::AuthRejected:
      return Status::AuthRejected;
    case Outcome::Refused:
      return Status::Refused;
    case Outcome::Offline:
      return Status::Offline;
    case Outcome::Failed:
      return Status::Failed;
    default:
      break;
  }
  // An unapplied server position outranks the last transaction: it is what the
  // reader is waiting on, exactly like the clients' conflict state.
  if (mismatchPending) return Status::Mismatch;
  switch (last) {
    case Outcome::Pushed:
      return Status::Pushed;
    case Outcome::Pulled:
      return Status::Pulled;
    case Outcome::Already:
      return Status::Already;
    default:
      return Status::NeverSynced;
  }
}

// Age bucket for the status line: minutes up to an hour, hours up to two days,
// days after that. Returns false when there is nothing to date.
inline bool describeAge(const int64_t at, const int64_t now, int& value, int& unit) {
  if (at <= 0 || now < at) return false;
  const int64_t minutes = (now - at) / 60;
  if (minutes < 60) {
    value = static_cast<int>(minutes);
    unit = 0;
    return true;
  }
  if (minutes < 60 * 48) {
    value = static_cast<int>(minutes / 60);
    unit = 1;
    return true;
  }
  value = static_cast<int>(minutes / (60 * 24));
  unit = 2;
  return true;
}

}  // namespace ybxkosync
