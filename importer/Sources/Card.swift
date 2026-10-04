import Foundation

/// The RETROTV card: the volume this app runs from, or else a mounted volume with a /retrotv folder.
struct Card {
  let root: URL
  let name: String
  let freeBytes: Int64
  let totalBytes: Int64
  let fsType: String  // "msdos" (FAT32, what the TV reads), "exfat", "apfs"...
  let removable: Bool
  let writable: Bool

  static let lockedMessage = "La tarjeta está protegida contra escritura: sácala, mueve la pestaña del lateral y vuelve a meterla."

  var channelsURL: URL { root.appendingPathComponent("retrotv/config/channels.json") }

  private static let keys: Set<URLResourceKey> = [
    .volumeNameKey, .volumeAvailableCapacityKey, .volumeTotalCapacityKey, .volumeIsRemovableKey, .volumeIsEjectableKey,
  ]

  var isExFAT: Bool { fsType == "exfat" }

  /// The volume the app runs from when it can be the card, else a mounted card with /retrotv on it, else the
  /// one removable exFAT volume (a new card, with the app opened from Downloads). Any other volume, only when
  /// chosen by hand: a USB stick is never written to by surprise.
  static func find() -> Card? {
    if let own = try? Bundle.main.bundleURL.resourceValues(forKeys: [.volumeURLKey]).volume, let card = at(own) {
      return card
    }
    let mounted = FileManager.default.mountedVolumeURLs(includingResourceValuesForKeys: nil, options: [.skipHiddenVolumes]) ?? []
    let cards = mounted.compactMap(at)
    let new = cards.filter { $0.isExFAT && $0.removable && !$0.hasRetroTV }
    return cards.first { $0.hasRetroTV } ?? (new.count == 1 ? new[0] : nil)
  }

  /// A volume that can be the card: under /Volumes (never the Mac's own disk, nor the read-only copy macOS
  /// runs a downloaded app from, in /private/var/folders), and FAT or exFAT or already with a /retrotv folder
  /// (not the app's disk image, nor some other external disk). A locked card is one too, read-only.
  static func at(_ url: URL) -> Card? {
    var fs = statfs()
    guard url.path.hasPrefix("/Volumes/"), statfs(url.path, &fs) == 0,
          let v = try? url.resourceValues(forKeys: keys) else { return nil }
    let type = withUnsafeBytes(of: fs.f_fstypename) { String(decoding: $0.prefix { $0 != 0 }, as: UTF8.self) }
    let card = Card(root: url, name: v.volumeName ?? url.lastPathComponent, freeBytes: Int64(v.volumeAvailableCapacity ?? 0),
                    totalBytes: Int64(v.volumeTotalCapacity ?? 0), fsType: type,
                    removable: v.volumeIsRemovable == true || v.volumeIsEjectable == true,
                    writable: FileManager.default.isWritableFile(atPath: url.path))
    return type == "msdos" || type == "exfat" || card.hasRetroTV ? card : nil
  }

  var hasRetroTV: Bool { FileManager.default.fileExists(atPath: root.appendingPathComponent("retrotv").path) }

  /// A channel as the app lists it. `folder` is set for local channels that play a folder on this card:
  /// those can take more episodes.
  struct Channel: Identifiable, Hashable {
    let id: String
    let number: Int
    let name: String
    let source: String
    let folder: URL?
    let episodes: Int
  }

  struct Listing {
    var text: String  // channels.json as it is, or the fresh list for a card that never was in the TV
    var local: [Channel]
    var others: Int  // the TV's own channels (teletext, test card, remote...), not listed
    var problem: String?
  }

  func listing() -> Listing {
    // The default list only when there is no file: one that cannot be read must never be written over.
    let text: String
    if FileManager.default.fileExists(atPath: channelsURL.path) {
      guard let read = try? String(contentsOf: channelsURL, encoding: .utf8) else {
        return Listing(text: "", local: [], others: 0, problem: "channels.json no se puede leer: corrígelo en un ordenador")
      }
      text = read
    } else {
      text = ChannelsFile.freshText
    }
    guard let entries = ChannelsFile.channels(in: text) else {
      return Listing(text: text, local: [], others: 0, problem: "channels.json no se entiende: corrígelo en un ordenador")
    }
    var local: [Channel] = []
    for e in entries where e["type"] as? String == "local" {
      guard let id = e["id"] as? String, let number = e["number"] as? Int,
            let source = e["source"] as? String, source.hasPrefix("/") else { continue }
      // A source is one episode (.mjpeg) or a folder, which may not exist yet: converting creates it.
      let url = root.appendingPathComponent(String(source.dropFirst()))
      let isFile = source.lowercased().hasSuffix(".mjpeg")
      local.append(Channel(id: id, number: number, name: e["name"] as? String ?? id, source: source,
                           folder: isFile ? nil : url,
                           episodes: isFile ? (FileManager.default.fileExists(atPath: url.path) ? 1 : 0) : Card.episodes(in: url)))
    }
    return Listing(text: text, local: local.sorted { $0.number < $1.number }, others: entries.count - local.count, problem: nil)
  }

  static func episodes(in folder: URL) -> Int {
    let names = (try? FileManager.default.contentsOfDirectory(atPath: folder.path)) ?? []
    return names.filter { !$0.hasPrefix(".") && $0.lowercased().hasSuffix(".mjpeg") }.count
  }
}
