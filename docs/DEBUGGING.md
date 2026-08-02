# Diagnostics and controls

IntelMKLFixup is experimental kernel software. Active validation-page patching
has been revoked: hardware testing proved that it can change the vnode-backed
file contents. The current source treats the callback as detection-only. The
plugin does not install itself or edit OpenCore.

## Boot arguments

Append arguments to the existing OpenCore `boot-args` value under
`NVRAM -> Add -> 7C436110-AB2A-4BBB-A880-FE41995C9F82`. Do not replace other
arguments already required by the system.

| Argument | Effect |
|---|---|
| `-imklfxoff` | Completely prevents Lilu from starting this plugin. A reboot is required. |
| `-imklfxdbg` | Enables verbose IntelMKLFixup candidate diagnostics. Usernames are redacted from paths. It does not enable patching by itself. |
| `-imklfxdryrun` | Performs the OS, CPU, page-validation, path, signing-identity, configured CDHash/offset or bounded-window, signature, and context checks. The current source never writes even when this argument is absent. |
| `-imklfxwindow` | Experimentally enables compiled BoundedWindow policies. It does not enable broad or image-wide scanning. Without it, BoundedWindow policies fail closed. |
| `-imklfxbuiltin` | Explicitly pins policy to the whitelist compiled into this kext. This is already the only policy source in the current version; the flag records that the choice was deliberate. |

The recommended first-test set is:

```text
-imklfxdryrun -imklfxdbg -imklfxbuiltin
```

That set exercises StrictVariant only. After the strict fixture has passed and
BoundedWindow testing is explicitly approved, use:

```text
-imklfxwindow -imklfxdryrun -imklfxdbg -imklfxbuiltin
```

If `-imklfxoff` is present, the other IntelMKLFixup arguments have no effect
because Lilu does not invoke the plugin startup callback.

There is deliberately no kernel boot argument for an external whitelist. The
current kext does not read files, JSON, NVRAM blobs, or network data. Phase 5
must first establish a signed userspace update and safe boot-time delivery
design. Until then, the compiled whitelist is mandatory.

## Log prefix and events

Every plugin message uses Lilu's unique `IntelMKLFixup` product prefix and the
`imklfx` module prefix. Messages use stable `key=value` fields. Important
states are distinct:

| Evidence | Representative event | Meaning |
|---|---|---|
| Plugin loaded | `lifecycle=loaded` | Lilu accepted the plugin configuration and ran its startup callback. This does not mean the kernel route was installed. |
| Route installed | `lifecycle=route-installed darwin=24 cpu=amd` | All required symbols resolved and `_cs_validate_page` was routed. This does not mean Discord was seen. |
| Candidate identified | `event=candidate-app-approved` | The current callback range can contain a compiled policy target and the module passed the applicable path and signing gates. It may still be rejected. |
| Window search started | `event=search-started mode=bounded-window` | The complete approved one-page window is present and its experimental boot gate is enabled. This is not an image-wide search. |
| Window search disabled | `outcome=search-mode-disabled` | A matching BoundedWindow policy exists, but `-imklfxwindow` is absent. No search or write occurs. |
| Signature found in dry run | `signature=supported outcome=dry-run modified=no` | Both image approval and the exact compiled MKL signature/context succeeded. No write was attempted. |
| Unsafe active write blocked | `signature=supported outcome=unsafe-file-backed-write-blocked modified=no` | A supported target was detected without dry-run, but the validation page was not written. This is the required fail-closed result. |
| Skipped | `outcome=skipped` | The target was already patched during the checked callback. |
| Rejected | `outcome=rejected reason=...` | A candidate failed closed. The reason identifies the failed gate without printing untrusted identity strings. |
| Error | `outcome=error reason=...` | A policy or routing error occurred. Stop testing and use the recovery procedure. |

No message is emitted for ordinary validation callbacks, unrelated paths, or
the absence of MKL code. Candidate messages occur only at a compiled strict
target page or approved BoundedWindow. With verbose mode enabled, a candidate rejected by XNU page
validation can be reported before identity matching; this is useful when
examining an on-disk-modified module.

The callback does not provide trustworthy host-process identity. Logs report
the image-family, exact image-variant, signing, and patch-definition identifiers
that the callback can establish. They do not claim that a particular process
owns the validated page.

## Inspecting startup and runtime logs

Show IntelMKLFixup messages from the current boot:

```sh
/usr/bin/log show --last boot --style compact \
  --predicate 'process == "kernel" AND eventMessage CONTAINS[c] "IntelMKLFixup"'
```

Watch for new messages while launching Discord:

```sh
/usr/bin/log stream --style compact \
  --predicate 'process == "kernel" AND eventMessage CONTAINS[c] "IntelMKLFixup"'
```

Show only final candidate outcomes:

```sh
/usr/bin/log show --last boot --style compact \
  --predicate 'process == "kernel" AND eventMessage CONTAINS[c] "IntelMKLFixup" AND eventMessage CONTAINS[c] "outcome="'
```

Inspect the configured boot arguments without changing them:

```sh
/usr/sbin/nvram boot-args
```

Check whether the operating system reports IntelMKLFixup and Lilu as loaded:

```sh
/usr/bin/kmutil showloaded --list-only | /usr/bin/grep -Ei 'IntelMKLFixup|Lilu'
```

An empty `kmutil` result is not by itself proof that an OpenCore-injected kext
did not run. Correlate it with the lifecycle logs. Conversely,
`lifecycle=loaded` is not proof that the route, candidate, signature, patch, or
Discord functionality succeeded.

Lilu Debug builds can additionally use Lilu's documented `-liludbg` or
`-liludbgall` controls. Those flags are not required for IntelMKLFixup's own
`-imklfxdbg` messages and may substantially increase unrelated log volume.

## Interpreting the currently patched Discord installation

The locally installed `discord_krisp.node` remains modified and ad-hoc signed
by the separate Swift patcher. IntelMKLFixup must not approve that altered
identity as the reviewed original. In verbose dry-run testing, an
`xnu-page-validation`, `signing-flags`, signing-identity, or CDHash rejection is
therefore expected. Do not restore or alter the file merely to make a dry-run
log succeed; follow the staged Ryzen 9 3900X test plan when it is created.
