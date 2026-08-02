# File-backed validation-page patching

In accessible terms, macOS asks the code-signing subsystem to validate a page
of executable bytes before use. On tested Darwin 24 systems,
`_cs_validate_page` receives a page associated with the file's vnode and
Unified Buffer Cache (UBC), not a guaranteed private copy belonging to one
process.

IntelMKLFixup writes the reviewed replacement into that validation page only
after an exact strict match and explicit `-imklfxfilepatch` acknowledgement.
Because the page is vnode/UBC-backed, ordinary reads of the application binary
may observe the changed bytes. The plugin does not call a normal filesystem
write API, so `mtime` and `ctime` may remain unchanged.

Whether a dirty page is later written to physical storage depends on
undocumented kernel and filesystem behaviour. IntelMKLFixup neither guarantees
nor relies on physical persistence. The supported safety statement is simply:
the change is file-visible and can invalidate the application's code signature.

This behaviour is deliberately accepted for this experimental personal
Hackintosh tool. It is not a process-private or guaranteed transient in-memory
patcher. Users enabling active mode accept file-visible modification,
code-signature invalidation, undocumented VM/kernel behaviour, possible crashes
after updates, and responsibility for backups and recovery media.
