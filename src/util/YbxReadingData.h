#pragma once
#include "YbxAwards.h"
#include "YbxReadingSummary.h"

namespace ybx {
enum class ReadingResult { Ok, NotConnected, ManifestUnsupported, NetworkError, InvalidData, StorageError };
// No network. Cache is bound to the current token, so another account cannot
// inherit the previous account's statistics after relinking.
ReadingResult readingConnectionState();
bool loadReadingSummary(ReadingSummary& out);
ReadingResult refreshReadingSummary(ReadingSummary& out);
ReadingResult awardsConnectionState();
bool loadAwardsPage(AwardsPage& out, AwardSection section, size_t offset);
ReadingResult refreshAwardsPage(AwardsPage& out, AwardSection section, size_t offset);
}  // namespace ybx
