#include "../../IntelMKLFixup/IntelMKLFixupCatalogue.hpp"

#include <assert.h>
#include <string.h>

namespace {

constexpr char DiscordCurrentPath[] =
	"/Users/test/Library/Application Support/discord/app-0.0.403/modules/"
	"discord_krisp-1/discord_krisp/discord_krisp.node";
constexpr char DiscordChangedVersionPath[] =
	"/Users/test/Library/Application Support/discord/app-99.42.7/modules/"
	"discord_krisp-812/discord_krisp.node";
constexpr char RejectedChannelPath[] =
	"/Users/test/Library/Application Support/discordcanary/app-0.0.403/modules/"
	"discord_krisp-1/discord_krisp/discord_krisp.node";
constexpr char RejectedBasenamePath[] =
	"/Users/test/Library/Application Support/discord/app-0.0.403/modules/"
	"discord_krisp-1/discord_krisp/other.node";
constexpr char MalformedPath[] =
	"/Users/test/Library/Application Support/discord/app-latest/modules/"
	"discord_krisp-1/discord_krisp.node";
constexpr char NonWhitelistedPath[] =
	"/Applications/Other.app/Contents/MacOS/discord_krisp.node";
constexpr uint8_t ArbitraryChangedCdHash[20] = {
	0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9,
	0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB0, 0xB1, 0xB2, 0xB3
};
constexpr IMKLFX::RuntimePolicyControls WindowOff {false};
constexpr IMKLFX::RuntimePolicyControls WindowOn {true};

const IMKLFX::ImageIdentity StrictIdentity {
	"discord_krisp", "53Q6R32WPB", IMKLFX::DiscordStable00403KrispCdHash,
	sizeof(IMKLFX::DiscordStable00403KrispCdHash), true, true, false
};

const IMKLFX::ImageIdentity ChangedIdentity {
	"discord_krisp", "53Q6R32WPB", ArbitraryChangedCdHash,
	sizeof(ArbitraryChangedCdHash), true, true, false
};

void putMklFunction(uint8_t *page, size_t targetInPage) {
	const auto &patch = IMKLFX::MklServIntelCpuTrueOneApiBuild20201104X8664;
	assert(targetInPage >= patch.contextBeforeSize);
	assert(targetInPage + patch.searchSize + patch.contextAfterSize <=
		IMKLFX::X8664ValidationPageSize);
	memcpy(page + targetInPage - patch.contextBeforeSize,
		patch.contextBefore, patch.contextBeforeSize);
	memcpy(page + targetInPage, patch.search, patch.searchSize);
	memcpy(page + targetInPage + patch.searchSize,
		patch.contextAfter, patch.contextAfterSize);
}

void testDiscordPathGrammar() {
	assert(IMKLFX::matchApplicationRule(IMKLFX::DiscordStableKrispApplication,
		DiscordCurrentPath, sizeof(DiscordCurrentPath) - 1));
	assert(IMKLFX::matchApplicationRule(IMKLFX::DiscordStableKrispApplication,
		DiscordChangedVersionPath, sizeof(DiscordChangedVersionPath) - 1));
	assert(!IMKLFX::matchApplicationRule(IMKLFX::DiscordStableKrispApplication,
		RejectedChannelPath, sizeof(RejectedChannelPath) - 1));
	assert(!IMKLFX::matchApplicationRule(IMKLFX::DiscordStableKrispApplication,
		RejectedBasenamePath, sizeof(RejectedBasenamePath) - 1));
	assert(!IMKLFX::matchApplicationRule(IMKLFX::DiscordStableKrispApplication,
		MalformedPath, sizeof(MalformedPath) - 1));
	assert(!IMKLFX::matchApplicationRule(IMKLFX::DiscordStableKrispApplication,
		NonWhitelistedPath, sizeof(NonWhitelistedPath) - 1));
}

void testStrictFixtureIsRetained() {
	assert(IMKLFX::DiscordStable00403KrispX8664.matchMode ==
		IMKLFX::MatchMode::StrictVariant);
	assert(IMKLFX::DiscordStable00403KrispX8664.allowedPatches[0] ==
		&IMKLFX::MklServIntelCpuTrueOneApiBuild20201104X8664);
	assert(IMKLFX::DiscordStable00403KrispX8664.codeDirectoryHashSize == 20);
	assert(IMKLFX::DiscordStable00403KrispX8664.targetFileOffset == 0x650100);
}

void testStrictPolicyPrecedesWindowWhenBothApprove() {
	const IMKLFX::ImageVariant *variants[] = {
		&IMKLFX::DiscordStable00403KrispX8664,
		&IMKLFX::DiscordStableKrispBoundedWindowX8664
	};
	const auto selection = IMKLFX::selectImageVariant(variants, 2,
		DiscordCurrentPath, sizeof(DiscordCurrentPath) - 1,
		0x650000, IMKLFX::X8664ValidationPageSize, StrictIdentity, WindowOn);
	assert(selection.state == IMKLFX::VariantMatchState::Approved);
	assert(selection.variant == &IMKLFX::DiscordStable00403KrispX8664);
}

void testChangedVersionCdHashAndOffsetUseWindow() {
	const IMKLFX::ImageVariant *variants[] = {
		&IMKLFX::DiscordStable00403KrispX8664,
		&IMKLFX::DiscordStableKrispBoundedWindowX8664
	};
	auto selection = IMKLFX::selectImageVariant(variants, 2,
		DiscordChangedVersionPath, sizeof(DiscordChangedVersionPath) - 1,
		0x650000, IMKLFX::X8664ValidationPageSize, ChangedIdentity, WindowOn);
	assert(selection.state == IMKLFX::VariantMatchState::Approved);
	assert(selection.variant == &IMKLFX::DiscordStableKrispBoundedWindowX8664);
	assert(selection.variant->applicationVersion == nullptr);
	assert(selection.variant->codeDirectoryHash == nullptr);

	uint8_t page[IMKLFX::X8664ValidationPageSize] {};
	constexpr size_t ChangedTargetInPage = 0x280;
	putMklFunction(page, ChangedTargetInPage);
	const auto patch = IMKLFX::selectBoundedWindowPatch(page, sizeof(page),
		0x650000, *selection.variant, true);
	assert(patch.state == IMKLFX::TargetState::Original);
	assert(patch.patch == &IMKLFX::MklServIntelCpuTrueOneApiBuild20201104X8664);
	assert(patch.targetFileOffset == 0x650000 + ChangedTargetInPage);
}

void testWindowBootGateAndIdentityRejections() {
	const IMKLFX::ImageVariant *window[] = {
		&IMKLFX::DiscordStableKrispBoundedWindowX8664
	};
	auto selection = IMKLFX::selectImageVariant(window, 1,
		DiscordChangedVersionPath, sizeof(DiscordChangedVersionPath) - 1,
		0x650000, IMKLFX::X8664ValidationPageSize, ChangedIdentity, WindowOff);
	assert(selection.state == IMKLFX::VariantMatchState::ModeDisabled);

	auto wrong = ChangedIdentity;
	wrong.signingIdentifier = "not.discord_krisp";
	selection = IMKLFX::selectImageVariant(window, 1,
		DiscordChangedVersionPath, sizeof(DiscordChangedVersionPath) - 1,
		0x650000, IMKLFX::X8664ValidationPageSize, wrong, WindowOn);
	assert(selection.state == IMKLFX::VariantMatchState::SigningIdentifierRejected);

	wrong = ChangedIdentity;
	wrong.teamIdentifier = "AAAAAAAAAA";
	selection = IMKLFX::selectImageVariant(window, 1,
		DiscordChangedVersionPath, sizeof(DiscordChangedVersionPath) - 1,
		0x650000, IMKLFX::X8664ValidationPageSize, wrong, WindowOn);
	assert(selection.state == IMKLFX::VariantMatchState::TeamIdentifierRejected);

	wrong = ChangedIdentity;
	wrong.runtimeSigned = false;
	selection = IMKLFX::selectImageVariant(window, 1,
		DiscordChangedVersionPath, sizeof(DiscordChangedVersionPath) - 1,
		0x650000, IMKLFX::X8664ValidationPageSize, wrong, WindowOn);
	assert(selection.state == IMKLFX::VariantMatchState::SigningPolicyRejected);

	selection = IMKLFX::selectImageVariant(window, 1,
		NonWhitelistedPath, sizeof(NonWhitelistedPath) - 1,
		0x650000, IMKLFX::X8664ValidationPageSize, ChangedIdentity, WindowOn);
	assert(selection.state == IMKLFX::VariantMatchState::NotCandidate);
}

void testTargetOutsideWindowAndNonWhitelistBytesReject() {
	uint8_t page[IMKLFX::X8664ValidationPageSize] {};
	putMklFunction(page, 0x200);
	assert(IMKLFX::selectBoundedWindowPatch(page, sizeof(page), 0x651000,
		IMKLFX::DiscordStableKrispBoundedWindowX8664, true).state ==
		IMKLFX::TargetState::NotCovered);

	const IMKLFX::ImageVariant *window[] = {
		&IMKLFX::DiscordStableKrispBoundedWindowX8664
	};
	assert(IMKLFX::selectImageVariant(window, 1, NonWhitelistedPath,
		sizeof(NonWhitelistedPath) - 1, 0x650000,
		IMKLFX::X8664ValidationPageSize, ChangedIdentity, WindowOn).state ==
		IMKLFX::VariantMatchState::NotCandidate);
}

} // namespace

int main() {
	testDiscordPathGrammar();
	testStrictFixtureIsRetained();
	testStrictPolicyPrecedesWindowWhenBothApprove();
	testChangedVersionCdHashAndOffsetUseWindow();
	testWindowBootGateAndIdentityRejections();
	testTargetOutsideWindowAndNonWhitelistBytesReject();
	return 0;
}
