# Signed whitelist updates

## Safety boundary

IntelMKLFixup does not read an external whitelist in the kernel. The routed
`_cs_validate_page` callback is a page-validation path with no safe,
deterministic facility for opening files, allocating a JSON object graph,
waiting for userspace, or contacting a network service. Loading policy there
would also make boot and application launch depend on mutable filesystem state.

The Phase 5 boundary is therefore:

1. the kext retains a reviewed whitelist compiled into its source;
2. a userspace tool downloads or accepts a manifest and detached signature;
3. the tool authenticates the exact manifest bytes, then validates a strict
   schema, compatibility range, expiry, and monotonic version;
4. an accepted manifest is retained in a user-owned, versioned store for review
   and future source builds; and
5. a reviewed kext release compiles eligible identity records into typed C++
   data. The running kext never parses the manifest.

Installing a userspace manifest does **not** change the active kext and does not
make an application eligible at runtime. That limitation is intentional. A
future build-time generator may consume authenticated identity fields, but it
must map only to path rules, target profiles, and patch definitions already
compiled into reviewed source. It must not generate machine code or offsets
from remotely supplied values.

There is no kernel networking, GitHub API client, JSON parser, manifest file
reader, or arbitrary NVRAM policy blob.

## Manifest format

The formal schemas are:

- [`whitelist/manifest.schema.json`](../whitelist/manifest.schema.json)
- [`whitelist/signature.schema.json`](../whitelist/signature.schema.json)

The shipped example is [`whitelist/manifest.json`](../whitelist/manifest.json).
The Swift validator implements the schema plus additional semantic constraints
such as the 370-day maximum lifetime, duplicate identity rejection, exact
plugin compatibility, and compiled-identifier membership.

Each rule may contain only:

- a stable rule ID;
- the application family and observed application version;
- `x86_64` architecture;
- a precompiled path-rule ID;
- a precompiled target-profile ID;
- a precompiled patch-definition ID;
- exact signing identifier;
- exact Team ID or an explicit `null` absent-Team policy; and
- exact lowercase 20-byte CDHash.

The schema has no fields for search bytes, replacement bytes, masks, machine
code, file offsets, arbitrary paths, scripts, URLs, or executable content.
Unknown fields are rejected even when the file has a valid signature.

Manifest versions are positive, monotonically increasing integers. Reusing a
version with different authenticated bytes is rejected. A valid manifest also
contains generation and expiry times and an inclusive plugin-version range.

## Signature format and trust root

The detached signature is a small JSON envelope containing:

- format version `1`;
- algorithm `ed25519`;
- a bounded key ID;
- SHA-256 of the exact manifest file bytes; and
- a base64-encoded 64-byte Ed25519 signature.

SHA-256 is diagnostic and detects accidental mismatch; it is not treated as
authenticity. Authenticity comes from Ed25519 verification with the public key
compiled into `ReleaseTrust` in
`Sources/WhitelistCore/Authentication.swift`. The signed message is the exact
byte sequence:

```text
IntelMKLFixup whitelist manifest v1\n || manifest-file-bytes
```

The fixed prefix provides domain separation. Schema parsing happens only after
the signature succeeds. The tiny signature envelope is size-bounded and parsed
strictly before verification.

The source currently contains an `UNCONFIGURED` production trust root. This is
a deliberate release blocker: remote check, install, status, rollback, and
release signing fail closed until the maintainer performs a key ceremony and
commits the public key. Tests use the published RFC 8032 test vector only; that
test key is not reachable through `ReleaseTrust` and cannot authorize a
release.

## Initial key ceremony

Perform this once on a trusted offline or tightly controlled macOS system. Do
not generate the key inside the repository directory.

Build the signing tool from reviewed source:

```sh
swift build -c release --product imklfx-whitelist-sign
```

Generate an Ed25519 key seed, selecting a stable key ID and an explicit private
output path. The tool refuses to overwrite an existing file and writes it with
mode `0600`:

```sh
umask 077
.build/release/imklfx-whitelist-sign generate-key \
  --key-id imklfx-release-2026-01 \
  --private-output /secure/offline/path/imklfx-release-2026-01.key
```

Commit only the printed key ID and public-key base64 value into `ReleaseTrust`.
Never commit the `.key` file. Store an offline backup, then add the private seed
and non-secret key ID to GitHub without printing the seed:

```sh
gh secret set IMKLFX_SIGNING_PRIVATE_KEY_BASE64 \
  < /secure/offline/path/imklfx-release-2026-01.key
gh variable set IMKLFX_SIGNING_KEY_ID --body imklfx-release-2026-01
```

The Actions signer derives the public key from the secret and refuses to sign
unless it matches the public key embedded in the source being released.

Key rotation requires a reviewed updater release that trusts the new public key
before manifests are signed only by the new private key. Do not replace the key
and publish a new-key-only manifest in one step for clients that possess only
the old trust root.

## Building and using the updater

Build and test without third-party Swift packages:

```sh
swift test
swift build -c release --product imklfx-whitelist
```

The default store is:

```text
~/Library/Application Support/IntelMKLFixup/whitelist
```

Checking and installing there require no root privileges.

Check the latest stable release without changing files:

```sh
.build/release/imklfx-whitelist check
```

The equivalent explicit check-only update is:

```sh
.build/release/imklfx-whitelist update --check-only
```

Install an authenticated newer stable manifest:

```sh
.build/release/imklfx-whitelist update
```

Prereleases are ignored unless explicitly requested:

```sh
.build/release/imklfx-whitelist check --include-prereleases
```

Verify an offline manifest/signature pair without changing the store:

```sh
.build/release/imklfx-whitelist check \
  --manifest /path/to/whitelist-manifest.json \
  --signature /path/to/whitelist-manifest.json.sig
```

Install that offline pair after reviewing the printed changes:

```sh
.build/release/imklfx-whitelist update \
  --manifest /path/to/whitelist-manifest.json \
  --signature /path/to/whitelist-manifest.json.sig
```

Inspect state or explicitly roll back to the previous still-valid signed
manifest:

```sh
.build/release/imklfx-whitelist status
.build/release/imklfx-whitelist rollback
```

Every check and update prints the authenticated release tag or `offline`, key
ID, manifest SHA-256, version transition, sorted added, removed, and changed
rule IDs, and the complete identity record on each side of every change.
Downloaded scripts and binaries are never executed.

## Atomic storage and rollback

Authenticated artefacts are stored in immutable directories named from the
manifest version and SHA-256. `state.json` records the current and previous
artefact plus the highest accepted version and its authenticated digest.

An update writes a fresh staging directory on the same filesystem, completes
the manifest and signature files, renames the directory into the version store,
then atomically renames a small state file. Failure before the final state rename
leaves the current selection unchanged. The previous directory remains intact.

Rollback performs the same atomic state replacement. It is the only explicit
downgrade operation, does not lower the recorded highest accepted version, and
still rejects an expired, malformed, incompatible, or incorrectly signed
target. A later normal update cannot silently replay a version below the
highest accepted version.

The user-owned state file is not a hardware monotonic counter. A local attacker
who can delete or replace the entire store can cause replay of an older but
still-valid signed manifest on the next fresh install. The signature prevents
creation of arbitrary rules, while expiry limits replay duration. Stronger
anti-rollback would require a separately audited Keychain or secure-hardware
state design.

## Publishing a manifest

For every identity update:

1. obtain the original application image from known provenance;
2. review its code signature, CDHash, MKL signature/context, target profile, and
   application path rule;
3. update the manifest with a new version and bounded lifetime;
4. run `swift test` and the validator;
5. review the exact added, removed, and changed IDs;
6. publish a GitHub Release; and
7. let Actions sign and attach `whitelist-manifest.json` and
   `whitelist-manifest.json.sig` using the repository secret.

Do not attach a manually unsigned replacement asset under the expected name.
The updater rejects it, and a checksum file cannot make it authentic.

## GitHub Actions security and build information

`.github/workflows/main.yml` pins `actions/checkout`, `actions/upload-artifact`,
`actions/download-artifact`, Lilu 1.7.2 source, and MacKernelSDK source to exact
commits. It does not execute a remotely fetched `curl | eval` bootstrap. The
workflow:

- checks whitespace and the kext property list;
- runs the Swift tests and strict manifest validator;
- builds the pinned Lilu SDK from source;
- runs Xcode static analysis for Debug and Release;
- builds both kext configurations and the userspace updater;
- packages the manifest and formal schemas;
- signs only for a published GitHub Release;
- generates `SHA256SUMS`; and
- records source/dependency commits, source timestamp, Xcode, Swift, and Clang
  versions in `BUILD_INFO.txt`.

The Xcode kext and zip outputs are not claimed to be bit-for-bit reproducible
across Xcode or SDK releases. Pinned source inputs, `ZERO_AR_DATE`, a source
commit timestamp, and build metadata make differences attributable as far as
the current toolchain permits.

The userspace package vendors no third-party Swift source. Build-only
dependencies, test-vector provenance, and upstream licences are recorded in
[`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md), which the release
workflow places beside the produced artefacts.
