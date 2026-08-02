# Release candidate validation report

## Recommendation

**Revoked: not ready for installation or active runtime testing.**

Controlled Ryzen 9 3900X testing proved that the active validation-page write
changed the vnode-backed Discord module and cache-visible file contents. The
artefact hashes below identify historical, unsafe-for-active-use products; they
must not be installed as a runtime patch. See `FILE_BACKED_WRITE_INCIDENT.md`.

The previous dry-run recommendation is superseded. Current source is
detection-only and has not been rebuilt in this incident response.

## Source state

- Code and pre-existing documentation commit built:
  `1cabee7c23a06fccfaa646c016b381434779898f`.
- Branch at build start: `main`, 18 commits ahead of `origin/main`.
- `git status --short --branch` was clean before generated dependencies and
  build products were created.
- `git diff --check` passed before builds and after release documentation.
- Fresh official source checkouts were used for pinned dependencies:
  - Lilu `e4748cc081bf060302c7d3c44a643ce1d11b7e1d` (tag 1.7.2);
  - MacKernelSDK `05094e5e88cec7caedbfb35e8449ed0db94bf95b`.
- Generated dependencies, build trees, and `Release/` are ignored and are not
  tracked source inputs. A clean checkout remains buildable through the pinned
  workflow.

The root release notes, changelog, README update, and this report are release
metadata created after the tested code commit; they do not change kernel or
userspace implementation.

## Build environment

| Item | Value |
|---|---|
| macOS | 15.7.7 (24G720) |
| Darwin | 24.6.0, XNU `11417.140.69.710.16~1` |
| Host architecture | x86_64 |
| Xcode | 16.4 (16F6) |
| Apple Clang | 17.0.0 (`clang-1700.0.13.5`) |
| Swift | 6.1.2 (`swiftlang-6.1.2.1.2`) |
| macOS SDK | 15.5 (24F74) |
| Kext C++ standard | C++14 (`c++1y`) |
| Kext deployment setting | macOS 10.13; runtime catalogue gates Darwin 24 only |
| Userspace deployment target | macOS 13 |

All final builds used isolated directories beneath
`/private/tmp/imklfx-phase7.f9crkH`; no pre-existing `.build` or DerivedData
product was used.

## Build results

| Target | Result |
|---|---|
| Pinned Lilu Debug SDK | Passed from fresh source |
| IntelMKLFixup Debug kext | Clean build passed |
| IntelMKLFixup Release kext | Clean build passed |
| Debug updater/signer tooling | Clean SwiftPM build passed |
| Release updater/signer tooling | Clean SwiftPM build passed with `-gnone` |

IntelMKLFixup C/C++ and Swift source warnings were treated as errors. The final
four project builds had no source warning or failure.

The first preparatory Lilu invocation attempted Xcode's user DerivedData path
and was denied by the workspace sandbox before compilation. It was discarded
and rerun successfully as a clean build with an explicit temporary
`-derivedDataPath`. This was an environment-path failure, not a source/build
failure in the accepted artifacts.

## Tests and automated validation

- Pure C++ policy tests: passed with C++14, `-Wall -Wextra -Werror -pedantic`.
- C++ catalogue tests: passed with the same flags.
- AddressSanitizer plus UndefinedBehaviorSanitizer policy tests: passed.
- AddressSanitizer plus UndefinedBehaviorSanitizer catalogue tests: passed.
- Swift updater/manifest tests: **25 passed, 0 failed**.
- Debug and Release manifest CLI validation: passed; schema version 3, one
  application rule, two image variants.
- Manifest and both JSON schema documents: parsed successfully with `jq`.
- Source, Debug, Release, and packaged Info.plists: passed `plutil -lint`.
- Debug and Release `xcodebuild analyze`: passed with empty Clang diagnostic
  arrays for the real kext translation unit.
- Final package code-signature verification: passed.

LeakSanitizer is unsupported by Apple's sanitizer runtime on this host, so leak
detection is not claimed. The pure policy/catalogue test path does not allocate
dynamically.

Static analysis and host tests do not prove live XNU/Lilu routing, private ABI,
pointer lifetime, write protection, or callback concurrency safety.

## Warnings

Accepted warning categories were external to IntelMKLFixup source:

- Xcode could not access CoreSimulator services or user cache/log directories
  in the restricted workspace. The macOS kext target does not use Simulator.
- SwiftPM disabled inaccessible user-level caches and used the explicit clean
  scratch paths.
- Pinned Lilu 1.7.2 reports an upstream macOS 10.6 deployment target warning
  under Xcode 16.4 and undeclared outputs for its `Copy SDK` and `Archive`
  scripts. Its build still completed successfully.

No warning was suppressed to make IntelMKLFixup compile.

## Release kext inspection

Packaged path:

```text
Release/IntelMKLFixup.kext
```

| Property | Result |
|---|---|
| Bundle identifier | `com.github.whatdahopper.IntelMKLFixup` |
| Short/bundle version | `1.0.0` / `1.0.0` |
| Executable | thin Mach-O 64-bit kext bundle |
| Architecture | x86_64 only |
| Local signature | valid ad-hoc “Sign to Run Locally”; no Team ID/certificate |
| Bundle files | Info.plist, executable, `_CodeSignature/CodeResources` only |
| Debug material | no `__DWARF` section, dSYM, object, archive, or test fixture |
| Personal paths | none in the kext executable |
| Embedded secrets | none detected |
| Network/userspace dependencies | none detected |

`otool -L` reports no linked userspace dylibrary or framework. Info.plist
declares the expected Lilu dependency and Apple kernel KPIs: BSD, DSEP, IOKit,
libkern, Mach, and unsupported KPI.

The only externally defined global symbols are the expected
`IntelMKLFixup` IOKit metaclass/constructors/destructors, `probe`, `start`,
`stop`, class vtables, and `_kmod_info`. Undefined imports are the expected
IOKit/XNU class surface, Lilu/KernelPatcher APIs, boot-argument parser,
`vn_getpath`, vnode/code-signing helpers, and compiler memory/stack intrinsics.
No CFNetwork, URLSession, socket, filesystem-loader, or updater symbol is linked.

The generic literal `/Users/` is part of the bounded Discord path grammar; the
tester name, repository path, temporary path, keys, tokens, and private-key
markers are absent.

## SHA-256 inventory

Because a `.kext` is a directory, both a stable bundle-content inventory digest
and the directly verifiable executable digest are recorded. The inventory
digest hashes the ordered SHA-256 lines for the relative paths
`Contents/Info.plist`, `Contents/MacOS/IntelMKLFixup`, and
`Contents/_CodeSignature/CodeResources`.

| Artifact | SHA-256 |
|---|---|
| Release kext bundle-content inventory | `3c32d118babb93ec67a6eb85a45b539960ead2fad1e6f2dc40343174ff3ad8dd` |
| Release kext executable | `69407f5369c15cd6c1d6317ce954e61981dd39a5d09d7a47124550d7e0808403` |
| Debug kext bundle-content inventory | `9e0651efd06b3dad2798db0c60f35b7a5c8705744744a8da5bc52a3c00d89998` |
| Debug kext executable | `6b589e691d18729ef6bedc619d5989840a00522c15c34193a8199a44e16862ba` |
| Release userspace updater | `d6855f408feec146773814536ef0081d3767efb30a10ced594a449161e3f33a3` |
| Whitelist manifest | `15ecc63b3751260915a459a87d83d1a030af7a3bddb0bdd7949c3438cbaf2d0d` |

The userspace updater and manifest were validated and hashed but intentionally
are not in the requested minimal `Release/` directory.

## Package contents

`Release/` contains only:

```text
IntelMKLFixup.kext
README.md
CHANGELOG.md
TEST_PLAN_3900X.md
PHASE6_REPORT.md
```

The four documents are byte-identical copies of their root counterparts. The
package contains no `.git`, `.build`, DerivedData, dSYM, object file, archive,
temporary file, test fixture, dependency checkout, or cache.

## Unresolved issues and risks

1. The release candidate was loaded into the Ryzen 9 3900X kernel and its
   validation-page write changed cache/file-read-visible Discord module bytes.
2. The active architecture is revoked; application functionality cannot make
   this destination safe.
3. The affected Discord module must be restored and verified against original
   SHA-256 `de061edb4387fc5bba2b8535483aa2f4347c17bc9e4d25babef36172c86a9f9a`.
4. StrictVariant and BoundedWindow are detection policies only in current
   source. The revoked binary does not contain that source mitigation.
5. Future userspace-assisted image-wide pre-scan remains design-only.
6. The production manifest trust key is unconfigured, and installed manifests
   have no runtime effect.
7. The bundle is locally ad-hoc signed and is neither Developer ID signed nor
   notarized.
8. The kext bundle version remains `1.0.0`; `1.0.0-rc1` is the release-candidate
   label rather than a changed bundle version.

## Safety-boundary confirmation

Phase 7 did not mount an EFI, modify OpenCore or `config.plist`, invoke `sudo`,
install a kext, change boot arguments, reboot, terminate or modify Discord, or
invoke/modify the separate Swift patcher.

Stop after this package. The next action, if approved by the owner, is the
manual StrictVariant dry-run—not an automatic installation or runtime test.
