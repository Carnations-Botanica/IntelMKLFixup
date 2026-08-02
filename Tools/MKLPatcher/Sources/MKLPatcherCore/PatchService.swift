import Foundation

public struct PatchTarget: Identifiable, Equatable, Sendable {
    public let url: URL
    public let state: PatchState
    public let hasBackup: Bool

    public var id: URL { url }

    public init(url: URL, state: PatchState, hasBackup: Bool) {
        self.url = url
        self.state = state
        self.hasBackup = hasBackup
    }
}

public enum PatchServiceError: LocalizedError, Sendable {
    case noTargets
    case targetNotPatchable(String)
    case backupConflict(String)
    case backupMissing(String)
    case commandFailed(String)

    public var errorDescription: String? {
        switch self {
        case .noTargets:
            return "No compatible MKL binaries were found."
        case .targetNotPatchable(let message),
             .backupConflict(let message),
             .backupMissing(let message),
             .commandFailed(let message):
            return message
        }
    }
}

public enum PatchService {
    public static let backupExtension = "mkl-original"

    public static func backupURL(for target: URL) -> URL {
        target.appendingPathExtension(backupExtension)
    }

    public static func inspect(url: URL) -> PatchTarget {
        let backup = backupURL(for: url)
        do {
            let data = try Data(contentsOf: url, options: .mappedIfSafe)
            return PatchTarget(
                url: url,
                state: MachOPatcher.inspect(data: data).state,
                hasBackup: FileManager.default.fileExists(atPath: backup.path)
            )
        } catch {
            return PatchTarget(
                url: url,
                state: .unsupported(error.localizedDescription),
                hasBackup: FileManager.default.fileExists(atPath: backup.path)
            )
        }
    }

    public static func patch(urls: [URL]) throws {
        guard !urls.isEmpty else { throw PatchServiceError.noTargets }

        var prepared: [(url: URL, original: Data, patched: Data, backup: URL)] = []
        for url in urls {
            let original = try Data(contentsOf: url, options: .mappedIfSafe)
            let inspection = MachOPatcher.inspect(data: original)
            guard inspection.state == .original else {
                throw PatchServiceError.targetNotPatchable(
                    "\(url.lastPathComponent) is not in the expected unpatched state."
                )
            }
            let patched = try MachOPatcher.patchedData(from: original)
            let backup = backupURL(for: url)
            if FileManager.default.fileExists(atPath: backup.path) {
                let existing = try Data(contentsOf: backup, options: .mappedIfSafe)
                guard existing == original else {
                    throw PatchServiceError.backupConflict(
                        "A different backup already exists at \(backup.path). It was left untouched."
                    )
                }
            }
            prepared.append((url, original, patched, backup))
        }

        for item in prepared {
            if !FileManager.default.fileExists(atPath: item.backup.path) {
                try FileManager.default.copyItem(at: item.url, to: item.backup)
            }
        }

        for item in prepared {
            try replace(item.url, with: item.patched, sign: true)
        }
    }

    public static func unpatch(urls: [URL]) throws {
        guard !urls.isEmpty else { throw PatchServiceError.noTargets }

        var prepared: [(url: URL, backupData: Data)] = []
        for url in urls {
            let current = try Data(contentsOf: url, options: .mappedIfSafe)
            let inspection = MachOPatcher.inspect(data: current)
            guard inspection.state == .projectPatched else {
                throw PatchServiceError.targetNotPatchable(
                    "\(url.lastPathComponent) is not in the expected patched state."
                )
            }

            let backup = backupURL(for: url)
            guard FileManager.default.fileExists(atPath: backup.path) else {
                throw PatchServiceError.backupMissing(
                    "The original backup is missing for \(url.path); unpatch was not attempted."
                )
            }
            let backupData = try Data(contentsOf: backup, options: .mappedIfSafe)
            guard MachOPatcher.inspect(data: backupData).state == .original else {
                throw PatchServiceError.backupConflict(
                    "The backup for \(url.lastPathComponent) is not a recognized unpatched binary."
                )
            }
            prepared.append((url, backupData))
        }

        for item in prepared {
            try replace(item.url, with: item.backupData, sign: false)
        }
    }

    private static func replace(_ target: URL, with data: Data, sign: Bool) throws {
        let manager = FileManager.default
        let attributes = try manager.attributesOfItem(atPath: target.path)
        let temporary = target
            .deletingLastPathComponent()
            .appendingPathComponent(".\(target.lastPathComponent).mkl-\(UUID().uuidString)")
        defer { try? manager.removeItem(at: temporary) }

        try data.write(to: temporary, options: .withoutOverwriting)
        var restoredAttributes: [FileAttributeKey: Any] = [:]
        if let permissions = attributes[.posixPermissions] {
            restoredAttributes[.posixPermissions] = permissions
        }
        if let owner = attributes[.ownerAccountID] {
            restoredAttributes[.ownerAccountID] = owner
        }
        if !restoredAttributes.isEmpty {
            try manager.setAttributes(restoredAttributes, ofItemAtPath: temporary.path)
        }

        if sign {
            try adHocSign(temporary)
        }

        _ = try manager.replaceItemAt(
            target,
            withItemAt: temporary,
            backupItemName: nil,
            options: []
        )
    }

    private static func adHocSign(_ url: URL) throws {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/codesign")
        process.arguments = ["--force", "--sign", "-", url.path]
        let errorPipe = Pipe()
        process.standardError = errorPipe
        try process.run()
        process.waitUntilExit()
        guard process.terminationStatus == 0 else {
            let data = errorPipe.fileHandleForReading.readDataToEndOfFile()
            let message = String(decoding: data, as: UTF8.self)
                .trimmingCharacters(in: .whitespacesAndNewlines)
            throw PatchServiceError.commandFailed(
                message.isEmpty ? "Ad-hoc code signing failed." : message
            )
        }
    }
}
