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
		XCTAssertEqual(artifact.manifest.manifestVersion, 1)
		XCTAssertEqual(artifact.manifest.applicationRules.count, 1)
		XCTAssertEqual(artifact.manifest.imageVariants.count, 1)
		XCTAssertEqual(artifact.manifest.imageVariants[0].allowedPatchDefinitionIDs,
			["mkl-serv-intel-cpu-true-oneapi-build-20201104-x86_64-v1"])
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

	func testReviewedSearchModeIsRejectedWhileCompiledDisabled() throws {
		var object = manifestObject(version: 1)
		var variants = object["image_variants"] as! [[String: Any]]
		variants[0]["match_mode"] = "reviewed_search"
		variants[0]["target_file_offset"] = NSNull()
		object["image_variants"] = variants
		XCTAssertThrowsError(try authenticate(try encode(object))) { error in
			guard case WhitelistError.invalidManifest(let reason) = error,
				reason.contains("reviewed_search is compiled disabled") else {
				return XCTFail("unexpected error: \(error)")
			}
		}
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
			"schema_version": 2,
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
