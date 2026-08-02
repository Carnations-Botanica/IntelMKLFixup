# Changelog

## 0.2.0-rc1 - 2026-08-02

- Restored the verified Darwin 24 strict validation-page patch.
- Added the required `-imklfxfilepatch` acknowledgement gate.
- Made default and dry-run operation detection-only.
- Restricted active writes to the exact `StrictVariant`; `BoundedWindow` is
  detection-only.
- Renamed patch outcomes to state file-visible behaviour explicitly.
- Retained fail-closed application, image, signature, CDHash, offset, byte,
  context, and bounds checks.
- Added host tests for operating modes and static write-scope assertions.
- Retained the Swift MKL inspector and distinguished no Mach-O, symbol absent,
  recognised original, this project's patch, recognised upstream patch, and
  unknown implementation states.
- Consolidated the validation-page incident and memory-only research record.
- Changed the bundle identifier to `com.richardhedges.IntelMKLFixup` and the
  project version to `0.2.0`.

## Earlier fork work

The pre-release fork work introduced the generic `ApplicationRule`,
`ImageVariant`, and `PatchDefinition` architecture, strict identity checks,
bounded research matching, signed userspace whitelist tooling, host policy
tests, and reproducible CI inputs. The exact pre-cleanup state is preserved on
`archive/pre-file-backed-release-cleanup`.
