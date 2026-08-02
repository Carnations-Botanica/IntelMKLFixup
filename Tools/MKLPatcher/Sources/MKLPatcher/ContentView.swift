import MKLPatcherCore
import SwiftUI
import UniformTypeIdentifiers

struct ContentView: View {
    @StateObject private var model = PatcherViewModel()
    @State private var isDropTargeted = false

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            VStack(alignment: .leading, spacing: 5) {
                Text("MKL Patcher")
                    .font(.system(size: 28, weight: .semibold))
                Text("MKL vendor-gate patch for x86_64 AMD Macs")
                    .foregroundStyle(.secondary)
            }

            dropZone

            if !model.targets.isEmpty {
                targetList
                if model.containsAppBundleTargets {
                    signedAppWarning
                }
            }

            HStack {
                if model.isWorking {
                    ProgressView()
                        .controlSize(.small)
                } else {
                    Image(systemName: statusIcon)
                        .foregroundStyle(statusColor)
                }
                Text(model.message)
                    .font(.callout)
                    .textSelection(.enabled)
                Spacer()
            }

            Spacer(minLength: 0)

            HStack {
                Text("A “.mkl-original” backup is kept beside each modified binary.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                Spacer()
                Button("Unpatch") {
                    model.unpatch()
                }
                .disabled(!model.canUnpatch)
                Button("Patch") {
                    model.requestPatch()
                }
                .buttonStyle(.borderedProminent)
                .disabled(!model.canPatch)
            }
        }
        .padding(24)
        .alert(
            "Signed application bundle",
            isPresented: $model.showsSignedAppWarning
        ) {
            Button("Cancel", role: .cancel) {}
            Button("Patch Binary") {
                model.confirmSignedAppPatch()
            }
        } message: {
            Text(
                "Modifying a binary inside an app invalidates the app’s outer vendor signature. "
                    + "The modified binary will be ad-hoc signed, but the containing app may still "
                    + "need vendor-specific re-signing and may reject the change. Work on a copy."
            )
        }
    }

    private var dropZone: some View {
        VStack(spacing: 12) {
            Image(systemName: model.selection == nil ? "arrow.down.app" : "app.dashed")
                .font(.system(size: 38, weight: .light))
                .foregroundStyle(isDropTargeted ? Color.accentColor : Color.secondary)
            Text(model.selection?.lastPathComponent ?? "Drop an app, folder, or Mach-O")
                .font(.headline)
            if let selection = model.selection {
                Text(selection.path)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .lineLimit(2)
                    .truncationMode(.middle)
                    .textSelection(.enabled)
            } else {
                Text("Compatible binaries are identified by their Mach-O symbol, not filename.")
                    .font(.callout)
                    .foregroundStyle(.secondary)
            }
            Button("Browse in Finder…") {
                model.chooseInFinder()
            }
        }
        .frame(maxWidth: .infinity, minHeight: 150)
        .padding()
        .background(
            RoundedRectangle(cornerRadius: 14)
                .fill(isDropTargeted ? Color.accentColor.opacity(0.08) : Color.secondary.opacity(0.06))
        )
        .overlay(
            RoundedRectangle(cornerRadius: 14)
                .strokeBorder(
                    isDropTargeted ? Color.accentColor : Color.secondary.opacity(0.35),
                    style: StrokeStyle(lineWidth: isDropTargeted ? 2 : 1, dash: [7])
                )
        )
        .onDrop(
            of: [UTType.fileURL.identifier],
            isTargeted: $isDropTargeted,
            perform: handleDrop
        )
    }

    private var signedAppWarning: some View {
        Label {
            Text(
                "These files are inside a signed app bundle. The binary backup is reversible, "
                    + "but the parent app’s signature may require app-specific repair."
            )
            .font(.caption)
        } icon: {
            Image(systemName: "signature")
        }
        .foregroundStyle(.orange)
    }

    private var targetList: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Compatible binaries")
                .font(.subheadline.weight(.semibold))
            ForEach(model.targets) { target in
                HStack(spacing: 8) {
                    Image(systemName: icon(for: target.state))
                        .foregroundStyle(color(for: target.state))
                    VStack(alignment: .leading, spacing: 2) {
                        Text(target.url.lastPathComponent)
                            .font(.callout.weight(.medium))
                        Text(target.url.deletingLastPathComponent().path)
                            .font(.caption2)
                            .foregroundStyle(.secondary)
                            .lineLimit(1)
                            .truncationMode(.middle)
                    }
                    Spacer()
                    Text(label(for: target.state))
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    if target.hasBackup {
                        Label("Backup", systemImage: "externaldrive.badge.checkmark")
                            .labelStyle(.iconOnly)
                            .foregroundStyle(.green)
                            .help("Original backup retained")
                    }
                }
            }
        }
    }

    private func handleDrop(_ providers: [NSItemProvider]) -> Bool {
        guard let provider = providers.first else { return false }
        provider.loadItem(forTypeIdentifier: UTType.fileURL.identifier, options: nil) { item, _ in
            let url: URL?
            if let data = item as? Data {
                url = URL(dataRepresentation: data, relativeTo: nil)
            } else if let itemURL = item as? URL {
                url = itemURL
            } else {
                url = nil
            }
            guard let url else { return }
            Task { @MainActor in
                model.select(url)
            }
        }
        return true
    }

    private var statusIcon: String {
        if model.targets.isEmpty { return "info.circle" }
        if model.targets.allSatisfy({ $0.state == .projectPatched && $0.hasBackup }) {
            return "checkmark.seal.fill"
        }
        if model.targets.allSatisfy({ $0.state == .original }) {
            return "checkmark.circle"
        }
        return "exclamationmark.triangle.fill"
    }

    private var statusColor: Color {
        if model.targets.allSatisfy({ $0.state == .projectPatched && $0.hasBackup }) {
            return .green
        }
        return model.targets.isEmpty ? .secondary : .orange
    }

    private func icon(for state: PatchState) -> String {
        switch state {
        case .original: "circle"
        case .projectPatched: "checkmark.circle.fill"
        case .upstreamPatched: "checkmark.seal"
        case .symbolAbsent: "questionmark.circle"
        case .notMachO: "doc.badge.questionmark"
        case .unknownImplementation, .unsupported: "xmark.octagon.fill"
        }
    }

    private func color(for state: PatchState) -> Color {
        switch state {
        case .original: .secondary
        case .projectPatched: .green
        case .upstreamPatched: .blue
        case .symbolAbsent, .notMachO: .orange
        case .unknownImplementation, .unsupported: .red
        }
    }

    private func label(for state: PatchState) -> String {
        switch state {
        case .original: "Recognised original"
        case .projectPatched: "Patched by this project"
        case .upstreamPatched: "Recognised upstream patch"
        case .symbolAbsent: "Symbol absent"
        case .notMachO: "Not Mach-O"
        case .unknownImplementation: "Unknown implementation"
        case .unsupported: "Unsupported"
        }
    }
}
