#include "../../IntelMKLFixup/IntelMKLFixupPolicy.hpp"

#include <assert.h>
#include <string.h>

namespace {

constexpr char FixturePath[] =
	"/Applications/Fixture.app/Contents/Frameworks/fixture.node";
constexpr char SecondFixturePath[] =
	"/Applications/Second.app/Contents/Frameworks/second.node";
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
constexpr uint8_t FixtureContextBefore[] = {0x90, 0x90, 0x90, 0x90};
constexpr uint8_t FixtureContextAfter[] = {0x33, 0xF6, 0x89, 0x74};
constexpr IMKLFX::RuntimePolicyControls WindowOff {false};
constexpr IMKLFX::RuntimePolicyControls WindowOn {true};

bool matchFixturePath(const char *path, size_t length) {
	return length == sizeof(FixturePath) - 1 &&
		IMKLFX::bytesEqual(reinterpret_cast<const uint8_t *>(path),
			reinterpret_cast<const uint8_t *>(FixturePath), length);
}

bool matchSecondFixturePath(const char *path, size_t length) {
	return length == sizeof(SecondFixturePath) - 1 &&
		IMKLFX::bytesEqual(reinterpret_cast<const uint8_t *>(path),
			reinterpret_cast<const uint8_t *>(SecondFixturePath), length);
}

const IMKLFX::ApplicationRule FixtureApplication {
	"fixture-native-module", "Generic policy fixture", "fixture-exact-path-v1",
	"fixture.node", sizeof("fixture.node") - 1, matchFixturePath,
	"fixture.signing.identifier", sizeof("fixture.signing.identifier") - 1,
	IMKLFX::TeamIdentifierPolicy::Exact, "ABCDE12345", sizeof("ABCDE12345") - 1,
	IMKLFX::CodeSigningPolicy::ValidRuntimeNotAdHoc
};

const IMKLFX::ApplicationRule SecondFixtureApplication {
	"second-native-module", "Second generic policy fixture", "second-exact-path-v1",
	"second.node", sizeof("second.node") - 1, matchSecondFixturePath,
	"second.signing.identifier", sizeof("second.signing.identifier") - 1,
	IMKLFX::TeamIdentifierPolicy::Exact, "ZYXWV98765", sizeof("ZYXWV98765") - 1,
	IMKLFX::CodeSigningPolicy::ValidRuntimeNotAdHoc
};

const IMKLFX::PatchDefinition FixturePatch {
	"fixture-mkl-implementation-x86_64-v1", "Generic MKL policy fixture", "x86_64",
	FixtureSearch, sizeof(FixtureSearch), nullptr, 0,
	FixtureReplacement, sizeof(FixtureReplacement), nullptr, 0,
	FixtureContextBefore, sizeof(FixtureContextBefore),
	FixtureContextAfter, sizeof(FixtureContextAfter),
	IMKLFX::PatchValidationPolicy::ExactBytesAndContext,
	"host-test fixture", "exact bytes and context"
};

const IMKLFX::PatchDefinition *FixtureAllowedPatches[] = {&FixturePatch};

const IMKLFX::ImageVariant FixtureStrictVariant {
	"fixture-strict-v1", &FixtureApplication, "1.0.0", "x86_64",
	FixtureCdHash, sizeof(FixtureCdHash), IMKLFX::MatchMode::StrictVariant,
	32, 0, 128, 0, 0, FixtureAllowedPatches, 1, "Host strict fixture"
};

const IMKLFX::ImageVariant FixtureWindowVariant {
	"fixture-window-v1", &FixtureApplication, nullptr, "x86_64",
	nullptr, 0, IMKLFX::MatchMode::BoundedWindow,
	0, 0, IMKLFX::X8664ValidationPageSize,
	0, IMKLFX::X8664ValidationPageSize,
	FixtureAllowedPatches, 1, "Host bounded-window fixture"
};

const IMKLFX::ImageVariant SecondWindowVariant {
	"second-window-v1", &SecondFixtureApplication, nullptr, "x86_64",
	nullptr, 0, IMKLFX::MatchMode::BoundedWindow,
	0, 0, IMKLFX::X8664ValidationPageSize,
	0, IMKLFX::X8664ValidationPageSize,
	FixtureAllowedPatches, 1, "Second application reusing the same patch"
};

const IMKLFX::ImageIdentity FixtureIdentity {
	"fixture.signing.identifier", "ABCDE12345", FixtureCdHash,
	sizeof(FixtureCdHash), true, true, false
};

void putSupportedFunction(uint8_t *buffer, size_t size, size_t target) {
	assert(target >= FixturePatch.contextBeforeSize);
	assert(target <= size);
	assert(FixturePatch.searchSize <= size - target);
	assert(FixturePatch.contextAfterSize <= size - target - FixturePatch.searchSize);
	memcpy(buffer + target - FixturePatch.contextBeforeSize,
		FixturePatch.contextBefore, FixturePatch.contextBeforeSize);
	memcpy(buffer + target, FixturePatch.search, FixturePatch.searchSize);
	memcpy(buffer + target + FixturePatch.searchSize,
		FixturePatch.contextAfter, FixturePatch.contextAfterSize);
}

IMKLFX::PatchSelection search(const uint8_t *buffer, size_t size,
	const IMKLFX::ImageVariant &variant = FixtureWindowVariant, bool enabled = true) {
	return IMKLFX::selectBoundedWindowPatch(buffer, size, 0, variant, enabled);
}

void testGenericApplicationSelection() {
	const IMKLFX::ImageVariant *variants[] = {&FixtureStrictVariant};
	auto selection = IMKLFX::selectImageVariant(variants, 1, FixturePath,
		sizeof(FixturePath) - 1, 0, 128, FixtureIdentity, WindowOff);
	assert(selection.state == IMKLFX::VariantMatchState::Approved);
	assert(selection.variant == &FixtureStrictVariant);

	constexpr char WrongPath[] =
		"/Applications/Other.app/Contents/Frameworks/fixture.node";
	selection = IMKLFX::selectImageVariant(variants, 1, WrongPath,
		sizeof(WrongPath) - 1, 0, 128, FixtureIdentity, WindowOff);
	assert(selection.state == IMKLFX::VariantMatchState::NotCandidate);

	auto wrongIdentity = FixtureIdentity;
	wrongIdentity.codeDirectoryHash = WrongFixtureCdHash;
	selection = IMKLFX::selectImageVariant(variants, 1, FixturePath,
		sizeof(FixturePath) - 1, 0, 128, wrongIdentity, WindowOff);
	assert(selection.state == IMKLFX::VariantMatchState::CodeDirectoryHashRejected);

	wrongIdentity = FixtureIdentity;
	wrongIdentity.signingIdentifier = "other.signing.identifier";
	selection = IMKLFX::selectImageVariant(variants, 1, FixturePath,
		sizeof(FixturePath) - 1, 0, 128, wrongIdentity, WindowOff);
	assert(selection.state == IMKLFX::VariantMatchState::SigningIdentifierRejected);

	wrongIdentity = FixtureIdentity;
	wrongIdentity.teamIdentifier = "ZZZZZ99999";
	selection = IMKLFX::selectImageVariant(variants, 1, FixturePath,
		sizeof(FixturePath) - 1, 0, 128, wrongIdentity, WindowOff);
	assert(selection.state == IMKLFX::VariantMatchState::TeamIdentifierRejected);
}

void testStrictModeRemainsExactAndNeverSearches() {
	uint8_t buffer[128] {};
	putSupportedFunction(buffer, sizeof(buffer), FixtureStrictVariant.targetFileOffset);
	auto selection = IMKLFX::selectPolicyPatch(buffer, sizeof(buffer), 0,
		FixtureStrictVariant, WindowOn);
	assert(selection.state == IMKLFX::TargetState::Original);
	assert(selection.targetFileOffset == FixtureStrictVariant.targetFileOffset);

	memset(buffer, 0, sizeof(buffer));
	putSupportedFunction(buffer, sizeof(buffer), 64);
	selection = IMKLFX::selectPolicyPatch(buffer, sizeof(buffer), 0,
		FixtureStrictVariant, WindowOn);
	assert(selection.state == IMKLFX::TargetState::Mismatch);

	memset(buffer, 0, sizeof(buffer));
	putSupportedFunction(buffer, sizeof(buffer), FixtureStrictVariant.targetFileOffset);
	buffer[FixtureStrictVariant.targetFileOffset] ^= 1;
	assert(IMKLFX::selectStrictPatch(buffer, sizeof(buffer), 0,
		FixtureStrictVariant).state == IMKLFX::TargetState::Mismatch);

	buffer[FixtureStrictVariant.targetFileOffset] ^= 1;
	memcpy(buffer + FixtureStrictVariant.targetFileOffset, FixturePatch.replacement,
		FixturePatch.replacementSize);
	assert(IMKLFX::selectStrictPatch(buffer, sizeof(buffer), 0,
		FixtureStrictVariant).state == IMKLFX::TargetState::AlreadyPatched);

	const IMKLFX::ImageVariant *variants[] = {&FixtureStrictVariant};
	assert(IMKLFX::selectImageVariant(variants, 1, FixturePath,
		sizeof(FixturePath) - 1, 128, 128, FixtureIdentity, WindowOn).state ==
		IMKLFX::VariantMatchState::NotCandidate);
}

void testWindowZeroUniqueTwoAndMany() {
	uint8_t buffer[IMKLFX::X8664ValidationPageSize] {};
	assert(search(buffer, sizeof(buffer)).state == IMKLFX::TargetState::Mismatch);

	putSupportedFunction(buffer, sizeof(buffer), 64);
	auto selection = search(buffer, sizeof(buffer));
	assert(selection.state == IMKLFX::TargetState::Original);
	assert(selection.patch == &FixturePatch);
	assert(selection.targetFileOffset == 64);
	assert(selection.matchCount == 1);

	putSupportedFunction(buffer, sizeof(buffer), 256);
	selection = search(buffer, sizeof(buffer));
	assert(selection.state == IMKLFX::TargetState::Ambiguous);
	assert(selection.matchCount == 2);

	putSupportedFunction(buffer, sizeof(buffer), 512);
	assert(search(buffer, sizeof(buffer)).state == IMKLFX::TargetState::Ambiguous);
}

void testWindowBoundaryPositionsAndTruncation() {
	uint8_t buffer[IMKLFX::X8664ValidationPageSize] {};
	const size_t first = FixturePatch.contextBeforeSize;
	const size_t last = sizeof(buffer) - FixturePatch.searchSize -
		FixturePatch.contextAfterSize;
	putSupportedFunction(buffer, sizeof(buffer), first);
	assert(search(buffer, sizeof(buffer)).targetFileOffset == first);

	memset(buffer, 0, sizeof(buffer));
	putSupportedFunction(buffer, sizeof(buffer), last);
	assert(search(buffer, sizeof(buffer)).targetFileOffset == last);
	assert(search(buffer, sizeof(buffer) - 1).state == IMKLFX::TargetState::NotCovered);
	for (size_t size = 0; size < sizeof(buffer); size++) {
		assert(search(buffer, size).state == IMKLFX::TargetState::NotCovered);
	}
	assert(search(nullptr, sizeof(buffer)).state == IMKLFX::TargetState::InvalidPolicy);
	assert(search(buffer, 0).state == IMKLFX::TargetState::NotCovered);

	memset(buffer, 0, sizeof(buffer));
	memcpy(buffer, FixturePatch.search, FixturePatch.searchSize);
	assert(search(buffer, sizeof(buffer)).state == IMKLFX::TargetState::Mismatch);
	memset(buffer, 0, sizeof(buffer));
	memcpy(buffer + sizeof(buffer) - FixturePatch.searchSize,
		FixturePatch.search, FixturePatch.searchSize);
	assert(search(buffer, sizeof(buffer)).state == IMKLFX::TargetState::Mismatch);
	memset(buffer, 0, sizeof(buffer));
	memcpy(buffer + sizeof(buffer) - 3, FixturePatch.search, 3);
	assert(search(buffer, sizeof(buffer)).state == IMKLFX::TargetState::Mismatch);
}

void testCrossWindowAndCrossPageCandidatesReject() {
	uint8_t buffer[IMKLFX::X8664ValidationPageSize * 2] {};
	auto shortWindow = FixtureWindowVariant;
	shortWindow.searchWindowEnd = 256;

	// The complete implementation is present in the supplied bytes, but its
	// after-context extends past the approved window.
	putSupportedFunction(buffer, sizeof(buffer), 240);
	assert(IMKLFX::selectBoundedWindowPatch(buffer, sizeof(buffer), 0,
		shortWindow, true).state == IMKLFX::TargetState::Mismatch);

	memset(buffer, 0, sizeof(buffer));
	const size_t crossPageTarget = IMKLFX::X8664ValidationPageSize -
		FixturePatch.searchSize;
	putSupportedFunction(buffer, sizeof(buffer), crossPageTarget);
	assert(IMKLFX::selectBoundedWindowPatch(buffer, sizeof(buffer), 0,
		FixtureWindowVariant, true).state == IMKLFX::TargetState::Mismatch);
}

void testEveryByteAndContextMutationFails() {
	uint8_t buffer[IMKLFX::X8664ValidationPageSize] {};
	constexpr size_t target = 128;
	putSupportedFunction(buffer, sizeof(buffer), target);
	for (size_t i = 0; i < FixturePatch.searchSize; i++) {
		buffer[target + i] ^= 1;
		assert(search(buffer, sizeof(buffer)).state == IMKLFX::TargetState::Mismatch);
		buffer[target + i] ^= 1;
	}
	for (size_t i = 0; i < FixturePatch.contextBeforeSize; i++) {
		buffer[target - FixturePatch.contextBeforeSize + i] ^= 1;
		assert(search(buffer, sizeof(buffer)).state == IMKLFX::TargetState::Mismatch);
		buffer[target - FixturePatch.contextBeforeSize + i] ^= 1;
	}
	for (size_t i = 0; i < FixturePatch.contextAfterSize; i++) {
		buffer[target + FixturePatch.searchSize + i] ^= 1;
		assert(search(buffer, sizeof(buffer)).state == IMKLFX::TargetState::Mismatch);
		buffer[target + FixturePatch.searchSize + i] ^= 1;
	}
}

void testAlreadyPartialUnknownAndReplacementBounds() {
	uint8_t buffer[IMKLFX::X8664ValidationPageSize] {};
	constexpr size_t target = 128;
	putSupportedFunction(buffer, sizeof(buffer), target);
	memcpy(buffer + target, FixturePatch.replacement, FixturePatch.replacementSize);
	assert(search(buffer, sizeof(buffer)).state == IMKLFX::TargetState::AlreadyPatched);

	buffer[target + FixturePatch.replacementSize] ^= 1;
	assert(search(buffer, sizeof(buffer)).state == IMKLFX::TargetState::Mismatch);

	auto invalidPatch = FixturePatch;
	uint8_t oversizedReplacement[32] {};
	invalidPatch.replacement = oversizedReplacement;
	invalidPatch.replacementSize = sizeof(oversizedReplacement);
	assert(!IMKLFX::validPatchDefinition(invalidPatch));

	auto unknownPatch = FixturePatch;
	uint8_t unknownSearch[sizeof(FixtureSearch)] {};
	unknownPatch.search = unknownSearch;
	const IMKLFX::PatchDefinition *unknownAllowed[] = {&unknownPatch};
	auto unknownVariant = FixtureWindowVariant;
	unknownVariant.allowedPatches = unknownAllowed;
	memset(buffer, 0, sizeof(buffer));
	putSupportedFunction(buffer, sizeof(buffer), target);
	assert(search(buffer, sizeof(buffer), unknownVariant).state ==
		IMKLFX::TargetState::Mismatch);
}

void testMultipleDefinitionsAtOnePositionAreAmbiguous() {
	uint8_t buffer[IMKLFX::X8664ValidationPageSize] {};
	putSupportedFunction(buffer, sizeof(buffer), 128);
	auto duplicatePatch = FixturePatch;
	duplicatePatch.identifier = "fixture-mkl-implementation-x86_64-v2";
	const IMKLFX::PatchDefinition *allowed[] = {&FixturePatch, &duplicatePatch};
	auto variant = FixtureWindowVariant;
	variant.allowedPatches = allowed;
	variant.allowedPatchCount = 2;
	const auto selection = search(buffer, sizeof(buffer), variant);
	assert(selection.state == IMKLFX::TargetState::Ambiguous);
	assert(selection.matchCount == 2);
}

void testWindowPolicyBoundsAndGate() {
	uint8_t buffer[IMKLFX::X8664ValidationPageSize] {};
	putSupportedFunction(buffer, sizeof(buffer), 128);
	assert(search(buffer, sizeof(buffer), FixtureWindowVariant, false).state ==
		IMKLFX::TargetState::ModeDisabled);

	const IMKLFX::ImageVariant *variants[] = {&FixtureWindowVariant};
	auto selection = IMKLFX::selectImageVariant(variants, 1, FixturePath,
		sizeof(FixturePath) - 1, 0, sizeof(buffer), FixtureIdentity, WindowOff);
	assert(selection.state == IMKLFX::VariantMatchState::ModeDisabled);
	selection = IMKLFX::selectImageVariant(variants, 1, FixturePath,
		sizeof(FixturePath) - 1, 0, sizeof(buffer), FixtureIdentity, WindowOn);
	assert(selection.state == IMKLFX::VariantMatchState::Approved);
	auto invalidSigning = FixtureIdentity;
	invalidSigning.codeValid = false;
	selection = IMKLFX::selectImageVariant(variants, 1, FixturePath,
		sizeof(FixturePath) - 1, 0, sizeof(buffer), invalidSigning, WindowOn);
	assert(selection.state == IMKLFX::VariantMatchState::SigningPolicyRejected);

	auto outside = FixtureWindowVariant;
	outside.searchWindowStart = IMKLFX::X8664ValidationPageSize;
	outside.searchWindowEnd = IMKLFX::X8664ValidationPageSize * 2;
	outside.executableTextEnd = IMKLFX::X8664ValidationPageSize * 2;
	assert(IMKLFX::selectBoundedWindowPatch(buffer, sizeof(buffer), 0, outside,
		true).state == IMKLFX::TargetState::NotCovered);

	auto crossPage = FixtureWindowVariant;
	crossPage.searchWindowStart = IMKLFX::X8664ValidationPageSize - 16;
	crossPage.searchWindowEnd = IMKLFX::X8664ValidationPageSize + 16;
	crossPage.executableTextEnd = IMKLFX::X8664ValidationPageSize * 2;
	assert(!IMKLFX::validImageVariant(crossPage));
	assert(IMKLFX::callbackRangeContainsWindow(UINT64_MAX - 2, 2,
		UINT64_MAX - 1, UINT64_MAX));
	assert(!IMKLFX::callbackRangeContainsWindow(UINT64_MAX - 2, 1,
		UINT64_MAX - 1, UINT64_MAX));
	size_t checked = 0;
	assert(IMKLFX::checkedAddSize(SIZE_MAX - 1, 1, checked));
	assert(checked == SIZE_MAX);
	assert(!IMKLFX::checkedAddSize(SIZE_MAX, 1, checked));
}

void testSecondApplicationReusesPatchWithoutEngineChanges() {
	const IMKLFX::ImageIdentity identity {
		"second.signing.identifier", "ZYXWV98765", WrongFixtureCdHash,
		sizeof(WrongFixtureCdHash), true, true, false
	};
	const IMKLFX::ImageVariant *variants[] = {&SecondWindowVariant};
	const auto selection = IMKLFX::selectImageVariant(variants, 1,
		SecondFixturePath, sizeof(SecondFixturePath) - 1, 0,
		IMKLFX::X8664ValidationPageSize, identity, WindowOn);
	assert(selection.state == IMKLFX::VariantMatchState::Approved);
	assert(selection.variant->allowedPatches[0] == &FixturePatch);
	assert(FixtureApplication.pathMatcher != SecondFixtureApplication.pathMatcher);
}

} // namespace

int main() {
	testGenericApplicationSelection();
	testStrictModeRemainsExactAndNeverSearches();
	testWindowZeroUniqueTwoAndMany();
	testWindowBoundaryPositionsAndTruncation();
	testCrossWindowAndCrossPageCandidatesReject();
	testEveryByteAndContextMutationFails();
	testAlreadyPartialUnknownAndReplacementBounds();
	testMultipleDefinitionsAtOnePositionAreAmbiguous();
	testWindowPolicyBoundsAndGate();
	testSecondApplicationReusesPatchWithoutEngineChanges();
	return 0;
}
