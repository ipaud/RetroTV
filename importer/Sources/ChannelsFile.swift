import Foundation

// Adds a local channel to /retrotv/config/channels.json. Every other byte of the file stays as it was:
// people edit it by hand and the TV rewrites it too. The number is the first free one after the highest
// local channel, so series stay together before the TV's own pages (teletext 8, MANDO, MENSAJES...); the
// first free one from 1 when 999 is reached. Limits from src/channels/ChannelManager.h.
enum ChannelsFile {
  static let maxChannels = 48
  static let idMaxLength = 23  // CHANNEL_ID_LEN - 1
  static let nameMaxLength = 23  // CHANNEL_NAME_LEN - 1, what the TV shows
  static let numberMax = 999
  static let mediaRoot = "/retrotv/media"

  /// What a card that never was in the TV starts from: only the TV's own pages. The firmware's default list
  /// (DEFAULT_CHANNELS_JSON) also has a demo clip and two empty channels, which a new card does not have.
  static let freshText = """
    {
      "channels": [
        { "id": "teletext", "number": 8, "name": "TELETEXT", "type": "internal",
          "source": "teletext", "enabled": true },
        { "id": "testcard", "number": 9, "name": "CARTA DE AJUSTE", "type": "internal",
          "source": "testcard", "enabled": true }
      ]
    }

    """

  struct Failure: Error, CustomStringConvertible {
    let description: String
  }

  struct Added {
    let text: String
    let id: String
    let number: Int
    var source: String { "\(mediaRoot)/\(id)" }
  }

  /// The channel objects of a channels.json, or nil when it is not one.
  static func channels(in text: String) -> [[String: Any]]? {
    guard let root = try? JSONSerialization.jsonObject(with: Data(text.utf8)) as? [String: Any],
          let list = root["channels"] as? [Any] else { return nil }
    return list.compactMap { $0 as? [String: Any] }
  }

  static func addLocal(name rawName: String, to text: String) throws -> Added {
    guard let list = channels(in: text) else {
      throw Failure(description: "channels.json no se entiende (no es JSON o no tiene \"channels\")")
    }
    guard list.count < maxChannels else {
      throw Failure(description: "La tele admite \(maxChannels) canales y ya hay \(list.count)")
    }
    let name = String(rawName.trimmingCharacters(in: .whitespacesAndNewlines).prefix(nameMaxLength))
    guard !name.isEmpty else { throw Failure(description: "El canal necesita un nombre") }

    let numbers = Set(list.compactMap { $0["number"] as? Int }.filter { (0...numberMax).contains($0) })
    let locals = list.filter { $0["type"] as? String == "local" }.compactMap { $0["number"] as? Int }
    var number = (locals.filter { (0...numberMax).contains($0) }.max() ?? 0) + 1
    while numbers.contains(number) { number += 1 }
    if number > numberMax {
      guard let free = (1...numberMax).first(where: { !numbers.contains($0) }) else {
        throw Failure(description: "No queda ningún número de canal libre")
      }
      number = free
    }
    // Taken: every id, every folder another channel plays from (the id names the new folder), and
    // "mensajes", the firmware's: a channel with that id stops it adding MENSAJES. A folder no channel
    // points to is free: it is what an interrupted import leaves, and converting again resumes it.
    let folders = list.compactMap { ($0["source"] as? String)?.split(separator: "/").map(String.init) }
      .filter { $0.count >= 3 && $0[0] == "retrotv" && $0[1] == "media" }.map { $0[2] }
    let id = uniqueId(for: name, taken: Set(list.compactMap { $0["id"] as? String } + folders + ["mensajes"]))

    let entry = "{ \"id\": \(quoted(id)), \"number\": \(number), \"name\": \(quoted(name)), \"type\": \"local\", "
      + "\"source\": \(quoted("\(mediaRoot)/\(id)")), \"enabled\": true }"
    let bytes = Array(text.utf8)
    guard let (open, close) = channelsArray(in: bytes) else {
      throw Failure(description: "channels.json: no encuentro la lista \"channels\"")
    }
    var last = close - 1
    while last > open && [0x20, 0x09, 0x0A, 0x0D].contains(bytes[last]) { last -= 1 }
    let out: [UInt8] = last == open  // empty list
      ? Array(bytes[...open]) + Array("\n    \(entry)\n  ".utf8) + Array(bytes[close...])
      : Array(bytes[...last]) + Array(",\n    \(entry)".utf8) + Array(bytes[(last + 1)...])
    let result = String(decoding: out, as: UTF8.self)
    guard channels(in: result)?.count == list.count + 1 else {  // never write a file the TV cannot read
      throw Failure(description: "channels.json: no he podido añadir el canal sin romperlo")
    }
    return Added(text: result, id: id, number: number)
  }

  /// Byte positions of the "[" and "]" of the top-level "channels" array, skipping strings.
  static func channelsArray(in b: [UInt8]) -> (open: Int, close: Int)? {
    var depth = 0
    var key: String?
    var open: Int?
    var i = 0
    while i < b.count {
      switch b[i] {
      case UInt8(ascii: "\""):
        let start = i + 1
        i += 1
        while i < b.count && b[i] != UInt8(ascii: "\"") { i += b[i] == UInt8(ascii: "\\") ? 2 : 1 }
        if depth == 1 { key = String(decoding: b[start..<min(i, b.count)], as: UTF8.self) }
      case UInt8(ascii: "{"), UInt8(ascii: "["):
        depth += 1
        if b[i] == UInt8(ascii: "[") && depth == 2 && open == nil && key == "channels" { open = i }
      case UInt8(ascii: "]") where depth == 2 && open != nil:
        return (open!, i)
      case UInt8(ascii: "}"), UInt8(ascii: "]"):
        depth -= 1
      case UInt8(ascii: ",") where depth == 1:
        key = nil
      default:
        break
      }
      i += 1
    }
    return nil
  }

  /// A channel id from its name: ASCII lowercase letters, digits and "_", unique, at most 23 characters.
  /// It also names the channel's folder and its logo.
  static func uniqueId(for name: String, taken: Set<String>) -> String {
    var base = name.decomposedStringWithCompatibilityMapping.unicodeScalars
      .filter { $0.properties.generalCategory != .nonspacingMark }
      .map { Character($0) }
      .reduce(into: "") { $0.append($1) }
      .lowercased()
      .replacingOccurrences(of: "[^a-z0-9]+", with: "_", options: .regularExpression)
      .trimmingCharacters(in: CharacterSet(charactersIn: "_"))
    if base.isEmpty { base = "canal" }
    var id = String(base.prefix(idMaxLength))
    var n = 2
    while taken.contains(id) {
      let suffix = "_\(n)"
      id = String(base.prefix(idMaxLength - suffix.count)) + suffix
      n += 1
    }
    return id
  }

  private static func quoted(_ s: String) -> String {
    let data = try? JSONSerialization.data(withJSONObject: s, options: [.fragmentsAllowed, .withoutEscapingSlashes])
    return data.map { String(decoding: $0, as: UTF8.self) } ?? "\"\""
  }
}
