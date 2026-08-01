import CoreFoundation
import Foundation

public enum CompiledWhitelistBindings {
	public static let applicationFamilies: Set<String> = [
		"discord-stable-krisp"
	]

	public static let architectures: Set<String> = [
		"x86_64"
	]

	public static let pathRuleIDs: Set<String> = [
		"discord-stable-krisp-path-v1"
	]

	public static let targetProfileIDs: Set<String> = [
		"discord-krisp-x86_64-offset-650100-v1"
	]

	public static let patchDefinitionIDs: Set<String> = [
		"mkl-serv-intel-cpu-true-x86_64-discord-v1"
	]
}

public struct ManifestValidator {
	public static let maximumManifestSize = 256 * 1024
	public static let maximumRules = 128
	public static let maximumLifetime: TimeInterval = 370 * 24 * 60 * 60
	public static let maximumFutureSkew: TimeInterval = 5 * 60

	public init() {}

	public func validate(
		_ data: Data,
		now: Date = Date(),
		pluginVersion: SemanticVersion,
		enforceTemporalValidity: Bool = true
	) throws -> WhitelistManifest {
		guard !data.isEmpty, data.count <= Self.maximumManifestSize else {
			throw WhitelistError.invalidManifest("file size is outside the accepted bounds")
		}

		let object: Any
		do {
			object = try JSONSerialization.jsonObject(with: data, options: [])
		} catch {
			throw WhitelistError.invalidManifest("JSON parsing failed")
		}
		guard let root = object as? [String: Any] else {
			throw WhitelistError.invalidManifest("root must be an object")
		}

		try requireExactKeys(
			root,
			allowed: [
				"schema_version", "manifest_version", "generated_at", "expires_at",
				"plugin_compatibility", "rules"
			],
			context: "root"
		)

		let schemaVersion = try requireInteger(root["schema_version"], name: "schema_version", range: 1...1)
		let manifestVersion = try requireInteger(
			root["manifest_version"], name: "manifest_version", range: 1...Int.max
		)
		let generatedAt = try requireDate(root["generated_at"], name: "generated_at")
		let expiresAt = try requireDate(root["expires_at"], name: "expires_at")
		guard expiresAt > generatedAt else {
			throw WhitelistError.invalidManifest("expires_at must be later than generated_at")
		}
		guard expiresAt.timeIntervalSince(generatedAt) <= Self.maximumLifetime else {
			throw WhitelistError.invalidManifest("manifest lifetime exceeds 370 days")
		}
		if enforceTemporalValidity {
			guard generatedAt.timeIntervalSince(now) <= Self.maximumFutureSkew else {
				throw WhitelistError.futureManifest
			}
			guard expiresAt > now else {
				throw WhitelistError.expiredManifest
			}
		}

		guard let compatibility = root["plugin_compatibility"] as? [String: Any] else {
			throw WhitelistError.invalidManifest("plugin_compatibility must be an object")
		}
		try requireExactKeys(
			compatibility,
			allowed: ["minimum", "maximum"],
			context: "plugin_compatibility"
		)
		let minimumVersion = try SemanticVersion(
			try requireString(compatibility["minimum"], name: "plugin_compatibility.minimum", maximumUTF8Length: 32)
		)
		let maximumVersion = try SemanticVersion(
			try requireString(compatibility["maximum"], name: "plugin_compatibility.maximum", maximumUTF8Length: 32)
		)
		guard minimumVersion <= maximumVersion else {
			throw WhitelistError.invalidManifest("plugin compatibility range is reversed")
		}
		guard pluginVersion >= minimumVersion && pluginVersion <= maximumVersion else {
			throw WhitelistError.incompatibleManifest(
				"plugin \(pluginVersion) is outside \(minimumVersion)...\(maximumVersion)"
			)
		}

		guard let rawRules = root["rules"] as? [Any], !rawRules.isEmpty,
			rawRules.count <= Self.maximumRules else {
			throw WhitelistError.invalidManifest("rules must contain 1...\(Self.maximumRules) entries")
		}

		var rules: [ManifestRule] = []
		var ruleIDs = Set<String>()
		var contentIdentities = Set<String>()
		for (index, rawRule) in rawRules.enumerated() {
			guard let rule = rawRule as? [String: Any] else {
				throw WhitelistError.invalidManifest("rules[\(index)] must be an object")
			}
			let parsed = try validateRule(rule, index: index)
			guard ruleIDs.insert(parsed.id).inserted else {
				throw WhitelistError.invalidManifest("duplicate rule id \(parsed.id)")
			}
			let identityKey = "\(parsed.architecture):\(parsed.cdhash)"
			guard contentIdentities.insert(identityKey).inserted else {
				throw WhitelistError.invalidManifest("duplicate architecture/CDHash identity")
			}
			rules.append(parsed)
		}

		return WhitelistManifest(
			schemaVersion: schemaVersion,
			manifestVersion: manifestVersion,
			generatedAt: generatedAt,
			expiresAt: expiresAt,
			minimumPluginVersion: minimumVersion,
			maximumPluginVersion: maximumVersion,
			rules: rules
		)
	}

	private func validateRule(_ rule: [String: Any], index: Int) throws -> ManifestRule {
		let prefix = "rules[\(index)]"
		try requireExactKeys(
			rule,
			allowed: [
				"id", "application_family", "application_version", "architecture",
				"path_rule_id", "target_profile_id", "patch_definition_id",
				"signing_identifier", "team_identifier", "cdhash"
			],
			context: prefix
		)

		let id = try requirePatternString(
			rule["id"], name: "\(prefix).id", pattern: "^[a-z0-9][a-z0-9._-]{0,95}$", maximumUTF8Length: 96
		)
		let applicationFamily = try requireMember(
			rule["application_family"], name: "\(prefix).application_family",
			allowed: CompiledWhitelistBindings.applicationFamilies
		)
		let applicationVersion = try requirePatternString(
			rule["application_version"], name: "\(prefix).application_version",
			pattern: "^[0-9]+\\.[0-9]+\\.[0-9]+$", maximumUTF8Length: 32
		)
		let architecture = try requireMember(
			rule["architecture"], name: "\(prefix).architecture",
			allowed: CompiledWhitelistBindings.architectures
		)
		let pathRuleID = try requireMember(
			rule["path_rule_id"], name: "\(prefix).path_rule_id",
			allowed: CompiledWhitelistBindings.pathRuleIDs
		)
		let targetProfileID = try requireMember(
			rule["target_profile_id"], name: "\(prefix).target_profile_id",
			allowed: CompiledWhitelistBindings.targetProfileIDs
		)
		let patchDefinitionID = try requireMember(
			rule["patch_definition_id"], name: "\(prefix).patch_definition_id",
			allowed: CompiledWhitelistBindings.patchDefinitionIDs
		)
		let signingIdentifier = try requirePatternString(
			rule["signing_identifier"], name: "\(prefix).signing_identifier",
			pattern: "^[A-Za-z0-9._-]{1,128}$", maximumUTF8Length: 128
		)
		let teamIdentifier: String?
		if rule["team_identifier"] is NSNull {
			teamIdentifier = nil
		} else {
			teamIdentifier = try requirePatternString(
				rule["team_identifier"], name: "\(prefix).team_identifier",
				pattern: "^[A-Z0-9]{10}$", maximumUTF8Length: 10
			)
		}
		let cdhash = try requirePatternString(
			rule["cdhash"], name: "\(prefix).cdhash",
			pattern: "^[0-9a-f]{40}$", maximumUTF8Length: 40
		)

		return ManifestRule(
			id: id,
			applicationFamily: applicationFamily,
			architecture: architecture,
			pathRuleID: pathRuleID,
			targetProfileID: targetProfileID,
			patchDefinitionID: patchDefinitionID,
			signingIdentifier: signingIdentifier,
			teamIdentifier: teamIdentifier,
			cdhash: cdhash,
			applicationVersion: applicationVersion
		)
	}

	private func requireExactKeys(_ dictionary: [String: Any], allowed: Set<String>, context: String) throws {
		let actual = Set(dictionary.keys)
		guard actual == allowed else {
			let missing = allowed.subtracting(actual).sorted()
			let unknown = actual.subtracting(allowed).sorted()
			throw WhitelistError.invalidManifest(
				"\(context) keys differ; missing=\(missing) unknown=\(unknown)"
			)
		}
	}

	private func requireInteger(_ value: Any?, name: String, range: ClosedRange<Int>) throws -> Int {
		guard let number = value as? NSNumber,
			CFGetTypeID(number) != CFBooleanGetTypeID(),
			number.doubleValue.rounded(.towardZero) == number.doubleValue,
			number.doubleValue >= Double(Int.min), number.doubleValue <= Double(Int.max) else {
			throw WhitelistError.invalidManifest("\(name) must be an integer")
		}
		let integer = number.intValue
		guard range.contains(integer) else {
			throw WhitelistError.invalidManifest("\(name) is outside its accepted range")
		}
		return integer
	}

	private func requireString(_ value: Any?, name: String, maximumUTF8Length: Int) throws -> String {
		guard let string = value as? String, !string.isEmpty,
			string.utf8.count <= maximumUTF8Length else {
			throw WhitelistError.invalidManifest("\(name) must be a bounded non-empty string")
		}
		return string
	}

	private func requirePatternString(
		_ value: Any?, name: String, pattern: String, maximumUTF8Length: Int
	) throws -> String {
		let string = try requireString(value, name: name, maximumUTF8Length: maximumUTF8Length)
		let range = NSRange(string.startIndex..<string.endIndex, in: string)
		let expression = try NSRegularExpression(pattern: pattern)
		guard expression.firstMatch(in: string, options: [], range: range)?.range == range else {
			throw WhitelistError.invalidManifest("\(name) has an invalid format")
		}
		return string
	}

	private func requireMember(_ value: Any?, name: String, allowed: Set<String>) throws -> String {
		let string = try requireString(value, name: name, maximumUTF8Length: 128)
		guard allowed.contains(string) else {
			throw WhitelistError.invalidManifest("\(name) is not compiled into this updater")
		}
		return string
	}

	private func requireDate(_ value: Any?, name: String) throws -> Date {
		let string = try requireString(value, name: name, maximumUTF8Length: 40)
		let fractional = ISO8601DateFormatter()
		fractional.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
		let wholeSeconds = ISO8601DateFormatter()
		wholeSeconds.formatOptions = [.withInternetDateTime]
		guard let date = fractional.date(from: string) ?? wholeSeconds.date(from: string) else {
			throw WhitelistError.invalidManifest("\(name) must be an ISO-8601 date-time")
		}
		return date
	}
}
