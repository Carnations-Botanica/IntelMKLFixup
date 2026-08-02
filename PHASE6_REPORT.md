# Phase 6 build and validation report

## Outcome

**Active runtime patch unsafe: current validation-page write persists to disk.**

This report's original build/test results are retained as historical evidence,
but its recommendation is superseded. Controlled Ryzen 9 3900X testing proved
that the active callback write changed the vnode-backed `discord_krisp.node`
page and cache-visible file contents. The 1.0.0-rc1 active path is revoked.
Current source is detection-only and blocks every non-dry-run match with
`outcome=unsafe-file-backed-write-blocked modified=no`. See
`FILE_BACKED_WRITE_INCIDENT.md`.

The generic policy architecture passed source review, clean Debug and Release
builds succeeded, all host tests passed, both sanitizer runs were clean, and
Clang's static analyzer emitted no diagnostics for the kext translation unit.
This was not evidence of kernel runtime safety. The subsequent live kernel test
disproved the assumed memory-only behaviour: the local Discord module became
cache/file-read-visible with the six replacement bytes. No validation-page
artifact is approved for active patching.

StrictVariant and BoundedWindow matching remain host-tested policy components,
but neither authorises writes to validation pages. Any future live use is
detection-only until a separately reviewed process-private engine exists.

## Source state and cleanliness

- Initial Phase 6 commit: `b6cfe43e7ae3e78bd6defd3af0b9c3d0240bca24`.
- Commit whose code and packaging were built and inspected:
  `9b884557e6b4acf5bc1ca7a18ef680970deeb48c`.
- Expected BoundedWindow commits present:
  - `f64742f` — bounded validation-window patch mode;
  - `ce68fd2` — schema representation; and
  - `b6cfe43` — architecture and future ImageScan documentation.
- Phase 6 validation commits:
  - `222eaef` — updater-store hardening and expanded host tests; and
  - `9b88455` — minimal declared-output kext archives.
- The source tree was clean immediately before the final isolated builds.
- Repository-local `.build/`, `build/`, and stray `.DS_Store` output was removed
  before clean builds. No tracked fixture or source file was deleted.
- `git diff --check` passed before building and after documentation changes.
- `git ls-files` found no tracked `build`, `.build`, `DerivedData`, `Lilu.kext`,
  or `MacKernelSDK` output.
- `.gitignore` now excludes `.build/`, `build/`, `DerivedData/`, `.DS_Store`,
  Xcode user/workspace data, object files, dSYMs, archives, xcarchives, the
  generated Lilu bundle, the SDK link, and dependency worktrees.

The ignored handoff artifacts under `build/Phase6` are intentionally untracked.
They can be regenerated from the tested commit.

## Build environment

| Item | Observed value |
|---|---|
| macOS | 15.7.7 (24G720) |
| Darwin | 24.6.0, XNU `11417.140.69.710.16~1` |
| Host architecture | x86_64 (`arch` reports macOS's compatibility value `i386`) |
| Xcode | 16.4 (16F6) |
| Apple Clang | 17.0.0 (`clang-1700.0.13.5`) |
| Swift | 6.1.2 (`swiftlang-6.1.2.1.2`) |
| macOS SDK | 15.5 (24F74) |
| Kext deployment target | macOS 10.13 build setting; runtime policy is Darwin 24 only |
| Userspace deployment target | macOS 13 |
| Kext architecture | x86_64 only |
| C++ language standard | `c++1y`, i.e. C++14 |
| Bundle ID/version | `com.github.whatdahopper.IntelMKLFixup`, 1.0.0 |

The kext executable has no normal userspace `LC_BUILD_VERSION` load command;
the processed Info.plist records `LSMinimumSystemVersion=10.13`. Both Swift
tools contain `LC_BUILD_VERSION` with minimum macOS 13.0 and SDK 15.5.

OpenCore is not a build dependency. The runtime assumptions are x86_64 AMD,
Darwin 24, Lilu loaded first, and an OpenCore `Kernel -> Add` entry constrained
to Darwin 24. The actual test system's OpenCore, Lilu, and SMBIOS versions must
be recorded manually in Stage A.

## Dependency verification

The workflow pins source revisions rather than binary downloads:

| Dependency | Pinned and checked revision | Verification |
|---|---|---|
| Lilu | `e4748cc081bf060302c7d3c44a643ce1d11b7e1d` | Official repository; exact tag `1.7.2`; fresh detached checkout; clean status; `git fsck --no-dangling` passed |
| MacKernelSDK | `05094e5e88cec7caedbfb35e8449ed0db94bf95b` | Official repository; current pinned master revision; fresh detached checkout; clean status; `git fsck --no-dangling` passed |

The revisions were checked with `git ls-remote` against the official source
repositories and then re-checked in fresh source worktrees under `/private/tmp`.
No pre-existing local dependency copy was trusted, and no unreviewed binary
dependency was downloaded or executed.

The pinned Lilu Debug SDK build succeeded. Its upstream project emitted Xcode
16 compatibility warnings about an old 10.6 deployment target and two run
scripts without output declarations. These are Lilu-build warnings, not
IntelMKLFixup source warnings, but they should remain visible in future CI.

## Architecture regression gate

Source review at `9b88455` confirmed:

| Requirement | Evidence/result |
|---|---|
| StrictVariant never searches | `selectPolicyPatch` dispatches strict rules directly to `selectStrictPatch`, which checks only `targetFileOffset`. |
| No silent strict fallback | `selectImageVariant` treats strict and window policies independently and gives an independently approved strict rule precedence. BoundedWindow must itself be compiled and enabled. |
| Experimental gate | `-imklfxwindow` populates `RuntimePolicyControls`; a window rule returns `ModeDisabled` without it. |
| Exact window | Discord's compiled window is `0x650000..<0x651000`, page aligned and exactly 4096 bytes. |
| Complete callback | `callbackRangeContainsWindow` must prove the full window is in the one callback before selection/search. The callback passes one x86_64 `PAGE_SIZE`; a static assertion requires 4096 bytes. |
| Identity before search | The callback obtains the bounded vnode path, validates the application rule, reads XNU code-signing identity, approves a variant, and only then calls the patch selector. |
| Match cardinality | Zero returns mismatch; a second candidate immediately returns ambiguous; exactly one original or already-patched candidate is accepted. |
| Context bounds | Checked sizes and `classifyCandidateAt` keep before/function/after bytes inside the supplied callback and compiled text range. |
| No cross-page reconstruction | No callback data is retained and no state joins adjacent pages. A split candidate is rejected. |
| Discord update metadata | The BoundedWindow rule has no version, CDHash, or fixed target offset requirement. |
| Strict fixture retained | Discord 0.0.403 remains a separate CDHash-plus-offset StrictVariant at `0x650100`. |
| Layer separation | The MKL definition contains no Discord path, signature, Team ID, or CDHash. The Discord application rule contains no MKL machine code. |
| Manifest runtime status | The kext reads only the compiled C++ catalogue. Installed userspace manifests do not affect the current or next-boot kext. |
| Kernel networking | No HTTP, GitHub, URL session, socket, updater, or manifest-loading code exists in the kext source. The only `http` text is the standard Info.plist DTD URL. |

BoundedWindow's exact guarantee remains: identity-gated uniqueness inside one
approved, callback-complete 4 KiB window only. It makes no image-wide claim.

## Commands run

The substantive final commands were:

```sh
rtk xcodebuild -project IntelMKLFixup.xcodeproj -scheme IntelMKLFixup \
  -configuration Debug -destination platform=macOS,arch=x86_64 \
  -derivedDataPath /private/tmp/imklfx-phase6.UooiW5/FinalKextDebug \
  GCC_TREAT_WARNINGS_AS_ERRORS=YES clean build

rtk xcodebuild -project IntelMKLFixup.xcodeproj -scheme IntelMKLFixup \
  -configuration Release -destination platform=macOS,arch=x86_64 \
  -derivedDataPath /private/tmp/imklfx-phase6.UooiW5/FinalKextRelease \
  GCC_TREAT_WARNINGS_AS_ERRORS=YES clean build

rtk swift build -c debug --disable-sandbox \
  --scratch-path /private/tmp/imklfx-phase6.UooiW5/FinalSwiftDebug \
  -Xswiftc -warnings-as-errors

rtk swift build -c release --disable-sandbox \
  --scratch-path /private/tmp/imklfx-phase6.UooiW5/FinalSwiftRelease \
  -Xswiftc -warnings-as-errors -Xswiftc -gnone

rtk xcrun clang++ -std=c++14 -Wall -Wextra -Werror -pedantic \
  Tests/PolicyTests/PolicyTests.cpp -o /private/tmp/imklfx-policy-tests
rtk /private/tmp/imklfx-policy-tests

rtk xcrun clang++ -std=c++14 -Wall -Wextra -Werror -pedantic \
  Tests/PolicyTests/CatalogueTests.cpp -o /private/tmp/imklfx-catalogue-tests
rtk /private/tmp/imklfx-catalogue-tests

rtk swift test --disable-sandbox \
  --scratch-path /private/tmp/imklfx-phase6.UooiW5/SwiftTests \
  -Xswiftc -warnings-as-errors
```

Release Swift tools use `-gnone` so distributable binaries do not embed local
source/build paths. The Debug tools retain debug information by design and are
not release packages.

## Build results and warnings

| Target | Result |
|---|---|
| Debug kext | Clean build passed; source warnings treated as errors |
| Release kext | Clean build passed; source warnings treated as errors |
| Debug updater and signer | Clean isolated SwiftPM build passed |
| Release updater and signer | Clean isolated SwiftPM build passed with debug info disabled for path hygiene |

The kext archive phase originally warned because it declared no output and
included the Release dSYM. Phase 6 corrected it: the phase now declares its ZIP
and packages only `IntelMKLFixup.kext`. The warning no longer appears.

Remaining console warnings were environmental:

- Xcode could not reach CoreSimulator services or user cache/log locations in
  the restricted build environment. This macOS kext target does not use a
  simulator.
- SwiftPM disabled inaccessible user-level caches and built from the explicit
  scratch paths instead.

An attempted prefix-mapped Swift build was rejected because Apple's linker
warned that remapped PCM paths did not exist. The accepted clean Release build
instead uses `-gnone`; it emitted no source/linker diagnostic and contains no
personal build path.

## Automated tests

### Pure C++ policy and catalogue

Both executables compiled under C++14 with `-Wall -Wextra -Werror -pedantic`
and exited 0. The suites cover:

- exact original and already-patched forms;
- every single-byte mutation of the function, before-context, and after-context;
- empty/null/truncated buffers, first/final valid positions, partial patch,
  unknown implementations, replacement bounds, and checked overflow;
- strict exact evidence, wrong CDHash/offset/signature, already patched,
  unrelated callback, and no search fallback;
- window boot gating, zero/one/two/many candidates, ambiguity across multiple
  definitions, full-callback coverage, cross-window/page rejection, and
  start/end context rejection;
- changed Discord version, arbitrary CDHash, and changed target inside the page;
- target outside the page; wrong signing identifier, Team ID, signing policy,
  basename, and path; identical bytes in a non-whitelisted application; and
- strict precedence plus a synthetic second application reusing the same MKL
  definition without modifying the engine.

### Swift updater and manifest

Swift executed **25 tests with 0 failures**. Coverage includes schema version 3,
StrictVariant and BoundedWindow representation, page alignment/size, forbidden
fixed window offset, reserved ImageScan, malformed/unknown records, signatures,
tampering, wrong key ID/key, downgrade and same-version substitution rejection,
atomic install/rollback, corrupt state, symlink rejection, serialized concurrent
install, interrupted install, oversized/interrupted download, malformed GitHub
response, duplicate assets, and prerelease filtering/opt-in.

Both Debug and Release updater binaries also validated
`whitelist/manifest.json` as version 3 with one application rule and two image
variants. `jq` parsed the manifest and both formal schema documents. There was
no separately installed third-party JSON Schema engine; schema semantics are
enforced by the reviewed Swift validator and its tests.

Successful manifest installation still has **no kext runtime effect**. The
userspace store is not read at boot or in the callback.

## Sanitizers

Policy and catalogue tests were rebuilt with:

```text
-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined
```

Both passed with `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1`. No AddressSanitizer or UndefinedBehaviorSanitizer
finding occurred. Apple's sanitizer runtime reported that LeakSanitizer is not
supported on this platform when `detect_leaks=1`; leak detection was therefore
not claimed. The pure C++ policy path performs no dynamic allocation.

## Static analysis

Debug and Release `xcodebuild analyze` runs succeeded with warnings-as-errors.
Clang 17 generated analyzer results for the real `IntelMKLFixup.cpp` translation
unit, generated bundle metadata, and Lilu `plugin_start`; all diagnostic arrays
were empty.

This means the analyzer found no path it knows how to diagnose. It does **not**
model Lilu's live routing, XNU private-symbol ABI changes, callback scheduling,
code-signing blob lifetime, kernel write-protection effects, or concurrent
mutation by the kernel. Host tests and static analysis do not establish kernel
runtime safety.

## Plist, manifest, and whitespace validation

- Source, Debug, and Release Info.plists passed `plutil -lint`.
- The built Info.plists agree on bundle ID and version.
- `whitelist/manifest.json` passed both tool configurations' semantic validator.
- Manifest and signature schema JSON files passed strict JSON parsing with `jq`.
- `git diff --check` passed.

## Artifacts and hashes

Artifacts are intentionally ignored under:

```text
/Users/richardhedges/Desktop/IntelMKLFixup/build/Phase6
```

| Artifact | SHA-256 |
|---|---|
| Debug kext executable | `6b589e691d18729ef6bedc619d5989840a00522c15c34193a8199a44e16862ba` |
| Debug ZIP | `944fd8db07364b1788cf2138577cdf3fed99f98e464f906abaf83edc98905d87` |
| Debug updater | `560f8cce44500d97763c81fa1352c568ae7b3d30f69ac38161316d9b71034a4b` |
| Debug signer | `8ca1811b3d4c414de5b9ba92b95c48dc3432af0e8b605cd8e04858e0447be0e4` |
| Release kext executable | `9b68af38ec220c033e1913d8e841a26525aedb72699e578e97b94e09c19359d5` |
| Release ZIP | `48b53d6d5ba3f0629c8313f836ab101cbc8e43a9b97e2b5f71676f2e44cc22a1` |
| Release updater | `d6855f408feec146773814536ef0081d3767efb30a10ced594a449161e3f33a3` |
| Release signer | `139096b4840d760cfc8002ad91fdd35fbfe988a9284d7c97c827a737874f328c` |

Exact kext paths:

```text
/Users/richardhedges/Desktop/IntelMKLFixup/build/Phase6/Debug/IntelMKLFixup.kext
/Users/richardhedges/Desktop/IntelMKLFixup/build/Phase6/Release/IntelMKLFixup.kext
```

Both are thin x86_64 Mach-O kext bundles. Both contain only `Info.plist`, the
executable, and Xcode's `_CodeSignature/CodeResources`. `codesign --verify
--deep --strict` passed. Xcode used local ad-hoc identity “Sign to Run Locally”;
no certificate, Team ID, notarization, private key, or signing secret was used.

The Debug and Release executables have different hashes, sizes, code-section
sizes, and symbol counts, while their Info.plists are byte-identical. This is
consistent with optimization/stripping differences.

`otool -L` showed no linked userspace dylibrary. Undefined symbols are the
expected XNU/IOKit kernel interfaces, Lilu/KernelPatcher interfaces, boot-arg
parser, vnode/code-signing helpers, and compiler memory/stack-check intrinsics.
No unexpected framework or updater/network dependency appears.

## Packaging inspection

Each generated ZIP contains exactly:

```text
IntelMKLFixup.kext/Contents/Info.plist
IntelMKLFixup.kext/Contents/MacOS/IntelMKLFixup
```

There is no `.git`, `.build`, `build`, DerivedData, `.DS_Store`, dependency
checkout, dSYM, cache, source checkout, personal path, or signing secret. The
separate local Release dSYM is retained beside the handoff artifacts for crash
symbolication and intentionally excluded from the ZIP because debug data holds
local source paths.

The Xcode archive phase runs before Xcode's automatic ad-hoc signing step, so
the ZIP contains the minimal unsigned kext, whereas the direct post-build kext
contains an ad-hoc signature. The embedded ZIP executable hashes are:

- Debug: `6e16c31851c7884aefc9427ff6b1a834147b9c11e053dc15494e8234a671f4d7`;
- Release: `0a798e729f832be0e75c5b5312109648db855dbfd73f394e1129e1df9e5628d1`.

The manual plan pins and uses the inspected post-build Release bundle, not an
extracted ZIP. Before a public release, the project should explicitly choose
and document whether release ZIPs remain unsigned OpenCore payloads or are
packaged after local signing.

The kext binaries contain the intentional generic path-grammar literal
`/Users/`; neither contains the tester's name, repository path, temporary build
path, secrets, or private key material. Release userspace binaries were built
without debug info and likewise contain no personal build path.

## Changes made during Phase 6

- Added missing policy/catalogue regression assertions.
- Expanded updater tests for all requested malformed, transport, signing,
  concurrency, rollback, and filesystem cases.
- Added an exclusive updater-store lock, no-follow regular-file reads, and
  real-directory/symlink validation so concurrent or redirected installs fail
  closed.
- Expanded `.gitignore` for generated products.
- Made kext ZIP output explicit and minimal; dSYMs are no longer distributed.
- Aligned CI with the validated warnings-as-errors settings and path-clean
  Release Swift build (`-gnone`).
- Authored `TEST_PLAN_3900X.md`.

No kernel matching, StrictVariant, BoundedWindow, Discord catalogue, or patch
bytes were changed during Phase 6.

## Unresolved risks and blockers

1. The kext was subsequently loaded on the Ryzen 9 3900X. Darwin 24 symbol
   resolution and Lilu routing succeeded, but the active callback write was
   proven unsafe because it mutated the vnode-backed page and bytes returned by
   ordinary file reads.
2. The installed Discord module is known to be patched on disk. StrictVariant
   approval requires restoring and hashing the exact original. A patched/ad-hoc
   module is expected to fail the signing/CDHash gate.
3. The one strict CDHash fixture is Discord Stable 0.0.403. Its provenance must
   be confirmed manually against the restored active file before an active test.
4. BoundedWindow proves uniqueness only in `0x650000..<0x651000`. Moving the
   function to another page requires a reviewed policy/kext update.
5. The userspace updater's production release public key remains deliberately
   unconfigured and fails closed. Signed-update deployment is not release-ready.
6. Installed manifests do not affect runtime policy. Supporting a completely
   new application still requires a reviewed compiled catalogue and kext build.
7. LeakSanitizer was unavailable; Xcode static analysis cannot prove kernel
   concurrency or ABI safety.
8. Release ZIP signing policy remains a release-readiness decision as described
   above; the manual test uses the inspected direct bundle.

## Safety-boundary confirmation

Phase 6 did not install a kext, mount or modify an EFI, edit OpenCore or
`config.plist`, change boot arguments, invoke `sudo`, terminate or modify
Discord, invoke the Swift patcher, reboot, shut down, log out, or perform live
kernel testing.

Phase 7 later produced the now-revoked 1.0.0-rc1 candidate. No existing build
artifact is approved for active runtime patching.
