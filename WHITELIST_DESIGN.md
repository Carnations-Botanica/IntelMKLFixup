# Whitelist design

## Status and scope

This document is the Phase 2 design for IntelMKLFixup. It does not change the
kernel extension, approve a patch definition, or authorise installation. Phase
3 must not start until this design and the Phase 1 audit are approved.

The design is intentionally fail-closed. A page is eligible only when all of
the following independently agree:

1. the running OS and architecture are supported;
2. the callback identifies an approved file-backed native image;
3. the image has an approved code-signing identity and exact CodeDirectory
   hash for the slice covering the callback offset;
4. the callback covers the one reviewed file offset for that image variant;
5. the complete named MKL signature and its required surrounding context are
   present at that offset; and
6. the original XNU validation completed successfully before any change.

There is no fallback from a missing strong identifier to a process name, broad
path substring, relaxed mask, or whole-page scan.

## Evidence base

This design was checked against:

- the repository source audited in `AUDIT.md`;
- Lilu source revision `8e8eb256c5d8fa50d44b49255a55f093132d2f25`,
  corresponding to the Lilu source state used for the Phase 1 API audit;
- public XNU Sequoia source revision
  `e3723e1f17661b24996789d8afc084c0c3303b26`;
- the installed macOS 15.7.7 build 24G720 / Darwin 24.6.0 x86_64 kernel
  (`xnu-11417.140.69.710.16~1`); and
- the locally installed Discord Stable 0.0.403 files, inspected without
  modifying or launching them.

The exact running XNU source revision is not public in the inspected Apple
repository. The local kernel symbol table was therefore checked as a second,
runtime-specific source of evidence. It exports `_cs_validate_page`,
`_csvnode_get_blob`, `_csblob_get_cdhash`, `_csblob_get_identity`,
`_csblob_get_teamid`, and `_csblob_get_flags` on this host. Availability on a
future OS must be re-established; it must not be inferred from this result.

Primary source references:

- [Lilu `ProcInfo` and binary callback types](https://github.com/acidanthera/Lilu/blob/8e8eb256c5d8fa50d44b49255a55f093132d2f25/Lilu/Headers/kern_user.hpp#L140)
- [Lilu execution listener](https://github.com/acidanthera/Lilu/blob/8e8eb256c5d8fa50d44b49255a55f093132d2f25/Lilu/Sources/kern_user.cpp#L94)
- [Lilu path matching and process callback](https://github.com/acidanthera/Lilu/blob/8e8eb256c5d8fa50d44b49255a55f093132d2f25/Lilu/Sources/kern_user.cpp#L276)
- [XNU vnode path API](https://github.com/apple-oss-distributions/xnu/blob/e3723e1f17661b24996789d8afc084c0c3303b26/bsd/sys/vnode.h#L2077)
- [XNU code-signing vnode/blob accessors](https://github.com/apple-oss-distributions/xnu/blob/e3723e1f17661b24996789d8afc084c0c3303b26/bsd/sys/codesign.h#L107)
- [XNU accessor implementations](https://github.com/apple-oss-distributions/xnu/blob/e3723e1f17661b24996789d8afc084c0c3303b26/bsd/kern/kern_cs.c#L597)
- [XNU code-sign page callback](https://github.com/apple-oss-distributions/xnu/blob/e3723e1f17661b24996789d8afc084c0c3303b26/bsd/kern/ubc_subr.c#L6053)

## What the callback actually identifies

On Darwin 24 the routed `_cs_validate_page` callback supplies:

- the vnode for the file-backed object;
- the pager;
- the file offset being validated;
- a kernel mapping of the page bytes; and
- validation, taint, and no-execute output fields.

It does **not** supply a `proc_t`, PID, task, `vm_map_t`, process executable
path, bundle identifier, audit token, or dyld loaded-image record. The vnode
and offset identify the image being validated much more directly than the
currently executing thread identifies a consumer of that image.

The page-fault path can operate with switched maps, and a validated file-backed
page can later be shared by more than one process. `proc_self()`,
`proc_selfpid()`, `proc_name()`, and `proc_selfname()` exist as kernel KPIs, but
in this callback they describe the current execution context, not a proven
owner of the vnode page. They must not grant patch permission.

Lilu's `onProcLoad` facility does provide a process executable path and the new
task's `vm_map_t`. Its source shows that the path originates from a
`KAUTH_FILEOP_EXEC` listener. The callback therefore describes the main image
at `exec`, not every native module later mapped by `dlopen`. Associating a Lilu
exec callback with a later `_cs_validate_page` call would require retained
task/map state, lifecycle handling, synchronisation, and a reliable map or task
identifier in the validation callback. The current callback has none of those.
Lilu also notes that `current_map()` has not always meant the current process's
map. The initial design will not build an authorisation boundary from that
ambiguous association.

## Identifier assessment

| Identifier | Available in this callback? | Reliability and intended use |
|---|---:|---|
| Loaded-image vnode | Yes | Strongest object anchor. Use only synchronously; never retain the pointer. |
| Loaded-image path | Usually | `vn_getpath` is a documented KPI and does not enter the filesystem. It can fail, and hard links can make a path ambiguous. Required as a structural restriction, never sufficient alone. |
| Binary filename | Usually | Derived from the approved full path. Exact `discord_krisp.node` basename is required, but basename alone is weak. |
| Callback file offset | Yes | Strong and cheap. It selects the code-signing blob for the correct fat-binary slice and the one reviewed patch location. |
| CodeDirectory hash (CDHash) | Yes on the tested Darwin 24 kernel | Strong content identity for the signed slice. Require an exact 20-byte approved value from the code blob covering the callback offset. It is not a publisher identity. |
| Code-signing identifier | Yes on the tested Darwin 24 kernel | Require an exact bounded string. The observed module identifier is `.discord_krisp.node`. Missing or malformed values reject. |
| Team identifier | Yes on the tested Darwin 24 kernel, but nullable | Strong publisher evidence when present. The observed module is ad-hoc signed and has no Team ID, so the parent app's Team ID cannot be attributed to it. Each rule must require either an exact Team ID or explicitly require absence; there is no “ignore” mode. |
| Code-signing flags | Yes on the tested Darwin 24 kernel | Supplementary validation. Require the expected validity/signing policy for each variant, but do not substitute flags for CDHash. |
| Process name | Technically obtainable | Short, spoofable, and not reliably associated with the vnode page. Debug-only and only in explicit verbose mode. |
| Process executable path | Not supplied | Lilu has it at `exec`, but no safe association with this later native-module callback is established. Not an authorisation input initially. |
| Process Team ID/signing identity | Not reliably associated | Private XNU accessors can describe `proc_self`, but `proc_self` is not a proven consumer/owner here. Not an authorisation input. |
| Bundle identifier | No | Stored in userspace bundle metadata, not supplied for a native module page fault. Do not read `Info.plist` in this path. |
| Mach-O UUID | Not safely available from an arbitrary page | Usually resides in the Mach-O header. Reading another file range or relying on a header page arriving first would add I/O/state to the fault path. Record it in review metadata, but do not use it as an initial runtime gate. |
| Full-file SHA-256 | No safe hot-path mechanism | Hashing the file would require filesystem reads, allocations, and unbounded work. Use in userspace review, release manifests, and test records only. CDHash is the runtime content anchor. |
| Vnode generation (`vnode_vid`) | Yes | Useful only when safely keying a short-lived cache with a held vnode reference. It is not persistent identity and no cache is required by this design. |
| File size/attributes | Obtainable through VFS calls | Not required. `vnode_getattr` can enter filesystem code and adds avoidable work in the validation path. Keep size as offline review metadata. |
| MKL signature/context | Yes, within the supplied bytes | Always mandatory. It proves only the reviewed machine-code form, so it is combined with image identity and exact file offset. |

### Why CDHash is usable here

XNU exposes `csvnode_get_blob(vnode, offset)`. It selects the code-signing blob
covering that file offset; the blob accessors then expose its fixed-length
CDHash, identity, Team ID, and flags. In public Sequoia XNU,
`ubc_cs_blob_get` takes the vnode spin lock while choosing a blob. XNU also
documents that during page-fault signature validation the VM paging reference
keeps the vnode pager and UBC information alive. The plugin must nevertheless
use returned pointers immediately, perform bounded comparisons, and retain
nothing after the callback.

These are XNU-private interfaces rather than stable Lilu APIs. Phase 3 may use
them only through explicit, version-checked symbol resolution. If any required
symbol is absent or the tested ABI does not match, startup must disable the
patch route and report one error. It must not fall back to path-only matching.

### Limits of an ad-hoc-signed module

The installed `discord_krisp.node` is ad-hoc signed, has the signing identifier
`.discord_krisp.node`, and has no Team ID. Discord.app and its Renderer helper
are instead signed with Team ID `53Q6R32WPB`, but that parent/helper identity is
not inherited by the separately signed native module and is not available from
the module vnode.

An exact SHA-256-based CDHash still prevents a changed ad-hoc-signed module
from matching an approved record, absent a cryptographic collision. It does
not prove that Discord, Inc. published the file. A byte-for-byte copy of an
approved module can carry the same CDHash, but the only eligible change remains
the reviewed gate at the reviewed offset in that exact module content.

## Discord Stable rule

The initial built-in whitelist is Discord Stable only. Discord PTB, Discord
Canary, development builds, other Electron applications, renamed samples, and
copies outside the approved layout are excluded.

The kernel code should use a bounded component parser, not a regex engine or
general glob matcher. Two explicit layouts may be recognised because both the
user-supplied layout and the locally observed Discord layout need to be
accounted for:

```text
/Users/<account>/Library/Application Support/discord/
  app-<major>.<minor>.<build>/modules/
  discord_krisp-<module-revision>/discord_krisp.node

/Users/<account>/Library/Application Support/discord/
  app-<major>.<minor>.<build>/modules/
  discord_krisp-<module-revision>/discord_krisp/discord_krisp.node
```

The second form is present locally for Discord Stable 0.0.403. Matching rules:

- the path is absolute, NUL-terminated within `MAXPATHLEN`, and returned
  successfully by `vn_getpath`;
- `vnode_vtype(vp)` is `VREG`;
- every literal component above is case-sensitive and exact;
- `<account>` is one non-empty component and may not be `.` or `..`;
- each version component and `<module-revision>` contains ASCII decimal digits
  only and is length-bounded;
- no extra, empty, `.` or `..` components are allowed;
- the base directory is exactly `discord`, not `discordptb` or
  `discordcanary`; and
- the basename is exactly `discord_krisp.node`.

This deliberately does not pin the directory to one Discord version number.
Version independence comes from the structural path parser; binary approval is
still exact and version-specific through the CDHash and file offset. A new
Discord release with an unknown module CDHash is rejected until reviewed.

The username is not stored in the built-in catalogue and should be redacted as
`~` in normal logs. Full paths may appear only in explicit verbose mode.

## Approved image variants

The path rule identifies a candidate family. An image-variant record then
authorises one exact native-module build:

```text
ImageVariant
  stable identifier
  application rule identifier
  target architecture (x86_64)
  exact 20-byte CodeDirectory hash
  exact code-signing identifier
  Team ID policy: exact value or required-absent
  required code-signing flags
  expected patch file offset
  reviewed executable Mach-O segment/section bounds
  compiled patch-definition identifier
  expected match policy (exactly one location)
  optional review-only Mach-O UUID
  optional review-only full-file SHA-256 and size
  known Discord/MKL version notes
```

The user confirmed that the locally installed module remains patched by the
separate Swift MKL patcher. It currently reports:

```text
Discord version:       0.0.403
Architectures:         x86_64, arm64
Signing identifier:    .discord_krisp.node
Signing form:          ad hoc
Team ID:               absent
Observed CDHash:       06c3f5729e91b29579ef153859cba5daae83d3d8
Observed x86_64 UUID:  9F796A90-D4AE-3E1B-A748-620B623EA9AA
Observed file SHA-256: 2de4c18ae1cab371df8e786d88492ea06d88f8d1763f785e35f27e624bc0c6cd
```

These values describe the **patched on-disk state** and are **not approved
built-in values**. In particular, the observed CDHash and full-file SHA-256
must not be mistaken for the original module's identities; an on-disk change
and any ad-hoc re-signing can change them. The observed UUID may survive a code
edit, but it is still only corroborating metadata and is not trusted by itself.
Phase 3 must obtain the restored original only when the manual test workflow
calls for it, verify its signature, compare it with the known-working Swift
patch, record both original and patched hashes, locate the exact x86_64 file
offset, and review surrounding instructions. Until that evidence exists, the
Discord rule has no enabled image variant and actual patching must remain
impossible.

## Patch decision algorithm

The bounded Phase 3 decision should be:

1. Detect x86_64, `AuthenticAMD`, and an explicitly supported Darwin version
   before resolving or routing anything. The first controlled implementation
   should target Darwin 24 only.
2. Resolve the route and every required vnode code-signing accessor with the
   exact verified ABI. Any failure disables patching before route activation.
3. In the callback, call the original XNU function exactly once and first.
4. Continue only if the original page is reported validated, untainted, and
   not marked no-execute under the verified Darwin 24 callback contract. Also
   require the target offset to lie in the variant's reviewed executable
   Mach-O segment/section bounds; the callback result alone is not segment
   metadata.
5. Validate all pointers and callback-size assumptions. Do not read outside the
   bytes explicitly supplied by XNU.
6. Require a regular vnode and a successful strict Discord Stable path match.
7. Obtain the code blob covering `page_offset`. Require exact CDHash, signing
   identifier, Team ID policy, and signing flags for one enabled image variant.
8. Select the one compiled patch definition named by that image variant.
9. Use `page_offset` and the variant's reviewed target file offset to calculate
   a single candidate address with checked arithmetic. Do not scan the page.
10. Require the complete search bytes and all required before/after context to
    fit within this callback range. If they cross a boundary, decline to patch.
11. Require exact bytes and exact context. Masks may exist only where a named
    patch definition has a reviewed relocation field; they may not be broadened
    at runtime.
12. Require the already-patched form to be absent. Recheck the original bytes
    immediately before the write, change only the reviewed replacement length,
    verify the result, and record the matched image-variant and patch IDs.

Using a reviewed file offset removes the need to search every candidate page.
The policy is exactly one permitted location per image variant. A matching byte
sequence at another offset is irrelevant and never modified. Arithmetic
failure, truncated context, duplicate catalogue records, contradictory
metadata, or any unknown state rejects the callback.

The exact MKL search bytes from Phase 1 remain necessary, but they are not yet
sufficient as a Phase 3 patch definition. The known-good original must provide
the fixed RIP-relative displacement, surrounding function context, function
boundary evidence, exact replacement, and target offset. No unknown MKL
variant is patched.

## Helpers and native modules

Discord normally loads the Krisp native module in an Electron helper, commonly
the Renderer helper rather than the main `Discord` executable. A rule based on
only `Discord` would therefore be both spoofable and incomplete. A list of all
helper short names would remain spoofable.

The initial rule follows the native module itself. Any process mapping the
approved vnode sees only the reviewed in-memory change to that exact approved
image. This naturally handles Discord helper-process changes without accepting
other binaries or pretending the callback proves a host process identity.

This is an **image whitelist**, not a guarantee that the consumer process is a
Discord process. If host-process exclusivity is a hard requirement, the current
callback is insufficient. That would require a separately audited mapping/load
hook that provides both a stable task identity and the loaded image vnode, plus
safe lifetime tracking. It is materially more invasive and is not proposed for
the first controlled implementation.

## Failure behaviour and diagnostics

When identity cannot be established safely, no patch occurs. Specifically:

- `vn_getpath` failure: reject;
- path ambiguity or grammar mismatch: reject;
- code blob or accessor unavailable: reject;
- missing, malformed, or unexpected signing identity: reject;
- Team ID contradicts the variant's exact/absent policy: reject;
- CDHash not in the built-in approved set: reject;
- unknown Discord or MKL version: reject;
- target offset not fully covered by this callback: reject;
- signature/context mismatch or already-patched bytes: reject; and
- more than one applicable catalogue record: catalogue error; disable patching.

Normal mode should log only lifecycle errors and successful/rejected candidate
events with stable identifiers. It must not log every validation callback.
Verbose mode may log a redacted path and non-sensitive signing metadata. No
code-signing pointer, kernel address, username, or arbitrary path should be
logged by default.

## Adding future applications or Discord builds

A future addition is a source-reviewed data change, not a wildcard expansion:

1. obtain the original, unmodified x86_64 image from a known provenance;
2. record full-file SHA-256, Mach-O UUID, architecture, size, CodeDirectory
   hash, signing identifier, Team ID, and signature status in the review;
3. define a strict component path rule or reuse an already reviewed rule;
4. prove the exact target file offset and executable Mach-O region;
5. map the image only to an existing reviewed patch definition, or review a new
   compiled patch definition as code;
6. add positive, one-byte-mutation, wrong-path, wrong-CDHash, wrong-offset,
   truncation, duplicate, and unknown-variant tests; and
7. document the application/MKL versions actually tested.

Remote whitelist updates considered in Phase 5 may update only application and
image identity records, and may refer only to patch-definition identifiers
already compiled into the installed plugin. They may not contain search bytes,
replacement bytes, masks, offsets that select unreviewed code, or executable
logic. Built-in-only operation remains the trusted fallback. Runtime loading of
an external manifest is not assumed safe by this design and must be separately
proved before it is implemented.

## Recommended Phase 3 boundary

Proceed only after the following are agreed and supplied:

- Darwin 24/macOS 15 is the sole initial OS target;
- Discord Stable is the sole initial application family;
- image identity is accepted as the authorisation boundary, with process name
  used only for diagnostics;
- the original, restored `discord_krisp.node` can be inspected alongside the
  Swift patcher's exact known-working definition; and
- the original module produces a reviewed CDHash, signing identity, exact
  x86_64 target offset, and sufficient surrounding instruction context.

If host-process identity must also be authoritative, stop before Phase 3 and
design a different, more invasive load/mapping architecture. The current XNU
validation callback cannot safely provide that guarantee.
