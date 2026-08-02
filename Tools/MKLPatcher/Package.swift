// swift-tools-version: 6.0

import PackageDescription

let package = Package(
    name: "MKLPatcher",
    platforms: [
        .macOS(.v13),
    ],
    products: [
        .executable(name: "MKLPatcher", targets: ["MKLPatcher"]),
        .library(name: "MKLPatcherCore", targets: ["MKLPatcherCore"]),
    ],
    targets: [
        .target(name: "MKLPatcherCore"),
        .executableTarget(
            name: "MKLPatcher",
            dependencies: ["MKLPatcherCore"]
        ),
        .testTarget(
            name: "MKLPatcherCoreTests",
            dependencies: ["MKLPatcherCore"]
        ),
    ]
)

