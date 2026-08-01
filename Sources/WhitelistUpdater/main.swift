import Foundation
import WhitelistCore

@main
struct WhitelistUpdaterCommand {
	static let pluginVersion = try! SemanticVersion("1.0.0")

	static func main() async {
		do {
			try await run(Array(CommandLine.arguments.dropFirst()))
		} catch {
			let message = (error as? LocalizedError)?.errorDescription ?? error.localizedDescription
			FileHandle.standardError.write(Data("imklfx-whitelist: error: \(message)\n".utf8))
			exit(1)
		}
	}

	static func run(_ arguments: [String]) async throws {
		guard let command = arguments.first else {
			throw WhitelistError.usage(usage)
		}
		let options = try Options(Array(arguments.dropFirst()))

		switch command {
			case "validate":
				guard let manifestPath = options.manifestPath, options.signaturePath == nil else {
					throw WhitelistError.usage("validate requires --manifest and does not accept --signature")
				}
				let data = try Data(contentsOf: URL(fileURLWithPath: manifestPath))
				let manifest = try ManifestValidator().validate(data, pluginVersion: pluginVersion)
				print("valid manifest version \(manifest.manifestVersion) with " +
					"\(manifest.applicationRules.count) application rule(s) and " +
					"\(manifest.imageVariants.count) image variant(s)")

			case "update", "check":
				let checkOnly = command == "check" || options.checkOnly
				try await update(options: options, checkOnly: checkOnly)

			case "status":
				try rejectUpdateOnlyOptions(options)
				let store = try makeStore(root: options.storeURL)
				let status = try store.status()
				print("current manifest version: \(status.current.manifest.manifestVersion)")
				print("current SHA-256: \(status.current.sha256)")
				print("previous manifest version: \(status.previousVersion.map(String.init) ?? "none")")
				print("highest accepted version: \(status.highestAcceptedVersion)")

			case "rollback":
				try rejectUpdateOnlyOptions(options)
				let report = try makeStore(root: options.storeURL).rollback()
				printDifference(report.difference)
				print("action: rolled back atomically to \(report.artifactIdentifier)")

			case "help", "--help", "-h":
				print(usage)

			default:
				throw WhitelistError.usage("unknown command \(command)\n\n\(usage)")
		}
	}

	static func update(options: Options, checkOnly: Bool) async throws {
		let trustRoot = try ReleaseTrust.trustRoot()
		let authenticator = ManifestAuthenticator(trustRoot: trustRoot)
		let raw: DownloadedReleaseArtifact
		if let manifestPath = options.manifestPath, let signaturePath = options.signaturePath {
			guard !options.includePrereleases else {
				throw WhitelistError.usage("--include-prereleases cannot be combined with offline files")
			}
			raw = DownloadedReleaseArtifact(
				manifestData: try Data(contentsOf: URL(fileURLWithPath: manifestPath)),
				signatureData: try Data(contentsOf: URL(fileURLWithPath: signaturePath)),
				releaseTag: "offline"
			)
		} else if options.manifestPath == nil && options.signaturePath == nil {
			raw = try await GitHubReleaseClient().downloadLatest(
				includePrereleases: options.includePrereleases
			)
		} else {
			throw WhitelistError.usage("--manifest and --signature must be provided together")
		}

		let artifact = try authenticator.authenticate(
			manifestData: raw.manifestData,
			signatureData: raw.signatureData,
			pluginVersion: pluginVersion
		)
		print("authenticated release: \(raw.releaseTag)")
		print("authenticated key id: \(trustRoot.keyID)")
		print("manifest SHA-256: \(artifact.sha256)")

		let store = ManifestStore(
			root: options.storeURL,
			authenticator: authenticator,
			pluginVersion: pluginVersion
		)
		let report = try checkOnly ? store.check(artifact) : store.install(artifact)
		printDifference(report.difference)
		if checkOnly {
			print("action: check only; no files changed")
		} else if report.installed {
			print("action: installed atomically as \(report.artifactIdentifier)")
		} else {
			print("action: already current; no files changed")
		}
	}

	static func makeStore(root: URL) throws -> ManifestStore {
		ManifestStore(
			root: root,
			authenticator: ManifestAuthenticator(trustRoot: try ReleaseTrust.trustRoot()),
			pluginVersion: pluginVersion
		)
	}

	static func rejectUpdateOnlyOptions(_ options: Options) throws {
		if options.manifestPath != nil || options.signaturePath != nil ||
			options.includePrereleases || options.checkOnly {
			throw WhitelistError.usage("this command accepts only --store")
		}
	}

	static func printDifference(_ difference: ManifestDifference) {
		print("manifest version: \(difference.fromVersion.map(String.init) ?? "none") -> \(difference.toVersion)")
		print("added rules: \(difference.added.isEmpty ? "none" : difference.added.joined(separator: ", "))")
		print("removed rules: \(difference.removed.isEmpty ? "none" : difference.removed.joined(separator: ", "))")
		print("changed rules: \(difference.changed.isEmpty ? "none" : difference.changed.joined(separator: ", "))")
		for record in difference.addedRecords {
			print("+ \(record)")
		}
		for record in difference.removedRecords {
			print("- \(record)")
		}
		for change in difference.changedRecords {
			print("- \(change.oldRecord)")
			print("+ \(change.newRecord)")
		}
	}

	static let usage = """
	Usage:
	  imklfx-whitelist check [--include-prereleases] [--store PATH]
	  imklfx-whitelist update [--check-only] [--include-prereleases] [--store PATH]
	  imklfx-whitelist check --manifest FILE --signature FILE [--store PATH]
	  imklfx-whitelist update --manifest FILE --signature FILE [--check-only] [--store PATH]
	  imklfx-whitelist status [--store PATH]
	  imklfx-whitelist rollback [--store PATH]
	  imklfx-whitelist validate --manifest FILE

	The default store is ~/Library/Application Support/IntelMKLFixup/whitelist.
	Checking and user-store installation do not require root.
	"""
}

struct Options {
	var checkOnly = false
	var includePrereleases = false
	var manifestPath: String?
	var signaturePath: String?
	var storeURL: URL

	init(_ arguments: [String]) throws {
		guard let applicationSupport = FileManager.default.urls(
			for: .applicationSupportDirectory,
			in: .userDomainMask
		).first else {
			throw WhitelistError.usage("could not determine the user Application Support directory")
		}
		storeURL = applicationSupport
			.appendingPathComponent("IntelMKLFixup", isDirectory: true)
			.appendingPathComponent("whitelist", isDirectory: true)

		var index = 0
		while index < arguments.count {
			switch arguments[index] {
				case "--check-only":
					checkOnly = true
				case "--include-prereleases":
					includePrereleases = true
				case "--manifest":
					index += 1
					guard index < arguments.count else { throw WhitelistError.usage("--manifest requires a path") }
					manifestPath = arguments[index]
				case "--signature":
					index += 1
					guard index < arguments.count else { throw WhitelistError.usage("--signature requires a path") }
					signaturePath = arguments[index]
				case "--store":
					index += 1
					guard index < arguments.count else { throw WhitelistError.usage("--store requires a path") }
					storeURL = URL(fileURLWithPath: arguments[index], isDirectory: true)
				default:
					throw WhitelistError.usage("unknown option \(arguments[index])")
			}
			index += 1
		}
	}
}
