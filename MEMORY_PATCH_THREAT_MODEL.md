# Memory-patch threat model

## Assets and security objectives

The protected assets are Discord's signed files, vnode/UBC contents, other
processes' mappings, the target process's code-signing state, user credentials
inside Discord, and the narrowness of the compiled patch catalogue. The
security objective is not merely “leave the hash unchanged”: the operation
must affect exactly one verified process-private page for exactly one approved
module and disappear with that process.

Trust is divided among:

- the local compiled `ApplicationRule`, `ImageVariant`, and `PatchDefinition`;
- a signed local program or kernel component, if ever approved;
- Discord's signed executable and same-Team module;
- optional remotely delivered policy selection; and
- release/update infrastructure.

The process path, PID, module path, remotely supplied metadata, and any
userspace helper are inputs, not identities.

## Candidate risk comparison

| Candidate | Privilege / authority | Principal attack surface | Consequence of compromise |
|---|---|---|---|
| Official in-process companion | Discord Team admission; target's own VM authority | Native parser/callback in a credential-bearing Renderer | Arbitrary behavior inside that Renderer, but not a general cross-process task port |
| Lilu payload injection | Kernel extension and kernel VM/code-signing manipulation | Kernel parsers, path matching, raw payload, W+X window | Kernel compromise or injection/security weakening of matched processes |
| External task helper | Debug/task-port authority, likely extraordinary platform grant | IPC, PID lifecycle, task port, address derivation, update channel | General read/write/control of Discord and possibly other tasks |
| Wrapper/environment preload | User launch environment plus admitted code | Environment inheritance, preload/package changes, updater divergence | Persistent client compromise or credential theft |
| Unofficial plugin/bootstrap modification | Modified application trust boundary | Third-party plugin ecosystem and updater conflicts | Full Discord client compromise |
| Symbol/pointer interception | In-process admitted component | Resolver/rebinding logic | Target-process control; ineffective for current direct calls |

## Required authorization model

An approved target must be authenticated as a tuple, not a pathname:

```text
live process identity + parent/exec generation
+ canonical executable vnode and code-signing identity
+ Team ID and required code-signing flags
+ mapped module vnode and validated Mach-O identity
+ compiled image variant
+ exactly one compiled patch definition
```

PID reuse is handled by binding the policy decision to the acquired task and
its process start/exec identity. Path spoofing is handled with opened vnode
identity plus signature validation, not string comparison. Symlinks, deleted
or replaced path components, writable ancestors, and mount changes must fail
closed. A same-Team identifier string alone is not proof; the kernel-validated
signature and designated requirement/CDHash must be checked.

## TOCTOU and substitution risks

- **Process substitution:** a PID can exit and be reused between discovery and
  action. Retain and revalidate the task/unique process identity.
- **Module substitution:** the path can name a different vnode after mapping.
  Bind the loaded region to its vnode/object and validate the in-memory Mach-O
  plus the opened file identity.
- **Update race:** Discord can replace versions while a renderer starts. Policy
  must select only a compiled variant whose live identity and exact bytes
  match; unknown updates are a no-op.
- **Address redirection:** never accept a target address from IPC or remote
  policy. Derive it from the verified load address and compiled variant.
- **Protection race:** re-query the region immediately before and after any
  later operation; abort on split/remap, identity, or protection changes.
- **Page provenance:** COW-capable is not the same as privately copied. The
  future mechanism must establish private ownership before copying bytes and
  must never temporarily alter the shared vnode page.

## Remote-policy limits

Remote policy may authenticate an application release and select among
already compiled `PatchDefinition` identifiers. It may never supply:

- search or context bytes;
- replacement bytes;
- code, shell commands, or payloads;
- absolute or relative unrestricted addresses;
- unbounded search regions;
- new path grammars or signing identities; or
- flags that weaken any mandatory verification.

Policies must be signed, versioned, expiry/rollback constrained, and bound to
the application/module identity. Replay of an older valid policy against a new
module must fail because the live identity and compiled definition do not
match. Revocation and key rotation must not create a “fail open” mode.

## Compromise analysis

### Compromised userspace helper

A helper holding task ports or patch authority is effectively a process
injector. IPC authentication does not contain a compromised helper itself.
The safest design gives it no caller-selected address/bytes, performs policy
and identity checks in a smaller privileged component, uses one-shot narrowly
typed requests, and drops authority after the target window. Even then, a
general task port exposes far more than the required predicate and is a strong
reason to reject candidate C.

### Compromised GitHub release or policy host

Repository/release compromise must not turn the updater into arbitrary write.
Independent signing keys, reproducible reviewed catalogue data, transparency
or pinned release metadata, rollback protection, and local compiled byte
definitions are required. A valid release of malicious executable code can
still compromise its granted authority; remote-policy byte restrictions do
not solve malicious-binary distribution.

### Compromised target process

Discord can race or deceive an in-process observer after compromise. The goal
is not to defend Discord from itself, but the component must still prevent the
process from redirecting privileged patch authority to other tasks, files, or
addresses. An in-process design with no privileged IPC has a smaller
cross-process blast radius than a generic helper.

## Candidate-specific findings

- **A:** narrowest authority and best timing, but native code inside Discord
  expands the renderer attack surface. Only official/same-Team admission is
  acceptable; accepting arbitrary locally signed companions by disabling LV
  is not.
- **B:** path-oriented targeting, kernel memory manipulation, raw payload
  injection, W+X, and persistent `CS_DEBUGGED`/enforcement changes make the
  blast radius unacceptable.
- **C:** PID/task-port and IPC TOCTOU plus general arbitrary-write capability
  are disproportionate; lack of a supported target grant also blocks it.
- **D:** environment and package/bootstrap modification are spoofable,
  update-fragile trust channels and do not overcome LV safely.
- **E:** an official extension would inherit Discord's supply-chain trust;
  unofficial client modifications add an unbounded third-party code surface.
- **F:** direct calls prevent pointer-only rebinding, so attempted fallback
  becomes executable-byte modification with the same private-page proof
  burden.

## Non-negotiable rejection rules

Reject any design that patches a shared writable mapping, writes a validation
callback page, edits and restores a vnode page, leaves RWX or a writable alias,
sets `CS_DEBUGGED`, clears enforcement or Library Validation, trusts only a
path/PID, accepts remote bytes/addresses, or cannot prove that fresh processes
and ordinary file reads remain original.
