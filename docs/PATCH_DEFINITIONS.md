# Patch definitions

The 0.2.0 catalogue contains one reviewed x86_64 MKL implementation:

`mkl-serv-intel-cpu-true-oneapi-build-20201104-x86_64-v1`

At file offset `0x650100` in the strict Discord variant, the original function
begins:

```text
53 48 83 ec 20 8b 35 61 0f 79 00 85 f6 7c 08 89
f0 48 83 c4 20 5b c3
```

The patch replaces only the first six bytes:

```text
b8 01 00 00 00 c3
```

The remaining original function bytes stay unchanged. Matching requires the
complete 23-byte original implementation plus exact 16-byte context before and
after it. The already-patched state requires the six-byte replacement followed
by the unchanged original tail and the same context. Partial patches and all
one-byte mutations are rejected.

The known upstream IntelMKLFixup layout is different and is recognised by the
Swift inspector only:

```text
55 48 89 e5 b8 01 00 00 00 5d c3
00 00 00 00 00 00 00 00 00 00 00 00
```

It is not produced or automatically restored by this fork.
