#include "TlsFailureText.h"

namespace tlsui {

bool failureMessage(const tlspolicy::Failure failure, StrId& out) {
  switch (failure) {
    case tlspolicy::Failure::Clock:
      out = StrId::STR_TLS_CLOCK_FAILED;
      return true;
    case tlspolicy::Failure::Certificate:
    case tlspolicy::Failure::Configuration:
      out = StrId::STR_TLS_CERT_FAILED;
      return true;
    case tlspolicy::Failure::Hostname:
      out = StrId::STR_TLS_HOSTNAME_FAILED;
      return true;
    case tlspolicy::Failure::InsecureRedirect:
      out = StrId::STR_TLS_INSECURE_REDIRECT;
      return true;
    case tlspolicy::Failure::None:
    case tlspolicy::Failure::Network:
      return false;
  }
  return false;
}

const char* failureDetail(const tlspolicy::Failure failure) {
  StrId id;
  return failureMessage(failure, id) ? I18N.get(id) : nullptr;
}

}  // namespace tlsui
