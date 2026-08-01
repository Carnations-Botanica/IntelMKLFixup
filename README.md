# !!WARNING!!
Me (Kaitlyn), or Carnations Botanica is not responsible for any data loss incurred by using this kernel extension. Although it is ***highly*** unlikely, you have been warned.

## IntelMKLFixup
Dead-simple Intel(tm) MKL (Math Kernel Library) patcher for macOS, with a twist.

The patch engine is application-independent: reviewed MKL vendor-gate
implementations are eligible only when a separate built-in application and
image-variant policy approves the native module. Discord Stable/Krisp is the
first controlled-test fixture, not the product boundary.

## Why?
Hackintoshes with AMD CPUs have infamously had a problem with software compiled against Intel's MKL, often resulting in many popular applications just not running correctly or at all.

This is where IntelMKLFixup comes in, IntelMKLFixup will *invisibly* patch bits of Intel's MKL in memory to help provide compatibility for AMD CPUs, without any user interaction or tweaking. Thus, allowing applications that once ran incorrectly or didn't work at all, to now run with little to no issues.

## Requirements

- an x86_64 AMD Hackintosh;
- macOS 15 / Darwin 24 (the only runtime enabled by this release candidate);
- [Lilu](https://github.com/acidanthera/Lilu/releases), loaded before
  IntelMKLFixup; and
- a known-good recovery EFI that has been boot-tested before installation.

## Testing controls

This release candidate has passed clean builds, host tests, sanitizers, static
analysis, and artifact inspection. It is ready only for the staged manual
StrictVariant dry run in [TEST_PLAN_3900X.md](TEST_PLAN_3900X.md). It has not
completed live kernel or functional Krisp testing and is not a general release.
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
