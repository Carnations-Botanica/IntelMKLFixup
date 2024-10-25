# !!WARNING!!
Me (Kaitlyn), or Carnations Botanica is not responsible for any data loss incured by using this kernel extension. Although it is ***highly*** unlikely, you have been warned.

## IntelMKLFixup
Dead-simple Intel(tm) MKL (Math Kernel Library) fixup for macOS.

## Why?
Hackintoshes with AMD CPUs have infamously had a problem with software compiled against Intel's MKL, often resulting in many popular applications just not running correctly or at all.

This is where IntelMKLFixup comes in, IntelMKLFixup will *invisibly* patch bits of Intel's MKL in memory to help provide compatibility for AMD CPUs, without any user interaction or tweaking. Thus, allowing applications that once ran incorrectly or didn't work at all, to now run with little to no issues.

## Requirements
- [Lilu](https://github.com/acidanthera/Lilu/releases)

## Credits & Thanks
- [vit9696](https://github.com/vit9696) (and contributors) for [RestrictEvents](https://github.com/acidanthera/RestrictEvents), which served as the basis for this project.
- [NyaomiDEV](https://github.com/NyaomiDEV) for [AMDFriend](https://github.com/NyaomiDEV/AMDFriend), which served as inspiration for this project.
- And to anybody who gave me words of encouragement or helped me figure out kernel extension development, thank you.