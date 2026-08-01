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

public enum ManifestTeamIdentifierPolicy: String, Equatable {
	case exact
	case absent
}

public enum ManifestMatchMode: String, Equatable {
	case strictVariant = "strict_variant"
	case reviewedSearch = "reviewed_search"
}

public struct ManifestApplicationRule: Equatable {
	public let id: String
	public let applicationFamily: String
	public let displayName: String
	public let pathRuleID: String
	public let basename: String
	public let signingIdentifier: String
	public let teamIdentifierPolicy: ManifestTeamIdentifierPolicy
	public let teamIdentifier: String?
	public let signingPolicyID: String
}

public struct ManifestExecutableRange: Equatable {
	public let start: UInt64
	public let end: UInt64
}

public struct ManifestImageVariant: Equatable {
	public let id: String
	public let applicationRuleID: String
	public let applicationVersion: String
	public let architecture: String
	public let cdhash: String
	public let matchMode: ManifestMatchMode
	public let targetFileOffset: UInt64?
	public let executableRange: ManifestExecutableRange
	public let allowedPatchDefinitionIDs: [String]
}

public struct WhitelistManifest: Equatable {
	public let schemaVersion: Int
	public let manifestVersion: Int
	public let generatedAt: Date
	public let expiresAt: Date
	public let minimumPluginVersion: SemanticVersion
	public let maximumPluginVersion: SemanticVersion
	public let applicationRules: [ManifestApplicationRule]
	public let imageVariants: [ManifestImageVariant]
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
	public let addedRecords: [String]
	public let removedRecords: [String]
	public let changedRecords: [RecordChange]

	public static func compare(_ old: WhitelistManifest?, _ new: WhitelistManifest) -> ManifestDifference {
		let oldRules = recordMap(old)
		let newRules = recordMap(new)
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
			addedRecords: added.compactMap { newRules[$0] },
			removedRecords: removed.compactMap { oldRules[$0] },
			changedRecords: changed.compactMap { id in
				guard let oldRule = oldRules[id], let newRule = newRules[id] else { return nil }
				return RecordChange(id: id, oldRecord: oldRule, newRecord: newRule)
			}
		)
	}

	private static func recordMap(_ manifest: WhitelistManifest?) -> [String: String] {
		guard let manifest else { return [:] }
		var records: [String: String] = [:]
		for rule in manifest.applicationRules {
			let key = "application_rule:\(rule.id)"
			records[key] = "\(key) {family=\(rule.applicationFamily), name=\(rule.displayName), " +
				"path_rule=\(rule.pathRuleID), basename=\(rule.basename), " +
				"signing_id=\(rule.signingIdentifier), team_policy=\(rule.teamIdentifierPolicy.rawValue), " +
				"team_id=\(rule.teamIdentifier ?? "null"), signing_policy=\(rule.signingPolicyID)}"
		}
		for variant in manifest.imageVariants {
			let key = "image_variant:\(variant.id)"
			let offset = variant.targetFileOffset.map(String.init) ?? "null"
			records[key] = "\(key) {application_rule=\(variant.applicationRuleID), " +
				"version=\(variant.applicationVersion), architecture=\(variant.architecture), " +
				"cdhash=\(variant.cdhash), mode=\(variant.matchMode.rawValue), " +
				"target_offset=\(offset), executable_range=\(variant.executableRange.start)..<" +
				"\(variant.executableRange.end), patches=\(variant.allowedPatchDefinitionIDs.joined(separator: ","))}"
		}
		return records
	}
}

public struct RecordChange: Equatable {
	public let id: String
	public let oldRecord: String
	public let newRecord: String
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
