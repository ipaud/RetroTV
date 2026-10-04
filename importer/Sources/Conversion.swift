import Foundation

/// One import: the bundled tools/convert_video.sh over the chosen videos, into a channel folder on the card,
/// then the new channel in channels.json once it has an episode. Both the window and `--import` use it.
/// It reads the script's output by its line tags (convert, done, skip, FAILED...).
@MainActor
final class Conversion {
  enum Destination {
    case newChannel(ChannelsFile.Added)  // channels.json with the channel in it, written at the end
    case existing(source: String)  // "/retrotv/media/futurama"
  }

  struct Request {
    let card: URL
    let videos: [URL]
    var seconds: [URL: Double] = [:]  // durations, for progress within an episode
    let destination: Destination
    let track: Int  // convert_video.sh's PISTA: which audio track, 0 = the first
  }

  enum Event {
    case started(index: Int, total: Int, name: String)
    case progress(fraction: Double)  // of the episode being converted
    case finished(index: Int, total: Int, name: String, ok: Bool)
  }

  struct Summary {
    var converted = 0
    var skipped = 0
    var failed: [String] = []  // "name: why"
    var cancelled = false
    var added: ChannelsFile.Added?
    var problem: String?  // something outside the episodes: the script did not run, channels.json...
  }

  struct Tools {
    let script: URL
    let bin: URL  // ffmpeg and ffprobe
    let indexer: URL  // this program: convert_video.sh calls `--index` through INDEXER
    var ffmpeg: URL { bin.appendingPathComponent("ffmpeg") }
    var ffprobe: URL { bin.appendingPathComponent("ffprobe") }

    /// Inside RetroTV Importar.app: Contents/MacOS/{RetroTVImporter,ffmpeg,ffprobe}, Resources/convert_video.sh.
    static func bundled() -> Tools? {
      guard let script = Bundle.main.url(forResource: "convert_video", withExtension: "sh"),
            let exe = Bundle.main.executableURL else { return nil }
      let tools = Tools(script: script, bin: exe.deletingLastPathComponent(), indexer: exe)
      return FileManager.default.isExecutableFile(atPath: tools.ffmpeg.path) ? tools : nil
    }
  }

  private var process: Process?
  private var cancelled = false

  func run(_ r: Request, tools: Tools, onEvent: (Event) -> Void) async -> Summary {
    var summary = Summary()
    cancelled = false
    let source: String
    switch r.destination {
    case .newChannel(let added): source = added.source
    case .existing(let s): source = s
    }

    // convert_video.sh reads one folder: links to the chosen videos, named as the originals.
    let inputs = FileManager.default.temporaryDirectory.appendingPathComponent("retrotv-import-\(UUID().uuidString)")
    defer { try? FileManager.default.removeItem(at: inputs) }
    var seconds: [String: Double] = [:]  // by the link's name, as the script prints it
    do {
      try FileManager.default.createDirectory(at: inputs, withIntermediateDirectories: true)
      var taken = Set<String>()
      for video in r.videos {
        var name = video.lastPathComponent
        var n = 2
        while !taken.insert(name.lowercased()).inserted {  // the same name from two folders
          name = "\(video.deletingPathExtension().lastPathComponent) (\(n)).\(video.pathExtension)"
          n += 1
        }
        try FileManager.default.createSymbolicLink(at: inputs.appendingPathComponent(name), withDestinationURL: video)
        seconds[name] = r.seconds[video]
      }
    } catch {
      summary.problem = "No he podido preparar los vídeos: \(error.localizedDescription)"
      return summary
    }

    let p = Process()
    p.executableURL = URL(fileURLWithPath: "/bin/bash")
    p.arguments = [tools.script.path, inputs.path, source, r.card.path]
    p.environment = [
      "PATH": "\(tools.bin.path):/usr/bin:/bin:/usr/sbin:/sbin", "HOME": NSHomeDirectory(), "LANG": "en_US.UTF-8",
      "INDEXER": tools.indexer.path, "PISTA": String(r.track), "PROGRESO": "1",
    ]
    let pipe = Pipe()
    p.standardOutput = pipe
    p.standardError = pipe
    // A season takes minutes: the Mac must not fall asleep in the middle.
    let activity = ProcessInfo.processInfo.beginActivity(options: [.userInitiated, .idleSystemSleepDisabled],
                                                         reason: "Convirtiendo vídeos para RETROTV")
    defer { ProcessInfo.processInfo.endActivity(activity) }
    do {
      try p.run()
    } catch {
      summary.problem = "No he podido empezar la conversión: \(error.localizedDescription)"
      return summary
    }
    process = p

    let total = r.videos.count
    var index = 0
    var current = ""
    var detail: String?  // the first line ffmpeg printed, for a failure
    var duration = 0.0
    do {
      for try await line in pipe.fileHandleForReading.bytes.lines {
        if !line.contains(" "), let eq = line.firstIndex(of: "=") {  // ffmpeg's progress: key=value
          if line[..<eq] == "out_time_us", let us = Double(line[line.index(after: eq)...]), duration > 0 {
            onEvent(.progress(fraction: min(us / 1_000_000 / duration, 1)))
          }
          continue
        }
        let tag = String(line.prefix { $0 != " " })
        let rest = String(line.dropFirst(tag.count).drop { $0 == " " })
        switch tag {
        case "convert":  // "convert  <file> -> <name>"
          index += 1
          current = rest.components(separatedBy: " -> ").last ?? rest
          duration = seconds[rest.components(separatedBy: " -> ").first ?? ""] ?? 0
          detail = nil
          onEvent(.started(index: index, total: total, name: current))
        case "done":
          summary.converted += 1
          onEvent(.finished(index: index, total: total, name: current, ok: true))
        case "skip":  // "skip     <name> (already converted)": no convert line before it
          index += 1
          summary.skipped += 1
          current = rest.components(separatedBy: " (").first ?? rest
          onEvent(.finished(index: index, total: total, name: current, ok: true))
        case "FAILED" where !rest.contains(".mjpeg:"):  // "FAILED   <name>: <why>" (an index failure is only a warning)
          summary.failed.append(Conversion.reason(rest, detail: detail))
          onEvent(.finished(index: index, total: total, name: current, ok: false))
        case "index", "warn", "converted", "tip:", "interrupted:", "FAILED", "":
          break
        default:  // ffmpeg's own lines: the first one after a failure says why
          if detail == nil {
            detail = line.replacingOccurrences(of: "^\\[[^\\]]*\\] *", with: "", options: .regularExpression)
          }
        }
      }
    } catch {
      detail = error.localizedDescription
    }
    while p.isRunning { try? await Task.sleep(nanoseconds: 50_000_000) }
    process = nil
    summary.cancelled = cancelled
    if p.terminationStatus != 0 && index == 0 && !cancelled {
      summary.problem = detail ?? "La conversión no ha funcionado (código \(p.terminationStatus))"
    }

    // ponytail: channels.json was read when the import started; nothing else edits the card meanwhile.
    if case .newChannel(let added) = r.destination {
      let folder = r.card.appendingPathComponent(String(added.source.dropFirst()))
      guard Card.episodes(in: folder) > 0 else {  // nothing converted: no channel, and no empty folder either
        if (try? FileManager.default.contentsOfDirectory(atPath: folder.path))?.isEmpty == true {
          try? FileManager.default.removeItem(at: folder)
        }
        return summary
      }
      do {
        try Conversion.writeAtomically(added.text, to: r.card.appendingPathComponent("retrotv/config/channels.json"))
        summary.added = added
      } catch {
        summary.problem = "Los capítulos están en la tarjeta, pero no he podido apuntar el canal en channels.json: "
          + error.localizedDescription
      }
    }
    await Conversion.removeAppleDouble(in: [r.card.appendingPathComponent(String(source.dropFirst())),
                                            r.card.appendingPathComponent("retrotv/config")])
    return summary
  }

  /// Stops the script. bash runs its trap (which deletes the half-made episode) only once its ffmpeg has
  /// exited, so the ffmpeg goes too.
  func cancel() {
    guard let p = process, p.isRunning else { return }
    cancelled = true
    p.terminate()
    _ = Probe.run(URL(fileURLWithPath: "/usr/bin/pkill"), ["-TERM", "-P", String(p.processIdentifier)])
  }

  /// The ._ files macOS leaves on a FAT card (the TV skips them, but they fill the folder). Off the main
  /// thread: on a slow card it takes a while.
  static func removeAppleDouble(in folders: [URL]) async {
    let paths = folders.map(\.path).filter { FileManager.default.fileExists(atPath: $0) }
    guard !paths.isEmpty else { return }
    await Task.detached { _ = Probe.run(URL(fileURLWithPath: "/usr/sbin/dot_clean"), ["-m"] + paths) }.value
  }

  /// convert_video.sh's reasons, in Spanish.
  static func reason(_ rest: String, detail: String?) -> String {
    let name = rest.components(separatedBy: ": ").first ?? rest
    let why = rest.dropFirst(name.count + 2)
    if why.hasPrefix("no audio track") { return "\(name): no tiene esa pista de audio" }
    if why.hasPrefix("video conversion error") {
      return "\(name): no se ha podido convertir el vídeo" + (detail.map { " (\($0))" } ?? "")
    }
    return rest
  }

  /// As the TV does it: a .part file renamed over the old one, so the card never holds half a file.
  nonisolated static func writeAtomically(_ text: String, to url: URL) throws {
    try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
    let part = url.appendingPathExtension("part")
    try Data(text.utf8).write(to: part)
    guard rename(part.path, url.path) == 0 else {
      try? FileManager.default.removeItem(at: part)
      throw ChannelsFile.Failure(description: String(cString: strerror(errno)))
    }
  }
}
