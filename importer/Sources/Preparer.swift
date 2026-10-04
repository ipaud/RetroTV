import AppKit
import Foundation

/// Turns a new card into one the TV reads. Cards over 32 GB come in exFAT and the TV's core reads only
/// FAT32: erase it as FAT32 named RETROTV, write /retrotv/config/channels.json and copy this app onto it.
/// Erasing deletes everything on the card: the window asks first, with its name and size.
enum Preparer {
  struct Disk: Equatable, Sendable {
    let volume: URL
    let whole: String  // "disk4": what gets erased
    let size: Int64
    let volumeUUID: String?
  }

  static let diskutil = URL(fileURLWithPath: "/usr/sbin/diskutil")
  static let ditto = URL(fileURLWithPath: "/usr/bin/ditto")
  static let maxSize: Int64 = 2 << 40  // 2 TB, the most an SD card (SDXC) holds

  /// The disk under an exFAT card volume, or why it must not be erased. Only removable media (an SD card in a
  /// reader: external SSDs and USB disks are "fixed"), writable, up to 2 TB, with that one partition (a GPT
  /// card's hidden EFI one aside), and never the disk the Mac starts from.
  static func disk(of volume: URL) -> Result<Disk, ChannelsFile.Failure> {
    func fail(_ why: String) -> Result<Disk, ChannelsFile.Failure> { .failure(.init(description: why)) }
    guard let info = plist(["info", "-plist", volume.path]), let whole = info["ParentWholeDisk"] as? String,
          info["MountPoint"] as? String == volume.path else { return fail("No encuentro el disco de la tarjeta.") }
    let size = (info["TotalSize"] as? NSNumber)?.int64Value ?? 0
    guard info["RemovableMedia"] as? Bool == true, info["FilesystemType"] as? String == "exfat", size <= maxSize,
          whole != plist(["info", "-plist", "/"])?["ParentWholeDisk"] as? String else {
      return fail("\(volume.lastPathComponent) no parece una tarjeta de memoria: no la borro.")
    }
    guard info["WritableMedia"] as? Bool == true else { return fail(Card.lockedMessage) }
    let disks = plist(["list", "-plist", whole])?["AllDisksAndPartitions"] as? [[String: Any]]
    let partitions = (disks?.first?["Partitions"] as? [[String: Any]])?.filter { $0["Content"] as? String != "EFI" }
    guard partitions?.count == 1 else {
      return fail("La tarjeta tiene varias particiones: prepárala con Utilidad de Discos (MS-DOS FAT, esquema MBR).")
    }
    return .success(Disk(volume: volume, whole: whole, size: size, volumeUUID: info["VolumeUUID"] as? String))
  }

  /// Checks the card again (the person may have swapped it while the alert was up, and macOS reuses disk
  /// numbers), erases it and sets it up; returns where the new volume is mounted.
  static func prepare(_ asked: Disk) throws -> URL {
    guard case .success(let now) = disk(of: asked.volume), now == asked else {
      throw ChannelsFile.Failure(description: "La tarjeta ha cambiado desde que lo preguntaste: no he borrado nada. Vuelve a empezar.")
    }
    guard Probe.run(diskutil, ["eraseDisk", "FAT32", "RETROTV", "MBRFormat", "/dev/\(asked.whole)"]) != nil else {
      throw ChannelsFile.Failure(description: "No he podido formatear la tarjeta. Si se ha desconectado, métela otra vez y vuelve a prepararla.")
    }
    guard let mount = plist(["info", "-plist", "\(asked.whole)s1"])?["MountPoint"] as? String, !mount.isEmpty else {
      throw ChannelsFile.Failure(description: "La tarjeta está formateada pero no aparece: sácala y vuelve a meterla.")
    }
    let root = URL(fileURLWithPath: mount)
    try FileManager.default.createDirectory(at: root.appendingPathComponent("retrotv/media"), withIntermediateDirectories: true)
    try Conversion.writeAtomically(ChannelsFile.freshText, to: root.appendingPathComponent("retrotv/config/channels.json"))
    // No ._ files nor quarantine on the copy: the person running this app has already allowed it.
    guard Probe.run(ditto, ["--norsrc", "--noextattr", "--noqtn", Bundle.main.bundleURL.path,
                            root.appendingPathComponent("RetroTV Importar.app").path]) != nil else {
      throw ChannelsFile.Failure(description: "La tarjeta está lista, pero no he podido copiar la app en ella: cópiala a mano.")
    }
    return root
  }

  /// Erasing always happens from a copy in the temporary folder: this one may run from the card itself, or
  /// from the read-only copy macOS makes of a downloaded app, and either keeps the card busy. The copy opens
  /// with --prepare and this one quits. Returns why it failed, or nil.
  @MainActor static func handOver(preparing volume: URL) async -> String? {
    let copy = FileManager.default.temporaryDirectory.appendingPathComponent("RetroTV Importar.app")
    let source = Bundle.main.bundleURL
    let copied = await Task.detached {
      try? FileManager.default.removeItem(at: copy)
      return Probe.run(ditto, ["--norsrc", "--noextattr", "--noqtn", source.path, copy.path]) != nil
    }.value
    guard copied else { return "No he podido abrir una copia de la app para preparar la tarjeta." }
    let config = NSWorkspace.OpenConfiguration()
    config.arguments = ["--prepare", volume.path]
    config.createsNewApplicationInstance = true
    let opened = await withCheckedContinuation { done in
      NSWorkspace.shared.openApplication(at: copy, configuration: config) { app, _ in done.resume(returning: app != nil) }
    }
    guard opened else { return "No he podido abrir una copia de la app para preparar la tarjeta." }
    NSApp.terminate(nil)
    return nil
  }

  /// The volume a hand-over asked to prepare.
  static var requested: URL? {
    let args = CommandLine.arguments
    guard let i = args.firstIndex(of: "--prepare"), i + 1 < args.count else { return nil }
    return URL(fileURLWithPath: args[i + 1])
  }

  private static func plist(_ args: [String]) -> [String: Any]? {
    Probe.run(diskutil, args).flatMap { try? PropertyListSerialization.propertyList(from: $0, format: nil) as? [String: Any] }
  }
}
