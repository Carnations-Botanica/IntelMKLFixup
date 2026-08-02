# Userspace whitelist metadata

The current kext never reads an external whitelist. Application rules, image
variants, and patch definitions are compiled into reviewed C++ source. The
retained Swift tooling validates and stores signed metadata for research and
release review only; installing a manifest does not change runtime eligibility.

The schema can name only compiled path-rule, signing-policy, and
patch-definition identifiers. It has no fields for search bytes, replacement
bytes, masks, arbitrary path expressions, scripts, URLs, or executable content.
Unknown fields are rejected. The kernel component contains no GitHub client,
JSON parser, filesystem manifest reader, or networking code.

The example manifest and formal schemas are in `whitelist/`. The validator also
checks size limits, exact keys, compatibility range, timestamps, referential
integrity, duplicate policy identities, executable bounds, offset/window
consistency, and membership in compiled identifier sets.

The detached signature format is Ed25519 over a domain-separated copy of the
exact manifest bytes. The production trust root remains deliberately
unconfigured, so network install and signing operations fail closed. Tests use
public RFC 8032 vectors only. No signing secret belongs in this repository.

The retained store uses versioned immutable directories and an atomic state
file with downgrade protection. This is userspace state only and is not a
runtime policy transport. Any future transport would require a separate design
review and must never broaden matching or supply machine code.
