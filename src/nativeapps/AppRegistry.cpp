#include "AppRegistry.h"

#include <BoardConfig.h>
#include <Memory.h>

#include <array>

#include "CatalogPolicy.h"
#include "MemoryStatusActivity.h"

namespace nativeapps {
namespace {

std::unique_ptr<Activity> createMemoryStatus(GfxRenderer& renderer, MappedInputManager& input) {
  return makeUniqueNoThrow<MemoryStatusActivity>(renderer, input);
}

#if FREEINK_DEVICE_X3 || FREEINK_DEVICE_X4 || FREEINK_DEVICE_X4PRO
// Deliberately small pilot registry: all other InkHome apps remain unlinked.
constexpr std::array<Descriptor, 1> kApps{{
    {"memory-status", StrId::STR_YBX_APP_MEMORY, StrId::STR_YBX_APP_MEMORY_SUMMARY, createMemoryStatus, nullptr, 12 * 1024},
}};
#else
constexpr std::array<Descriptor, 0> kApps{};
#endif

static_assert(catalog::valid(kApps.data(), kApps.size()), "Invalid native app metadata");

}  // namespace

bool isCatalogValid() { return catalog::valid(kApps.data(), kApps.size()); }
size_t availableCount() { return catalog::availableCount(kApps.data(), kApps.size()); }
const Descriptor* availableAt(size_t index) { return catalog::availableAt(kApps.data(), kApps.size(), index); }

}  // namespace nativeapps
