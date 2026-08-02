# Building

The release candidate is built on Darwin 24 with Xcode 16.4 for `x86_64`.
Dependencies are pinned by commit:

- Lilu: `e4748cc081bf060302c7d3c44a643ce1d11b7e1d`
- MacKernelSDK: `05094e5e88cec7caedbfb35e8449ed0db94bf95b`

Check out MacKernelSDK at `MacKernelSDK/`, check out Lilu at
`.dependencies/Lilu/`, link the SDK into the Lilu checkout, build Lilu Debug,
and copy the resulting `Lilu.kext` to the repository root. These dependency
directories are ignored and never packaged.

Then build both configurations:

```sh
xcodebuild -jobs 1 -scheme IntelMKLFixup -configuration Debug \
  GCC_TREAT_WARNINGS_AS_ERRORS=YES
xcodebuild -jobs 1 -scheme IntelMKLFixup -configuration Release \
  GCC_TREAT_WARNINGS_AS_ERRORS=YES
swift build -c release -Xswiftc -warnings-as-errors -Xswiftc -gnone
swift build --package-path Tools/MKLPatcher -c release \
  -Xswiftc -warnings-as-errors
```

The Xcode project uses `ARCHS=x86_64`, bundle identifier
`com.richardhedges.IntelMKLFixup`, and version `0.2.0`. See
[docs/TESTING.md](docs/TESTING.md) for the full verification matrix.
