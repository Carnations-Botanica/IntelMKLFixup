# Application rules

## Purpose

An `ApplicationRule` identifies an approved native-module family before any
MKL bytes are examined. It contains no search or replacement machine code.

Each compiled rule contains:

- stable application and path-rule identifiers;
- exact module basename;
- a bounded path grammar;
- exact signing identifier;
- explicit Team ID policy and value; and
- a compiled code-signing policy.

The generic matcher checks the path length and basename before calling the
application-specific grammar. Signing evidence is checked separately against
the selected rule. A short process name is never an authorisation signal.

## Discord Stable/Krisp

The initial rule is `discord-stable-krisp`. It accepts only Discord Stable's
numeric version/module layouts documented in `WHITELIST_DESIGN.md` and requires:

```text
basename:             discord_krisp.node
signing identifier:   discord_krisp
Team ID:              53Q6R32WPB
signing policy:       valid runtime, not ad-hoc
```

The numeric application and module revision components are metadata in the
path grammar, not fixed supported version numbers. Discord Canary, PTB,
renamed modules, non-numeric layouts, and copies elsewhere fail closed.

## Relationship to match modes

The same ApplicationRule may be associated with multiple explicit policies:

- the Discord 0.0.403 `StrictVariant`, which additionally requires its compiled
  CDHash and exact target offset; and
- the experimental Discord `BoundedWindow`, which omits version/CDHash and
  requires `-imklfxwindow` plus one unique exact match inside its compiled page.

Strict and window policies are independent. A strict policy never changes its
mode or starts searching. When both approve the same callback, the more
restrictive strict policy wins.

## Adding another application

Adding an application requires reviewed evidence for its path grammar,
basename, signing identifier, Team ID, signing flags, executable range, and
chosen match-mode metadata. Tests must cover accepted paths, near-miss paths,
wrong signing identity, wrong Team ID, and exact MKL bytes in a non-whitelisted
path.

The new application may reuse an existing `PatchDefinition`; doing so does not
modify the core matcher or callback. The current runtime catalogue is compiled,
so a new rule still requires a kext release.
