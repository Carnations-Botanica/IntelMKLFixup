# IntelMKLFixup 0.2.0 release candidate

Experimental Lilu plugin for x86_64 AMD Hackintosh systems on Darwin 24.

This is an experimental kernel-assisted file-backed executable-page
compatibility patcher. It is not a process-private or guaranteed transient
in-memory patcher. On the tested system, changed validation-page bytes became
visible through ordinary reads of the application binary even though no normal
filesystem write occurred and timestamps could remain unchanged.

Active writing requires explicit `-imklfxfilepatch` acknowledgement and is
limited to the compiled strict Discord Stable/Krisp variant. With no
acknowledgement, or with `-imklfxdryrun`, the plugin detects and logs only.
`-imklfxoff` disables it.

Enabling active mode accepts file-visible binary modification, application
code-signature invalidation, undocumented VM/kernel behaviour, crashes after
updates, and the need for backups and a recovery EFI. Read `INSTALLATION.md`
and `RECOVERY.md` before use. No installer or automatic configuration editor is
included.

Project: https://github.com/richardhedges/IntelMKLFixup
