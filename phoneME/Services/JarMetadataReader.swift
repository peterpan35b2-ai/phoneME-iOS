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
    private static let supportedProfiles = Set([
        "MIDP-1.0", "MIDP-2.0", "MIDP-2.1"
    ])
    private static let supportedConfigurations = Set([
        "CLDC-1.0", "CLDC-1.1", "CLDC-1.1.1"
    ])

    static func read(from url: URL) throws -> JarMetadata {
        let archive = try ZipArchiveReader(data: Data(contentsOf: url))
        guard let manifestData = try archive.data(for: "META-INF/MANIFEST.MF") else {
            throw JarMetadataError.missingManifest
        }
        guard let manifest = String(data: manifestData, encoding: .utf8)
            ?? String(data: manifestData, encoding: .isoLatin1) else {
            throw JarMetadataError.unreadableManifest
        }

        let attributes = parseManifest(manifest)
        try validateRequiredAttributes(attributes)
        try validateVersion(attributes["MIDlet-Version"])
        try validateCapability(
            attributes["MicroEdition-Profile"],
            supported: supportedProfiles,
            familyPrefix: "MIDP-",
            attribute: "MicroEdition-Profile"
        )
        try validateCapability(
            attributes["MicroEdition-Configuration"],
            supported: supportedConfigurations,
            familyPrefix: "CLDC-",
            attribute: "MicroEdition-Configuration"
        )

        let midlets = try validateMIDletDeclarations(
            attributes,
            archive: archive
        )
        guard let firstMIDlet = midlets.first else {
            throw JarMetadataError.missingMIDlet1
        }

        let fields = firstMIDlet.declaration
            .split(separator: ",", maxSplits: 2, omittingEmptySubsequences: false)
            .map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }
        let title = nonEmpty(attributes["MIDlet-Name"])
            ?? fields.first.flatMap(nonEmpty)
        let vendor = nonEmpty(attributes["MIDlet-Vendor"])
        let version = nonEmpty(attributes["MIDlet-Version"])

        var iconData: Data?
        var iconExtension: String?
        if fields.count == 3, let rawIconPath = nonEmpty(fields[1]) {
            let iconPath = rawIconPath.hasPrefix("/")
                ? String(rawIconPath.dropFirst())
                : rawIconPath
            iconData = try archive.data(for: iconPath)
            iconExtension = URL(fileURLWithPath: iconPath)
                .pathExtension
                .lowercased()
            if iconExtension?.isEmpty == true { iconExtension = "png" }
        }

        return JarMetadata(
            title: title,
            vendor: vendor,
            version: version,
            mainClass: firstMIDlet.mainClass,
            iconData: iconData,
            iconExtension: iconExtension
        )
    }

    private struct MIDletDeclaration {
        let index: Int
        let declaration: String
        let mainClass: String
    }

    private static func validateRequiredAttributes(
        _ attributes: [String: String]
    ) throws {
        for attribute in [
            "MIDlet-Name",
            "MIDlet-Vendor",
            "MIDlet-Version",
            "MicroEdition-Profile",
            "MicroEdition-Configuration"
        ] where nonEmpty(attributes[attribute]) == nil {
            throw JarMetadataError.missingRequiredAttribute(attribute)
        }
    }

    private static func validateVersion(_ value: String?) throws {
        guard let value = nonEmpty(value) else {
            throw JarMetadataError.missingRequiredAttribute("MIDlet-Version")
        }
        let components = value.split(
            separator: ".",
            omittingEmptySubsequences: false
        )
        guard !components.isEmpty,
              components.count <= 64,
              components.allSatisfy({ component in
                  !component.isEmpty
                      && component.allSatisfy { $0.isNumber }
                      && UInt32(component) != nil
              }) else {
            throw JarMetadataError.invalidVersion(value)
        }
    }

    private static func validateCapability(
        _ value: String?,
        supported: Set<String>,
        familyPrefix: String,
        attribute: String
    ) throws {
        guard let value = nonEmpty(value) else {
            throw JarMetadataError.missingRequiredAttribute(attribute)
        }
        let tokens = value.split { $0.isWhitespace }.map(String.init)
        guard tokens.contains(where: { $0.hasPrefix(familyPrefix) }) else {
            throw JarMetadataError.invalidCapability(attribute, value)
        }
        guard tokens.contains(where: supported.contains) else {
            throw JarMetadataError.unsupportedCapability(attribute, value)
        }
    }

    private static func validateMIDletDeclarations(
        _ attributes: [String: String],
        archive: ZipArchiveReader
    ) throws -> [MIDletDeclaration] {
        var declarations: [MIDletDeclaration] = []

        for (key, declaration) in attributes {
            guard let index = midletIndex(for: key) else { continue }
            guard let separator = declaration.lastIndex(of: ",") else {
                throw JarMetadataError.invalidMIDletDeclaration(key)
            }
            let mainClass = declaration[declaration.index(after: separator)...]
                .trimmingCharacters(in: .whitespacesAndNewlines)
            guard isValidBinaryClassName(mainClass) else {
                throw JarMetadataError.invalidMIDletDeclaration(key)
            }

            let classPath = mainClass
                .replacingOccurrences(of: ".", with: "/") + ".class"
            guard let classData = try archive.data(for: classPath) else {
                throw JarMetadataError.missingMIDletClass(mainClass)
            }
            guard classData.count >= 4,
                  classData.prefix(4) == Data([0xCA, 0xFE, 0xBA, 0xBE]) else {
                throw JarMetadataError.invalidMIDletClass(mainClass)
            }

            declarations.append(MIDletDeclaration(
                index: index,
                declaration: declaration,
                mainClass: mainClass
            ))
        }

        guard !declarations.isEmpty else {
            throw JarMetadataError.missingMIDlet1
        }
        declarations.sort { $0.index < $1.index }
        for (offset, declaration) in declarations.enumerated()
            where declaration.index != offset + 1 {
            throw JarMetadataError.nonContiguousMIDletDeclarations
        }
        return declarations
    }

    private static func midletIndex(for key: String) -> Int? {
        let prefix = "MIDlet-"
        guard key.hasPrefix(prefix) else { return nil }
        let suffix = key.dropFirst(prefix.count)
        guard !suffix.isEmpty,
              suffix.allSatisfy({ $0.isNumber }),
              let index = Int(suffix),
              index > 0 else {
            return nil
        }
        return index
    }

    private static func isValidBinaryClassName(_ value: String) -> Bool {
        guard !value.isEmpty,
              value.first != ".",
              value.last != "." else {
            return false
        }

        for component in value.split(
            separator: ".",
            omittingEmptySubsequences: false
        ) {
            guard let first = component.first,
                  first.isASCII,
                  first.isLetter || first == "_" || first == "$" else {
                return false
            }
            guard component.dropFirst().allSatisfy({ character in
                character.isASCII
                    && (character.isLetter
                        || character.isNumber
                        || character == "_"
                        || character == "$")
            }) else {
                return false
            }
        }
        return true
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
            if line.isEmpty {
                break
            }
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

enum JarMetadataError: LocalizedError {
    case missingManifest
    case unreadableManifest
    case missingRequiredAttribute(String)
    case invalidVersion(String)
    case invalidCapability(String, String)
    case unsupportedCapability(String, String)
    case missingMIDlet1
    case invalidMIDletDeclaration(String)
    case nonContiguousMIDletDeclarations
    case missingMIDletClass(String)
    case invalidMIDletClass(String)

    var errorDescription: String? {
        switch self {
        case .missingManifest:
            return L10n.string("The JAR does not contain META-INF/MANIFEST.MF.")
        case .unreadableManifest:
            return L10n.string("The JAR manifest cannot be read.")
        case .missingRequiredAttribute(let attribute):
            return L10n.format(
                "The JAR manifest is missing the required %@ attribute.",
                attribute
            )
        case .invalidVersion(let version):
            return L10n.format(
                "The JAR declares an invalid MIDlet-Version: %@.",
                version
            )
        case .invalidCapability(let attribute, let value):
            return L10n.format(
                "The JAR declares an invalid %@ value: %@.",
                attribute,
                value
            )
        case .unsupportedCapability(let attribute, let value):
            return L10n.format(
                "The JAR requires an unsupported %@ value: %@.",
                attribute,
                value
            )
        case .missingMIDlet1:
            return L10n.string(
                "The JAR manifest does not contain a valid MIDlet-1 class."
            )
        case .invalidMIDletDeclaration(let attribute):
            return L10n.format(
                "The JAR manifest contains an invalid %@ declaration.",
                attribute
            )
        case .nonContiguousMIDletDeclarations:
            return L10n.string(
                "MIDlet declarations must start at MIDlet-1 and use consecutive numbers."
            )
        case .missingMIDletClass(let mainClass):
            return L10n.format(
                "The JAR does not contain the declared MIDlet class %@.",
                mainClass
            )
        case .invalidMIDletClass(let mainClass):
            return L10n.format(
                "The declared MIDlet class %@ is not a valid Java class file.",
                mainClass
            )
        }
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
        guard data.uint32LE(at: offset) == 0x04034B50 else {
            throw ZipError.invalidArchive
        }

        let nameLength = Int(data.uint16LE(at: offset + 26))
        let extraLength = Int(data.uint16LE(at: offset + 28))
        let start = offset + 30 + nameLength + extraLength
        let end = start + entry.compressedSize
        guard start >= 0, end <= data.count else {
            throw ZipError.invalidArchive
        }

        let compressed = data.subdata(in: start..<end)
        switch entry.method {
        case 0:
            return compressed
        case 8:
            var output = Data(count: entry.uncompressedSize)
            let status = compressed.withUnsafeBytes { sourceBuffer in
                output.withUnsafeMutableBytes { destinationBuffer in
                    guard let source = sourceBuffer
                        .bindMemory(to: UInt8.self)
                        .baseAddress,
                          let destination = destinationBuffer
                        .bindMemory(to: UInt8.self)
                        .baseAddress else {
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
                  let name = String(
                      data: data.subdata(in: nameStart..<nameEnd),
                      encoding: .utf8
                  ) else {
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

private enum ZipError: LocalizedError {
    case invalidArchive
    case unsupportedCompression
    case decompressionFailed

    var errorDescription: String? {
        switch self {
        case .invalidArchive:
            return L10n.string("The selected file is not a valid JAR archive.")
        case .unsupportedCompression:
            return L10n.string(
                "The JAR uses a compression method that is not supported."
            )
        case .decompressionFailed:
            return L10n.string("The JAR could not be decompressed.")
        }
    }
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
