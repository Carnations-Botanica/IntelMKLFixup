# Read-only diagnostics

## Safety status

No diagnostic in this investigation intentionally wrote task memory, changed
VM protections, injected code, changed code-signing state, used `sudo`, or
patched a file. The fixture only compiled temporary binaries and recorded load
events.

However, the unsafe kext remained loaded. An ordinary read/mapping operation
or the user's normal Discord launch coincided with the active module's
file-visible bytes changing from the reviewed original to the revoked
replacement. Because the unsafe implementation acts when executable pages are
validated, even nominally read-only commands that cause a Mach-O page to be
mapped are unsafe while it is loaded. All access to the live Krisp path and all
Discord launches were stopped.

Do not run the live workflow below until the user has rebooted with the unsafe
kext disabled and restored the module through a trusted source.

## Diagnostic tools added

### `Tools/ReadOnlyKrispProbe`

`read_only_krisp_probe.c`:

- enumerates Discord processes using `libproc`;
- records PID, parent PID, start time, executable path, and code-signing status;
- enumerates matching module regions with `PROC_PIDREGIONPATHINFO`;
- reports current/max protection, share mode, object ID, residency, private and
  shared page counts, and vnode path;
- derives the target VM address from the region/file offset;
- attempts read-only `task_for_pid` plus `mach_vm_read_overwrite`; and
- compares target bytes to the compiled reviewed original sequence.

It contains no task write, `mach_vm_protect`, `mprotect`, `vm_protect`, ptrace,
code-signing mutation, or injection primitive. Task-port acquisition is used
only for a read attempt and is expected to be denied by normal production
policy.

Build/run command, **only after the reboot/restore gate**:

```sh
Tools/ReadOnlyKrispProbe/run.sh /restored/absolute/path/to/discord_krisp.node
```

The script builds into a `mktemp` directory and removes only that directory on
exit. Default constants are universal-file offset `0x650100` and x86_64 slice
offset `0x4000`, yielding image-relative `0x64c100`.

### `Tools/LoadOrderFixture`

The fixture host registers `_dyld_register_func_for_add_image`, queries its
own new image with read-only Mach/proc APIs, and `dlopen`s a harmless library.
The library constructor calls a known test predicate immediately. No bytes or
protections are changed.

```sh
Tools/LoadOrderFixture/run.sh
```

Observed output:

```text
01 host-start           registering image observer
02 before-dlopen        .../libFixture.dylib
03 image-add-callback   ... prot=r-x max=rwx shared=no share-mode=COW
04 initializer          library constructor entered
05 initializer-before-predicate calling predicate from constructor
06 predicate            test predicate executed
07 after-dlopen         dlopen returned
08 before-predicate     calling test predicate
09 predicate            test predicate executed
10 after-predicate      result=7
```

## Commands used and findings

All commands were non-privileged. Repository shell commands were executed
through the repository-required `rtk` wrapper.

### Host and bundle

```sh
sw_vers
uname -a
file /Applications/Discord.app/Contents/MacOS/Discord
defaults read /Applications/Discord.app/Contents/Info CFBundleShortVersionString
```

Results: macOS 15.7.7 (24G720), Darwin 24.6.0, x86_64; Discord 0.0.403.
The bundled Electron Framework reports 42.7.1 / Chrome 148.

### Signing and entitlements

```sh
codesign -dvvv --entitlements - /Applications/Discord.app/Contents/MacOS/Discord
codesign -dvvv --entitlements - '/Applications/Discord.app/Contents/Frameworks/Discord Helper (Renderer).app/Contents/MacOS/Discord Helper (Renderer)'
codesign -dvvv --entitlements - '/Applications/Discord.app/Contents/Frameworks/Discord Helper.app/Contents/MacOS/Discord Helper'
codesign -dvvv --entitlements - '/Applications/Discord.app/Contents/Frameworks/Discord Helper (GPU).app/Contents/MacOS/Discord Helper (GPU)'
codesign -dvvv --entitlements - '/Applications/Discord.app/Contents/Frameworks/Discord Helper (Plugin).app/Contents/MacOS/Discord Helper (Plugin)'
codesign -dvvv --entitlements - "$krisp_path"
```

Material results are tabulated in `DISCORD_KRISP_RUNTIME_MAP.md`. The Renderer
is Team `53Q6R32WPB`, Hardened Runtime, without get-task-allow, App Sandbox, or
disable-Library-Validation entitlements. The generic Helper's LV exception
does not transfer to the Renderer.

### Mach-O layout and symbols

These commands were completed against the reviewed original before the safety
stop, or against the non-active backup copy for later static disassembly:

```sh
lipo -detailed_info "$krisp_path"
otool -arch x86_64 -l "$krisp_path"
nm -arch x86_64 -nm "$krisp_path" | rg 'mkl_serv_intel_cpu_true'
dyld_info -arch x86_64 -exports "$krisp_path" | rg 'mkl_serv_intel_cpu_true'
otool -arch x86_64 -Iv "$krisp_path" | rg 'mkl_serv_intel_cpu_true'
xxd -g1 -l 64 -s 0x6500f0 "$krisp_path"
```

The current x86_64 symbol is exported at `0x64c100`, with no indirect-symbol
entry. Disassembly found 16 direct internal call sites. The target's reviewed
23-byte sequence is:

```text
53 48 83 ec 20 8b 35 61 0f 79 00 85 f6 7c 08 89 f0 48 83 c4 20 5b c3
```

The `.mkl-original` backup matched the initial SHA-256 and bytes, but
`codesign --verify --strict` reported an invalid x86_64 signature. It was
therefore useful as corroborating static byte material, not as proof of a
trusted publisher artifact.

### Historical process ownership

```sh
rg -l 'discord_krisp\.node' "$HOME/Library/Logs/DiagnosticReports"
rg -n -B8 -A12 'discord_krisp\.node' "$report_path"
```

Two existing incident reports identify Discord Helper (Renderer) as the
loader, record its parent Discord process, and include the Krisp used image.
The current UUID differs, so their addresses are historical only.

### Installed wrapper and timing logs

```sh
sed -n '1,220p' "$discord_modules/discord_krisp-1/discord_krisp/index.js"
rg -n 'discord_krisp|Initializing voice engine|KrispNCSetup|Mic' "$discord_logs"
```

The wrapper synchronously requires the `.node` module and calls `_initialize`.
Logs show SDK initialization during Renderer startup and noise-canceller/Mic
setup later. Mic Test is not the module-load trigger.

### Pinned Lilu source

The upstream 1.7.2 tag was cloned read-only into a temporary directory and
resolved to commit `e4748cc081bf060302c7d3c44a643ce1d11b7e1d`.
`rg`/`sed` inspection covered `vmProtect`, `performPagePatch`, process-exec
listeners, `injectPayload`, `injectSegment`, and `ProcInfo` matching. Findings
are in `MEMORY_ONLY_ARCHITECTURE_OPTIONS.md`.

## Post-reboot live workflow requiring separate approval

The following is a proposed read-only workflow, not performed in this phase:

1. Confirm the unsafe kext is absent using a read-only loaded-extension query.
2. Restore/update Discord from a trusted source; do not use the invalid backup
   as publisher proof.
3. Before launch, capture `codesign --verify --strict --deep`, SHA-256, UUID,
   inode, size, mtime, ctime, and target/context bytes.
4. Launch Discord normally and record Renderer PID/PPID/start/command line.
5. Run the read-only probe and `vmmap <pid>`/`lsof -p <pid>` during early
   startup, then after Mic Test.
6. Quit normally and re-check every file identity/timestamp/hash value.
7. Compare object/share/residency evidence across all Discord processes.

No write, protection change, injection, debugger attach, crash, or security
flag change is part of that workflow.
