import Darwin
import CoreFoundation
import Foundation

public struct StoreHooks {
	public var beforeStateCommit: (() throws -> Void)?

	public init(beforeStateCommit: (() throws -> Void)? = nil) {
		self.beforeStateCommit = beforeStateCommit
	}
}

private struct StoreState {
	let currentArtifact: String
	let previousArtifact: String?
	let highestAcceptedVersion: Int
	let highestAcceptedSHA256: String
}

public final class ManifestStore {
	public static let stateFileName = "state.json"
	public static let versionsDirectoryName = "versions"
	public static let lockFileName = ".update.lock"

	public let root: URL
	public let authenticator: ManifestAuthenticator
	public let pluginVersion: SemanticVersion
	private let fileManager: FileManager

	public init(
		root: URL,
		authenticator: ManifestAuthenticator,
		pluginVersion: SemanticVersion,
		fileManager: FileManager = .default
	) {
		self.root = root.standardizedFileURL
		self.authenticator = authenticator
		self.pluginVersion = pluginVersion
		self.fileManager = fileManager
	}

	public func status(now: Date = Date()) throws -> StoreStatus {
		guard let state = try loadState() else {
			throw WhitelistError.noInstalledManifest
		}
		let current = try loadArtifact(identifier: state.currentArtifact, now: now)
		let previousVersion: Int?
		if let previous = state.previousArtifact {
			previousVersion = try loadArtifact(
				identifier: previous,
				now: now,
				enforceTemporalValidity: false
			).manifest.manifestVersion
		} else {
			previousVersion = nil
		}
		return StoreStatus(
			current: current,
			previousVersion: previousVersion,
			highestAcceptedVersion: state.highestAcceptedVersion
		)
	}

	public func check(_ artifact: SignedManifestArtifact, now: Date = Date()) throws -> InstallReport {
		let state = try loadState()
		let current = try state.map {
			try loadArtifact(identifier: $0.currentArtifact, now: now, enforceTemporalValidity: false)
		}
		try enforceMonotonicity(artifact, state: state, current: current)
		return InstallReport(
			installed: false,
			difference: ManifestDifference.compare(current?.manifest, artifact.manifest),
			artifactIdentifier: artifactIdentifier(for: artifact)
		)
	}

	public func install(
		_ artifact: SignedManifestArtifact,
		now: Date = Date(),
		hooks: StoreHooks = StoreHooks()
	) throws -> InstallReport {
		try withExclusiveStoreLock {
			let state = try loadState()
			let current = try state.map {
				try loadArtifact(identifier: $0.currentArtifact, now: now, enforceTemporalValidity: false)
			}
			try enforceMonotonicity(artifact, state: state, current: current)

			let identifier = artifactIdentifier(for: artifact)
			let difference = ManifestDifference.compare(current?.manifest, artifact.manifest)
			if current?.sha256 == artifact.sha256 {
				return InstallReport(installed: false, difference: difference, artifactIdentifier: identifier)
			}

			try persistArtifact(artifact, identifier: identifier)
			try hooks.beforeStateCommit?()
			let advancesHighest = artifact.manifest.manifestVersion > (state?.highestAcceptedVersion ?? 0)
			let nextState = StoreState(
				currentArtifact: identifier,
				previousArtifact: state?.currentArtifact,
				highestAcceptedVersion: max(
					state?.highestAcceptedVersion ?? 0,
					artifact.manifest.manifestVersion
				),
				highestAcceptedSHA256: advancesHighest ? artifact.sha256 :
					(state?.highestAcceptedSHA256 ?? artifact.sha256)
			)
			try writeStateAtomically(nextState)
			return InstallReport(installed: true, difference: difference, artifactIdentifier: identifier)
		}
	}

	public func rollback(now: Date = Date(), hooks: StoreHooks = StoreHooks()) throws -> InstallReport {
		try withExclusiveStoreLock {
			guard let state = try loadState() else {
				throw WhitelistError.noInstalledManifest
			}
			guard let previousIdentifier = state.previousArtifact else {
				throw WhitelistError.noRollbackManifest
			}

			let current = try loadArtifact(
				identifier: state.currentArtifact,
				now: now,
				enforceTemporalValidity: false
			)
			// Rollback is an explicit downgrade exception, but the target must still
			// carry a valid signature, pass schema checks, remain compatible, and not
			// be expired.
			let previous = try loadArtifact(identifier: previousIdentifier, now: now)
			let difference = ManifestDifference.compare(current.manifest, previous.manifest)
			try hooks.beforeStateCommit?()
			try writeStateAtomically(StoreState(
				currentArtifact: previousIdentifier,
				previousArtifact: state.currentArtifact,
				highestAcceptedVersion: state.highestAcceptedVersion,
				highestAcceptedSHA256: state.highestAcceptedSHA256
			))
			return InstallReport(
				installed: true,
				difference: difference,
				artifactIdentifier: previousIdentifier
			)
		}
	}

	private func enforceMonotonicity(
		_ proposed: SignedManifestArtifact,
		state: StoreState?,
		current: SignedManifestArtifact?
	) throws {
		guard let state else { return }
		let proposedVersion = proposed.manifest.manifestVersion
		if proposedVersion < state.highestAcceptedVersion {
			throw WhitelistError.downgrade(
				current: state.highestAcceptedVersion,
				proposed: proposedVersion
			)
		}
		if proposedVersion == state.highestAcceptedVersion,
			proposed.sha256 != state.highestAcceptedSHA256 {
			throw WhitelistError.sameVersionConflict(proposedVersion)
		}
		if proposedVersion == current?.manifest.manifestVersion, proposed.sha256 != current?.sha256 {
			throw WhitelistError.sameVersionConflict(proposedVersion)
		}
	}

	private func artifactIdentifier(for artifact: SignedManifestArtifact) -> String {
		"v\(artifact.manifest.manifestVersion)-\(artifact.sha256)"
	}

	private func prepareDirectories() throws {
		try createOrValidateDirectory(root, kind: "store root")
		try createOrValidateDirectory(
			root.appendingPathComponent(Self.versionsDirectoryName, isDirectory: true),
			kind: "versions directory"
		)
	}

	private func createOrValidateDirectory(_ url: URL, kind: String) throws {
		if try nodeType(at: url) == nil {
			try fileManager.createDirectory(at: url, withIntermediateDirectories: true)
		}
		guard try nodeType(at: url) == mode_t(S_IFDIR) else {
			throw WhitelistError.corruptStore("\(kind) is not a real directory")
		}
		try fileManager.setAttributes([.posixPermissions: 0o700], ofItemAtPath: url.path)
	}

	private func withExclusiveStoreLock<T>(_ body: () throws -> T) throws -> T {
		try prepareDirectories()
		let lockURL = root.appendingPathComponent(Self.lockFileName)
		let descriptor = open(lockURL.path, O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0o600)
		guard descriptor >= 0 else {
			throw WhitelistError.corruptStore("could not open the updater lock without following links")
		}
		defer { close(descriptor) }

		var information = stat()
		guard fstat(descriptor, &information) == 0,
			(information.st_mode & mode_t(S_IFMT)) == mode_t(S_IFREG) else {
			throw WhitelistError.corruptStore("updater lock is not a regular file")
		}
		guard flock(descriptor, LOCK_EX) == 0 else {
			throw WhitelistError.corruptStore("could not acquire the updater lock")
		}
		defer { flock(descriptor, LOCK_UN) }
		return try body()
	}

	private func persistArtifact(_ artifact: SignedManifestArtifact, identifier: String) throws {
		let versions = root.appendingPathComponent(Self.versionsDirectoryName, isDirectory: true)
		let destination = versions.appendingPathComponent(identifier, isDirectory: true)
		if try nodeType(at: destination) != nil {
			guard try nodeType(at: destination) == mode_t(S_IFDIR) else {
				throw WhitelistError.corruptStore("artifact path is not a real directory")
			}
			let existing = try loadArtifact(identifier: identifier, enforceTemporalValidity: false)
			guard existing.sha256 == artifact.sha256,
				existing.signatureData == artifact.signatureData else {
				throw WhitelistError.corruptStore("existing artifact directory conflicts with authenticated data")
			}
			return
		}

		let staging = versions.appendingPathComponent(".staging-\(UUID().uuidString)", isDirectory: true)
		do {
			try fileManager.createDirectory(at: staging, withIntermediateDirectories: false)
			try artifact.manifestData.write(
				to: staging.appendingPathComponent("manifest.json"),
				options: [.atomic]
			)
			try artifact.signatureData.write(
				to: staging.appendingPathComponent("manifest.sig"),
				options: [.atomic]
			)
			try fileManager.setAttributes(
				[.posixPermissions: 0o700],
				ofItemAtPath: staging.path
			)
			try fileManager.setAttributes(
				[.posixPermissions: 0o600],
				ofItemAtPath: staging.appendingPathComponent("manifest.json").path
			)
			try fileManager.setAttributes(
				[.posixPermissions: 0o600],
				ofItemAtPath: staging.appendingPathComponent("manifest.sig").path
			)
			try fileManager.moveItem(at: staging, to: destination)
		} catch {
			try? fileManager.removeItem(at: staging)
			throw error
		}
	}

	private func loadArtifact(
		identifier: String,
		now: Date = Date(),
		enforceTemporalValidity: Bool = true
	) throws -> SignedManifestArtifact {
		guard isValidArtifactIdentifier(identifier) else {
			throw WhitelistError.corruptStore("artifact identifier is malformed")
		}
		let directory = root
			.appendingPathComponent(Self.versionsDirectoryName, isDirectory: true)
			.appendingPathComponent(identifier, isDirectory: true)
		guard try nodeType(at: directory) == mode_t(S_IFDIR) else {
			throw WhitelistError.corruptStore("artifact directory is missing or is a link")
		}
		let manifestURL = directory.appendingPathComponent("manifest.json")
		let signatureURL = directory.appendingPathComponent("manifest.sig")
		let manifestData = try boundedRead(
			manifestURL,
			maximumSize: ManifestValidator.maximumManifestSize,
			kind: "manifest"
		)
		let signatureData = try boundedRead(
			signatureURL,
			maximumSize: ManifestAuthenticator.maximumSignatureSize,
			kind: "signature"
		)
		let artifact = try authenticator.authenticate(
			manifestData: manifestData,
			signatureData: signatureData,
			now: now,
			pluginVersion: pluginVersion,
			enforceTemporalValidity: enforceTemporalValidity
		)
		guard artifactIdentifier(for: artifact) == identifier else {
			throw WhitelistError.corruptStore("artifact directory name does not match authenticated content")
		}
		return artifact
	}

	private func boundedRead(_ url: URL, maximumSize: Int, kind: String) throws -> Data {
		let descriptor = open(url.path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW)
		guard descriptor >= 0 else {
			throw WhitelistError.corruptStore("\(kind) file could not be opened safely")
		}
		defer { close(descriptor) }
		var information = stat()
		guard fstat(descriptor, &information) == 0,
			(information.st_mode & mode_t(S_IFMT)) == mode_t(S_IFREG),
			information.st_size > 0, information.st_size <= off_t(maximumSize) else {
			throw WhitelistError.corruptStore("\(kind) file size is invalid")
		}
		let handle = FileHandle(fileDescriptor: descriptor, closeOnDealloc: false)
		guard let data = try handle.read(upToCount: maximumSize + 1),
			data.count == Int(information.st_size) else {
			throw WhitelistError.corruptStore("\(kind) file changed while being read")
		}
		return data
	}

	private func loadState() throws -> StoreState? {
		let url = root.appendingPathComponent(Self.stateFileName)
		guard let type = try nodeType(at: url) else { return nil }
		guard type == mode_t(S_IFREG) else {
			throw WhitelistError.corruptStore("state path is not a regular file")
		}
		let data = try boundedRead(url, maximumSize: 4096, kind: "state")
		let object: Any
		do {
			object = try JSONSerialization.jsonObject(with: data, options: [])
		} catch {
			throw WhitelistError.corruptStore("state JSON parsing failed")
		}
		guard let dictionary = object as? [String: Any],
			Set(dictionary.keys) == [
				"format_version", "current_artifact", "previous_artifact",
				"highest_accepted_version", "highest_accepted_sha256"
			],
			let format = dictionary["format_version"] as? NSNumber,
			CFGetTypeID(format) != CFBooleanGetTypeID(), format.intValue == 1, format.doubleValue == 1,
			let current = dictionary["current_artifact"] as? String,
			isValidArtifactIdentifier(current),
			let highest = dictionary["highest_accepted_version"] as? NSNumber,
			CFGetTypeID(highest) != CFBooleanGetTypeID(),
			highest.doubleValue.rounded(.towardZero) == highest.doubleValue,
			highest.intValue >= 1,
			let highestSHA256 = dictionary["highest_accepted_sha256"] as? String,
			isValidSHA256(highestSHA256) else {
			throw WhitelistError.corruptStore("state fields are malformed")
		}
		let previous: String?
		if dictionary["previous_artifact"] is NSNull {
			previous = nil
		} else if let value = dictionary["previous_artifact"] as? String,
			isValidArtifactIdentifier(value) {
			previous = value
		} else {
			throw WhitelistError.corruptStore("previous artifact identifier is malformed")
		}
		return StoreState(
			currentArtifact: current,
			previousArtifact: previous,
			highestAcceptedVersion: highest.intValue,
			highestAcceptedSHA256: highestSHA256
		)
	}

	private func writeStateAtomically(_ state: StoreState) throws {
		try prepareDirectories()
		let object: [String: Any] = [
			"format_version": 1,
			"current_artifact": state.currentArtifact,
			"previous_artifact": state.previousArtifact ?? NSNull(),
			"highest_accepted_version": state.highestAcceptedVersion,
			"highest_accepted_sha256": state.highestAcceptedSHA256
		]
		var data = try JSONSerialization.data(withJSONObject: object, options: [.prettyPrinted, .sortedKeys])
		data.append(0x0A)
		let temporary = root.appendingPathComponent(".state-\(UUID().uuidString)")
		try data.write(to: temporary, options: [.atomic])
		try fileManager.setAttributes([.posixPermissions: 0o600], ofItemAtPath: temporary.path)
		let handle = try FileHandle(forWritingTo: temporary)
		try handle.synchronize()
		try handle.close()
		let destination = root.appendingPathComponent(Self.stateFileName)
		guard rename(temporary.path, destination.path) == 0 else {
			let code = errno
			try? fileManager.removeItem(at: temporary)
			throw WhitelistError.corruptStore("atomic state rename failed with errno \(code)")
		}
	}

	private func isValidArtifactIdentifier(_ value: String) -> Bool {
		value.range(of: "^v[1-9][0-9]*-[0-9a-f]{64}$", options: .regularExpression) != nil
	}

	private func isValidSHA256(_ value: String) -> Bool {
		value.range(of: "^[0-9a-f]{64}$", options: .regularExpression) != nil
	}

	private func nodeType(at url: URL) throws -> mode_t? {
		var information = stat()
		if lstat(url.path, &information) == 0 {
			return information.st_mode & mode_t(S_IFMT)
		}
		if errno == ENOENT {
			return nil
		}
		throw WhitelistError.corruptStore("could not inspect \(url.lastPathComponent)")
	}
}
