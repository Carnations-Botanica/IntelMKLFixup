# Ryzen 9 3900X manual validation plan

This plan is for the owner of the test system to execute manually. Phase 6 did
not install a kext, mount an EFI, edit OpenCore, change boot arguments, invoke
the Swift patcher, terminate Discord, or reboot. Do not skip a stop condition.

The artifact reviewed in Phase 6 is:

```text
/Users/richardhedges/Desktop/IntelMKLFixup/build/Phase6/Release/IntelMKLFixup.kext
```

Its executable SHA-256 is:

```text
9b68af38ec220c033e1913d8e841a26525aedb72699e578e97b94e09c19359d5
```

The test is deliberately staged. These are different claims:

1. the plugin loaded;
2. the application was approved;
3. a compiled `PatchDefinition` matched;
4. memory was modified successfully; and
5. Discord voice and Krisp behavior were functionally verified.

Launching Discord proves none of the later claims by itself.

## Common log and recovery commands

After each boot, save the relevant unified log before changing configuration:

```sh
/usr/bin/log show --last boot --style compact \
  --predicate 'eventMessage CONTAINS[c] "imklfx"'
```

For a narrower result:

```sh
/usr/bin/log show --last boot --style compact \
  --predicate 'eventMessage CONTAINS[c] "imklfx" AND (eventMessage CONTAINS[c] "lifecycle" OR eventMessage CONTAINS[c] "candidate" OR eventMessage CONTAINS[c] "image=" OR eventMessage CONTAINS[c] "patch=")'
```

Confirm that the kext is present in the loaded-kext list:

```sh
/usr/bin/kmutil showloaded --list-only | /usr/bin/grep -i 'IntelMKLFixup\|com.github.whatdahopper.IntelMKLFixup'
```

The unique log prefix is `imklfx`. Full candidate paths appear only with
`-imklfxdbg`; home-directory paths are redacted to start with `~`.

### Recovery action R

Use this after any abnormal boot, panic, watchdog, routing failure, or test
stop condition:

1. Boot with the known-good USB EFI and select the normal macOS volume. Do not
   boot through the NVMe OpenCore configuration being tested.
2. Identify the internal NVMe EFI; never assume its disk identifier:

   ```sh
   /usr/sbin/diskutil list
   ```

3. Mount only the verified NVMe EFI partition, substituting its exact ID:

   ```sh
   sudo /usr/sbin/diskutil mount diskNs1
   ```

4. Move the plugin out of the active kext directory and restore the exact
   pre-test configuration backup. Substitute the actual EFI mount point if it
   is not `/Volumes/EFI`:

   ```sh
   sudo /bin/mv "/Volumes/EFI/EFI/OC/Kexts/IntelMKLFixup.kext" \
     "/Volumes/EFI/IntelMKLFixup.kext.disabled"
   sudo /bin/cp "/Volumes/EFI/EFI/OC/config.plist.pre-imklfx-phase6" \
     "/Volumes/EFI/EFI/OC/config.plist"
   /usr/bin/plutil -lint "/Volumes/EFI/EFI/OC/config.plist"
   /path/to/the-matching-OpenCore-version/ocvalidate \
     "/Volumes/EFI/EFI/OC/config.plist"
   ```

5. Unmount, reboot manually, and use the USB again if the restored NVMe EFI
   does not boot:

   ```sh
   /usr/sbin/diskutil unmount "/Volumes/EFI"
   sudo /sbin/reboot
   ```

Do not use wildcards, delete the recovery USB copy, or use an `ocvalidate`
binary from a different OpenCore release.

## Stage A — Baseline and recovery

### Prerequisites

- The current NVMe EFI and known-good USB EFI are unchanged.
- Discord may remain patched on disk by the separate Swift application.
- IntelMKLFixup is not yet present in the active EFI.

### Commands and observations

Record the operating system, Darwin kernel, CPU, SMBIOS, OpenCore, and Lilu:

```sh
/usr/bin/sw_vers
/usr/bin/uname -a
/usr/sbin/sysctl -n machdep.cpu.brand_string
/usr/sbin/system_profiler SPHardwareDataType
/usr/sbin/nvram -p | /usr/bin/grep -i 'opencore-version'
/usr/bin/kmutil showloaded --list-only | /usr/bin/grep -i 'as.vit9696.Lilu'
```

Boot the NVMe EFI once and the USB EFI once, recording which OpenCore picker
entry was used each time. Return to the normal NVMe boot only after confirming
that the USB remains independently bootable.

Record Discord's version and enumerate candidate modules:

```sh
/usr/bin/defaults read /Applications/Discord.app/Contents/Info CFBundleShortVersionString
/usr/bin/find "$HOME/Library/Application Support/discord" \
  -type f -name discord_krisp.node -print
/usr/sbin/lsof -nP | /usr/bin/grep '/discord_krisp.node'
```

Use the `lsof` result while Discord is running to identify the active module,
then set the exact path manually:

```sh
KRISP_MODULE='/Users/your-account/Library/Application Support/discord/app-X.Y.Z/modules/discord_krisp-N/discord_krisp.node'
/usr/bin/shasum -a 256 "$KRISP_MODULE"
/usr/bin/codesign -d --verbose=4 "$KRISP_MODULE" 2>&1
/usr/bin/codesign --verify --strict --verbose=4 "$KRISP_MODULE"
```

Record the current patched-file SHA-256 as `DISCORD_PATCHED_SHA256`. Because
the file is already patched, code-signature verification or the strict CDHash
may fail; record that result. Do not treat this expected consequence as proof
that the original Discord module has the wrong publisher.

When a later stage explicitly requires the original, use the Swift patcher UI
to select this exact active module and choose its restore/unpatch operation.
Do not restore a stale version directory. Immediately record:

```sh
/usr/bin/shasum -a 256 "$KRISP_MODULE"
/usr/bin/codesign --verify --strict --verbose=4 "$KRISP_MODULE"
/usr/bin/codesign -d --verbose=4 "$KRISP_MODULE" 2>&1
```

Record that SHA-256 as `DISCORD_ORIGINAL_SHA256`. Confirm the Swift application
can restore the original and, separately, can return to the known patched hash
before installing the kext. Stage C must start from the restored original.

The compiled Discord path requires a regular file named `discord_krisp.node`
under the bounded Stable grammar:

```text
~/Library/Application Support/discord/app-<numeric.version>/modules/
  discord_krisp-<numeric.version>/discord_krisp.node
```

The nested `discord_krisp/discord_krisp.node` form is also compiled. The
unmodified module must report signing identifier `discord_krisp`, Team ID
`53Q6R32WPB`, a valid hardened-runtime signature, and no ad-hoc signature.

### Expected result and success criteria

- Both EFI copies boot independently.
- The CPU record identifies the Ryzen 9 3900X.
- macOS and Darwin are recorded rather than assumed.
- Exactly one active Krisp module is identified.
- The path grammar, basename, signing identifier, and Team ID agree with the
  compiled rule after the original is restored.
- Both patched and restored-original hashes are recorded.

### Failure criteria and stop condition

Stop if the USB is not independently bootable, the active module is ambiguous,
the original cannot be restored byte-for-byte, or the restored original does
not meet the compiled path/signing identity. A known signature failure while
the file remains patched is not a waiver: restore and re-check before Stage B
can be completed.

### Recovery

No EFI change has occurred. If a baseline boot fails, use Recovery action R
without copying IntelMKLFixup to the NVMe EFI.

## Stage B — StrictVariant dry run

### Prerequisites

- Stage A passed and the recovery USB remains unchanged.
- The matching OpenCore release's `ocvalidate` is available.
- The Release kext executable hash matches the value at the top of this file.
- Keep a second copy of the current NVMe `config.plist` outside the EFI.

### Installation and configuration commands

Identify and mount the NVMe EFI manually. Replace `diskNs1` only after checking
the disk model and size:

```sh
/usr/sbin/diskutil list
sudo /usr/sbin/diskutil mount diskNs1
/bin/cp "/Volumes/EFI/EFI/OC/config.plist" \
  "/Volumes/EFI/EFI/OC/config.plist.pre-imklfx-phase6"
sudo /usr/bin/ditto \
  "/Users/richardhedges/Desktop/IntelMKLFixup/build/Phase6/Release/IntelMKLFixup.kext" \
  "/Volumes/EFI/EFI/OC/Kexts/IntelMKLFixup.kext"
/usr/bin/shasum -a 256 \
  "/Volumes/EFI/EFI/OC/Kexts/IntelMKLFixup.kext/Contents/MacOS/IntelMKLFixup"
```

In a plist-aware OpenCore editor, add this `Kernel -> Add` entry immediately
after Lilu (never before it):

```text
BundlePath: IntelMKLFixup.kext
Comment: IntelMKLFixup Phase 6 strict dry run
Enabled: true
ExecutablePath: Contents/MacOS/IntelMKLFixup
MinKernel: 24.0.0
MaxKernel: 24.99.99
PlistPath: Contents/Info.plist
```

Append, without replacing existing arguments:

```text
-imklfxdryrun -imklfxdbg
```

Ensure `-imklfxwindow` and `-imklfxoff` are absent. Validate, unmount, and
reboot manually from the NVMe EFI:

```sh
/usr/bin/plutil -lint "/Volumes/EFI/EFI/OC/config.plist"
/path/to/the-matching-OpenCore-version/ocvalidate \
  "/Volumes/EFI/EFI/OC/config.plist"
/usr/sbin/diskutil unmount "/Volumes/EFI"
sudo /sbin/reboot
```

### Dry-run sequence

Discord may remain patched for the first boot-and-routing smoke check. In that
case strict approval is not expected: an altered file normally fails the valid
runtime signature or exact CDHash gate. To complete Stage B successfully, use
the Swift application manually to restore the active original, verify
`DISCORD_ORIGINAL_SHA256`, fully quit Discord and all helpers, and relaunch it.
Do not claim a strict dry-run match while the on-disk patch still invalidates
strict evidence.

### Expected logs

```text
imklfx ... lifecycle=loaded mode=dry-run ... bounded-window=0 image-scan=reserved
imklfx ... lifecycle=route-installed darwin=24 cpu=amd ... bounded-window=disabled
imklfx ... candidate-app-approved application=discord-stable-krisp image=discord-stable-0.0.403-krisp-x86_64-585e9575 mode=strict-variant
imklfx ... image=discord-stable-0.0.403-krisp-x86_64-585e9575 patch=mkl-serv-intel-cpu-true-oneapi-build-20201104-x86_64-v1 signature=supported outcome=dry-run modified=no offset=0x650100
```

The exact strict image identifier is evidence that the CDHash fixture matched.
The final line must say `modified=no`.

### Success criteria

- The plugin and Lilu are loaded.
- Darwin/AMD gating passed.
- all five private code-signing helpers resolved;
- `_cs_validate_page` routing succeeded;
- the strict Discord image variant was approved;
- the named compiled MKL definition matched at `0x650100`;
- dry-run reported `modified=no`; and
- unrelated applications produced no patch attempt.

### Failure criteria and stop condition

Stop on any of these fragments or observations:

```text
lifecycle=registration-failed
lifecycle=route-rejected reason=missing-symbol
lifecycle=route-rejected reason=cs-validate-page-routing
lifecycle=route-rejected reason=unsupported-darwin
lifecycle=route-rejected reason=non-amd-cpu
```

Also stop if strict approval is absent after the verified original is loaded,
an unrelated app reaches `outcome=dry-run` or `outcome=patched`, logging is
excessive, or any panic/watchdog/abnormal boot occurs.

### Recovery

Use Recovery action R. Restore `config.plist.pre-imklfx-phase6`, move the kext
out of `EFI/OC/Kexts`, and keep the USB EFI unchanged.

## Stage C — StrictVariant active runtime patch

### Prerequisites

- Stage B passed with the exact strict fixture and `modified=no`.
- `DISCORD_ORIGINAL_SHA256` is recorded.
- No BoundedWindow testing is enabled.

### Commands and procedure

Mount the verified NVMe EFI and remove only `-imklfxdryrun`. Retain
`-imklfxdbg`; ensure `-imklfxwindow` and `-imklfxoff` remain absent. Validate
with `plutil` and the matching `ocvalidate`, unmount, and reboot manually as in
Stage B.

After login, before launching Discord, use the Swift application manually to
restore the exact active module. Verify:

```sh
/usr/bin/shasum -a 256 "$KRISP_MODULE"
/usr/bin/codesign --verify --strict --verbose=4 "$KRISP_MODULE"
```

The hash must equal `DISCORD_ORIGINAL_SHA256`. Quit Discord normally and use
Activity Monitor to confirm Discord and its helpers are gone. This read-only
check should return no Discord process before relaunch:

```sh
/usr/bin/pgrep -ifl 'Discord|discord'
```

Launch Discord normally.

### Expected logs

```text
candidate-app-approved application=discord-stable-krisp image=discord-stable-0.0.403-krisp-x86_64-585e9575 mode=strict-variant
image=discord-stable-0.0.403-krisp-x86_64-585e9575 patch=mkl-serv-intel-cpu-true-oneapi-build-20201104-x86_64-v1 signature=supported outcome=patched modified=yes offset=0x650100
```

### Success criteria

- The strict variant and named MKL definition are approved.
- The log reports `outcome=patched`, `modified=yes`, and `offset=0x650100`.
- Discord opens.
- Voice input and voice output each work in a controlled call/test.
- Enabling Krisp/noise suppression changes behavior without a crash.
- Switching input and output devices works.
- The on-disk hash remains `DISCORD_ORIGINAL_SHA256` before and after use:

  ```sh
  /usr/bin/shasum -a 256 "$KRISP_MODULE"
  ```

Discord opening is only a launch observation; Stage C passes only after the
voice, Krisp, device-change, log, and unchanged-file checks also pass.

### Failure criteria and stop condition

Stop on signature/context rejection, concurrent-change, write-protection,
replacement-verification, a changed on-disk hash, Discord/Krisp malfunction,
panic, watchdog, or unrelated patch outcome.

### Recovery

Quit Discord if macOS is responsive, then use Recovery action R. Do not use the
Swift application as EFI recovery.

## Stage D — StrictVariant repeatability

### Prerequisites

- Stage C passed completely.
- The module on disk still hashes to `DISCORD_ORIGINAL_SHA256`.

### Procedure and commands

Quit Discord and all helpers, verify with `pgrep`, relaunch, and repeat at least
five times. Save the `imklfx` log after every cycle. An `already-patched` result
is acceptable only for a duplicate validation of the same already-modified
in-memory load; each fresh process/load must still become usable without an
on-disk change.

Reboot once more manually, relaunch Discord, and repeat the Stage C functional
checks. Put the system to sleep, wake it, and repeat input/output/Krisp/device
changes. Open normal audio and several unrelated applications.

### Expected logs

```text
outcome=patched modified=yes
```

or, for repeated validation of the same memory load:

```text
outcome=already-patched
```

There must be no ambiguous, concurrent-change, verification, or unrelated-app
patch outcome.

### Success criteria

Five clean launch cycles, one clean reboot, stable sleep/wake, normal unrelated
audio/apps, bounded log volume, and an unchanged on-disk SHA-256.

### Failure criteria and stop condition

Stop for a panic, watchdog, hang, audio regression, growing log storm, changed
file hash, or a fresh process that does not receive the expected runtime patch.

### Recovery

Use Recovery action R and retain the captured logs for the report.

## Stage E — BoundedWindow dry run

### Prerequisites

- Stages B through D passed.
- Understand that strict policy wins when both strict and BoundedWindow rules
  independently approve the callback.
- Do not edit, resign, mutate, or copy a production Discord binary merely to
  defeat the strict CDHash.

### Safe selector validation

The host fixture is always safe and proves selector policy without loading a
kext:

```sh
cd /Users/richardhedges/Desktop/IntelMKLFixup
xcrun clang++ -std=c++14 -Wall -Wextra -Werror -pedantic \
  Tests/PolicyTests/CatalogueTests.cpp -o /tmp/imklfx-catalogue-tests
/tmp/imklfx-catalogue-tests
```

An exit status of zero covers changed version, arbitrary CDHash, changed offset
inside the window, strict precedence, boot gating, and identity rejection.

A live BoundedWindow dry run requires a genuine publisher-signed Discord update
whose strict CDHash no longer matches, but which still has:

- the compiled path grammar and basename;
- signing identifier `discord_krisp`;
- Team ID `53Q6R32WPB`;
- valid hardened-runtime, non-ad-hoc signing; and
- exactly one complete compiled MKL definition and context inside
  `0x650000..<0x651000`.

If no such genuine update exists, stop Stage E after the host fixture and do
not manufacture one.

For a genuine update only, enable:

```text
-imklfxwindow -imklfxdryrun -imklfxdbg
```

Validate the plist, reboot manually, and launch the unmodified signed update.

### Expected logs

```text
candidate-app-approved application=discord-stable-krisp image=discord-stable-krisp-bounded-window-0x650000-x86_64-v1 mode=bounded-window
image=discord-stable-krisp-bounded-window-0x650000-x86_64-v1 outcome=search-started scope=bounded-window start=0x650000 end=0x651000
image=discord-stable-krisp-bounded-window-0x650000-x86_64-v1 patch=mkl-serv-intel-cpu-true-oneapi-build-20201104-x86_64-v1 search=unique-supported-signature outcome=dry-run-match modified=no offset=0x...
```

### Success criteria

- Strict evidence does not approve the updated module.
- BoundedWindow approves only after identity and signing checks.
- Search begins only for the one compiled page.
- Exactly one complete signature/context match is reported.
- The candidate offset lies within `0x650000..<0x651000`.
- `modified=no` is reported.

This proves uniqueness only in that callback-complete 4 KiB window. It makes no
statement about matching bytes elsewhere in the image.

### Failure criteria and stop condition

Stop on zero or multiple matches, a match outside the page, incomplete callback
coverage, identity/signing rejection, any write in dry-run, or any claim/log
interpreted as image-wide uniqueness.

### Recovery

Remove `-imklfxwindow` and `-imklfxdryrun`, validate, and return to the last
passing strict configuration. Use Recovery action R for abnormal behavior.

## Stage F — BoundedWindow active patch

### Prerequisites

- A genuine updated Discord module—not a modified test copy—passed Stage E
  with exactly one dry-run match.
- Its original on-disk SHA-256 is recorded and it remains publisher-signed.

### Procedure

Remove only `-imklfxdryrun`; retain:

```text
-imklfxwindow -imklfxdbg
```

Validate, reboot manually, keep the updated module unpatched on disk, fully
quit Discord/helpers, and launch Discord.

### Expected logs

```text
mode=bounded-window
outcome=search-started scope=bounded-window start=0x650000 end=0x651000
search=unique-supported-signature outcome=patched modified=yes offset=0x...
```

### Success criteria

Exactly one compiled definition is found in the approved page, runtime memory
is modified, Discord voice/Krisp/device behavior passes, and the on-disk hash
remains the genuine update's original hash.

This proves update tolerance only while the reviewed implementation remains
inside `0x650000..<0x651000`. It is not image-wide update tolerance.

### Failure criteria and stop condition

Stop on zero/multiple signatures, offset outside the window, signature/context
failure, write/verification failure, changed on-disk hash, instability, or a
functional Discord/Krisp regression.

### Recovery

Use Recovery action R, then return to the last passing strict configuration.

## Stage G — Negative controls

### Prerequisites

- Preserve all passing configuration backups.
- Use host fixtures for altered code and wrong identities. Never modify a
  production signed application to create a negative case.

### Procedures

1. **Plugin disabled:** append `-imklfxoff`, validate, reboot manually, and
   confirm no `lifecycle=loaded` or patch log appears. With original Discord on
   disk, record the expected affected behavior; do not assume the symptom.
2. **BoundedWindow absent:** remove `-imklfxwindow`, retain strict settings,
   validate/reboot, and confirm no bounded-window `search-started` line occurs.
3. **Dry run:** enable `-imklfxdryrun`, validate/reboot, and require
   `modified=no`.
4. **Unknown/altered/zero/multiple signatures and wrong identity:** run the
   reviewed host suites:

   ```sh
   cd /Users/richardhedges/Desktop/IntelMKLFixup
   xcrun clang++ -std=c++14 -Wall -Wextra -Werror -pedantic \
     Tests/PolicyTests/PolicyTests.cpp -o /tmp/imklfx-policy-tests
   /tmp/imklfx-policy-tests
   xcrun clang++ -std=c++14 -Wall -Wextra -Werror -pedantic \
     Tests/PolicyTests/CatalogueTests.cpp -o /tmp/imklfx-catalogue-tests
   /tmp/imklfx-catalogue-tests
   ```

### Expected result and success criteria

- `-imklfxoff`: plugin does not load or patch.
- no `-imklfxwindow`: BoundedWindow cannot patch.
- dry run: a valid match reports no modification.
- host fixtures: altered/unknown/zero/multiple/wrong-identity cases all reject
  with process exit status zero for the test executables.

### Failure criteria and stop condition

Stop if the plugin logs while disabled, BoundedWindow operates without its boot
argument, dry-run modifies memory, or any negative host fixture accepts.

### Recovery

Restore the last passing boot-argument/config backup. Use Recovery action R if
the system is unstable.

## Stage H — Stability observation

### Prerequisites

- Complete only modes that passed their preceding functional stage.
- Keep the USB recovery EFI connected and unchanged.

### Commands and observations

Observe for at least several normal work sessions and one additional reboot:

```sh
/usr/bin/log show --last 24h --style compact \
  --predicate 'eventMessage CONTAINS[c] "panic" OR eventMessage CONTAINS[c] "watchdog"'
/usr/bin/log show --last 24h --style compact \
  --predicate 'eventMessage CONTAINS[c] "imklfx"'
/usr/bin/pmset -g log | /usr/bin/grep -E ' Sleep | Wake '
```

Record boot delay relative to baseline, `imklfx` log-line count, CPU use before
and during repeated Discord launches, sleep/wake, unrelated applications, and
normal audio. Re-run Discord voice input/output, Krisp, and device changes after
sleep/wake and after the additional reboot.

### Success criteria

No kernel panic/watchdog, no material boot delay, bounded event-driven logs,
no persistent CPU overhead, stable sleep/wake, normal unrelated applications,
repeatable Discord behavior, and unchanged on-disk module hash.

### Failure criteria and stop condition

Any panic, watchdog, boot-loop tendency, persistent CPU/log spike, sleep/wake
failure, unrelated-app regression, or changed on-disk hash ends the test.

### Recovery

Use Recovery action R. Preserve the panic file, unified logs, OpenCore/Lilu
versions, exact boot arguments, kext hash, Discord version/path/hash, and the
last passing stage when reporting the failure.
