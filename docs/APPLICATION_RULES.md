# Application rules

The active catalogue approves Discord Stable's `discord_krisp.node` only. The
path must match this grammar exactly:

```text
/Users/<account>/Library/Application Support/discord/
app-<decimal>.<decimal>.<decimal>/modules/discord_krisp-<decimal>/
[discord_krisp/]discord_krisp.node
```

Canary, PTB, alternative roots, malformed versions, extra path components,
wrong basenames, dot accounts, and paths longer than the compiled maximum are
rejected.

The `ApplicationRule` also requires signing identifier `discord_krisp`, Team ID
`53Q6R32WPB`, a valid Hardened Runtime code signature, and no ad-hoc signature.
The strict `ImageVariant` additionally requires the compiled 20-byte CDHash and
target offset `0x650100`. Matching a path alone never authorises a patch.

Application-specific rules live in `IntelMKLFixupCatalogue.hpp`; the generic
engine contains no Discord conditionals.
