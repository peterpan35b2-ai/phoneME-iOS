import Foundation

struct JarMetadata {
    var title: String?
    var vendor: String?
    var version: String?
    var mainClass: String?
    var iconData: Data?
    var iconExtension: String?
}

enum JarMetadataReader {
    static func read(from url: URL) throws -> JarMetadata {
        let archive = try ZipArchiveReader(data: Data(contentsOf: url))
        guard let manifestData = try archive.data(for: "META-INF/MANIFEST.MF"),
              let manifest = String(data: manifestData, encoding: .utf8)
                ?? String(data: manifestData, encoding: .isoLatin1) else {
            return JarMetadata()
        }

        let attributes = parseManifest(manifest)
        let midlet = attributes["MIDlet-1"]?.split(separator: ",", omittingEmptySubsequences: false)
            .map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }

        let title = nonEmpty(attributes["MIDlet-Name"]) ?? midlet?.first.flatMap(nonEmpty)
        let vendor = nonEmpty(attributes["MIDlet-Vendor"])
        let version = nonEmpty(attributes["MIDlet-Version"])
        let mainClass = midlet?.dropFirst(2).first.flatMap(nonEmpty)

        var iconData: Data?
        var iconExtension: String?
        if let rawIconPath = midlet?.dropFirst().first.flatMap(nonEmpty) {
            let iconPath = rawIconPath.hasPrefix("/") ? String(rawIconPath.dropFirst()) : rawIconPath
            iconData = try archive.data(for: iconPath)
            iconExtension = URL(fileURLWithPath: iconPath).pathExtension.lowercased()
            if iconExtension?.isEmpty == true { iconExtension = "png" }
        }

        return JarMetadata(
            title: title,
            vendor: vendor,
            version: version,
            mainClass: mainClass,
            iconData: iconData,
            iconExtension: iconExtension
        )
    }

    private static func parseManifest(_ text: String) -> [String: String] {
        let lines = text
            .replacingOccurrences(of: "\r\n", with: "\n")
            .replacingOccurrences(of: "\r", with: "\n")
            .split(separator: "\n", omittingEmptySubsequences: false)

        var result: [String: String] = [:]
        var currentKey: String?

        for rawLine in lines {
            let line = String(rawLine)
            if line.hasPrefix(" "), let key = currentKey {
                result[key, default: ""] += String(line.dropFirst())
                continue
            }

            guard let separator = line.firstIndex(of: ":") else {
                currentKey = nil
                continue
            }

            let key = String(line[..<separator])
            let valueStart = line.index(after: separator)
            let value = line[valueStart...].trimmingCharacters(in: .whitespaces)
            result[key] = value
            currentKey = key
        }
        return result
    }

    private static func nonEmpty(_ value: String?) -> String? {
        guard let value else { return nil }
        let trimmed = value.trimmingCharacters(in: .whitespacesAndNewlines)
        return trimmed.isEmpty ? nil : trimmed
    }
}

private struct ZipArchiveReader {
    private struct Entry {
        let method: UInt16
        let compressedSize: Int
        let uncompressedSize: Int
        let localHeaderOffset: Int
    }

    private let data: Data
    private let entries: [String: Entry]

    init(data: Data) throws {
        self.data = data
        self.entries = try Self.readEntries(from: data)
    }

    func data(for path: String) throws -> Data? {
        guard let entry = entries[path.lowercased()] else { return nil }
        let offset = entry.localHeaderOffset
        guard data.uint32LE(at: offset) == 0x04034B50 else { throw ZipError.invalidArchive }

        let nameLength = Int(data.uint16LE(at: offset + 26))
        let extraLength = Int(data.uint16LE(at: offset + 28))
        let start = offset + 30 + nameLength + extraLength
        let end = start + entry.compressedSize
        guard start >= 0, end <= data.count else { throw ZipError.invalidArchive }

        let compressed = data.subdata(in: start..<end)
        switch entry.method {
        case 0:
            return compressed
        case 8:
            var output = Data(count: entry.uncompressedSize)
            let status = compressed.withUnsafeBytes { sourceBuffer in
                output.withUnsafeMutableBytes { destinationBuffer in
                    guard let source = sourceBuffer.bindMemory(to: UInt8.self).baseAddress,
                          let destination = destinationBuffer.bindMemory(to: UInt8.self).baseAddress else {
                        return Int32(-1)
                    }
                    return Int32(phoneme_inflate_raw(
                        source,
                        compressed.count,
                        destination,
                        entry.uncompressedSize
                    ))
                }
            }
            guard status == 0 else { throw ZipError.decompressionFailed }
            return output
        default:
            throw ZipError.unsupportedCompression
        }
    }

    private static func readEntries(from data: Data) throws -> [String: Entry] {
        guard data.count >= 22 else { throw ZipError.invalidArchive }
        let searchStart = max(0, data.count - 65_557)
        var eocdOffset: Int?
        var cursor = data.count - 22

        while cursor >= searchStart {
            if data.uint32LE(at: cursor) == 0x06054B50 {
                eocdOffset = cursor
                break
            }
            cursor -= 1
        }

        guard let eocdOffset else { throw ZipError.invalidArchive }
        let entryCount = Int(data.uint16LE(at: eocdOffset + 10))
        var centralOffset = Int(data.uint32LE(at: eocdOffset + 16))
        var entries: [String: Entry] = [:]

        for _ in 0..<entryCount {
            guard data.uint32LE(at: centralOffset) == 0x02014B50 else {
                throw ZipError.invalidArchive
            }

            let method = data.uint16LE(at: centralOffset + 10)
            let compressedSize = Int(data.uint32LE(at: centralOffset + 20))
            let uncompressedSize = Int(data.uint32LE(at: centralOffset + 24))
            let nameLength = Int(data.uint16LE(at: centralOffset + 28))
            let extraLength = Int(data.uint16LE(at: centralOffset + 30))
            let commentLength = Int(data.uint16LE(at: centralOffset + 32))
            let localOffset = Int(data.uint32LE(at: centralOffset + 42))
            let nameStart = centralOffset + 46
            let nameEnd = nameStart + nameLength

            guard nameEnd <= data.count,
                  let name = String(data: data.subdata(in: nameStart..<nameEnd), encoding: .utf8) else {
                throw ZipError.invalidArchive
            }

            entries[name.lowercased()] = Entry(
                method: method,
                compressedSize: compressedSize,
                uncompressedSize: uncompressedSize,
                localHeaderOffset: localOffset
            )
            centralOffset = nameEnd + extraLength + commentLength
        }
        return entries
    }
}

private enum ZipError: Error {
    case invalidArchive
    case unsupportedCompression
    case decompressionFailed
}

private extension Data {
    func uint16LE(at offset: Int) -> UInt16 {
        guard offset >= 0, offset + 2 <= count else { return 0 }
        return withUnsafeBytes { buffer in
            let bytes = buffer.bindMemory(to: UInt8.self)
            return UInt16(bytes[offset]) | (UInt16(bytes[offset + 1]) << 8)
        }
    }

    func uint32LE(at offset: Int) -> UInt32 {
        guard offset >= 0, offset + 4 <= count else { return 0 }
        return withUnsafeBytes { buffer in
            let bytes = buffer.bindMemory(to: UInt8.self)
            return UInt32(bytes[offset])
                | (UInt32(bytes[offset + 1]) << 8)
                | (UInt32(bytes[offset + 2]) << 16)
                | (UInt32(bytes[offset + 3]) << 24)
        }
    }
}
