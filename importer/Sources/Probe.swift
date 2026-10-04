import Foundation

/// What the app needs to know about the videos dropped on it, from the bundled ffprobe.
enum Probe {
  /// The inputs convert_video.sh takes.
  static let extensions: Set<String> = ["mp4", "mkv", "avi", "mov", "m4v", "webm"]
  /// Bytes per second on the card: ~13 KB per JPEG frame at the default quality and 20 fps (measured,
  /// docs/CHANNELS.md), plus 32 kb/s of AAC. An estimate: the real size depends on the picture.
  static let bytesPerSecond: Double = 13_500 * 20 + 4_000

  struct AudioTrack: Hashable, Sendable {
    let codec: String
    let channels: Int
    let language: String?
    let title: String?

    /// "japonés · 5.1", "Pista 2" when it has nothing better.
    func label(position: Int) -> String {
      var parts: [String] = []
      if let title, !title.isEmpty { parts.append(title) }
      if let language, language != "und",
         let name = Locale(identifier: "es").localizedString(forLanguageCode: language) {
        parts.append(name)
      }
      if parts.isEmpty { parts.append("Pista \(position + 1)") }
      parts.append(channels >= 6 ? "5.1" : channels == 2 ? "estéreo" : channels == 1 ? "mono" : "\(channels) canales")
      return parts.joined(separator: " · ")
    }
  }

  struct Video: Sendable {
    let url: URL
    let seconds: Double
    let videoCodec: String?
    let audio: [AudioTrack]
  }

  /// The videos in what was dropped: a folder gives the videos directly inside it, as convert_video.sh
  /// reads it; a file gives itself. Sorted by name, as the TV plays them.
  static func videos(in urls: [URL]) -> [URL] {
    var found: [URL] = []
    for url in urls {
      var isDir: ObjCBool = false
      guard FileManager.default.fileExists(atPath: url.path, isDirectory: &isDir) else { continue }
      let candidates = isDir.boolValue
        ? (try? FileManager.default.contentsOfDirectory(at: url, includingPropertiesForKeys: nil, options: [.skipsHiddenFiles])) ?? []
        : [url]
      found += candidates.filter { extensions.contains($0.pathExtension.lowercased()) }
    }
    var seen = Set<String>()
    return found.filter { seen.insert($0.standardizedFileURL.path).inserted }
      .sorted { $0.lastPathComponent.localizedStandardCompare($1.lastPathComponent) == .orderedAscending }
  }

  private struct ProbeOutput: Decodable {
    struct Stream: Decodable {
      let codec_type: String?
      let codec_name: String?
      let channels: Int?
      let tags: [String: String]?
    }
    struct Format: Decodable { let duration: String? }
    let streams: [Stream]?
    let format: Format?
  }

  static func video(_ url: URL, ffprobe: URL) -> Video? {
    let out = run(ffprobe, ["-v", "error", "-show_entries",
                            "stream=codec_type,codec_name,channels:stream_tags=language,title:format=duration",
                            "-of", "json", url.path])
    guard let data = out, let p = try? JSONDecoder().decode(ProbeOutput.self, from: data) else { return nil }
    let streams = p.streams ?? []
    return Video(url: url, seconds: Double(p.format?.duration ?? "") ?? 0,
                 videoCodec: streams.first { $0.codec_type == "video" }?.codec_name,
                 audio: streams.filter { $0.codec_type == "audio" }.map {
                   AudioTrack(codec: $0.codec_name ?? "?", channels: $0.channels ?? 0,
                              language: $0.tags?["language"], title: $0.tags?["title"])
                 })
  }

  /// The decoders the bundled ffmpeg has ("h264", "vp9"...): a video codec not in it cannot be converted
  /// (AV1, for one; importer/build_ffmpeg.sh says why).
  static func decoders(ffmpeg: URL) -> Set<String> {
    guard let data = run(ffmpeg, ["-hide_banner", "-decoders"]) else { return [] }
    var names = Set<String>()
    for line in String(decoding: data, as: UTF8.self).split(separator: "\n") {
      let fields = line.split(separator: " ")
      if fields.count >= 2, fields[0].count == 6, fields[0].first == "V" { names.insert(String(fields[1])) }
    }
    return names
  }

  static func run(_ tool: URL, _ args: [String]) -> Data? {
    let p = Process()
    p.executableURL = tool
    p.arguments = args
    let pipe = Pipe()
    p.standardOutput = pipe
    p.standardError = FileHandle.nullDevice
    do { try p.run() } catch { return nil }
    let data = pipe.fileHandleForReading.readDataToEndOfFile()
    p.waitUntilExit()
    return p.terminationStatus == 0 ? data : nil
  }
}
