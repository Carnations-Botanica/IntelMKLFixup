import CoreFoundation
import Foundation

public enum CompiledWhitelistBindings {
	public static let architectures: Set<String> = [
		"x86_64"
	]

	// Path grammars are executable policy and remain compiled into a reviewed
	// release. The manifest may select one, but cannot upload a regex or parser.
	public static let pathRuleIDs: Set<String> = [
		"discord-stable-krisp-path-v1"
	]

	public static let signingPolicyIDs: Set<String> = [
		"valid-runtime-no-adhoc-v1"
	]

	public static let patchDefinitionIDs: Set<String> = [
		"mkl-serv-intel-cpu-true-oneapi-build-20201104-x86_64-v1"
	]

	// ImageScan is reserved for a future authenticated userspace pre-scan
	// transport. BoundedWindow is implemented in the compiled runtime policy.
	public static let imageScanEnabled = false
}

public struct ManifestValidator {
	public static let maximumManifestSize = 256 * 1024
	public static let maximumApplicationRules = 128
	public static let maximumImageVariants = 256
	public static let maximumPatchesPerVariant = 8
	public static let maximumReviewedFileOffset: UInt64 = UInt64(UInt32.max)
	public static let x8664ValidationPageSize: UInt64 = 4096
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
				"plugin_compatibility", "application_rules", "image_variants"
			],
			context: "root"
		)

		let schemaVersion = try requireInteger(root["schema_version"], name: "schema_version", range: 3...3)
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

		let applicationRules = try validateApplicationRules(root["application_rules"])
		let applicationIDs = Set(applicationRules.map(\.id))
		let imageVariants = try validateImageVariants(root["image_variants"], applicationIDs: applicationIDs)
		let usedApplications = Set(imageVariants.map(\.applicationRuleID))
		guard usedApplications == applicationIDs else {
			let unused = applicationIDs.subtracting(usedApplications).sorted()
			throw WhitelistError.invalidManifest("application rules without an image variant: \(unused)")
		}

		return WhitelistManifest(
			schemaVersion: schemaVersion,
			manifestVersion: manifestVersion,
			generatedAt: generatedAt,
			expiresAt: expiresAt,
			minimumPluginVersion: minimumVersion,
			maximumPluginVersion: maximumVersion,
			applicationRules: applicationRules,
			imageVariants: imageVariants
		)
	}

	private func validateApplicationRules(_ value: Any?) throws -> [ManifestApplicationRule] {
		guard let rawRules = value as? [Any], !rawRules.isEmpty,
			rawRules.count <= Self.maximumApplicationRules else {
			throw WhitelistError.invalidManifest(
				"application_rules must contain 1...\(Self.maximumApplicationRules) entries"
			)
		}
		var rules: [ManifestApplicationRule] = []
		var identifiers = Set<String>()
		for (index, rawRule) in rawRules.enumerated() {
			guard let rule = rawRule as? [String: Any] else {
				throw WhitelistError.invalidManifest("application_rules[\(index)] must be an object")
			}
			let parsed = try validateApplicationRule(rule, index: index)
			guard identifiers.insert(parsed.id).inserted else {
				throw WhitelistError.invalidManifest("duplicate application rule id \(parsed.id)")
			}
			rules.append(parsed)
		}
		return rules
	}

	private func validateApplicationRule(_ rule: [String: Any], index: Int) throws -> ManifestApplicationRule {
		let prefix = "application_rules[\(index)]"
		try requireExactKeys(
			rule,
			allowed: [
				"id", "application_family", "display_name", "path_rule_id", "basename",
				"signing_identifier", "team_identifier_policy", "team_identifier",
				"signing_policy_id"
			],
			context: prefix
		)
		let id = try requireIdentifier(rule["id"], name: "\(prefix).id")
		let family = try requireIdentifier(
			rule["application_family"], name: "\(prefix).application_family"
		)
		let displayName = try requireString(
			rule["display_name"], name: "\(prefix).display_name", maximumUTF8Length: 128
		)
		let pathRuleID = try requireMember(
			rule["path_rule_id"], name: "\(prefix).path_rule_id",
			allowed: CompiledWhitelistBindings.pathRuleIDs
		)
		let basename = try requirePatternString(
			rule["basename"], name: "\(prefix).basename",
			pattern: "^[A-Za-z0-9][A-Za-z0-9._+-]{0,127}$", maximumUTF8Length: 128
		)
		let signingIdentifier = try requirePatternString(
			rule["signing_identifier"], name: "\(prefix).signing_identifier",
			pattern: "^[A-Za-z0-9._-]{1,128}$", maximumUTF8Length: 128
		)
		let teamPolicyString = try requireString(
			rule["team_identifier_policy"], name: "\(prefix).team_identifier_policy",
			maximumUTF8Length: 16
		)
		guard let teamPolicy = ManifestTeamIdentifierPolicy(rawValue: teamPolicyString) else {
			throw WhitelistError.invalidManifest("\(prefix).team_identifier_policy is unsupported")
		}
		let teamIdentifier: String?
		if rule["team_identifier"] is NSNull {
			teamIdentifier = nil
		} else {
			teamIdentifier = try requirePatternString(
				rule["team_identifier"], name: "\(prefix).team_identifier",
				pattern: "^[A-Z0-9]{10}$", maximumUTF8Length: 10
			)
		}
		guard (teamPolicy == .exact && teamIdentifier != nil) ||
			(teamPolicy == .absent && teamIdentifier == nil) else {
			throw WhitelistError.invalidManifest(
				"\(prefix) team_identifier must agree with team_identifier_policy"
			)
		}
		let signingPolicyID = try requireMember(
			rule["signing_policy_id"], name: "\(prefix).signing_policy_id",
			allowed: CompiledWhitelistBindings.signingPolicyIDs
		)
		return ManifestApplicationRule(
			id: id,
			applicationFamily: family,
			displayName: displayName,
			pathRuleID: pathRuleID,
			basename: basename,
			signingIdentifier: signingIdentifier,
			teamIdentifierPolicy: teamPolicy,
			teamIdentifier: teamIdentifier,
			signingPolicyID: signingPolicyID
		)
	}

	private func validateImageVariants(_ value: Any?, applicationIDs: Set<String>) throws -> [ManifestImageVariant] {
		guard let rawVariants = value as? [Any], !rawVariants.isEmpty,
			rawVariants.count <= Self.maximumImageVariants else {
			throw WhitelistError.invalidManifest(
				"image_variants must contain 1...\(Self.maximumImageVariants) entries"
			)
		}
		var variants: [ManifestImageVariant] = []
		var identifiers = Set<String>()
		var policyIdentities = Set<String>()
		for (index, rawVariant) in rawVariants.enumerated() {
			guard let variant = rawVariant as? [String: Any] else {
				throw WhitelistError.invalidManifest("image_variants[\(index)] must be an object")
			}
			let parsed = try validateImageVariant(variant, index: index, applicationIDs: applicationIDs)
			guard identifiers.insert(parsed.id).inserted else {
				throw WhitelistError.invalidManifest("duplicate image variant id \(parsed.id)")
			}
			let contentIdentity = parsed.cdhash ?? "none"
			let windowIdentity = parsed.searchWindow.map { "\($0.start)-\($0.end)" } ?? "none"
			let identityKey = "\(parsed.applicationRuleID):\(parsed.architecture):" +
				"\(parsed.matchMode.rawValue):\(contentIdentity):\(windowIdentity)"
			guard policyIdentities.insert(identityKey).inserted else {
				throw WhitelistError.invalidManifest("duplicate application/mode/content policy identity")
			}
			variants.append(parsed)
		}
		return variants
	}

	private func validateImageVariant(
		_ variant: [String: Any], index: Int, applicationIDs: Set<String>
	) throws -> ManifestImageVariant {
		let prefix = "image_variants[\(index)]"
		try requireExactKeys(
			variant,
			allowed: [
				"id", "application_rule_id", "application_version", "architecture", "cdhash",
				"match_mode", "target_file_offset", "executable_range", "search_window",
				"allowed_patch_definition_ids"
			],
			context: prefix
		)
		let id = try requireIdentifier(variant["id"], name: "\(prefix).id")
		let applicationRuleID = try requireIdentifier(
			variant["application_rule_id"], name: "\(prefix).application_rule_id"
		)
		guard applicationIDs.contains(applicationRuleID) else {
			throw WhitelistError.invalidManifest("\(prefix) references an unknown application rule")
		}
		let applicationVersion: String?
		if variant["application_version"] is NSNull {
			applicationVersion = nil
		} else {
			applicationVersion = try requirePatternString(
				variant["application_version"], name: "\(prefix).application_version",
				pattern: "^[A-Za-z0-9][A-Za-z0-9._+-]{0,31}$", maximumUTF8Length: 32
			)
		}
		let architecture = try requireMember(
			variant["architecture"], name: "\(prefix).architecture",
			allowed: CompiledWhitelistBindings.architectures
		)
		let cdhash: String?
		if variant["cdhash"] is NSNull {
			cdhash = nil
		} else {
			cdhash = try requirePatternString(
				variant["cdhash"], name: "\(prefix).cdhash",
				pattern: "^[0-9a-f]{40}$", maximumUTF8Length: 40
			)
		}
		let matchModeString = try requireString(
			variant["match_mode"], name: "\(prefix).match_mode", maximumUTF8Length: 32
		)
		guard let matchMode = ManifestMatchMode(rawValue: matchModeString) else {
			throw WhitelistError.invalidManifest("\(prefix).match_mode is unsupported")
		}
		if matchMode == .imageScan && !CompiledWhitelistBindings.imageScanEnabled {
			throw WhitelistError.invalidManifest(
				"image_scan is reserved for a future authenticated userspace pre-scan"
			)
		}

		let targetFileOffset: UInt64?
		if variant["target_file_offset"] is NSNull {
			targetFileOffset = nil
		} else {
			targetFileOffset = try requireUInt64(
				variant["target_file_offset"], name: "\(prefix).target_file_offset",
				maximum: Self.maximumReviewedFileOffset
			)
		}
		guard (matchMode == .strictVariant && targetFileOffset != nil) ||
			(matchMode != .strictVariant && targetFileOffset == nil) else {
			throw WhitelistError.invalidManifest(
				"strict_variant requires an offset; other modes require null"
			)
		}

		guard let rawRange = variant["executable_range"] as? [String: Any] else {
			throw WhitelistError.invalidManifest("\(prefix).executable_range must be an object")
		}
		try requireExactKeys(rawRange, allowed: ["start", "end"], context: "\(prefix).executable_range")
		let rangeStart = try requireUInt64(
			rawRange["start"], name: "\(prefix).executable_range.start",
			maximum: Self.maximumReviewedFileOffset
		)
		let rangeEnd = try requireUInt64(
			rawRange["end"], name: "\(prefix).executable_range.end",
			maximum: Self.maximumReviewedFileOffset
		)
		guard rangeStart < rangeEnd else {
			throw WhitelistError.invalidManifest("\(prefix).executable_range is empty or reversed")
		}
		if let targetFileOffset {
			guard targetFileOffset >= rangeStart && targetFileOffset < rangeEnd else {
				throw WhitelistError.invalidManifest("\(prefix).target_file_offset is outside executable_range")
			}
		}

		let searchWindow: ManifestExecutableRange?
		if variant["search_window"] is NSNull {
			searchWindow = nil
		} else {
			guard let rawWindow = variant["search_window"] as? [String: Any] else {
				throw WhitelistError.invalidManifest("\(prefix).search_window must be an object or null")
			}
			try requireExactKeys(rawWindow, allowed: ["start", "end"], context: "\(prefix).search_window")
			let windowStart = try requireUInt64(
				rawWindow["start"], name: "\(prefix).search_window.start",
				maximum: Self.maximumReviewedFileOffset
			)
			let windowEnd = try requireUInt64(
				rawWindow["end"], name: "\(prefix).search_window.end",
				maximum: Self.maximumReviewedFileOffset
			)
			guard windowStart < windowEnd,
				windowEnd - windowStart <= Self.x8664ValidationPageSize,
				windowStart % Self.x8664ValidationPageSize == 0,
				windowStart >= rangeStart, windowEnd <= rangeEnd else {
				throw WhitelistError.invalidManifest(
					"\(prefix).search_window must be page-aligned, non-empty, at most 4096 bytes, and inside executable_range"
				)
			}
			searchWindow = ManifestExecutableRange(start: windowStart, end: windowEnd)
		}
		guard (matchMode == .boundedWindow && searchWindow != nil) ||
			(matchMode != .boundedWindow && searchWindow == nil) else {
			throw WhitelistError.invalidManifest(
				"bounded_window requires a search_window; other modes require null"
			)
		}

		guard let rawPatchIDs = variant["allowed_patch_definition_ids"] as? [Any],
			!rawPatchIDs.isEmpty, rawPatchIDs.count <= Self.maximumPatchesPerVariant else {
			throw WhitelistError.invalidManifest(
				"\(prefix).allowed_patch_definition_ids must contain 1...\(Self.maximumPatchesPerVariant) entries"
			)
		}
		var patchIDs: [String] = []
		var uniquePatchIDs = Set<String>()
		for (patchIndex, rawPatchID) in rawPatchIDs.enumerated() {
			let patchID = try requireMember(
				rawPatchID,
				name: "\(prefix).allowed_patch_definition_ids[\(patchIndex)]",
				allowed: CompiledWhitelistBindings.patchDefinitionIDs
			)
			guard uniquePatchIDs.insert(patchID).inserted else {
				throw WhitelistError.invalidManifest("\(prefix) contains a duplicate patch definition")
			}
			patchIDs.append(patchID)
		}

		return ManifestImageVariant(
			id: id,
			applicationRuleID: applicationRuleID,
			applicationVersion: applicationVersion,
			architecture: architecture,
			cdhash: cdhash,
			matchMode: matchMode,
			targetFileOffset: targetFileOffset,
			executableRange: ManifestExecutableRange(start: rangeStart, end: rangeEnd),
			searchWindow: searchWindow,
			allowedPatchDefinitionIDs: patchIDs
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

	private func requireUInt64(_ value: Any?, name: String, maximum: UInt64) throws -> UInt64 {
		guard let number = value as? NSNumber,
			CFGetTypeID(number) != CFBooleanGetTypeID(),
			number.doubleValue.rounded(.towardZero) == number.doubleValue,
			number.doubleValue >= 0, number.doubleValue <= Double(maximum) else {
			throw WhitelistError.invalidManifest("\(name) must be a bounded unsigned integer")
		}
		return number.uint64Value
	}

	private func requireString(_ value: Any?, name: String, maximumUTF8Length: Int) throws -> String {
		guard let string = value as? String, !string.isEmpty,
			string.utf8.count <= maximumUTF8Length else {
			throw WhitelistError.invalidManifest("\(name) must be a bounded non-empty string")
		}
		return string
	}

	private func requireIdentifier(_ value: Any?, name: String) throws -> String {
		try requirePatternString(
			value, name: name, pattern: "^[a-z0-9][a-z0-9._-]{0,95}$", maximumUTF8Length: 96
		)
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
