# Architecture

IntelMKLFixup is a Lilu plugin for x86_64 AMD systems running Darwin 24. It
routes `_cs_validate_page`, calls the original implementation first, and then
examines only callbacks that can cover a compiled catalogue target.

The policy has three independent layers:

1. `ApplicationRule` validates the exact path grammar, basename, signing
   identifier, Team ID policy, and signing state.
2. `ImageVariant` validates architecture, CDHash, executable range, match mode,
   and either an exact offset or a bounded research window.
3. `PatchDefinition` validates the reviewed MKL implementation, replacement,
   and exact before/after context.

The Discord strict rule requires all three layers. Failure at any layer is a
rejection; there is no broad scan or active fallback.

## Validation-page flow

On Darwin 24, the hook receives a vnode, pager, page file offset, page address,
and validation flags. The plugin requires a regular file, an approved callback
range, successful XNU validation, no taint/NX result, and an available code-signing
blob. It extracts signing identity and CDHash from XNU and performs allocation-free
policy matching.

The operating gate is evaluated only after a complete byte match. Default mode
reports `file-backed-write-not-acknowledged`. Dry-run reports the match without
writing. Acknowledged active mode reaches the single
`applyAcknowledgedStrictFileBackedPatch` function only for `StrictVariant`.
That function revalidates the target after acquiring the kernel write lock,
writes exactly the compiled replacement, verifies the resulting bytes, and
restores kernel write protection.

## Bounded research mode

`BoundedWindow` can inspect one compiled page-sized window when explicitly
enabled. It requires a unique complete signature and context within that
window. It cannot actively write, cannot scan an image, and is not a fallback
used by the strict write path.

## Trust boundaries

The kext contains no networking. Userspace whitelist tooling authenticates and
stores metadata, but the kernel release uses compiled catalogue data. Remote
metadata can refer only to compiled path, signing, and patch-definition IDs;
the schema cannot carry search bytes, masks, replacements, or executable code.
