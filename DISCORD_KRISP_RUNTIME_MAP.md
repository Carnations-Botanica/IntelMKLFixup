# Discord Krisp runtime map

## Status and safety boundary

This report records only read-only observations made on macOS 15.7.7
(24G720), Darwin 24.6.0, x86_64, on 2026-08-01. No task memory was written,
no VM protection was changed, and no code was injected.

Live mapping work is **incomplete**. During this investigation the ordinary
file-visible view of the installed module changed from the reviewed original
bytes to the revoked six-byte replacement while the unsafe kext had not yet
been confirmed disabled. The trigger could have been the user's normal Discord
launch or an analysis tool mapping the Mach-O slice; the evidence does not
distinguish them. All further live Discord mapping was stopped. Do not rerun
the live probe until the unsafe kext is disabled and the publisher file is
restored and verified.

## Process that loads Krisp

The best available evidence identifies **Discord Helper (Renderer)**, not the
main Discord, GPU, Plugin, or generic Helper process.

Two existing macOS incident reports contain both the process identity and
`discord_krisp.node` in `usedImages`:

| Field | First incident | Second incident |
|---|---:|---:|
| Report | `Discord Helper (Renderer)-2026-07-27-133528.ips` | `Discord Helper (Renderer)-2026-07-27-133803.000.ips` |
| PID | 11877 | 11986 |
| Launch | 2026-07-27 13:34:53.5335 +0100 | 2026-07-27 13:37:54.4767 +0100 |
| Capture | 2026-07-27 13:35:13.4529 +0100 | 2026-07-27 13:38:02.3236 +0100 |
| Executable | `/Applications/Discord.app/Contents/Frameworks/Discord Helper (Renderer).app/Contents/MacOS/Discord Helper (Renderer)` | same |
| Parent | Discord, PID 11845 | Discord, PID 11956 |
| Signing identifier | `com.hnc.Discord.helper.Renderer` | same |
| Team ID | `53Q6R32WPB` | same |
| CPU | x86_64 | x86_64 |
| SIP in report | enabled | enabled |

The first report records Krisp as an x86_64 image with base decimal
`16882593792` (`0x3ee47f000`) and text-image size `14168064` (`0xd83000`).
Its UUID is from the older July incident and differs from the currently
installed module, so this is ownership and address-layout evidence, not a
claim about the current build's live address.

The crash reports do not establish automatic restart. The later report has a
new Renderer PID and a new Discord parent PID, which is also consistent with a
manual whole-application relaunch. Deliberately crashing a current process was
out of scope.

## Current signing policy

Static `codesign` inspection of the current 0.0.403 bundle produced:

| Executable | Identifier | Team | CodeDirectory flags | Relevant entitlements |
|---|---|---|---|---|
| Discord | `com.hnc.Discord` | `53Q6R32WPB` | `0x10000(runtime)` | allow JIT; allow unsigned executable memory; no get-task-allow; no App Sandbox; no disable-library-validation |
| Helper (Renderer) | `com.hnc.Discord.helper.Renderer` | `53Q6R32WPB` | `0x10000(runtime)` | allow JIT; allow unsigned executable memory; audio/camera; no get-task-allow; no App Sandbox; no disable-library-validation |
| Generic Helper | `com.hnc.Discord.helper` | `53Q6R32WPB` | `0x10000(runtime)` | **disable-library-validation**; allow unsigned executable memory |
| Helper (GPU) | `com.hnc.Discord.helper.GPU` | `53Q6R32WPB` | `0x10000(runtime)` | allow unsigned executable memory; no disable-library-validation |
| Helper (Plugin) | `com.hnc.Discord.helper.Plugin` | `53Q6R32WPB` | `0x10000(runtime)` | allow unsigned executable memory; no disable-library-validation |
| Krisp module | `discord_krisp` | `53Q6R32WPB` | `0x10000(runtime)` | none displayed |

The Renderer therefore has Hardened Runtime and, because it has no
`com.apple.security.cs.disable-library-validation`, library validation applies
by default. Apple's documented rule permits Apple-signed or same-Team code;
the current Krisp module satisfies the same-Team rule. An independently signed
companion does not. See Apple's
[Library Validation entitlement documentation](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.security.cs.disable-library-validation).

The incident report's dynamic flags were decimal `570507777`
(`0x22014201`): valid, kill, entitlements validated, runtime, dyld-platform,
and signed. They did **not** include get-task-allow or `CS_DEBUGGED`.

No executable declares `com.apple.security.app-sandbox`. That proves absence
of the macOS App Sandbox entitlement only. Electron/Chromium can apply a
separate renderer sandbox dynamically; its live command line was not captured.
Because this renderer loads an arbitrary native Node module, Electron's own
documentation suggests it is probably configured unsandboxed, but that is an
inference and not a verified result.

## Module layout

Static inspection was completed while the ordinary file view still matched
the reviewed original:

| Property | Result |
|---|---|
| Current app version | 0.0.403 |
| Module path | `/Users/richardhedges/Library/Application Support/discord/app-0.0.403/modules/discord_krisp-1/discord_krisp/discord_krisp.node` |
| Original SHA-256 observed at start | `de061edb4387fc5bba2b8535483aa2f4347c17bc9e4d25babef36172c86a9f9a` |
| Format | universal Mach-O bundle |
| Slices | x86_64 and arm64 |
| x86_64 fat-slice file offset | `0x4000` |
| x86_64 slice size | `0x10fa0c0` (17,801,408 bytes) |
| x86_64 UUID | `9F796A90-D4AE-3E1B-A748-620B623EA9AA` |
| x86_64 `__TEXT` VM address | `0x0` |
| x86_64 `__TEXT` file offset within slice | `0x0` |
| x86_64 `__TEXT` VM/file size | `0xd83000` |
| Mach-O `__TEXT` initial/max protection | `r-x` / `r-x` in load command |
| `__text` | address `0xd00`, size `0xcb81d0` |
| Absolute universal-file target offset | `0x650100` |
| Slice-relative target VM offset | `0x64c100` |
| Original target sequence | `53 48 83 ec 20 8b 35 61 0f 79 00 85 f6 7c 08 89 f0 48 83 c4 20 5b c3` |

For any live instance:

```text
target VM address = x86_64 image load address + 0x64c100
```

For the older first incident only, this yields historical address
`0x3eeacb100` from load address `0x3ee47f000`. ASLR makes this address
run-specific.

## Mapping table

| Process | Module | File offset | VM address | Protections | Sharing/object state | Confidence and evidence |
|---|---|---:|---:|---|---|---|
| Discord Helper (Renderer), historical PID 11877 | historical `discord_krisp.node` | target layout believed compatible, but current UUID differs | historical formula result `0x3eeacb100` | not recorded by `.ips` | not recorded | High confidence for owner and load base; low for applying current variant identity |
| Current Renderer, after safe relaunch | current 0.0.403 module | `0x650100` absolute / `0x64c100` slice-relative | **not measured** | **not measured** | **not measured** | Pending `proc_pidinfo`/`vmmap` probe after kext disable and file restore |

Do not infer COW from `__TEXT`, `r-x`, or the presence of a file path. The
actual `pri_share_mode`, current and maximum protections, vnode path, object
ID, resident/private/shared page counts, and target address must be captured
from the live target.

## Load timing

The installed wrapper is direct:

```js
const KrispModule = require('./discord_krisp.node');
// ...
KrispModule._initialize(initializationParams);
```

`renderer_js.log` records “Initializing voice engine” during normal renderer
startup, followed by the list of native modules including `discord_krisp`.
`discord_krisp.log` then records `_initialize` and successful SDK
initialisation immediately. Mic Test-related `KrispNCSetup` occurs seconds
later. For example, PID 6556 logged SDK initialization at 23:17:21.583 and
Mic/Test noise-canceller setup at 23:17:29.712.

Therefore the module is loaded and initialized during Renderer startup on this
build; Mic Test is not a safe image-load trigger or a guaranteed patch window.
Whether the exact target page becomes resident, and whether the predicate is
first executed, inside `_initialize` was not traced.

## Exact commands and material results

Commands were run without `sudo`.

```sh
ps axo pid=,ppid=,state=,etime=,command= | rg -i 'Discord|discord_krisp'
```

This was blocked by the tool sandbox initially and later returned no live
Discord processes. No process was attached.

```sh
find "$HOME/Library/Application Support/discord" -name discord_krisp.node -type f -print
```

Result: the 0.0.403 path listed above.

```sh
codesign -dvvv --entitlements - /Applications/Discord.app/Contents/MacOS/Discord
codesign -dvvv --entitlements - '/Applications/Discord.app/Contents/Frameworks/Discord Helper (Renderer).app/Contents/MacOS/Discord Helper (Renderer)'
codesign -dvvv --entitlements - '/Users/richardhedges/Library/Application Support/discord/app-0.0.403/modules/discord_krisp-1/discord_krisp/discord_krisp.node'
```

Material results are in the signing table. Use `--entitlements -`, not the
deprecated `:-` form; the latter emitted a misleading invalid-blob warning.

```sh
lipo -detailed_info '/Users/richardhedges/Library/Application Support/discord/app-0.0.403/modules/discord_krisp-1/discord_krisp/discord_krisp.node'
otool -arch x86_64 -l '/Users/richardhedges/Library/Application Support/discord/app-0.0.403/modules/discord_krisp-1/discord_krisp/discord_krisp.node'
xxd -g1 -l 64 -s 0x6500f0 '/Users/richardhedges/Library/Application Support/discord/app-0.0.403/modules/discord_krisp-1/discord_krisp/discord_krisp.node'
```

These produced the static layout and original sequence above. They must not be
rerun against the active path until the unsafe kext is confirmed disabled.

```sh
rg -l 'discord_krisp\.node' "$HOME/Library/Logs/DiagnosticReports"
rg -n -B8 -A12 'discord_krisp\.node' "$HOME/Library/Logs/DiagnosticReports/Discord Helper (Renderer)-2026-07-27-133528.ips"
```

Results: two Renderer incident reports; the first contained image base
`16882593792`, size `14168064`, and the ownership/signing fields above.

## Unresolved live observations

- current PID, parent PID, and command-line sandbox switches;
- current load address and target address;
- current and maximum VM protections;
- COW/private/shared mode and per-region object ID;
- whether another Discord process maps the same vnode/object;
- exact target-page residency before Mic Test;
- equality of process bytes to the restored original sequence;
- read-only `task_for_pid` result on this exact current process;
- automatic restart behavior after Renderer failure.

These are prerequisites for any future write experiment, not optional polish.
