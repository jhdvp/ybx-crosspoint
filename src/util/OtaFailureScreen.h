#pragma once

#include "network/OtaUpdater.h"

// The pure decisions behind the OTA update failure screen: what the failure
// line says and whether re-running the failed step can help. Kept out of the
// activity so the host suite covers the table without a panel.
namespace otafailure {

enum class Reason {
  Memory,           // an allocation for the release lookup failed
  Network,          // no HTTP answer, or the body stopped early
  Server,           // the release JSON arrived but could not be read
  WrongDevice,      // the image names another board or MCU
  NoUpdate,         // the release is not newer than the running firmware
  FlashWrite,       // the OTA partition APIs refused the write
  UntrustedSource,  // release metadata points off the fork's release hosts
  ImageSize,        // the published image cannot fit this board's OTA slot
  CorruptImage,     // the body is not an ESP image or arrived incomplete
  Tls,              // certificate, clock or redirect policy refused the transfer
  Generic,          // anything else
};

struct Failure {
  Reason reason;
  // False when repeating the step returns the same answer, so the screen
  // offers Back alone.
  bool allowRetry;
};

// `error`: the non-OK value a step returned. OK/NO_UPDATE are not failures;
// they land on a retry-less Generic so a caller that routes one here by
// mistake cannot loop the user on a screen that re-runs a step that already
// succeeded.
constexpr Failure classify(const OtaUpdater::OtaUpdaterError error) {
  switch (error) {
    case OtaUpdater::OOM_ERROR:
      return {Reason::Memory, true};
    case OtaUpdater::HTTP_ERROR:
      return {Reason::Network, true};
    case OtaUpdater::JSON_PARSE_ERROR:
      return {Reason::Server, true};
    case OtaUpdater::WRONG_DEVICE_ERROR:
      return {Reason::WrongDevice, false};
    case OtaUpdater::UPDATE_OLDER_ERROR:
      return {Reason::NoUpdate, false};
    case OtaUpdater::INTERNAL_UPDATE_ERROR:
      return {Reason::FlashWrite, true};
    case OtaUpdater::UNTRUSTED_SOURCE_ERROR:
      return {Reason::UntrustedSource, false};
    case OtaUpdater::IMAGE_SIZE_ERROR:
      return {Reason::ImageSize, false};
    case OtaUpdater::CORRUPT_IMAGE_ERROR:
      return {Reason::CorruptImage, true};
    case OtaUpdater::TLS_ERROR:
      // A clock that was too far off to validate certificate dates can be
      // right on the next attempt, so this one stays retryable.
      return {Reason::Tls, true};
    case OtaUpdater::OK:
    case OtaUpdater::NO_UPDATE:
      break;
  }
  return {Reason::Generic, false};
}

}  // namespace otafailure
