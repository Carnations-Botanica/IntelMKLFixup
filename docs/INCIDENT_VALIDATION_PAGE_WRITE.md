# Validation-page write findings

Controlled testing on a Ryzen 9 3900X Hackintosh running Darwin 24/macOS
Sequoia showed that both the upstream kext and this fork modify the vnode/UBC
backed executable validation page. The changed bytes became visible through
ordinary reads of `discord_krisp.node`, although no conventional filesystem
write occurred and timestamps could remain unchanged.

## Fork observation

- Original SHA-256:
  `de061edb4387fc5bba2b8535483aa2f4347c17bc9e4d25babef36172c86a9f9a`
- Original bytes at `0x650100`: `53 48 83 ec 20 8b`
- Replacement: `b8 01 00 00 00 c3`
- Observed patched SHA-256:
  `95e611b3bd89d95d67f1e809eb1cbefcc8eedbba3bd5b70bf11cf044cf72b27d`

The log confirmed the Discord application, strict image variant, signing
identity, Team ID, CDHash, known patch definition, and offset before reporting
`modified=yes`. Discord Voice Test and Krisp worked in that controlled test.

## Upstream observation

The upstream replacement was observed as:

```text
55 48 89 e5 b8 01 00 00 00 5d c3
```

followed by its zero-filled replacement layout. The observed upstream-patched
SHA-256 was
`00136f3dcd94c8a4f1c0bb850b7d2cb8e7461599e61f72fbe9bcfc98d66ad0af`.
This establishes that file visibility follows from the inherited validation-page
architecture and was not introduced by the fork's later hardening.

The previous response blocked all writes while the behaviour was investigated.
Version 0.2.0 instead documents and accepts it, makes writing explicitly
acknowledged, and restricts active operation to the reviewed strict variant.
See [FILE_BACKED_PATCHING.md](FILE_BACKED_PATCHING.md) and
[RESEARCH_MEMORY_ONLY.md](RESEARCH_MEMORY_ONLY.md).
