import Foundation

// RetroTV Importar: converts videos onto the RETROTV card it runs from. With no arguments, the window.
//   RetroTVImporter --index <file.mjpeg> --fps <n>     convert_video.sh calls this through INDEXER
//   RetroTVImporter --import <card> (--new <name> | --into </retrotv/media/folder>) [--track <n>] <video|folder>...
//                                                      what the window does, without it (tests, scripts)
//   RetroTVImporter --self-test                        the pure parts (tools/run_host_tests.sh)

func indexCommand(_ args: [String]) -> Int32 {
  guard args.count == 4, args[2] == "--fps", let fps = Int(args[3]), (1...60).contains(fps) else {
    print("usage: RetroTVImporter --index <file.mjpeg> --fps <n>")
    return 2
  }
  let mjpeg = URL(fileURLWithPath: args[1])
  let idxName = mjpeg.deletingPathExtension().appendingPathExtension("idx").lastPathComponent
  do {
    let r = try EpisodeIndex.write(for: mjpeg, fps: fps)
    let seconds = r.durationMs / 1000
    print("index    \(idxName) (\(r.entries) entries, \(seconds / 60):\(String(format: "%02d", seconds % 60))"
          + "\(r.hasAudio ? "" : ", no audio"))")
    return 0
  } catch {
    print("FAILED   \(mjpeg.lastPathComponent): \(error)")
    return 1
  }
}

@MainActor
func importCommand(_ args: [String]) async -> Int32 {
  let usage = "usage: RetroTVImporter --import <card> (--new <name> | --into </retrotv/media/folder>) [--track <n>] <video|folder>..."
  var rest = Array(args.dropFirst())
  guard rest.count >= 4, let card = Card.at(URL(fileURLWithPath: rest.removeFirst())) else {
    print(usage)
    return 2
  }
  let listing = card.listing()
  if let problem = listing.problem {
    print("FAILED   \(problem)")
    return 1
  }
  let mode = rest.removeFirst()
  let value = rest.removeFirst()
  var track = 0
  if rest.first == "--track", rest.count >= 2, let n = Int(rest[1]) {
    track = n
    rest.removeFirst(2)
  }
  guard let tools = Conversion.Tools.bundled() else {
    print("run the binary inside RetroTV Importar.app (it needs its ffmpeg and convert_video.sh)")
    return 1
  }
  let videos = Probe.videos(in: rest.map { URL(fileURLWithPath: $0) })
  let seconds = Dictionary(videos.map { ($0, Probe.video($0, ffprobe: tools.ffprobe)?.seconds ?? 0) }) { a, _ in a }
  var shown = 0
  let destination: Conversion.Destination
  switch mode {
  case "--new":
    do {
      destination = .newChannel(try ChannelsFile.addLocal(name: value, to: listing.text))
    } catch {
      print("FAILED   \(error)")
      return 1
    }
  case "--into":
    destination = .existing(source: value)
  default:
    print(usage)
    return 2
  }
  let summary = await Conversion().run(.init(card: card.root, videos: videos, seconds: seconds, destination: destination,
                                             track: track),
                                       tools: tools) { event in
    switch event {
    case .progress(let fraction) where Int(fraction * 100) / 25 > shown:  // 25 %, 50 %, 75 %
      shown = Int(fraction * 100) / 25
      if shown < 4 { print("      \(shown * 25) %") }
    case .finished(let i, let total, let name, let ok):
      shown = 0
      print("\(i)/\(total) \(ok ? "ok" : "FAILED") \(name)")
    default:
      break
    }
  }
  print("converted \(summary.converted), skipped \(summary.skipped), failed \(summary.failed.count)"
        + (summary.added.map { ", new channel \($0.number) \($0.id)" } ?? ""))
  summary.failed.forEach { print("  \($0)") }
  if let problem = summary.problem { print("problem: \(problem)") }
  return summary.failed.isEmpty && summary.problem == nil ? 0 : 1
}

let args = Array(CommandLine.arguments.dropFirst())
switch args.first {
case "--index":
  exit(indexCommand(args))
case "--self-test":
  exit(SelfTest.run())
case "--import":
  exit(await importCommand(args))
default:
  ImporterApp.main()
}
