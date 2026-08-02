# Private-mapping feasibility

## Determination

**Outcome 3: no practical safe architecture has been identified under the
current constraints.**

A technically coherent design exists only conditionally: a component already
trusted and admitted by Discord would register a dyld image-add callback in
the Krisp-owning Renderer before the native module loads, validate the mapped
image, and operate only on a proven private COW page. The controlled fixture
verifies the callback ordering and shows a normal executable dylib mapping as
RX/COW. It does not provide a way to admit that component to Discord.

The current Renderer is Hardened Runtime, lacks `get-task-allow`, and does not
disable Library Validation. No supported Discord extension point was found
that can load an independently signed native observer before Krisp. The
kernel-assisted and task-port alternatives examined require one or more of
the expressly prohibited security compromises.

## What is established

| Question | Result | Evidence strength |
|---|---|---|
| Krisp-owning process | Discord Helper (Renderer) | High: two existing incident reports contain both the process identity and module |
| Current target layout | universal offset `0x650100`; x86_64 image-relative address `0x64c100` | High: Mach-O parsing of the reviewed original |
| Target binding | exported symbol, but 16 internal direct calls and no indirect-symbol entry | High: symbols, exports trie, indirect table, and disassembly |
| General image callback order | add-image callback precedes the library constructor and its immediate predicate call | High: controlled fixture event log |
| Fixture executable sharing | RX, maximum RWX, `SM_COW`, not shared | High for the fixture only |
| Discord target sharing/COW | Not measured | Live work stopped because the still-loaded unsafe kext could mutate the file-visible page |
| In-process observer admission | Independently signed code is rejected by current Renderer policy | High for ordinary dylib/native-addon admission; no supported same-Team extension was found |
| External task access | Target has no `get-task-allow`; no safe guaranteed task-port/timing route found | High for normal supported debugging route |

## Why private post-map writing remains unproven

An RX file-backed mapping can be COW-capable without already being private.
`SM_COW` describes the mapping strategy, not proof that the particular target
page has been privately copied. Conversely, a path-bearing executable region
must never be treated as safe merely because it belongs to the target task.

The live Discord facts still required are:

- exact region boundaries at the current target address;
- current and maximum protections;
- `pri_share_mode`, object identity, and resident/private/shared page counts;
- whether other Discord processes map the same vnode/object;
- whether the target page is resident before Mic Test;
- whether the in-process bytes equal the reviewed original; and
- whether the restored file's identity and signature match the approved
  variant.

These observations can classify the initial mapping. They cannot, by
themselves, make a writer safe or solve observer admission.

## Mandatory proof before any future write

Any later implementation or experiment must fail closed unless all of the
following are established atomically or revalidated immediately before the
operation:

1. The task is the approved Renderer instance, with expected audit identity,
   executable path, signing identifier, Team ID, code-signing flags, and
   parent relationship.
2. The module is the approved vnode and Mach-O slice, with canonical path,
   architecture, UUID/CDHash or stronger configured identity, load commands,
   and ASLR slide validated from the mapped image rather than user-supplied
   addresses.
3. The target address is derived from the verified image and lands entirely
   within its executable region at the reviewed file offset.
4. The complete current function and context equal exactly one compiled
   `PatchDefinition`. Remote policy may select that definition but may not
   supply offsets, search bytes, replacement bytes, shell code, or addresses.
5. The original region is file-backed RX and COW-capable. A shared writable
   mapping is an unconditional rejection.
6. The write destination is demonstrably a private page owned by only the
   approved task before replacement bytes are copied. No method may write a
   vnode/UBC page and then restore it.
7. Any protection transition is scoped to the one private page, uses no RWX
   interval, and returns the page to RX on success and every error path. A
   platform-supported W^X mechanism would be required where Hardened Runtime
   enforces it.
8. Instruction-cache synchronization uses the supported architecture API
   (for example `sys_icache_invalidate`/the compiler cache-clear primitive),
   even on coherent x86 hardware.
9. A second process mapping the same module continues to expose the original
   bytes, and ordinary file reads continue to expose the original bytes.
10. File SHA-256, inode, size, mtime, and ctime are captured before and after
    and remain identical; vnode/UBC state is independently checked where a
    supported read-only observation exists.
11. The page is RX after the operation, no writable alias remains, and the
    change disappears with process exit and is absent in a fresh process.

The checks must be bound to open handles/task identities to limit path-swap,
PID-reuse, and process-exec TOCTOU. A successful preflight followed by an
unchecked delayed write is not sufficient.

## Architecture ranking

- **Safest in principle:** an official Discord/same-Team component loaded by
  the Renderer before Krisp. This has no current admission route available to
  this project.
- **Most practical under the current constraints:** none. External polling is
  too late, normal task-port access is unavailable, environment injection is
  blocked or unsupported, and Lilu payload injection violates the constraints.
- **Function interception:** not a substitute. The exported predicate is
  called directly inside the same image, bypassing import-pointer rebinding.

## Smallest safe next experiment

No reboot is needed to finish this report. The next live experiment must wait
until the user has disabled the unsafe kext by reboot and restored the Krisp
file through a trusted source. Then, without `sudo`, injection, task writes, or
protection changes:

1. verify the kext is absent from the loaded-kext list;
2. record file signature, hash, UUID, inode, timestamps, and original bytes;
3. launch Discord normally;
4. run `Tools/ReadOnlyKrispProbe/run.sh /restored/absolute/path/to/discord_krisp.node`
   once during early startup and once after Mic Test; and
5. capture `vmmap` and `lsof` snapshots for the identified Renderer.

This experiment can resolve the mapping table and residency questions. It
will not authorize or perform a patch. Any write experiment remains a
separate approval decision.
