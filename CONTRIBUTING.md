# Contributing

Changes must preserve the fail-closed policy architecture. Application paths
belong in `ApplicationRule`, image evidence in `ImageVariant`, and reviewed MKL
implementations in `PatchDefinition`. Do not add generic process-wide scans,
masked replacements, network-supplied machine code, or an active
`BoundedWindow` path.

Every new strict variant needs provenance, exact signing evidence, CDHash,
target offset, full original function bytes, before/after context, and mutation
tests. Run the commands in [docs/TESTING.md](docs/TESTING.md), treat warnings as
errors, and document what is host-tested versus hardware-tested.

Never commit proprietary application binaries, EFI copies, signing material,
logs with personal paths, or generated build directories.
