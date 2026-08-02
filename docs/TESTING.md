# Testing

Host tests validate policy and byte-matching behaviour; they do not prove that
undocumented kernel behaviour is safe.

## Host checks

```sh
clang++ -std=c++14 -Wall -Wextra -Werror -pedantic \
  Tests/PolicyTests/PolicyTests.cpp -o /tmp/imklfx-policy-tests
/tmp/imklfx-policy-tests
clang++ -std=c++14 -Wall -Wextra -Werror -pedantic \
  Tests/PolicyTests/CatalogueTests.cpp -o /tmp/imklfx-catalogue-tests
/tmp/imklfx-catalogue-tests
sh Tests/check_file_backed_write_scope.sh
swift test -Xswiftc -warnings-as-errors
swift run imklfx-whitelist validate --manifest whitelist/manifest.json
swift test --package-path Tools/MKLPatcher -Xswiftc -warnings-as-errors
plutil -lint IntelMKLFixup/Info.plist Tools/MKLPatcher/Info.plist
git diff --check
```

The C++ suite covers valid strict identity; wrong path, basename, signing ID,
Team ID, CDHash, and callback offset; non-whitelisted applications; exact bytes;
every byte/context mutation; truncation and bounds; already/partially patched
states; replacement length; unknown definitions; strict/no-fallback behaviour;
and all acknowledgement, dry-run, off, strict, and bounded operating decisions.

The static architecture check asserts that the validation-page write primitive
appears once, in the acknowledged strict function, with the required boot gate
and logs. Swift manifest tests reject unknown fields such as machine-code search
or replacement bytes. Kernel-facing sources are checked for networking
primitives.

The Swift MKL inspector uses synthetic Mach-O fixtures to test no Mach-O,
symbol absent, recognised original, project-patched, recognised upstream, and
unknown implementation states without copying or reading Discord binaries.

## Build checks

Clean Debug and Release kext builds use the pinned dependencies in
[BUILDING.md](../BUILDING.md), warnings as errors, x86_64 architecture, plist
lint, kernel-facing syntax compilation, Xcode static analysis, and meaningful
host sanitizer builds. Record unsupported checks honestly rather than treating
them as kernel runtime evidence.
