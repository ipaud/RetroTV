import Foundation

// RetroTVImporter --self-test: the pure parts, no card needed.
// tools/run_host_tests.sh runs it on macOS.
enum SelfTest {
  nonisolated(unsafe) static var failures = 0

  static func check(_ ok: Bool, _ what: String, line: Int = #line) {
    if !ok {
      failures += 1
      print("FAIL     SelfTest.swift:\(line): \(what)")
    }
  }

  static func run() -> Int32 {
    index()
    channels()
    print(failures == 0 ? "importer self-test: OK" : "importer self-test: \(failures) failures")
    return failures == 0 ? 0 : 1
  }

  // The same synthetic episode as make_index.py's self-test, with the same expectations.
  static func index() {
    let fps = 10
    var video = Data("junk".utf8)
    var starts: [Int] = []
    for i in 0..<35 {  // 3.5 s: 4 entries
      starts.append(video.count)
      video.append(contentsOf: [0xFF, 0xD8, 0xFF, 0xE0] + Array(repeating: UInt8(i), count: 20 + i) + [0xFF, 0x00, 0xFF, 0xD9])
    }
    var audio = Data()
    var audioStarts: [Int] = []
    for _ in 0..<160 {  // 1024 samples at 44.1 kHz each: ~3.7 s
      audioStarts.append(audio.count)
      let length = 7 + 30
      audio.append(contentsOf: [0xFF, 0xF1, (1 << 6) | (4 << 2), UInt8(0x40 | (length >> 11)),
                                UInt8((length >> 3) & 0xFF), UInt8(((length & 7) << 5) | 0x1F), 0xFC])
      audio.append(Data(count: 30))
    }

    let built = try? video.withUnsafeBytes { v in
      try audio.withUnsafeBytes { a in try EpisodeIndex.build(video: v, audio: a, fps: fps, interval: fps) }
    }
    guard let built else { return check(false, "build threw") }
    let d = [UInt8](built.data)
    func u16(_ at: Int) -> Int { Int(d[at]) | Int(d[at + 1]) << 8 }
    func u32(_ at: Int) -> Int { u16(at) | u16(at + 2) << 16 }
    check(Array(d[0..<8]) == EpisodeIndex.magic && u16(8) == 1 && u16(10) == fps, "header magic/version/fps")
    check(u32(12) == 35 && u32(16) == 3500 && built.durationMs == 3500, "frames and duration")
    check(u16(20) == fps && u16(22) == 0 && u32(24) == 4 && built.entries == 4 && u32(28) == 44100, "interval/entries/rate")
    check(d.count == 32 + 4 * 8, "size")
    for k in 0..<4 {
      check(u32(32 + k * 8) == starts[k * fps], "entry \(k) video offset")
      let frame = k * 44100 / 1024  // audio frame that started at or before second k
      check(u32(36 + k * 8) == audioStarts[frame], "entry \(k) audio offset")
    }
    let silent = try? video.withUnsafeBytes { try EpisodeIndex.build(video: $0, audio: nil, fps: fps, interval: fps) }
    check(silent.map { [UInt8]($0.data)[36..<40] == [0xFF, 0xFF, 0xFF, 0xFF] } ?? false, "silent episode")
    let empty = try? Data("no jpeg here".utf8).withUnsafeBytes {
      try EpisodeIndex.build(video: $0, audio: nil, fps: fps, interval: fps)
    }
    check(empty == nil, "no frames throws")

    // File handling: .idx written next to the episode, no .part left.
    let dir = FileManager.default.temporaryDirectory.appendingPathComponent("retrotv-selftest-\(getpid())")
    try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
    defer { try? FileManager.default.removeItem(at: dir) }
    let mjpeg = dir.appendingPathComponent("ep.mjpeg")
    try? video.write(to: mjpeg)
    try? audio.write(to: dir.appendingPathComponent("ep.aac"))
    let written = try? EpisodeIndex.write(for: mjpeg, fps: fps)
    let idx = try? Data(contentsOf: dir.appendingPathComponent("ep.idx"))
    check(written?.hasAudio == true && idx == built.data, "write: same bytes as build")
    check(!FileManager.default.fileExists(atPath: dir.appendingPathComponent("ep.idx.part").path), "no .part left")
  }

  // data/example-config/channels.json: a card the TV has written its default list to.
  static let sample = """
    {
      "channels": [
        { "id": "demo", "number": 1, "name": "RETROTV DEMO", "type": "local",
          "source": "/retrotv/media/demo/demo.mjpeg", "enabled": true },
        { "id": "channel01", "number": 2, "name": "CANAL 1", "type": "local",
          "source": "/retrotv/media/channel01", "enabled": true },
        { "id": "channel02", "number": 3, "name": "CANAL 2", "type": "local",
          "source": "/retrotv/media/channel02", "enabled": true },
        { "id": "teletext", "number": 8, "name": "TELETEXT", "type": "internal",
          "source": "teletext", "enabled": true },
        { "id": "testcard", "number": 9, "name": "CARTA DE AJUSTE", "type": "internal",
          "source": "testcard", "enabled": true },
        { "id": "remote", "number": 10, "name": "RETROTV REMOTE", "type": "remote",
          "source": "http://retrotv-server.local:8080/channel/1", "enabled": false }
      ]
    }

    """

  static func channels() {
    // After the highest local channel (3), id from the name, the rest of the file untouched.
    let added = try? ChannelsFile.addLocal(name: "  Bola de Drac ", to: sample)
    check(added?.number == 4 && added?.id == "bola_de_drac" && added?.source == "/retrotv/media/bola_de_drac", "default list")
    let entry = ChannelsFile.channels(in: added?.text ?? "")?.last
    check(entry?["name"] as? String == "Bola de Drac" && entry?["type"] as? String == "local"
            && entry?["enabled"] as? Bool == true, "new entry fields")
    let unchanged = sample.components(separatedBy: "\n  ]")[0]
    check(added?.text.hasPrefix(unchanged) == true, "existing entries untouched")
    check(added?.text.contains("\"source\": \"/retrotv/media/bola_de_drac\"") == true, "slashes not escaped")

    // Accents, quotes and a taken id; "mensajes" is the firmware's.
    let accents = try? ChannelsFile.addLocal(name: "Canción \"Ñ\" 1", to: sample)
    check(accents?.id == "cancion_n_1", "accents folded in the id")
    check(ChannelsFile.channels(in: accents?.text ?? "")?.last?["name"] as? String == "Canción \"Ñ\" 1", "name kept as typed")
    check((try? ChannelsFile.addLocal(name: "Canal 1", to: sample))?.id == "canal_1", "free id")
    check((try? ChannelsFile.addLocal(name: "demo", to: sample))?.id == "demo_2", "taken id")
    check((try? ChannelsFile.addLocal(name: "Mensajes", to: sample))?.id == "mensajes_2", "reserved id")
    check((try? ChannelsFile.addLocal(name: "¡¡!!", to: sample))?.id == "canal", "empty id")
    let shared = "{\"channels\": [{\"id\": \"x\", \"number\": 1, \"source\": \"/retrotv/media/futurama\"}]}"
    check((try? ChannelsFile.addLocal(name: "Futurama", to: shared))?.id == "futurama_2", "folder of another channel")
    let long = try? ChannelsFile.addLocal(name: String(repeating: "abc ", count: 20), to: sample)
    check(long.map { $0.id.count <= 23 } ?? false, "id at most 23")

    // Empty list, compact file as the TV writes it, other keys and arrays around, strings with brackets.
    // A new card: series 1 to 7, then past the TV's teletext (8) and test card (9).
    var fresh = ChannelsFile.freshText
    var numbers: [Int] = []
    for n in 1...8 {
      guard let next = try? ChannelsFile.addLocal(name: "Serie \(n)", to: fresh) else { break }
      fresh = next.text
      numbers.append(next.number)
    }
    check(numbers == [1, 2, 3, 4, 5, 6, 7, 10], "fresh card numbers \(numbers)")
    let mine = "{\"channels\": [{\"id\": \"a\", \"number\": 31, \"type\": \"local\"}, {\"id\": \"m\", \"number\": 33, \"type\": \"internal\"}]}"
    check((try? ChannelsFile.addLocal(name: "B", to: mine))?.number == 32, "before MANDO")
    let empty = try? ChannelsFile.addLocal(name: "A", to: "{\n  \"channels\": []\n}\n")
    check(empty?.number == 1 && ChannelsFile.channels(in: empty?.text ?? "")?.count == 1, "empty list")
    let compact = "{\"x\":[1,{\"channels\":[]}],\"channels\":[{\"id\":\"a]\",\"number\":999,\"name\":\"[\",\"type\":\"local\","
      + "\"source\":\"/a\"},{\"id\":\"b\",\"number\":1}],\"y\":[2]}"
    let tricky = try? ChannelsFile.addLocal(name: "B", to: compact)
    check(tricky?.number == 2 && ChannelsFile.channels(in: tricky?.text ?? "")?.count == 3, "999 taken: first free")
    check(tricky?.text.hasSuffix("],\"y\":[2]}") == true && tricky?.text.hasPrefix("{\"x\":[1,{\"channels\":[]}],") == true,
          "other keys untouched")

    // Refusals: not JSON, no list, full.
    check((try? ChannelsFile.addLocal(name: "A", to: "{")) == nil, "not JSON")
    check((try? ChannelsFile.addLocal(name: "A", to: "{\"canales\": []}")) == nil, "no channels list")
    check((try? ChannelsFile.addLocal(name: "   ", to: sample)) == nil, "empty name")
    let full = "{\"channels\": [" + (1...48).map { "{\"id\": \"c\($0)\", \"number\": \($0)}" }.joined(separator: ",") + "]}"
    check((try? ChannelsFile.addLocal(name: "A", to: full)) == nil, "48 channels")
  }
}
