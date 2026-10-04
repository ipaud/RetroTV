import AppKit
import Foundation

/// The window's state: which card, its channels, and where the import is.
@MainActor
final class ImporterModel: ObservableObject {
  enum Stage { case noCard, home, probing, plan, converting, finished, preparing }

  struct Plan {
    var videos: [Probe.Video] = []
    var skipped: [String] = []  // videos that cannot be converted, with why
    var channel: Card.Channel?  // nil: a new channel
    var name = ""
    var tracks: [Probe.AudioTrack] = []
    var track = 0
    var bytes: Int64 = 0
  }

  struct Progress {
    var done = 0
    var fraction = 0.0  // of the current episode
    var total = 0
    var name = ""
    var started = Date()
  }

  @Published private(set) var card: Card?
  @Published private(set) var listing: Card.Listing?
  @Published private(set) var stage = Stage.noCard
  @Published var plan = Plan()
  @Published private(set) var progress = Progress()
  @Published private(set) var summary = Conversion.Summary()
  @Published private(set) var problem: String?
  @Published private(set) var highlight: String?  // the channel just imported into, shown in the list
  @Published var confirmingErase = false
  @Published private(set) var eraseTarget: Preparer.Disk?
  @Published private(set) var busy = ""  // what the preparing stage says

  private let conversion = Conversion()
  private let tools = Conversion.Tools.bundled()
  private var decoders: Set<String>?
  private var chosenCard: URL?
  private var handedOver = false

  init() {
    let center = NSWorkspace.shared.notificationCenter
    for name in [NSWorkspace.didMountNotification, NSWorkspace.didUnmountNotification] {
      center.addObserver(forName: name, object: nil, queue: .main) { [weak self] _ in
        MainActor.assumeIsolated { self?.refresh() }
      }
    }
    chosenCard = Preparer.requested
    refresh()
    if tools == nil { problem = "A esta app le faltan piezas (ffmpeg o convert_video.sh). Vuelve a copiarla en la tarjeta." }
  }

  /// Looks for the card again; leaves an import in progress alone.
  func refresh() {
    guard stage == .noCard || stage == .home || stage == .finished else { return }
    card = chosenCard.flatMap(Card.at) ?? Card.find()
    listing = card?.listing()
    if confirmingErase && card?.root != eraseTarget?.volume {  // the card went away while asked
      confirmingErase = false
      eraseTarget = nil
    }
    if card?.writable == false {
      problem = Card.lockedMessage
    } else if problem == Card.lockedMessage {
      problem = nil
    }
    if stage != .finished { stage = card == nil ? .noCard : .home }
  }

  func choose(card url: URL) {
    let volume = (try? url.resourceValues(forKeys: [.volumeURLKey]).volume) ?? url
    guard Card.at(volume) != nil else {
      problem = "\(volume.lastPathComponent) no puede ser la tarjeta de la tele: elige la tarjeta (FAT32) que has metido."
      return
    }
    problem = nil
    chosenCard = volume
    stage = .noCard
    refresh()
  }

  func drop(_ urls: [URL], onto channel: Card.Channel?) {
    guard let tools, stage == .home || stage == .finished else { return }
    if card?.isExFAT == true {
      problem = "Primero prepara la tarjeta: la tele no lee exFAT."
      return
    }
    if card?.writable == false {
      problem = Card.lockedMessage
      return
    }
    if let broken = listing?.problem {  // never import into a channels.json that cannot be read
      problem = broken
      return
    }
    stage = .probing
    let known = decoders
    Task {
      let (videos, decoders) = await Task.detached {
        let found = Probe.videos(in: urls).map { ($0, Probe.video($0, ffprobe: tools.ffprobe)) }
        return (found, known ?? Probe.decoders(ffmpeg: tools.ffmpeg))
      }.value
      self.decoders = decoders

      var plan = Plan(channel: channel)
      for (url, video) in videos {
        guard let video else {
          plan.skipped.append("\(url.lastPathComponent): no se puede leer")
          continue
        }
        if let codec = video.videoCodec, !decoders.contains(codec) {
          plan.skipped.append("\(url.lastPathComponent): vídeo en \(codec.uppercased()), este formato no se puede convertir")
          continue
        }
        plan.videos.append(video)
      }
      plan.tracks = plan.videos.first?.audio ?? []
      plan.bytes = Int64(plan.videos.reduce(0) { $0 + $1.seconds } * Probe.bytesPerSecond)
      let folder = urls.count == 1 && urls[0].hasDirectoryPath ? urls[0] : urls.first?.deletingLastPathComponent()
      plan.name = String((folder?.lastPathComponent ?? "").prefix(ChannelsFile.nameMaxLength))
      self.plan = plan
      stage = .plan
    }
  }

  func backHome() {
    highlight = nil
    stage = card == nil ? .noCard : .home
    refresh()
  }

  func start() {
    guard let card, let listing, let tools, stage == .plan else { return }
    let destination: Conversion.Destination
    if let channel = plan.channel {
      destination = .existing(source: channel.source)
    } else {
      do {
        destination = .newChannel(try ChannelsFile.addLocal(name: plan.name, to: listing.text))
      } catch {
        problem = "\(error)"
        return
      }
    }
    problem = nil
    progress = Progress(total: plan.videos.count)
    stage = .converting
    let request = Conversion.Request(card: card.root, videos: plan.videos.map(\.url),
                                     seconds: Dictionary(plan.videos.map { ($0.url, $0.seconds) }) { a, _ in a },
                                     destination: destination, track: plan.track)
    highlight = nil
    Task {
      summary = await conversion.run(request, tools: tools) { event in
        switch event {
        case .started(_, _, let name):
          progress.name = name
          progress.fraction = 0
        case .progress(let fraction):
          progress.fraction = fraction
        case .finished(let index, _, _, _):
          progress.done = index
          progress.fraction = 0
        }
      }
      switch destination {
      case .newChannel(let added): highlight = summary.added == nil ? nil : added.id
      case .existing: highlight = plan.channel?.id
      }
      stage = .finished
      refresh()
    }
  }

  func cancel() { conversion.cancel() }

  /// Once the window is up (an alert set before it never shows): a copy handed over goes on with the
  /// preparation it was opened for, only if that card is still the one there.
  func appeared() {
    guard let requested = Preparer.requested, !handedOver else { return }
    handedOver = true
    NSApp.activate(ignoringOtherApps: true)
    guard card?.root.path == requested.path, card?.isExFAT == true else {
      problem = "La tarjeta que ibas a preparar ya no está: métela y vuelve a pulsar Preparar."
      return
    }
    DispatchQueue.main.async { self.prepare() }
  }

  /// An exFAT card: check it can be erased, then ask (the window shows the alert). Only a copy handed over
  /// asks; any other first opens that copy (Preparer.handOver).
  func prepare() {
    guard let card, stage == .home || stage == .noCard else { return }
    guard card.writable else {
      problem = Card.lockedMessage
      return
    }
    let volume = card.root
    problem = nil
    busy = "Comprobando la tarjeta…"
    stage = .preparing
    Task {
      let checked = await Task.detached { Preparer.disk(of: volume) }.value
      switch checked {
      case .failure(let why):
        problem = why.description
      case .success where Preparer.requested == nil:
        busy = "Abriendo una copia de la app…"
        if let failure = await Preparer.handOver(preparing: volume) { problem = failure }
      case .success(let disk):
        eraseTarget = disk
        confirmingErase = true
      }
      backHome()
    }
  }

  func erase() {
    guard let disk = eraseTarget else { return }
    eraseTarget = nil
    problem = nil
    busy = "Preparando la tarjeta… no la saques"
    stage = .preparing
    Task {
      let result = await Task.detached { Result { try Preparer.prepare(disk) } }.value
      switch result {
      case .success(let root): chosenCard = root
      case .failure(let error): problem = "\(error)"
      }
      backHome()
    }
  }

  /// The app may be running from the card itself, which cannot be ejected while it is open: a shell from
  /// the Mac's disk waits for it to quit, then ejects.
  func ejectAndQuit() {
    if let card {
      let p = Process()
      p.executableURL = URL(fileURLWithPath: "/bin/sh")
      p.arguments = ["-c", "sleep 1; /usr/sbin/diskutil eject \"$0\" >/dev/null", card.root.path]
      try? p.run()
    }
    NSApp.terminate(nil)
  }
}
