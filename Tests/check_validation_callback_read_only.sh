#!/bin/sh
set -eu

if [ "$#" -eq 0 ]; then
	set -- IntelMKLFixup/IntelMKLFixup.cpp IntelMKLFixup/IntelMKLFixupPolicy.hpp
fi

for source_file in "$@"; do
	if grep -En 'const_cast|setKernelWriting|lilu_os_memcpy|memcpy|memmove|bcopy|copyout|vm_map_write|vm_map_copy_overwrite|orgVmMapWriteUser|vmProtect|modified=yes|ApplyResult::Patched|applyApprovedPatch|uint8_t \*targetPointer' "$source_file"; then
		echo "validation callback contains a forbidden writable-patch primitive" >&2
		exit 1
	fi
done

if ! grep -Fq 'ValidationCallbackWritePermitted {false}' IntelMKLFixup/IntelMKLFixup.cpp; then
	echo "validation callback read-only architecture assertion is missing" >&2
	exit 1
fi

if ! grep -Fq 'outcome=unsafe-file-backed-write-blocked modified=no' IntelMKLFixup/IntelMKLFixup.cpp; then
	echo "validation callback is missing the fail-closed active-mode outcome" >&2
	exit 1
fi

echo "validation callback is detection-only"
