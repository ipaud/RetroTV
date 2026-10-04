import Foundation

// Port of tools/make_index.py (same bytes, checked against it): <name>.idx next to an episode, one
// entry per second with the byte offset of the MJPEG frame and of the AAC (ADTS) frame playing then,
// so the TV can tune in mid-episode. Binary layout: src/media/EpisodeIndex.h. In the app because a Mac
// without developer tools has no python3, only a stub that asks to install them.
enum EpisodeIndex {
  static let magic = Array("PAUTVIDX".utf8)
  static let version: UInt16 = 1
  static let noAudio: UInt32 = 0xFFFF_FFFF
  static let aacRates = [96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350]

  struct Failure: Error, CustomStringConvertible {
    let description: String
  }

  /// Byte offset of every JPEG in a raw MJPEG stream: each starts FF D8 FF, which JPEG data never contains.
  static func frameOffsets(_ p: UnsafeRawBufferPointer) -> [Int] {
    var offsets: [Int] = []
    guard let base = p.baseAddress else { return offsets }
    let n = p.count
    var i = 0
    while i + 3 <= n, let hit = memchr(base + i, 0xFF, n - 2 - i) {
      let j = base.distance(to: UnsafeRawPointer(hit))
      if p[j + 1] == 0xD8 && p[j + 2] == 0xFF {
        offsets.append(j)
        i = j + 2
      } else {
        i = j + 1
      }
    }
    return offsets
  }

  /// (sample position, byte offset) of every ADTS frame, and the sample rate.
  static func adtsFrames(_ p: UnsafeRawBufferPointer) -> (frames: [(samples: Int, offset: Int)], rate: Int) {
    var frames: [(samples: Int, offset: Int)] = []
    guard let base = p.baseAddress else { return (frames, 0) }
    let n = p.count
    var rate = 0
    var samples = 0
    var i = 0
    while i + 7 <= n {
      if p[i] != 0xFF || (p[i + 1] & 0xF6) != 0xF0 {  // 0xFFF sync, layer 0
        guard let hit = memchr(base + i + 1, 0xFF, n - i - 1) else { break }
        i = base.distance(to: UnsafeRawPointer(hit))
        continue
      }
      let rateIndex = Int((p[i + 2] >> 2) & 0x0F)
      let length = (Int(p[i + 3] & 0x03) << 11) | (Int(p[i + 4]) << 3) | (Int(p[i + 5]) >> 5)
      if length < 7 || rateIndex >= aacRates.count {
        i += 1
        continue
      }
      if rate == 0 { rate = aacRates[rateIndex] }
      frames.append((samples, i))
      samples += 1024 * (Int(p[i + 6] & 0x03) + 1)
      i += length
    }
    return (frames, rate)
  }

  static func build(video: UnsafeRawBufferPointer, audio: UnsafeRawBufferPointer?, fps: Int,
                    interval: Int) throws -> (data: Data, durationMs: Int, entries: Int) {
    let frames = frameOffsets(video)
    guard !frames.isEmpty else { throw Failure(description: "no JPEG frames found") }
    let (audioFrames, rate) = audio.map(adtsFrames) ?? ([], 0)

    var body = Data()
    var entries = 0
    var a = 0
    for first in stride(from: 0, to: frames.count, by: interval) {
      // Audio frame that starts at or before this video frame: samples * fps <= frame * rate.
      while a + 1 < audioFrames.count && audioFrames[a + 1].samples * fps <= first * rate { a += 1 }
      append(UInt32(frames[first]), to: &body)
      append(audioFrames.isEmpty ? noAudio : UInt32(audioFrames[a].offset), to: &body)
      entries += 1
    }

    let durationMs = frames.count * 1000 / fps
    var data = Data(magic)
    append(version, to: &data)
    append(UInt16(fps), to: &data)
    append(UInt32(frames.count), to: &data)
    append(UInt32(durationMs), to: &data)
    append(UInt16(interval), to: &data)
    append(UInt16(0), to: &data)
    append(UInt32(entries), to: &data)
    append(UInt32(rate), to: &data)
    data.append(body)
    return (data, durationMs, entries)
  }

  /// Writes <stem>.idx for <stem>.mjpeg (with <stem>.aac when there is one), as .part and then renamed.
  static func write(for mjpeg: URL, fps: Int) throws -> (durationMs: Int, entries: Int, hasAudio: Bool) {
    let stem = mjpeg.deletingPathExtension()
    let aac = stem.appendingPathExtension("aac")
    let idx = stem.appendingPathExtension("idx")
    let video = try Data(contentsOf: mjpeg, options: .alwaysMapped)
    let audio = FileManager.default.fileExists(atPath: aac.path) ? try Data(contentsOf: aac, options: .alwaysMapped) : nil
    let result = try video.withUnsafeBytes { v in
      if let audio {
        return try audio.withUnsafeBytes { a in try build(video: v, audio: a, fps: fps, interval: fps) }
      }
      return try build(video: v, audio: nil, fps: fps, interval: fps)  // one entry per second
    }
    let part = idx.appendingPathExtension("part")
    try result.data.write(to: part)
    guard rename(part.path, idx.path) == 0 else {
      try? FileManager.default.removeItem(at: part)
      throw Failure(description: "cannot rename \(part.lastPathComponent): \(String(cString: strerror(errno)))")
    }
    return (result.durationMs, result.entries, audio != nil)
  }

  private static func append<T: FixedWidthInteger>(_ value: T, to data: inout Data) {
    withUnsafeBytes(of: value.littleEndian) { data.append(contentsOf: $0) }
  }
}
