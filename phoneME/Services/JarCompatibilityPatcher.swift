import CryptoKit
import Foundation

enum JarCompatibilityPatcher {
    private static let classesRequiringPreverification: [String: Set<String>] = [
        // VQSV1.jar: h.class contains stale CLDC StackMap entries after the
        // game was modified. Re-running this class through the embedded
        // preverifier regenerates its frames without touching unrelated code.
        "h.class": [
            "d4fbf900bea192a056ec2dfc300cd8046f6c7f92fa288aa96fde2fc4511b8c24"
        ]
    ]

    private struct StackMapPatch {
        let methodName: String
        let descriptor: String
        let payload: Data
    }

    private struct ClassPatch {
        let path: String
        let sha256: String
        let arrayClassNameIndex: UInt16
        let stackMaps: [StackMapPatch]
    }

    private static let patches: [ClassPatch] = [
        ClassPatch(
            path: "zm.class",
            sha256: "c2ae100f5499b878aacfa4f13047268be13a74484bdfedccecbacb3c76cd773b",
            arrayClassNameIndex: 46,
            stackMaps: [
                map("a", "(JJLjava/lang/Object;)Lwt;", "AAMACgADBAQHAIgAAAALAAMEBAcAiAABAQAwAAYEBAcAiAcAMgcAMgcAMgAA"),
                map("c", "(J)Lwt;", "AAIAJQACBAEAAAA+AAIEAQAA"),
                map("a", "(J)J", "AAEAOQAEBwBTBAQEAAA="),
                map("a", "(Lwt;)V", "AAIAEgACBwBTBwAyAAAAHAACBwBTBwAyAAA="),
                map("equals", "(Ljava/lang/Object;)Z", "AAMABwACBwBTBwCIAAAAPQADBwBTBwCIBwBTAAAAPwACBwBTBwCIAAA="),
                map("b", "(Lwt;)Z", "AAMABwACBwBTBwAyAAAAQQADBwBTBwAyBwBTAAAAQwACBwBTBwAyAAA="),
                map("a", "(JII[I[J)J", "AAcADgAIBAEBBwBCBwjABAEBAAAANQAKBAEBBwBCBwjABAEBBAEAAABFAAoEAQEHAEIHCMAEAQEEAQAAAEwACgQBAQcAQgcIwAQBAQQBAAAATwAIBAEBBwBCBwjABAEBAAAAcgAKBAEBBwBCBwjABAEBBAEAAACIAAsEAQEHAEIHCMAEAQEEAQEAAA=="),
                map("c", "()V", "AAIAFQADAQcAMwEAAAAjAAMBBwAzAQAA"),
                map("a", "(IILjava/util/Vector;Ljava/util/Vector;I)V", "AAIALwAGAQEHADMHADMBAQAAADgABQEBBwAzBwAzAQAA"),
                map("a", "(IIILjava/util/Vector;Ljava/util/Vector;)V", "AAgADgAIAQEBBwAzBwAzAQEBAAAAHgAIAQEBBwAzBwAzAQEBAAAAJwAIAQEBBwAzBwAzAQEBAAAAVQAIAQEBBwAzBwAzAQEBAAAAZAAJAQEBBwAzBwAzAQEBBwAyAAAAbgAIAQEBBwAzBwAzAQEBAAAAfQAIAQEBBwAzBwAzAQEBAAAAjwAIAQEBBwAzBwAzAQEBAAA="),
                map("<clinit>", "()V", "AAIAGwACBAEAAAAxAAIEAQAA")
            ]
        ),
        ClassPatch(
            path: "wd.class",
            sha256: "f1064c6bc4a164e696cef510ef64a64fcc396c39e2464acfb779ab59a270e494",
            arrayClassNameIndex: 27,
            stackMaps: [
                map("b", "(Lwt;Lwt;)Lwt;", "AAEAEgAEBwBIBwAtBwAtBwAtAAA="),
                map("a", "(Lwt;)V", "AAIAEgACBwBIBwAtAAAAHAACBwBIBwAtAAA="),
                map("a", "(J)J", "AAEANgAFBwBIBAcALQQHM4AAAQcASA=="),
                map("b", "(Lwt;)Z", "AAMABwACBwBIBwAtAAAAHAACBwBIBwAtAAAAHgACBwBIBwAtAAA=")
            ]
        )
    ]

    static func requiresTargetedCompatibilityRefresh(
        at sourceURL: URL
    ) -> Bool {
        do {
            let archive = try CompatibilityZipArchive(
                data: Data(contentsOf: sourceURL)
            )
            return try !targetedPreverificationClassPaths(
                in: archive
            ).isEmpty
        } catch {
            return false
        }
    }

    /// Writes a normalized JAR when a known malformed class is present.
    /// Returns false without creating the destination when no patch applies.
    static func writeNormalizedJar(from sourceURL: URL, to destinationURL: URL) throws -> Bool {
#if DEBUG
        if ProcessInfo.processInfo.environment["PHONEME_DISABLE_COMPATIBILITY"] == "1" {
            return false
        }
#endif
        let archive = try CompatibilityZipArchive(data: Data(contentsOf: sourceURL))
        var replacements: [String: Data] = [:]

        for patch in patches {
            guard let original = try archive.data(forExactPath: patch.path),
                  sha256(original) == patch.sha256 else {
                continue
            }
            replacements[patch.path] = try ClassFileStackMapInjector.inject(
                into: original,
                arrayClassNameIndex: patch.arrayClassNameIndex,
                patches: patch.stackMaps
            )
        }

        if false, let patchSet = try BinaryClassPatchSet.loadFromBundle() {
            let generated = try patchSet.replacements(
                in: archive,
                overlay: replacements
            )
            replacements.merge(generated) { _, generatedValue in generatedValue }
        }

        if let classPaths = try compatibilityPreverificationClassPaths(
            in: archive
        ) {
            let generated = try preverifyCompatibilityClasses(
                sourceURL: sourceURL,
                classPaths: classPaths
            )
            replacements.merge(generated) { explicitPatch, _ in explicitPatch }
        }

        for path in archive.classPaths {
            let original: Data
            if let replacement = replacements[path] {
                original = replacement
            } else if let archivedClass = try archive.data(forExactPath: path) {
                original = archivedClass
            } else {
                continue
            }
            if let normalizedClass = try ClassFileVersionNormalizer.normalize(original) {
                replacements[path] = normalizedClass
            }
        }

        guard !replacements.isEmpty else { return false }
        let normalized = try archive.replacing(replacements)
        try normalized.write(to: destinationURL, options: .atomic)
        return true
    }

    private static func compatibilityPreverificationClassPaths(
        in archive: CompatibilityZipArchive
    ) throws -> [String]? {
        let matchingPaths = try targetedPreverificationClassPaths(
            in: archive
        )
        return matchingPaths.isEmpty ? nil : matchingPaths
    }

    private static func targetedPreverificationClassPaths(
        in archive: CompatibilityZipArchive
    ) throws -> [String] {
        var matchingPaths = Set<String>()

        for (path, knownHashes) in classesRequiringPreverification {
            guard let classData = try archive.data(forExactPath: path) else {
                continue
            }
            if knownHashes.contains(sha256(classData)) {
                matchingPaths.insert(path)
            }
        }

        // Java 1.2-1.4 class files are bytecode-compatible with phoneME,
        // but CLDC requires legacy StackMap attributes. Only preverify class
        // files that have not already been CLDC-preverified. This avoids
        // rewriting valid mixed-version suites such as Avatar 2.9.3 while
        // repairing merged launchers whose Java 47 classes have no frames.
        for path in archive.classPaths {
            guard let classData = try archive.data(forExactPath: path) else {
                continue
            }
            if try ClassFileVersionNormalizer.requiresCLDCPreverification(
                classData
            ) {
                matchingPaths.insert(path)
            }
        }

        return archive.classPaths.filter { matchingPaths.contains($0) }
    }

    private static func preverifyCompatibilityClasses(
        sourceURL: URL,
        classPaths: [String]
    ) throws -> [String: Data] {
        let fileManager = FileManager.default
        let workingURL = fileManager.temporaryDirectory.appendingPathComponent(
            "phoneme-preverify-\(UUID().uuidString)",
            isDirectory: true
        )
        let outputURL = workingURL.appendingPathComponent("classes", isDirectory: true)
        let classListURL = workingURL.appendingPathComponent("classes.txt")

        try fileManager.createDirectory(
            at: outputURL,
            withIntermediateDirectories: true
        )
        defer {
            try? fileManager.removeItem(at: workingURL)
        }

        let classList = classPaths.joined(separator: "\n") + "\n"
        try classList.write(to: classListURL, atomically: true, encoding: .utf8)

        var result = PhoneMEPreverifyResult()
        let status = sourceURL.path.withCString { jarPath in
            classListURL.path.withCString { classListPath in
                outputURL.path.withCString { outputPath in
                    phoneme_preverify_jar_classes(
                        nil,
                        jarPath,
                        classListPath,
                        outputPath,
                        &result
                    )
                }
            }
        }
        guard status == 0 else {
            throw PatchError.preverificationFailed(status)
        }
        guard Int(result.attempted) == classPaths.count,
              Int(result.succeeded) == classPaths.count,
              result.failed == 0,
              result.skipped == 0 else {
            throw PatchError.preverificationIncomplete(
                attempted: result.attempted,
                succeeded: result.succeeded,
                failed: result.failed,
                skipped: result.skipped
            )
        }

        var replacements: [String: Data] = [:]
        replacements.reserveCapacity(Int(result.succeeded))
        for (index, classPath) in classPaths.enumerated() {
            let alias = String(format: "%08lx.class", UInt(index))
            let fileURL = outputURL.appendingPathComponent(alias, isDirectory: false)
            guard fileManager.fileExists(atPath: fileURL.path) else { continue }
            replacements[classPath] = try Data(
                contentsOf: fileURL,
                options: .mappedIfSafe
            )
        }

        guard replacements.count == classPaths.count else {
            throw PatchError.preverificationOutputMissing(
                expected: classPaths.count,
                actual: replacements.count
            )
        }

        print(
            "[JarCompatibilityPatcher] compatibility preverifier: "
                + "attempted=\(result.attempted) "
                + "succeeded=\(result.succeeded) "
                + "failed=\(result.failed) "
                + "skipped=\(result.skipped) "
                + "replacements=\(replacements.count)"
        )
        return replacements
    }

    private static func map(_ name: String, _ descriptor: String, _ base64: String) -> StackMapPatch {
        StackMapPatch(
            methodName: name,
            descriptor: descriptor,
            payload: Data(base64Encoded: base64) ?? Data()
        )
    }

    private static func sha256(_ data: Data) -> String {
        SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
    }

    private struct BinaryClassPatchSet {
        private static let maximumPayloadSize = 16 * 1_024 * 1_024
        private static let maximumClassSize = 2 * 1_024 * 1_024
        private static let maximumPatchCount = 2_048
        private static let maximumOperationCount = 250_000

        let patches: [String: BinaryClassPatch]

        static func loadFromBundle() throws -> BinaryClassPatchSet? {
#if DEBUG
            let overrideURL = ProcessInfo.processInfo.environment[
                "PHONEME_PATCH_RESOURCE_PATH"
            ].flatMap { path in
                path.isEmpty ? nil : URL(fileURLWithPath: path)
            }
#else
            let overrideURL: URL? = nil
#endif
            guard let url = overrideURL ?? Bundle.main.url(
                forResource: "AvatarLAPROStackMaps",
                withExtension: "pmpatch"
            ) else {
                return nil
            }

            let container = try Data(contentsOf: url, options: .mappedIfSafe)
            var containerReader = BinaryPatchReader(container)
            guard try containerReader.readMagic() == "PMZ1" else {
                throw PatchError.invalidPatch
            }

            let payloadSize = try containerReader.readInt32Length(
                maximum: maximumPayloadSize
            )
            let compressed = try containerReader.readRemainingData()
            guard !compressed.isEmpty else { throw PatchError.invalidPatch }

            var payload = Data(count: payloadSize)
            let status = compressed.withUnsafeBytes { sourceBuffer in
                payload.withUnsafeMutableBytes { destinationBuffer in
                    guard let source = sourceBuffer.bindMemory(to: UInt8.self).baseAddress,
                          let destination = destinationBuffer.bindMemory(to: UInt8.self).baseAddress else {
                        return Int32(-1)
                    }
                    return Int32(phoneme_inflate_zlib(
                        source,
                        compressed.count,
                        destination,
                        payloadSize
                    ))
                }
            }
            guard status == 0 else { throw PatchError.decompressionFailed }

            var reader = BinaryPatchReader(payload)
            guard try reader.readMagic() == "PMP1" else {
                throw PatchError.invalidPatch
            }
            let patchCount = try reader.readInt32Length(maximum: maximumPatchCount)
            var parsed: [String: BinaryClassPatch] = [:]
            parsed.reserveCapacity(patchCount)

            for _ in 0..<patchCount {
                let nameLength = Int(try reader.readUInt16BE())
                guard nameLength > 0 else { throw PatchError.invalidPatch }
                let nameData = try reader.readData(count: nameLength)
                guard let name = String(data: nameData, encoding: .utf8),
                      name.hasSuffix(".class"),
                      parsed[name] == nil else {
                    throw PatchError.invalidPatch
                }

                let sourceHash = try reader.readData(count: SHA256.byteCount)
                let targetHash = try reader.readData(count: SHA256.byteCount)
                let targetSize = try reader.readInt32Length(maximum: maximumClassSize)
                let operationCount = try reader.readInt32Length(
                    maximum: maximumOperationCount
                )
                let operationStart = reader.offset

                for _ in 0..<operationCount {
                    switch try reader.readUInt8() {
                    case 0:
                        _ = try reader.readUInt32BE()
                        _ = try reader.readUInt32BE()
                    case 1:
                        let length = try reader.readInt32Length(
                            maximum: maximumClassSize
                        )
                        try reader.skip(count: length)
                    default:
                        throw PatchError.invalidPatch
                    }
                }

                let operations = payload.subdata(in: operationStart..<reader.offset)
                parsed[name] = BinaryClassPatch(
                    sourceHash: sourceHash,
                    targetHash: targetHash,
                    targetSize: targetSize,
                    operationCount: operationCount,
                    operations: operations
                )
            }

            guard reader.isAtEnd else { throw PatchError.invalidPatch }
            return BinaryClassPatchSet(patches: parsed)
        }

        func replacements(
            in archive: CompatibilityZipArchive,
            overlay: [String: Data]
        ) throws -> [String: Data] {
            var result: [String: Data] = [:]
            result.reserveCapacity(patches.count)

            for (path, patch) in patches {
                let current: Data
                if let overlaid = overlay[path] {
                    current = overlaid
                } else if let archived = try archive.data(forExactPath: path) {
                    current = archived
                } else {
                    continue
                }

                if let patched = try patch.apply(to: current) {
                    result[path] = patched
                }
            }
            return result
        }
    }

    private struct BinaryClassPatch {
        let sourceHash: Data
        let targetHash: Data
        let targetSize: Int
        let operationCount: Int
        let operations: Data

        func apply(to source: Data) throws -> Data? {
            let currentHash = Data(SHA256.hash(data: source))
            if currentHash == targetHash {
                return nil
            }
            guard currentHash == sourceHash else {
                return nil
            }

            var reader = BinaryPatchReader(operations)
            var result = Data()
            result.reserveCapacity(targetSize)

            for _ in 0..<operationCount {
                switch try reader.readUInt8() {
                case 0:
                    let sourceOffset = try reader.readInt32Length(
                        maximum: source.count
                    )
                    let length = try reader.readInt32Length(
                        maximum: source.count
                    )
                    guard sourceOffset <= source.count,
                          length <= source.count - sourceOffset else {
                        throw PatchError.invalidPatch
                    }
                    result.append(
                        source.subdata(in: sourceOffset..<(sourceOffset + length))
                    )
                case 1:
                    let length = try reader.readInt32Length(maximum: targetSize)
                    result.append(try reader.readData(count: length))
                default:
                    throw PatchError.invalidPatch
                }

                guard result.count <= targetSize else {
                    throw PatchError.invalidPatch
                }
            }

            guard reader.isAtEnd,
                  result.count == targetSize,
                  Data(SHA256.hash(data: result)) == targetHash else {
                throw PatchError.patchOutputMismatch
            }
            return result
        }
    }

    private struct BinaryPatchReader {
        let data: Data
        private(set) var offset = 0

        init(_ data: Data) {
            self.data = data
        }

        var isAtEnd: Bool { offset == data.count }

        mutating func readMagic() throws -> String {
            let bytes = try readData(count: 4)
            guard let value = String(data: bytes, encoding: .ascii) else {
                throw PatchError.invalidPatch
            }
            return value
        }

        mutating func readUInt8() throws -> UInt8 {
            guard offset < data.count else { throw PatchError.invalidPatch }
            defer { offset += 1 }
            return data[offset]
        }

        mutating func readUInt16BE() throws -> UInt16 {
            let bytes = try readData(count: 2)
            return (UInt16(bytes[bytes.startIndex]) << 8)
                | UInt16(bytes[bytes.startIndex + 1])
        }

        mutating func readUInt32BE() throws -> UInt32 {
            let bytes = try readData(count: 4)
            let start = bytes.startIndex
            return (UInt32(bytes[start]) << 24)
                | (UInt32(bytes[start + 1]) << 16)
                | (UInt32(bytes[start + 2]) << 8)
                | UInt32(bytes[start + 3])
        }

        mutating func readInt32Length(maximum: Int) throws -> Int {
            let value = Int(try readUInt32BE())
            guard value <= maximum else {
                throw PatchError.invalidPatch
            }
            return value
        }

        mutating func readData(count: Int) throws -> Data {
            guard count >= 0,
                  offset <= data.count,
                  count <= data.count - offset else {
                throw PatchError.invalidPatch
            }
            defer { offset += count }
            return data.subdata(in: offset..<(offset + count))
        }

        mutating func skip(count: Int) throws {
            guard count >= 0,
                  offset <= data.count,
                  count <= data.count - offset else {
                throw PatchError.invalidPatch
            }
            offset += count
        }

        mutating func readRemainingData() throws -> Data {
            try readData(count: data.count - offset)
        }
    }

    private enum ClassFileStackMapInjector {
        static func inject(
            into original: Data,
            arrayClassNameIndex: UInt16,
            patches: [StackMapPatch]
        ) throws -> Data {
            guard original.uint32BE(at: 0) == 0xCAFEBABE else {
                throw PatchError.invalidClassFile
            }

            let parsedPool = try parseConstantPool(original)
            guard let stackMapNameIndex = parsedPool.utf8.first(where: {
                $0.value == "StackMap"
            })?.key,
                  stackMapNameIndex <= Int(UInt16.max),
                  parsedPool.count < Int(UInt16.max),
                  (parsedPool.utf8[Int(arrayClassNameIndex)] == "[J"
                    || parsedPool.utf8[Int(arrayClassNameIndex)] == "[I") else {
                throw PatchError.invalidClassFile
            }

            var result = Data()
            result.append(original.subdata(in: 0..<8))
            result.appendUInt16BE(UInt16(parsedPool.count + 1))
            result.append(original.subdata(in: 10..<parsedPool.endOffset))
            result.append(7) // CONSTANT_Class
            result.appendUInt16BE(arrayClassNameIndex)

            var cursor = parsedPool.endOffset
            let classHeaderStart = cursor
            cursor += 6
            let interfaceCount = Int(original.uint16BE(at: cursor))
            cursor += 2 + interfaceCount * 2

            let fieldCount = Int(original.uint16BE(at: cursor))
            cursor += 2
            for _ in 0..<fieldCount {
                cursor = try skipMember(in: original, from: cursor)
            }

            let methodsCountOffset = cursor
            let methodCount = Int(original.uint16BE(at: cursor))
            cursor += 2
            result.append(original.subdata(in: classHeaderStart..<cursor))

            var remaining = Dictionary(
                uniqueKeysWithValues: patches.map { (MethodKey($0.methodName, $0.descriptor), $0.payload) }
            )

            for _ in 0..<methodCount {
                let methodStart = cursor
                guard cursor + 8 <= original.count else { throw PatchError.invalidClassFile }
                let nameIndex = Int(original.uint16BE(at: cursor + 2))
                let descriptorIndex = Int(original.uint16BE(at: cursor + 4))
                let attributeCount = Int(original.uint16BE(at: cursor + 6))
                guard let methodName = parsedPool.utf8[nameIndex],
                      let descriptor = parsedPool.utf8[descriptorIndex] else {
                    throw PatchError.invalidClassFile
                }
                cursor += 8

                result.append(original.subdata(in: methodStart..<(methodStart + 8)))
                let key = MethodKey(methodName, descriptor)
                let payload = remaining[key]

                for _ in 0..<attributeCount {
                    let attributeStart = cursor
                    guard cursor + 6 <= original.count else { throw PatchError.invalidClassFile }
                    let attributeNameIndex = Int(original.uint16BE(at: cursor))
                    let attributeLength = Int(original.uint32BE(at: cursor + 2))
                    let infoStart = cursor + 6
                    let attributeEnd = infoStart + attributeLength
                    guard attributeEnd <= original.count else { throw PatchError.invalidClassFile }

                    if let payload,
                       parsedPool.utf8[attributeNameIndex] == "Code" {
                        let patchedCode = try patchCodeAttribute(
                            original.subdata(in: infoStart..<attributeEnd),
                            stackMapNameIndex: UInt16(stackMapNameIndex),
                            payload: payload
                        )
                        result.appendUInt16BE(UInt16(attributeNameIndex))
                        result.appendUInt32BE(UInt32(patchedCode.count))
                        result.append(patchedCode)
                        remaining.removeValue(forKey: key)
                    } else {
                        result.append(original.subdata(in: attributeStart..<attributeEnd))
                    }
                    cursor = attributeEnd
                }
            }

            guard methodsCountOffset < cursor else { throw PatchError.invalidClassFile }
            guard remaining.isEmpty else {
                throw PatchError.patchTargetMissing
            }
            result.append(original.subdata(in: cursor..<original.count))
            return result
        }

        private static func patchCodeAttribute(
            _ code: Data,
            stackMapNameIndex: UInt16,
            payload: Data
        ) throws -> Data {
            guard code.count >= 12 else { throw PatchError.invalidClassFile }
            let bytecodeLength = Int(code.uint32BE(at: 4))
            var cursor = 8 + bytecodeLength
            guard cursor + 2 <= code.count else { throw PatchError.invalidClassFile }
            let exceptionCount = Int(code.uint16BE(at: cursor))
            cursor += 2 + exceptionCount * 8
            guard cursor + 2 <= code.count else { throw PatchError.invalidClassFile }

            let nestedCountOffset = cursor
            let nestedCount = code.uint16BE(at: cursor)
            cursor += 2

            var nestedCursor = cursor
            var attributes: [Range<Int>] = []
            var stackMapAttributeIndex: Int?
            attributes.reserveCapacity(Int(nestedCount))

            for index in 0..<Int(nestedCount) {
                guard nestedCursor + 6 <= code.count else { throw PatchError.invalidClassFile }
                let nameIndex = code.uint16BE(at: nestedCursor)
                let length = Int(code.uint32BE(at: nestedCursor + 2))
                let attributeEnd = nestedCursor + 6 + length
                guard attributeEnd <= code.count else { throw PatchError.invalidClassFile }
                attributes.append(nestedCursor..<attributeEnd)
                if nameIndex == stackMapNameIndex {
                    guard stackMapAttributeIndex == nil else {
                        throw PatchError.invalidClassFile
                    }
                    stackMapAttributeIndex = index
                }
                nestedCursor = attributeEnd
            }
            guard nestedCursor == code.count else {
                throw PatchError.invalidClassFile
            }

            var result = Data()
            result.append(code.subdata(in: 0..<nestedCountOffset))
            result.appendUInt16BE(stackMapAttributeIndex == nil ? nestedCount + 1 : nestedCount)

            for (index, range) in attributes.enumerated() {
                if index == stackMapAttributeIndex {
                    result.appendUInt16BE(stackMapNameIndex)
                    result.appendUInt32BE(UInt32(payload.count))
                    result.append(payload)
                } else {
                    result.append(code.subdata(in: range))
                }
            }

            if stackMapAttributeIndex == nil {
                result.appendUInt16BE(stackMapNameIndex)
                result.appendUInt32BE(UInt32(payload.count))
                result.append(payload)
            }
            return result
        }

        private static func parseConstantPool(_ data: Data) throws -> ConstantPoolInfo {
            let count = Int(data.uint16BE(at: 8))
            guard count > 0 else { throw PatchError.invalidClassFile }
            var utf8: [Int: String] = [:]
            var index = 1
            var cursor = 10

            while index < count {
                guard cursor < data.count else { throw PatchError.invalidClassFile }
                let tag = data[cursor]
                cursor += 1
                switch tag {
                case 1:
                    let length = Int(data.uint16BE(at: cursor))
                    cursor += 2
                    guard cursor + length <= data.count else { throw PatchError.invalidClassFile }
                    let valueData = data.subdata(in: cursor..<(cursor + length))
                    utf8[index] = String(data: valueData, encoding: .utf8)
                        ?? String(data: valueData, encoding: .isoLatin1)
                    cursor += length
                case 3, 4:
                    cursor += 4
                case 5, 6:
                    cursor += 8
                    index += 1
                case 7, 8, 16, 19, 20:
                    cursor += 2
                case 9, 10, 11, 12, 17, 18:
                    cursor += 4
                case 15:
                    cursor += 3
                default:
                    throw PatchError.invalidClassFile
                }
                guard cursor <= data.count else { throw PatchError.invalidClassFile }
                index += 1
            }
            return ConstantPoolInfo(count: count, endOffset: cursor, utf8: utf8)
        }

        private static func skipMember(in data: Data, from start: Int) throws -> Int {
            guard start + 8 <= data.count else { throw PatchError.invalidClassFile }
            let attributeCount = Int(data.uint16BE(at: start + 6))
            var cursor = start + 8
            for _ in 0..<attributeCount {
                guard cursor + 6 <= data.count else { throw PatchError.invalidClassFile }
                let length = Int(data.uint32BE(at: cursor + 2))
                cursor += 6 + length
                guard cursor <= data.count else { throw PatchError.invalidClassFile }
            }
            return cursor
        }

        private struct ConstantPoolInfo {
            let count: Int
            let endOffset: Int
            let utf8: [Int: String]
        }

        private struct MethodKey: Hashable {
            let name: String
            let descriptor: String

            init(_ name: String, _ descriptor: String) {
                self.name = name
                self.descriptor = descriptor
            }
        }
    }
}

/// Converts Java 5-8 class files that still use the classic JVM instruction
/// set into the CLDC class-file shape understood by phoneME. Modern constant
/// pool features (method handles/invokedynamic) are deliberately left alone so
/// the VM never attempts to execute a class it cannot safely interpret.
private enum ClassFileVersionNormalizer {
    private static let phoneMEMajorVersion = 48
    private static let newestConvertibleMajorVersion = 52
    private static let accStatic: UInt16 = 0x0008

    static func requiresCLDCPreverification(_ original: Data) throws -> Bool {
        guard original.count >= 10,
              original.uint32BE(at: 0) == 0xCAFEBABE else {
            throw PatchError.invalidClassFile
        }

        let minorVersion = original.uint16BE(at: 4)
        let majorVersion = original.uint16BE(at: 6)
        guard (majorVersion == 45 && minorVersion != 3)
                || (46...48).contains(majorVersion) else {
            return false
        }

        let pool = try parseConstantPool(original)
        let inspection = try inspectClass(original, pool: pool)
        return !inspection.hasLegacyStackMap
    }

    static func normalize(_ original: Data) throws -> Data? {
        guard original.count >= 10,
              original.uint32BE(at: 0) == 0xCAFEBABE else {
            throw PatchError.invalidClassFile
        }

        let majorVersion = Int(original.uint16BE(at: 6))
        guard majorVersion > phoneMEMajorVersion,
              majorVersion <= newestConvertibleMajorVersion else {
            return nil
        }

        let pool = try parseConstantPool(original)
        guard !pool.containsUnsupportedModernConstants else {
            return nil
        }

        let inspection = try inspectClass(original, pool: pool)
        var poolBuilder = ConstantPoolBuilder(pool: pool)
        var stackMapNameIndex: UInt16?

        if inspection.needsStackMapConversion {
            stackMapNameIndex = try poolBuilder.ensureUTF8("StackMap")
            for referenceName in inspection.descriptorReferenceNames.sorted() {
                _ = try poolBuilder.ensureClass(referenceName)
            }
        }

        var result = Data()
        result.append(original.subdata(in: 0..<4))
        result.appendUInt16BE(0)
        result.appendUInt16BE(UInt16(phoneMEMajorVersion))
        result.appendUInt16BE(try poolBuilder.finalCount())
        result.append(original.subdata(in: 10..<pool.endOffset))
        result.append(poolBuilder.addedEntries)
        result.append(original.subdata(in: pool.endOffset..<inspection.firstMethodOffset))

        var cursor = inspection.firstMethodOffset
        for _ in 0..<inspection.methodCount {
            guard cursor + 8 <= original.count else {
                throw PatchError.invalidClassFile
            }

            let methodStart = cursor
            let accessFlags = original.uint16BE(at: cursor)
            let nameIndex = Int(original.uint16BE(at: cursor + 2))
            let descriptorIndex = Int(original.uint16BE(at: cursor + 4))
            let attributeCount = Int(original.uint16BE(at: cursor + 6))
            guard let methodName = pool.utf8[nameIndex],
                  let descriptor = pool.utf8[descriptorIndex] else {
                throw PatchError.invalidClassFile
            }
            cursor += 8
            result.append(original.subdata(in: methodStart..<cursor))

            for _ in 0..<attributeCount {
                let attribute = try readAttribute(in: original, from: cursor, utf8: pool.utf8)
                if attribute.name == "Code",
                   let rewrittenCode = try rewriteCodeAttribute(
                       original.subdata(in: attribute.infoStart..<attribute.end),
                       methodName: methodName,
                       descriptor: descriptor,
                       accessFlags: accessFlags,
                       thisClassIndex: inspection.thisClassIndex,
                       classIndices: poolBuilder.classIndices,
                       utf8: pool.utf8,
                       stackMapNameIndex: stackMapNameIndex
                   ) {
                    result.appendUInt16BE(original.uint16BE(at: attribute.start))
                    result.appendUInt32BE(UInt32(rewrittenCode.count))
                    result.append(rewrittenCode)
                } else {
                    result.append(original.subdata(in: attribute.start..<attribute.end))
                }
                cursor = attribute.end
            }
        }

        result.append(original.subdata(in: cursor..<original.count))
        return result
    }

    private static func inspectClass(_ data: Data, pool: ConstantPoolInfo) throws -> ClassInspection {
        var cursor = pool.endOffset
        guard cursor + 8 <= data.count else { throw PatchError.invalidClassFile }

        let thisClassIndex = data.uint16BE(at: cursor + 2)
        cursor += 6
        let interfaceCount = Int(data.uint16BE(at: cursor))
        cursor += 2 + interfaceCount * 2
        guard cursor <= data.count else { throw PatchError.invalidClassFile }

        let fieldCount = Int(data.uint16BE(at: cursor))
        cursor += 2
        for _ in 0..<fieldCount {
            cursor = try skipMember(in: data, from: cursor)
        }

        let methodCount = Int(data.uint16BE(at: cursor))
        cursor += 2
        let firstMethodOffset = cursor
        var descriptorReferenceNames = Set<String>()
        var needsStackMapConversion = false
        var hasAnyLegacyStackMap = false

        for _ in 0..<methodCount {
            guard cursor + 8 <= data.count else { throw PatchError.invalidClassFile }
            let descriptorIndex = Int(data.uint16BE(at: cursor + 4))
            let attributeCount = Int(data.uint16BE(at: cursor + 6))
            guard let descriptor = pool.utf8[descriptorIndex] else {
                throw PatchError.invalidClassFile
            }
            cursor += 8

            var hasStackMapTable = false
            var hasLegacyStackMap = false
            for _ in 0..<attributeCount {
                let attribute = try readAttribute(in: data, from: cursor, utf8: pool.utf8)
                if attribute.name == "Code" {
                    let flags = try stackMapFlags(
                        in: data.subdata(in: attribute.infoStart..<attribute.end),
                        utf8: pool.utf8
                    )
                    hasStackMapTable = hasStackMapTable || flags.hasStackMapTable
                    hasLegacyStackMap = hasLegacyStackMap || flags.hasLegacyStackMap
                    hasAnyLegacyStackMap = hasAnyLegacyStackMap
                        || flags.hasLegacyStackMap
                }
                cursor = attribute.end
            }

            if hasStackMapTable && !hasLegacyStackMap {
                needsStackMapConversion = true
                descriptorReferenceNames.formUnion(try parameterTypes(in: descriptor).compactMap {
                    guard case let .reference(name) = $0 else { return nil }
                    return name
                })
            }
        }

        return ClassInspection(
            thisClassIndex: thisClassIndex,
            methodCount: methodCount,
            firstMethodOffset: firstMethodOffset,
            needsStackMapConversion: needsStackMapConversion,
            hasLegacyStackMap: hasAnyLegacyStackMap,
            descriptorReferenceNames: descriptorReferenceNames
        )
    }

    private static func rewriteCodeAttribute(
        _ code: Data,
        methodName: String,
        descriptor: String,
        accessFlags: UInt16,
        thisClassIndex: UInt16,
        classIndices: [String: UInt16],
        utf8: [Int: String],
        stackMapNameIndex: UInt16?
    ) throws -> Data? {
        let attributes = try nestedCodeAttributes(in: code, utf8: utf8)
        let hasStackMapTable = attributes.contains { $0.name == "StackMapTable" }
        guard hasStackMapTable else { return nil }

        let hasLegacyStackMap = attributes.contains { $0.name == "StackMap" }
        let initialLocals: [VerificationType]
        if hasLegacyStackMap {
            initialLocals = []
        } else {
            guard let stackMapNameIndex else { throw PatchError.invalidClassFile }
            _ = stackMapNameIndex
            initialLocals = try makeInitialLocals(
                methodName: methodName,
                descriptor: descriptor,
                accessFlags: accessFlags,
                thisClassIndex: thisClassIndex,
                classIndices: classIndices
            )
        }

        let nestedCountOffset = try nestedAttributeCountOffset(in: code)
        var nestedData = Data()
        var nestedCount: UInt16 = 0
        var emittedConvertedStackMap = false

        for attribute in attributes {
            if attribute.name == "StackMapTable" {
                guard !hasLegacyStackMap, !emittedConvertedStackMap else { continue }
                guard let stackMapNameIndex else { throw PatchError.invalidClassFile }
                let converted = try convertStackMapTable(
                    code.subdata(in: attribute.infoStart..<attribute.end),
                    initialLocals: initialLocals
                )
                nestedData.appendUInt16BE(stackMapNameIndex)
                nestedData.appendUInt32BE(UInt32(converted.count))
                nestedData.append(converted)
                nestedCount += 1
                emittedConvertedStackMap = true
            } else {
                nestedData.append(code.subdata(in: attribute.start..<attribute.end))
                nestedCount += 1
            }
        }

        var result = Data()
        result.append(code.subdata(in: 0..<nestedCountOffset))
        result.appendUInt16BE(nestedCount)
        result.append(nestedData)
        return result
    }

    private static func convertStackMapTable(
        _ table: Data,
        initialLocals: [VerificationType]
    ) throws -> Data {
        var cursor = 0
        let frameCount = Int(try readU2(table, cursor: &cursor))
        var locals = initialLocals
        var previousOffset = -1
        var outputFrames = Data()

        for _ in 0..<frameCount {
            let frameType = Int(try readU1(table, cursor: &cursor))
            let offsetDelta: Int
            var stack: [VerificationType] = []

            switch frameType {
            case 0...63:
                offsetDelta = frameType
            case 64...127:
                offsetDelta = frameType - 64
                stack = [try readVerificationType(table, cursor: &cursor)]
            case 247:
                offsetDelta = Int(try readU2(table, cursor: &cursor))
                stack = [try readVerificationType(table, cursor: &cursor)]
            case 248...250:
                offsetDelta = Int(try readU2(table, cursor: &cursor))
                let removedCount = 251 - frameType
                guard locals.count >= removedCount else { throw PatchError.invalidClassFile }
                locals.removeLast(removedCount)
            case 251:
                offsetDelta = Int(try readU2(table, cursor: &cursor))
            case 252...254:
                offsetDelta = Int(try readU2(table, cursor: &cursor))
                for _ in 0..<(frameType - 251) {
                    locals.append(try readVerificationType(table, cursor: &cursor))
                }
            case 255:
                offsetDelta = Int(try readU2(table, cursor: &cursor))
                let localCount = Int(try readU2(table, cursor: &cursor))
                locals.removeAll(keepingCapacity: true)
                for _ in 0..<localCount {
                    locals.append(try readVerificationType(table, cursor: &cursor))
                }
                let stackCount = Int(try readU2(table, cursor: &cursor))
                stack.reserveCapacity(stackCount)
                for _ in 0..<stackCount {
                    stack.append(try readVerificationType(table, cursor: &cursor))
                }
            default:
                throw PatchError.invalidClassFile
            }

            let bytecodeOffset = previousOffset + offsetDelta + 1
            guard bytecodeOffset >= 0, bytecodeOffset <= Int(UInt16.max),
                  locals.count <= Int(UInt16.max), stack.count <= Int(UInt16.max) else {
                throw PatchError.invalidClassFile
            }
            previousOffset = bytecodeOffset

            outputFrames.appendUInt16BE(UInt16(bytecodeOffset))
            outputFrames.appendUInt16BE(UInt16(locals.count))
            for type in locals { type.appendEncoded(to: &outputFrames) }
            outputFrames.appendUInt16BE(UInt16(stack.count))
            for type in stack { type.appendEncoded(to: &outputFrames) }
        }

        guard cursor == table.count else { throw PatchError.invalidClassFile }
        var result = Data()
        result.appendUInt16BE(UInt16(frameCount))
        result.append(outputFrames)
        return result
    }

    private static func makeInitialLocals(
        methodName: String,
        descriptor: String,
        accessFlags: UInt16,
        thisClassIndex: UInt16,
        classIndices: [String: UInt16]
    ) throws -> [VerificationType] {
        var locals: [VerificationType] = []
        if (accessFlags & accStatic) == 0 {
            locals.append(methodName == "<init>" ? .simple(6) : .object(thisClassIndex))
        }

        for parameter in try parameterTypes(in: descriptor) {
            switch parameter {
            case .integer:
                locals.append(.simple(1))
            case .float:
                locals.append(.simple(2))
            case .double:
                locals.append(.simple(3))
            case .long:
                locals.append(.simple(4))
            case let .reference(name):
                guard let index = classIndices[name] else { throw PatchError.invalidClassFile }
                locals.append(.object(index))
            }
        }
        return locals
    }

    private static func parameterTypes(in descriptor: String) throws -> [ParameterType] {
        let bytes = Array(descriptor.utf8)
        guard bytes.first == 0x28 else {
            throw PatchError.invalidClassFile
        }
        var cursor = 1
        var result: [ParameterType] = []

        while cursor < bytes.count, bytes[cursor] != 0x29 {
            switch bytes[cursor] {
            case 0x42, 0x43, 0x49, 0x53, 0x5A:
                result.append(.integer)
                cursor += 1
            case 0x46:
                result.append(.float)
                cursor += 1
            case 0x44:
                result.append(.double)
                cursor += 1
            case 0x4A:
                result.append(.long)
                cursor += 1
            case 0x4C:
                let nameStart = cursor + 1
                guard let end = bytes[nameStart...].firstIndex(of: 0x3B),
                      end > nameStart else {
                    throw PatchError.invalidClassFile
                }
                result.append(.reference(String(decoding: bytes[nameStart..<end], as: UTF8.self)))
                cursor = end + 1
            case 0x5B:
                let nameStart = cursor
                repeat { cursor += 1 } while cursor < bytes.count && bytes[cursor] == 0x5B
                guard cursor < bytes.count else { throw PatchError.invalidClassFile }
                if bytes[cursor] == 0x4C {
                    guard let end = bytes[(cursor + 1)...].firstIndex(of: 0x3B) else {
                        throw PatchError.invalidClassFile
                    }
                    cursor = end + 1
                } else {
                    guard "BCDFIJSZ".utf8.contains(bytes[cursor]) else {
                        throw PatchError.invalidClassFile
                    }
                    cursor += 1
                }
                result.append(.reference(String(decoding: bytes[nameStart..<cursor], as: UTF8.self)))
            default:
                throw PatchError.invalidClassFile
            }
        }

        guard cursor < bytes.count, bytes[cursor] == 0x29 else {
            throw PatchError.invalidClassFile
        }
        return result
    }

    private static func stackMapFlags(in code: Data, utf8: [Int: String]) throws -> StackMapFlags {
        let attributes = try nestedCodeAttributes(in: code, utf8: utf8)
        return StackMapFlags(
            hasStackMapTable: attributes.contains { $0.name == "StackMapTable" },
            hasLegacyStackMap: attributes.contains { $0.name == "StackMap" }
        )
    }

    private static func nestedCodeAttributes(in code: Data, utf8: [Int: String]) throws -> [AttributeSlice] {
        let nestedCountOffset = try nestedAttributeCountOffset(in: code)
        let nestedCount = Int(code.uint16BE(at: nestedCountOffset))
        var cursor = nestedCountOffset + 2
        var result: [AttributeSlice] = []
        result.reserveCapacity(nestedCount)

        for _ in 0..<nestedCount {
            let attribute = try readAttribute(in: code, from: cursor, utf8: utf8)
            result.append(attribute)
            cursor = attribute.end
        }
        guard cursor == code.count else { throw PatchError.invalidClassFile }
        return result
    }

    private static func nestedAttributeCountOffset(in code: Data) throws -> Int {
        guard code.count >= 12 else { throw PatchError.invalidClassFile }
        let bytecodeLength = Int(code.uint32BE(at: 4))
        var cursor = 8 + bytecodeLength
        guard cursor + 2 <= code.count else { throw PatchError.invalidClassFile }
        let exceptionCount = Int(code.uint16BE(at: cursor))
        cursor += 2 + exceptionCount * 8
        guard cursor + 2 <= code.count else { throw PatchError.invalidClassFile }
        return cursor
    }

    private static func readAttribute(
        in data: Data,
        from start: Int,
        utf8: [Int: String]
    ) throws -> AttributeSlice {
        guard start + 6 <= data.count else { throw PatchError.invalidClassFile }
        let nameIndex = Int(data.uint16BE(at: start))
        let length = Int(data.uint32BE(at: start + 2))
        let infoStart = start + 6
        let end = infoStart + length
        guard end <= data.count, let name = utf8[nameIndex] else {
            throw PatchError.invalidClassFile
        }
        return AttributeSlice(start: start, infoStart: infoStart, end: end, name: name)
    }

    private static func skipMember(in data: Data, from start: Int) throws -> Int {
        guard start + 8 <= data.count else { throw PatchError.invalidClassFile }
        let attributeCount = Int(data.uint16BE(at: start + 6))
        var cursor = start + 8
        for _ in 0..<attributeCount {
            guard cursor + 6 <= data.count else { throw PatchError.invalidClassFile }
            let length = Int(data.uint32BE(at: cursor + 2))
            cursor += 6 + length
            guard cursor <= data.count else { throw PatchError.invalidClassFile }
        }
        return cursor
    }

    private static func parseConstantPool(_ data: Data) throws -> ConstantPoolInfo {
        let count = Int(data.uint16BE(at: 8))
        guard count > 0 else { throw PatchError.invalidClassFile }
        var utf8: [Int: String] = [:]
        var classNameIndices: [Int: Int] = [:]
        var containsUnsupportedModernConstants = false
        var index = 1
        var cursor = 10

        while index < count {
            guard cursor < data.count else { throw PatchError.invalidClassFile }
            let tag = data[cursor]
            cursor += 1
            switch tag {
            case 1:
                let length = Int(data.uint16BE(at: cursor))
                cursor += 2
                guard cursor + length <= data.count else { throw PatchError.invalidClassFile }
                let valueData = data.subdata(in: cursor..<(cursor + length))
                utf8[index] = String(data: valueData, encoding: .utf8)
                    ?? String(data: valueData, encoding: .isoLatin1)
                cursor += length
            case 3, 4:
                cursor += 4
            case 5, 6:
                cursor += 8
                index += 1
            case 7:
                classNameIndices[index] = Int(data.uint16BE(at: cursor))
                cursor += 2
            case 8:
                cursor += 2
            case 9, 10, 11, 12:
                cursor += 4
            case 15:
                containsUnsupportedModernConstants = true
                cursor += 3
            case 16, 19, 20:
                containsUnsupportedModernConstants = true
                cursor += 2
            case 17, 18:
                containsUnsupportedModernConstants = true
                cursor += 4
            default:
                throw PatchError.invalidClassFile
            }
            guard cursor <= data.count else { throw PatchError.invalidClassFile }
            index += 1
        }

        var classIndices: [String: UInt16] = [:]
        for (classIndex, nameIndex) in classNameIndices {
            if let name = utf8[nameIndex], classIndices[name] == nil {
                classIndices[name] = UInt16(classIndex)
            }
        }
        var utf8Indices: [String: UInt16] = [:]
        for (utf8Index, value) in utf8 where utf8Indices[value] == nil {
            utf8Indices[value] = UInt16(utf8Index)
        }

        return ConstantPoolInfo(
            count: count,
            endOffset: cursor,
            utf8: utf8,
            utf8Indices: utf8Indices,
            classIndices: classIndices,
            containsUnsupportedModernConstants: containsUnsupportedModernConstants
        )
    }

    private static func readVerificationType(_ data: Data, cursor: inout Int) throws -> VerificationType {
        let tag = try readU1(data, cursor: &cursor)
        switch tag {
        case 0...6:
            return .simple(tag)
        case 7:
            return .object(try readU2(data, cursor: &cursor))
        case 8:
            return .uninitialized(try readU2(data, cursor: &cursor))
        default:
            throw PatchError.invalidClassFile
        }
    }

    private static func readU1(_ data: Data, cursor: inout Int) throws -> UInt8 {
        guard cursor < data.count else { throw PatchError.invalidClassFile }
        defer { cursor += 1 }
        return data[cursor]
    }

    private static func readU2(_ data: Data, cursor: inout Int) throws -> UInt16 {
        guard cursor + 2 <= data.count else { throw PatchError.invalidClassFile }
        defer { cursor += 2 }
        return data.uint16BE(at: cursor)
    }

    private struct ConstantPoolBuilder {
        private var nextIndex: Int
        private(set) var addedEntries = Data()
        private var utf8Indices: [String: UInt16]
        private(set) var classIndices: [String: UInt16]

        init(pool: ConstantPoolInfo) {
            nextIndex = pool.count
            utf8Indices = pool.utf8Indices
            classIndices = pool.classIndices
        }

        mutating func ensureUTF8(_ value: String) throws -> UInt16 {
            if let existing = utf8Indices[value] { return existing }
            let bytes = Data(value.utf8)
            guard bytes.count <= Int(UInt16.max), nextIndex < Int(UInt16.max) else {
                throw PatchError.invalidClassFile
            }
            let index = UInt16(nextIndex)
            nextIndex += 1
            addedEntries.append(1)
            addedEntries.appendUInt16BE(UInt16(bytes.count))
            addedEntries.append(bytes)
            utf8Indices[value] = index
            return index
        }

        mutating func ensureClass(_ name: String) throws -> UInt16 {
            if let existing = classIndices[name] { return existing }
            let nameIndex = try ensureUTF8(name)
            guard nextIndex < Int(UInt16.max) else { throw PatchError.invalidClassFile }
            let index = UInt16(nextIndex)
            nextIndex += 1
            addedEntries.append(7)
            addedEntries.appendUInt16BE(nameIndex)
            classIndices[name] = index
            return index
        }

        func finalCount() throws -> UInt16 {
            guard nextIndex <= Int(UInt16.max) else { throw PatchError.invalidClassFile }
            return UInt16(nextIndex)
        }
    }

    private enum VerificationType {
        case simple(UInt8)
        case object(UInt16)
        case uninitialized(UInt16)

        func appendEncoded(to data: inout Data) {
            switch self {
            case let .simple(tag):
                data.append(tag)
            case let .object(index):
                data.append(7)
                data.appendUInt16BE(index)
            case let .uninitialized(offset):
                data.append(8)
                data.appendUInt16BE(offset)
            }
        }
    }

    private enum ParameterType {
        case integer
        case float
        case double
        case long
        case reference(String)
    }

    private struct ConstantPoolInfo {
        let count: Int
        let endOffset: Int
        let utf8: [Int: String]
        let utf8Indices: [String: UInt16]
        let classIndices: [String: UInt16]
        let containsUnsupportedModernConstants: Bool
    }

    private struct ClassInspection {
        let thisClassIndex: UInt16
        let methodCount: Int
        let firstMethodOffset: Int
        let needsStackMapConversion: Bool
        let hasLegacyStackMap: Bool
        let descriptorReferenceNames: Set<String>
    }

    private struct AttributeSlice {
        let start: Int
        let infoStart: Int
        let end: Int
        let name: String
    }

    private struct StackMapFlags {
        let hasStackMapTable: Bool
        let hasLegacyStackMap: Bool
    }
}

private struct CompatibilityZipArchive {
    private struct Entry {
        let name: String
        let method: UInt16
        let compressedSize: Int
        let uncompressedSize: Int
        let localHeaderOffset: Int
    }

    private let archiveData: Data
    private let entries: [Entry]

    init(data: Data) throws {
        archiveData = data
        entries = try Self.readEntries(from: data)
    }

    var classPaths: [String] {
        entries.map(\.name).filter { $0.lowercased().hasSuffix(".class") }
    }

    func data(forExactPath path: String) throws -> Data? {
        guard let entry = entries.first(where: { $0.name == path }) else { return nil }
        return try data(for: entry)
    }

    func replacing(_ replacements: [String: Data]) throws -> Data {
        var materialized: [(name: String, data: Data)] = []
        materialized.reserveCapacity(entries.count)
        for entry in entries {
            let content: Data
            if let replacement = replacements[entry.name] {
                content = replacement
            } else {
                content = try data(for: entry)
            }
            materialized.append((entry.name, content))
        }
        return try StoredZipWriter.write(materialized)
    }

    private func data(for entry: Entry) throws -> Data {
        let offset = entry.localHeaderOffset
        guard archiveData.uint32LE(at: offset) == 0x04034B50 else {
            throw PatchError.invalidArchive
        }
        let nameLength = Int(archiveData.uint16LE(at: offset + 26))
        let extraLength = Int(archiveData.uint16LE(at: offset + 28))
        let start = offset + 30 + nameLength + extraLength
        let end = start + entry.compressedSize
        guard start >= 0, end <= archiveData.count else { throw PatchError.invalidArchive }

        let compressed = archiveData.subdata(in: start..<end)
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
            guard status == 0 else { throw PatchError.decompressionFailed }
            return output
        default:
            throw PatchError.unsupportedCompression
        }
    }

    private static func readEntries(from data: Data) throws -> [Entry] {
        guard data.count >= 22 else { throw PatchError.invalidArchive }
        let searchStart = max(0, data.count - 65_557)
        var endOffset: Int?
        var cursor = data.count - 22
        while cursor >= searchStart {
            if data.uint32LE(at: cursor) == 0x06054B50 {
                endOffset = cursor
                break
            }
            cursor -= 1
        }
        guard let endOffset else { throw PatchError.invalidArchive }

        let entryCount = Int(data.uint16LE(at: endOffset + 10))
        var centralOffset = Int(data.uint32LE(at: endOffset + 16))
        var result: [Entry] = []
        result.reserveCapacity(entryCount)

        for _ in 0..<entryCount {
            guard data.uint32LE(at: centralOffset) == 0x02014B50 else {
                throw PatchError.invalidArchive
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
            guard nameEnd <= data.count else { throw PatchError.invalidArchive }

            let nameData = data.subdata(in: nameStart..<nameEnd)
            guard let name = String(data: nameData, encoding: .utf8)
                ?? String(data: nameData, encoding: .isoLatin1) else {
                throw PatchError.invalidArchive
            }
            result.append(Entry(
                name: name,
                method: method,
                compressedSize: compressedSize,
                uncompressedSize: uncompressedSize,
                localHeaderOffset: localOffset
            ))
            centralOffset = nameEnd + extraLength + commentLength
        }
        return result
    }
}

private enum StoredZipWriter {
    static func write(_ entries: [(name: String, data: Data)]) throws -> Data {
        guard entries.count <= Int(UInt16.max) else { throw PatchError.archiveTooLarge }
        var output = Data()
        var central = Data()

        for entry in entries {
            guard let nameData = entry.name.data(using: .utf8),
                  nameData.count <= Int(UInt16.max),
                  entry.data.count <= Int(UInt32.max),
                  output.count <= Int(UInt32.max) else {
                throw PatchError.archiveTooLarge
            }
            let offset = UInt32(output.count)
            let crc = entry.data.withUnsafeBytes { buffer -> UInt32 in
                guard let address = buffer.bindMemory(to: UInt8.self).baseAddress else { return 0 }
                return phoneme_crc32(address, entry.data.count)
            }
            let size = UInt32(entry.data.count)
            let flags: UInt16 = 0x0800

            output.appendUInt32LE(0x04034B50)
            output.appendUInt16LE(20)
            output.appendUInt16LE(flags)
            output.appendUInt16LE(0)
            output.appendUInt16LE(0)
            output.appendUInt16LE(0)
            output.appendUInt32LE(crc)
            output.appendUInt32LE(size)
            output.appendUInt32LE(size)
            output.appendUInt16LE(UInt16(nameData.count))
            output.appendUInt16LE(0)
            output.append(nameData)
            output.append(entry.data)

            central.appendUInt32LE(0x02014B50)
            central.appendUInt16LE(20)
            central.appendUInt16LE(20)
            central.appendUInt16LE(flags)
            central.appendUInt16LE(0)
            central.appendUInt16LE(0)
            central.appendUInt16LE(0)
            central.appendUInt32LE(crc)
            central.appendUInt32LE(size)
            central.appendUInt32LE(size)
            central.appendUInt16LE(UInt16(nameData.count))
            central.appendUInt16LE(0)
            central.appendUInt16LE(0)
            central.appendUInt16LE(0)
            central.appendUInt16LE(0)
            central.appendUInt32LE(0)
            central.appendUInt32LE(offset)
            central.append(nameData)
        }

        guard output.count <= Int(UInt32.max), central.count <= Int(UInt32.max) else {
            throw PatchError.archiveTooLarge
        }
        let centralOffset = UInt32(output.count)
        output.append(central)
        output.appendUInt32LE(0x06054B50)
        output.appendUInt16LE(0)
        output.appendUInt16LE(0)
        output.appendUInt16LE(UInt16(entries.count))
        output.appendUInt16LE(UInt16(entries.count))
        output.appendUInt32LE(UInt32(central.count))
        output.appendUInt32LE(centralOffset)
        output.appendUInt16LE(0)
        return output
    }
}

private enum PatchError: Error {
    case invalidArchive
    case preverificationFailed(Int32)
    case preverificationIncomplete(
        attempted: Int32,
        succeeded: Int32,
        failed: Int32,
        skipped: Int32
    )
    case preverificationOutputMissing(expected: Int, actual: Int)
    case invalidClassFile
    case invalidPatch
    case unsupportedCompression
    case decompressionFailed
    case archiveTooLarge
    case patchTargetMissing
    case patchOutputMismatch
}

private extension Data {
    func uint16BE(at offset: Int) -> UInt16 {
        guard offset >= 0, offset + 2 <= count else { return 0 }
        return (UInt16(self[offset]) << 8) | UInt16(self[offset + 1])
    }

    func uint32BE(at offset: Int) -> UInt32 {
        guard offset >= 0, offset + 4 <= count else { return 0 }
        return (UInt32(self[offset]) << 24)
            | (UInt32(self[offset + 1]) << 16)
            | (UInt32(self[offset + 2]) << 8)
            | UInt32(self[offset + 3])
    }

    func uint16LE(at offset: Int) -> UInt16 {
        guard offset >= 0, offset + 2 <= count else { return 0 }
        return UInt16(self[offset]) | (UInt16(self[offset + 1]) << 8)
    }

    func uint32LE(at offset: Int) -> UInt32 {
        guard offset >= 0, offset + 4 <= count else { return 0 }
        return UInt32(self[offset])
            | (UInt32(self[offset + 1]) << 8)
            | (UInt32(self[offset + 2]) << 16)
            | (UInt32(self[offset + 3]) << 24)
    }

    mutating func appendUInt16BE(_ value: UInt16) {
        append(UInt8((value >> 8) & 0xFF))
        append(UInt8(value & 0xFF))
    }

    mutating func appendUInt32BE(_ value: UInt32) {
        append(UInt8((value >> 24) & 0xFF))
        append(UInt8((value >> 16) & 0xFF))
        append(UInt8((value >> 8) & 0xFF))
        append(UInt8(value & 0xFF))
    }

    mutating func appendUInt16LE(_ value: UInt16) {
        append(UInt8(value & 0xFF))
        append(UInt8((value >> 8) & 0xFF))
    }

    mutating func appendUInt32LE(_ value: UInt32) {
        append(UInt8(value & 0xFF))
        append(UInt8((value >> 8) & 0xFF))
        append(UInt8((value >> 16) & 0xFF))
        append(UInt8((value >> 24) & 0xFF))
    }
}
