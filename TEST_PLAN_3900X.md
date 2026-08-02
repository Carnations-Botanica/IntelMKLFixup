# Ryzen 9 3900X safety and detection-only test plan

## Incident status

**All active runtime patch stages are withdrawn. Do not boot any existing
IntelMKLFixup build without `-imklfxdryrun` or `-imklfxoff`.**

Controlled hardware testing proved that the 1.0.0-rc1 write through
`_cs_validate_page` changed the vnode-backed `discord_krisp.node` page and the
bytes returned by ordinary file reads. The previous Stage C, D, F, and active
stability procedures are invalid. Do not repeat them. See
`FILE_BACKED_WRITE_INCIDENT.md`.

Current source has not been built in response to the incident. When a later
reviewed build is produced, it must be detection-only and log
`outcome=unsafe-file-backed-write-blocked modified=no` for every non-dry-run
match. No replacement runtime patch architecture is implemented.

## Claims that were established

The controlled Ryzen 9 3900X test established only:

1. Lilu 1.7.2 and IntelMKLFixup 1.0.0 loaded.
2. Darwin 24 and AMD gating passed.
3. Required private symbols resolved and `_cs_validate_page` routing succeeded.
4. The active six-byte callback write changed cache-visible backing-file bytes.

It did not establish a safe memory-only patch, stable Krisp support, or release
readiness.

## Stage A — Disable and recover

### Prerequisites

- Keep the verified USB recovery EFI unchanged and available.
- Record the current boot arguments and loaded IntelMKLFixup/Lilu versions.
- Do not launch a new active-mode test.

### Required manual actions

Use one of these fail-safe states before normal testing:

- boot with `-imklfxoff`; or
- boot through the known-good USB EFI and remove/disable IntelMKLFixup in the
  NVMe EFI using `docs/RECOVERY.md`.

Do not use a write-then-restore workflow as a runtime mitigation.

### Restore the affected module

Identify the exact active `discord_krisp.node`. Use the existing Swift patcher
manually to restore that file from its known-good original, or reinstall the
matching Discord version from a trusted source. Then verify:

```sh
/usr/bin/shasum -a 256 "$KRISP_MODULE"
/usr/bin/codesign --verify --strict --verbose=4 "$KRISP_MODULE"
```

Expected original SHA-256 for the incident fixture:

```text
de061edb4387fc5bba2b8535483aa2f4347c17bc9e4d25babef36172c86a9f9a
```

The incident result that must not remain is:

```text
95e611b3bd89d95d67f1e809eb1cbefcc8eedbba3bd5b70bf11cf044cf72b27d
```

At file offset `0x650100`, the restored bytes must begin:

```text
53 48 83 ec 20 8b
```

Stop if the hash or signature cannot be restored. Do not treat unchanged mtime
or ctime as proof that the bytes are original.

## Stage B — Historical log preservation

Preserve the following with the incident report:

- macOS version/build and Darwin version;
- OpenCore and Lilu versions;
- IntelMKLFixup UUID and executable SHA-256;
- exact boot arguments;
- Discord version and module path;
- original and resulting SHA-256 values;
- `cmp` output and bytes at `0x650100`;
- inode, mtime and ctime observations; and
- Lilu dump containing lifecycle, route, candidate and patch outcomes.

Do not interpret `lifecycle=route-installed` as patch safety.

## Stage C — Detection-only validation for a future safety build

This stage is blocked until a new source build containing the incident
mitigation has been separately reviewed. Do not reuse the 1.0.0-rc1 binary.

### Required controls

For any future detection-only test:

```text
-imklfxdryrun -imklfxdbg
```

Keep `-imklfxwindow` absent for the strict fixture. Debug Lilu may additionally
use `-liludbgall liludump=60` only for diagnostics.

### Required evidence

The strict original fixture may report:

```text
candidate-app-approved ... mode=strict-variant
patch=mkl-serv-intel-cpu-true-oneapi-build-20201104-x86_64-v1 ... outcome=dry-run modified=no offset=0x650100
```

Immediately verify the on-disk SHA-256 remains the original. Any changed byte
is a critical failure.

### Active-mode fail-closed check

Do not perform this on hardware until the new source is built, reviewed, and
explicitly approved. The required automated/source behaviour is:

```text
outcome=unsafe-file-backed-write-blocked modified=no
```

There must be no `outcome=patched`, `modified=yes`, `setKernelWriting`, mutable
validation pointer, or replacement copy in the callback implementation.

## Stage D — Host-only regression checks

Host-side policy and catalogue tests may continue because they operate on
ordinary test buffers. They do not establish kernel runtime safety.

The source-safety regression is:

```sh
sh Tests/check_validation_callback_read_only.sh
```

It must report:

```text
validation callback is detection-only
```

Do not compile or run a new kext merely to satisfy this document; normal build
validation resumes only after the incident analysis is approved.

## Stage E — Replacement feasibility (not yet approved)

The smallest future milestone is read-only observation:

1. identify the Discord process that loads `discord_krisp.node`;
2. record its mapping address, protections and private/shared status;
3. prove an image-add notification occurs before initializers or the first MKL
   vendor-gate call;
4. determine whether an authenticated in-process component can be admitted
   without weakening process-wide code-signing policy; and
5. retain the existing ApplicationRule and PatchDefinition checks without
   writing any bytes.

No replacement write or live patch test is authorised by this plan.

## Stop and recovery conditions

Stop immediately for:

- any non-dry-run use of the old release candidate;
- any `modified=yes` or `outcome=patched` log;
- any changed module hash or invalid publisher signature;
- panic, watchdog, abnormal boot, or unrelated application impact; or
- any proposal to restore original bytes after temporarily mutating a
  validation page.

Use the known-good USB EFI and `docs/RECOVERY.md`. Preserve evidence before
cleaning up the affected module.
