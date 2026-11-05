#pragma once

#include <I18n.h>

#include "network/TlsPolicy.h"

// User-facing explanation for a transport-trust failure. Kept apart from
// TlsPolicy so the policy logic stays host-testable without the i18n tables.
namespace tlsui {

// StrId explaining `failure`; false means the failure has no
// certificate-specific explanation (None/Network) and the caller should show
// its generic error.
bool failureMessage(tlspolicy::Failure failure, StrId& out);

// Same message as text, or nullptr for failures without one. The returned
// pointer is flash-resident for the life of the firmware.
const char* failureDetail(tlspolicy::Failure failure);

}  // namespace tlsui
