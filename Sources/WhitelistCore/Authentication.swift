import CryptoKit
import CoreFoundation
import Foundation

public struct TrustRoot {
	public let keyID: String
	public let publicKey: Data

	public init(keyID: String, publicKey: Data) {
		self.keyID = keyID
		self.publicKey = publicKey
	}
}

public enum ReleaseTrust {
	// Replace these two values with the reviewed production Ed25519 public key
	// before publishing a release. Keeping this fail-closed placeholder prevents
	// a release from silently trusting test material.
	public static let keyID = "UNCONFIGURED"
	public static let publicKeyBase64 = ""

	public static func trustRoot() throws -> TrustRoot {
		guard keyID != "UNCONFIGURED",
			let key = Data(base64Encoded: publicKeyBase64), key.count == 32 else {
			throw WhitelistError.unconfiguredTrustRoot
		}
		return TrustRoot(keyID: keyID, publicKey: key)
	}
}

public struct SignatureEnvelope: Codable, Equatable {
	public let formatVersion: Int
	public let algorithm: String
	public let keyID: String
	public let manifestSHA256: String
	public let signature: String

	private enum CodingKeys: String, CodingKey {
		case formatVersion = "format_version"
		case algorithm
		case keyID = "key_id"
		case manifestSHA256 = "manifest_sha256"
		case signature
	}

	public init(formatVersion: Int, algorithm: String, keyID: String, manifestSHA256: String, signature: String) {
		self.formatVersion = formatVersion
		self.algorithm = algorithm
		self.keyID = keyID
		self.manifestSHA256 = manifestSHA256
		self.signature = signature
	}
}

public struct ManifestAuthenticator {
	public static let maximumSignatureSize = 4096
	public static let domainSeparator = Data("IntelMKLFixup whitelist manifest v1\n".utf8)

	public let trustRoot: TrustRoot
	public let validator: ManifestValidator

	public init(trustRoot: TrustRoot, validator: ManifestValidator = ManifestValidator()) {
		self.trustRoot = trustRoot
		self.validator = validator
	}

	public func authenticate(
		manifestData: Data,
		signatureData: Data,
		now: Date = Date(),
		pluginVersion: SemanticVersion,
		enforceTemporalValidity: Bool = true
	) throws -> SignedManifestArtifact {
		guard !manifestData.isEmpty, manifestData.count <= ManifestValidator.maximumManifestSize else {
			throw WhitelistError.invalidManifest("file size is outside the accepted bounds")
		}
		guard !signatureData.isEmpty, signatureData.count <= Self.maximumSignatureSize else {
			throw WhitelistError.invalidSignature("file size is outside the accepted bounds")
		}

		let envelope = try parseEnvelope(signatureData)
		guard envelope.keyID == trustRoot.keyID else {
			throw WhitelistError.invalidSignature("unknown key id")
		}
		guard trustRoot.publicKey.count == 32 else {
			throw WhitelistError.invalidSignature("trust root is malformed")
		}

		let digest = SHA256.hash(data: manifestData).hexString
		guard envelope.manifestSHA256 == digest else {
			throw WhitelistError.invalidSignature("manifest digest does not match envelope")
		}
		guard let signature = Data(base64Encoded: envelope.signature), signature.count == 64 else {
			throw WhitelistError.invalidSignature("Ed25519 signature must be 64 bytes")
		}

		let publicKey: Curve25519.Signing.PublicKey
		do {
			publicKey = try Curve25519.Signing.PublicKey(rawRepresentation: trustRoot.publicKey)
		} catch {
			throw WhitelistError.invalidSignature("Ed25519 public key is invalid")
		}
		var signedBytes = Self.domainSeparator
		signedBytes.append(manifestData)
		guard publicKey.isValidSignature(signature, for: signedBytes) else {
			throw WhitelistError.invalidSignature("Ed25519 verification failed")
		}

		// Schema parsing is intentionally after signature verification. The only
		// unauthenticated parser above handles the tiny, bounded signature envelope.
		let manifest = try validator.validate(
			manifestData,
			now: now,
			pluginVersion: pluginVersion,
			enforceTemporalValidity: enforceTemporalValidity
		)
		return SignedManifestArtifact(
			manifestData: manifestData,
			signatureData: signatureData,
			manifest: manifest,
			sha256: digest
		)
	}

	public func parseEnvelope(_ data: Data) throws -> SignatureEnvelope {
		let object: Any
		do {
			object = try JSONSerialization.jsonObject(with: data, options: [])
		} catch {
			throw WhitelistError.invalidSignature("JSON parsing failed")
		}
		guard let dictionary = object as? [String: Any] else {
			throw WhitelistError.invalidSignature("root must be an object")
		}
		let expected: Set<String> = [
			"format_version", "algorithm", "key_id", "manifest_sha256", "signature"
		]
		guard Set(dictionary.keys) == expected else {
			throw WhitelistError.invalidSignature("envelope contains missing or unknown fields")
		}
		guard let format = dictionary["format_version"] as? NSNumber,
			CFGetTypeID(format) != CFBooleanGetTypeID(),
			format.intValue == 1, format.doubleValue == 1,
			let algorithm = dictionary["algorithm"] as? String, algorithm == "ed25519",
			let keyID = dictionary["key_id"] as? String,
			keyID.range(of: "^[a-z0-9][a-z0-9._-]{0,63}$", options: .regularExpression) != nil,
			let digest = dictionary["manifest_sha256"] as? String,
			digest.range(of: "^[0-9a-f]{64}$", options: .regularExpression) != nil,
			let signature = dictionary["signature"] as? String,
			signature.utf8.count <= 128 else {
			throw WhitelistError.invalidSignature("envelope fields are malformed")
		}
		return SignatureEnvelope(
			formatVersion: 1,
			algorithm: algorithm,
			keyID: keyID,
			manifestSHA256: digest,
			signature: signature
		)
	}
}

public struct ManifestSigner {
	public init() {}

	public func generateKeyPair() -> (privateKey: Data, publicKey: Data) {
		let key = Curve25519.Signing.PrivateKey()
		return (key.rawRepresentation, key.publicKey.rawRepresentation)
	}

	public func sign(manifestData: Data, privateKey: Data, keyID: String) throws -> Data {
		guard privateKey.count == 32 else {
			throw WhitelistError.invalidSignature("Ed25519 private key seed must be 32 bytes")
		}
		guard keyID.range(of: "^[a-z0-9][a-z0-9._-]{0,63}$", options: .regularExpression) != nil else {
			throw WhitelistError.invalidSignature("key id is malformed")
		}
		let signingKey: Curve25519.Signing.PrivateKey
		do {
			signingKey = try Curve25519.Signing.PrivateKey(rawRepresentation: privateKey)
		} catch {
			throw WhitelistError.invalidSignature("Ed25519 private key is invalid")
		}
		var signedBytes = ManifestAuthenticator.domainSeparator
		signedBytes.append(manifestData)
		let signature = try signingKey.signature(for: signedBytes)
		let digest = SHA256.hash(data: manifestData).hexString
		let envelope = SignatureEnvelope(
			formatVersion: 1,
			algorithm: "ed25519",
			keyID: keyID,
			manifestSHA256: digest,
			signature: signature.base64EncodedString()
		)
		let encoder = JSONEncoder()
		encoder.outputFormatting = [.prettyPrinted, .sortedKeys, .withoutEscapingSlashes]
		var output = try encoder.encode(envelope)
		output.append(0x0A)
		return output
	}

	public func publicKey(for privateKey: Data) throws -> Data {
		guard privateKey.count == 32 else {
			throw WhitelistError.invalidSignature("Ed25519 private key seed must be 32 bytes")
		}
		return try Curve25519.Signing.PrivateKey(rawRepresentation: privateKey).publicKey.rawRepresentation
	}
}

extension Digest {
	var hexString: String {
		map { String(format: "%02x", $0) }.joined()
	}
}
