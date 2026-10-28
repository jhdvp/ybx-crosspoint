#include <gtest/gtest.h>

#include <string>

#include "util/YbxKosyncDecision.h"

using ybxkosync::Action;
using ybxkosync::decideAction;
using ybxkosync::deriveStatus;
using ybxkosync::describeAge;
using ybxkosync::Outcome;
using ybxkosync::ProvisionState;
using ybxkosync::remoteJumpNeedsConsent;
using ybxkosync::Status;
using ybxkosync::ybxDerivedCredentialsRemain;

namespace {
// A card that carries a readable key and a settings file with nothing in it: the
// state a freshly paired reader starts from.
ProvisionState paired() {
  ProvisionState s;
  s.enabled = true;
  s.pluginInstalled = true;
  s.tokenPresent = true;
  return s;
}

ProvisionState withManualServer() {
  ProvisionState s = paired();
  s.credentialsPresent = true;
  return s;
}

// YBX wrote the settings; `keyMatches` says the card key and the password on file
// are both still the ones written (true) or both stale (false, a rotated key).
ProvisionState configuredByYbx(const bool keyMatches) {
  ProvisionState s = paired();
  s.credentialsPresent = true;
  s.provisioned = true;
  s.pointsAtYbx = true;
  s.keyFpMatches = keyMatches;
  s.credentialCarriesKey = keyMatches;
  return s;
}
}  // namespace

TEST(YbxKosyncDecision, AppliesTheBridgeWhenNothingIsConfigured) { EXPECT_EQ(decideAction(paired()), Action::Apply); }

TEST(YbxKosyncDecision, LeavesAHandConfiguredServerAloneUntilAsked) {
  EXPECT_EQ(decideAction(withManualServer()), Action::KeepManual);
}

TEST(YbxKosyncDecision, AdoptsTheConfigurationTheBrowserPageWrote) {
  ProvisionState s = withManualServer();
  s.pointsAtYbx = true;
  s.credentialCarriesKey = true;
  EXPECT_EQ(decideAction(s), Action::Adopt);
  // Same target, stale key: write the current one instead of adopting it.
  s.credentialCarriesKey = false;
  EXPECT_EQ(decideAction(s), Action::Apply);
}

TEST(YbxKosyncDecision, ReappliesARotatedKeyAndStopsWhenIdle) {
  EXPECT_EQ(decideAction(configuredByYbx(false)), Action::Apply);
  EXPECT_EQ(decideAction(configuredByYbx(true)), Action::None);
}

TEST(YbxKosyncDecision, GivesTheServerBackWhenTheUserTypedOverIt) {
  ProvisionState s = configuredByYbx(true);
  s.pointsAtYbx = false;
  EXPECT_EQ(decideAction(s), Action::Abandon);
}

TEST(YbxKosyncDecision, GivesTheServerBackWhenOnlyThePasswordWasEdited) {
  // URL still the bridge and the card key unchanged, but the password on file is
  // other text: a hand-edit of the password row, not a rotation. It must survive.
  ProvisionState s = configuredByYbx(true);
  s.credentialCarriesKey = false;
  EXPECT_EQ(decideAction(s), Action::Abandon);
  // A rotated key differs in the fingerprint instead: re-apply over it.
  s.keyFpMatches = false;
  EXPECT_EQ(decideAction(s), Action::Apply);
  // The pass after that Abandon: unprovisioned, but the fingerprint it kept still
  // matches, so the hand-typed password is not rewritten again.
  s.provisioned = false;
  s.keyFpMatches = true;
  EXPECT_EQ(decideAction(s), Action::KeepManual);
  // Only a rotated key after the hand-edit makes YBX write over it again.
  s.keyFpMatches = false;
  EXPECT_EQ(decideAction(s), Action::Apply);
}

TEST(YbxKosyncDecision, AbandonDropsTheYbxKeyFromAHandTypedServer) {
  const std::string key = "cp_dummyreaderkeydummyreaderkeydummyre";
  // YBX provisioned, the user retyped only the URL: the derived pair is still on
  // file and must be cleared with the Abandon.
  EXPECT_TRUE(ybxDerivedCredentialsRemain("ybx", key, "ybx", key));
  // The user replaced the password too: what is on file belongs to them.
  EXPECT_FALSE(ybxDerivedCredentialsRemain("ybx", "hand-typed", "ybx", key));
  // A different username says the pair is theirs even if the text matches.
  EXPECT_FALSE(ybxDerivedCredentialsRemain("me", key, "ybx", key));
  // No key readable on the card: never clear on a guess.
  EXPECT_FALSE(ybxDerivedCredentialsRemain("ybx", "", "ybx", ""));
}

TEST(YbxKosyncDecision, OptingOutClearsOnlyWhatYbxWrote) {
  ProvisionState off = configuredByYbx(true);
  off.enabled = false;
  EXPECT_EQ(decideAction(off), Action::Clear);

  // A manual server survives the switch: turning YBX off must not erase it.
  off.pointsAtYbx = false;
  off.provisioned = false;
  EXPECT_EQ(decideAction(off), Action::None);
}

TEST(YbxKosyncDecision, RemovesTheBridgeWhenThePluginLeavesTheCard) {
  ProvisionState s = configuredByYbx(true);
  s.pluginInstalled = false;
  EXPECT_EQ(decideAction(s), Action::Forget);
  // Uninstalling before it was ever provisioned changes nothing.
  s = withManualServer();
  s.pluginInstalled = false;
  EXPECT_EQ(decideAction(s), Action::None);
}

TEST(YbxKosyncDecision, AKeyStillBeingWrittenKeepsTheLastConfiguration) {
  ProvisionState s = paired();
  s.tokenPresent = false;
  EXPECT_EQ(decideAction(s), Action::None);
  s.provisioned = true;
  s.pointsAtYbx = true;
  EXPECT_EQ(decideAction(s), Action::None);
}

TEST(YbxKosyncDecision, ConsentStartsAboveOnePercentagePoint) {
  EXPECT_FALSE(remoteJumpNeedsConsent(0.10f, 0.10f));
  EXPECT_FALSE(remoteJumpNeedsConsent(0.10f, 0.105f));  // within the accepted gap
  EXPECT_TRUE(remoteJumpNeedsConsent(0.10f, 0.12f));
  EXPECT_FALSE(remoteJumpNeedsConsent(0.90f, 0.10f));  // behind: never asks
  EXPECT_FALSE(remoteJumpNeedsConsent(0.10f, NAN));    // unmapped position
  EXPECT_FALSE(remoteJumpNeedsConsent(NAN, 0.10f));
}

TEST(YbxKosyncDecision, StatusReportsWhatMustBeFixedFirst) {
  // Not paired outranks a stale verdict; a refused key outranks an old mismatch.
  EXPECT_EQ(deriveStatus(false, true, false, false, Outcome::Pushed), Status::Disabled);
  EXPECT_EQ(deriveStatus(true, false, false, true, Outcome::Pushed), Status::NotPaired);
  EXPECT_EQ(deriveStatus(true, true, true, true, Outcome::Pushed), Status::ManualServer);
  EXPECT_EQ(deriveStatus(true, true, false, true, Outcome::AuthRejected), Status::AuthRejected);
  EXPECT_EQ(deriveStatus(true, true, false, true, Outcome::Already), Status::Mismatch);
  EXPECT_EQ(deriveStatus(true, true, false, false, Outcome::Already), Status::Already);
  EXPECT_EQ(deriveStatus(true, true, false, false, Outcome::None), Status::NeverSynced);
  EXPECT_EQ(deriveStatus(true, true, false, false, Outcome::Offline), Status::Offline);
}

TEST(YbxKosyncDecision, AgeBucketsSwitchAtHourAndDayLimits) {
  int value = -1;
  int unit = -1;
  const int64_t base = 1700000000;
  EXPECT_FALSE(describeAge(0, base, value, unit));
  EXPECT_FALSE(describeAge(base, base - 1, value, unit));  // clock behind the record
  EXPECT_TRUE(describeAge(base, base + 59, value, unit));
  EXPECT_EQ(unit, 0);
  EXPECT_TRUE(describeAge(base, base + 3600, value, unit));
  EXPECT_EQ(unit, 1);
  EXPECT_TRUE(describeAge(base, base + 47 * 3600, value, unit));
  EXPECT_EQ(unit, 1);
  EXPECT_TRUE(describeAge(base, base + 48 * 3600, value, unit));
  EXPECT_EQ(unit, 2);
}
