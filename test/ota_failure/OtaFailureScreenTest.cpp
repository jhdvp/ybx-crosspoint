#include <gtest/gtest.h>

#include "util/OtaFailureScreen.h"

namespace {

constexpr freeink::ui::Rect kBody{16, 100, 448, 500};

bool fitsInside(const freeink::ui::Rect rect, const freeink::ui::Rect outer) {
  return rect.x >= outer.x && rect.y >= outer.y && rect.width > 0 && rect.height > 0 && rect.right() <= outer.right() &&
         rect.bottom() <= outer.bottom();
}

TEST(OtaFailureClassify, EveryErrorCodeNamesAReason) {
  EXPECT_EQ(otafailure::classify(OtaUpdater::OOM_ERROR).reason, otafailure::Reason::Memory);
  EXPECT_EQ(otafailure::classify(OtaUpdater::HTTP_ERROR).reason, otafailure::Reason::Network);
  EXPECT_EQ(otafailure::classify(OtaUpdater::JSON_PARSE_ERROR).reason, otafailure::Reason::Server);
  EXPECT_EQ(otafailure::classify(OtaUpdater::WRONG_DEVICE_ERROR).reason, otafailure::Reason::WrongDevice);
  EXPECT_EQ(otafailure::classify(OtaUpdater::UPDATE_OLDER_ERROR).reason, otafailure::Reason::NoUpdate);
  EXPECT_EQ(otafailure::classify(OtaUpdater::INTERNAL_UPDATE_ERROR).reason, otafailure::Reason::FlashWrite);
}

TEST(OtaFailureClassify, OnlyFailuresThatCanRepeatOfferRetry) {
  // A truncated download, a refused flash write or a memory error are worth
  // re-running; the image that names another device, and a release that is no
  // newer, answer the same way every time.
  EXPECT_TRUE(otafailure::classify(OtaUpdater::HTTP_ERROR).allowRetry);
  EXPECT_TRUE(otafailure::classify(OtaUpdater::INTERNAL_UPDATE_ERROR).allowRetry);
  EXPECT_TRUE(otafailure::classify(OtaUpdater::OOM_ERROR).allowRetry);
  EXPECT_TRUE(otafailure::classify(OtaUpdater::JSON_PARSE_ERROR).allowRetry);
  EXPECT_FALSE(otafailure::classify(OtaUpdater::WRONG_DEVICE_ERROR).allowRetry);
  EXPECT_FALSE(otafailure::classify(OtaUpdater::UPDATE_OLDER_ERROR).allowRetry);
}

TEST(OtaFailureClassify, NonFailuresAreNotRetryable) {
  EXPECT_FALSE(otafailure::classify(OtaUpdater::OK).allowRetry);
  EXPECT_FALSE(otafailure::classify(OtaUpdater::NO_UPDATE).allowRetry);
}

TEST(OtaFailureLayout, PairSplitsTheBandAndKeepsTheGap) {
  const auto buttons = otafailure::layout(kBody, 8, 44, true);
  ASSERT_TRUE(buttons.valid);
  ASSERT_TRUE(buttons.hasRetry);
  EXPECT_EQ(buttons.back.width, buttons.retry.width);
  EXPECT_TRUE(fitsInside(buttons.back, kBody));
  EXPECT_TRUE(fitsInside(buttons.retry, kBody));
  // No overlap: a tap between the controls belongs to neither.
  EXPECT_GT(buttons.retry.x - buttons.back.right(), 0);
  EXPECT_EQ(buttons.back.height, 44);
}

TEST(OtaFailureLayout, BackAloneIsCentred) {
  const auto buttons = otafailure::layout(kBody, 8, 44, false);
  ASSERT_TRUE(buttons.valid);
  EXPECT_FALSE(buttons.hasRetry);
  EXPECT_TRUE(fitsInside(buttons.back, kBody));
  EXPECT_EQ(buttons.back.width, kBody.width / 2);
  EXPECT_EQ(buttons.back.x, kBody.x + (kBody.width - buttons.back.width) / 2);
}

TEST(OtaFailureLayout, ControlHeightStaysInsideTheBand) {
  // A short landscape band leaves less room than one themed control: the row
  // shrinks instead of reaching past the band, where the hint bar is drawn.
  const freeink::ui::Rect shortBody{0, 300, 480, 30};
  const auto buttons = otafailure::layout(shortBody, 8, 44, true);
  ASSERT_TRUE(buttons.valid);
  EXPECT_TRUE(fitsInside(buttons.back, shortBody));
  EXPECT_TRUE(fitsInside(buttons.retry, shortBody));
  EXPECT_LE(buttons.back.bottom(), shortBody.bottom());
}

TEST(OtaFailureLayout, NarrowBandStillLeavesPositiveRects) {
  // The gap must not eat the controls: it shrinks to a third of the band.
  const freeink::ui::Rect narrow{0, 0, 9, 100};
  const auto buttons = otafailure::layout(narrow, 16, 44, true);
  ASSERT_TRUE(buttons.valid);
  EXPECT_GE(buttons.back.width, 1);
  EXPECT_GE(buttons.retry.width, 1);
  EXPECT_TRUE(fitsInside(buttons.back, narrow));
  EXPECT_TRUE(fitsInside(buttons.retry, narrow));
  EXPECT_GE(buttons.retry.x, buttons.back.right());
}

TEST(OtaFailureLayout, EmptyBandDrawsNothing) {
  const freeink::ui::Rect noWidth{0, 0, 0, 200};
  EXPECT_FALSE(otafailure::layout(noWidth, 8, 44, true).valid);

  const freeink::ui::Rect noHeight{0, 0, 480, 0};
  EXPECT_FALSE(otafailure::layout(noHeight, 8, 44, true).valid);
  EXPECT_FALSE(otafailure::layout(noHeight, 8, 44, false).valid);
}

}  // namespace
