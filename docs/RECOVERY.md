# Emergency recovery

Keep the known-good bootable USB EFI connected and confirmed bootable before
testing IntelMKLFixup. The recovery operation changes only the internal NVMe
EFI selected after inspection; it does not require changing the Discord
installation.

## Recovery procedure

1. Power on through the known-good USB EFI and select the normal macOS system
   volume. Do not boot through the possibly broken NVMe OpenCore configuration.

2. Identify the internal NVMe EFI partition. Do not assume it is `disk0s1`:

   ```sh
   /usr/sbin/diskutil list
   ```

   Confirm the physical disk, capacity, and `EFI` partition, then mount that
   exact identifier, replacing `diskNs1` below only after verification:

   ```sh
   sudo /usr/sbin/diskutil mount diskNs1
   ```

   Note the actual mount point printed by `diskutil`; it may be `/Volumes/EFI`
   or a numbered variant such as `/Volumes/EFI 1`.

3. Disable the plugin recoverably. Using the verified mount point, move only
   `IntelMKLFixup.kext` out of `EFI/OC/Kexts` rather than deleting it. For a
   mount point of `/Volumes/EFI`, the command is:

   ```sh
   sudo /bin/mv "/Volumes/EFI/EFI/OC/Kexts/IntelMKLFixup.kext" \
     "/Volumes/EFI/IntelMKLFixup.kext.disabled"
   ```

   If the file is not at that exact path, stop and inspect the mounted EFI. Do
   not use wildcards or recursive removal commands.

4. Restore the known-good `config.plist` backup made before installation. The
   following example assumes it was deliberately named
   `config.plist.pre-imklfx`; use the actual verified backup filename:

   ```sh
   sudo /bin/cp "/Volumes/EFI/EFI/OC/config.plist.pre-imklfx" \
     "/Volumes/EFI/EFI/OC/config.plist"
   /usr/bin/plutil -lint "/Volumes/EFI/EFI/OC/config.plist"
   ```

   If `ocvalidate` matching the installed OpenCore release is available, run it
   against the restored file before rebooting. Do not substitute an
   `ocvalidate` binary from a different OpenCore release.

5. Unmount the internal EFI cleanly and reboot through its OpenCore entry:

   ```sh
   /usr/sbin/diskutil unmount "/Volumes/EFI"
   sudo /sbin/reboot
   ```

   Use the actual mount point in the unmount command. If the internal EFI does
   not boot normally, return to the USB EFI and recheck the selected partition,
   restored configuration, kext ordering, and backup provenance.

## Non-emergency disable

For a controlled negative test, append `-imklfxoff` to the existing OpenCore
`boot-args` and reboot. This is preferable to editing kext files for routine
testing. It is not a substitute for the USB recovery path if OpenCore cannot
reach macOS.

