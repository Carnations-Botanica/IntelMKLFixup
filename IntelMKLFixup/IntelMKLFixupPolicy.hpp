//
//  IntelMKLFixupPolicy.hpp
//  IntelMKLFixup
//
//  Pure, allocation-free policy and byte matching helpers. This header must
//  remain usable by host-side tests without Lilu or kernel headers.
//

#ifndef IntelMKLFixupPolicy_hpp
#define IntelMKLFixupPolicy_hpp

#include <stddef.h>
#include <stdint.h>

namespace IMKLFX {

static constexpr size_t MaximumPathLength {1024};
static constexpr size_t MaximumCatalogueVariants {128};
static constexpr size_t MaximumAllowedPatchesPerVariant {8};
static constexpr bool ReviewedSearchModeEnabled {false};

enum class MatchMode : uint8_t {
	StrictVariant,
	ReviewedSearch
};

enum class TeamIdentifierPolicy : uint8_t {
	Exact,
	Absent
};

enum class CodeSigningPolicy : uint8_t {
	ValidRuntimeNotAdHoc,
	ValidAdHoc
};

enum class PatchValidationPolicy : uint8_t {
	ExactBytesAndContext
};

enum class VariantMatchState : uint8_t {
	NotCandidate,
	Approved,
	ModeDisabled,
	SigningPolicyRejected,
	SigningIdentifierRejected,
	TeamIdentifierRejected,
	CodeDirectoryHashRejected,
	Ambiguous
};

enum class TargetState : uint8_t {
	NotCovered,
	Original,
	AlreadyPatched,
	Mismatch,
	ModeDisabled,
	Ambiguous
};

using PathRuleMatcher = bool (*)(const char *, size_t);

struct ApplicationRule {
	const char *identifier;
	const char *name;
	const char *pathRuleIdentifier;
	const char *basename;
	size_t basenameSize;
	PathRuleMatcher pathMatcher;
	const char *signingIdentifier;
	size_t signingIdentifierSize;
	TeamIdentifierPolicy teamIdentifierPolicy;
	const char *teamIdentifier;
	size_t teamIdentifierSize;
	CodeSigningPolicy codeSigningPolicy;
};

struct PatchDefinition {
	const char *identifier;
	const char *name;
	const char *architecture;
	const uint8_t *search;
	size_t searchSize;
	const uint8_t *searchMask;
	size_t searchMaskSize;
	const uint8_t *replacement;
	size_t replacementSize;
	const uint8_t *replacementMask;
	size_t replacementMaskSize;
	const uint8_t *contextBefore;
	size_t contextBeforeSize;
	const uint8_t *contextAfter;
	size_t contextAfterSize;
	PatchValidationPolicy validationPolicy;
	const char *knownMklGeneration;
	const char *validationRequirements;
};

struct ImageVariant {
	const char *identifier;
	const ApplicationRule *application;
	const char *applicationVersion;
	const char *architecture;
	const uint8_t *codeDirectoryHash;
	size_t codeDirectoryHashSize;
	MatchMode matchMode;
	uint64_t targetFileOffset;
	uint64_t executableTextStart;
	uint64_t executableTextEnd;
	const PatchDefinition *const *allowedPatches;
	size_t allowedPatchCount;
	const char *consumerNotes;
};

struct ImageIdentity {
	const char *signingIdentifier;
	const char *teamIdentifier;
	const uint8_t *codeDirectoryHash;
	size_t codeDirectoryHashSize;
	bool codeValid;
	bool runtimeSigned;
	bool adHocSigned;
};

struct VariantSelection {
	VariantMatchState state;
	const ImageVariant *variant;
};

struct PatchSelection {
	TargetState state;
	const PatchDefinition *patch;
};

inline bool bytesEqual(const uint8_t *left, const uint8_t *right, size_t size) {
	if (size == 0)
		return true;
	if (left == nullptr || right == nullptr)
		return false;
	for (size_t i = 0; i < size; i++) {
		if (left[i] != right[i])
			return false;
	}
	return true;
}

inline bool hasExactCString(const char *value, const char *expected, size_t expectedSize) {
	if (value == nullptr || expected == nullptr || expectedSize == 0)
		return false;
	for (size_t i = 0; i < expectedSize; i++) {
		if (value[i] != expected[i])
			return false;
	}
	return value[expectedSize] == '\0';
}

inline bool boundedCStringsEqual(const char *left, const char *right,
	size_t maximumSize) {
	if (left == nullptr || right == nullptr || maximumSize == 0)
		return false;
	for (size_t i = 0; i < maximumSize; i++) {
		if (left[i] != right[i])
			return false;
		if (left[i] == '\0')
			return true;
	}
	return false;
}

inline bool pathHasExactBasename(const char *path, size_t length,
	const char *basename, size_t basenameSize) {
	if (path == nullptr || basename == nullptr || basenameSize == 0 ||
		length < basenameSize || length > MaximumPathLength)
		return false;
	const size_t start = length - basenameSize;
	if (start != 0 && path[start - 1] != '/')
		return false;
	return bytesEqual(reinterpret_cast<const uint8_t *>(path + start),
		reinterpret_cast<const uint8_t *>(basename), basenameSize);
}

inline bool matchApplicationRule(const ApplicationRule &rule,
	const char *path, size_t length) {
	if (path == nullptr || length == 0 || length > MaximumPathLength ||
		rule.identifier == nullptr || rule.pathRuleIdentifier == nullptr ||
		rule.basename == nullptr || rule.pathMatcher == nullptr ||
		rule.signingIdentifier == nullptr || rule.signingIdentifierSize == 0)
		return false;
	return pathHasExactBasename(path, length, rule.basename, rule.basenameSize) &&
		rule.pathMatcher(path, length);
}

inline bool callbackRangeContainsOffset(uint64_t rangeOffset, size_t rangeSize,
	uint64_t targetOffset) {
	if (rangeOffset > targetOffset)
		return false;
	return targetOffset - rangeOffset < static_cast<uint64_t>(rangeSize);
}

inline bool catalogueMayTargetRange(const ImageVariant *const *variants,
	size_t variantCount, uint64_t rangeOffset, size_t rangeSize) {
	if (variants == nullptr || variantCount == 0 ||
		variantCount > MaximumCatalogueVariants || rangeSize == 0)
		return false;
	for (size_t i = 0; i < variantCount; i++) {
		const ImageVariant *variant = variants[i];
		if (variant == nullptr)
			continue;
		if (variant->matchMode == MatchMode::StrictVariant &&
			callbackRangeContainsOffset(rangeOffset, rangeSize, variant->targetFileOffset))
			return true;
		if (variant->matchMode == MatchMode::ReviewedSearch && ReviewedSearchModeEnabled &&
			rangeOffset < variant->executableTextEnd) {
			if (rangeOffset >= variant->executableTextStart ||
				variant->executableTextStart - rangeOffset < static_cast<uint64_t>(rangeSize))
				return true;
		}
	}
	return false;
}

inline bool matchesCodeSigningPolicy(const ApplicationRule &rule,
	const ImageIdentity &identity) {
	if (!identity.codeValid)
		return false;
	switch (rule.codeSigningPolicy) {
		case CodeSigningPolicy::ValidRuntimeNotAdHoc:
			return identity.runtimeSigned && !identity.adHocSigned;
		case CodeSigningPolicy::ValidAdHoc:
			return identity.adHocSigned && !identity.runtimeSigned;
	}
	return false;
}

inline VariantMatchState classifyImageVariant(const ImageVariant &variant,
	const char *path, size_t pathLength, uint64_t rangeOffset, size_t rangeSize,
	const ImageIdentity &identity) {
	const ApplicationRule *application = variant.application;
	if (application == nullptr || !matchApplicationRule(*application, path, pathLength))
		return VariantMatchState::NotCandidate;

	if (variant.matchMode == MatchMode::ReviewedSearch) {
		if (!ReviewedSearchModeEnabled)
			return VariantMatchState::ModeDisabled;
	} else if (!callbackRangeContainsOffset(rangeOffset, rangeSize, variant.targetFileOffset)) {
		return VariantMatchState::NotCandidate;
	}

	if (!matchesCodeSigningPolicy(*application, identity))
		return VariantMatchState::SigningPolicyRejected;
	if (!hasExactCString(identity.signingIdentifier, application->signingIdentifier,
		application->signingIdentifierSize))
		return VariantMatchState::SigningIdentifierRejected;

	if (application->teamIdentifierPolicy == TeamIdentifierPolicy::Exact) {
		if (!hasExactCString(identity.teamIdentifier, application->teamIdentifier,
			application->teamIdentifierSize))
			return VariantMatchState::TeamIdentifierRejected;
	} else if (identity.teamIdentifier != nullptr && identity.teamIdentifier[0] != '\0') {
		return VariantMatchState::TeamIdentifierRejected;
	}

	if (identity.codeDirectoryHash == nullptr || variant.codeDirectoryHash == nullptr ||
		variant.codeDirectoryHashSize != 20 ||
		identity.codeDirectoryHashSize != variant.codeDirectoryHashSize ||
		!bytesEqual(identity.codeDirectoryHash, variant.codeDirectoryHash,
			variant.codeDirectoryHashSize))
		return VariantMatchState::CodeDirectoryHashRejected;
	return VariantMatchState::Approved;
}

inline VariantSelection selectImageVariant(const ImageVariant *const *variants,
	size_t variantCount, const char *path, size_t pathLength, uint64_t rangeOffset,
	size_t rangeSize, const ImageIdentity &identity) {
	if (variants == nullptr || variantCount == 0 || variantCount > MaximumCatalogueVariants)
		return {VariantMatchState::NotCandidate, nullptr};

	const ImageVariant *selected = nullptr;
	VariantMatchState firstRejection = VariantMatchState::NotCandidate;
	for (size_t i = 0; i < variantCount; i++) {
		if (variants[i] == nullptr)
			continue;
		const auto state = classifyImageVariant(*variants[i], path, pathLength,
			rangeOffset, rangeSize, identity);
		if (state == VariantMatchState::Approved) {
			if (selected != nullptr)
				return {VariantMatchState::Ambiguous, nullptr};
			selected = variants[i];
		} else if (state != VariantMatchState::NotCandidate &&
			firstRejection == VariantMatchState::NotCandidate) {
			firstRejection = state;
		}
	}
	if (selected != nullptr)
		return {VariantMatchState::Approved, selected};
	return {firstRejection, nullptr};
}

inline bool validPatchDefinition(const PatchDefinition &patch) {
	return patch.identifier != nullptr && patch.architecture != nullptr &&
		patch.search != nullptr && patch.searchSize != 0 &&
		patch.replacement != nullptr && patch.replacementSize != 0 &&
		patch.replacementSize <= patch.searchSize &&
		patch.searchMask == nullptr && patch.searchMaskSize == 0 &&
		patch.replacementMask == nullptr && patch.replacementMaskSize == 0 &&
		patch.validationPolicy == PatchValidationPolicy::ExactBytesAndContext &&
		patch.contextBefore != nullptr && patch.contextBeforeSize != 0 &&
		patch.contextAfter != nullptr && patch.contextAfterSize != 0;
}

inline TargetState classifyTarget(const uint8_t *data, size_t dataSize,
	uint64_t rangeOffset, const ImageVariant &variant, const PatchDefinition &patch) {
	if (data == nullptr || !validPatchDefinition(patch) ||
		!boundedCStringsEqual(patch.architecture, variant.architecture, 16))
		return TargetState::Mismatch;
	if (variant.matchMode != MatchMode::StrictVariant)
		return TargetState::ModeDisabled;
	if (variant.targetFileOffset < variant.executableTextStart ||
		variant.targetFileOffset > variant.executableTextEnd ||
		patch.searchSize > variant.executableTextEnd - variant.targetFileOffset)
		return TargetState::Mismatch;

	if (rangeOffset > variant.targetFileOffset)
		return TargetState::NotCovered;
	const uint64_t relative64 = variant.targetFileOffset - rangeOffset;
	if (relative64 > static_cast<uint64_t>(dataSize))
		return TargetState::NotCovered;
	const size_t relative = static_cast<size_t>(relative64);
	if (patch.contextBeforeSize > relative)
		return TargetState::NotCovered;
	if (patch.searchSize > dataSize - relative)
		return TargetState::NotCovered;
	const size_t afterOffset = relative + patch.searchSize;
	if (patch.contextAfterSize > dataSize - afterOffset)
		return TargetState::NotCovered;

	if (!bytesEqual(data + relative - patch.contextBeforeSize,
		patch.contextBefore, patch.contextBeforeSize) ||
		!bytesEqual(data + afterOffset, patch.contextAfter, patch.contextAfterSize))
		return TargetState::Mismatch;
	if (bytesEqual(data + relative, patch.search, patch.searchSize))
		return TargetState::Original;
	if (bytesEqual(data + relative, patch.replacement, patch.replacementSize) &&
		bytesEqual(data + relative + patch.replacementSize,
			patch.search + patch.replacementSize,
			patch.searchSize - patch.replacementSize))
		return TargetState::AlreadyPatched;
	return TargetState::Mismatch;
}

inline PatchSelection selectStrictPatch(const uint8_t *data, size_t dataSize,
	uint64_t rangeOffset, const ImageVariant &variant) {
	if (variant.matchMode != MatchMode::StrictVariant)
		return {TargetState::ModeDisabled, nullptr};
	if (variant.allowedPatches == nullptr || variant.allowedPatchCount == 0 ||
		variant.allowedPatchCount > MaximumAllowedPatchesPerVariant)
		return {TargetState::Mismatch, nullptr};

	const PatchDefinition *selected = nullptr;
	TargetState selectedState = TargetState::Mismatch;
	bool everyDefinitionNotCovered = true;
	for (size_t i = 0; i < variant.allowedPatchCount; i++) {
		const PatchDefinition *patch = variant.allowedPatches[i];
		if (patch == nullptr) {
			everyDefinitionNotCovered = false;
			continue;
		}
		const auto state = classifyTarget(data, dataSize, rangeOffset, variant, *patch);
		if (state != TargetState::NotCovered)
			everyDefinitionNotCovered = false;
		if (state == TargetState::Original || state == TargetState::AlreadyPatched) {
			if (selected != nullptr)
				return {TargetState::Ambiguous, nullptr};
			selected = patch;
			selectedState = state;
		}
	}
	if (selected != nullptr)
		return {selectedState, selected};
	return {everyDefinitionNotCovered ? TargetState::NotCovered : TargetState::Mismatch, nullptr};
}

inline uint8_t *targetPointer(uint8_t *data, size_t dataSize, uint64_t rangeOffset,
	const ImageVariant &variant, const PatchDefinition &patch) {
	if (data == nullptr || !validPatchDefinition(patch) ||
		rangeOffset > variant.targetFileOffset)
		return nullptr;
	const uint64_t relative = variant.targetFileOffset - rangeOffset;
	if (relative > static_cast<uint64_t>(dataSize))
		return nullptr;
	const size_t offset = static_cast<size_t>(relative);
	if (patch.replacementSize > dataSize - offset)
		return nullptr;
	return data + offset;
}

} // namespace IMKLFX

#endif /* IntelMKLFixupPolicy_hpp */
