#include <gtest/gtest.h>

#include "util/OtaFailureScreen.h"

TEST(OtaFailureClassify, EveryErrorCodeNamesAReason) {
  EXPECT_EQ(otafailure::classify(OtaUpdater::OOM_ERROR).reason, otafailure::Reason::Memory);
  EXPECT_EQ(otafailure::classify(OtaUpdater::HTTP_ERROR).reason, otafailure::Reason::Network);
  EXPECT_EQ(otafailure::classify(OtaUpdater::JSON_PARSE_ERROR).reason, otafailure::Reason::Server);
  EXPECT_EQ(otafailure::classify(OtaUpdater::WRONG_DEVICE_ERROR).reason, otafailure::Reason::WrongDevice);
  EXPECT_EQ(otafailure::classify(OtaUpdater::UPDATE_OLDER_ERROR).reason, otafailure::Reason::NoUpdate);
  EXPECT_EQ(otafailure::classify(OtaUpdater::INTERNAL_UPDATE_ERROR).reason, otafailure::Reason::FlashWrite);
  EXPECT_EQ(otafailure::classify(OtaUpdater::UNTRUSTED_SOURCE_ERROR).reason, otafailure::Reason::UntrustedSource);
  EXPECT_EQ(otafailure::classify(OtaUpdater::IMAGE_SIZE_ERROR).reason, otafailure::Reason::ImageSize);
  EXPECT_EQ(otafailure::classify(OtaUpdater::CORRUPT_IMAGE_ERROR).reason, otafailure::Reason::CorruptImage);
  EXPECT_EQ(otafailure::classify(OtaUpdater::TLS_ERROR).reason, otafailure::Reason::Tls);
}

TEST(OtaFailureClassify, OnlyFailuresThatCanRepeatOfferRetry) {
  // A truncated download, a refused flash write, a memory error or a clock that
  // was too far off are worth re-running; an image that names another device, a
  // release that is no newer, an off-host download URL and an image that cannot
  // fit the slot answer the same way every time.
  EXPECT_TRUE(otafailure::classify(OtaUpdater::HTTP_ERROR).allowRetry);
  EXPECT_TRUE(otafailure::classify(OtaUpdater::INTERNAL_UPDATE_ERROR).allowRetry);
  EXPECT_TRUE(otafailure::classify(OtaUpdater::OOM_ERROR).allowRetry);
  EXPECT_TRUE(otafailure::classify(OtaUpdater::JSON_PARSE_ERROR).allowRetry);
  EXPECT_TRUE(otafailure::classify(OtaUpdater::CORRUPT_IMAGE_ERROR).allowRetry);
  EXPECT_TRUE(otafailure::classify(OtaUpdater::TLS_ERROR).allowRetry);
  EXPECT_FALSE(otafailure::classify(OtaUpdater::WRONG_DEVICE_ERROR).allowRetry);
  EXPECT_FALSE(otafailure::classify(OtaUpdater::UPDATE_OLDER_ERROR).allowRetry);
  EXPECT_FALSE(otafailure::classify(OtaUpdater::UNTRUSTED_SOURCE_ERROR).allowRetry);
  EXPECT_FALSE(otafailure::classify(OtaUpdater::IMAGE_SIZE_ERROR).allowRetry);
}

TEST(OtaFailureClassify, NonFailuresAreNotRetryable) {
  EXPECT_EQ(otafailure::classify(OtaUpdater::OK).reason, otafailure::Reason::Generic);
  EXPECT_EQ(otafailure::classify(OtaUpdater::NO_UPDATE).reason, otafailure::Reason::Generic);
  EXPECT_FALSE(otafailure::classify(OtaUpdater::OK).allowRetry);
  EXPECT_FALSE(otafailure::classify(OtaUpdater::NO_UPDATE).allowRetry);
}

TEST(OtaFailureClassify, ReasonsAreDistinct) {
  // A shared reason would make two different failures show the same line.
  const OtaUpdater::OtaUpdaterError errors[] = {
      OtaUpdater::OOM_ERROR,      OtaUpdater::HTTP_ERROR,        OtaUpdater::JSON_PARSE_ERROR,
      OtaUpdater::WRONG_DEVICE_ERROR, OtaUpdater::UPDATE_OLDER_ERROR, OtaUpdater::INTERNAL_UPDATE_ERROR,
      OtaUpdater::UNTRUSTED_SOURCE_ERROR, OtaUpdater::IMAGE_SIZE_ERROR, OtaUpdater::CORRUPT_IMAGE_ERROR,
      OtaUpdater::TLS_ERROR};
  for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
    for (size_t j = i + 1; j < sizeof(errors) / sizeof(errors[0]); ++j) {
      EXPECT_NE(otafailure::classify(errors[i]).reason, otafailure::classify(errors[j]).reason)
          << "errors " << errors[i] << " and " << errors[j];
    }
  }
}