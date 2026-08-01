# Changelog

All notable changes to this fork are documented here.

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
