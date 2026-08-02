#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
	echo "usage: $0 /absolute/path/to/discord_krisp.node" >&2
	exit 64
fi

tool_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/imklfx-readonly-probe.XXXXXX")
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM

xcrun clang -std=c11 -Wall -Wextra -Werror -O2 \
	"$tool_dir/read_only_krisp_probe.c" -o "$build_dir/read_only_krisp_probe"
"$build_dir/read_only_krisp_probe" "$1" 0x650100 0x4000
