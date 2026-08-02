# Changelog

All notable changes to this fork are documented here.

## Unreleased — validation-page write incident

### Critical safety change

- Revoked 1.0.0-rc1 for active use after controlled Ryzen 9 3900X testing
  proved that its six-byte `_cs_validate_page` write changed the vnode-backed
  `discord_krisp.node` page and cache-visible file contents without advancing
  inode timestamps.
- Removed all validation-callback writes, kernel write-protection changes,
  mutable casts, mutable target-pointer helpers, replacement copies, and
  `modified=yes` outcomes.
- Non-dry-run matches now fail closed with
  `outcome=unsafe-file-backed-write-blocked modified=no`; dry-run detection
  remains available.
- Added a CI source-safety regression and
  `FILE_BACKED_WRITE_INCIDENT.md`.
- Withdrew all active StrictVariant and BoundedWindow hardware-test stages.

### Architecture review

- Verified against Apple XNU 11417.140.69 that `_cs_validate_page` receives a
  const alias of a vnode-pager-backed VM page; it is not a process-private
  patch destination.
- Verified that pinned Lilu 1.7.2 `BinaryModInfo` patching writes through the
  same validation-buffer class and is not a safe replacement for arbitrary
  native modules.
- Reserved replacement work for a separately approved process-private,
  post-load architecture.

## 1.0.0-rc1 — 2026-08-01

Release candidate for controlled manual testing. The kext bundle version
remains `1.0.0`.

### Added

- Application-independent MKL patch engine with separately compiled
  `PatchDefinition`, `ApplicationRule`, and `ImageVariant` policy layers.
- Exact reviewed patch definition
  `mkl-serv-intel-cpu-true-oneapi-build-20201104-x86_64-v1`.
- Discord Stable/Krisp as the first compiled application rule.
- Exact Discord 0.0.403 StrictVariant fixture.
- Experimental one-page BoundedWindow rule gated by `-imklfxwindow`.
- Dry-run, verbose, disable, and built-in-policy boot controls.
- Host policy/catalogue tests and signed-manifest/updater tests.
- Signed userspace whitelist tooling with fail-closed validation, atomic
  installation, rollback, locking, and symlink-safe storage.
- Clean Debug/Release builds, sanitizer coverage, static analysis, CI, recovery
  documentation, and a staged Ryzen 9 3900X manual test plan.

### Security and safety

- Application identity and signing policy are checked before byte matching.
- StrictVariant never searches or falls back to BoundedWindow.
- BoundedWindow searches one compiled, callback-complete 4 KiB window and
  rejects zero or multiple matches; it makes no image-wide claim.
- Only compiled exact MKL bytes and context are accepted.
- Remote manifests cannot supply kernel search or replacement machine code.
- Installed manifests do not affect runtime kext policy in this release.

### Known limitations

- Runtime support is limited to x86_64 AMD and Darwin 24.
- No live kernel, Discord voice, or Krisp functional test has yet passed.
- BoundedWindow tolerance ends if the implementation moves outside
  `0x650000..<0x651000`.
- Future full-image userspace-assisted pre-scan remains design-only.
- The production whitelist signing trust root is not configured.
