# Architecture

## Purpose

IntelMKLFixup is an x86_64 AMD Hackintosh Lilu plugin that changes only a
reviewed Intel MKL CPU-vendor predicate. A supported
`_mkl_serv_intel_cpu_true` implementation is replaced in validated executable
memory with `mov eax, 1; ret`. Numerical MKL entry points are not redirected or
replaced.

Discord is the first strict policy fixture, not a core-engine dependency.

## Components

### Lilu/XNU integration

`IntelMKLFixup.cpp` performs startup gating, resolves the required Darwin 24
code-signing symbols, routes `_cs_validate_page`, invokes the original function
first, converts XNU code-blob evidence into a generic `ImageIdentity`, and logs
bounded outcomes.

It knows only the built-in catalogue arrays and generic selector APIs. It does
not call a Discord matcher, hold a Discord variant directly, scan a process,
parse manifests, access the network, or read policy files.

### Policy engine

`IntelMKLFixupPolicy.hpp` is allocation-free and independent of Lilu/kernel
headers. It defines:

- `ApplicationRule`: approved path/basename/signing family;
- `PatchDefinition`: exact MKL implementation and replacement;
- `ImageVariant`: binary evidence and the allowed association between them;
- generic variant selection and strict patch selection.

The pure header is compiled directly by host tests. It contains no Discord
identifiers or path grammar.

### Reviewed catalogue

`IntelMKLFixupCatalogue.hpp` supplies the reviewed, compiled data consumed by
the engine: MKL-oriented `PatchDefinition` objects, application-rule
implementations, and their `ImageVariant` associations. Application-specific
path grammars are bounded rule implementations behind function pointers, so a
new consumer does not alter the callback or matching engine.

### Userspace policy tools

The Swift package verifies detached Ed25519 signatures, validates schema
version 2, downloads stable GitHub Release assets to temporary files, stages
authenticated policy atomically, rejects replay/downgrade conditions, and
supports rollback.

It has no kernel privileges and its installed store is not consumed by the
current kext. It cannot change runtime eligibility.

## Runtime flow

```text
kext startup
  → require x86_64 + AMD + Darwin 24
  → resolve every required XNU symbol
  → route _cs_validate_page or remain inactive

validation callback
  → invoke original _cs_validate_page exactly once
  → bounded catalogue offset prefilter
  → obtain regular-vnode path
  → match generic ApplicationRule
  → require XNU validated + untainted + executable result
  → obtain code blob for vnode/offset
  → select exactly one ImageVariant from signing evidence + CDHash
  → select exactly one allowed compiled PatchDefinition at strict offset
  → dry-run, already-patched skip, or guarded six-byte write
  → reclassify bytes and log the outcome
```

Every failure returns to the original validation flow without a deliberate
panic. No match is a normal result.

## Strict variant guarantees

Strict mode performs no byte search. The callback is considered only if it
contains a reviewed target file offset. Complete function bytes and context
must fit in that callback range and inside reviewed executable bounds. The
engine declines cross-range signatures and rejects multiple allowed definitions
that match the same target.

The original bytes are rechecked after enabling kernel writing. Only the
compiled replacement length is copied, and the resulting already-patched form
is verified before write protection is restored.

## Reviewed search state

Reviewed search is a named match mode but is compiled disabled. There is no
search loop in the callback and no boot argument that enables one. Enabling it
requires separate approval after complete pure host tests and callback-range
review.

## Policy sources

The runtime policy source is currently the catalogue compiled into the kext.
The signed userspace manifest mirrors and stages application/image policy but
does not affect the running plugin. Consequently, adding any application today
requires both a new signed manifest and a reviewed kext release.

The preferred future direction is a compact, signed, fixed-capacity boot policy
verified and frozen before route installation. It is intentionally not
implemented until a real OpenCore-to-kernel transport and its APIs, bounds,
authentication, and fallback semantics are independently verified. JSON and
networking will remain outside the kernel.

## Current application-specific code

Only these elements of `IntelMKLFixupCatalogue.hpp` are Discord-specific:

- `matchDiscordStableKrispPath`;
- `DiscordStableKrispApplication`;
- the Discord 0.0.403 CDHash and `ImageVariant`;
- its allowed-patch association and consumer notes; and
- Discord-focused testing/documentation procedures.

The MKL bytes, replacement, validation routines, catalogue selectors, callback,
write path, and userspace signature/store machinery are application-independent.
