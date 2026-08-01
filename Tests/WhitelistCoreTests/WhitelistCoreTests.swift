import CryptoKit
import Foundation
import XCTest
@testable import WhitelistCore

final class WhitelistCoreTests: XCTestCase {
	private let now = Date(timeIntervalSince1970: 1_785_542_400) // 2026-07-31T00:00:00Z
	private let pluginVersion = try! SemanticVersion("1.0.0")
	private let keyID = "imklfx-test-rfc8032"
	private let privateKey = Data(hex: "9d61b19deffd5a60ba844af492ec2cc4" +
		"4449c5697b326919703bac031cae7f60")!
	private let publicKey = Data(hex: "d75a980182b10ab7d54bfed3c964073a" +
		"0ee172f3daa62325af021a68f707511a")!

	func testValidManifestAndSignature() throws {
		let artifact = try makeArtifact(version: 1)
		XCTAssertEqual(artifact.manifest.schemaVersion, 3)
		XCTAssertEqual(artifact.manifest.manifestVersion, 1)
		XCTAssertEqual(artifact.manifest.applicationRules.count, 1)
		XCTAssertEqual(artifact.manifest.imageVariants.count, 2)
		XCTAssertEqual(artifact.manifest.imageVariants[0].allowedPatchDefinitionIDs,
			["mkl-serv-intel-cpu-true-oneapi-build-20201104-x86_64-v1"])
		XCTAssertEqual(artifact.manifest.imageVariants[1].matchMode, .boundedWindow)
		XCTAssertNil(artifact.manifest.imageVariants[1].applicationVersion)
		XCTAssertNil(artifact.manifest.imageVariants[1].cdhash)
		XCTAssertEqual(artifact.manifest.imageVariants[1].searchWindow?.start, 0x650000)
		XCTAssertEqual(artifact.manifest.imageVariants[0].matchMode, .strictVariant)
		XCTAssertEqual(artifact.manifest.imageVariants[0].targetFileOffset, 0x650100)
		XCTAssertNil(artifact.manifest.imageVariants[0].searchWindow)
		XCTAssertNil(artifact.manifest.imageVariants[1].targetFileOffset)
	}

	func testProductionTrustRootFailsClosedUntilConfigured() {
		XCTAssertThrowsError(try ReleaseTrust.trustRoot()) { error in
			guard case WhitelistError.unconfiguredTrustRoot = error else {
				return XCTFail("unexpected error: \(error)")
			}
		}
	}

	func testBooleanSignatureFormatVersionIsRejected() throws {
		let manifest = try manifestData(version: 1)
		let signature = try sign(manifest)
		var envelope = try JSONSerialization.jsonObject(with: signature) as! [String: Any]
		envelope["format_version"] = true
		XCTAssertThrowsError(try authenticator.authenticate(
			manifestData: manifest,
			signatureData: try encode(envelope),
			now: now,
			pluginVersion: pluginVersion
		)) { error in
			guard case WhitelistError.invalidSignature = error else {
				return XCTFail("unexpected error: \(error)")
			}
		}
	}

	func testUnknownManifestFieldIsRejectedEvenWhenSigned() throws {
		var object = manifestObject(version: 1)
		object["search_bytes"] = "b801000000c3"
		let data = try encode(object)
		XCTAssertThrowsError(try authenticate(data)) { error in
			guard case WhitelistError.invalidManifest = error else {
				return XCTFail("unexpected error: \(error)")
			}
		}
	}

	func testRemoteMachineCodeCannotEnterSchema() throws {
		var object = manifestObject(version: 1)
		var variants = object["image_variants"] as! [[String: Any]]
		variants[0]["replacement_bytes"] = "b801000000c3"
		object["image_variants"] = variants
		let data = try encode(object)
		XCTAssertThrowsError(try authenticate(data))
	}

	func testImageScanModeIsReservedAndRejected() throws {
		var object = manifestObject(version: 1)
		var variants = object["image_variants"] as! [[String: Any]]
		variants[0]["match_mode"] = "image_scan"
		variants[0]["target_file_offset"] = NSNull()
		object["image_variants"] = variants
		XCTAssertThrowsError(try authenticate(try encode(object))) { error in
			guard case WhitelistError.invalidManifest(let reason) = error,
				reason.contains("image_scan is reserved") else {
				return XCTFail("unexpected error: \(error)")
			}
		}
	}

	func testBoundedWindowMustBePageAlignedAndBounded() throws {
		var object = manifestObject(version: 1)
		var variants = object["image_variants"] as! [[String: Any]]
		variants[1]["search_window"] = ["start": 0x650001, "end": 0x651001]
		object["image_variants"] = variants
		XCTAssertThrowsError(try authenticate(try encode(object)))

		object = manifestObject(version: 1)
		variants = object["image_variants"] as! [[String: Any]]
		variants[1]["search_window"] = ["start": 0x650000, "end": 0x652000]
		object["image_variants"] = variants
		XCTAssertThrowsError(try authenticate(try encode(object)))
	}

	func testBoundedWindowRequiresNoFixedOffset() throws {
		var object = manifestObject(version: 1)
		var variants = object["image_variants"] as! [[String: Any]]
		variants[1]["target_file_offset"] = 0x650100
		object["image_variants"] = variants
		XCTAssertThrowsError(try authenticate(try encode(object)))
	}

	func testImageVariantMustReferenceAnApplicationRule() throws {
		var object = manifestObject(version: 1)
		var variants = object["image_variants"] as! [[String: Any]]
		variants[0]["application_rule_id"] = "unapproved-application"
		object["image_variants"] = variants
		XCTAssertThrowsError(try authenticate(try encode(object)))
	}

	func testTamperedManifestFailsSignatureBeforeSchemaAcceptance() throws {
		let original = try manifestData(version: 1)
		let signature = try sign(original)
		var tampered = original
		let index = tampered.startIndex + 10
		tampered[index] ^= 1
		XCTAssertThrowsError(try authenticator.authenticate(
			manifestData: tampered,
			signatureData: signature,
			now: now,
			pluginVersion: pluginVersion
		)) { error in
			guard case WhitelistError.invalidSignature = error else {
				return XCTFail("unexpected error: \(error)")
			}
		}
	}

	func testTamperedDetachedSignatureIsRejected() throws {
		let manifest = try manifestData(version: 1)
		let signature = try sign(manifest)
		var envelope = try JSONSerialization.jsonObject(with: signature) as! [String: Any]
		envelope["signature"] = Data(repeating: 0, count: 64).base64EncodedString()
		let tampered = try encode(envelope)
		XCTAssertThrowsError(try authenticator.authenticate(
			manifestData: manifest,
			signatureData: tampered,
			now: now,
			pluginVersion: pluginVersion
		)) { error in
			guard case WhitelistError.invalidSignature = error else {
				return XCTFail("unexpected error: \(error)")
			}
		}
	}

	func testWrongKeyIDAndPublicKeyAreRejected() throws {
		let manifest = try manifestData(version: 1)
		let signature = try sign(manifest)
		let wrongID = ManifestAuthenticator(
			trustRoot: TrustRoot(keyID: "different-key", publicKey: publicKey)
		)
		XCTAssertThrowsError(try wrongID.authenticate(
			manifestData: manifest, signatureData: signature,
			now: now, pluginVersion: pluginVersion
		))

		let wrongKey = ManifestAuthenticator(
			trustRoot: TrustRoot(keyID: keyID, publicKey: Data(repeating: 0xA5, count: 32))
		)
		XCTAssertThrowsError(try wrongKey.authenticate(
			manifestData: manifest, signatureData: signature,
			now: now, pluginVersion: pluginVersion
		))
	}

	func testUnknownMatchModeAndMalformedManifestAreRejected() throws {
		var object = manifestObject(version: 1)
		var variants = object["image_variants"] as! [[String: Any]]
		variants[0]["match_mode"] = "approximate_search"
		object["image_variants"] = variants
		XCTAssertThrowsError(try authenticate(try encode(object)))
		XCTAssertThrowsError(try authenticate(Data("not-json".utf8)))
	}

	func testExpiredManifestIsRejected() throws {
		var object = manifestObject(version: 1)
		object["generated_at"] = "2025-08-01T00:00:00Z"
		object["expires_at"] = "2026-07-30T00:00:00Z"
		let data = try encode(object)
		XCTAssertThrowsError(try authenticate(data)) { error in
			guard case WhitelistError.expiredManifest = error else {
				return XCTFail("unexpected error: \(error)")
			}
		}
	}

	func testDowngradeIsRejected() throws {
		try withStore { store in
			_ = try store.install(makeArtifact(version: 2), now: now)
			XCTAssertThrowsError(try store.install(makeArtifact(version: 1), now: now)) { error in
				guard case WhitelistError.downgrade(current: 2, proposed: 1) = error else {
					return XCTFail("unexpected error: \(error)")
				}
			}
		}
	}

	func testCheckOnlyDoesNotCreateStore() throws {
		let root = temporaryDirectory()
		defer { try? FileManager.default.removeItem(at: root) }
		let store = ManifestStore(root: root, authenticator: authenticator, pluginVersion: pluginVersion)
		let report = try store.check(makeArtifact(version: 1), now: now)
		XCTAssertFalse(report.installed)
		XCTAssertFalse(FileManager.default.fileExists(atPath: root.path))
	}

	func testInterruptedInstallLeavesCurrentStateUnchanged() throws {
		try withStore { store in
			_ = try store.install(makeArtifact(version: 1), now: now)
			XCTAssertThrowsError(try store.install(
				makeArtifact(version: 2),
				now: now,
				hooks: StoreHooks(beforeStateCommit: { throw InjectedFailure() })
			))
			let status = try store.status(now: now)
			XCTAssertEqual(status.current.manifest.manifestVersion, 1)
			XCTAssertNil(status.previousVersion)
		}
	}

	func testRollbackIsAtomicAndRetainsHighestVersion() throws {
		try withStore { store in
			_ = try store.install(makeArtifact(version: 1), now: now)
			_ = try store.install(makeArtifact(version: 2), now: now)

			XCTAssertThrowsError(try store.rollback(
				now: now,
				hooks: StoreHooks(beforeStateCommit: { throw InjectedFailure() })
			))
			var status = try store.status(now: now)
			XCTAssertEqual(status.current.manifest.manifestVersion, 2)
			XCTAssertEqual(status.previousVersion, 1)

			_ = try store.rollback(now: now)
			status = try store.status(now: now)
			XCTAssertEqual(status.current.manifest.manifestVersion, 1)
			XCTAssertEqual(status.previousVersion, 2)
			XCTAssertEqual(status.highestAcceptedVersion, 2)
		}
	}

	func testSameVersionDifferentAuthenticatedContentIsRejected() throws {
		try withStore { store in
			_ = try store.install(makeArtifact(version: 1), now: now)
			let conflicting = try makeArtifact(version: 1, applicationVersion: "0.0.404")
			XCTAssertThrowsError(try store.install(conflicting, now: now)) { error in
				guard case WhitelistError.sameVersionConflict(1) = error else {
					return XCTFail("unexpected error: \(error)")
				}
			}
		}
	}

	func testCorruptStateIsRejected() throws {
		try withStore { store in
			_ = try store.install(makeArtifact(version: 1), now: now)
			try Data("not-json\n".utf8).write(
				to: store.root.appendingPathComponent(ManifestStore.stateFileName),
				options: [.atomic]
			)
			XCTAssertThrowsError(try store.status(now: now)) { error in
				guard case WhitelistError.corruptStore = error else {
					return XCTFail("unexpected error: \(error)")
				}
			}
		}
	}

	func testStoreRejectsSymlinkedVersionsDirectory() throws {
		let root = temporaryDirectory()
		let destination = temporaryDirectory()
		defer {
			try? FileManager.default.removeItem(at: root)
			try? FileManager.default.removeItem(at: destination)
		}
		try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
		try FileManager.default.createDirectory(at: destination, withIntermediateDirectories: true)
		try FileManager.default.createSymbolicLink(
			at: root.appendingPathComponent(ManifestStore.versionsDirectoryName),
			withDestinationURL: destination
		)
		let store = ManifestStore(root: root, authenticator: authenticator, pluginVersion: pluginVersion)
		XCTAssertThrowsError(try store.install(makeArtifact(version: 1), now: now)) { error in
			guard case WhitelistError.corruptStore = error else {
				return XCTFail("unexpected error: \(error)")
			}
		}
		XCTAssertEqual(try FileManager.default.contentsOfDirectory(atPath: destination.path), [])
	}

	func testConcurrentInstallsAreSerialized() throws {
		let root = temporaryDirectory()
		defer { try? FileManager.default.removeItem(at: root) }
		let store = ManifestStore(root: root, authenticator: authenticator, pluginVersion: pluginVersion)
		let firstEnteredCommit = DispatchSemaphore(value: 0)
		let allowFirstCommit = DispatchSemaphore(value: 0)
		let group = DispatchGroup()
		let errors = ErrorCollector()

		group.enter()
		DispatchQueue.global().async {
			defer { group.leave() }
			do {
				_ = try store.install(
					self.makeArtifact(version: 1), now: self.now,
					hooks: StoreHooks(beforeStateCommit: {
						firstEnteredCommit.signal()
						allowFirstCommit.wait()
					})
				)
			} catch {
				errors.append(error)
			}
		}
		XCTAssertEqual(firstEnteredCommit.wait(timeout: .now() + 5), .success)

		group.enter()
		DispatchQueue.global().async {
			defer { group.leave() }
			do {
				_ = try store.install(self.makeArtifact(version: 2), now: self.now)
			} catch {
				errors.append(error)
			}
		}
		Thread.sleep(forTimeInterval: 0.1)
		allowFirstCommit.signal()
		XCTAssertEqual(group.wait(timeout: .now() + 10), .success)
		XCTAssertTrue(errors.values.isEmpty, "unexpected errors: \(errors.values)")
		XCTAssertEqual(try store.status(now: now).current.manifest.manifestVersion, 2)
	}

	func testMalformedDuplicateAndPrereleaseGitHubResponsesReject() async throws {
		var session = mockSession { request in
			XCTAssertTrue(request.url!.absoluteString.contains("/releases/latest"))
			return (200, Data("{".utf8))
		}
		await XCTAssertThrowsAsyncError(try await GitHubReleaseClient(session: session).downloadLatest())

		session.invalidateAndCancel()
		session = mockSession { _ in
			(200, try self.releaseData(prerelease: false, duplicateManifest: true))
		}
		await XCTAssertThrowsAsyncError(try await GitHubReleaseClient(session: session).downloadLatest())

		session.invalidateAndCancel()
		session = mockSession { _ in
			(200, try self.releaseData(prerelease: true))
		}
		await XCTAssertThrowsAsyncError(try await GitHubReleaseClient(session: session).downloadLatest())
		session.invalidateAndCancel()
	}

	func testPrereleaseOptInAndRequiredAssetDownloads() async throws {
		let manifest = try manifestData(version: 1)
		let signature = try sign(manifest)
		let session = mockSession { request in
			switch request.url!.path {
				case let path where path.hasSuffix("/releases"):
					let release = try JSONSerialization.jsonObject(
						with: self.releaseData(prerelease: true)
					)
					return (200, try self.encode([release]))
				case "/manifest": return (200, manifest)
				case "/signature": return (200, signature)
				default: throw URLError(.badURL)
			}
		}
		let result = try await GitHubReleaseClient(session: session)
			.downloadLatest(includePrereleases: true)
		XCTAssertEqual(result.releaseTag, "v-test")
		XCTAssertEqual(result.manifestData, manifest)
		XCTAssertEqual(result.signatureData, signature)
		session.invalidateAndCancel()
	}

	func testOversizedAndInterruptedDownloadsReject() async throws {
		var session = mockSession { request in
			if request.url!.path.contains("releases") {
				return (200, try self.releaseData(prerelease: false))
			}
			if request.url!.path == "/manifest" {
				return (200, Data(repeating: 0x41, count: ManifestValidator.maximumManifestSize + 1))
			}
			return (200, try self.sign(self.manifestData(version: 1)))
		}
		await XCTAssertThrowsAsyncError(try await GitHubReleaseClient(session: session).downloadLatest())

		session.invalidateAndCancel()
		session = mockSession { request in
			if request.url!.path.contains("releases") {
				return (200, try self.releaseData(prerelease: false))
			}
			throw URLError(.networkConnectionLost)
		}
		await XCTAssertThrowsAsyncError(try await GitHubReleaseClient(session: session).downloadLatest())
		session.invalidateAndCancel()
	}

	private var authenticator: ManifestAuthenticator {
		ManifestAuthenticator(trustRoot: TrustRoot(keyID: keyID, publicKey: publicKey))
	}

	private func makeArtifact(version: Int, applicationVersion: String = "0.0.403") throws -> SignedManifestArtifact {
		try authenticate(try manifestData(version: version, applicationVersion: applicationVersion))
	}

	private func authenticate(_ data: Data) throws -> SignedManifestArtifact {
		try authenticator.authenticate(
			manifestData: data,
			signatureData: sign(data),
			now: now,
			pluginVersion: pluginVersion
		)
	}

	private func sign(_ data: Data) throws -> Data {
		try ManifestSigner().sign(manifestData: data, privateKey: privateKey, keyID: keyID)
	}

	private func manifestData(version: Int, applicationVersion: String = "0.0.403") throws -> Data {
		try encode(manifestObject(version: version, applicationVersion: applicationVersion))
	}

	private func manifestObject(version: Int, applicationVersion: String = "0.0.403") -> [String: Any] {
		[
			"schema_version": 3,
			"manifest_version": version,
			"generated_at": "2026-07-30T00:00:00Z",
			"expires_at": "2027-07-30T00:00:00Z",
			"plugin_compatibility": [
				"minimum": "1.0.0",
				"maximum": "1.0.0"
			],
			"application_rules": [[
				"id": "discord-stable-krisp",
				"application_family": "discord-stable-krisp",
				"display_name": "Discord Stable Krisp native module",
				"path_rule_id": "discord-stable-krisp-path-v1",
				"basename": "discord_krisp.node",
				"signing_identifier": "discord_krisp",
				"team_identifier_policy": "exact",
				"team_identifier": "53Q6R32WPB",
				"signing_policy_id": "valid-runtime-no-adhoc-v1"
			]],
			"image_variants": [[
				"id": "discord-stable-\(applicationVersion)-krisp-x86_64-test",
				"application_rule_id": "discord-stable-krisp",
				"application_version": applicationVersion,
				"architecture": "x86_64",
				"cdhash": "585e9575a870db3f7de0e9d3c74fc9d7e7f084cd",
				"match_mode": "strict_variant",
				"target_file_offset": 0x650100,
				"executable_range": ["start": 0x4D00, "end": 0xCBCED0],
				"search_window": NSNull(),
				"allowed_patch_definition_ids": [
					"mkl-serv-intel-cpu-true-oneapi-build-20201104-x86_64-v1"
				]
			], [
				"id": "discord-stable-krisp-bounded-window-test",
				"application_rule_id": "discord-stable-krisp",
				"application_version": NSNull(),
				"architecture": "x86_64",
				"cdhash": NSNull(),
				"match_mode": "bounded_window",
				"target_file_offset": NSNull(),
				"executable_range": ["start": 0x4D00, "end": 0xCBCED0],
				"search_window": ["start": 0x650000, "end": 0x651000],
				"allowed_patch_definition_ids": [
					"mkl-serv-intel-cpu-true-oneapi-build-20201104-x86_64-v1"
				]
			]]
		]
	}

	private func encode(_ object: Any) throws -> Data {
		var data = try JSONSerialization.data(withJSONObject: object, options: [.prettyPrinted, .sortedKeys])
		data.append(0x0A)
		return data
	}

	private func releaseData(prerelease: Bool, duplicateManifest: Bool = false) throws -> Data {
		var assets: [[String: Any]] = [
			["name": GitHubReleaseClient.manifestAssetName,
			 "browser_download_url": "https://assets.example/manifest"],
			["name": GitHubReleaseClient.signatureAssetName,
			 "browser_download_url": "https://assets.example/signature"]
		]
		if duplicateManifest {
			assets.append(assets[0])
		}
		return try encode([
			"tag_name": "v-test",
			"draft": false,
			"prerelease": prerelease,
			"assets": assets
		])
	}

	private func mockSession(
		_ handler: @escaping (URLRequest) throws -> (Int, Data)
	) -> URLSession {
		MockURLProtocol.handler = handler
		let configuration = URLSessionConfiguration.ephemeral
		configuration.protocolClasses = [MockURLProtocol.self]
		return URLSession(configuration: configuration)
	}

	private func withStore(_ body: (ManifestStore) throws -> Void) throws {
		let root = temporaryDirectory()
		defer { try? FileManager.default.removeItem(at: root) }
		try body(ManifestStore(root: root, authenticator: authenticator, pluginVersion: pluginVersion))
	}

	private func temporaryDirectory() -> URL {
		FileManager.default.temporaryDirectory
			.appendingPathComponent("imklfx-whitelist-tests-\(UUID().uuidString)", isDirectory: true)
	}
}

private struct InjectedFailure: Error {}

private final class ErrorCollector: @unchecked Sendable {
	private let lock = NSLock()
	private var storage: [Error] = []

	func append(_ error: Error) {
		lock.lock()
		storage.append(error)
		lock.unlock()
	}

	var values: [Error] {
		lock.lock()
		defer { lock.unlock() }
		return storage
	}
}

private final class MockURLProtocol: URLProtocol {
	static var handler: ((URLRequest) throws -> (Int, Data))?

	override class func canInit(with request: URLRequest) -> Bool { true }
	override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }

	override func startLoading() {
		do {
			guard let handler = Self.handler else { throw URLError(.unknown) }
			let (status, data) = try handler(request)
			let response = HTTPURLResponse(
				url: request.url!, statusCode: status, httpVersion: "HTTP/1.1",
				headerFields: ["Content-Length": String(data.count)]
			)!
			client?.urlProtocol(self, didReceive: response, cacheStoragePolicy: .notAllowed)
			client?.urlProtocol(self, didLoad: data)
			client?.urlProtocolDidFinishLoading(self)
		} catch {
			client?.urlProtocol(self, didFailWithError: error)
		}
	}

	override func stopLoading() {}
}

private func XCTAssertThrowsAsyncError<T>(
	_ expression: @autoclosure () async throws -> T,
	file: StaticString = #filePath,
	line: UInt = #line
) async {
	do {
		_ = try await expression()
		XCTFail("expected expression to throw", file: file, line: line)
	} catch {}
}

private extension Data {
	init?(hex: String) {
		guard hex.count.isMultiple(of: 2) else { return nil }
		var bytes: [UInt8] = []
		bytes.reserveCapacity(hex.count / 2)
		var index = hex.startIndex
		while index < hex.endIndex {
			let next = hex.index(index, offsetBy: 2)
			guard let byte = UInt8(hex[index..<next], radix: 16) else { return nil }
			bytes.append(byte)
			index = next
		}
		self.init(bytes)
	}
}
