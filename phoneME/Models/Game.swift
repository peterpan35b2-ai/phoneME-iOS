import Foundation

struct Game: Identifiable, Codable, Hashable {
    let id: UUID
    var title: String
    var vendor: String
    var version: String
    var mainClass: String
    var fileName: String
    var iconFileName: String?
    var importedAt: Date
    var lastPlayedAt: Date?
    var playCount: Int

    init(
        id: UUID = UUID(),
        title: String,
        vendor: String = "",
        version: String = "",
        mainClass: String = "",
        fileName: String,
        iconFileName: String? = nil,
        importedAt: Date = Date(),
        lastPlayedAt: Date? = nil,
        playCount: Int = 0
    ) {
        self.id = id
        self.title = title
        self.vendor = vendor
        self.version = version
        self.mainClass = mainClass
        self.fileName = fileName
        self.iconFileName = iconFileName
        self.importedAt = importedAt
        self.lastPlayedAt = lastPlayedAt
        self.playCount = playCount
    }

    private enum CodingKeys: String, CodingKey {
        case id
        case title
        case vendor
        case version
        case mainClass
        case fileName
        case iconFileName
        case importedAt
        case lastPlayedAt
        case playCount
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        id = try container.decode(UUID.self, forKey: .id)
        title = try container.decode(String.self, forKey: .title)
        vendor = try container.decodeIfPresent(String.self, forKey: .vendor) ?? ""
        version = try container.decodeIfPresent(String.self, forKey: .version) ?? ""
        mainClass = try container.decodeIfPresent(String.self, forKey: .mainClass) ?? ""
        fileName = try container.decode(String.self, forKey: .fileName)
        iconFileName = try container.decodeIfPresent(String.self, forKey: .iconFileName)
        importedAt = try container.decode(Date.self, forKey: .importedAt)
        lastPlayedAt = try container.decodeIfPresent(Date.self, forKey: .lastPlayedAt)
        playCount = try container.decodeIfPresent(Int.self, forKey: .playCount) ?? 0
    }
}
