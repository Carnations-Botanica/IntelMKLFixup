import Foundation
import Testing
@testable import MKLPatcherCore

struct MachOPatcherTests {
    @Test
    func distinguishesAllRequiredImplementationStates() throws {
        #expect(MachOPatcher.inspect(data: Data("not Mach-O".utf8)).state == .notMachO)
        #expect(MachOPatcher.inspect(data: machO(symbol: nil, implementation: [])).state == .symbolAbsent)

        let original = machO(
            symbol: MachOPatcher.symbolName,
            implementation: MachOPatcher.recognizedOriginalBytes
        )
        #expect(MachOPatcher.inspect(data: original).state == .original)

        let project = machO(
            symbol: MachOPatcher.symbolName,
            implementation: MachOPatcher.recognizedProjectPatchedBytes
        )
        #expect(MachOPatcher.inspect(data: project).state == .projectPatched)

        let upstream = machO(
            symbol: MachOPatcher.symbolName,
            implementation: MachOPatcher.recognizedUpstreamPatchedBytes
        )
        #expect(MachOPatcher.inspect(data: upstream).state == .upstreamPatched)

        let unknown = machO(
            symbol: MachOPatcher.symbolName,
            implementation: Array(repeating: 0xcc, count: 23)
        )
        guard case .unknownImplementation = MachOPatcher.inspect(data: unknown).state else {
            Issue.record("Expected a symbol-bearing unknown implementation")
            return
        }
    }

    @Test
    func patchesOnlyTheRecognizedOriginal() throws {
        let original = machO(
            symbol: MachOPatcher.symbolName,
            implementation: MachOPatcher.recognizedOriginalBytes
        )
        let patched = try MachOPatcher.patchedData(from: original)
        #expect(MachOPatcher.inspect(data: patched).state == .projectPatched)

        let upstream = machO(
            symbol: MachOPatcher.symbolName,
            implementation: MachOPatcher.recognizedUpstreamPatchedBytes
        )
        #expect(throws: MKLPatcherError.self) {
            _ = try MachOPatcher.patchedData(from: upstream)
        }
    }

    @Test
    func directoryStatusSeparatesNoMachOAndSymbolAbsent() throws {
        let root = try temporaryDirectory()
        defer { try? FileManager.default.removeItem(at: root) }

        #expect(throws: TargetResolverError.self) {
            _ = try TargetResolver.resolve(selection: root)
        }
        do {
            _ = try TargetResolver.resolve(selection: root)
            Issue.record("Expected no-Mach-O result")
        } catch {
            #expect(error.localizedDescription == "No Mach-O files were found.")
        }

        let symbolAbsent = root.appendingPathComponent("symbol-absent.dylib")
        try machO(symbol: nil, implementation: []).write(to: symbolAbsent)
        do {
            _ = try TargetResolver.resolve(selection: root)
            Issue.record("Expected symbol-absent result")
        } catch {
            #expect(
                error.localizedDescription
                    == "Mach-O files were found, but _mkl_serv_intel_cpu_true was absent."
            )
        }
    }

    @Test
    func unknownSymbolImplementationIsReturnedForInspection() throws {
        let root = try temporaryDirectory()
        defer { try? FileManager.default.removeItem(at: root) }
        let target = root.appendingPathComponent("unknown.dylib")
        try machO(
            symbol: MachOPatcher.symbolName,
            implementation: Array(repeating: 0x90, count: 23)
        ).write(to: target)

        #expect(try TargetResolver.resolve(selection: root) == [target])
    }

    private func temporaryDirectory() throws -> URL {
        let url = FileManager.default.temporaryDirectory
            .appendingPathComponent("IntelMKLFixup-MKLPatcherTests-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        return url
    }

    private func machO(symbol: String?, implementation: [UInt8]) -> Data {
        let textAddress: UInt64 = 0x1_0000_0000
        let functionOffset = 0x100
        let symbolOffset = 0x180
        let stringOffset = 0x1a0
        var bytes = [UInt8](repeating: 0, count: 0x240)

        put32(0xfeedfacf, at: 0, in: &bytes)
        put32(0x01000007, at: 4, in: &bytes)
        put32(2, at: 16, in: &bytes)

        let segment = 32
        put32(0x19, at: segment, in: &bytes)
        put32(72, at: segment + 4, in: &bytes)
        putString("__TEXT", at: segment + 8, maximum: 16, in: &bytes)
        put64(textAddress, at: segment + 24, in: &bytes)
        put64(0, at: segment + 40, in: &bytes)

        let symtab = segment + 72
        put32(0x2, at: symtab, in: &bytes)
        put32(24, at: symtab + 4, in: &bytes)
        put32(UInt32(symbolOffset), at: symtab + 8, in: &bytes)
        put32(symbol == nil ? 0 : 1, at: symtab + 12, in: &bytes)
        put32(UInt32(stringOffset), at: symtab + 16, in: &bytes)

        if let symbol {
            put32(1, at: symbolOffset, in: &bytes)
            put64(textAddress + UInt64(functionOffset), at: symbolOffset + 8, in: &bytes)
            putString(symbol, at: stringOffset + 1, maximum: symbol.utf8.count + 1, in: &bytes)
            bytes.replaceSubrange(
                functionOffset..<(functionOffset + implementation.count),
                with: implementation
            )
        }
        return Data(bytes)
    }

    private func put32(_ value: UInt32, at offset: Int, in bytes: inout [UInt8]) {
        for index in 0..<4 {
            bytes[offset + index] = UInt8(truncatingIfNeeded: value >> UInt32(index * 8))
        }
    }

    private func put64(_ value: UInt64, at offset: Int, in bytes: inout [UInt8]) {
        for index in 0..<8 {
            bytes[offset + index] = UInt8(truncatingIfNeeded: value >> UInt64(index * 8))
        }
    }

    private func putString(
        _ string: String,
        at offset: Int,
        maximum: Int,
        in bytes: inout [UInt8]
    ) {
        let encoded = Array(string.utf8.prefix(maximum - 1))
        bytes.replaceSubrange(offset..<(offset + encoded.count), with: encoded)
    }
}
