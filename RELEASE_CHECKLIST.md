# Release checklist

- [ ] Confirm branch, clean worktree, `origin`, and `upstream`.
- [ ] Confirm version `0.2.0`, bundle ID, x86_64 architecture, and plist.
- [ ] Confirm pinned Lilu and MacKernelSDK commits.
- [ ] Run C++ tests, Swift tests, manifest validation, plist lint, write-scope
      checks, whitespace checks, sanitizers, analysis, and clean Debug/Release builds.
- [ ] Confirm active mode needs `-imklfxfilepatch` and only StrictVariant writes.
- [ ] Confirm no proprietary binaries, EFI files, logs, secrets, personal paths,
      dependency checkouts, or build caches are tracked or packaged.
- [ ] Assemble `release/IntelMKLFixup-0.2.0/` and verify its contents.
- [ ] Record Debug, Release, package, and archive SHA-256 hashes.
- [ ] Update `FINAL_PROJECT_REPORT.md` and `RELEASE_NOTES_0.2.0-rc1.md`.
- [ ] Create logical commits and annotated `v0.2.0-rc1` tag without rewriting history.
- [ ] Push normally and publish an experimental, file-visible prerelease if authorised.
