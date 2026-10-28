#pragma once

#include <cstdint>

#include "util/YbxKosyncDecision.h"

// Native KOSync setup for a reader paired with a YBX account.
//
// The pairing already carries everything the sync needs: the YBX bridge accepts the
// reader key as the KOSync password, so the reader configures its built-in sync
// itself instead of asking for a server URL and password. Document hashing,
// position mapping, the pull/upload screens and retries stay in the KOSync client and
// activity; this module writes no connection of its own and starts no timer.
namespace ybxkosync {

// Reads back the saved switch and history. Call once at boot, before the sync
// settings or the reader can use this module.
void begin();

// Reconciles the KOSync configuration with the pairing on the card. Reads two small
// plugin files and touches storage only when something changed, so the callers below
// can each run it without a schedule: after a key is written, when the sync settings
// open, and before the reader starts a sync.
Action refresh();

// Same, but the user chose YBX over the server they configured by hand.
Action takeOver();

// The reader's own switch. Off forgets only the configuration this module wrote.
bool isEnabled();
void setEnabled(bool enabled);

// True when the sync settings should show the YBX rows.
bool isVisible();

// True when another sync server is configured and "use YBX instead" can replace it.
bool needsTakeOver();

// True while the built-in client is pointed at YBX by this module.
bool isActive();

// Status line inputs.
Status getStatus();
Outcome getLastOutcome();
int64_t getLastOutcomeAt();
bool isMismatchPending();
void setMismatchPending(bool pending);

// Records what the last pull/push attempt did. Ignored unless the reader syncs
// through YBX, so a manual server keeps no YBX history.
void noteTransaction(Outcome outcome);

}  // namespace ybxkosync
