//
//  IntelMKLFixup.cpp
//  IntelMKLFixup
//
//  Copyright © 2024 whatdahopper. All rights reserved.
//

// Credits to vit9696, since most of this code is based on RestrictEvents.

#include <IOKit/IOService.h>
#include <Headers/kern_api.hpp>
#include <Headers/kern_patcher.hpp>
#include <Headers/plugin_start.hpp>

#define MODULE_SHORT "imklfx"

// While this works for most software with modern Intel MKL versions,
// there is a good chance that patching just this might not be enough for older Intel MKL versions.
static const uint8_t findMklServIntelCpuTrue[] = {
	0x53, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0x35, 0x00,
	0x00, 0x00, 0x00, 0x85, 0xF6, 0x7C, 0x08, 0x89,
	0xF0, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3
};
static const uint8_t findMklServIntelCpuTrueMask[] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
	0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
static const uint8_t replMklServIntelCpuTrue[] = {
	0x55, 0x48, 0x89, 0xE5, 0xB8, 0x01, 0x00, 0x00,
	0x00, 0x5D, 0xC3, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t replMklServIntelCpuTrueMask[] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

static mach_vm_address_t orgCsValidateFunc;

void wrapCsValidate(vnode_t vp, const void* data, vm_size_t size) {
	char path[PATH_MAX];
	int pathlen = PATH_MAX;
	if (vn_getpath(vp, path, &pathlen)) return;
	
	//SYSLOG(MODULE_SHORT, "wrapCsValidate: %s", path);
	
	if (UNLIKELY(KernelPatcher::findAndReplaceWithMask(const_cast<void*>(data), size,
			findMklServIntelCpuTrue, findMklServIntelCpuTrueMask,
			replMklServIntelCpuTrue, replMklServIntelCpuTrueMask, 0, 0))) {
		SYSLOG(MODULE_SHORT, "Patched _mkl_serv_intel_cpu_true for \"%s\"", path);
	}
}

void wrapCsValidatePageBigSur(vnode_t vp,
							  memory_object_t pager,
							  memory_object_offset_t page_offset,
							  const void* data,
							  int* validated_p,
							  int* tainted_p,
							  int* nx_p) {
	FunctionCast(wrapCsValidatePageBigSur, orgCsValidateFunc)(vp, pager, page_offset, data, validated_p, tainted_p, nx_p);
	wrapCsValidate(vp, data, PAGE_SIZE);
}

void wrapCsValidateRangeHighSierra(vnode_t vp,
								   memory_object_t pager,
								   memory_object_offset_t offset,
								   const void* data,
								   vm_size_t size,
								   unsigned* result) {
	FunctionCast(wrapCsValidateRangeHighSierra, orgCsValidateFunc)(vp, pager, offset, data, size, result);
	wrapCsValidate(vp, data, size);
}

static const char *bootargOff[] {
	"-imklfxoff"
};

static const char *bootargDebug[] {
	"-imklfxdbg"
};

static const char *bootargBeta[] {
	"-imklfxbeta"
};

PluginConfiguration ADDPR(config) {
	xStringify(PRODUCT_NAME),
	parseModuleVersion(xStringify(MODULE_VERSION)),
	LiluAPI::AllowNormal,
	bootargOff,
	arrsize(bootargOff),
	bootargDebug,
	arrsize(bootargDebug),
	bootargBeta,
	arrsize(bootargBeta),
	KernelVersion::HighSierra,
	KernelVersion::Tahoe,
	[]() {
		DBGLOG(MODULE_SHORT, "Intel(tm) Math Kernel Library fixup plugin loaded");
		
		lilu.onPatcherLoadForce([](void *user, KernelPatcher &patcher) {
			if ((lilu.getRunMode() & LiluAPI::RunningNormal) != 0) {
				KernelPatcher::RouteRequest csValidateRoute = getKernelVersion() >= KernelVersion::BigSur ?
					KernelPatcher::RouteRequest("_cs_validate_page", wrapCsValidatePageBigSur, orgCsValidateFunc) :
					KernelPatcher::RouteRequest("_cs_validate_range", wrapCsValidateRangeHighSierra, orgCsValidateFunc);
				if (!patcher.routeMultipleLong(KernelPatcher::KernelID, &csValidateRoute, 1))
					SYSLOG(MODULE_SHORT, "Failed to route _cs_validate_page/range");
			}
		});
	}
};
