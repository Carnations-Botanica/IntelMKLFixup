# IntelMKLFixup 0.2.0-rc1 final project report

## Source-control starting point and preservation

- Starting branch: `main`
- Starting commit: `7a0273326f570a10d2418f3eabc60286b5f3b73a`
  (`Prepare 1.0.0 rc1 manual-test candidate`)
- Starting relationship: local `main` was 19 commits ahead of `origin/main`.
- Last committed active strict patch: `7a02733`.
- Later write removal/block: no later commit removed the write; the blanket
  read-only block, `targetPointer` removal, incident reports, diagnostic tools,
  and research documents were uncommitted starting-worktree changes.
- Safety branch: `archive/pre-file-backed-release-cleanup`
- Safety snapshot commit: `92787e14175b0b221a037e9d0c0deb82984a7836`
- Safety branch push: succeeded to the fork before restructuring.
- Release branch: `codex/0.2.0-rc1`

The history audit covered all refs, tags, the reflog, unreachable objects, commit
diffs, tracked changes, untracked files, ignored build output, and remotes. No
upstream wholesale revert was used.

## Commits created

- `92787e1` Archive validation-page file-backed research state
- `8b77123` Restore acknowledged strict file-backed patch mode
- `5adcea5` Fix userspace patch-state detection
- `7c7ddde` Consolidate documentation and research conclusions
- `824e0be` Clean repository and prepare 0.2.0 release candidate
- `f593d78` Add 0.2.0-rc1 release artifacts
- `c6f8035` Add packaged 0.2.0 release bundle

This report is committed after the tagged artefact commit so it can record the
actual branch/tag publication result without moving the release tag.

## Restored behaviour and operating model

The Darwin 24 `_cs_validate_page` route and verified strict Discord Stable/Krisp
implementation were restored while retaining `ApplicationRule`, `ImageVariant`,
and `PatchDefinition`. The strict rule validates the exact path grammar,
basename, signing identifier `discord_krisp`, Team ID `53Q6R32WPB`, valid
Hardened Runtime state, non-ad-hoc signature, compiled CDHash, executable range,
callback offset `0x650100`, complete reviewed MKL implementation, and exact
16-byte context on both sides. Bounds are checked and unknown or partial states
fail closed.

The first six bytes are replaced with `b8 01 00 00 00 c3`. Already-patched
bytes are recognised. The target is revalidated under the kernel write lock and
the result is verified after the write.

Boot arguments:

- `-imklfxoff`: Lilu disables the plugin.
- no `-imklfxfilepatch`: detection/logging only; matched originals report
  `outcome=file-backed-write-not-acknowledged modified=no`.
- `-imklfxdryrun`: always detection-only.
- `-imklfxfilepatch -imklfxdryrun`: acknowledged detection-only test.
- `-imklfxfilepatch`: permits only the reviewed `StrictVariant` write.
- `-imklfxdbg`: verbose logging.

Active load logging includes
`mode=file-backed-patch acknowledged=yes file-visible=yes experimental=yes`.
Success reports
`outcome=file-backed-patched modified=yes file-visible=yes`. The source never
uses `mode=in-memory`. `BoundedWindow` remains optional detection research and
cannot actively write or become an active strict fallback. No image-wide scan
exists.

## Swift status fix

The retained Swift MKL Patcher was added as a self-contained package under
`Tools/MKLPatcher` without its proprietary Discord fixture. Synthetic Mach-O
tests now distinguish:

- no Mach-O files;
- Mach-O files with the symbol absent;
- the exact reviewed original implementation;
- this project's exact six-byte patch layout;
- the exact 23-byte upstream IntelMKLFixup replacement layout; and
- a symbol with unknown or unsupported implementation bytes.

The upstream state is labelled `Patched by recognised upstream IntelMKLFixup
pattern.` Automatic upstream restoration is disabled because the tool cannot
assume the exact original bytes for an arbitrary upstream-patched image.

## Documentation and repository cleanup

The repository now has the requested top-level build, installation, recovery,
security, contribution, release-checklist, changelog, and release-note files.
The durable documentation set covers architecture, application rules, patch
definitions, file-backed behaviour, testing, the incident, and memory-only
research.

Phase prompts, superseded candidate notes, duplicate whitelist narratives,
temporary incident reports, and speculative procedural documents were removed
after their useful conclusions were consolidated. Research source fixtures and
tests were retained. `.gitignore` now covers build/cache output, Xcode user
data, temporary dependencies, logs, EFI copies, Discord binaries, crash
reports, and signing secrets. No accidental build output or dependency checkout
is tracked.

## Test and analysis results

All final checks passed unless explicitly noted:

- 18 C++ policy/catalogue test functions, compiled with C++14, `-Wall`,
  `-Wextra`, `-Werror`, and `-pedantic`.
- The same C++ suites under AddressSanitizer and UndefinedBehaviorSanitizer.
- Static architecture/write-scope check, including one scoped write primitive,
  required boot/log strings, no in-memory mode label, no kernel networking, and
  no remote machine-code schema fields.
- 25 root Swift whitelist tests with warnings as errors.
- Offline manifest semantic validation: version 3, one application rule, two
  image variants.
- 4 synthetic Swift MKL inspector tests with warnings as errors.
- Root userspace tools Release build and MKLPatcher Release build.
- Source and built plist lint.
- JSON parsing for all retained manifests/schemas.
- Shell syntax checks for retained scripts.
- GitHub Actions YAML parse.
- `git diff --check`.
- Debug and Release Xcode static analysis: zero HTML diagnostics.
- Clean Debug and Release kext builds with project warnings treated as errors.
- Both built bundles passed strict ad-hoc code-signature verification.
- Both binaries are thin x86_64 Mach-O kext bundles.

The pinned Lilu dependency built successfully but its upstream project emitted
deployment-target and always-run script-phase warnings. IntelMKLFixup itself
built with `GCC_TREAT_WARNINGS_AS_ERRORS=YES`. Host tests and static analysis do
not prove kernel runtime safety. This release candidate was not installed or
runtime-tested during preparation; the accepted runtime evidence is the prior
controlled Ryzen 9 3900X/Darwin 24 test recorded in the incident document.

## Build commands and pinned inputs

Pinned revisions:

- Lilu `e4748cc081bf060302c7d3c44a643ce1d11b7e1d`
- MacKernelSDK `05094e5e88cec7caedbfb35e8449ed0db94bf95b`

Principal commands:

```text
xcodebuild -jobs 1 -arch x86_64 -configuration Debug
xcodebuild analyze -jobs 1 -scheme IntelMKLFixup -configuration Debug ...
xcodebuild analyze -jobs 1 -scheme IntelMKLFixup -configuration Release ...
xcodebuild -jobs 1 -scheme IntelMKLFixup -configuration Debug \
  GCC_TREAT_WARNINGS_AS_ERRORS=YES SYMROOT=<repo>/build
xcodebuild -jobs 1 -scheme IntelMKLFixup -configuration Release \
  GCC_TREAT_WARNINGS_AS_ERRORS=YES SYMROOT=<repo>/build
swift test -Xswiftc -warnings-as-errors
swift test --package-path Tools/MKLPatcher -Xswiftc -warnings-as-errors
swift build -c release -Xswiftc -warnings-as-errors -Xswiftc -gnone
swift build --package-path Tools/MKLPatcher -c release \
  -Xswiftc -warnings-as-errors
```

## Artefacts and hashes

- Debug development kext:
  `release/IntelMKLFixup-0.2.0-Debug.kext`
- Release package directory: `release/IntelMKLFixup-0.2.0/`
- Release archive: `release/IntelMKLFixup-0.2.0.zip`
- Root hash inventory: `SHA256SUMS.txt`

SHA-256 of executable payloads:

- Debug: `8759ab16ed0cd09159acb9f5b3f6cd4881d88325fb438670ae3806fbdb1d950a`
- Release: `2e9ed1b0e311d44c05bc4c700a5f58d25bb77cb56123047646a2ed526f079a1f`

Release archive SHA-256:

`1e01242f4cdd820bfae41a3ece413fdf027f3c328f75ac1aedf6998dde792aab`

The archive contains only the Release kext, README, installation, recovery,
changelog, licence, and package hash inventory. It contains no Git metadata,
build caches, tests, logs, dependencies, Discord binaries, EFI contents, or
personal files. The Debug kext is separate.

## Remotes, push, tag, and prerelease

- `origin`: `https://github.com/richardhedges/IntelMKLFixup` for fetch and push.
- `upstream`: `https://github.com/Carnations-Botanica/IntelMKLFixup`.
- Safety archive push: succeeded.
- Release branch push: succeeded without force. HTTPS authentication failed
  with `could not read Username for 'https://github.com': Device not
  configured`; the already-proven SSH credential was used temporarily for the
  same fork, then `origin` was restored to HTTPS.
- Annotated tag `v0.2.0-rc1`: created and pushed without force. The tag peels to
  `c6f8035fee3fedbfc496a3c9cdd2f90b604b3116`.
- GitHub prerelease: not created. GitHub CLI 2.96.0 reports that the active
  `richardhedges` token is invalid, and the installed GitHub connector exposes
  no release-creation/upload operation. Consequently the zip and
  `SHA256SUMS.txt` could not be uploaded as GitHub Release assets in this run.

## Known risks and memory-only research status

Users enabling active mode accept file-visible binary modification,
application signature invalidation, undocumented vnode/UBC and kernel
behaviour, and possible application or system instability after updates. A
backup and recovery EFI are required. Physical persistence is not guaranteed
or relied upon.

Proven research conclusions: the validation-page route and upstream design are
file-visible; private/COW process mappings are the desired true memory-only
target; Discord/Krisp uses direct internal calls, making simple interposition
unsuitable; and the strict identity/MKL evidence remains reusable.

Unresolved: trusted observation inside Discord's hardened process, timing
before the first gate call, safe private executable-page modification,
code-signing/Hardened Runtime effects, and whether a practical process-private
solution exists without unacceptable security compromises. Future techniques
remain high-level; no injection or security-bypass procedure is published.

## Scope confirmation

No EFI or OpenCore file was readied for installation or modified. No
`config.plist`, Discord application/module, or copied Discord binary was
modified. The kext was not installed or loaded. No `sudo` command was invoked,
and the machine was not rebooted.
