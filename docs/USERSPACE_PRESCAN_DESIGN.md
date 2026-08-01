# Userspace-assisted ImageScan design

## Status

This is a design only. No ImageScan scanner, policy generator, kernel transport,
or runtime consumer is implemented. The current manifest validator rejects
`image_scan`.

The objective is to establish exactly one reviewed MKL implementation across
all approved executable sections of one whitelisted x86_64 Mach-O slice—an
assurance that `_cs_validate_page` cannot provide by itself.

## Userspace scanner

A future scanner should operate on an explicitly selected file and:

1. open it without executing or loading it;
2. select the x86_64 slice with checked fat/Mach-O arithmetic;
3. validate its static code signature and extract signing identifier, Team ID,
   CDHash, architecture, file size, and Mach-O UUID;
4. enumerate only executable sections from validated load commands;
5. use patch definitions compiled into the scanner release—never definitions
   supplied by a manifest or server;
6. count complete exact function-plus-context matches across those sections;
7. reject zero, multiple, truncated, masked, or unknown matches;
8. record the containing validation page and exact candidate file offset; and
9. produce a canonical, bounded policy record without executable content.

The scanner must not trust path alone. The resulting record should bind:

- application-rule and compiled path-rule identifiers;
- signing identifier and Team ID;
- architecture and exact CDHash;
- full-file SHA-256 for userspace verification;
- Mach-O UUID, slice offset/size, and file size;
- executable section ranges inspected;
- compiled PatchDefinition identifier;
- exact candidate and containing-page file offsets;
- policy format/version, plugin compatibility, generation, expiry, and a
  monotonically increasing release sequence.

CDHash binds the kernel policy to the signed executable code XNU actually
validates. Full-file SHA-256 and UUID add userspace provenance and diagnostic
binding; they must not replace the kernel CDHash comparison unless the kernel
can safely obtain the same complete content identity.

## Authentication and replay resistance

The canonical record should be signed with the release Ed25519 key after schema
validation. The kext or build process must accept only known key IDs and exact
format versions. Signature verification must precede policy parsing.

The userspace store can enforce monotonic versions, expiry, atomic installation,
and rollback to a retained authenticated record. Binding the record to CDHash,
architecture, slice, UUID, and size prevents reuse against a different binary.

Persistent anti-rollback inside the kext remains unresolved. A local attacker
who can replace the complete EFI/configuration and its policy can also attempt
to restore an older still-signed record unless a protected monotonic counter or
equivalent trust anchor is introduced. A future design must state whether its
threat model includes such an EFI-level attacker; signatures alone prevent
tampering but do not provide freshness.

## Policy contents

Remote or generated policy may select only PatchDefinition identifiers already
compiled into the matching kext release. It may carry bounded application and
image identity plus reviewed offsets/ranges. It must not carry search bytes,
context bytes, masks, replacement bytes, kernel addresses, scripts, URLs, or
code.

Kernel parsing must use a fixed binary format with explicit byte order,
fixed-capacity arrays, checked lengths, and a small absolute size limit. JSON
and networking remain userspace-only.

## Transport comparison

### 1. Generate source and rebuild the kext

The scanner emits a reviewed C++ policy header containing identity and offset
metadata. Existing compiled PatchDefinitions are referenced by stable ID. CI
rebuilds the kext and publishes it with signed release metadata.

Advantages:

- uses the project's verified compile-time catalogue mechanism;
- no kernel parser, crypto implementation, mutable policy, or handoff race;
- policy is immutable for the boot; and
- easiest source review and rollback.

Limitations:

- every regenerated policy requires a new kext build and release;
- distribution authenticity still depends on users verifying the signed
  release and artefact hashes; and
- a reboot is required to load the new kext through OpenCore.

This is the recommended first ImageScan transport because it is the only option
already supported by the project's verified runtime architecture.

### 2. OpenCore/device-property injection

OpenCore officially supports adding device properties to PCI device paths and
injecting kernel extensions. Its configuration and vault mechanisms are
documented in the [OpenCore reference manual](https://github.com/acidanthera/OpenCorePkg/blob/master/Docs/Configuration.tex).

IntelMKLFixup is not currently an IOService attached to a stable PCI device, and
the project has not verified a fixed-capacity property carrier, retrieval API,
or lifecycle suitable for this policy. OpenCore configuration presence would
not authenticate the record to the kext; the record would still need its own
signature verification and binary binding.

This approach would require a reboot because OpenCore supplies the property at
boot. It is not recommended until a specific carrier, size limit, IORegistry
location, authentication implementation, and failure behaviour are proven with
the exact supported OpenCore release. No such API is claimed here.

### 3. Authenticated userspace-to-kernel handoff

An IOKit service/user-client could theoretically copy a small signed policy
record into fixed-capacity kernel storage. The kext would verify the signature,
schema, compatibility, expiry, and binary binding, then freeze the record before
allowing matching callbacks to consume it.

Advantages:

- no reboot strictly required after policy generation; and
- userspace can perform complete Mach-O and signature inspection with supported
  frameworks.

Risks and unresolved work:

- new IOService/IOUserClient attack surface and input validation;
- client authorization and signature-verification implementation;
- locking and immutable-publication correctness;
- policy arrival races with pages validated before handoff;
- teardown and sleep/wake lifecycle;
- inability to retroactively patch an already validated candidate page; and
- persistent rollback protection.

Lilu provides patching infrastructure but no verified turnkey authenticated
policy channel for this plugin. This option is not recommended until separately
threat-modelled and prototyped. Operationally, a reboot may still be required to
guarantee the policy is available before any target image is validated.

## Recommended sequence

1. Keep BoundedWindow for controlled one-page experiments.
2. Implement the host-only scanner and canonical signed record with no kernel
   consumer.
3. Validate exact image-wide uniqueness and reproducibility on fixtures.
4. Initially generate a compiled policy header and rebuild the kext.
5. Consider a runtime transport only after its authentication, timing, and
   rollback model passes a separate audit.
