# IntelMKLFixup Phase 1 audit

Audit date: 2026-08-01  
Audited repository state: `main` at `5181e8cbc860a234e353e97a7eb59dc34b617830`  
Release source compared: prerelease `1.0.0`, tag commit `52212946bfbccb4dc617236d2505fd4889387669`  
Audit status: **complete; no implementation was performed**

## 2026-08-01 hardware-incident addendum

**Critical: active validation-page patching is unsafe and must not be used.**

Subsequent controlled Ryzen 9 3900X testing of commit `7a02733` proved that the
six-byte write through `_cs_validate_page` changed the vnode-backed
`discord_krisp.node` page and the bytes returned by ordinary file reads. The
hash changed from
`de061edb4387fc5bba2b8535483aa2f4347c17bc9e4d25babef36172c86a9f9a` to
`95e611b3bd89d95d67f1e809eb1cbefcc8eedbba3bd5b70bf11cf044cf72b27d`;
offset `0x650100` changed from `53 48 83 ec 20 8b` to
`b8 01 00 00 00 c3`. The inode and timestamps did not change.

Apple XNU 11417.140.69 shows that the callback pointer is a const kernel alias
of the actual `vm_page` in an external vnode-pager object. On the x86_64
one-page path, `vm_paging_map_object` can return a direct `phystokv` alias.
Writing after original validation mutates the shared resident page without a
normal vnode/UPL write transaction or metadata update. Dirty/writeback state
is not coherently controlled.

The current source has removed every validation-page write. Dry-run detection
remains; active mode fails closed with
`outcome=unsafe-file-backed-write-blocked modified=no`. The 1.0.0-rc1 release
candidate is revoked for active use. Full evidence and replacement options are
in `FILE_BACKED_WRITE_INCIDENT.md`.

## Executive conclusion

**Final recommendation for the current source and the published 1.0.0 binaries: Do not install.**

Two critical defects block a controlled test of the current release:

1. The plugin scans system-wide code-signing validation buffers and rewrites every matching 23-byte sequence without checking the application, process, image identity, Mach-O identity, or surrounding instructions (`IntelMKLFixup/IntelMKLFixup.cpp:42-53`). A false positive changes already-validated bytes in an unrelated file-backed VM page.
2. The High Sierra-through-Catalina wrapper is declared `void`, although Apple's `_cs_validate_range` returns `boolean_t` (`IntelMKLFixup/IntelMKLFixup.cpp:67-75`). It calls the original but discards its return value, so Darwin 17-19 callers receive an undefined result.

Both defects can be mitigated with small, reviewable source changes. They are not reasons to abandon the Lilu architecture, but they are reasons not to boot the current build. A future controlled test should use a build made from reviewed, pinned source after the ABI, targeting, dependency, and patch-validation changes in this report. It should not use the existing release binary.

The audit host was detected rather than assumed: macOS 15.7.7 (24G720), Darwin 24.6.0, x86_64, Xcode 16.4, Apple clang 17.0.0. The source's normal route on this host is `_cs_validate_page`. Apple has since published the tested kernel's base XNU tag, `xnu-11417.140.69`; the running build reports the downstream suffix `11417.140.69.710.16~1`. The route was observed to install, but that does not make the callback writable or establish ABI support beyond this tested build.

## Scope and evidence

The repository contains one implementation file, one plist, one Xcode project, one CI workflow, the README, `.gitignore`, and GPLv3. There are no submodules, vendored Lilu headers, tests, fixtures, patch catalogues, whitelist files, or release tooling in the checkout.

Primary evidence inspected:

- Every tracked file and all six local commits.
- The [initial 1.0.0 prerelease](https://github.com/Carnations-Botanica/IntelMKLFixup/releases), both hosted branches, all three issues, and the single open pull request.
- Lilu source at `8e8eb256`, the revision at the tip of Lilu `master` when the release workflow ran in October 2024. In particular: [plugin startup](https://github.com/acidanthera/Lilu/blob/8e8eb256c5d8fa50d44b49255a55f093132d2f25/Lilu/Library/plugin_start.cpp#L55), [`onPatcherLoadForce`](https://github.com/acidanthera/Lilu/blob/8e8eb256c5d8fa50d44b49255a55f093132d2f25/Lilu/Headers/kern_api.hpp#L142), [routing and masked replacement](https://github.com/acidanthera/Lilu/blob/8e8eb256c5d8fa50d44b49255a55f093132d2f25/Lilu/Sources/kern_patcher.cpp#L605), and [kernel-write protection handling](https://github.com/acidanthera/Lilu/blob/8e8eb256c5d8fa50d44b49255a55f093132d2f25/Lilu/Sources/kern_mach.cpp#L301).
- Apple XNU source for representative Darwin 17 through Darwin 24 releases. The transition is visible in the [Darwin 17 prototype](https://github.com/apple-oss-distributions/xnu/blob/76e12aa3ea3036173a61fa1081c3be890e626e79/osfmk/vm/vm_protos.h#L451-L458) and the added [Darwin 20 page-validation prototype](https://github.com/apple-oss-distributions/xnu/blob/bb611c8fecc755a0d8e56e2fa51513527c5b7a0e/osfmk/vm/vm_protos.h#L503-L518).

No release binary was downloaded or executed. The binary-to-source correspondence and binary hash are therefore not asserted. The workflow's mutable dependencies also prevent reconstructing that correspondence from the repository alone.

## Architecture overview

IntelMKLFixup is not a normal userspace binary patcher and does not resolve an MKL symbol. It is an x86_64 Lilu plugin that routes a private XNU code-signing validation function. The audited historical implementation invoked XNU's original validator, obtained the vnode path, scanned the callback buffer, and overwrote matches in the vnode-pager-backed VM page. Hardware testing later proved that ordinary reads of the backing file returned those changed bytes; the claim that the disk file remained unchanged was false. Current source retains detection but blocks every active validation-page write.

The data path is:

`kext load` -> `Lilu plugin_start` -> `Lilu shouldLoad` -> `onPatcherLoadForce` -> `routeMultipleLong` -> XNU code-signing callback -> original XNU validation -> `vn_getpath` -> masked byte search -> temporary kernel write enable -> replacement -> log

The Xcode project compiles only `IntelMKLFixup.cpp` plus Lilu's generated/copied `plugin_start.cpp` (`IntelMKLFixup.xcodeproj/project.pbxproj:228-234`). It links `libkmod.a` and consumes the headers/resources copied into `Lilu.kext` during bootstrap (`project.pbxproj:55-96`, `339-377`). Consequently, the repository is not self-contained: plugin startup and much of the patch engine come from the Lilu revision present at build time.

## Complete execution flow

### 1. Kernel extension and plugin startup

`Info.plist` declares a root-required kext, an `IOResources`/`IOKit` personality, and a dependency on `as.vit9696.Lilu` (`IntelMKLFixup/Info.plist:21-59`). The generated module entry points are `IntelMKLFixup_kern_start` and `IntelMKLFixup_kern_stop` (`project.pbxproj:357-361`, `406-410`).

Lilu's `plugin_start.cpp` performs the actual module startup. It:

1. Requests Lilu API access.
2. Calls `shouldLoad` with this plugin's run mode, boot arguments, and kernel bounds.
3. Sets `startSuccess` and invokes the plugin's configured lambda only if allowed.
4. Returns `KERN_SUCCESS` even when disabled so I/O Kit can unload the inactive plugin.
5. Refuses unload after successful activation.

The plugin allows normal mode only and declares:

- Disable: `-imklfxoff` (`IntelMKLFixup.cpp:77-79`).
- Debug: `-imklfxdbg` (`IntelMKLFixup.cpp:81-83`).
- Unsupported-OS override: `-imklfxbeta` (`IntelMKLFixup.cpp:85-87`).
- Nominal kernel range: High Sierra through Sequoia (`IntelMKLFixup.cpp:89-100`).

There is no CPU-vendor check. The plugin is built for x86_64 (`project.pbxproj:242`, `292`) but is eligible on Intel as well as AMD x86_64 systems.

### 2. Lilu registration and callbacks

The plugin-start lambda logs a debug-only load message and registers one patcher-load callback with `lilu.onPatcherLoadForce` (`IntelMKLFixup.cpp:101-112`). The `Force` variant intentionally panics if Lilu cannot allocate/store the callback. Under normal ordering this should succeed, but it is still a boot-time panic path.

Inside the patcher-load callback, routing is attempted only when Lilu reports `RunningNormal` (`IntelMKLFixup.cpp:104-110`). No process-load, binary-load, kext-load, vnode, or exec callback is registered.

### 3. Kernel symbol resolution and routing

Exactly one private XNU function is routed at a time:

| Source-selected OS range | Darwin | Routed symbol | Wrapper | XNU return type | Audit result |
|---|---:|---|---|---|---|
| macOS 10.13 High Sierra | 17 | `_cs_validate_range` | `wrapCsValidateRangeHighSierra` | `boolean_t` | **Wrapper ABI is wrong (`void`)** |
| macOS 10.14 Mojave | 18 | `_cs_validate_range` | same | `boolean_t` | **Wrapper ABI is wrong (`void`)** |
| macOS 10.15 Catalina | 19 | `_cs_validate_range` | same | `boolean_t` | **Wrapper ABI is wrong (`void`)** |
| macOS 11 Big Sur | 20 | `_cs_validate_page` | `wrapCsValidatePageBigSur` | `void` | Signature matches public XNU |
| macOS 12 Monterey | 21 | `_cs_validate_page` | same | `void` | Signature matches public XNU |
| macOS 13 Ventura | 22 | `_cs_validate_page` | same | `void` | Signature matches public XNU |
| macOS 14 Sonoma | 23 | `_cs_validate_page` | same | `void` | Signature matches public XNU |
| macOS 15 Sequoia | 24 | `_cs_validate_page` | same | `void` | Signature matches available public XNU snapshots |

The selection is a single `getKernelVersion() >= KernelVersion::BigSur` conditional (`IntelMKLFixup.cpp:106-108`). Darwin 20 and newer still contain `_cs_validate_range`, and XNU still uses it for some sub-page and initial Mach-O validation paths; the plugin chooses not to route those paths on newer systems.

`routeMultipleLong` resolves the symbol from Lilu's kernel `MachInfo`, constructs a trampoline, stores it in the single global `orgCsValidateFunc`, and installs a long route. Lilu writes the trampoline pointer before activating the route. If symbol resolution or routing fails, Lilu and the plugin log the failure, the hook is not installed, and boot continues (`IntelMKLFixup.cpp:109-110`). The plugin does not resolve `_mkl_serv_intel_cpu_true`; the MKL name in the success log is inferred solely from the byte match.

### 4. Invocation of original XNU validation

Both wrappers call the original function first (`IntelMKLFixup.cpp:63`, `73`). Patching therefore happens **after** XNU hashes and classifies the original bytes.

- Darwin 20-24: the original `void cs_validate_page(...)` is called with the correct apparent ABI, then a full `PAGE_SIZE` is scanned (`IntelMKLFixup.cpp:56-65`).
- Darwin 17-19: the original `boolean_t cs_validate_range(...)` is called through a function pointer cast derived from the plugin's `void` wrapper. Its result is discarded, the buffer is scanned, and the wrapper returns without a defined integer result (`IntelMKLFixup.cpp:67-75`). Lilu's `FunctionCast` is only a `reinterpret_cast` to the wrapper's function-pointer type; it does not repair the ABI.

Because validation precedes modification, XNU's validation flags describe the original page, not the modified page. That is the mechanism by which the plugin supplies patched code while leaving the file signature and disk contents alone, but it also bypasses the integrity relationship XNU normally maintains between validated bytes and mapped bytes.

### 5. Memory ranges selected for scanning

`wrapCsValidate` scans exactly the `data` and `size` supplied by its wrapper (`IntelMKLFixup.cpp:42-53`). It does not locate Mach-O executable segments itself.

- Darwin 17-19: it scans every range delivered to routed `_cs_validate_range`, using XNU's explicit `size`. Public XNU call sites pass both full pages and smaller chunks.
- Darwin 20-24: it scans `PAGE_SIZE` bytes for every routed `_cs_validate_page` call. Public XNU calls this while validating file-backed pages of code-signed userspace VM objects. The validation predicate is not restricted to `VM_PROT_EXECUTE`, so signed data pages can also reach the hook.

The current hook is system-wide. It can inspect pages belonging to application executables, frameworks, dylibs, bundles, native modules such as `.node` files, and other code-signed file-backed userspace objects. It does not intentionally scan anonymous memory, unsigned objects, or kernel-space mappings. It silently skips a callback if `vn_getpath` cannot construct a vnode path (`IntelMKLFixup.cpp:43-45`).

No current-process identity is collected. The resolved vnode path is used only after the callback begins and only for logging; it does not constrain the scan. Because the modified VM page can be shared, process identity alone would not be an adequate future boundary.

### 6. Signature matching

There is one unnamed, 23-byte pattern (`IntelMKLFixup.cpp:17-28`):

```text
53 48 83 EC 20 8B 35 00 00 00 00 85 F6 7C 08 89 F0 48 83 C4 20 5B C3
FF FF FF FF FF FF FF 00 00 00 00 FF FF FF FF FF FF FF FF FF FF FF FF
```

Mask semantics are `(candidate_byte & mask_byte) == pattern_byte`. Bytes 7-10 are wildcarded and the pattern stores zero in those positions, so the four-byte RIP-relative displacement is ignored. The other 19 bytes must match exactly. There is no validation before byte 0 or after byte 22, no Mach-O UUID/version check, and no proof that the match begins at a function boundary or in an executable segment.

Using zero-based offsets, the fixed portion disassembles as:

```text
00: 53                         push rbx
01: 48 83 EC 20                sub  rsp, 0x20
05: 8B 35 xx xx xx xx          mov  esi, dword ptr [rip + disp32]
11: 85 F6                      test esi, esi
13: 7C 08                      jl   +8              ; target is offset 23
15: 89 F0                      mov  eax, esi
17: 48 83 C4 20                add  rsp, 0x20
21: 5B                         pop  rbx
22: C3                         ret
```

This is consistent with a function reading a cached integer, returning it when non-negative, and branching at offset 23 to a slow path when negative. The repository labels it `_mkl_serv_intel_cpu_true`, but it is not symbol-resolved, so the identity cannot be established from the runtime code alone.

Lilu's search bounds are correct for this call: it rejects `dataSize < 23`, calculates `lastOffset = dataSize - 23`, and permits a match ending exactly at the buffer end. A pattern split across two callbacks is not matched; there is no carry-over buffer.

### 7. Memory replacement

The replacement and mask are both 23 bytes and the mask is all `FF` (`IntelMKLFixup.cpp:29-38`):

```text
55 48 89 E5 B8 01 00 00 00 5D C3 00 00 00 00 00 00 00 00 00 00 00 00
FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF
```

Disassembly of the reachable replacement is:

```text
00: 55                         push rbp
01: 48 89 E5                   mov  rbp, rsp
04: B8 01 00 00 00             mov  eax, 1
09: 5D                         pop  rbp
10: C3                         ret
11-22: 00 ... 00               unreachable from the normal entry; overwritten padding/tail
```

For an ordinary x86_64 System V call entering at offset 0, this is precisely an integer return of `1`/true with a balanced stack. It does not replace or redirect any BLAS, FFT, vector, allocator, or other numerical MKL routine. It replaces only the matched predicate entry sequence. It does not guarantee that subsequent MKL dispatch or numerical code is valid on AMD.

The twelve zero bytes after `ret` are not needed to return true. They overwrite the rest of the 23-byte matched window and would decode as memory-modifying instructions if control entered there. The searched original window makes normal fall-through into them impossible, but the project does not prove there are no nonstandard interior entry points.

Lilu enables kernel writing, performs a byte-at-a-time masked replacement, restores write protection, and releases its simple lock. The current arrays use Lilu's equal-size template overload, so the replacement cannot exceed the verified 23-byte target in this particular call.

### 8. Repetition, logging, and failure handling

The call passes `count = 0` and `skip = 0` (`IntelMKLFixup.cpp:49-51`). In Lilu, zero count means **replace every match in the callback range**, not only one.

There is no patch registry. After replacement, the same bytes no longer satisfy the search pattern, so a sequential second scan naturally skips them. A newly faulted copy of the original page can be patched again. Concurrent scanners can both discover the original bytes before either acquires Lilu's write lock; the second can then rewrite the already-replaced bytes because the match is not revalidated under the lock. The write is idempotent here, but duplicate application is not explicitly prevented or reported.

Failure behavior:

- Lilu missing or too old: the kext dependency/link should prevent normal activation, but the declared Lilu floor is incorrect; see H-1.
- `-imklfxoff`, unsupported normal OS without beta, or non-normal run mode: Lilu declines activation and the plugin does nothing.
- Patcher callback allocation failure: `onPatcherLoadForce` panics intentionally.
- Kernel symbol or route failure: log and continue boot without a hook.
- `vn_getpath` failure: silently skip the page.
- Buffer shorter than 23 bytes or no signature: silently continue.
- Write-enable failure: Lilu logs and returns false; plugin emits no success log.
- Write-protection restore failure: Lilu logs but still returns success if it changed bytes; the plugin can then log `Patched`, even though protection restoration failed.
- Match: unconditional `SYSLOG` includes the full vnode path (`IntelMKLFixup.cpp:52`), even without explicit debug mode.
- No MKL code ever loaded: no match, no patch, no panic, but the system-wide per-page path and scan overhead remains.

## Findings by severity

### Critical

#### C-1 — Unrestricted system-wide post-validation code modification

**Location:** `IntelMKLFixup/IntelMKLFixup.cpp:42-53`  
**Impact:** unrelated executable or signed data pages can be changed if their bytes match; a false positive can crash a process, corrupt shared in-memory code, or destabilise the system. The page is changed after XNU validated the original bytes.

The application path is not checked at all. The 23-byte pattern has four wildcard bytes and no surrounding context, function-boundary, executable-segment, code-signing identity, UUID, or supported-MKL-version check. Every match in the supplied range is replaced. The upstream release itself acknowledges that it searches every binary and calls that behaviour bad.

**Required remediation:** fail closed before scanning. Require an approved image identity, an approved process identity where reliable, an exact separately named MKL patch definition, executable-segment/file-offset bounds where obtainable, and surrounding context. Bound the number of accepted matches. Do not load actual-patch mode until this is complete.

#### C-2 — Wrong `_cs_validate_range` wrapper ABI on Darwin 17-19

**Location:** `IntelMKLFixup/IntelMKLFixup.cpp:67-75`  
**Impact:** undefined return value from a kernel code-signing validation function; possible validation failures, process termination, kernel instability, or boot failure on High Sierra, Mojave, and Catalina.

Apple declares and implements `boolean_t cs_validate_range(...)`. The wrapper returns `void`, and the original result is discarded. This is not merely incomplete error handling; it changes the routed kernel ABI.

**Required remediation:** declare the wrapper `boolean_t`, store the original result, run only bounded post-validation policy/patch logic, and return the original result unchanged. Add a compile-time function type and tests for wrapper declarations. Until corrected, Darwin 17-19 must be excluded rather than claimed supported.

### High

#### H-1 — Incorrect minimum Lilu dependency

**Location:** `IntelMKLFixup/Info.plist:41-44`; `IntelMKLFixup.cpp:49`, `109`  
**Impact:** unresolved imports or plugin load failure with a Lilu version that the plist says is compatible; possible boot trouble depending on injection configuration.

The plist declares Lilu 1.2.0. Lilu 1.2.0 has neither `routeMultipleLong` nor `findAndReplaceWithMask`. `routeMultipleLong` is present by Lilu 1.5.5, while the masked helper used here is present by Lilu 1.6.1. Therefore 1.2.0 is not a valid runtime floor.

**Required remediation:** determine the exact minimum after the final code is compiled, set the plist and README to that version or newer, and test both the minimum and current Lilu releases. Pin the reviewed Lilu source used for builds.

#### H-2 — Validated-page state no longer describes mapped bytes

**Location:** `IntelMKLFixup.cpp:63-64`, `73-74`  
**Impact:** XNU validates one byte sequence and maps another. The effect can be visible through a shared VM page, not solely to the process that happened to fault it.

This is intrinsic to the current technique and is the intended compatibility mechanism, but it raises the consequence of any identity or signature error. It also means a process-only whitelist is insufficient.

**Required remediation:** preserve the order (original first) only after confirming it is required, then make the image/vnode identity the primary boundary, add process identity as a second condition, and ensure replacement occurs before first mapping in the observed XNU path. Document the integrity trade-off explicitly.

#### H-3 — Private XNU hooks treated as stable across eight OS generations

**Location:** `IntelMKLFixup.cpp:99-110`  
**Impact:** an ABI, symbol, call-context, range, or lifetime change can turn a compatible-looking route into a kernel crash.

The source has one version threshold and no per-build validation. Public XNU confirms the inspected prototypes, but private symbols are not a supported kext KPI. `-imklfxbeta` can force the plugin beyond its stated maximum. Open PR #4 only changes the enum from Sequoia to Tahoe; it provides no XNU ABI or callback-context analysis.

**Required remediation:** keep explicit, evidence-backed kernel ranges; decline unknown Darwin majors; verify symbol ABI and range semantics per supported major; never describe a one-line maximum-version bump as compatibility support.

#### H-4 — Mutable, non-reproducible release build inputs

**Location:** `.github/workflows/main.yml:18-31`; release version at tag `5221294` used equivalent lines 20-33  
**Impact:** a release build can consume different Lilu/MacKernelSDK source or execute changed bootstrap code without a change to this repository.

The workflow checks out MacKernelSDK without a ref and downloads then `eval`s scripts from the mutable `master` branches of `ocbuild` and Lilu. The release has no recorded dependency SHAs, source provenance, or hashes in the repository.

**Required remediation:** pin actions and source dependencies to reviewed immutable SHAs, avoid `curl | eval`-equivalent bootstrap, record toolchain and dependency identifiers, and publish hashes/build metadata. Build the first test artifact locally from the reviewed tree rather than trusting the existing release asset.

#### H-5 — No AMD CPU-vendor or supported-architecture policy

**Location:** `IntelMKLFixup.cpp:89-100`; `project.pbxproj:242`, `292`  
**Impact:** the plugin can route XNU and alter matching pages on any x86_64 Mac, including Intel systems where the bypass is unnecessary.

**Required remediation:** require x86_64 at build and an `AuthenticAMD` runtime vendor check before registering the route. Unknown vendors and architectures must fail closed. CPU family/model should be diagnostic metadata, not a reason to broaden patch patterns.

### Medium

#### M-1 — Insufficient local pointer and size validation

**Location:** `IntelMKLFixup.cpp:42-51`  

The wrapper does not check `vp`, `data`, `size`, or output pointers. Current public XNU supplies a live vnode and valid callback buffer, and Lilu's search rejects a buffer shorter than 23 bytes, so no direct out-of-bounds access was found under the audited contracts. A private-ABI/context change would remove that protection.

**Remediation:** check all locally usable preconditions; use compile-time array-length assertions; reject zero, null, unexpectedly large, misaligned, or contract-inconsistent ranges without dereferencing them.

#### M-2 — No explicit single/multiple-match or already-patched policy

**Location:** `IntelMKLFixup.cpp:49-51`  

Zero count patches all matches. There is no expected match count, state record, patch identifier, duplicate statistic, or already-patched pattern. Concurrent duplicate writes are possible because Lilu searches before taking its write lock.

**Superseded remediation:** match-count and already-patched checks remain useful
for detection, but no validation-buffer write is permitted. Any future
replacement must revalidate bytes in a verified process-private/COW mapping
immediately before a separately reviewed write.

#### M-3 — Split signatures are silently missed

**Location:** `IntelMKLFixup.cpp:56-75`; Lilu `kern_patcher.cpp:605-649`  

The helper is bounds-safe and accepts an end-of-buffer match, but it cannot match across two pages/ranges. This is safe failure, not memory corruption, but compatibility depends on function placement.

**Remediation:** explicitly decline split matches and document the limitation unless a safe, immutable adjacent-range mechanism can be proven. Do not read beyond the callback range.

#### M-4 — Unbounded aggregate hot-path work

**Location:** `IntelMKLFixup.cpp:42-53`  

Every hooked validation first performs `vn_getpath`, using a `PATH_MAX` stack buffer, then runs a naïve masked scan over the whole range. On Darwin 20-24 this is normally a page at a time across code-signed user objects. Work per callback is bounded, but total work is system-wide and scales with page validation activity.

**Remediation:** use cheap, reliable rejection gates before byte scanning; cache only immutable, lifetime-safe identity decisions if the callback context permits; do not log or allocate per validation; measure boot and application fault overhead.

#### M-5 — Success/failure observability is ambiguous

**Location:** `IntelMKLFixup.cpp:45-53`, `109-110`  

Path failure and no match are silent. A write-protection restore failure can still lead to a `Patched` message. The route failure message does not say which symbol was selected. There is no distinction among plugin loaded, candidate identified, signature found, dry run, bytes changed, duplicate, and functional success.

**Remediation:** add bounded lifecycle/status events and separate status codes. Validation callbacks must remain detection-only; no write-protection or post-write verification scheme can make the vnode-backed callback page an acceptable destination.

#### M-6 — Full user paths are logged outside explicit debug mode

**Location:** `IntelMKLFixup.cpp:52`  

The path can reveal the account name and application-support layout. It is emitted by `SYSLOG` whenever a match is changed.

**Remediation:** log a redacted image identity by default and full paths only in explicit verbose mode.

#### M-7 — Intentional panic path during plugin registration

**Location:** `IntelMKLFixup.cpp:104`; Lilu `kern_api.hpp:142-146`  

`onPatcherLoadForce` panics on callback-storage allocation failure. An application patcher should degrade to disabled rather than turn a resource failure into a boot panic.

**Remediation:** use the non-force registration API, log once, and leave the plugin inactive on failure.

#### M-8 — Write is not transactionally verified

**Location:** Lilu `kern_patcher.cpp:651-704` as called at `IntelMKLFixup.cpp:49-51`  

The helper changes bytes under Lilu's write lock but does not compare the target again under that lock, does not roll back a partial write, and does not verify the final bytes. The current equal-length arrays avoid replacement overrun, but transactional guarantees are absent.

**Remediation:** under the appropriate lock/context, recheck the entire expected original, write the exact replacement, verify it, and restore on failure where safe.

### Low

#### L-1 — Newer systems route only one of two still-used validators

On Darwin 20-24 both functions exist. `_cs_validate_page` handles the ordinary whole-page path; `_cs_validate_range` remains in sub-page and Mach-O header validation paths. This may miss an otherwise valid target. It is a compatibility limitation, not justification to hook both broadly.

#### L-2 — Fixed 1 KiB-class path stack allocation on a kernel path

`char path[PATH_MAX]` is simple and allocation-free, but consumes a material fraction of a kernel stack alongside XNU's VM-fault call chain. No stack overflow is demonstrated.

#### L-3 — Documentation omits operational controls and exact support

The README does not document the existing off/debug/beta arguments, supported Darwin range, Lilu minimum, removal/recovery, identity scope, or the difference between bypassing a vendor predicate and numerical correctness (`README.md:1-19`).

#### L-4 — No tests or static-analysis job

There are no tests for matching, truncation, end-of-buffer behavior, mutated signatures, multiple matches, or whitelist policy. CI only builds Debug and Release.

### Informational

- **I-1:** No direct out-of-bounds read was found in the audited search call. Lilu searches only through `dataSize - findSize`, inclusive.
- **I-2:** No direct out-of-bounds write was found for the current arrays. The equal-size template fixes search, replacement, and both masks at 23 bytes. This safety depends on retaining that overload or adding explicit validation.
- **I-3:** No integer truncation occurs in the plugin's current `vm_size_t` to `size_t` call on x86_64. Lilu guards the subtraction by checking `dataSize < findSize` first.
- **I-4:** A signature at the exact end of a range is handled correctly. A signature crossing a boundary is skipped.
- **I-5:** Sequential reapplication to the same already-patched bytes does not match because the replacement differs from the search pattern.
- **I-6 (superseded):** The original audit observed no explicit vnode write and incorrectly treated validation-page mutation as memory-only. Hardware testing proved that ordinary file reads returned the replacement bytes with unchanged inode and timestamps. XNU source confirms that the callback aliases the vnode-pager-backed VM page. Current source blocks all validation-page writes.
- **I-7:** If no supported bytes ever load, the system continues normally apart from hook overhead.
- **I-8:** The replacement returns integer true only from the matched function entry. It makes no claim about MKL numerical correctness or non-MKL Intel-only instructions.
- **I-9:** Logging occurs after Lilu releases its kernel-write lock. No per-call logging is enabled in current source; the commented line at `IntelMKLFixup.cpp:47` would be unsafe from a performance/privacy perspective if restored broadly.

## Security and reliability checklist

| Area | Result |
|---|---|
| Out-of-bounds reads | No current defect found under the XNU callback contract; null and contract validation are absent. |
| Out-of-bounds writes | Current 23-byte equal-size call is bounded; generic API use must remain size-checked. |
| Page/range size assumptions | Pre-Big Sur trusts explicit `size`; Big Sur+ assumes `PAGE_SIZE` exactly as XNU's page callback does. Private ABI makes this an ongoing verification requirement. |
| Integer overflow/truncation | No current x86_64 issue found in the call path. |
| Null pointers | Not checked locally; current XNU is relied upon. |
| Invalid/stale pointers | Vnode and page lifetime are relied upon from the synchronous private callback. No retained pointers exist. |
| Races/re-entrancy/thread safety | Lilu serialises writes but not searches. Concurrent duplicate discovery and non-transactional reapplication are possible. No mutable plugin state besides the startup trampoline exists. |
| Duplicate patching | Naturally skipped sequentially, not explicitly tracked; concurrent repeat is possible. |
| Boundary matches | End-of-range safe; cross-range match declined implicitly. |
| False positives | Critical exposure: 19 fixed bytes plus a four-byte wildcard, no identity/context restriction. |
| Surrounding instructions | Not checked. |
| Inappropriate memory | Any matching code-signed userspace validation buffer can be written; execute permission is not verified by the plugin. |
| Lilu routing failure | Logged, then fail open with no patch. Boot continues. |
| Kernel symbol failure | Logged by Lilu/plugin, no hook. |
| XNU behavior changes | Only a single Big Sur threshold exists; symbols are private and beta override can bypass the max version. |
| Lilu missing/incompatible | Missing dependency normally prevents load; declared minimum is wrong and must be fixed. |
| No MKL loaded | No patch; no intentional panic. |
| Boot-loop/panic risk | Real: wrong legacy ABI, broad kernel hook, forced registration panic, unresolved hosted boot-loop report. |
| Logging context | Match-only log is outside the write lock, but includes sensitive paths; route and Lilu errors also log. |
| Performance | Path reconstruction plus naïve scan for every routed validation page/range. Aggregate cost is not measured. |
| CPU/OS assumptions | x86_64-only build; no AMD check; nominal Darwin 17-24; only upstream Sequoia/7950X3D test claim; current host is Darwin 24.6/3900X per user, not yet tested. |

## Git history, branches, release, issues, and pull requests

### Local and hosted history

The repository has six commits, all dated 2024-10-25. The implementation, plist, and project have not changed since the initial commit. Later commits only changed README wording/credits and the workflow. The current implementation file is byte-identical to the one at the prerelease tag (SHA-256 `fcac9aa082a2dc499cb1f494d3611964ec8349649eea337823d88d9cac562c29`).

Hosted branches:

- `main` at `5181e8c`.
- `royalStaging` at the same commit; comparison is identical.

The user's fork `richardhedges/IntelMKLFixup` has only `main`, the same six commits, no releases, no issues, and no pull requests at audit time.

### Release

There is one prerelease, `1.0.0`, based on `5221294`. Its release notes explicitly say it is unfinished, that issues should be expected, and that it currently searches every loaded binary without a whitelist. It claims testing only on Sequoia with a Ryzen 9 7950X3D and says High Sierra-and-newer should work; the Darwin 17-19 ABI defect disproves that latter claim for the audited source.

### Known reports

- [Issue #1 — “Crash on macOS Ventura 13.7”](https://github.com/Carnations-Botanica/IntelMKLFixup/issues/1), open: a Ryzen 5 5600H user reported a kernel page fault after opening Discord and joining voice. The backtrace is in Apple audio components (`DspFuncLib`, `AppleHDA`, `IOAudioFamily`) with `coreaudiod` as the current process, not in IntelMKLFixup. The maintainer acknowledged Discord was not crash-free. **Causality is not established**, but the report remains unresolved.
- [Issue #3 — “Bootloop on AMD, MacOS 13”](https://github.com/Carnations-Botanica/IntelMKLFixup/issues/3), open: reports an endless boot loop after enabling the kext. No diagnostic log was supplied. A maintainer marked it as a repeat of #1. **Causality and root cause are not established**, and no source fix exists.
- [Issue #2 — Intel fast memset request](https://github.com/Carnations-Botanica/IntelMKLFixup/issues/2), open: requests unrelated TBB/`intel_fast_memset` patches for other applications. Nothing from that thread was merged. The current source still changes only the one vendor-gate pattern.
- [PR #4 — Tahoe constant](https://github.com/Carnations-Botanica/IntelMKLFixup/pull/4), open: changes only `KernelVersion::Sequoia` to `KernelVersion::Tahoe`. It does not address the crash reports, ABI defect, whitelist, matching safety, or callback changes.

No hosted data-corruption report was found. Absence of a report is not evidence that the unrestricted replacement is safe.

## Recommended remediation and phase gate

The smallest safe sequence is:

1. Correct the Darwin 17-19 wrapper return ABI or temporarily remove those OS versions from the supported range.
2. Correct the Lilu minimum and pin reviewed Lilu/MacKernelSDK revisions.
3. Add an AMD vendor gate before routing.
4. Add a fail-closed image/application targeting policy before any byte scan.
5. Convert the one pattern into a named catalogue entry with fixed lengths, exact version evidence, surrounding context, one-match policy, and already-patched form.
6. Keep validation callback bytes const and detection-only; design a separate
   process-private post-load patch path before considering any write.
7. Replace forced registration with graceful failure and distinguish dry-run/candidate/match/write/verification statuses.
8. Unit-test the pure policy/matching logic before producing a bootable artifact.

No current critical issue appears impossible to mitigate safely, so design work can continue after approval. Implementation must not begin from the release binary and must not install to EFI during development.

## Preliminary whitelist architecture for Phase 2

This is a proposal only; the actual identifiers and APIs must be verified in the callback context before implementation.

### Identifiers actually present now

The routed callbacks directly provide:

- A live `vnode_t` for the backing image.
- File offset/page offset.
- A synchronous valid byte range.
- On Darwin 17-19, the exact range size; on Darwin 20-24, page-sized data.

`vn_getpath(vp, ...)` is an available KPI and produces the loaded image path without entering the filesystem, but paths can fail, change, or be non-unique for hard-linked files. The callback does **not** directly provide a bundle identifier, team identifier, signing identity, Mach-O UUID, whole-file hash, process path, or safe user-client configuration channel.

The current fault path likely runs in the context of the process causing the fault, but that must be verified before trusting `current_proc`, `proc_name`, or any process-path API. A short process name alone is unacceptable.

### Proposed fail-closed rule

A patch should require all of the following:

1. Runtime platform is x86_64 AMD and the Darwin major is explicitly supported.
2. `vn_getpath` succeeds and the vnode image path structurally matches the built-in Discord native-module layout, with exact path-component boundaries:

   ```text
   /Users/<one user component>/Library/Application Support/discord/
     <one version component>/modules/discord_krisp-<restricted suffix>/discord_krisp.node
   ```

   The version is variable but only one component; the basename is exactly `discord_krisp.node`; `discord`, `modules`, and the `discord_krisp-` module-directory prefix are exact and case-sensitive. No `..`, alternate separators, partial basename, or substring-only match is accepted.
3. The current executable, if reliably obtainable in this callback, is an exact approved Discord main/helper executable path inside the installed `Discord.app`, not merely a process named `Discord`. If reliable process identity cannot be established, the first implementation should fail closed rather than silently downgrade to process-name matching.
4. Where a safe non-allocating API exists, require an approved Discord signing/team identity and/or Mach-O UUID. If these cannot be obtained safely here, leave them out and document the limitation; do not invent an API.
5. The page offset lies in a verified executable Mach-O region for the same vnode, if that can be established without unsafe parsing or cross-range reads.
6. Exactly one named, compiled-in MKL patch definition matches its full exact signature and required surrounding context. Unknown and near-match variants are rejected.
7. The replacement length equals the verified target length, the original bytes are rechecked under the write lock, and the expected match count is exactly one.

Image/vnode identity remains useful for detection, but no identity check makes
a shared validation page a safe write destination. Any future write must target
a verified private/COW mapping in the approved process after loading. Process
identity is mandatory for that separate architecture.

If any identifier is missing, ambiguous, stale, or contradictory, no scan or patch occurs. Future applications should be added as reviewed source records pairing narrowly defined identity rules with already-reviewed compiled patch definitions. A future remote whitelist may update identity rules only; it must not supply machine-code patterns or replacement bytes to the kernel.

## Decisions needed before Phase 2/3

1. **Initial OS scope:** for the first controlled Ryzen 9 3900X build, the safest choice is Darwin 24/macOS 15 only (the detected test environment), while retaining legacy code only after its ABI is fixed and separately tested. Confirm whether to narrow the first test build this way.
2. **Discord channel scope:** the proposed built-in rule is Discord Stable only. Confirm whether PTB/Canary should remain excluded initially.
3. **Known-good evidence:** Phase 2/3 will need the original, unpatched `discord_krisp.node` version/hash and the known-working Swift patch's exact search/replacement plus surrounding bytes for comparison. Confirm that these can be supplied or inspected locally before a patch definition is approved.
