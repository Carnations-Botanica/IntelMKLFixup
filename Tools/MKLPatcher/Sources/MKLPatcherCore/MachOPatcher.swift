import Foundation

public enum PatchState: Equatable, Sendable {
    case notMachO
    case symbolAbsent
    case original
    case projectPatched
    case upstreamPatched
    case unknownImplementation(String)
    case unsupported(String)
}

public struct PatchInspection: Equatable, Sendable {
    public let state: PatchState
    public let patchOffset: Int?

    public init(state: PatchState, patchOffset: Int?) {
        self.state = state
        self.patchOffset = patchOffset
    }
}

public enum MKLPatcherError: LocalizedError, Sendable {
    case notMachO
    case symbolAbsent
    case malformed(String)
    case unsupported(String)

    public var errorDescription: String? {
        switch self {
        case .notMachO:
            return "The selected file is not a supported Mach-O binary."
        case .symbolAbsent:
            return "The Mach-O file does not contain \(MachOPatcher.symbolName)."
        case .malformed(let message), .unsupported(let message):
            return message
        }
    }
}

public enum MachOPatcher {
    public static let symbolName = "_mkl_serv_intel_cpu_true"
    public static let patchBytes: [UInt8] = [0xb8, 0x01, 0x00, 0x00, 0x00, 0xc3]
    public static let recognizedOriginalBytes: [UInt8] = [
        0x53, 0x48, 0x83, 0xec, 0x20, 0x8b, 0x35, 0x61,
        0x0f, 0x79, 0x00, 0x85, 0xf6, 0x7c, 0x08, 0x89,
        0xf0, 0x48, 0x83, 0xc4, 0x20, 0x5b, 0xc3,
    ]
    public static let recognizedProjectPatchedBytes: [UInt8] =
        patchBytes + Array(recognizedOriginalBytes.dropFirst(patchBytes.count))
    public static let recognizedUpstreamPatchedBytes: [UInt8] = [
        0x55, 0x48, 0x89, 0xe5, 0xb8, 0x01, 0x00, 0x00,
        0x00, 0x5d, 0xc3, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    ]

    public static func inspect(data: Data) -> PatchInspection {
        do {
            let offset = try locatePatchOffset(in: data)
            let implementationSize = recognizedOriginalBytes.count
            guard offset >= 0, offset + implementationSize <= data.count else {
                return PatchInspection(
                    state: .unsupported("The resolved MKL function lies outside the file."),
                    patchOffset: offset
                )
            }
            let bytes = Array(data[offset..<(offset + implementationSize)])
            if bytes == recognizedOriginalBytes {
                return PatchInspection(state: .original, patchOffset: offset)
            }
            if bytes == recognizedProjectPatchedBytes {
                return PatchInspection(state: .projectPatched, patchOffset: offset)
            }
            if bytes == recognizedUpstreamPatchedBytes {
                return PatchInspection(state: .upstreamPatched, patchOffset: offset)
            }
            return PatchInspection(
                state: .unknownImplementation(
                    "The symbol exists but its implementation starts with \(hex(Array(bytes.prefix(23)))); "
                        + "no supported patch or restore operation is safe."
                ),
                patchOffset: offset
            )
        } catch MKLPatcherError.notMachO {
            return PatchInspection(state: .notMachO, patchOffset: nil)
        } catch MKLPatcherError.symbolAbsent {
            return PatchInspection(state: .symbolAbsent, patchOffset: nil)
        } catch {
            return PatchInspection(
                state: .unsupported(error.localizedDescription),
                patchOffset: nil
            )
        }
    }

    public static func patchedData(from data: Data) throws -> Data {
        let inspection = inspect(data: data)
        switch inspection.state {
        case .projectPatched:
            return data
        case .notMachO:
            throw MKLPatcherError.notMachO
        case .symbolAbsent:
            throw MKLPatcherError.symbolAbsent
        case .upstreamPatched:
            throw MKLPatcherError.unsupported(
                "Patched by recognised upstream IntelMKLFixup pattern; automatic restore is disabled."
            )
        case .unknownImplementation(let reason), .unsupported(let reason):
            throw MKLPatcherError.unsupported(reason)
        case .original:
            break
        }
        guard let offset = inspection.patchOffset else {
            throw MKLPatcherError.malformed("Could not resolve the MKL patch location.")
        }
        var output = data
        output.replaceSubrange(offset..<(offset + patchBytes.count), with: patchBytes)
        return output
    }

    private static func locatePatchOffset(in data: Data) throws -> Int {
        let slice = try x86Slice(in: data)
        let sliceData = data.subdata(in: slice.offset..<(slice.offset + slice.size))
        guard try sliceData.u32LE(at: 0) == 0xfeedfacf else {
            throw MKLPatcherError.unsupported("The x86_64 slice is not a 64-bit Mach-O file.")
        }

        let commandCount = Int(try sliceData.u32LE(at: 16))
        var commandOffset = 32
        var symbolTable: (offset: Int, count: Int, strings: Int)?
        var textSegment: (address: UInt64, fileOffset: UInt64)?

        for _ in 0..<commandCount {
            let command = try sliceData.u32LE(at: commandOffset)
            let commandSize = Int(try sliceData.u32LE(at: commandOffset + 4))
            guard commandSize >= 8, commandOffset + commandSize <= sliceData.count else {
                throw MKLPatcherError.malformed("The Mach-O load commands are malformed.")
            }

            if command == 0x2 {
                symbolTable = (
                    offset: Int(try sliceData.u32LE(at: commandOffset + 8)),
                    count: Int(try sliceData.u32LE(at: commandOffset + 12)),
                    strings: Int(try sliceData.u32LE(at: commandOffset + 16))
                )
            } else if command == 0x19 {
                let name = try sliceData.fixedString(at: commandOffset + 8, count: 16)
                if name == "__TEXT" {
                    textSegment = (
                        address: try sliceData.u64LE(at: commandOffset + 24),
                        fileOffset: try sliceData.u64LE(at: commandOffset + 40)
                    )
                }
            }
            commandOffset += commandSize
        }

        guard let symbolTable else {
            throw MKLPatcherError.unsupported("The Mach-O symbol table was not found.")
        }
        guard let textSegment else {
            throw MKLPatcherError.unsupported("The Mach-O __TEXT segment was not found.")
        }

        for index in 0..<symbolTable.count {
            let entry = symbolTable.offset + index * 16
            let stringIndex = Int(try sliceData.u32LE(at: entry))
            guard stringIndex > 0 else { continue }
            let name = try sliceData.cString(at: symbolTable.strings + stringIndex)
            guard name == symbolName else { continue }

            let address = try sliceData.u64LE(at: entry + 8)
            guard address >= textSegment.address else {
                throw MKLPatcherError.malformed("The MKL symbol precedes the __TEXT segment.")
            }
            let relative = address - textSegment.address + textSegment.fileOffset
            guard relative <= UInt64(Int.max), Int(relative) + patchBytes.count <= slice.size else {
                throw MKLPatcherError.malformed("The MKL symbol resolves outside the x86_64 slice.")
            }
            return slice.offset + Int(relative)
        }

        throw MKLPatcherError.symbolAbsent
    }

    private static func x86Slice(in data: Data) throws -> (offset: Int, size: Int) {
        let magic = try data.u32BE(at: 0)
        if magic == 0xcafebabe || magic == 0xcafebabf {
            let is64 = magic == 0xcafebabf
            let count = Int(try data.u32BE(at: 4))
            let entrySize = is64 ? 32 : 20
            for index in 0..<count {
                let entry = 8 + index * entrySize
                let cpuType = try data.u32BE(at: entry)
                guard cpuType == 0x01000007 else { continue }
                let offset: UInt64
                let size: UInt64
                if is64 {
                    offset = try data.u64BE(at: entry + 8)
                    size = try data.u64BE(at: entry + 16)
                } else {
                    offset = UInt64(try data.u32BE(at: entry + 8))
                    size = UInt64(try data.u32BE(at: entry + 12))
                }
                guard offset <= UInt64(Int.max), size <= UInt64(Int.max) else {
                    throw MKLPatcherError.malformed("The x86_64 slice is too large.")
                }
                let intOffset = Int(offset)
                let intSize = Int(size)
                guard intOffset >= 0, intSize > 0, intOffset + intSize <= data.count else {
                    throw MKLPatcherError.malformed("The universal Mach-O has an invalid x86_64 slice.")
                }
                return (intOffset, intSize)
            }
            throw MKLPatcherError.unsupported("The Mach-O file has no x86_64 slice.")
        }

        guard (try? data.u32LE(at: 0)) == 0xfeedfacf else {
            throw MKLPatcherError.notMachO
        }
        let cpuType = try data.u32LE(at: 4)
        guard cpuType == 0x01000007 else {
            throw MKLPatcherError.unsupported("The Mach-O file has no x86_64 slice.")
        }
        return (0, data.count)
    }

    private static func hex(_ bytes: [UInt8]) -> String {
        bytes.map { String(format: "%02x", $0) }.joined()
    }
}

private extension Data {
    func checkedRange(at offset: Int, count: Int) throws -> Range<Int> {
        guard offset >= 0, count >= 0, offset <= self.count, count <= self.count - offset else {
            throw MKLPatcherError.malformed("The Mach-O file ended unexpectedly.")
        }
        return offset..<(offset + count)
    }

    func u32LE(at offset: Int) throws -> UInt32 {
        let bytes = self[try checkedRange(at: offset, count: 4)]
        return bytes.enumerated().reduce(0) { value, pair in
            value | UInt32(pair.element) << UInt32(pair.offset * 8)
        }
    }

    func u32BE(at offset: Int) throws -> UInt32 {
        let bytes = self[try checkedRange(at: offset, count: 4)]
        return bytes.reduce(0) { ($0 << 8) | UInt32($1) }
    }

    func u64LE(at offset: Int) throws -> UInt64 {
        let bytes = self[try checkedRange(at: offset, count: 8)]
        return bytes.enumerated().reduce(0) { value, pair in
            value | UInt64(pair.element) << UInt64(pair.offset * 8)
        }
    }

    func u64BE(at offset: Int) throws -> UInt64 {
        let bytes = self[try checkedRange(at: offset, count: 8)]
        return bytes.reduce(0) { ($0 << 8) | UInt64($1) }
    }

    func fixedString(at offset: Int, count: Int) throws -> String {
        let bytes = self[try checkedRange(at: offset, count: count)]
        let content = bytes.prefix { $0 != 0 }
        return String(decoding: content, as: UTF8.self)
    }

    func cString(at offset: Int) throws -> String {
        guard offset >= 0, offset < count else {
            throw MKLPatcherError.malformed("A Mach-O string-table offset is invalid.")
        }
        var end = offset
        while end < count, self[end] != 0 {
            end += 1
        }
        guard end < count else {
            throw MKLPatcherError.malformed("A Mach-O string is not terminated.")
        }
        return String(decoding: self[offset..<end], as: UTF8.self)
    }
}
