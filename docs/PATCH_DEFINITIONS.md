# Patch definitions

## Purpose

A `PatchDefinition` describes one reviewed Intel MKL implementation. It is
application-independent and contains:

- stable MKL-oriented identifier and name;
- target architecture;
- exact function bytes;
- exact before/after instruction context;
- reviewed replacement bytes;
- validation policy and known MKL generation; and
- human-readable validation requirements.

Application paths, signing identities, Team IDs, CDHashes, versions, offsets,
and consumer names do not belong in a patch definition.

## Current definition

```text
mkl-serv-intel-cpu-true-oneapi-build-20201104-x86_64-v1
```

This definition describes the reviewed Intel oneAPI MKL build `20201104`
implementation of `_mkl_serv_intel_cpu_true`:

```text
53 48 83 EC 20 8B 35 61 0F 79 00 85 F6 7C 08
89 F0 48 83 C4 20 5B C3
```

It requires the complete compiled 16-byte context on each side. The
replacement is:

```text
B8 01 00 00 00 C3
```

which is `mov eax, 1; ret` on x86_64. Only those six bytes are written. The
remaining original function bytes are used when recognising the already-
patched form and are not overwritten.

## Matching rules

The current engine accepts only exact bytes and exact context. Search and
replacement masks are structurally reserved but must be null and empty. A
replacement longer than the verified function is invalid.

StrictVariant evaluates allowed definitions only at its fixed offset.
BoundedWindow evaluates allowed definitions at every context-safe position in
one approved page and accepts exactly one complete match in that page.

## Adding another MKL implementation

Unknown or approximate implementations are skipped. Supporting a new one
requires:

1. independently obtaining and disassembling the implementation;
2. selecting sufficient exact surrounding instruction context;
3. proving the replacement ABI and return semantics;
4. adding a separately named compiled definition;
5. mutation, truncation, ambiguity, and already-patched tests; and
6. a reviewed kext release.

A signed application manifest can select only identifiers already compiled into
the release. It cannot supply search bytes, context, masks, or replacements.
