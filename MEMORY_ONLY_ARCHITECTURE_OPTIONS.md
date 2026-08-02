# Memory-only architecture options

## Decision summary

| Candidate | Can be early enough? | Current admission/authority | Security fit | Finding |
|---|---|---|---|---|
| A. In-process authenticated companion | Yes, if present before `require()` | No supported route in current Renderer | Best conditional fit | Architecturally sound but unavailable |
| B. Lilu `injectPayload` | Yes, at process entry | Kernel plugin can force it | Violates explicit constraints | Rejected |
| C. External task-aware helper | Not reliably; polling loses the race | No normal task port (`get-task-allow` absent) | Would add powerful debugger/injection authority | Rejected under current policy |
| D. Wrapper/environment interposition | Potentially | DYLD admission blocked; Node preload cannot bypass native LV | Fragile and update-hostile | Rejected |
| E. Supported app extension | Would depend on Discord bootstrap | None found | Official same-Team extension would be preferable | Not currently available |
| F. Function interception | Only if installed early | Internal direct calls bypass symbol rebinding | Would fall back to code modification | Not applicable |

## A. In-process authenticated companion library

The controlled fixture establishes the desirable sequence:

```text
observer already running
  -> dlopen maps native module
  -> dyld add-image callback identifies mapping
  -> library initializers
  -> Node-API registration
  -> JavaScript _initialize
```

In principle, the callback could validate the Renderer and module, derive the
address from the mapped Mach-O, and act before even an initializer calls the
predicate. Running inside the target also avoids a task-port race and naturally
scopes a COW fault to that task.

The blocker is admission. The Krisp-owning Renderer is Hardened Runtime and
does not carry `com.apple.security.cs.disable-library-validation`. Ordinary
loaded code must therefore be Apple-signed or signed by Discord's Team
`53Q6R32WPB`. This project cannot independently create such code. Discord's
generic Helper has the disable-LV entitlement, but it is not the process that
loads Krisp.

Electron preload scripts are selected by the application when it constructs a
renderer. They are not an external extension contract. A JavaScript preload
also cannot load an independently signed native observer past the Renderer's
Library Validation. Resigning Discord, changing entitlements, or disabling LV
would violate the requirements and disrupt update trust.

**Finding:** safest architecture if Discord itself adopts or signs the
component; not presently implementable by this project.

## B. Lilu `injectPayload` and related facilities

Pinned source reviewed: Lilu 1.7.2, commit
`e4748cc081bf060302c7d3c44a643ce1d11b7e1d`.

`injectPayload` replaces the process entry point, copies a raw payload into
Mach-O header slack, and changes the containing protection while writing.
For executable pages, Lilu's `vmProtect` requests W+X. When code-signing
enforcement applies, `vmProtect` clears `CS_KILL`, `CS_HARD`, and
`CS_ENFORCEMENT`, sets `CS_DEBUGGED`, and disables map switch protection. The
source explicitly does not restore those process code-signing flags.

Lilu does not directly clear the Library Validation flag in this path; raw
payload injection avoids ordinary dylib admission instead. That distinction
does not make it acceptable: the process is injected, temporarily W+X, and
left with prohibited security state changes.

The process-load hook observes `exec`, so it can run before Electron starts and
can match the Renderer executable path. It does not natively authenticate a
Team ID/CDHash in `ProcInfo`; matching is path/prefix/suffix based. It also
does not observe later `dlopen` by itself, so the injected payload would need
to remain in process and register the callback.

Ordinary Lilu `BinaryModInfo`/`performPagePatch` writes the validation buffer,
the same revoked vnode/UBC-backed class of destination. `injectSegment` also
adds write permission to existing segment protections, yielding RWX for an
executable segment.

Source evidence:

- [Lilu `vmProtect`](https://github.com/acidanthera/Lilu/blob/e4748cc081bf060302c7d3c44a643ce1d11b7e1d/Lilu/Sources/kern_user.cpp#L29-L91)
- [Lilu validation-page patching](https://github.com/acidanthera/Lilu/blob/e4748cc081bf060302c7d3c44a643ce1d11b7e1d/Lilu/Sources/kern_user.cpp#L166-L274)
- [Lilu `injectPayload`](https://github.com/acidanthera/Lilu/blob/e4748cc081bf060302c7d3c44a643ce1d11b7e1d/Lilu/Sources/kern_user.cpp#L468-L585)

**Finding:** technically early, but explicitly disqualified by injection,
`CS_DEBUGGED`, enforcement weakening, W+X, and insufficient native identity
binding.

## C. External task-aware userspace patcher

A helper would have to learn of the exact Renderer instance, acquire its task
port, wait for the Krisp image, and change that task's VM before synchronous
`require()` proceeds to `_initialize`. The target has no `get-task-allow`.
Current macOS taskgated/code-signing policy does not make a Hardened Runtime
production process a supported debug target merely because a helper is root;
a debugger-capable caller entitlement is not a grant by the target.
Apple documents the caller-side authority separately as the
[`com.apple.security.cs.debugger` entitlement](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.security.cs.debugger).

Even if extraordinary kernel-granted task access were added, a polling helper
has no proven scheduling point between dyld mapping and initializers. A launch
daemon improves lifecycle observation but not this ordering. Such a helper's
task port is also general read/write/control authority over Discord; compromise
turns a six-byte policy into arbitrary process injection.

**Finding:** no safe, supported acquisition or race-free timing under the
current constraints. Root alone is neither sufficient evidence nor an
acceptable security design.

## D. Launch wrapper or environment interposition

`DYLD_INSERT_LIBRARIES` is not a viable production route. The Renderer lacks
the Hardened Runtime entitlement that permits DYLD environment variables, and
Library Validation would reject an independently signed inserted library.
Launching through a wrapper also bypasses normal Finder/updater launch paths
and creates environment inheritance and maintenance risk.

Relevant platform references are Apple's
[`allow-dyld-environment-variables` entitlement](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.security.cs.allow-dyld-environment-variables)
and Electron's [fuse documentation](https://www.electronjs.org/docs/latest/tutorial/fuses).

Electron's embedded fuse settings indicate the `NODE_OPTIONS` fuse is enabled,
but this does not create a supported Discord extension point. A Node preload
that attempts to load a native observer still encounters Library Validation;
modifying packaged bootstrap JavaScript would alter the application and be
update-fragile. `RunAsNode` is fused off.

**Finding:** predictably blocked for native injection and unsuitable for
normal update-compatible launching.

## E. Supported application-level extension point

No Discord-supported plugin, preload, updater, helper-extension, accessibility,
or audio-plugin mechanism was found that executes a native component inside
the Krisp-owning Renderer before its native-module `require()`. Accessibility
and audio plug-ins normally execute in other security/process domains and do
not provide this load-order point.

Unofficial Discord client modifications can alter preload/bootstrap behavior,
but they modify Discord's trust boundary, increase account and supply-chain
risk, and are routinely broken by updates. They are not a reasonable security
foundation for this project.

**Finding:** an official, same-Team Discord extension would solve the hardest
admission problem; none is currently exposed.

## F. Function interception instead of byte replacement

The x86_64 target is exported as `_mkl_serv_intel_cpu_true` at image-relative
address `0x64c100`. Export visibility alone is insufficient for interposition.
Static disassembly finds 16 calls within `discord_krisp.node`, all direct
PC-relative calls to `0x64c100`. The indirect-symbol table has no entry for the
predicate.

Fishhook-style rebinding changes imported lazy/non-lazy symbol pointers. It
cannot redirect these direct internal calls. Dyld interposition similarly does
not rewrite already locally bound direct branches. Hooking every call site or
the function entry would again modify executable bytes; changing a private
function pointer is unavailable because no such dispatch pointer was found.

**Finding:** no byte-free interception mechanism applies to the actual call
structure.

## Overall conclusion

The official/same-Team in-process design is the safest conditional
architecture, but it is not available. No candidate is both practical and
compliant today. The investigation therefore concludes **not currently
practical**, rather than treating technical injection capability as
feasibility.
