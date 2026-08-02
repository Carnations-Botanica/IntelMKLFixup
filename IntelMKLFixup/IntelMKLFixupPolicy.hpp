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
static constexpr uint64_t X8664ValidationPageSize {4096};

enum class MatchMode : uint8_t {
	StrictVariant,
	BoundedWindow
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
	InvalidPolicy,
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
	Ambiguous,
	InvalidPolicy
};

using PathRuleMatcher = bool (*)(const char *, size_t);

struct RuntimePolicyControls {
	bool boundedWindowEnabled;
};

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
	uint64_t searchWindowStart;
	uint64_t searchWindowEnd;
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
	uint64_t targetFileOffset;
	size_t matchCount;
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

inline bool checkedAddSize(size_t left, size_t right, size_t &result) {
	if (right > SIZE_MAX - left)
		return false;
	result = left + right;
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
		rule.basename == nullptr || rule.basenameSize == 0 ||
		rule.pathMatcher == nullptr || rule.signingIdentifier == nullptr ||
		rule.signingIdentifierSize == 0)
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

inline bool callbackRangeContainsWindow(uint64_t rangeOffset, size_t rangeSize,
	uint64_t windowStart, uint64_t windowEnd) {
	if (rangeSize == 0 || windowStart >= windowEnd || rangeOffset > windowStart)
		return false;
	const uint64_t relativeStart = windowStart - rangeOffset;
	if (relativeStart > static_cast<uint64_t>(rangeSize))
		return false;
	const uint64_t windowSize = windowEnd - windowStart;
	return windowSize <= static_cast<uint64_t>(rangeSize) - relativeStart;
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

inline bool validImageVariant(const ImageVariant &variant) {
	if (variant.identifier == nullptr || variant.application == nullptr ||
		variant.architecture == nullptr || variant.executableTextStart >= variant.executableTextEnd ||
		variant.allowedPatches == nullptr || variant.allowedPatchCount == 0 ||
		variant.allowedPatchCount > MaximumAllowedPatchesPerVariant)
		return false;
	if ((variant.codeDirectoryHash == nullptr) != (variant.codeDirectoryHashSize == 0) ||
		(variant.codeDirectoryHashSize != 0 && variant.codeDirectoryHashSize != 20))
		return false;
	if (variant.matchMode == MatchMode::StrictVariant) {
		return variant.targetFileOffset >= variant.executableTextStart &&
			variant.targetFileOffset < variant.executableTextEnd &&
			variant.searchWindowStart == 0 && variant.searchWindowEnd == 0;
	}
	if (variant.targetFileOffset != 0 || variant.searchWindowStart >= variant.searchWindowEnd ||
		variant.searchWindowStart < variant.executableTextStart ||
		variant.searchWindowEnd > variant.executableTextEnd ||
		(variant.searchWindowStart & (X8664ValidationPageSize - 1)) != 0)
		return false;
	return variant.searchWindowEnd - variant.searchWindowStart <= X8664ValidationPageSize;
}

inline bool catalogueMayTargetRange(const ImageVariant *const *variants,
	size_t variantCount, uint64_t rangeOffset, size_t rangeSize) {
	if (variants == nullptr || variantCount == 0 ||
		variantCount > MaximumCatalogueVariants || rangeSize == 0)
		return false;
	for (size_t i = 0; i < variantCount; i++) {
		const ImageVariant *variant = variants[i];
		if (variant == nullptr || !validImageVariant(*variant))
			continue;
		if (variant->matchMode == MatchMode::StrictVariant &&
			callbackRangeContainsOffset(rangeOffset, rangeSize, variant->targetFileOffset))
			return true;
		if (variant->matchMode == MatchMode::BoundedWindow &&
			callbackRangeContainsWindow(rangeOffset, rangeSize,
				variant->searchWindowStart, variant->searchWindowEnd))
			return true;
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
	const ImageIdentity &identity, const RuntimePolicyControls &controls) {
	if (!validImageVariant(variant))
		return VariantMatchState::InvalidPolicy;
	const ApplicationRule *application = variant.application;
	if (!matchApplicationRule(*application, path, pathLength))
		return VariantMatchState::NotCandidate;
	if (!matchesCodeSigningPolicy(*application, identity))
		return VariantMatchState::SigningPolicyRejected;
	if (!hasExactCString(identity.signingIdentifier, application->signingIdentifier,
		application->signingIdentifierSize))
		return VariantMatchState::SigningIdentifierRejected;
	if (application->teamIdentifierPolicy == TeamIdentifierPolicy::Exact) {
		if (application->teamIdentifier == nullptr || application->teamIdentifierSize == 0 ||
			!hasExactCString(identity.teamIdentifier, application->teamIdentifier,
				application->teamIdentifierSize))
			return VariantMatchState::TeamIdentifierRejected;
	} else if (identity.teamIdentifier != nullptr && identity.teamIdentifier[0] != '\0') {
		return VariantMatchState::TeamIdentifierRejected;
	}

	if (variant.codeDirectoryHashSize != 0 &&
		(identity.codeDirectoryHash == nullptr ||
		 identity.codeDirectoryHashSize != variant.codeDirectoryHashSize ||
		 !bytesEqual(identity.codeDirectoryHash, variant.codeDirectoryHash,
			variant.codeDirectoryHashSize)))
		return VariantMatchState::CodeDirectoryHashRejected;

	if (variant.matchMode == MatchMode::StrictVariant) {
		return callbackRangeContainsOffset(rangeOffset, rangeSize,
			variant.targetFileOffset) ? VariantMatchState::Approved :
			VariantMatchState::NotCandidate;
	}
	if (!controls.boundedWindowEnabled)
		return VariantMatchState::ModeDisabled;
	return callbackRangeContainsWindow(rangeOffset, rangeSize,
		variant.searchWindowStart, variant.searchWindowEnd) ?
		VariantMatchState::Approved : VariantMatchState::NotCandidate;
}

inline VariantSelection selectImageVariant(const ImageVariant *const *variants,
	size_t variantCount, const char *path, size_t pathLength, uint64_t rangeOffset,
	size_t rangeSize, const ImageIdentity &identity,
	const RuntimePolicyControls &controls) {
	if (variants == nullptr || variantCount == 0 || variantCount > MaximumCatalogueVariants)
		return {VariantMatchState::NotCandidate, nullptr};

	const ImageVariant *strict = nullptr;
	const ImageVariant *window = nullptr;
	VariantMatchState firstRejection = VariantMatchState::NotCandidate;
	for (size_t i = 0; i < variantCount; i++) {
		if (variants[i] == nullptr)
			continue;
		const auto state = classifyImageVariant(*variants[i], path, pathLength,
			rangeOffset, rangeSize, identity, controls);
		if (state == VariantMatchState::Approved) {
			const ImageVariant **selected = variants[i]->matchMode == MatchMode::StrictVariant ?
				&strict : &window;
			if (*selected != nullptr)
				return {VariantMatchState::Ambiguous, nullptr};
			*selected = variants[i];
		} else if (state == VariantMatchState::ModeDisabled ||
			(state != VariantMatchState::NotCandidate &&
			 firstRejection == VariantMatchState::NotCandidate)) {
			firstRejection = state;
		}
	}
	// An exact strict policy is more restrictive and wins when both independent
	// policies approve the same callback. BoundedWindow is never an implicit
	// fallback: it must also be present in the catalogue and boot-enabled.
	if (strict != nullptr)
		return {VariantMatchState::Approved, strict};
	if (window != nullptr)
		return {VariantMatchState::Approved, window};
	return {firstRejection, nullptr};
}

inline TargetState classifyCandidateAt(const uint8_t *data, size_t dataSize,
	uint64_t rangeOffset, uint64_t targetFileOffset, const ImageVariant &variant,
	const PatchDefinition &patch) {
	if (data == nullptr || !validPatchDefinition(patch) ||
		!boundedCStringsEqual(patch.architecture, variant.architecture, 16))
		return TargetState::Mismatch;
	if (targetFileOffset < variant.executableTextStart ||
		targetFileOffset > variant.executableTextEnd ||
		patch.searchSize > variant.executableTextEnd - targetFileOffset)
		return TargetState::Mismatch;
	if (rangeOffset > targetFileOffset)
		return TargetState::NotCovered;
	const uint64_t relative64 = targetFileOffset - rangeOffset;
	if (relative64 > static_cast<uint64_t>(dataSize))
		return TargetState::NotCovered;
	const size_t relative = static_cast<size_t>(relative64);
	if (patch.contextBeforeSize > relative || patch.searchSize > dataSize - relative)
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

inline TargetState classifyTarget(const uint8_t *data, size_t dataSize,
	uint64_t rangeOffset, const ImageVariant &variant, const PatchDefinition &patch) {
	if (variant.matchMode != MatchMode::StrictVariant)
		return TargetState::ModeDisabled;
	return classifyCandidateAt(data, dataSize, rangeOffset,
		variant.targetFileOffset, variant, patch);
}

inline PatchSelection selectStrictPatch(const uint8_t *data, size_t dataSize,
	uint64_t rangeOffset, const ImageVariant &variant) {
	if (variant.matchMode != MatchMode::StrictVariant)
		return {TargetState::ModeDisabled, nullptr, 0, 0};
	if (!validImageVariant(variant))
		return {TargetState::InvalidPolicy, nullptr, 0, 0};

	const PatchDefinition *selected = nullptr;
	TargetState selectedState = TargetState::Mismatch;
	bool everyDefinitionNotCovered = true;
	for (size_t i = 0; i < variant.allowedPatchCount; i++) {
		const PatchDefinition *patch = variant.allowedPatches[i];
		if (patch == nullptr || !validPatchDefinition(*patch))
			return {TargetState::InvalidPolicy, nullptr, 0, 0};
		const auto state = classifyTarget(data, dataSize, rangeOffset, variant, *patch);
		if (state != TargetState::NotCovered)
			everyDefinitionNotCovered = false;
		if (state == TargetState::Original || state == TargetState::AlreadyPatched) {
			if (selected != nullptr)
				return {TargetState::Ambiguous, nullptr, 0, 2};
			selected = patch;
			selectedState = state;
		}
	}
	if (selected != nullptr)
		return {selectedState, selected, variant.targetFileOffset, 1};
	return {everyDefinitionNotCovered ? TargetState::NotCovered : TargetState::Mismatch,
		nullptr, 0, 0};
}

inline PatchSelection selectBoundedWindowPatch(const uint8_t *data, size_t dataSize,
	uint64_t rangeOffset, const ImageVariant &variant, bool boundedWindowEnabled) {
	if (variant.matchMode != MatchMode::BoundedWindow || !boundedWindowEnabled)
		return {TargetState::ModeDisabled, nullptr, 0, 0};
	if (data == nullptr || !validImageVariant(variant))
		return {TargetState::InvalidPolicy, nullptr, 0, 0};
	if (!callbackRangeContainsWindow(rangeOffset, dataSize,
		variant.searchWindowStart, variant.searchWindowEnd))
		return {TargetState::NotCovered, nullptr, 0, 0};

	const uint64_t windowRelative64 = variant.searchWindowStart - rangeOffset;
	if (windowRelative64 > static_cast<uint64_t>(dataSize))
		return {TargetState::NotCovered, nullptr, 0, 0};
	const size_t windowRelative = static_cast<size_t>(windowRelative64);
	const size_t windowSize = static_cast<size_t>(
		variant.searchWindowEnd - variant.searchWindowStart);
	if (windowSize > dataSize - windowRelative)
		return {TargetState::NotCovered, nullptr, 0, 0};

	const PatchDefinition *selected = nullptr;
	TargetState selectedState = TargetState::Mismatch;
	uint64_t selectedOffset = 0;
	size_t matchCount = 0;
	for (size_t patchIndex = 0; patchIndex < variant.allowedPatchCount; patchIndex++) {
		const PatchDefinition *patch = variant.allowedPatches[patchIndex];
		if (patch == nullptr || !validPatchDefinition(*patch) ||
			!boundedCStringsEqual(patch->architecture, variant.architecture, 16))
			return {TargetState::InvalidPolicy, nullptr, 0, 0};

		size_t required = 0;
		if (!checkedAddSize(patch->contextBeforeSize, patch->searchSize, required) ||
			!checkedAddSize(required, patch->contextAfterSize, required) ||
			required > windowSize)
			continue;
		const size_t first = patch->contextBeforeSize;
		const size_t last = windowSize - patch->searchSize - patch->contextAfterSize;
		for (size_t position = first; position <= last; position++) {
			const uint64_t candidateOffset = variant.searchWindowStart + position;
			const auto state = classifyCandidateAt(data, dataSize, rangeOffset,
				candidateOffset, variant, *patch);
			if (state != TargetState::Original && state != TargetState::AlreadyPatched)
				continue;
			matchCount++;
			if (matchCount > 1)
				return {TargetState::Ambiguous, nullptr, 0, matchCount};
			selected = patch;
			selectedState = state;
			selectedOffset = candidateOffset;
		}
	}
	if (matchCount == 1)
		return {selectedState, selected, selectedOffset, matchCount};
	return {TargetState::Mismatch, nullptr, 0, 0};
}

inline PatchSelection selectPolicyPatch(const uint8_t *data, size_t dataSize,
	uint64_t rangeOffset, const ImageVariant &variant,
	const RuntimePolicyControls &controls) {
	if (variant.matchMode == MatchMode::StrictVariant)
		return selectStrictPatch(data, dataSize, rangeOffset, variant);
	return selectBoundedWindowPatch(data, dataSize, rangeOffset, variant,
		controls.boundedWindowEnabled);
}

} // namespace IMKLFX

#endif /* IntelMKLFixupPolicy_hpp */
