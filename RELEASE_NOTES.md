# IntelMKLFixup 1.0.0-rc1 release notes

## Status

**This release candidate is revoked. Do not use it in active mode.** Controlled
Ryzen 9 3900X testing proved that its write through `_cs_validate_page`
changed the vnode-backed Discord module and cache-visible file contents without
advancing mtime or ctime. The historical build results do not establish safety.
See `FILE_BACKED_WRITE_INCIDENT.md`.

Current source blocks active writes and remains detection-only, but no
replacement release artefact has been built.

## Architecture

IntelMKLFixup is a Lilu plugin for x86_64 AMD Hackintosh systems. Its callback
and patch engine are application-independent:

- `PatchDefinition` describes an exact reviewed Intel MKL implementation and
  the compiled `mov eax, 1; ret` replacement.
- `ApplicationRule` identifies an approved application/native-module family by
  bounded path grammar, basename, signing identifier, Team ID, and signing
  policy.
- `ImageVariant` associates an application rule with allowed compiled patch
  definitions and mode-specific evidence.

The kext calls the original `_cs_validate_page` first. It considers only a
compiled target page, requires successful XNU validation and approved image
identity, then performs exact MKL function and surrounding-context checks.

## Supported modes

### StrictVariant

The default controlled mode requires the exact configured CDHash, fixed target
offset, application identity, signing policy, MKL function, and context. It
never searches and never falls back to another mode.

### BoundedWindow

Experimental and disabled unless `-imklfxwindow` is present. It searches only
the compiled callback-complete page `0x650000..<0x651000`, accepts exactly one
complete compiled definition in that window, and rejects zero or multiple
matches. It does not establish uniqueness elsewhere in the image.

Future userspace-assisted full-image pre-scan is design-only and is not a
runtime mode in this release.

## Supported environment

- Architecture/CPU class: x86_64 AMD only.
- Intended first hardware: AMD Ryzen 9 3900X; live validation is pending.
- Runtime OS gate: macOS 15 / Darwin 24 only.
- Build-validation OS: macOS 15.7.7 (24G720), Darwin 24.6.0.
- Required plugin dependency: Lilu, loaded before IntelMKLFixup.
- Required boot environment: OpenCore with a separately verified recovery EFI.

No other macOS, Darwin, CPU architecture, or physical AMD CPU has been
validated by this release candidate.

## Compiled application rules

The only enabled application rule is Discord Stable/Krisp:

```text
basename:           discord_krisp.node
signing identifier: discord_krisp
Team ID:            53Q6R32WPB
signing policy:     valid hardened runtime, not ad-hoc
```

The strict live-test fixture is Discord Stable 0.0.403 with its reviewed CDHash
and target offset `0x650100`. The version-independent Discord path rule and its
BoundedWindow selector have passed host tests. Discord runtime behavior, voice,
audio-device switching, and Krisp remain unverified.

Photoshop, Lightroom, DaVinci Resolve, and other applications are not enabled
or claimed as supported.

## Compiled MKL support

The sole compiled patch definition is:

```text
mkl-serv-intel-cpu-true-oneapi-build-20201104-x86_64-v1
```

It recognises one reviewed Intel oneAPI MKL build 20201104 implementation of
`_mkl_serv_intel_cpu_true` using exact function bytes and exact 16-byte context
on both sides. The historical active implementation wrote
`B8 01 00 00 00 C3` (`mov eax, 1; ret`), but that write path is revoked because
it changed vnode-backed file contents. Current source is detection-only.
Numerical MKL functions are not replaced or redirected.

## Boot arguments

Recommended initial dry-run arguments:

```text
-imklfxdryrun -imklfxdbg -imklfxbuiltin
```

Other controls:

| Argument | Effect |
|---|---|
| `-imklfxoff` | Disable the plugin completely. |
| `-imklfxdbg` | Enable meaningful verbose diagnostics; home usernames are redacted. |
| `-imklfxdryrun` | Run all eligibility and byte checks without modifying memory. This is the only acceptable mode for the revoked binary. |
| `-imklfxbuiltin` | Explicitly record use of the compiled catalogue, currently the only runtime policy source. |
| `-imklfxwindow` | Experimentally enable compiled BoundedWindow policies. Do not use for the first strict test. |

The original active test instructions are withdrawn. Follow the incident-era
`TEST_PLAN_3900X.md` only for recovery and detection evidence; do not improvise
a live fixture or modify a publisher-signed application.

## Known limitations and risks

- This is kernel software using Darwin 24 private code-signing symbols. Symbol
  or callback behavior may change with OS updates.
- Static analysis and host tests cannot prove live kernel concurrency, pointer
  lifetime, write-protection, routing, or ABI safety.
- The current Discord installation is known to be patched on disk by a separate
  userspace tool. Strict testing requires manually restoring and hashing the
  exact original at the test-plan stage.
- The patch bypasses one CPU-vendor gate. It does not guarantee numerical MKL
  correctness on AMD or compatibility with arbitrary Intel-only software.
- BoundedWindow is update-tolerant only while the complete implementation and
  context remain uniquely inside its one approved page.
- Unknown application identities, signing evidence, paths, MKL variants,
  incomplete callbacks, and ambiguous matches fail closed.
- The signed userspace manifest store is not consumed by the kext. Installing
  a manifest has no runtime effect, and a new application still requires a
  reviewed kext release.
- The production manifest verification key is deliberately unconfigured.
- No codesigning certificate or notarization is provided. The local test build
  is ad-hoc signed by Xcode's “Sign to Run Locally” identity.

## Recovery

Before testing, boot the known-good USB EFI and confirm it remains independent
of the NVMe EFI. Back up the current NVMe `config.plist` outside the EFI.

If the test produces a panic, watchdog, abnormal boot, or routing error:

1. boot macOS through the known-good USB EFI;
2. identify and mount the correct NVMe EFI—never assume its disk identifier;
3. move `IntelMKLFixup.kext` out of `EFI/OC/Kexts`;
4. restore the exact pre-test `config.plist` backup;
5. validate it with `plutil` and the matching OpenCore `ocvalidate`; and
6. unmount and reboot manually.

The complete commands and stop conditions are in `TEST_PLAN_3900X.md` and
`docs/RECOVERY.md`.
