# Whitelist and targeting design

## Product boundary

IntelMKLFixup is an application-independent runtime patcher for named, reviewed
x86_64 Intel MKL vendor-gate implementations. Discord Stable/Krisp is the first
compiled application policy and test fixture.

Every patch requires all three layers:

```text
ApplicationRule ──┐
                  ├── ImageVariant ── allowed compiled PatchDefinitions
callback bytes ───┘
```

Missing, unknown, ambiguous, disabled, or inconsistent evidence fails closed.

## Available callback evidence

Darwin 24 `_cs_validate_page` supplies a vnode, pager, file offset, one mapped
page, and XNU validation results. The plugin can synchronously obtain a bounded
vnode path and code-blob signing evidence. It cannot infer a trustworthy owning
process, bundle identifier, complete loaded image, or validation completion.

The implementation uses:

- exact bounded path grammar and basename;
- exact signing identifier and Team ID policy;
- code-valid, runtime, and ad-hoc signing flags;
- exact CDHash only when the selected variant requires one;
- the callback file offset and supplied 4 KiB bytes; and
- compiled MKL function bytes and exact context.

It does not read arbitrary vnode contents, join pages, retain callback pointers,
or scan unrelated executable mappings.

## ApplicationRule

An ApplicationRule contains no MKL bytes. It defines a stable ID, bounded path
matcher, exact basename, signing identifier, Team ID policy, and signing-policy
profile. `matchApplicationRule` is generic; application-specific grammars are
compiled function pointers in the catalogue.

The Discord Stable rule accepts only versioned Stable paths beneath:

```text
/Users/<account>/Library/Application Support/discord/app-<n>.<n>.<n>/modules/
  discord_krisp-<n>/discord_krisp.node

/Users/<account>/Library/Application Support/discord/app-<n>.<n>.<n>/modules/
  discord_krisp-<n>/discord_krisp/discord_krisp.node
```

It requires basename `discord_krisp.node`, signing identifier `discord_krisp`,
Team ID `53Q6R32WPB`, and valid hardened-runtime non-ad-hoc signing. Canary,
PTB, malformed numeric components, renamed modules, and unrelated paths fail.

See `docs/APPLICATION_RULES.md`.

## PatchDefinition

Patch definitions are application-independent and compiled into reviewed kext
source. The current definition is:

```text
mkl-serv-intel-cpu-true-oneapi-build-20201104-x86_64-v1
```

It records an exact 23-byte oneAPI MKL implementation, exact 16-byte context on
both sides, x86_64 architecture, and replacement `B8 01 00 00 00 C3`
(`mov eax, 1; ret`). Masks are not enabled. A new MKL implementation requires a
new definition and kext release.

See `docs/PATCH_DEFINITIONS.md`.

## ImageVariant and match modes

An ImageVariant associates one ApplicationRule with one or more allowed
PatchDefinitions. Its mode is explicit; no mode silently broadens into another.

### StrictVariant

StrictVariant requires a fixed target offset and any configured CDHash. It
checks only the allowed definitions at that offset. It never scans and never
falls back to BoundedWindow. Discord 0.0.403 remains the initial exact fixture.

### BoundedWindow

BoundedWindow is enabled only with `-imklfxwindow`. It has no fixed target
offset and may omit application version and CDHash metadata.

The policy supplies an executable range and one start-aligned search window no
larger than 4096 bytes. The callback must contain that entire window. Only after
path and signing approval does the engine search every valid position for each
allowed compiled definition. Complete exact function and context bytes count as
a match. Zero or multiple matches within the window reject.

This is window-local uniqueness, not image-wide uniqueness. No claim is made
about code outside the window, and candidates crossing a boundary are ignored.
An update is eligible only if the recognised implementation remains uniquely
inside the same page.

The initial Discord window is:

```text
0x650000..<0x651000
```

The current known strict target `0x650100` lies inside it. The window is source
evidence, not a promise that every future Discord version will retain that
layout.

### Future ImageScan

`image_scan` is a reserved manifest term for a future authenticated userspace
pre-scan. The current validator rejects it and the kernel has no ImageScan mode.
See `docs/USERSPACE_PRESCAN_DESIGN.md`.

## Signed manifest and runtime status

Schema version 3 represents application rules, strict variants, bounded-window
variants, and the reserved ImageScan mode. It permits optional version/CDHash
metadata and a bounded `search_window`, but has no machine-code, mask, script,
URL, or replacement fields. Patch identifiers must already be compiled into the
updater and kext release.

The updater authenticates and stores this manifest in userspace. The current
kext does not read that store. A manifest installation alone cannot add an app,
move a window, or enable a policy.

## Adding or updating support

A Discord update needs no policy change when its existing path/signing rule
passes and the exact supported MKL implementation remains unique inside the
compiled window. A changed Team ID, signing identifier, module path grammar, or
window page requires review and a compiled catalogue update.

A second application currently requires:

1. original binary provenance and signing evidence;
2. a bounded ApplicationRule/path grammar;
3. either an exact strict variant or reviewed one-page window;
4. association with an existing compiled PatchDefinition, or a new reviewed
   definition if its MKL implementation differs;
5. host tests and a schema/manifest update; and
6. a reviewed kext release.

The callback and core engine do not change when a second application reuses the
existing policy types.

## Non-functional Photoshop example

This example is conceptual only. No Photoshop binary, path, signature, window,
or MKL implementation has been reviewed.

```yaml
application_rules:
  - id: adobe-photoshop-example
    application_family: adobe-photoshop
    display_name: Adobe Photoshop module (EXAMPLE ONLY)
    path_rule_id: adobe-photoshop-module-path-v1  # not compiled
    basename: <reviewed-basename>
    signing_identifier: <reviewed-signing-id>
    team_identifier_policy: exact
    team_identifier: <reviewed-team-id>
    signing_policy_id: valid-runtime-no-adhoc-v1

image_variants:
  - id: adobe-photoshop-example-strict
    application_rule_id: adobe-photoshop-example
    application_version: <reviewed-version>
    architecture: x86_64
    cdhash: <reviewed-cdhash>
    match_mode: strict_variant
    target_file_offset: <reviewed-offset>
    executable_range: {start: <start>, end: <end>}
    search_window: null
    allowed_patch_definition_ids:
      - mkl-serv-intel-cpu-true-oneapi-build-20201104-x86_64-v1
```
