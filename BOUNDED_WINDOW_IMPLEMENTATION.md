# BoundedWindow implementation

## Selected design

BoundedWindow is a kernel-local, one-validation-window mode. It is deliberately
distinct from future ImageScan because it cannot prove uniqueness over a Mach-O
image or executable segment.

Each policy declares one non-empty window whose start is 4096-byte aligned and
whose size is at most 4096 bytes. `_cs_validate_page` must supply a page that
contains the complete window. After application path and code-signing approval,
the pure matcher counts complete exact matches for the policy's allowed
compiled PatchDefinitions within that window.

Zero matches reject, one accepts, and two terminate the scan as ambiguous. No
bytes outside the window influence that result.

## Verified APIs and assumptions

The implementation is gated to x86_64 Darwin 24 and asserts `PAGE_SIZE == 4096`.
Review used Apple XNU `xnu-11215.81.4`, the repository-pinned MacKernelSDK
commit `05094e5e88cec7caedbfb35e8449ed0db94bf95b`, the pinned Lilu commit
`e4748cc081bf060302c7d3c44a643ce1d11b7e1d`, and the symbols present in the
local Darwin 24.6 kernel.

XNU calls `_cs_validate_page` with a vnode, pager, page file offset, page data,
and validation bitmaps while the VM page is busy and its object is locked. The
plugin invokes the original routine first and examines only the supplied page.
It uses the existing verified `vn_getpath`, `_csvnode_get_blob`,
`_csblob_get_identity`, `_csblob_get_teamid`, `_csblob_get_cdhash`, and
`_csblob_get_flags` surfaces.

It does not route `_cs_validate_range`, call `vn_rdwr`, retain the vnode or data
pointer, or infer that other executable pages have been seen.

Primary references used for that boundary:

- [Apple XNU `vm_fault_validate_cs` call to `cs_validate_page`](https://github.com/apple-oss-distributions/xnu/blob/xnu-11215.81.4/osfmk/vm/vm_fault.c#L7888-L7933)
- [Apple XNU validation while the page is busy and object is locked](https://github.com/apple-oss-distributions/xnu/blob/xnu-11215.81.4/osfmk/vm/vm_fault.c#L8043-L8108)
- [repository-pinned Lilu source](https://github.com/acidanthera/Lilu/tree/e4748cc081bf060302c7d3c44a643ce1d11b7e1d)
- [repository-pinned MacKernelSDK source](https://github.com/acidanthera/MacKernelSDK/tree/05094e5e88cec7caedbfb35e8449ed0db94bf95b)

## State and concurrency model

There is no cross-callback search state and therefore no vnode lifetime,
completion, cleanup, or stale-pointer problem. Policy and catalogue data are
immutable compile-time objects. Runtime controls are set once during plugin
startup.

The pure scan uses stack locals only. The existing Lilu kernel write lock
serialises write-protection changes. After acquiring it, the complete policy
selection is repeated and must produce the same PatchDefinition and file
offset. Any difference is a concurrent-change rejection.

The verified replacement plus untouched original tail is the handled marker.
Subsequent callbacks recognise `AlreadyPatched` and do not write again. Dry-run
does not acquire the write lock or modify this marker.

## Bounds

- Maximum callback/search window: 4096 bytes.
- Window start: 4096-byte aligned.
- Maximum variants: 128.
- Maximum allowed PatchDefinitions per variant: 8.
- Context and function must be complete inside the window.
- Search positions use checked size addition and subtraction.
- The scan stops on the second match because uniqueness is already disproved.
- Null buffers, incomplete callback ranges, invalid definitions, oversized
  replacements, cross-page windows, and arithmetic inconsistencies fail closed.

The worst-case pure scan is bounded by 4096 candidate positions times eight
small compiled definitions. There are no attacker-sized allocations.

## Discord policy

The experimental Discord Stable/Krisp policy searches only:

```text
0x650000..<0x651000
```

It requires the existing Stable path grammar, basename `discord_krisp.node`,
signing identifier `discord_krisp`, Team ID `53Q6R32WPB`, and valid runtime
signing. It intentionally has no required application version, CDHash, or exact
target offset. Its only allowed definition is:

```text
mkl-serv-intel-cpu-true-oneapi-build-20201104-x86_64-v1
```

`-imklfxwindow` is required. Strict Discord 0.0.403 policy remains separate and
wins when its stronger identity evidence also matches.

## Failure cases

The mode rejects when identity fails, the boot gate is absent, the callback
does not contain the entire window, a definition is invalid, no complete match
exists, more than one complete match exists, bytes are partially patched, the
candidate crosses a boundary, bytes change before the write, or post-write
verification fails.

Moving the implementation to another page is not a successful search. It
requires an updated reviewed window policy and kext release.

## Host tests

Pure tests cover zero, unique, two, and many matches; first/final positions;
truncated context/function cases; every single-byte function/context mutation;
null and empty inputs; overflow boundaries; already/partially patched forms;
unknown definitions; disabled boot gate; strict non-fallback; and a second
synthetic application reusing the same definition.

Catalogue tests cover Discord version/path changes, arbitrary CDHash changes,
offset movement within the page, wrong signing identity and Team ID, malformed
paths, non-whitelisted paths containing matching bytes, and movement outside
the page.

## Readiness

The pure implementation is ready for Phase 6 build, static analysis, and
manual dry-run validation. This document does not claim that the kext has been
built or exercised in a running kernel, and Phase 6 must not be considered
complete until those separate steps pass.
