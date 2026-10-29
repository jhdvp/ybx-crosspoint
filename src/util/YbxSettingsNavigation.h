#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace ybxsettings {
// The eight rows of the original 08-settings mockup plus one explicit entry
// retaining the complete FreeInk settings tree (network/controls/library).
enum class Row : uint8_t {
  Account, Sources, Sync, Appearance, Language, Sleep, Updates, Support, System,
};
enum class Destination : uint8_t {
  Account, Sync, DisplayTheme, Language, DisplaySleep, Updates, About, AllSettings,
};
constexpr std::array<Row,9> ORDER = {
    Row::Account, Row::Sources, Row::Sync, Row::Appearance, Row::Language,
    Row::Sleep, Row::Updates, Row::Support, Row::System,
};
constexpr Destination destination(Row row) {
  switch (row) {
    case Row::Account:
    case Row::Sources: return Destination::Account;
    case Row::Sync: return Destination::Sync;
    case Row::Appearance: return Destination::DisplayTheme;
    case Row::Language: return Destination::Language;
    case Row::Sleep: return Destination::DisplaySleep;
    case Row::Updates: return Destination::Updates;
    case Row::Support: return Destination::About;
    case Row::System: return Destination::AllSettings;
  }
  return Destination::AllSettings;
}
constexpr bool selectable(int index) { return index>=0 && static_cast<size_t>(index)<ORDER.size(); }
}  // namespace ybxsettings
