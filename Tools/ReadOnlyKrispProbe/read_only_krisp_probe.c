#include <errno.h>
#include <inttypes.h>
#include <libproc.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/proc_info.h>
#include <time.h>
#include <unistd.h>

/* csops(2) and these public XNU status bits are not exposed by the macOS SDK. */
extern int csops(pid_t pid, unsigned int ops, void *useraddr, size_t usersize);

enum {
	CS_OPS_STATUS = 0,
	CS_VALID = 0x00000001,
	CS_ADHOC = 0x00000002,
	CS_GET_TASK_ALLOW = 0x00000004,
	CS_FORCED_LV = 0x00000010,
	CS_HARD = 0x00000100,
	CS_KILL = 0x00000200,
	CS_RESTRICT = 0x00000800,
	CS_ENFORCEMENT = 0x00001000,
	CS_REQUIRE_LV = 0x00002000,
	CS_RUNTIME = 0x00010000,
	CS_DEBUGGED = 0x10000000,
	CS_SIGNED = 0x20000000
};

static const uint8_t expected_function[] = {
	0x53, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0x35, 0x61,
	0x0F, 0x79, 0x00, 0x85, 0xF6, 0x7C, 0x08, 0x89,
	0xF0, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3
};

static const char *share_mode_name(uint32_t mode) {
	switch (mode) {
		case SM_COW: return "COW";
		case SM_PRIVATE: return "PRIVATE";
		case SM_EMPTY: return "EMPTY";
		case SM_SHARED: return "SHARED";
		case SM_TRUESHARED: return "TRUE_SHARED";
		case SM_PRIVATE_ALIASED: return "PRIVATE_ALIASED";
		case SM_SHARED_ALIASED: return "SHARED_ALIASED";
		case SM_LARGE_PAGE: return "LARGE_PAGE";
		default: return "UNKNOWN";
	}
}

static void protection_string(uint32_t protection, char output[4]) {
	output[0] = (protection & VM_PROT_READ) ? 'r' : '-';
	output[1] = (protection & VM_PROT_WRITE) ? 'w' : '-';
	output[2] = (protection & VM_PROT_EXECUTE) ? 'x' : '-';
	output[3] = '\0';
}

static void print_csflags(pid_t pid) {
	uint32_t flags = 0;
	if (csops(pid, CS_OPS_STATUS, &flags, sizeof(flags)) != 0) {
		printf("  csflags=unavailable errno=%d (%s)\n", errno, strerror(errno));
		return;
	}
	printf("  csflags=0x%08x valid=%s signed=%s runtime=%s hard=%s kill=%s "
		"enforcement=%s require-lv=%s forced-lv=%s get-task-allow=%s "
		"debugged=%s restricted=%s\n",
		flags,
		(flags & CS_VALID) ? "yes" : "no",
		(flags & CS_SIGNED) ? "yes" : "no",
		(flags & CS_RUNTIME) ? "yes" : "no",
		(flags & CS_HARD) ? "yes" : "no",
		(flags & CS_KILL) ? "yes" : "no",
		(flags & CS_ENFORCEMENT) ? "yes" : "no",
		(flags & CS_REQUIRE_LV) ? "yes" : "no",
		(flags & CS_FORCED_LV) ? "yes" : "no",
		(flags & CS_GET_TASK_ALLOW) ? "yes" : "no",
		(flags & CS_DEBUGGED) ? "yes" : "no",
		(flags & CS_RESTRICT) ? "yes" : "no");
}

static void print_process(pid_t pid, const struct proc_bsdinfo *bsd,
	const char *path) {
	char parent_path[PROC_PIDPATHINFO_MAXSIZE] = {0};
	(void)proc_pidpath((int)bsd->pbi_ppid, parent_path, sizeof(parent_path));
	time_t start = (time_t)bsd->pbi_start_tvsec;
	struct tm local = {0};
	char start_text[64] = "unknown";
	if (localtime_r(&start, &local) != NULL)
		(void)strftime(start_text, sizeof(start_text), "%Y-%m-%dT%H:%M:%S%z", &local);
	printf("process pid=%d ppid=%u start=%s name=%s\n", pid, bsd->pbi_ppid,
		start_text, bsd->pbi_name[0] ? bsd->pbi_name : bsd->pbi_comm);
	printf("  executable=%s\n", path);
	printf("  parent-executable=%s\n", parent_path[0] ? parent_path : "unavailable");
	print_csflags(pid);
}

static int inspect_regions(pid_t pid, const char *module_path,
	uint64_t target_file_offset, uint64_t slice_file_offset,
	mach_vm_address_t *target_address_out, int emit) {
	uint64_t cursor = 0;
	int mapped_regions = 0;
	*target_address_out = 0;

	while (cursor < UINT64_MAX) {
		struct proc_regionwithpathinfo region = {0};
		int got = proc_pidinfo(pid, PROC_PIDREGIONPATHINFO, cursor,
			&region, sizeof(region));
		if (got != (int)sizeof(region))
			break;

		const struct proc_regioninfo *info = &region.prp_prinfo;
		if (info->pri_size == 0 || info->pri_address > UINT64_MAX - info->pri_size)
			break;

		if (strcmp(region.prp_vip.vip_path, module_path) == 0) {
			char current[4], maximum[4];
			protection_string(info->pri_protection, current);
			protection_string(info->pri_max_protection, maximum);
			if (emit) printf("  region start=0x%016" PRIx64 " end=0x%016" PRIx64
				" size=0x%" PRIx64 " file-offset=0x%" PRIx64
				" prot=%s max=%s share=%s object-id=%u ref-count=%u"
				" resident=%u private-resident=%u shared-resident=%u"
				" now-private=%u dirtied=%u shadow-depth=%u flags=0x%x\n",
				info->pri_address, info->pri_address + info->pri_size,
				info->pri_size, info->pri_offset, current, maximum,
				share_mode_name(info->pri_share_mode), info->pri_obj_id,
				info->pri_ref_count, info->pri_pages_resident,
				info->pri_private_pages_resident,
				info->pri_shared_pages_resident,
				info->pri_pages_shared_now_private, info->pri_pages_dirtied,
				info->pri_shadow_depth, info->pri_flags);
			mapped_regions++;

			if (target_file_offset >= info->pri_offset &&
				target_file_offset < info->pri_offset + info->pri_size) {
				*target_address_out = info->pri_address +
					(target_file_offset - info->pri_offset);
				if (emit) printf("  target-via-region=0x%016" PRIx64 "\n",
					(uint64_t)*target_address_out);
			}

			if (info->pri_offset == slice_file_offset) {
				uint64_t slide = info->pri_address;
				uint64_t target = slide + target_file_offset - slice_file_offset;
				if (emit) printf("  image-load-address=0x%016" PRIx64
					" aslr-slide=0x%016" PRIx64
					" target-via-slide=0x%016" PRIx64 "\n",
					slide, slide, target);
				if (*target_address_out == 0)
					*target_address_out = target;
			}
		}

		cursor = info->pri_address + info->pri_size;
	}
	return mapped_regions;
}

static void try_read_target(pid_t pid, mach_vm_address_t target) {
	if (target == 0)
		return;
	mach_port_t task = MACH_PORT_NULL;
	kern_return_t kr = task_for_pid(mach_task_self(), pid, &task);
	if (kr != KERN_SUCCESS) {
		printf("  task-for-pid=denied kern=%d (%s); target bytes not read\n",
			kr, mach_error_string(kr));
		return;
	}
	uint8_t bytes[sizeof(expected_function)] = {0};
	mach_vm_size_t read_size = 0;
	kr = mach_vm_read_overwrite(task, target, sizeof(bytes),
		(mach_vm_address_t)bytes, &read_size);
	if (kr != KERN_SUCCESS) {
		printf("  mach-vm-read=failed kern=%d (%s)\n", kr, mach_error_string(kr));
	} else {
		printf("  target-bytes=");
		for (size_t i = 0; i < read_size; i++)
			printf("%s%02x", i ? " " : "", bytes[i]);
		printf("\n  target-match=%s\n",
			read_size == sizeof(expected_function) &&
			memcmp(bytes, expected_function, sizeof(bytes)) == 0 ? "exact-original" : "no");
	}
	mach_port_deallocate(mach_task_self(), task);
}

static int parse_u64(const char *text, uint64_t *value) {
	char *end = NULL;
	errno = 0;
	unsigned long long parsed = strtoull(text, &end, 0);
	if (errno != 0 || end == text || *end != '\0')
		return -1;
	*value = (uint64_t)parsed;
	return 0;
}

int main(int argc, char **argv) {
	if (argc != 4) {
		fprintf(stderr, "usage: %s MODULE_PATH TARGET_FILE_OFFSET SLICE_FILE_OFFSET\n", argv[0]);
		return 64;
	}
	char resolved[PATH_MAX] = {0};
	if (realpath(argv[1], resolved) == NULL) {
		perror("realpath(module)");
		return 66;
	}
	uint64_t target_file_offset = 0, slice_file_offset = 0;
	if (parse_u64(argv[2], &target_file_offset) != 0 ||
		parse_u64(argv[3], &slice_file_offset) != 0 ||
		target_file_offset < slice_file_offset) {
		fprintf(stderr, "invalid offsets\n");
		return 64;
	}

	int pid_capacity = proc_listallpids(NULL, 0);
	if (pid_capacity <= 0) {
		perror("proc_listallpids(size)");
		return 1;
	}
	pid_capacity *= 2;
	int buffer_size = pid_capacity * (int)sizeof(pid_t);
	pid_t *pids = calloc((size_t)pid_capacity, sizeof(*pids));
	if (pids == NULL)
		return 1;
	int pid_count = proc_listallpids(pids, buffer_size);
	if (pid_count <= 0) {
		perror("proc_listallpids");
		free(pids);
		return 1;
	}

	printf("probe-mode=read-only module=%s target-file-offset=0x%" PRIx64
		" slice-file-offset=0x%" PRIx64 "\n",
		resolved, target_file_offset, slice_file_offset);
	int owners = 0;
	for (int i = 0; i < pid_count; i++) {
		pid_t pid = pids[i];
		if (pid <= 0)
			continue;
		char process_path[PROC_PIDPATHINFO_MAXSIZE] = {0};
		if (proc_pidpath(pid, process_path, sizeof(process_path)) <= 0)
			continue;
		if (strstr(process_path, "Discord") == NULL)
			continue;

		mach_vm_address_t target = 0;
		int regions = inspect_regions(pid, resolved, target_file_offset,
			slice_file_offset, &target, 0);
		if (regions <= 0)
			continue;
		struct proc_bsdinfo bsd = {0};
		if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &bsd, sizeof(bsd)) != (int)sizeof(bsd))
			continue;
		print_process(pid, &bsd, process_path);
		/* Emit region details after identity so each record is self-contained. */
		(void)inspect_regions(pid, resolved, target_file_offset, slice_file_offset,
			&target, 1);
		try_read_target(pid, target);
		owners++;
	}
	printf("owners=%d\n", owners);
	free(pids);
	return owners > 0 ? 0 : 2;
}
