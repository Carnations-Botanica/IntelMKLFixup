import Foundation

public struct DownloadedReleaseArtifact {
	public let manifestData: Data
	public let signatureData: Data
	public let releaseTag: String

	public init(manifestData: Data, signatureData: Data, releaseTag: String) {
		self.manifestData = manifestData
		self.signatureData = signatureData
		self.releaseTag = releaseTag
	}
}

public final class GitHubReleaseClient {
	public static let repository = "richardhedges/IntelMKLFixup"
	public static let manifestAssetName = "whitelist-manifest.json"
	public static let signatureAssetName = "whitelist-manifest.json.sig"

	private let session: URLSession
	private let token: String?

	public init(session: URLSession = .shared, token: String? = ProcessInfo.processInfo.environment["GITHUB_TOKEN"]) {
		self.session = session
		self.token = token
	}

	public func downloadLatest(includePrereleases: Bool = false) async throws -> DownloadedReleaseArtifact {
		let endpoint = includePrereleases
			? "https://api.github.com/repos/\(Self.repository)/releases?per_page=20"
			: "https://api.github.com/repos/\(Self.repository)/releases/latest"
		guard let url = URL(string: endpoint) else {
			throw WhitelistError.network("release API URL is invalid")
		}
		let releaseData = try await fetch(url: url, maximumSize: 1024 * 1024, acceptsGitHubJSON: true)
		let release = try selectRelease(from: releaseData, includePrereleases: includePrereleases)

		guard let manifestURL = release.assets[Self.manifestAssetName],
			let signatureURL = release.assets[Self.signatureAssetName] else {
			throw WhitelistError.network("release \(release.tag) lacks the required manifest assets")
		}
		async let manifest = fetch(
			url: manifestURL,
			maximumSize: ManifestValidator.maximumManifestSize,
			acceptsGitHubJSON: false
		)
		async let signature = fetch(
			url: signatureURL,
			maximumSize: ManifestAuthenticator.maximumSignatureSize,
			acceptsGitHubJSON: false
		)
		return try await DownloadedReleaseArtifact(
			manifestData: manifest,
			signatureData: signature,
			releaseTag: release.tag
		)
	}

	private func fetch(url: URL, maximumSize: Int, acceptsGitHubJSON: Bool) async throws -> Data {
		guard url.scheme == "https" else {
			throw WhitelistError.network("refusing a non-HTTPS URL")
		}
		var request = URLRequest(url: url, cachePolicy: .reloadIgnoringLocalCacheData, timeoutInterval: 30)
		request.setValue("IntelMKLFixup-WhitelistUpdater/1.0", forHTTPHeaderField: "User-Agent")
		if acceptsGitHubJSON {
			request.setValue("application/vnd.github+json", forHTTPHeaderField: "Accept")
			request.setValue("2022-11-28", forHTTPHeaderField: "X-GitHub-Api-Version")
		}
		if let token, !token.isEmpty {
			request.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization")
		}
		let (temporaryURL, response): (URL, URLResponse)
		do {
			// A download task places the response in a system-managed temporary
			// file. Nothing is copied into the manifest store until authentication
			// and strict validation have both succeeded.
			(temporaryURL, response) = try await session.download(for: request)
		} catch {
			throw WhitelistError.network(error.localizedDescription)
		}
		defer { try? FileManager.default.removeItem(at: temporaryURL) }
		guard let http = response as? HTTPURLResponse, http.statusCode == 200 else {
			throw WhitelistError.network("server returned a non-200 response")
		}
		if response.expectedContentLength > Int64(maximumSize) {
			throw WhitelistError.network("downloaded asset exceeds its size limit")
		}
		let attributes = try FileManager.default.attributesOfItem(atPath: temporaryURL.path)
		guard let rawSize = attributes[.size] as? NSNumber,
			rawSize.int64Value > 0, rawSize.int64Value <= Int64(maximumSize) else {
			throw WhitelistError.network("downloaded asset exceeds its size limit")
		}
		do {
			return try Data(contentsOf: temporaryURL, options: [.mappedIfSafe])
		} catch {
			throw WhitelistError.network("could not read temporary download")
		}
	}

	private func selectRelease(from data: Data, includePrereleases: Bool) throws -> ReleaseRecord {
		let object: Any
		do {
			object = try JSONSerialization.jsonObject(with: data, options: [])
		} catch {
			throw WhitelistError.network("release JSON is malformed")
		}
		if includePrereleases {
			guard let releases = object as? [Any] else {
				throw WhitelistError.network("release list is malformed")
			}
			for raw in releases {
				if let record = try? parseRelease(raw), !record.isDraft {
					return record
				}
			}
			throw WhitelistError.network("no non-draft release was found")
		}
		let record = try parseRelease(object)
		guard !record.isDraft, !record.isPrerelease else {
			throw WhitelistError.network("latest stable endpoint returned a draft or prerelease")
		}
		return record
	}

	private func parseRelease(_ object: Any) throws -> ReleaseRecord {
		guard let dictionary = object as? [String: Any],
			let tag = dictionary["tag_name"] as? String, !tag.isEmpty, tag.utf8.count <= 128,
			let draft = dictionary["draft"] as? Bool,
			let prerelease = dictionary["prerelease"] as? Bool,
			let rawAssets = dictionary["assets"] as? [Any] else {
			throw WhitelistError.network("release record is malformed")
		}
		var assets: [String: URL] = [:]
		for raw in rawAssets {
			guard let asset = raw as? [String: Any],
				let name = asset["name"] as? String,
				let urlString = asset["browser_download_url"] as? String,
				let url = URL(string: urlString), url.scheme == "https" else {
				continue
			}
			if name == Self.manifestAssetName || name == Self.signatureAssetName {
				guard assets[name] == nil else {
					throw WhitelistError.network("release contains duplicate required assets")
				}
				assets[name] = url
			}
		}
		return ReleaseRecord(tag: tag, isDraft: draft, isPrerelease: prerelease, assets: assets)
	}
}

private struct ReleaseRecord {
	let tag: String
	let isDraft: Bool
	let isPrerelease: Bool
	let assets: [String: URL]
}
