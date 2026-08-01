# !!WARNING!!
Me (Kaitlyn), or Carnations Botanica is not responsible for any data loss incurred by using this kernel extension. Although it is ***highly*** unlikely, you have been warned.

## IntelMKLFixup
Dead-simple Intel(tm) MKL (Math Kernel Library) patcher for macOS, with a twist.

## Why?
Hackintoshes with AMD CPUs have infamously had a problem with software compiled against Intel's MKL, often resulting in many popular applications just not running correctly or at all.

This is where IntelMKLFixup comes in, IntelMKLFixup will *invisibly* patch bits of Intel's MKL in memory to help provide compatibility for AMD CPUs, without any user interaction or tweaking. Thus, allowing applications that once ran incorrectly or didn't work at all, to now run with little to no issues.

## Requirements
- [Lilu](https://github.com/acidanthera/Lilu/releases)

## Testing controls

The current development source includes fail-closed dry-run and diagnostic
controls. It is not ready for installation until the remaining build and test
phases are complete. See [diagnostics and controls](docs/DEBUGGING.md) for boot
arguments and exact log commands, and [emergency recovery](docs/RECOVERY.md)
before attempting any controlled test.

Whitelist release assets are handled only by the signed userspace mechanism
described in [whitelist updates](docs/WHITELIST_UPDATES.md). The kernel extension
does not contact GitHub or parse an external manifest.

## Credits & Thanks
- [vit9696](https://github.com/vit9696) (and contributors) for [RestrictEvents](https://github.com/acidanthera/RestrictEvents), which served as the basis for this project.
- [Tomnic](https://macos86.it/profile/69-tomnic/) for [the original patching guide](https://macos86.it/topic/5489-tutorial-for-patching-binaries-for-amd-hackintosh-compatibility/), which helped point me in the right direction.
- [NyaomiDEV](https://github.com/NyaomiDEV) for [AMDFriend](https://github.com/NyaomiDEV/AMDFriend), which served as inspiration for this project.
- And to anybody who gave me words of encouragement or helped me figure out kernel extension development, thank you.
