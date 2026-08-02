# CRITICAL SAFETY NOTICE

**Do not use the 1.0.0-rc1 kext in active mode.** Controlled Ryzen 9 3900X
testing proved that its write through `_cs_validate_page` changes the
vnode-backed `discord_krisp.node` page and becomes visible through ordinary
file reads without advancing mtime or ctime. The candidate is revoked for
active use. See [FILE_BACKED_WRITE_INCIDENT.md](FILE_BACKED_WRITE_INCIDENT.md).

Current source is detection-only: dry-run can report an eligible exact match,
while non-dry-run operation fails closed with
`outcome=unsafe-file-backed-write-blocked modified=no`. No safe runtime
replacement has been implemented.

Me (Kaitlyn), or Carnations Botanica is not responsible for data loss incurred
by using this experimental kernel extension.

## IntelMKLFixup
Dead-simple Intel(tm) MKL (Math Kernel Library) patcher for macOS, with a twist.

The patch engine is application-independent: reviewed MKL vendor-gate
implementations are eligible only when a separate built-in application and
image-variant policy approves the native module. Discord Stable/Krisp is the
first controlled-test fixture, not the product boundary.

## Why?
Hackintoshes with AMD CPUs have infamously had a problem with software compiled against Intel's MKL, often resulting in many popular applications just not running correctly or at all.

The project is investigating whether a strictly process-private runtime patch
can bypass this vendor gate safely. The former validation-page write is not an
in-memory-only mechanism and has been disabled. Until a replacement
architecture is reviewed and implemented, this repository provides only
bounded detection, policy, catalogue, and update tooling.

## Requirements

- an x86_64 AMD Hackintosh;
- macOS 15 / Darwin 24 (the only runtime enabled by this release candidate);
- [Lilu](https://github.com/acidanthera/Lilu/releases), loaded before
  IntelMKLFixup; and
- a known-good recovery EFI that has been boot-tested before installation.

## Testing controls

The former release candidate passed clean builds, host tests, sanitizers,
static analysis, and artifact inspection, but hardware testing invalidated its
core memory-only assumption. Active testing is prohibited. The revised
[TEST_PLAN_3900X.md](TEST_PLAN_3900X.md) permits recovery verification and
detection-only dry-run evidence; it is not a runtime-patch plan.
See [diagnostics and controls](docs/DEBUGGING.md) for boot arguments and exact
log commands, and [emergency recovery](docs/RECOVERY.md) before attempting the
controlled test.

Two runtime policy modes are compiled: StrictVariant requires the reviewed
binary identity and exact file offset; experimental BoundedWindow requires
`-imklfxwindow` and searches exactly one approved callback-complete validation
window no larger than one x86_64 page. It proves uniqueness only within that
window. Future image-wide tolerance requires the separately designed
userspace-assisted ImageScan architecture; it is not implemented.

The reviewed patch bypasses one exact Intel MKL CPU-vendor gate by replacing
its supported implementation with `mov eax, 1; ret`. It does not replace
numerical MKL routines, prove that all MKL operations are correct on AMD, or
make arbitrary Intel-only software compatible. Unknown applications and MKL
implementations are left untouched.

Whitelist release assets are handled only by the signed userspace mechanism
described in [whitelist updates](docs/WHITELIST_UPDATES.md). The kernel extension
does not contact GitHub or parse an external manifest. Installing a userspace
manifest does not change runtime behaviour in the current release.

## Credits & Thanks
- [vit9696](https://github.com/vit9696) (and contributors) for [RestrictEvents](https://github.com/acidanthera/RestrictEvents), which served as the basis for this project.
- [Tomnic](https://macos86.it/profile/69-tomnic/) for [the original patching guide](https://macos86.it/topic/5489-tutorial-for-patching-binaries-for-amd-hackintosh-compatibility/), which helped point me in the right direction.
- [NyaomiDEV](https://github.com/NyaomiDEV) for [AMDFriend](https://github.com/NyaomiDEV/AMDFriend), which served as inspiration for this project.
- And to anybody who gave me words of encouragement or helped me figure out kernel extension development, thank you.
