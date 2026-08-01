// swift-tools-version: 5.9

import PackageDescription

let package = Package(
	name: "IntelMKLWhitelistTools",
	platforms: [
		.macOS(.v13)
	],
	products: [
		.library(name: "WhitelistCore", targets: ["WhitelistCore"]),
		.executable(name: "imklfx-whitelist", targets: ["WhitelistUpdater"]),
		.executable(name: "imklfx-whitelist-sign", targets: ["WhitelistSigner"])
	],
	targets: [
		.target(name: "WhitelistCore"),
		.executableTarget(
			name: "WhitelistUpdater",
			dependencies: ["WhitelistCore"]
		),
		.executableTarget(
			name: "WhitelistSigner",
			dependencies: ["WhitelistCore"]
		),
		.testTarget(
			name: "WhitelistCoreTests",
			dependencies: ["WhitelistCore"]
		)
	]
)

