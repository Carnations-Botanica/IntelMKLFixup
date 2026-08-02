# IntelMKLFixup

IntelMKLFixup is an experimental Lilu plugin for x86_64 AMD Hackintosh
systems. It identifies explicitly approved applications containing reviewed
Intel MKL CPU-vendor predicates and applies a compatibility patch while macOS
validates the executable page.

On tested Darwin 24 systems, the validation page is vnode/UBC-backed. The
changed bytes therefore become visible through the application binary even
though the plugin does not perform a conventional filesystem write. This is
not a process-private or guaranteed transient in-memory patcher.

Version 0.2.0 supports one strict Discord Stable/Krisp image variant on Darwin
24. It requires an AMD CPU, the exact path grammar, basename, signing
identifier, Team ID, CDHash, page offset, MKL implementation bytes, and
surrounding context compiled into the kext. Unknown binaries fail closed.

## Explicit operating modes

Active writing is opt-in:

| Boot arguments | Behaviour |
| --- | --- |
| `-imklfxoff` | Disable the plugin. |
| none | Detect and log approved matches; never write. |
| `-imklfxdryrun` | Detection only; never write. |
| `-imklfxfilepatch -imklfxdryrun` | Acknowledged detection-only test. |
| `-imklfxfilepatch` | Permit only the reviewed `StrictVariant` file-backed page patch. |
| `-imklfxdbg` | Add verbose diagnostic logging. |

`BoundedWindow` remains detection/dry-run research code behind
`-imklfxwindow`. It can never write and is never an active fallback from a
strict rule. No image-wide scanning exists.

## Important risks

By enabling `-imklfxfilepatch`, users explicitly accept:

- file-visible binary modification and possible code-signature invalidation;
- undocumented VM, UBC, and kernel behaviour;
- crashes, application failure, or boot instability after updates;
- the need for current backups and a known-working recovery EFI.

Physical persistence to storage is neither guaranteed nor relied upon. If an
application binary is observed in a modified state, restore it from a known
good backup or the application vendor.

Read [INSTALLATION.md](INSTALLATION.md), [RECOVERY.md](RECOVERY.md), and
[docs/FILE_BACKED_PATCHING.md](docs/FILE_BACKED_PATCHING.md) before use.
Build and test instructions are in [BUILDING.md](BUILDING.md) and
[docs/TESTING.md](docs/TESTING.md).

## Project scope

This is a personal Hackintosh compatibility experiment, not a general-purpose
binary patcher and not a security boundary. It has no installer, does not edit
OpenCore configuration, and performs no kernel networking. Remote whitelist
metadata can select only identifiers compiled into a reviewed build; it cannot
supply machine-code search or replacement bytes.

The original project is
[Carnations-Botanica/IntelMKLFixup](https://github.com/Carnations-Botanica/IntelMKLFixup).
This fork preserves upstream credit while documenting the measured file-visible
behaviour honestly.
