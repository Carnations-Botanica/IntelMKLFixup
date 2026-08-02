#include "event_api.h"

#include <dlfcn.h>
#include <inttypes.h>
#include <libproc.h>
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/proc_info.h>
#include <unistd.h>

static unsigned sequence;

void fixture_record(const char *event, const char *detail) {
	printf("%02u %-20s %s\n", ++sequence, event, detail ? detail : "");
	fflush(stdout);
}

static const char *protection_text(vm_prot_t protection, char output[4]) {
	output[0] = (protection & VM_PROT_READ) ? 'r' : '-';
	output[1] = (protection & VM_PROT_WRITE) ? 'w' : '-';
	output[2] = (protection & VM_PROT_EXECUTE) ? 'x' : '-';
	output[3] = '\0';
	return output;
}

static const char *share_mode_text(uint32_t mode) {
	switch (mode) {
		case SM_COW: return "COW";
		case SM_PRIVATE: return "PRIVATE";
		case SM_EMPTY: return "EMPTY";
		case SM_SHARED: return "SHARED";
		case SM_TRUESHARED: return "TRUE_SHARED";
		case SM_PRIVATE_ALIASED: return "PRIVATE_ALIASED";
		case SM_SHARED_ALIASED: return "SHARED_ALIASED";
		default: return "UNKNOWN";
	}
}

static void image_added(const struct mach_header *header, intptr_t slide) {
	Dl_info info = {0};
	if (dladdr(header, &info) == 0 || info.dli_fname == NULL ||
		strstr(info.dli_fname, "libFixture.dylib") == NULL)
		return;

	mach_vm_address_t address = (mach_vm_address_t)header;
	mach_vm_size_t size = 0;
	vm_region_basic_info_data_64_t basic = {0};
	mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
	mach_port_t object_name = MACH_PORT_NULL;
	kern_return_t kr = mach_vm_region(mach_task_self(), &address, &size,
		VM_REGION_BASIC_INFO_64, (vm_region_info_t)&basic, &count, &object_name);
	char detail[1024];
	if (kr == KERN_SUCCESS) {
		char current[4], maximum[4];
		struct proc_regionwithpathinfo path_region = {0};
		int got = proc_pidinfo(getpid(), PROC_PIDREGIONPATHINFO,
			(uint64_t)header, &path_region, sizeof(path_region));
		const char *share = got == (int)sizeof(path_region) ?
			share_mode_text(path_region.prp_prinfo.pri_share_mode) : "unavailable";
		snprintf(detail, sizeof(detail),
			"path=%s header=%p slide=0x%" PRIxPTR " region=[0x%" PRIx64
			",0x%" PRIx64 ") prot=%s max=%s shared=%s share-mode=%s",
			info.dli_fname, (const void *)header, (uintptr_t)slide,
			(uint64_t)address, (uint64_t)(address + size),
			protection_text(basic.protection, current),
			protection_text(basic.max_protection, maximum),
			basic.shared ? "yes" : "no", share);
		if (object_name != MACH_PORT_NULL)
			mach_port_deallocate(mach_task_self(), object_name);
	} else {
		snprintf(detail, sizeof(detail), "path=%s header=%p slide=0x%" PRIxPTR
			" region-query-failed=%d", info.dli_fname, (const void *)header,
			(uintptr_t)slide, kr);
	}
	fixture_record("image-add-callback", detail);
}

int main(int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr, "usage: %s /absolute/path/to/libFixture.dylib\n", argv[0]);
		return 64;
	}
	fixture_record("host-start", "registering image observer");
	_dyld_register_func_for_add_image(image_added);
	fixture_record("before-dlopen", argv[1]);
	void *handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
	if (handle == NULL) {
		fprintf(stderr, "dlopen: %s\n", dlerror());
		return 1;
	}
	fixture_record("after-dlopen", "dlopen returned");

	int (*predicate)(void) = (int (*)(void))dlsym(handle, "fixture_predicate");
	if (predicate == NULL) {
		fprintf(stderr, "dlsym: %s\n", dlerror());
		return 1;
	}
	fixture_record("before-predicate", "calling test predicate");
	int result = predicate();
	char detail[64];
	snprintf(detail, sizeof(detail), "result=%d", result);
	fixture_record("after-predicate", detail);
	dlclose(handle);
	return result == 7 ? 0 : 1;
}
