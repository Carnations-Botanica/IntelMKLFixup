# Whitelist and targeting design

## Product boundary

IntelMKLFixup is a generic runtime patcher for named, reviewed x86_64
implementations of `_mkl_serv_intel_cpu_true`. Discord Stable/Krisp is the
first built-in application rule and controlled-test image; it is not part of
the patch engine's identity.

Every patch decision requires all three independently reviewed layers below.
No layer is sufficient by itself.

```text
ApplicationRule ──┐
                  ├── ImageVariant/ApplicationPatchRule ── PatchDefinition
image evidence ───┘
```

The callback fails closed if a path, image identity, match mode, patch
definition, callback range, or byte sequence is missing, unknown, ambiguous,
or inconsistent.

## Callback evidence

The Darwin 24 `_cs_validate_page` callback supplies a vnode, file offset, page
mapping, and XNU validation results. It does not supply a trustworthy owner
process, bundle identifier, task identity, or dyld image path. `proc_self()`
would identify the current execution context, not necessarily the process that
will consume a shared file-backed page, so process names do not authorise a
patch.

The current implementation uses only evidence available synchronously:

- a regular vnode and a bounded path returned by `vn_getpath`;
- the callback's file offset and explicitly supplied page bytes;
- XNU's validated, untainted, executable result;
- the code blob covering that vnode and offset;
- exact code-signing identifier, explicit Team ID policy, signing-policy
  profile, and 20-byte CDHash; and
- a compiled MKL signature and complete surrounding context.

Bundle IDs, host-process names, arbitrary filesystem reads, Mach-O header
lookups from another page, and full-file hashing are not used in this hot path.
Required private XNU symbols remain Darwin-version gated; failure to resolve
any one disables routing.

## Layer 1: ApplicationRule

An `ApplicationRule` identifies an approved application or native-module
family. It contains:

- stable application-rule and path-rule identifiers;
- a human-readable name;
- exact basename;
- a bounded path-rule matcher;
- exact code-signing identifier;
- Team ID policy: `Exact` or `Absent`; and
- a compiled signing-policy profile.

`matchApplicationRule` is the generic bounded entry point. It validates common
bounds and the exact basename, then invokes the rule's path matcher. The
Discord path grammar is one function pointer stored in one rule; the callback
does not call or name it.

Remote data can select only path and signing-policy identifiers already
compiled into a reviewed release. It cannot supply a regular expression,
function pointer, parser, or executable rule.

### Initial Discord rule

The sole built-in application rule accepts these exact layouts:

```text
/Users/<account>/Library/Application Support/discord/
  app-<major>.<minor>.<build>/modules/
  discord_krisp-<revision>/discord_krisp.node

/Users/<account>/Library/Application Support/discord/
  app-<major>.<minor>.<build>/modules/
  discord_krisp-<revision>/discord_krisp/discord_krisp.node
```

The account component and numeric fields are bounded. Discord PTB, Canary,
renamed files, other Electron applications, extra path components, and copies
outside this layout are rejected. A changing Discord directory version does
not broaden binary approval because the image variant remains exact.

## Layer 2: PatchDefinition

A `PatchDefinition` describes one reviewed MKL implementation and contains no
application-specific names or assumptions. The first definition is:

```text
mkl-serv-intel-cpu-true-oneapi-build-20201104-x86_64-v1
```

It records:

- x86_64 architecture;
- exact 23-byte function implementation;
- exact 16-byte context before and after it;
- replacement `B8 01 00 00 00 C3` (`mov eax, 1; ret`);
- Intel oneAPI MKL build `20201104`; and
- exact-context and replacement-length requirements.

Discord appears only in the associated image variant's consumer notes. If a
future Photoshop, Lightroom, DaVinci Resolve, or other approved module embeds
the same reviewed implementation, it may refer to this definition without any
change to the patch engine. A different MKL implementation requires a new,
separately named compiled definition and review.

The catalogue accepts no approximate match. Masks are structurally present for
future reviewed relocation cases, but the current definition has none and the
engine rejects non-empty masks. Replacement length must not exceed the verified
function sequence. Only the replacement's six bytes are written; the remainder
of the original function is checked and left unchanged.

## Layer 3: ImageVariant/ApplicationPatchRule

An `ImageVariant` binds one application rule to one or more compiled patch
definitions and exact binary evidence:

- stable image-variant identifier;
- application-rule identifier and observed application version;
- architecture and exact CDHash;
- explicit match mode;
- reviewed executable file range;
- strict target file offset when applicable; and
- a bounded list of allowed compiled patch-definition identifiers.

The initial variant is the Discord Stable 0.0.403 Krisp x86_64 fixture. Its
consumer-specific CDHash, executable bounds, signing evidence, and target
offset belong here—not in the MKL patch definition.

The user has stated that the installed Discord module remains patched on disk
by the separate Swift application. This refactor did not inspect or modify it.
Any future evidence capture must continue to distinguish patched and original
hashes and follow the controlled Phase 6/test plan.

## Match modes

### Strict variant mode — enabled

The first Discord live test uses strict variant mode:

1. require an approved `ApplicationRule` path and basename;
2. require its exact signing policy, signing identifier, Team ID policy, and
   image-variant CDHash;
3. require the callback page to contain the variant's reviewed target offset;
4. require that offset to lie inside the reviewed executable range;
5. evaluate only the variant's allowed compiled patch definitions at that one
   offset;
6. require exactly one complete original or already-patched match; and
7. require exact search bytes and complete before/after context inside the
   callback range.

Zero matches reject. More than one matching patch definition rejects as an
ambiguous catalogue. A split or truncated signature is declined; no adjacent
page is read.

### Reviewed search mode — represented but disabled

The data model explicitly contains `ReviewedSearch`, but
`ReviewedSearchModeEnabled` is `false`, the kernel range prefilter ignores such
variants, and the userspace validator rejects `reviewed_search` manifests.

Before separate approval, a future implementation must have host tests proving
that it:

- starts only after full application and native-module identity approval;
- scans only a reviewed executable range supplied by the callback;
- never joins signatures across callback ranges;
- requires exactly one complete match for an allowed compiled definition;
- rejects zero and multiple matches; and
- remains deterministic and strictly bounded.

No reviewed-search implementation or hidden enable switch exists today.

## Signed whitelist and runtime availability

Schema version 2 mirrors the three-layer design with `application_rules` and
`image_variants`. Patch definitions remain source-only and compiled into the
signed kext. A signed manifest may select compiled patch IDs and carry bounded
identity, CDHash, match mode, target offset, and executable-range metadata. It
has no fields for search bytes, replacement bytes, masks, scripts, URLs, or
kernel code.

The current updater authenticates, validates, reports, and atomically stores
this policy in userspace. The running kext does **not** consume that store, so
installing a manifest has no runtime effect.

For the initial release, the safest practical policy transport is therefore a
compiled-in catalogue. An application identity update requires a reviewed kext
release. This deliberately chooses an auditable boot-time constant over file
I/O, JSON parsing, mutable state, or an unauthenticated userspace handoff in the
page-validation path.

The intended later no-core-rebuild path is a bounded binary boot policy derived
from the authenticated manifest and verified before callback registration. It
must use a separately reviewed, real OpenCore/kernel transport and authenticate
the payload with a public key compiled into the kext. The kext must parse it
once at startup into fixed-capacity storage, freeze it before routing, and fall
back to the built-in catalogue on any error. No such transport is implemented
or claimed safe yet; its exact OpenCore carrier, size limits, retrieval API,
signature verifier, and failure semantics require a dedicated audit before
code is written.

A post-boot IOUserClient handoff is not selected initially: it adds client
authentication, lifecycle, locking, timing, and attack surface, and could miss
images validated before policy installation. Runtime filesystem parsing is
also rejected. Networking remains userspace-only.

## Non-functional Photoshop example

This conceptual example shows the linkage only. It is deliberately not valid
for release: the path-rule ID is not compiled, the signing evidence and CDHash
are placeholders, and no Photoshop version has been inspected or tested.

```yaml
application_rules:
  - id: adobe-photoshop-example
    application_family: adobe-photoshop
    display_name: Adobe Photoshop native MKL module (EXAMPLE ONLY)
    path_rule_id: adobe-photoshop-versioned-module-path-v1  # not compiled
    basename: <reviewed-native-module-basename>
    signing_identifier: <reviewed-signing-identifier>
    team_identifier_policy: exact
    team_identifier: <reviewed-team-id>
    signing_policy_id: valid-runtime-no-adhoc-v1

image_variants:
  - id: adobe-photoshop-example-x86_64
    application_rule_id: adobe-photoshop-example
    application_version: <reviewed-version>
    architecture: x86_64
    cdhash: <reviewed-20-byte-cdhash>
    match_mode: strict_variant
    target_file_offset: <reviewed-offset>
    executable_range:
      start: <reviewed-start>
      end: <reviewed-end>
    allowed_patch_definition_ids:
      - mkl-serv-intel-cpu-true-oneapi-build-20201104-x86_64-v1
```

This does not assert that Photoshop contains that MKL generation or that the
patch is safe for it.

## Adding a second application today

Supporting a second application currently requires both a manifest update and
a kext rebuild:

1. obtain an original image from known provenance and record its signature,
   CDHash, architecture, UUID, full-file hash, executable ranges, and version;
2. add or reuse a bounded `ApplicationRule` path matcher and signing profile;
3. prove the strict target offset and exact compiled MKL definition/context;
4. add an `ImageVariant` that links the application rule to allowed patch IDs;
5. add positive, mutation, truncation, wrong-path, wrong-signature, wrong-CDHash,
   offset, ambiguity, and already-patched host tests;
6. add the same layered identity records to a new signed manifest version; and
7. build, review, sign, and release the kext and manifest together.

If the MKL function differs, a new compiled `PatchDefinition` and its own
review are also required. The callback and patch engine do not need to be
rewritten.
