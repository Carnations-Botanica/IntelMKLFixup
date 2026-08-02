#!/bin/sh
set -eu

source_file="IntelMKLFixup/IntelMKLFixup.cpp"
policy_file="IntelMKLFixup/IntelMKLFixupPolicy.hpp"
manifest_schema="whitelist/manifest.schema.json"

test "$(grep -c 'lilu_os_memcpy' "$source_file")" -eq 1
grep -Fq 'applyAcknowledgedStrictFileBackedPatch' "$source_file"
grep -Fq 'variant.matchMode != IMKLFX::MatchMode::StrictVariant' "$source_file"
grep -Fq 'decideFileBackedWrite' "$source_file"
grep -Fq 'FileBackedWriteDecision::StrictWritePermitted' "$source_file"
grep -Fq 'fileBackedPatchAcknowledged = checkKernelArgument("-imklfxfilepatch")' "$source_file"
grep -Fq 'mode=file-backed-patch acknowledged=yes file-visible=yes experimental=yes' "$source_file"
grep -Fq 'outcome=file-backed-write-not-acknowledged modified=no' "$source_file"
grep -Fq 'outcome=file-backed-patched modified=yes file-visible=yes' "$source_file"

if grep -Fq 'mode=in-memory' "$source_file"; then
	echo "forbidden in-memory mode label found" >&2
	exit 1
fi

if grep -En '#include .*socket|#include .*network|http|URLSession|connect\(' \
		"$source_file" "$policy_file"; then
	echo "networking primitive found in kernel-facing source" >&2
	exit 1
fi

if grep -Eiq '"(search|replacement|machine_code|script|url)_bytes"' \
		"$manifest_schema"; then
	echo "remote manifest schema can carry machine-code material" >&2
	exit 1
fi

echo "file-backed write scope checks passed"
