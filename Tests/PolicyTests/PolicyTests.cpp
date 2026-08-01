#include "../../IntelMKLFixup/IntelMKLFixupPolicy.hpp"

#include <assert.h>
#include <string.h>

namespace {

constexpr char FixturePath[] =
	"/Applications/Fixture.app/Contents/Frameworks/fixture.node";
constexpr uint8_t FixtureCdHash[20] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
	0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13
};
constexpr uint8_t WrongFixtureCdHash[20] = {
	0xFF, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
	0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13
};
constexpr uint8_t FixtureSearch[] = {
	0x53, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0x35, 0x61,
	0x0F, 0x79, 0x00, 0x85, 0xF6, 0x7C, 0x08, 0x89,
	0xF0, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3
};
constexpr uint8_t FixtureReplacement[] = {
	0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3
};
constexpr uint8_t FixtureContextBefore[] = {
	0x90, 0x90, 0x90, 0x90
};
constexpr uint8_t FixtureContextAfter[] = {
	0x33, 0xF6, 0x89, 0x74
};

bool matchFixturePath(const char *path, size_t length) {
	return length == sizeof(FixturePath) - 1 &&
		IMKLFX::bytesEqual(reinterpret_cast<const uint8_t *>(path),
			reinterpret_cast<const uint8_t *>(FixturePath), length);
}

const IMKLFX::ApplicationRule FixtureApplication {
	"fixture-native-module",
	"Generic policy fixture",
	"fixture-exact-path-v1",
	"fixture.node",
	sizeof("fixture.node") - 1,
	matchFixturePath,
	"fixture.signing.identifier",
	sizeof("fixture.signing.identifier") - 1,
	IMKLFX::TeamIdentifierPolicy::Exact,
	"ABCDE12345",
	sizeof("ABCDE12345") - 1,
	IMKLFX::CodeSigningPolicy::ValidRuntimeNotAdHoc
};

const IMKLFX::PatchDefinition FixturePatch {
	"fixture-mkl-implementation-x86_64-v1",
	"Generic policy fixture",
	"x86_64",
	FixtureSearch,
	sizeof(FixtureSearch),
	nullptr,
	0,
	FixtureReplacement,
	sizeof(FixtureReplacement),
	nullptr,
	0,
	FixtureContextBefore,
	sizeof(FixtureContextBefore),
	FixtureContextAfter,
	sizeof(FixtureContextAfter),
	IMKLFX::PatchValidationPolicy::ExactBytesAndContext,
	"host-test fixture",
	"exact bytes and context"
};

const IMKLFX::PatchDefinition *FixtureAllowedPatches[] = {
	&FixturePatch
};

const IMKLFX::ImageVariant FixtureVariant {
	"fixture-image-v1",
	&FixtureApplication,
	"1.0.0",
	"x86_64",
	FixtureCdHash,
	sizeof(FixtureCdHash),
	IMKLFX::MatchMode::StrictVariant,
	32,
	0,
	128,
	FixtureAllowedPatches,
	1,
	"Host-only generic fixture"
};

const IMKLFX::ImageIdentity FixtureIdentity {
	"fixture.signing.identifier",
	"ABCDE12345",
	FixtureCdHash,
	sizeof(FixtureCdHash),
	true,
	true,
	false
};

void putSupportedFunction(uint8_t *buffer, size_t size) {
	memset(buffer, 0, size);
	const auto &patch = FixturePatch;
	assert(FixtureVariant.targetFileOffset >= patch.contextBeforeSize);
	assert(FixtureVariant.targetFileOffset + patch.searchSize +
		patch.contextAfterSize <= size);
	memcpy(buffer + FixtureVariant.targetFileOffset - patch.contextBeforeSize,
		patch.contextBefore, patch.contextBeforeSize);
	memcpy(buffer + FixtureVariant.targetFileOffset, patch.search, patch.searchSize);
	memcpy(buffer + FixtureVariant.targetFileOffset + patch.searchSize,
		patch.contextAfter, patch.contextAfterSize);
}

void testGenericApplicationSelection() {
	const IMKLFX::ImageVariant *variants[] = {&FixtureVariant};
	auto selection = IMKLFX::selectImageVariant(variants, 1, FixturePath,
		sizeof(FixturePath) - 1, 0, 128, FixtureIdentity);
	assert(selection.state == IMKLFX::VariantMatchState::Approved);
	assert(selection.variant == &FixtureVariant);

	constexpr char WrongPath[] =
		"/Applications/Other.app/Contents/Frameworks/fixture.node";
	selection = IMKLFX::selectImageVariant(variants, 1, WrongPath,
		sizeof(WrongPath) - 1, 0, 128, FixtureIdentity);
	assert(selection.state == IMKLFX::VariantMatchState::NotCandidate);

	auto wrongIdentity = FixtureIdentity;
	wrongIdentity.codeDirectoryHash = WrongFixtureCdHash;
	selection = IMKLFX::selectImageVariant(variants, 1, FixturePath,
		sizeof(FixturePath) - 1, 0, 128, wrongIdentity);
	assert(selection.state == IMKLFX::VariantMatchState::CodeDirectoryHashRejected);

	wrongIdentity = FixtureIdentity;
	wrongIdentity.signingIdentifier = "other.signing.identifier";
	selection = IMKLFX::selectImageVariant(variants, 1, FixturePath,
		sizeof(FixturePath) - 1, 0, 128, wrongIdentity);
	assert(selection.state == IMKLFX::VariantMatchState::SigningIdentifierRejected);

	wrongIdentity = FixtureIdentity;
	wrongIdentity.teamIdentifier = "ZZZZZ99999";
	selection = IMKLFX::selectImageVariant(variants, 1, FixturePath,
		sizeof(FixturePath) - 1, 0, 128, wrongIdentity);
	assert(selection.state == IMKLFX::VariantMatchState::TeamIdentifierRejected);

	wrongIdentity = FixtureIdentity;
	wrongIdentity.codeValid = false;
	selection = IMKLFX::selectImageVariant(variants, 1, FixturePath,
		sizeof(FixturePath) - 1, 0, 128, wrongIdentity);
	assert(selection.state == IMKLFX::VariantMatchState::SigningPolicyRejected);
}

void testExactPatchAndMutation() {
	uint8_t buffer[128] {};
	putSupportedFunction(buffer, sizeof(buffer));
	auto selection = IMKLFX::selectStrictPatch(buffer, sizeof(buffer), 0,
		FixtureVariant);
	assert(selection.state == IMKLFX::TargetState::Original);
	assert(selection.patch == &FixturePatch);

	buffer[FixtureVariant.targetFileOffset + 7] ^= 1;
	selection = IMKLFX::selectStrictPatch(buffer, sizeof(buffer), 0,
		FixtureVariant);
	assert(selection.state == IMKLFX::TargetState::Mismatch);
}

void testTruncationAndEndOfBuffer() {
	uint8_t buffer[128] {};
	putSupportedFunction(buffer, sizeof(buffer));
	const auto &patch = FixturePatch;
	const size_t completeSize = static_cast<size_t>(FixtureVariant.targetFileOffset) +
		patch.searchSize + patch.contextAfterSize;
	assert(IMKLFX::selectStrictPatch(buffer, completeSize, 0, FixtureVariant).state ==
		IMKLFX::TargetState::Original);
	assert(IMKLFX::selectStrictPatch(buffer, completeSize - 1, 0, FixtureVariant).state ==
		IMKLFX::TargetState::NotCovered);
}

void testAlreadyPatchedAndReplacementLength() {
	uint8_t buffer[128] {};
	putSupportedFunction(buffer, sizeof(buffer));
	const auto &patch = FixturePatch;
	memcpy(buffer + FixtureVariant.targetFileOffset, patch.replacement,
		patch.replacementSize);
	assert(IMKLFX::selectStrictPatch(buffer, sizeof(buffer), 0, FixtureVariant).state ==
		IMKLFX::TargetState::AlreadyPatched);

	uint8_t oversizedReplacement[32] {};
	auto invalidPatch = patch;
	invalidPatch.replacement = oversizedReplacement;
	invalidPatch.replacementSize = sizeof(oversizedReplacement);
	assert(IMKLFX::classifyTarget(buffer, sizeof(buffer), 0, FixtureVariant,
		invalidPatch) == IMKLFX::TargetState::Mismatch);

	invalidPatch = patch;
	invalidPatch.architecture = "arm64";
	assert(IMKLFX::classifyTarget(buffer, sizeof(buffer), 0, FixtureVariant,
		invalidPatch) == IMKLFX::TargetState::Mismatch);
}

void testMultipleMatchesAreRejected() {
	uint8_t buffer[128] {};
	putSupportedFunction(buffer, sizeof(buffer));
	const IMKLFX::PatchDefinition *duplicates[] = {
		&FixturePatch,
		&FixturePatch
	};
	auto ambiguousVariant = FixtureVariant;
	ambiguousVariant.allowedPatches = duplicates;
	ambiguousVariant.allowedPatchCount = 2;
	assert(IMKLFX::selectStrictPatch(buffer, sizeof(buffer), 0,
		ambiguousVariant).state == IMKLFX::TargetState::Ambiguous);
}

void testReviewedSearchIsExplicitlyDisabled() {
	auto searchVariant = FixtureVariant;
	searchVariant.matchMode = IMKLFX::MatchMode::ReviewedSearch;
	const IMKLFX::ImageVariant *variants[] = {&searchVariant};
	const auto selection = IMKLFX::selectImageVariant(variants, 1, FixturePath,
		sizeof(FixturePath) - 1, 0, 128, FixtureIdentity);
	assert(selection.state == IMKLFX::VariantMatchState::ModeDisabled);
	assert(!IMKLFX::catalogueMayTargetRange(variants, 1, 0, 128));
}

} // namespace

int main() {
	testGenericApplicationSelection();
	testExactPatchAndMutation();
	testTruncationAndEndOfBuffer();
	testAlreadyPatchedAndReplacementLength();
	testMultipleMatchesAreRejected();
	testReviewedSearchIsExplicitlyDisabled();
	return 0;
}
