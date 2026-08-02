# Third-party notices

IntelMKLFixup remains distributed under the licence in [`LICENSE`](LICENSE).
No third-party Swift package source is vendored by the userspace tools. They use
Apple's Foundation, CryptoKit, SwiftUI, and testing frameworks supplied by the
installed macOS/Swift SDK.

The signature tests use the public Ed25519 test key material from
[RFC 8032, section 7.1](https://www.rfc-editor.org/rfc/rfc8032#section-7.1).
It is test data only and is deliberately not the release trust root.

The CI workflow obtains the following build-only dependencies at exact commits:

- [Acidanthera Lilu 1.7.2 source](https://github.com/acidanthera/Lilu/tree/e4748cc081bf060302c7d3c44a643ce1d11b7e1d), whose upstream
  repository is BSD-3-Clause licensed. Its notices and bundled-component
  attributions remain in its pinned source checkout.
- [Acidanthera MacKernelSDK source](https://github.com/acidanthera/MacKernelSDK/tree/05094e5e88cec7caedbfb35e8449ed0db94bf95b), whose
  upstream [`LICENSE.txt`](https://github.com/acidanthera/MacKernelSDK/blob/05094e5e88cec7caedbfb35e8449ed0db94bf95b/LICENSE.txt)
  contains the applicable notices for its SDK-derived and contributed content.
- GitHub's [`actions/checkout`](https://github.com/actions/checkout/tree/11bd71901bbe5b1630ceea73d27597364c9af683),
  [`actions/upload-artifact`](https://github.com/actions/upload-artifact/tree/ea165f8d65b6e75b540449e92b4886f43607fa02),
  and [`actions/download-artifact`](https://github.com/actions/download-artifact/tree/d3f86a106a0bac45b974a628896c90dbdf5c8093),
  each used only by CI under its upstream MIT licence.

These checkouts are not committed to this repository. Release packaging keeps
this notice and the IntelMKLFixup licence beside the produced artefacts.
