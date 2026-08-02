# IntelMKLFixup 0.2.0-rc1

Experimental prerelease for x86_64 AMD Hackintosh systems on Darwin 24.

This release restores the verified strict Discord Stable/Krisp compatibility
patch while describing its behaviour accurately: `_cs_validate_page` receives
a vnode/UBC-backed executable page on the tested system, so changed bytes can
become visible through ordinary reads of the application binary. This is not a
process-private or guaranteed transient in-memory patcher.

## Safety model

- Default operation detects and logs only.
- `-imklfxfilepatch` is required for any active write.
- `-imklfxdryrun` always prevents modification.
- Active writes support only the compiled strict image variant.
- `BoundedWindow` is detection/dry-run research only and cannot write.
- Unknown paths, identities, CDHashes, offsets, implementations, or context
  fail closed.

The expected active log includes
`mode=file-backed-patch acknowledged=yes file-visible=yes experimental=yes`.
A successful patch reports
`outcome=file-backed-patched modified=yes file-visible=yes`.

## User-visible changes

- Bundle identifier is now `com.richardhedges.IntelMKLFixup`.
- Version is `0.2.0`.
- The Swift inspector distinguishes no Mach-O, symbol absent, recognised
  original, this project's patch, recognised upstream patch, and unknown
  implementation states.
- A recognised upstream replacement is labelled explicitly and is not
  automatically restored without an exact verified original.
- Documentation now records file-visible modification, signature invalidation,
  recovery requirements, and the unresolved process-private research path.

## Risk acknowledgement

Active mode can make application bytes file-visible, invalidate the application
signature, and cause application or system instability after updates. Users
need verified backups and a known-working recovery EFI. Physical persistence to
storage is not guaranteed or relied upon.

Host policy, Swift, sanitizer, lint, static-analysis, and clean build checks
passed for this candidate. Those checks do not prove kernel runtime safety.
