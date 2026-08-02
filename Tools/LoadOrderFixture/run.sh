#!/bin/sh
set -eu

tool_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/imklfx-load-order.XXXXXX")
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM

xcrun clang -std=c11 -Wall -Wextra -Werror -O2 -fPIC -dynamiclib \
	-undefined dynamic_lookup \
	"$tool_dir/library.c" -o "$build_dir/libFixture.dylib"
xcrun clang -std=c11 -Wall -Wextra -Werror -O2 \
	-Wl,-export_dynamic "$tool_dir/host.c" -o "$build_dir/fixture-host"
"$build_dir/fixture-host" "$build_dir/libFixture.dylib"
