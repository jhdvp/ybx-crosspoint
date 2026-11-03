#pragma once
#include <string>
#include <string_view>

#include "util/YbxNoteReanchor.h"

namespace ybxnote::contextstore {

// A private, OPTIONAL book-local companion to ClippingStore V4.
// No server wire-format change. The context sidecar follows the clipping
// store through built-in file/folder moves, but must be copied alongside it
// for external SD transfers. Missing anchors use the previous PR's exact
// unique-text fallback; corrupt anchors NEVER cause unsafe reattachment.
enum class Status : uint8_t { Missing, Found, Invalid };

Status read(const std::string& path, std::string_view clippingId,
            std::string_view quote, Context& out);
bool save(const std::string& path, std::string_view clippingId,
          std::string_view quote, const Context& anchor);

// A corrupt/ambiguous persisted sidecar must NEVER silently fall back to
// quote-only relocation. Only truly absent legacy context permits fallback.
inline Candidate resolve(std::string_view visible, std::string_view quote,
                         Status status, const Context& context) {
  if (status == Status::Found) return findWithContext(visible, quote, context);
  if (status == Status::Missing) return findUnique(visible, quote);
  return {};
}

inline constexpr size_t kMaxRecords = 64;
} // namespace ybxnote::contextstore
