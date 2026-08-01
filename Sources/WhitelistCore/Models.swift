import Foundation

public enum WhitelistError: Error, LocalizedError {
	case invalidManifest(String)
	case invalidSignature(String)
	case incompatibleManifest(String)
	case expiredManifest
	case futureManifest
	case unconfiguredTrustRoot
	case downgrade(current: Int, proposed: Int)
	case sameVersionConflict(Int)
	case noInstalledManifest
	case noRollbackManifest
	case corruptStore(String)
	case network(String)
	case usage(String)

	public var errorDescription: String? {
		switch self {
			case .invalidManifest(let reason):
				return "invalid manifest: \(reason)"
			case .invalidSignature(let reason):
				return "invalid signature: \(reason)"
			case .incompatibleManifest(let reason):
				return "incompatible manifest: \(reason)"
			case .expiredManifest:
				return "manifest is expired"
			case .futureManifest:
				return "manifest generation time is too far in the future"
			case .unconfiguredTrustRoot:
				return "release public key is not configured; refusing signed-manifest operations"
			case .downgrade(let current, let proposed):
				return "downgrade rejected: highest accepted version is \(current), proposed version is \(proposed)"
			case .sameVersionConflict(let version):
				return "manifest version \(version) has different authenticated content"
			case .noInstalledManifest:
				return "no manifest is installed"
			case .noRollbackManifest:
				return "no previous known-good manifest is available"
			case .corruptStore(let reason):
				return "manifest store is corrupt: \(reason)"
			case .network(let reason):
				return "network update failed: \(reason)"
			case .usage(let reason):
				return reason
		}
	}
}

public struct SemanticVersion: Comparable, Equatable, CustomStringConvertible {
	public let major: Int
	public let minor: Int
	public let patch: Int

	public init(_ string: String) throws {
		let components = string.split(separator: ".", omittingEmptySubsequences: false)
		guard components.count == 3 else {
			throw WhitelistError.invalidManifest("version must contain three decimal components")
		}
		let values = try components.map { component -> Int in
			guard !component.isEmpty,
				component.allSatisfy({ $0.isASCII && $0.isNumber }),
				(component == "0" || component.first != "0"),
				let value = Int(component), value <= 65_535 else {
				throw WhitelistError.invalidManifest("version component is malformed")
			}
			return value
		}
		major = values[0]
		minor = values[1]
		patch = values[2]
	}

	public var description: String { "\(major).\(minor).\(patch)" }

	public static func < (lhs: SemanticVersion, rhs: SemanticVersion) -> Bool {
		if lhs.major != rhs.major { return lhs.major < rhs.major }
		if lhs.minor != rhs.minor { return lhs.minor < rhs.minor }
		return lhs.patch < rhs.patch
	}
}

public struct ManifestRule: Equatable {
	public let id: String
	public let applicationFamily: String
	public let architecture: String
	public let pathRuleID: String
	public let targetProfileID: String
	public let patchDefinitionID: String
	public let signingIdentifier: String
	public let teamIdentifier: String?
	public let cdhash: String
	public let applicationVersion: String
}

public struct WhitelistManifest: Equatable {
	public let schemaVersion: Int
	public let manifestVersion: Int
	public let generatedAt: Date
	public let expiresAt: Date
	public let minimumPluginVersion: SemanticVersion
	public let maximumPluginVersion: SemanticVersion
	public let rules: [ManifestRule]
}

public struct SignedManifestArtifact {
	public let manifestData: Data
	public let signatureData: Data
	public let manifest: WhitelistManifest
	public let sha256: String

	public init(manifestData: Data, signatureData: Data, manifest: WhitelistManifest, sha256: String) {
		self.manifestData = manifestData
		self.signatureData = signatureData
		self.manifest = manifest
		self.sha256 = sha256
	}
}

public struct ManifestDifference: Equatable {
	public let fromVersion: Int?
	public let toVersion: Int
	public let added: [String]
	public let removed: [String]
	public let changed: [String]
	public let addedRules: [ManifestRule]
	public let removedRules: [ManifestRule]
	public let changedRules: [RuleChange]

	public static func compare(_ old: WhitelistManifest?, _ new: WhitelistManifest) -> ManifestDifference {
		let oldRules = Dictionary(uniqueKeysWithValues: (old?.rules ?? []).map { ($0.id, $0) })
		let newRules = Dictionary(uniqueKeysWithValues: new.rules.map { ($0.id, $0) })
		let oldIDs = Set(oldRules.keys)
		let newIDs = Set(newRules.keys)
		let added = newIDs.subtracting(oldIDs).sorted()
		let removed = oldIDs.subtracting(newIDs).sorted()
		let changed = oldIDs.intersection(newIDs).filter { oldRules[$0] != newRules[$0] }.sorted()
		return ManifestDifference(
			fromVersion: old?.manifestVersion,
			toVersion: new.manifestVersion,
			added: added,
			removed: removed,
			changed: changed,
			addedRules: added.compactMap { newRules[$0] },
			removedRules: removed.compactMap { oldRules[$0] },
			changedRules: changed.compactMap { id in
				guard let oldRule = oldRules[id], let newRule = newRules[id] else { return nil }
				return RuleChange(id: id, oldRule: oldRule, newRule: newRule)
			}
		)
	}
}

public struct RuleChange: Equatable {
	public let id: String
	public let oldRule: ManifestRule
	public let newRule: ManifestRule
}

public struct InstallReport {
	public let installed: Bool
	public let difference: ManifestDifference
	public let artifactIdentifier: String
}

public struct StoreStatus {
	public let current: SignedManifestArtifact
	public let previousVersion: Int?
	public let highestAcceptedVersion: Int
}
