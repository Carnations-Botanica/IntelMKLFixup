# Architecture

## Purpose

IntelMKLFixup is an x86_64 AMD Hackintosh Lilu plugin that changes only named,
reviewed implementations of Intel MKL's `_mkl_serv_intel_cpu_true` predicate.
The supported implementation is replaced in validated executable memory with
`mov eax, 1; ret`. Numerical MKL functions are not redirected or replaced.

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
provides a vnode, a 4 KiB page offset, a pointer to that page, and validation,
taint, and NX bitmaps. It does not provide a complete Mach-O image or a signal
that all executable pages have been observed.

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
  → dry-run, reject, recognise already-patched bytes, or perform guarded write
  → revalidate and report the outcome
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
search or patch is performed.

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

## Patch write path

After a unique selection, the engine acquires Lilu's kernel write lock, enables
kernel writing, repeats the same strict or bounded selection, and requires the
same patch pointer and file offset. It writes only the compiled replacement
length and verifies the already-patched form before restoring protection.

The replacement itself is the idempotence marker: subsequent callbacks detect
the complete reviewed replacement plus untouched original tail and do not
write again. Dry-run returns before changing write protection.

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
