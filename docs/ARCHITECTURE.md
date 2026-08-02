# Architecture

## Purpose

IntelMKLFixup is an x86_64 AMD Hackintosh Lilu plugin that detects named,
reviewed implementations of Intel MKL's `_mkl_serv_intel_cpu_true` predicate.
The catalogue retains the reviewed `mov eax, 1; ret` replacement, but current
kernel code does not apply it. Numerical MKL functions are not redirected or
replaced.

Discord Stable/Krisp is the first application policy, not a dependency of the
patch engine.

## Policy layers

The allocation-free core in `IntelMKLFixupPolicy.hpp` has three independent
layers:

1. `ApplicationRule` approves a bounded module path, basename, signing
   identifier, Team ID policy, and code-signing policy.
2. `PatchDefinition` describes an exact reviewed MKL implementation, exact
   context, architecture, and replacement. It contains no application names.
3. `ImageVariant` associates an application rule with allowed compiled patch
   definitions and either strict binary evidence or one bounded search window.

`IntelMKLFixupCatalogue.hpp` supplies the compiled data. The callback consumes
only generic catalogue arrays and selectors; it does not name Discord.

## Darwin 24 callback boundary

The plugin routes `_cs_validate_page` only. The original function is invoked
exactly once before IntelMKLFixup examines the result. On x86_64, the callback
provides a vnode, a 4 KiB page offset, a **const kernel alias of the actual
external vnode-pager VM page**, and validation, taint, and NX bitmaps. It is not
a copied scratch buffer or a process-private executable mapping. It also does
not provide a complete Mach-O image or a signal that all executable pages have
been observed.

XNU 11417.140.69 maps the `vm_page` for validation and can use a direct
`phystokv` alias on the one-page x86_64 path. Hardware testing demonstrated
that writes through this alias change bytes returned by ordinary reads of the
backing file. The callback is therefore permanently classified as a read-only
detection boundary.

The callback never reads another page, opens the vnode, parses a Mach-O image,
or retains the callback data pointer. Both runtime modes decline candidates
whose complete function and context are not present in the supplied page.

## Runtime flow

```text
startup
  → require x86_64, AMD, and Darwin 24
  → resolve all required XNU code-signing symbols
  → route _cs_validate_page or remain inactive

validation callback
  → invoke original _cs_validate_page exactly once
  → bounded catalogue page prefilter
  → obtain regular-vnode path
  → match a compiled ApplicationRule
  → require XNU validated, untainted, executable result
  → obtain signing identifier, Team ID, flags, and CDHash
  → approve exactly one ImageVariant
  → run only that variant's explicit match mode
  → dry-run, reject, or recognise already-patched bytes
  → if active mode requested, block with unsafe-file-backed-write-blocked
  → report modified=no
```

No application mismatch or missing MKL implementation causes a deliberate
panic.

## StrictVariant

StrictVariant is the default and remains the first controlled-test mode. It
requires the configured CDHash, reviewed target file offset, executable range,
and one exact allowed patch definition with complete context. It performs no
search and never falls back to BoundedWindow.

The Discord 0.0.403 strict fixture remains unchanged apart from additive
structure fields used by the generic policy model.

## BoundedWindow

BoundedWindow is experimental and additionally requires `-imklfxwindow`.
Without that boot argument, its application identity may be recognised but no
search is performed. With the argument, search remains detection-only; no
validation page is writable.

Its exact guarantee is:

- one policy-declared window whose start is 4 KiB aligned;
- a non-empty window no larger than one x86_64 validation page;
- one callback must contain the complete window;
- application path and signing identity are approved before searching;
- only the variant's compiled PatchDefinitions are considered;
- exact function bytes and complete before/after context are required;
- zero matches in the window reject;
- exactly one complete original or already-patched match accepts;
- more than one match in the window rejects; and
- no statement is made about bytes elsewhere in the image.

The matcher performs at most 4096 candidate-position checks for each of at most
eight compiled patch definitions. It allocates nothing and stops as soon as a
second match proves ambiguity. Cross-window and cross-page candidates are
rejected.

Version and CDHash may be omitted from a BoundedWindow policy. This permits an
application update only when its path/signing rule still passes and the exact
reviewed MKL implementation remains uniquely inside the same approved window.
Movement to another page requires reviewed policy changes.

## Future ImageScan

ImageScan is reserved for a future userspace-assisted design. It is not a
kernel runtime mode, has no boot argument, and is rejected by the current
manifest validator. Its intended purpose is complete executable-section
inspection in userspace followed by an authenticated, binary-bound policy.
See `docs/USERSPACE_PRESCAN_DESIGN.md`.

## Validation-page safety boundary

The previous write path cast away `const`, disabled kernel write protection,
and copied into the callback pointer. Controlled hardware testing proved that
this mutated a vnode-backed resident page and cache-visible file contents. That
architecture is revoked; see `FILE_BACKED_WRITE_INCIDENT.md`.

Current code never calls `setKernelWriting`, never casts the callback bytes to
a mutable pointer, and never copies replacement bytes. Exact original matches
are reported in dry-run. In non-dry-run mode they produce:

```text
outcome=unsafe-file-backed-write-blocked modified=no
```

Already-patched bytes may still be detected for diagnosis, but no restoration
or additional mutation is attempted. `Tests/check_validation_callback_read_only.sh`
guards this boundary in CI. A future runtime engine must operate only on a
verified private/COW mapping belonging to the approved target process after
loading; it must not reuse this callback destination.

Writing through `_cs_validate_page` is **permanently forbidden**, including a
purported write-then-restore operation. Neither unchanged file timestamps nor
restoring the original byte sequence can make a vnode/UBC mutation
process-private.

## Replacement architecture status

Pinned Lilu 1.7.2's ordinary `BinaryModInfo` path is not a replacement: its
`performPagePatch` writes through the same code-signing validation buffer.
Lilu's process-load callback observes `exec`, not arbitrary later native-module
loads such as Electron's `discord_krisp.node`.

The safest conditional candidate is an authenticated component present in the
approved process before the native module loads. A controlled fixture verifies
that an image-add callback can run before library initializers, but admission
into hardened-runtime Discord and the actual Discord mapping properties remain
unresolved. The current feasibility investigation found no practical safe
admission, task-port, environment, or interposition route under the stated
constraints. Process-private post-map patching is **unproven research**, not a
replacement architecture. No implementation or write experiment is approved.

See [`PRIVATE_MAPPING_FEASIBILITY.md`](../PRIVATE_MAPPING_FEASIBILITY.md),
[`DISCORD_KRISP_RUNTIME_MAP.md`](../DISCORD_KRISP_RUNTIME_MAP.md),
[`LOAD_ORDER_ANALYSIS.md`](../LOAD_ORDER_ANALYSIS.md),
[`MEMORY_ONLY_ARCHITECTURE_OPTIONS.md`](../MEMORY_ONLY_ARCHITECTURE_OPTIONS.md),
and [`MEMORY_PATCH_THREAT_MODEL.md`](../MEMORY_PATCH_THREAT_MODEL.md) for the
evidence and remaining approval gate.

## Policy source

Runtime policy is currently compiled into the kext. The signed userspace
manifest mirrors policy for authentication, review, update, and future
transport work, but the running kext does not consume the installed store.
Installing a manifest therefore has no runtime effect.

A Discord update can be tolerated by the compiled BoundedWindow rule without a
new kext only when the function remains in its existing page. Moving the
function to another page, adding a new application/path grammar, or supporting
a new MKL implementation requires a reviewed catalogue change and kext
release.
