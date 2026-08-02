# Installation

There is no installer. This release is for experienced Hackintosh users who
already maintain a recoverable OpenCore setup.

Before use, read [docs/FILE_BACKED_PATCHING.md](docs/FILE_BACKED_PATCHING.md),
make current backups, and prepare a known-working recovery EFI. Verify the
download against `SHA256SUMS.txt`. Lilu must load before IntelMKLFixup.

Default operation is detection-only. Add `-imklfxfilepatch` only after accepting
the file-visible and signature risks. `-imklfxdryrun` always prevents a write;
`-imklfxoff` disables the plugin. Begin with verbose detection using
`-imklfxdryrun -imklfxdbg` and inspect logs before considering active mode.

The project does not modify an EFI, edit `config.plist`, install a kext, or
alter Discord automatically. Those system administration choices remain with
the user.
