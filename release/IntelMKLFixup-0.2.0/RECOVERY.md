# Recovery

Keep a separate known-working EFI that does not load IntelMKLFixup. If the
system still reaches OpenCore, `-imklfxoff` disables the plugin. If boot or the
application becomes unstable, return to the recovery configuration before
investigating.

If ordinary reads show modified application bytes, quit the application and
restore the binary from a verified backup or reinstall it from the vendor. Do
not ask the Swift tool to restore the recognised upstream pattern unless an
exact original for that image variant is independently known; automatic
upstream restoration is intentionally disabled.

After recovery, confirm that the kext is not loaded, remove the active
acknowledgement from the configuration you control, and verify the application
signature and hash as appropriate. File timestamps alone are not reliable
evidence because the validation-page path is not a conventional filesystem
write.
