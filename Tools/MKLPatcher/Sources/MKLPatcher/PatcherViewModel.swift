import AppKit
import Foundation
import MKLPatcherCore

@MainActor
final class PatcherViewModel: ObservableObject {
    @Published var selection: URL?
    @Published var targets: [PatchTarget] = []
    @Published var message = "Drop an app, folder, or Mach-O file here."
    @Published var isWorking = false
    @Published var showsSignedAppWarning = false

    var canPatch: Bool {
        !isWorking && !targets.isEmpty && targets.allSatisfy { $0.state == .original }
    }

    var canUnpatch: Bool {
        !isWorking
            && !targets.isEmpty
            && targets.allSatisfy { $0.state == .projectPatched && $0.hasBackup }
    }

    var containsAppBundleTargets: Bool {
        targets.contains { target in
            target.url.pathComponents.contains {
                $0.lowercased().hasSuffix(".app")
            }
        }
    }

    func chooseInFinder() {
        let panel = NSOpenPanel()
        panel.title = "Choose an app, folder, or MKL-containing Mach-O"
        panel.prompt = "Choose"
        panel.canChooseFiles = true
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = false
        panel.treatsFilePackagesAsDirectories = false
        guard panel.runModal() == .OK, let url = panel.url else { return }
        select(url)
    }

    func requestPatch() {
        if containsAppBundleTargets {
            showsSignedAppWarning = true
        } else {
            patch()
        }
    }

    func confirmSignedAppPatch() {
        showsSignedAppWarning = false
        patch()
    }

    func select(_ url: URL) {
        selection = url
        isWorking = true
        message = "Inspecting \(url.lastPathComponent)…"
        Task {
            do {
                let urls = try await Task.detached {
                    try TargetResolver.resolve(selection: url)
                }.value
                targets = await Task.detached {
                    urls.map(PatchService.inspect(url:))
                }.value
                describeCurrentState()
            } catch {
                targets = []
                message = error.localizedDescription
            }
            isWorking = false
        }
    }

    func patch() {
        perform(actionName: "Patching") { urls in
            try PatchService.patch(urls: urls)
        }
    }

    func unpatch() {
        perform(actionName: "Restoring") { urls in
            try PatchService.unpatch(urls: urls)
        }
    }

    private func perform(
        actionName: String,
        operation: @escaping @Sendable ([URL]) throws -> Void
    ) {
        guard !isTargetHostRunning else {
            message = "Quit the target application completely before patching or unpatching."
            NSSound.beep()
            return
        }
        let urls = targets.map(\.url)
        isWorking = true
        message = "\(actionName) \(targets.count) binar\(targets.count == 1 ? "y" : "ies")…"
        Task {
            do {
                try await Task.detached {
                    try operation(urls)
                }.value
                refresh()
                message = actionName == "Patching"
                    ? "Patched successfully. The original backup is retained beside each binary."
                    : "Original file restored. The backup is still retained for future use."
            } catch {
                refresh()
                message = error.localizedDescription
                NSSound.beep()
            }
            isWorking = false
        }
    }

    private func refresh() {
        targets = targets.map { PatchService.inspect(url: $0.url) }
    }

    private func describeCurrentState() {
        if targets.allSatisfy({ $0.state == .original }) {
            message = "Ready to patch \(targets.count) MKL binar\(targets.count == 1 ? "y" : "ies")."
        } else if targets.allSatisfy({ $0.state == .projectPatched }) {
            message = targets.allSatisfy(\.hasBackup)
                ? "Patched by this project. A verified backup is available for Unpatch."
                : "Patched by this project, but at least one original backup is missing."
        } else if targets.allSatisfy({ $0.state == .upstreamPatched }) {
            message = "Patched by recognised upstream IntelMKLFixup pattern. Automatic restore is disabled."
        } else if targets.allSatisfy({ $0.state == .symbolAbsent }) {
            message = "Mach-O found, but \(MachOPatcher.symbolName) is absent."
        } else if targets.allSatisfy({ $0.state == .notMachO }) {
            message = "No Mach-O file was found in the selection."
        } else if let unknown = targets.first(where: {
            if case .unknownImplementation = $0.state { return true }
            return false
        }), case .unknownImplementation(let reason) = unknown.state {
            message = reason
        } else if let unsupported = targets.first(where: {
            if case .unsupported = $0.state { return true }
            return false
        }), case .unsupported(let reason) = unsupported.state {
            message = reason
        } else {
            message = "The discovered binaries do not all have the same patch state."
        }
    }

    private var isTargetHostRunning: Bool {
        let appPaths = Set(
            targets.compactMap { containingApp(for: $0.url)?.standardizedFileURL.path }
        )
        let hasDiscordExternalModule = targets.contains {
            let path = $0.url.path.lowercased()
            return path.contains("/library/application support/discord")
                || path.contains("/library/application support/discordptb")
                || path.contains("/library/application support/discordcanary")
        }

        return NSWorkspace.shared.runningApplications.contains {
            let name = $0.localizedName?.lowercased() ?? ""
            let identifier = $0.bundleIdentifier?.lowercased() ?? ""
            if let path = $0.bundleURL?.standardizedFileURL.path, appPaths.contains(path) {
                return true
            }
            return hasDiscordExternalModule
                && (name == "discord"
                    || name == "discord canary"
                    || name == "discord ptb"
                    || identifier.contains("discord"))
        }
    }

    private func containingApp(for url: URL) -> URL? {
        var candidate = url.standardizedFileURL
        while candidate.path != "/" {
            if candidate.pathExtension.lowercased() == "app" {
                return candidate
            }
            candidate.deleteLastPathComponent()
        }
        return nil
    }
}
