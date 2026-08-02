# Validation-page file-backed write incident

## Status

**Active runtime patching in the 1.0.0-rc1 candidate is unsafe and revoked.**

Controlled Ryzen 9 3900X testing on macOS 15.7.7 / Darwin 24.6 demonstrated
that the six-byte replacement written through `_cs_validate_page` became
visible through ordinary reads of the backing `discord_krisp.node`. The
current source has therefore been changed so that this callback is detection
only. Dry-run can report an exact match; non-dry-run execution fails closed
with `outcome=unsafe-file-backed-write-blocked modified=no`.

## Reproduction evidence

The test began with the restored publisher binary:

```text
SHA-256: de061edb4387fc5bba2b8535483aa2f4347c17bc9e4d25babef36172c86a9f9a
offset 0x650100: 53 48 83 ec 20 8b
```

After one IntelMKLFixup active-mode test, ordinary file reads reported:

```text
SHA-256: 95e611b3bd89d95d67f1e809eb1cbefcc8eedbba3bd5b70bf11cf044cf72b27d
offset 0x650100: b8 01 00 00 00 c3
```

`cmp` reported exactly six changed bytes, corresponding to `mov eax, 1; ret`.
The inode was unchanged. File modification and change timestamps did not
advance.

The loaded release-candidate executable was version 1.0.0, UUID
`E815F179-7FE0-3AC8-B71D-AA9D58BEADF5`, built from repository commit
`7a0273326f570a10d2418f3eabc60286b5f3b73a`. Its release executable SHA-256
was `69407f5369c15cd6c1d6317ce954e61981dd39a5d09d7a47124550d7e0808403`.

These observations prove mutation of the resident vnode-backed page and
cache-visible file contents. They do not, by themselves, distinguish a later
physical filesystem writeback from a still-resident Unified Buffer Cache page.
That distinction does not affect the safety conclusion: both violate the
requirement that only a target process's private executable mapping change.

## Verified root cause

Apple's XNU 11417.140.69 source matches the public base version of the tested
Darwin 24 kernel. Its code-signing fault path shows:

1. A faulted page belongs to an external, code-signed VM object with a vnode
   pager (`object->internal == false`).
2. XNU obtains the vnode from that pager and computes the file-backed memory
   object offset.
3. `vm_page_map_and_validate_cs` maps the actual `vm_page` into kernel address
   space with read protection and passes that address as `const void *data` to
   `cs_validate_page`.
4. On the x86_64 one-page path, `vm_paging_map_object` can return
   `phystokv(VM_PAGE_GET_PHYS_PAGE(page))`: a direct kernel virtual alias of the
   same physical page, not a copied buffer and not a process-private mapping.
5. `cs_validate_page` hashes `data`; it has no write contract.

The revoked implementation called the original validator first, then cast
away `const`, disabled kernel write protection, and copied replacement bytes
through that alias. It therefore modified the physical page owned by the
external vnode-pager VM object after XNU had validated the original bytes.

Ordinary reads use the same vnode/UBC-backed object and can consequently see
the changed resident page. No `write(2)`, `VNOP_WRITE`, UPL dirty commit, or
normal vnode mutation path ran, so the filesystem had no reason to update
mtime or ctime.

Primary source anchors used for this conclusion:

- XNU's [code-sign validation fault path](https://github.com/apple-oss-distributions/xnu/blob/xnu-11417.140.69/osfmk/vm/vm_fault.c#L8002-L8137)
  resolves the vnode pager, computes the object/file offset, maps the actual
  `vm_page`, and passes its address to `cs_validate_page`.
- XNU's [one-page paging-map path](https://github.com/apple-oss-distributions/xnu/blob/xnu-11417.140.69/osfmk/vm/vm_pageout.c#L8968-L9004)
  can return a direct `phystokv` alias of that page.
- XNU's [UBC code-sign implementation](https://github.com/apple-oss-distributions/xnu/blob/xnu-11417.140.69/bsd/kern/ubc_subr.c#L5815-L6154)
  consumes `data` as const hashing input.
- XNU's [vnode-pager data request](https://github.com/apple-oss-distributions/xnu/blob/xnu-11417.140.69/osfmk/vm/bsd_vm.c#L634-L905)
  supplies external-object pages through vnode page-in.

## Dirty state and coherency

The revoked code did not explicitly set `vm_page::vmp_dirty`, issue
`UPL_COMMIT_SET_DIRTY`, or call a vnode write operation. A raw CPU store can
set a hardware modified/reference bit for a mapped physical page, and XNU's
pageout scanner can later fold pmap modified state into `vmp_dirty`. However,
the direct `phystokv` fast path returns before the slower mapping path marks
the page `vmp_pmapped`, and the page's other mappings and timing affect what
dirty state is later observed.

The result is not a supported coherent write. The implementation could neither
guarantee writeback nor guarantee discard. Depending on VM state, the changed
page could remain cache-visible, be seen by other mappings, be discarded and
later reloaded from unchanged storage, or be treated as dirty and sent through
the vnode pager. This uncertainty is itself a critical defect.

XNU's [pageout scan](https://github.com/apple-oss-distributions/xnu/blob/xnu-11417.140.69/osfmk/vm/vm_pageout.c#L3644-L3653)
can promote a pmap modified bit into VM-page dirty state. The revoked plugin
did not control whether the necessary mapping/state conditions held.

## Risk assessment

- **Code-signature invalidation — critical:** XNU validated the original page
  and the callback then changed it. The resident page could remain marked
  validated while no longer matching the CodeDirectory. File-based signature
  verification can observe different bytes from those signed.
- **Cross-process visibility — critical:** multiple mappings and ordinary file
  reads can reference the same external object page. The callback supplies no
  process-private ownership guarantee.
- **Stale or inconsistent cache state — high:** cached reads, a private COW
  mapping, and a later storage reload can disagree.
- **Later writeback — high but timing-dependent:** the implementation bypassed
  normal dirty bookkeeping, but pmap/refmod and pageout can still discover a
  modified page. It is unsafe to assume either persistence or non-persistence.
- **File-content corruption — critical:** the publisher-signed native module
  can be changed without normal metadata updates. A crash or power loss can
  occur during an incoherent state.
- **Filesystem structural corruption — unproven:** no filesystem metadata was
  deliberately overwritten, so structural damage is less likely than binary
  content corruption. Undefined cache/writeback behaviour prevents claiming
  it is impossible.
- **Undefined kernel behaviour — critical:** writing through a pointer whose
  API contract is `const`, while bypassing write protection and VM/VFS write
  accounting, is unsupported.

## Immediate mitigation

- Do not use the 1.0.0-rc1 kext in active mode.
- Disable or remove the installed release candidate before normal use.
- Restore the exact active `discord_krisp.node` with the known-good Swift
  patcher or a trusted Discord reinstall, then verify the original SHA-256 and
  publisher signature.
- Keep the known-good USB EFI available.
- Do not implement write-then-restore. Restoration cannot remove race, crash,
  cross-process, signature, or writeback windows.

Current source keeps the validation callback only as a bounded, identity-gated
read-only detector. A matching non-dry-run request is blocked and logged. CI
runs `Tests/check_validation_callback_read_only.sh`, which rejects the known
writable primitives and requires the fail-closed outcome.

## Replacement architecture review

### Lilu binary modifications

Pinned Lilu 1.7.2 does not meet the memory-only requirement for arbitrary
native modules. `UserPatcher::codeSignValidatePageWrapper` and
`codeSignValidateRangeWrapper` call `performPagePatch` with the validation
buffer. `performPagePatch` casts away `const`, enables kernel writing, and
modifies that buffer. This is the same unsafe destination class as the revoked
IntelMKLFixup implementation.

This is visible directly in pinned Lilu's
[`performPagePatch` and code-sign wrappers](https://github.com/acidanthera/Lilu/blob/e4748cc081bf060302c7d3c44a643ce1d11b7e1d/Lilu/Sources/kern_user.cpp#L166-L274).

Lilu's separate `patchSharedCache` path writes a selected process VM map using
`vm_protect` and `vm_map_write_user`, but it is designed around binaries whose
addresses are derived from the dyld shared-cache map. A user-home Electron
native module such as `discord_krisp.node` is not in the dyld shared cache.

`LiluAPI::onProcLoad` is driven by a `KAUTH_FILEOP_EXEC` listener and supplies
the main process image map. It is not an arbitrary `dlopen`/native-module load
callback. It cannot by itself identify the later Krisp module mapping.
The relevant [exec listener and process patch entry](https://github.com/acidanthera/Lilu/blob/e4748cc081bf060302c7d3c44a643ce1d11b7e1d/Lilu/Sources/kern_user.cpp#L94-L148)
and [`onProcLoad` API registration](https://github.com/acidanthera/Lilu/blob/e4748cc081bf060302c7d3c44a643ce1d11b7e1d/Lilu/Sources/kern_api.cpp#L174-L214)
do not provide an arbitrary later-image callback.

### Candidate designs

| Design | Private mapping | Timing for Krisp | Sequoia/signing issues | Assessment |
|---|---|---|---|---|
| Existing `_cs_validate_page` or Lilu `BinaryModInfo` | No | Early | Bypasses validation and coherency | Rejected |
| External userspace task patcher | Possible if the target mapping is `MAP_PRIVATE` and the task port is authorised | Polling is racy; may lose before first call | Hardened runtime, task-port policy, SIP and entitlements can block access | Feasibility probe only |
| `DYLD_INSERT_LIBRARIES` interposer | Process-local if accepted | Can wrap module loading | Discord and helpers are hardened-runtime binaries; library validation acceptance is not established | Not currently viable |
| Kext hook after `mmap`/VM mapping | Can force COW in the current target map | Potentially before `mmap` returns | Broad private kernel hook, process/vnode identity and W^X handling are high risk | Possible but not preferred |
| Lilu process-start payload plus in-process dyld/add-image observer | Can patch the target's private COW mapping | Potentially before module use; ordering before constructors must be proven | Requires reviewed payload injection and targeted security concessions | Safest kernel-assisted candidate, not yet approved |
| Authorised self-patching companion loaded normally by the application | Yes | Best if loaded before Krisp | No supported Discord extension point has been verified | Preferred in principle, unavailable today |

Read-only `codesign` inspection of the installed Discord application and its
helpers reported hardened-runtime signing and Team ID `53Q6R32WPB`. It did not
establish a usable library-validation exception, and the module was already in
the incident-modified state. Consequently, neither environment interposition
nor injection is treated as viable merely because it works for an unhardened
test process.

Update tolerance comes from applying the retained identity rules and compiled
exact PatchDefinitions to a newly observed private mapping, not from weakening
the byte pattern. Any application update with a new signing policy, unrecognised
MKL implementation, ambiguous match, or incompatible loader timing still fails
closed and requires reviewed policy or code changes.

The most promising direction is a minimal, authenticated in-process component
present before Krisp loads. It would observe a verified image-add event, prove
the image path/signature/Team ID and exact compiled PatchDefinition, confirm
the target segment is a private file mapping, temporarily make only that page
writable, trigger COW, write and verify six bytes, and restore RX protection.
It must prove that its callback occurs before any initializer or caller can
execute the MKL gate. How that component is admitted to Discord remains the
main blocker.

Lilu's `onProcLoad` plus `injectPayload` is a possible admission mechanism, but
it is not a ready solution. Pinned Lilu's `vmProtect` path can clear target
code-signing enforcement flags and set `CS_DEBUGGED` to permit W+X, which is a
material reduction in process security. A replacement must avoid RWX where
possible, restrict itself to one approved process/module/page, and undergo a
separate design review.

The security-flag changes are explicit in Lilu's
[`vmProtect` implementation](https://github.com/acidanthera/Lilu/blob/e4748cc081bf060302c7d3c44a643ce1d11b7e1d/Lilu/Sources/kern_user.cpp#L29-L92).

## Retainable components

The pure, application-independent components remain useful:

- `ApplicationRule` path, basename, signing identifier and Team ID policy;
- `PatchDefinition` exact original/context/replacement catalogue;
- `ImageVariant`/mode metadata, with offsets reinterpreted against a verified
  private mapping rather than a validation buffer;
- exact-match, mutation, truncation and ambiguity tests;
- signed userspace identity-manifest tooling; and
- dry-run detection and diagnostics.

The validation callback write engine and any claim of runtime success are not
retainable.

## Smallest next milestone

After approval, perform a **read-only feasibility prototype** outside active
patching:

1. identify which Discord process loads `discord_krisp.node`;
2. record its actual VM mapping flags, protections, slide and lifetime;
3. verify whether the mapping is private/COW;
4. verify an authorised process-start/image-add callback and its ordering
   relative to module initializers and the first MKL call; and
5. verify whether a signed in-process component can be admitted without
   disabling global or process-wide security policy.

No byte write should be implemented until those five facts are established.
