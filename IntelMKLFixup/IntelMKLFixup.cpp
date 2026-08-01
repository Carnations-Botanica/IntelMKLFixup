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

#include "IntelMKLFixupPolicy.hpp"

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

enum class ImageResult : uint8_t {
	NotCandidate,
	Approved,
	CodeBlobUnavailable,
	SigningFlagsRejected,
	SigningIdentifierRejected,
	TeamIdentifierRejected,
	CodeDirectoryHashRejected
};

enum class ApplyResult : uint8_t {
	DryRunMatch,
	Patched,
	AlreadyPatched,
	SignatureRejected,
	RangeRejected,
	ConcurrentChangeRejected,
	WriteProtectionFailure,
	RestoreProtectionFailure,
	VerificationFailure
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

bool hasExactCString(const char *value, const char *expected, size_t expectedSize) {
	if (value == nullptr || expected == nullptr || expectedSize == 0)
		return false;
	for (size_t i = 0; i < expectedSize; i++) {
		if (value[i] != expected[i])
			return false;
	}
	return value[expectedSize] == '\0';
}

bool getDiscordCandidatePath(vnode_t vp, char *path, size_t &pathLength) {
	pathLength = 0;
	if (vp == nullptr || path == nullptr || vnode_vtype(vp) != VREG)
		return false;

	int returnedLength = PATH_MAX;
	if (vn_getpath(vp, path, &returnedLength) != 0 ||
		returnedLength <= 1 || returnedLength > PATH_MAX || path[returnedLength - 1] != '\0')
		return false;

	pathLength = static_cast<size_t>(returnedLength - 1);
	return IMKLFX::matchDiscordStableKrispPath(path, pathLength);
}

void logRedactedCandidatePath(const char *path, size_t pathLength) {
	if (!verboseLogging || path == nullptr || pathLength == 0)
		return;

	static constexpr char UserPrefix[] = "/Users/";
	if (pathLength <= sizeof(UserPrefix) - 1)
		return;
	const char *cursor = path + sizeof(UserPrefix) - 1;
	const char *end = path + pathLength;
	while (cursor < end && *cursor != '/')
		cursor++;
	if (cursor < end)
		SYSLOG(MODULE_SHORT, "verbose candidate=discord-stable-krisp path=~%s", cursor);
}

const char *imageResultReason(ImageResult result) {
	switch (result) {
		case ImageResult::CodeBlobUnavailable:
			return "code-blob-unavailable";
		case ImageResult::SigningFlagsRejected:
			return "signing-flags";
		case ImageResult::SigningIdentifierRejected:
			return "signing-identifier";
		case ImageResult::TeamIdentifierRejected:
			return "team-identifier";
		case ImageResult::CodeDirectoryHashRejected:
			return "cdhash";
		case ImageResult::NotCandidate:
			return "not-candidate";
		case ImageResult::Approved:
			return "approved";
	}
	return "unknown";
}

ImageResult inspectImage(vnode_t vp, memory_object_offset_t pageOffset) {
	if (csVnodeGetBlob == nullptr || csBlobGetIdentity == nullptr ||
		csBlobGetTeamId == nullptr || csBlobGetCdHash == nullptr ||
		csBlobGetFlags == nullptr)
		return ImageResult::NotCandidate;

	char path[PATH_MAX] {};
	size_t pathLength {};
	if (!getDiscordCandidatePath(vp, path, pathLength))
		return ImageResult::NotCandidate;
	logRedactedCandidatePath(path, pathLength);

	auto blob = csVnodeGetBlob(vp, static_cast<off_t>(pageOffset));
	if (blob == nullptr)
		return ImageResult::CodeBlobUnavailable;

	const auto &variant = IMKLFX::DiscordStable00403KrispX8664;
	static constexpr unsigned int RequiredCodeSigningFlags = CS_VALID | CS_RUNTIME;
	static constexpr unsigned int ForbiddenCodeSigningFlags = CS_ADHOC;
	const unsigned int flags = csBlobGetFlags(blob);
	if ((flags & RequiredCodeSigningFlags) != RequiredCodeSigningFlags ||
		(flags & ForbiddenCodeSigningFlags) != 0)
		return ImageResult::SigningFlagsRejected;

	const char *identity = csBlobGetIdentity(blob);
	const char *team = csBlobGetTeamId(blob);
	const uint8_t *cdhash = csBlobGetCdHash(blob);

	static constexpr size_t SigningIdentifierSize = sizeof("discord_krisp") - 1;
	static constexpr size_t TeamIdentifierSize = sizeof("53Q6R32WPB") - 1;

	if (!hasExactCString(identity, variant.signingIdentifier, SigningIdentifierSize))
		return ImageResult::SigningIdentifierRejected;
	if (!hasExactCString(team, variant.teamIdentifier, TeamIdentifierSize))
		return ImageResult::TeamIdentifierRejected;
	if (cdhash == nullptr ||
		!IMKLFX::bytesEqual(cdhash, variant.codeDirectoryHash, variant.codeDirectoryHashSize))
		return ImageResult::CodeDirectoryHashRejected;

	SYSLOG_COND(verboseLogging, MODULE_SHORT,
		"verbose candidate=%s image=%s signing-id=%s team-id=%s patch=%s identity=approved",
		variant.applicationIdentifier, variant.identifier, variant.signingIdentifier,
		variant.teamIdentifier, variant.patch->identifier);
	return ImageResult::Approved;
}

ApplyResult applyApprovedPatch(const void *data, size_t size, memory_object_offset_t pageOffset,
	bool dryRun) {
	const auto &variant = IMKLFX::DiscordStable00403KrispX8664;
	const auto *bytes = static_cast<const uint8_t *>(data);
	auto state = IMKLFX::classifyTarget(bytes, size, pageOffset, variant);
	if (state == IMKLFX::TargetState::AlreadyPatched)
		return ApplyResult::AlreadyPatched;
	if (state == IMKLFX::TargetState::NotCovered)
		return ApplyResult::RangeRejected;
	if (state != IMKLFX::TargetState::Original)
		return ApplyResult::SignatureRejected;
	if (dryRun)
		return ApplyResult::DryRunMatch;
	if (KernelPatcher::kernelWriteLock == nullptr ||
		MachInfo::setKernelWriting(true, KernelPatcher::kernelWriteLock) != KERN_SUCCESS)
		return ApplyResult::WriteProtectionFailure;

	ApplyResult result = ApplyResult::ConcurrentChangeRejected;
	state = IMKLFX::classifyTarget(bytes, size, pageOffset, variant);
	if (state == IMKLFX::TargetState::AlreadyPatched) {
		result = ApplyResult::AlreadyPatched;
	} else if (state == IMKLFX::TargetState::Original) {
		auto target = IMKLFX::targetPointer(const_cast<uint8_t *>(bytes), size, pageOffset, variant);
		if (target != nullptr) {
			lilu_os_memcpy(target, variant.patch->replacement, variant.patch->replacementSize);
			result = IMKLFX::classifyTarget(bytes, size, pageOffset, variant) == IMKLFX::TargetState::AlreadyPatched ?
				ApplyResult::Patched : ApplyResult::VerificationFailure;
		}
	}

	if (MachInfo::setKernelWriting(false, KernelPatcher::kernelWriteLock) != KERN_SUCCESS)
		return ApplyResult::RestoreProtectionFailure;
	return result;
}

void inspectValidatedPage(vnode_t vp, memory_object_offset_t pageOffset, const void *data,
	int *validated, int *tainted, int *nx) {
	const auto &variant = IMKLFX::DiscordStable00403KrispX8664;
	static constexpr uint64_t PageMask = static_cast<uint64_t>(PAGE_SIZE) - 1;
	static_assert(PAGE_SIZE == 4096, "the x86_64 validation-bit policy requires 4 KiB pages");
	static_assert((IMKLFX::DiscordStable00403KrispX8664.targetFileOffset & PageMask) >=
		sizeof(IMKLFX::MklServIntelCpuTrueContextBeforeV1), "before-context must fit in one page");
	static_assert((IMKLFX::DiscordStable00403KrispX8664.targetFileOffset & PageMask) +
		sizeof(IMKLFX::MklServIntelCpuTrueSearchV1) +
		sizeof(IMKLFX::MklServIntelCpuTrueContextAfterV1) <= PAGE_SIZE,
		"target and context must fit in one page");

	if (vp == nullptr || data == nullptr || validated == nullptr || tainted == nullptr || nx == nullptr)
		return;
	if ((static_cast<uint64_t>(pageOffset) & ~PageMask) != (variant.targetFileOffset & ~PageMask))
		return;

	// The target is in the first 4 KiB validation subrange on x86_64.
	static constexpr int TargetValidationBit = 1;
	if ((*validated & TargetValidationBit) == 0 ||
		(*tainted & TargetValidationBit) != 0 || (*nx & TargetValidationBit) != 0) {
		if (verboseLogging) {
			char path[PATH_MAX] {};
			size_t pathLength {};
			if (getDiscordCandidatePath(vp, path, pathLength)) {
				logRedactedCandidatePath(path, pathLength);
				SYSLOG(MODULE_SHORT,
					"candidate=%s outcome=rejected reason=xnu-page-validation validated=0x%x tainted=0x%x nx=0x%x",
					variant.applicationIdentifier, *validated, *tainted, *nx);
			}
		}
		return;
	}

	const auto imageResult = inspectImage(vp, pageOffset);
	if (imageResult == ImageResult::NotCandidate)
		return;
	if (imageResult != ImageResult::Approved) {
		SYSLOG(MODULE_SHORT, "candidate=%s outcome=rejected reason=%s",
			variant.applicationIdentifier, imageResultReason(imageResult));
		return;
	}

	const auto result = applyApprovedPatch(data, PAGE_SIZE, pageOffset, dryRunMode);
	switch (result) {
		case ApplyResult::DryRunMatch:
			SYSLOG(MODULE_SHORT,
				"image=%s patch=%s signature=supported outcome=dry-run modified=no",
				variant.identifier, variant.patch->identifier);
			break;
		case ApplyResult::Patched:
			SYSLOG(MODULE_SHORT,
				"image=%s patch=%s signature=supported outcome=patched modified=yes",
				variant.identifier, variant.patch->identifier);
			break;
		case ApplyResult::AlreadyPatched:
			SYSLOG(MODULE_SHORT, "image=%s patch=%s outcome=skipped reason=already-patched",
				variant.identifier, variant.patch->identifier);
			break;
		case ApplyResult::SignatureRejected:
			SYSLOG(MODULE_SHORT,
				"image=%s patch=%s outcome=rejected reason=signature-or-context",
				variant.identifier, variant.patch->identifier);
			break;
		case ApplyResult::RangeRejected:
			SYSLOG(MODULE_SHORT, "image=%s patch=%s outcome=rejected reason=callback-range",
				variant.identifier, variant.patch->identifier);
			break;
		case ApplyResult::ConcurrentChangeRejected:
			SYSLOG(MODULE_SHORT, "image=%s patch=%s outcome=rejected reason=concurrent-change",
				variant.identifier, variant.patch->identifier);
			break;
		case ApplyResult::WriteProtectionFailure:
			SYSLOG(MODULE_SHORT, "patch=%s outcome=error reason=write-protection-change",
				variant.patch->identifier);
			break;
		case ApplyResult::RestoreProtectionFailure:
			SYSLOG(MODULE_SHORT, "patch=%s outcome=error reason=write-protection-restore",
				variant.patch->identifier);
			break;
		case ApplyResult::VerificationFailure:
			SYSLOG(MODULE_SHORT, "patch=%s outcome=error reason=replacement-verification",
				variant.patch->identifier);
			break;
	}
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

	SYSLOG(MODULE_SHORT, "lifecycle=route-installed darwin=24 cpu=amd");
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
		SYSLOG(MODULE_SHORT,
			"lifecycle=loaded mode=%s whitelist=builtin-only explicit-builtin=%d verbose=%d",
			dryRunMode ? "dry-run" : "patch", builtInOnlyRequested, verboseLogging);
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
