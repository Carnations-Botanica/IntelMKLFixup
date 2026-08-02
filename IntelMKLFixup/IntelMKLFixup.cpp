//
//  IntelMKLFixup.cpp
//  IntelMKLFixup
//
//  Copyright © 2024 whatdahopper. All rights reserved.
//

// Credits to vit9696, since most of this code is based on RestrictEvents.

#if !defined(__x86_64__)
#error IntelMKLFixup supports x86_64 only.
#endif

#include <IOKit/IOService.h>
#include <Headers/kern_api.hpp>
#include <Headers/kern_cpu.hpp>
#include <Headers/kern_devinfo.hpp>
#include <Headers/kern_mach.hpp>
#include <Headers/kern_patcher.hpp>
#include <Headers/plugin_start.hpp>

#include <kern/cs_blobs.h>
#include <sys/vnode.h>

#include "IntelMKLFixupCatalogue.hpp"

#define MODULE_SHORT "imklfx"

struct cs_blob;

namespace {

using CsVnodeGetBlob = cs_blob *(*)(vnode_t, off_t);
using CsBlobGetString = const char *(*)(cs_blob *);
using CsBlobGetHash = const uint8_t *(*)(cs_blob *);
using CsBlobGetFlags = unsigned int (*)(cs_blob *);

static mach_vm_address_t orgCsValidatePage {};
static CsVnodeGetBlob csVnodeGetBlob {nullptr};
static CsBlobGetString csBlobGetIdentity {nullptr};
static CsBlobGetString csBlobGetTeamId {nullptr};
static CsBlobGetHash csBlobGetCdHash {nullptr};
static CsBlobGetFlags csBlobGetFlags {nullptr};
static bool verboseLogging {false};
static bool dryRunMode {false};
static bool builtInOnlyRequested {false};
static bool boundedWindowMode {false};

// Architectural invariant: XNU's validation callback supplies a const alias of
// a vnode-pager-backed page. It is detection input, never a writable runtime
// patch destination.
static constexpr bool ValidationCallbackWritePermitted {false};
static_assert(!ValidationCallbackWritePermitted,
	"validation callback pages must remain read-only");

enum class ApplyResult : uint8_t {
	DryRunMatch,
	UnsafeFileBackedWriteBlocked,
	AlreadyPatched,
	SignatureRejected,
	RangeRejected,
	ModeRejected,
	AmbiguousPatch,
	PolicyRejected
};

struct ApplyOutcome {
	ApplyResult result;
	const IMKLFX::PatchDefinition *patch;
	uint64_t targetFileOffset;
};

template <typename T>
bool solveRequiredSymbol(KernelPatcher &patcher, const char *name, T &function) {
	auto address = patcher.solveSymbol(KernelPatcher::KernelID, name);
	if (address == 0 || patcher.getError() != KernelPatcher::Error::NoError) {
		SYSLOG(MODULE_SHORT, "lifecycle=route-rejected reason=missing-symbol symbol=%s", name);
		patcher.clearError();
		function = nullptr;
		return false;
	}
	function = reinterpret_cast<T>(address);
	return true;
}

bool getCandidatePath(vnode_t vp, char *path, size_t &pathLength) {
	pathLength = 0;
	if (vp == nullptr || path == nullptr || vnode_vtype(vp) != VREG)
		return false;

	int returnedLength = PATH_MAX;
	if (vn_getpath(vp, path, &returnedLength) != 0 || returnedLength <= 1 ||
		returnedLength > PATH_MAX || path[returnedLength - 1] != '\0')
		return false;

	pathLength = static_cast<size_t>(returnedLength - 1);
	return pathLength <= IMKLFX::MaximumPathLength;
}

const IMKLFX::ApplicationRule *firstMatchingApplication(const char *path,
	size_t pathLength) {
	for (size_t i = 0; i < IMKLFX::BuiltInApplicationRuleCount; i++) {
		const auto *application = IMKLFX::BuiltInApplicationRules[i];
		if (application != nullptr && IMKLFX::matchApplicationRule(*application,
			path, pathLength))
			return application;
	}
	return nullptr;
}

void logCandidatePath(const IMKLFX::ApplicationRule &application,
	const char *path, size_t pathLength) {
	if (!verboseLogging || path == nullptr || pathLength == 0)
		return;

	static constexpr char UserPrefix[] = "/Users/";
	if (pathLength > sizeof(UserPrefix) - 1 &&
		IMKLFX::bytesEqual(reinterpret_cast<const uint8_t *>(path),
			reinterpret_cast<const uint8_t *>(UserPrefix), sizeof(UserPrefix) - 1)) {
		const char *cursor = path + sizeof(UserPrefix) - 1;
		const char *end = path + pathLength;
		while (cursor < end && *cursor != '/')
			cursor++;
		if (cursor < end) {
			SYSLOG(MODULE_SHORT, "verbose candidate=%s path=~%s",
				application.identifier, cursor);
			return;
		}
	}
	SYSLOG(MODULE_SHORT, "verbose candidate=%s path=%s", application.identifier, path);
}

const char *variantResultReason(IMKLFX::VariantMatchState result) {
	switch (result) {
		case IMKLFX::VariantMatchState::ModeDisabled:
			return "search-mode-disabled";
		case IMKLFX::VariantMatchState::InvalidPolicy:
			return "invalid-policy";
		case IMKLFX::VariantMatchState::SigningPolicyRejected:
			return "signing-policy";
		case IMKLFX::VariantMatchState::SigningIdentifierRejected:
			return "signing-identifier";
		case IMKLFX::VariantMatchState::TeamIdentifierRejected:
			return "team-identifier";
		case IMKLFX::VariantMatchState::CodeDirectoryHashRejected:
			return "cdhash";
		case IMKLFX::VariantMatchState::Ambiguous:
			return "ambiguous-image-variant";
		case IMKLFX::VariantMatchState::NotCandidate:
			return "not-candidate";
		case IMKLFX::VariantMatchState::Approved:
			return "approved";
	}
	return "unknown";
}

ApplyOutcome evaluateApprovedMatch(const void *data, size_t size,
	memory_object_offset_t pageOffset, const IMKLFX::ImageVariant &variant,
	const IMKLFX::RuntimePolicyControls &controls, bool dryRun) {
	const auto *bytes = static_cast<const uint8_t *>(data);
	auto selection = IMKLFX::selectPolicyPatch(bytes, size, pageOffset, variant, controls);
	if (selection.state == IMKLFX::TargetState::AlreadyPatched) {
		if (dryRun)
			return {ApplyResult::AlreadyPatched, selection.patch, selection.targetFileOffset};
		return {ApplyResult::UnsafeFileBackedWriteBlocked, selection.patch,
			selection.targetFileOffset};
	}
	if (selection.state == IMKLFX::TargetState::NotCovered)
		return {ApplyResult::RangeRejected, nullptr, 0};
	if (selection.state == IMKLFX::TargetState::ModeDisabled)
		return {ApplyResult::ModeRejected, nullptr, 0};
	if (selection.state == IMKLFX::TargetState::Ambiguous)
		return {ApplyResult::AmbiguousPatch, nullptr, 0};
	if (selection.state == IMKLFX::TargetState::InvalidPolicy)
		return {ApplyResult::PolicyRejected, nullptr, 0};
	if (selection.state != IMKLFX::TargetState::Original || selection.patch == nullptr)
		return {ApplyResult::SignatureRejected, nullptr, 0};
	if (dryRun)
		return {ApplyResult::DryRunMatch, selection.patch, selection.targetFileOffset};

	// Darwin 24 supplies a read-only kernel alias of a vnode-pager-backed VM
	// page here. Hardware testing proved that writing through this alias changes
	// bytes returned by ordinary reads of the backing file. This callback is a
	// detection boundary only; it must never be treated as a runtime patch target.
	return {ApplyResult::UnsafeFileBackedWriteBlocked, selection.patch,
		selection.targetFileOffset};
}

void logApplyOutcome(const IMKLFX::ImageVariant &variant,
	const ApplyOutcome &outcome) {
	const char *patchIdentifier = outcome.patch != nullptr ?
		outcome.patch->identifier : "none";
	const bool bounded = variant.matchMode == IMKLFX::MatchMode::BoundedWindow;
	switch (outcome.result) {
		case ApplyResult::DryRunMatch:
			if (bounded) {
				SYSLOG(MODULE_SHORT,
					"image=%s patch=%s search=unique-supported-signature outcome=dry-run-match modified=no offset=0x%llx",
					variant.identifier, patchIdentifier,
					static_cast<unsigned long long>(outcome.targetFileOffset));
			} else {
				SYSLOG(MODULE_SHORT,
					"image=%s patch=%s signature=supported outcome=dry-run modified=no offset=0x%llx",
					variant.identifier, patchIdentifier,
					static_cast<unsigned long long>(outcome.targetFileOffset));
			}
			break;
		case ApplyResult::UnsafeFileBackedWriteBlocked:
			SYSLOG(MODULE_SHORT,
				"image=%s patch=%s outcome=unsafe-file-backed-write-blocked modified=no offset=0x%llx",
				variant.identifier, patchIdentifier,
				static_cast<unsigned long long>(outcome.targetFileOffset));
			break;
		case ApplyResult::AlreadyPatched:
			SYSLOG(MODULE_SHORT,
				"image=%s patch=%s outcome=already-patched modified=no offset=0x%llx",
				variant.identifier, patchIdentifier,
				static_cast<unsigned long long>(outcome.targetFileOffset));
			break;
		case ApplyResult::SignatureRejected:
			if (bounded) {
				SYSLOG(MODULE_SHORT,
					"image=%s patch=%s outcome=no-supported-signature",
					variant.identifier, patchIdentifier);
			} else {
				SYSLOG(MODULE_SHORT,
					"image=%s patch=%s outcome=rejected reason=signature-or-context",
					variant.identifier, patchIdentifier);
			}
			break;
		case ApplyResult::RangeRejected:
			SYSLOG(MODULE_SHORT, "image=%s patch=%s outcome=rejected reason=callback-range",
				variant.identifier, patchIdentifier);
			break;
		case ApplyResult::ModeRejected:
			SYSLOG(MODULE_SHORT, "image=%s patch=%s outcome=rejected reason=search-mode-disabled",
				variant.identifier, patchIdentifier);
			break;
		case ApplyResult::AmbiguousPatch:
			if (bounded) {
				SYSLOG(MODULE_SHORT,
					"image=%s patch=%s outcome=ambiguous-signatures",
					variant.identifier, patchIdentifier);
			} else {
				SYSLOG(MODULE_SHORT,
					"image=%s patch=%s outcome=rejected reason=ambiguous-patch",
					variant.identifier, patchIdentifier);
			}
			break;
		case ApplyResult::PolicyRejected:
			SYSLOG(MODULE_SHORT,
				"image=%s patch=%s outcome=policy-rejected",
				variant.identifier, patchIdentifier);
			break;
	}
}

void inspectValidatedPage(vnode_t vp, memory_object_offset_t pageOffset,
	const void *data, int *validated, int *tainted, int *nx) {
	static constexpr int TargetValidationBit = 1;
	static_assert(PAGE_SIZE == IMKLFX::X8664ValidationPageSize,
		"the x86_64 validation-bit policy requires 4 KiB pages");

	if (vp == nullptr || data == nullptr || validated == nullptr ||
		tainted == nullptr || nx == nullptr)
		return;
	if (!IMKLFX::catalogueMayTargetRange(IMKLFX::BuiltInImageVariants,
		IMKLFX::BuiltInImageVariantCount, pageOffset, PAGE_SIZE))
		return;

	char path[PATH_MAX] {};
	size_t pathLength {};
	if (!getCandidatePath(vp, path, pathLength))
		return;
	const auto *candidateApplication = firstMatchingApplication(path, pathLength);
	if (candidateApplication == nullptr)
		return;
	logCandidatePath(*candidateApplication, path, pathLength);

	if ((*validated & TargetValidationBit) == 0 ||
		(*tainted & TargetValidationBit) != 0 || (*nx & TargetValidationBit) != 0) {
		SYSLOG_COND(verboseLogging, MODULE_SHORT,
			"candidate=%s outcome=rejected reason=xnu-page-validation validated=0x%x tainted=0x%x nx=0x%x",
			candidateApplication->identifier, *validated, *tainted, *nx);
		return;
	}

	if (csVnodeGetBlob == nullptr || csBlobGetIdentity == nullptr ||
		csBlobGetTeamId == nullptr || csBlobGetCdHash == nullptr ||
		csBlobGetFlags == nullptr)
		return;
	auto blob = csVnodeGetBlob(vp, static_cast<off_t>(pageOffset));
	if (blob == nullptr) {
		SYSLOG(MODULE_SHORT, "candidate=%s outcome=rejected reason=code-blob-unavailable",
			candidateApplication->identifier);
		return;
	}

	const unsigned int flags = csBlobGetFlags(blob);
	const IMKLFX::ImageIdentity identity {
		csBlobGetIdentity(blob),
		csBlobGetTeamId(blob),
		csBlobGetCdHash(blob),
		20,
		(flags & CS_VALID) != 0,
		(flags & CS_RUNTIME) != 0,
		(flags & CS_ADHOC) != 0
	};
	const IMKLFX::RuntimePolicyControls controls {boundedWindowMode};
	const auto selection = IMKLFX::selectImageVariant(IMKLFX::BuiltInImageVariants,
		IMKLFX::BuiltInImageVariantCount, path, pathLength, pageOffset, PAGE_SIZE,
		identity, controls);
	if (selection.state != IMKLFX::VariantMatchState::Approved ||
		selection.variant == nullptr) {
		if (selection.state != IMKLFX::VariantMatchState::NotCandidate) {
			SYSLOG(MODULE_SHORT, "candidate=%s outcome=rejected reason=%s",
				candidateApplication->identifier, variantResultReason(selection.state));
		}
		return;
	}

	const auto &variant = *selection.variant;
	SYSLOG(MODULE_SHORT,
		"candidate-app-approved application=%s image=%s mode=%s",
		variant.application->identifier, variant.identifier,
		variant.matchMode == IMKLFX::MatchMode::StrictVariant ? "strict-variant" :
			"bounded-window");
	if (variant.matchMode == IMKLFX::MatchMode::BoundedWindow) {
		SYSLOG(MODULE_SHORT,
			"image=%s outcome=search-started scope=bounded-window start=0x%llx end=0x%llx",
			variant.identifier,
			static_cast<unsigned long long>(variant.searchWindowStart),
			static_cast<unsigned long long>(variant.searchWindowEnd));
	}
	SYSLOG_COND(verboseLogging, MODULE_SHORT,
		"verbose candidate=%s image=%s signing-id=%s patch-policy-count=%lu identity=approved",
		variant.application->identifier, variant.identifier,
		variant.application->signingIdentifier,
		static_cast<unsigned long>(variant.allowedPatchCount));
	logApplyOutcome(variant, evaluateApprovedMatch(data, PAGE_SIZE, pageOffset,
		variant, controls, dryRunMode));
}

void wrapCsValidatePage(vnode_t vp, memory_object_t pager,
	memory_object_offset_t pageOffset, const void *data,
	int *validated, int *tainted, int *nx) {
	FunctionCast(wrapCsValidatePage, orgCsValidatePage)(vp, pager, pageOffset,
		data, validated, tainted, nx);
	inspectValidatedPage(vp, pageOffset, data, validated, tainted, nx);
}

bool prepareAndRoute(KernelPatcher &patcher) {
	if (getKernelVersion() != KernelVersion::Sequoia) {
		SYSLOG(MODULE_SHORT, "lifecycle=route-rejected reason=unsupported-darwin");
		return false;
	}
	if (BaseDeviceInfo::get().cpuVendor != CPUInfo::CpuVendor::AMD) {
		SYSLOG(MODULE_SHORT, "lifecycle=route-rejected reason=non-amd-cpu");
		return false;
	}

	if (!solveRequiredSymbol(patcher, "_csvnode_get_blob", csVnodeGetBlob) ||
		!solveRequiredSymbol(patcher, "_csblob_get_identity", csBlobGetIdentity) ||
		!solveRequiredSymbol(patcher, "_csblob_get_teamid", csBlobGetTeamId) ||
		!solveRequiredSymbol(patcher, "_csblob_get_cdhash", csBlobGetCdHash) ||
		!solveRequiredSymbol(patcher, "_csblob_get_flags", csBlobGetFlags))
		return false;

	KernelPatcher::RouteRequest route {
		"_cs_validate_page", wrapCsValidatePage, orgCsValidatePage
	};
	if (!patcher.routeMultipleLong(KernelPatcher::KernelID, &route, 1)) {
		SYSLOG(MODULE_SHORT, "lifecycle=route-rejected reason=cs-validate-page-routing");
		return false;
	}

	SYSLOG(MODULE_SHORT,
		"lifecycle=route-installed darwin=24 cpu=amd applications=%lu variants=%lu bounded-window=%s image-scan=reserved",
		static_cast<unsigned long>(IMKLFX::BuiltInApplicationRuleCount),
		static_cast<unsigned long>(IMKLFX::BuiltInImageVariantCount),
		boundedWindowMode ? "enabled" : "disabled");
	return true;
}

} // namespace

static const char *bootargOff[] {
	"-imklfxoff"
};

static const char *bootargDebug[] {
	"-imklfxdbg"
};

PluginConfiguration ADDPR(config) {
	xStringify(PRODUCT_NAME),
	parseModuleVersion(xStringify(MODULE_VERSION)),
	LiluAPI::AllowNormal,
	bootargOff,
	arrsize(bootargOff),
	bootargDebug,
	arrsize(bootargDebug),
	nullptr,
	0,
	KernelVersion::Sequoia,
	KernelVersion::Sequoia,
	[]() {
		verboseLogging = checkKernelArgument("-imklfxdbg");
		dryRunMode = checkKernelArgument("-imklfxdryrun");
		builtInOnlyRequested = checkKernelArgument("-imklfxbuiltin");
		boundedWindowMode = checkKernelArgument("-imklfxwindow");
		SYSLOG(MODULE_SHORT,
			"lifecycle=loaded mode=%s whitelist=builtin-only explicit-builtin=%d verbose=%d bounded-window=%d image-scan=reserved",
			dryRunMode ? "dry-run" : "patch", builtInOnlyRequested,
			verboseLogging, boundedWindowMode);
		auto error = lilu.onPatcherLoad([](void *, KernelPatcher &patcher) {
			if ((lilu.getRunMode() & LiluAPI::RunningNormal) != 0) {
				prepareAndRoute(patcher);
			} else {
				SYSLOG(MODULE_SHORT, "lifecycle=route-rejected reason=run-mode");
			}
		});
		if (error != LiluAPI::Error::NoError)
			SYSLOG(MODULE_SHORT, "lifecycle=registration-failed error=%d", static_cast<int>(error));
	}
};
