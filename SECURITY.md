# Security

IntelMKLFixup is experimental kernel software for a personal Hackintosh. It is
not a security product and its host-side tests do not prove kernel runtime
safety.

The plugin deliberately refuses unknown applications and image variants.
Active writing requires `-imklfxfilepatch` and is limited to a compiled strict
rule. The kernel component contains no networking. Authenticated userspace
metadata cannot provide executable search, mask, or replacement bytes.

Do not publish application binaries, signing secrets, EFI contents, crash dumps
containing personal data, or proprietary fixtures in an issue. Reports should
include the project commit, macOS/Darwin version, CPU, boot arguments, relevant
redacted logs, and whether file-visible bytes were observed.

Assume that enabling active mode can invalidate application signatures, crash
the application or system, and leave the application binary observable in a
modified state. Maintain offline recovery and known-good backups.
