# Process-private memory-only research

The file-backed validation-page design is accepted for version 0.2.0, while a
truly process-private approach remains long-term research.

## Proven

- The current `_cs_validate_page` route is file-visible on the tested Darwin 24
  Ryzen 9 3900X Hackintosh.
- The original upstream Lilu-style page patch shares this limitation; the
  behaviour was not introduced by this fork's strict policy engine.
- Private/copy-on-write process mappings are the desired target for a true
  memory-only implementation.
- Discord/Krisp makes direct internal calls to the MKL predicate, so simple
  symbol interposition is unsuitable.
- The current application identity, image evidence, and MKL matching layers are
  reusable in future research.

## Unresolved

- Admission of a trusted observer into Discord's hardened process.
- Reliable timing before the first MKL gate call.
- Safe private/COW executable-page modification.
- Code-signing and Hardened Runtime behaviour after such a change.
- Whether a practical process-private solution exists without unacceptable
  security compromises.

Future work is intentionally described only at the architectural level:
observe mapping provenance, determine whether a safe private page can exist,
and reuse strict identity and byte evidence before any modification. This
project does not publish procedural injection, entitlement bypass, or
Hardened Runtime bypass instructions.
