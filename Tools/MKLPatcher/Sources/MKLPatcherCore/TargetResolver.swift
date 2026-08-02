import Foundation

public enum TargetResolverError: LocalizedError, Sendable {
    case unsupportedSelection(String)
    case noCompatibleBinaries(String)

    public var errorDescription: String? {
        switch self {
        case .unsupportedSelection(let message), .noCompatibleBinaries(let message):
            return message
        }
    }
}

public enum TargetResolver {
    public static func resolve(selection: URL) throws -> [URL] {
        var isDirectory: ObjCBool = false
        guard FileManager.default.fileExists(
            atPath: selection.path,
            isDirectory: &isDirectory
        ) else {
            throw TargetResolverError.unsupportedSelection("The selected item no longer exists.")
        }

        if !isDirectory.boolValue {
            return [selection]
        }

        var binaries = findCompatibleMachOs(below: selection)
        if selection.pathExtension.lowercased() == "app" {
            binaries.append(contentsOf: resolveDiscordExternalModules(for: selection))
        }

        binaries = Array(Set(binaries.map(\.standardizedFileURL)))
            .sorted { $0.path < $1.path }
        guard !binaries.isEmpty else {
            if containsMachO(below: selection) {
                throw TargetResolverError.noCompatibleBinaries(
                    "Mach-O files were found, but \(MachOPatcher.symbolName) was absent."
                )
            }
            throw TargetResolverError.noCompatibleBinaries(
                "No Mach-O files were found."
            )
        }
        return binaries
    }

    private static func resolveDiscordExternalModules(for app: URL) -> [URL] {
        let buildInfo = app
            .appendingPathComponent("Contents/Resources/build_info.json")
        guard
            let data = try? Data(contentsOf: buildInfo),
            let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
            let version = json["version"] as? String,
            !version.isEmpty
        else {
            return []
        }

        var roots: [URL] = []
        let supportRoot = FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent("Library/Application Support")
        let bundleIdentifier = Bundle(url: app)?.bundleIdentifier?.lowercased() ?? ""
        let preferredName: String
        if bundleIdentifier.contains("canary") || app.lastPathComponent.lowercased().contains("canary") {
            preferredName = "discordcanary"
        } else if bundleIdentifier.contains("ptb") || app.lastPathComponent.lowercased().contains("ptb") {
            preferredName = "discordptb"
        } else {
            preferredName = "discord"
        }

        let supportNames = [preferredName, "discord", "discordptb", "discordcanary"]
        for name in supportNames {
            let support = supportRoot.appendingPathComponent(name, isDirectory: true)
            let versionRoot = support.appendingPathComponent(version, isDirectory: true)
            let appVersionRoot = support.appendingPathComponent("app-\(version)", isDirectory: true)
            if FileManager.default.fileExists(atPath: versionRoot.path) {
                roots.append(versionRoot)
            }
            if FileManager.default.fileExists(atPath: appVersionRoot.path) {
                roots.append(appVersionRoot)
            }
        }

        return roots.flatMap(findCompatibleMachOs(below:))
    }

    static func findCompatibleMachOs(below root: URL) -> [URL] {
        guard let enumerator = FileManager.default.enumerator(
            at: root,
            includingPropertiesForKeys: [.isRegularFileKey, .isSymbolicLinkKey],
            options: [.skipsHiddenFiles],
            errorHandler: { _, _ in true }
        ) else {
            return []
        }

        var results: [URL] = []
        for case let url as URL in enumerator {
            if url.lastPathComponent.hasSuffix(".\(PatchService.backupExtension)") {
                continue
            }
            guard
                let values = try? url.resourceValues(
                    forKeys: [.isRegularFileKey, .isSymbolicLinkKey]
                ),
                values.isRegularFile == true,
                values.isSymbolicLink != true,
                hasMachOMagic(url)
            else {
                continue
            }

            guard let data = try? Data(contentsOf: url, options: .mappedIfSafe) else {
                continue
            }
            switch MachOPatcher.inspect(data: data).state {
            case .original, .projectPatched, .upstreamPatched, .unknownImplementation:
                results.append(url)
            case .notMachO, .symbolAbsent, .unsupported:
                break
            }
        }
        return results
    }

    private static func hasMachOMagic(_ url: URL) -> Bool {
        guard
            let handle = try? FileHandle(forReadingFrom: url),
            let data = try? handle.read(upToCount: 4),
            data.count == 4
        else {
            return false
        }
        try? handle.close()
        let bytes = Array(data)
        return bytes == [0xcf, 0xfa, 0xed, 0xfe]
            || bytes == [0xca, 0xfe, 0xba, 0xbe]
            || bytes == [0xca, 0xfe, 0xba, 0xbf]
    }

    private static func containsMachO(below root: URL) -> Bool {
        guard let enumerator = FileManager.default.enumerator(
            at: root,
            includingPropertiesForKeys: [.isRegularFileKey, .isSymbolicLinkKey],
            options: [.skipsHiddenFiles],
            errorHandler: { _, _ in true }
        ) else {
            return false
        }
        for case let url as URL in enumerator {
            guard
                let values = try? url.resourceValues(
                    forKeys: [.isRegularFileKey, .isSymbolicLinkKey]
                ),
                values.isRegularFile == true,
                values.isSymbolicLink != true
            else {
                continue
            }
            if hasMachOMagic(url) {
                return true
            }
        }
        return false
    }
}
