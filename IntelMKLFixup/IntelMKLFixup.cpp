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

enum class ApplyResult : uint8_t {
	Patched,
	AlreadyPatched,
	Rejected,
	WriteProtectionFailure,
	RestoreProtectionFailure,
	VerificationFailure
};

template <typename T>
bool solveRequiredSymbol(KernelPatcher &patcher, const char *name, T &function) {
	auto address = patcher.solveSymbol(KernelPatcher::KernelID, name);
	if (address == 0 || patcher.getError() != KernelPatcher::Error::NoError) {
		SYSLOG(MODULE_SHORT, "required symbol %s is unavailable", name);
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

bool isApprovedImage(vnode_t vp, memory_object_offset_t pageOffset) {
	if (vp == nullptr || vnode_vtype(vp) != VREG ||
		csVnodeGetBlob == nullptr || csBlobGetIdentity == nullptr ||
		csBlobGetTeamId == nullptr || csBlobGetCdHash == nullptr ||
		csBlobGetFlags == nullptr)
		return false;

	char path[PATH_MAX] {};
	int pathLength = PATH_MAX;
	if (vn_getpath(vp, path, &pathLength) != 0 ||
		pathLength <= 1 || pathLength > PATH_MAX || path[pathLength - 1] != '\0' ||
		!IMKLFX::matchDiscordStableKrispPath(path, static_cast<size_t>(pathLength - 1)))
		return false;

	auto blob = csVnodeGetBlob(vp, static_cast<off_t>(pageOffset));
	if (blob == nullptr)
		return false;

	const auto &variant = IMKLFX::DiscordStable00403KrispX8664;
	static constexpr unsigned int RequiredCodeSigningFlags = CS_VALID | CS_RUNTIME;
	static constexpr unsigned int ForbiddenCodeSigningFlags = CS_ADHOC;
	const unsigned int flags = csBlobGetFlags(blob);
	if ((flags & RequiredCodeSigningFlags) != RequiredCodeSigningFlags ||
		(flags & ForbiddenCodeSigningFlags) != 0)
		return false;

	const char *identity = csBlobGetIdentity(blob);
	const char *team = csBlobGetTeamId(blob);
	const uint8_t *cdhash = csBlobGetCdHash(blob);

	static constexpr size_t SigningIdentifierSize = sizeof("discord_krisp") - 1;
	static constexpr size_t TeamIdentifierSize = sizeof("53Q6R32WPB") - 1;

	return hasExactCString(identity, variant.signingIdentifier, SigningIdentifierSize) &&
		hasExactCString(team, variant.teamIdentifier, TeamIdentifierSize) &&
		cdhash != nullptr &&
		IMKLFX::bytesEqual(cdhash, variant.codeDirectoryHash, variant.codeDirectoryHashSize);
}

ApplyResult applyApprovedPatch(const void *data, size_t size, memory_object_offset_t pageOffset) {
	const auto &variant = IMKLFX::DiscordStable00403KrispX8664;
	const auto *bytes = static_cast<const uint8_t *>(data);
	auto state = IMKLFX::classifyTarget(bytes, size, pageOffset, variant);
	if (state == IMKLFX::TargetState::AlreadyPatched)
		return ApplyResult::AlreadyPatched;
	if (state != IMKLFX::TargetState::Original)
		return ApplyResult::Rejected;
	if (KernelPatcher::kernelWriteLock == nullptr ||
		MachInfo::setKernelWriting(true, KernelPatcher::kernelWriteLock) != KERN_SUCCESS)
		return ApplyResult::WriteProtectionFailure;

	ApplyResult result = ApplyResult::Rejected;
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
		(*tainted & TargetValidationBit) != 0 || (*nx & TargetValidationBit) != 0)
		return;
	if (!isApprovedImage(vp, pageOffset))
		return;

	const auto result = applyApprovedPatch(data, PAGE_SIZE, pageOffset);
	switch (result) {
		case ApplyResult::Patched:
			SYSLOG(MODULE_SHORT, "patched image %s with definition %s",
				variant.identifier, variant.patch->identifier);
			break;
		case ApplyResult::WriteProtectionFailure:
			SYSLOG(MODULE_SHORT, "write protection change failed for definition %s",
				variant.patch->identifier);
			break;
		case ApplyResult::RestoreProtectionFailure:
			SYSLOG(MODULE_SHORT, "write protection restore failed for definition %s",
				variant.patch->identifier);
			break;
		case ApplyResult::VerificationFailure:
			SYSLOG(MODULE_SHORT, "replacement verification failed for definition %s",
				variant.patch->identifier);
			break;
		case ApplyResult::AlreadyPatched:
		case ApplyResult::Rejected:
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
		SYSLOG(MODULE_SHORT, "unsupported Darwin version; route not installed");
		return false;
	}
	if (BaseDeviceInfo::get().cpuVendor != CPUInfo::CpuVendor::AMD) {
		SYSLOG(MODULE_SHORT, "non-AMD CPU; route not installed");
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
		SYSLOG(MODULE_SHORT, "failed to route _cs_validate_page");
		return false;
	}

	DBGLOG(MODULE_SHORT, "Darwin 24 AMD validation route installed");
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
		DBGLOG(MODULE_SHORT, "Intel Math Kernel Library fixup plugin loaded");
		auto error = lilu.onPatcherLoad([](void *, KernelPatcher &patcher) {
			if ((lilu.getRunMode() & LiluAPI::RunningNormal) != 0)
				prepareAndRoute(patcher);
		});
		if (error != LiluAPI::Error::NoError)
			SYSLOG(MODULE_SHORT, "failed to register patcher callback: %d", error);
	}
};
