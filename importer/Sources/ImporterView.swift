import AppKit
import SwiftUI
import UniformTypeIdentifiers

// The window: the card's channels drawn like the TV's teletext guide, and under it what to do next.

extension Color {
  static let ttYellow = Color(red: 1, green: 0.86, blue: 0.1)
  static let ttCyan = Color(red: 0.3, green: 0.9, blue: 0.95)
  static let ttBlue = Color(red: 0.1, green: 0.22, blue: 0.75)
}

/// A name as the TV draws it: uppercase ASCII (the firmware folds accents, ChannelManager.cpp).
func tvName(_ name: String) -> String { name.folding(options: .diacriticInsensitive, locale: nil).uppercased() }

@MainActor private let bytes: ByteCountFormatter = {
  let f = ByteCountFormatter()
  f.countStyle = .file
  return f
}()

struct ImporterView: View {
  @ObservedObject var model: ImporterModel

  var body: some View {
    VStack(spacing: 0) {
      Header(model: model)
      Divider()
      if let problem = model.problem {
        Label(problem, systemImage: "exclamationmark.triangle.fill")
          .foregroundColor(.orange)
          .frame(maxWidth: .infinity, alignment: .leading)
          .padding(.horizontal, 20)
          .padding(.top, 12)
      }
      if model.stage == .noCard {
        NoCard(model: model)
      } else {
        if let listing = model.listing {
          ChannelGuide(listing: listing, acceptsDrops: model.stage == .home || model.stage == .finished,
                       highlight: model.highlight) { urls, channel in
            model.drop(urls, onto: channel)
          }
        }
        panel
          .frame(maxWidth: .infinity, minHeight: 230, alignment: .top)
          .padding(.horizontal, 20)
          .padding(.bottom, 20)
      }
    }
    .frame(minWidth: 580, maxWidth: .infinity, minHeight: 640, maxHeight: .infinity)
    .alert("¿Borrar la tarjeta \(model.card?.name ?? "")?", isPresented: $model.confirmingErase) {
      Button("Borrar y preparar", role: .destructive, action: model.erase)
      Button("Cancelar", role: .cancel) {}
    } message: {
      Text("Se borrará todo lo que tiene (\(bytes.string(fromByteCount: model.eraseTarget?.size ?? 0))). "
           + "Quedará en FAT32, con el nombre RETROTV y esta app dentro.")
    }
  }

  @ViewBuilder private var panel: some View {
    switch model.stage {
    case .noCard, .home:
      if model.card?.isExFAT == true { PreparePanel(model: model) } else { DropZone(model: model) }
    case .probing: ProgressView("Mirando los vídeos…").padding(.top, 60)
    case .preparing: ProgressView(model.busy).padding(.top, 60)
    case .plan: PlanPanel(model: model)
    case .converting: ProgressPanel(progress: model.progress, stop: model.cancel)
    case .finished: SummaryPanel(model: model)
    }
  }
}

private struct Header: View {
  @ObservedObject var model: ImporterModel

  var body: some View {
    HStack(spacing: 14) {
      Image(nsImage: NSApp.applicationIconImage)
        .resizable()
        .frame(width: 52, height: 52)
        .accessibilityHidden(true)
      VStack(alignment: .leading, spacing: 3) {
        Text("RetroTV Importar").font(.title2.weight(.bold))
        if let card = model.card {
          Text("Tarjeta \(card.name) · \(bytes.string(fromByteCount: card.freeBytes)) libres de \(bytes.string(fromByteCount: card.totalBytes))")
            .foregroundColor(.secondary)
        } else {
          Text("Sin tarjeta").foregroundColor(.secondary)
        }
      }
      Spacer()
    }
    .padding(.horizontal, 20)
    .padding(.vertical, 14)
  }
}

private struct NoCard: View {
  @ObservedObject var model: ImporterModel

  var body: some View {
    VStack(spacing: 14) {
      Spacer()
      Image(systemName: "sdcard").font(.system(size: 54, weight: .light)).foregroundColor(.secondary)
      Text("Mete la tarjeta de la tele en el Mac").font(.title3.weight(.semibold))
      Text("Cuando aparezca, esta ventana la encontrará sola.").foregroundColor(.secondary)
      Button("La tarjeta ya está dentro…") { chooseCard(model) }
      Spacer()
    }
    .frame(maxWidth: .infinity)
  }
}

@MainActor private func chooseCard(_ model: ImporterModel) {
  let panel = NSOpenPanel()
  panel.message = "Elige la tarjeta de la tele"
  panel.canChooseDirectories = true
  panel.canChooseFiles = false
  panel.directoryURL = URL(fileURLWithPath: "/Volumes")
  if panel.runModal() == .OK, let url = panel.url { model.choose(card: url) }
}

/// The teletext page: P100, a blue title bar, yellow numbers, the colour bar at the bottom.
private struct ChannelGuide: View {
  let listing: Card.Listing
  let acceptsDrops: Bool
  let highlight: String?
  let onDrop: ([URL], Card.Channel) -> Void
  @State private var targeted: String?

  var body: some View {
    VStack(alignment: .leading, spacing: 0) {
      HStack {
        Text("P100").foregroundColor(.ttYellow)
        Text("RETROTV").foregroundColor(.white)
        Spacer()
        Text("\(listing.local.count) CANALES").foregroundColor(.ttYellow)
      }
      .padding(.horizontal, 14)
      .padding(.top, 10)
      Text("EN LA TARJETA")
        .font(.system(size: 17, weight: .heavy, design: .monospaced))
        .foregroundColor(.ttYellow)
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.horizontal, 14)
        .padding(.vertical, 5)
        .background(Color.ttBlue)
        .padding(.vertical, 8)
      if let problem = listing.problem {
        Text(problem.uppercased()).foregroundColor(.ttYellow).padding(.horizontal, 14)
      }
      ScrollViewReader { scroller in
        ScrollView {
          LazyVStack(alignment: .leading, spacing: 1) {
            ForEach(listing.local) { row($0).id($0.id) }
            if listing.local.isEmpty && listing.problem == nil {
              Text("AÚN NO HAY SERIES: SUELTA UNA CARPETA ABAJO").foregroundColor(.ttCyan).padding(.horizontal, 14)
            }
          }
        }
        .onChange(of: highlight) { id in
          if let id { withAnimation { scroller.scrollTo(id, anchor: .center) } }
        }
      }
      if listing.others > 0 {
        Text("+ \(listing.others) DE LA TELE: TELETEXTO, CARTA...")
          .foregroundColor(.ttCyan.opacity(0.8))
          .padding(.horizontal, 14)
          .padding(.top, 6)
      }
      HStack(spacing: 0) {
        ForEach([Color.red, .ttYellow, .green, .ttCyan, .blue, .purple], id: \.self) { $0 }
      }
      .frame(height: 6)
      .padding(.horizontal, 14)
      .padding(.vertical, 10)
    }
    .font(.system(size: 13, weight: .bold, design: .monospaced))
    .background(Color.black)
    .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
    .padding(20)
  }

  private func row(_ channel: Card.Channel) -> some View {
    let canDrop = acceptsDrops && channel.folder != nil
    let isTargeted = Binding(get: { targeted == channel.id },
                             set: { targeted = $0 ? channel.id : (targeted == channel.id ? nil : targeted) })
    return HStack(spacing: 0) {
      Text(String(format: "%02d", channel.number)).foregroundColor(.ttYellow).frame(width: 46, alignment: .leading)
      Text(tvName(channel.name)).foregroundColor(.white).lineLimit(1)
      Spacer(minLength: 12)
      Text(isTargeted.wrappedValue ? "+ AÑADIR" : episodes(channel)).foregroundColor(.ttCyan)
    }
    .padding(.horizontal, 14)
    .padding(.vertical, 3)
    .background(isTargeted.wrappedValue ? Color.ttBlue : highlight == channel.id ? Color.ttBlue.opacity(0.55) : .clear)
    .onDrop(of: canDrop ? [.fileURL] : [], isTargeted: isTargeted) { providers in
      Task { @MainActor in onDrop(await fileURLs(providers), channel) }
      return true
    }
    .accessibilityElement(children: .combine)
    .accessibilityLabel("Canal \(channel.number), \(channel.name), \(episodes(channel).lowercased())")
  }

  private func episodes(_ channel: Card.Channel) -> String {
    if channel.folder == nil { return channel.episodes > 0 ? "CLIP" : "NO ESTÁ" }  // one .mjpeg, not a folder
    switch channel.episodes {
    case 0: return "VACÍO"
    case 1: return "1 CAPÍTULO"
    default: return "\(channel.episodes) CAPÍTULOS"
    }
  }
}

@MainActor private func fileURLs(_ providers: [NSItemProvider]) async -> [URL] {
  var urls: [URL] = []
  for provider in providers {
    let url: URL? = await withCheckedContinuation { done in
      _ = provider.loadObject(ofClass: URL.self) { url, _ in done.resume(returning: url) }
    }
    if let url { urls.append(url) }
  }
  return urls
}

private struct DropZone: View {
  @ObservedObject var model: ImporterModel
  @State private var targeted = false

  var body: some View {
    VStack(spacing: 10) {
      VStack(spacing: 8) {
        Image(systemName: "tray.and.arrow.down").font(.system(size: 30, weight: .regular))
        Text("Suelta aquí una carpeta de capítulos").font(.headline)
        Text("y se convierte en un canal nuevo de la tarjeta").foregroundColor(.secondary)
        Button("Elegir carpeta…", action: choose).padding(.top, 4)
      }
      .frame(maxWidth: .infinity, minHeight: 170)
      .background(RoundedRectangle(cornerRadius: 14).fill(Color.accentColor.opacity(targeted ? 0.12 : 0)))
      .overlay(RoundedRectangle(cornerRadius: 14)
        .strokeBorder(targeted ? Color.accentColor : Color.secondary.opacity(0.45),
                      style: StrokeStyle(lineWidth: 2, dash: targeted ? [] : [7, 5])))
      .onDrop(of: [.fileURL], isTargeted: $targeted) { providers in
        Task { @MainActor in model.drop(await fileURLs(providers), onto: nil) }
        return true
      }
      Text("Para añadir capítulos a un canal que ya existe, suéltalos encima de él, en la lista.")
        .font(.callout)
        .foregroundColor(.secondary)
    }
  }

  private func choose() {
    let panel = NSOpenPanel()
    panel.message = "Elige una carpeta de capítulos (o los vídeos)"
    panel.canChooseDirectories = true
    panel.allowsMultipleSelection = true
    if panel.runModal() == .OK { model.drop(panel.urls, onto: nil) }
  }
}

/// A new card in exFAT: the TV cannot read it until it is erased as FAT32.
private struct PreparePanel: View {
  @ObservedObject var model: ImporterModel

  var body: some View {
    VStack(alignment: .leading, spacing: 12) {
      Label("Esta tarjeta está en exFAT y la tele no la lee", systemImage: "exclamationmark.triangle.fill")
        .font(.title3.weight(.semibold))
        .foregroundColor(.orange)
      Text("Las tarjetas de más de 32 GB vienen así de fábrica. Hay que borrarla y dejarla en FAT32: "
           + "se pierde lo que tenga dentro, y esta app se vuelve a copiar en ella.")
        .fixedSize(horizontal: false, vertical: true)
      Button("Preparar la tarjeta…", action: model.prepare).padding(.top, 4)
      Spacer(minLength: 0)
    }
  }
}

private struct PlanPanel: View {
  @ObservedObject var model: ImporterModel

  var body: some View {
    let plan = model.plan
    let free = model.card?.freeBytes ?? 0
    VStack(alignment: .leading, spacing: 12) {
      Text(plan.channel.map { "Añadir al canal \($0.number) · \($0.name)" } ?? "Canal nuevo").font(.title3.weight(.semibold))
      if plan.videos.isEmpty {
        Text("Aquí no hay vídeos que se puedan convertir (mp4, mkv, avi, mov, m4v o webm).").foregroundColor(.secondary)
      } else {
        Text("\(plan.videos.count) \(plan.videos.count == 1 ? "vídeo" : "vídeos") · ocuparán unos \(bytes.string(fromByteCount: plan.bytes)) · quedan \(bytes.string(fromByteCount: free))")
          .foregroundColor(.secondary)
        if plan.bytes > free {
          Label("Puede que no quepa todo: lo que no quepa fallará.", systemImage: "exclamationmark.triangle.fill")
            .foregroundColor(.orange)
        }
      }
      if plan.channel == nil && !plan.videos.isEmpty {
        HStack {
          Text("Nombre").frame(width: 64, alignment: .leading)
          TextField("Nombre del canal", text: $model.plan.name)
            .frame(maxWidth: 300)
            .onChange(of: model.plan.name) { name in
              if name.count > ChannelsFile.nameMaxLength { model.plan.name = String(name.prefix(ChannelsFile.nameMaxLength)) }
            }
        }
        Text("En la tele: \(tvName(model.plan.name))")
          .font(.system(size: 12, weight: .bold, design: .monospaced))
          .foregroundColor(.secondary)
          .padding(.leading, 72)
      }
      if plan.tracks.count > 1 {
        HStack {
          Text("Audio").frame(width: 64, alignment: .leading)
          Picker("Audio", selection: $model.plan.track) {
            ForEach(Array(plan.tracks.enumerated()), id: \.offset) { i, track in Text(track.label(position: i)).tag(i) }
          }
          .labelsHidden()
          .fixedSize()
        }
      }
      if !plan.skipped.isEmpty {
        Text("Se saltarán:").font(.callout.weight(.semibold))
        ScrollView {
          VStack(alignment: .leading, spacing: 2) {
            ForEach(plan.skipped, id: \.self) { Text("• \($0)").font(.callout).foregroundColor(.secondary) }
          }
          .frame(maxWidth: .infinity, alignment: .leading)
        }
        .frame(maxHeight: 60)
      }
      Spacer(minLength: 0)
      HStack {
        Spacer()
        Button("Cancelar", action: model.backHome).keyboardShortcut(.cancelAction)
        Button("Convertir", action: model.start)
          .keyboardShortcut(.defaultAction)
          .disabled(plan.videos.isEmpty || (plan.channel == nil && plan.name.trimmingCharacters(in: .whitespaces).isEmpty))
      }
    }
  }
}

private struct ProgressPanel: View {
  let progress: ImporterModel.Progress
  let stop: () -> Void

  var body: some View {
    VStack(alignment: .leading, spacing: 12) {
      Text("Convirtiendo \(min(progress.done + 1, progress.total)) de \(progress.total)").font(.title3.weight(.semibold))
      Text(progress.name.isEmpty ? " " : progress.name).foregroundColor(.secondary).lineLimit(1).truncationMode(.middle)
      ProgressView(value: min(Double(progress.done) + progress.fraction, Double(progress.total)),
                   total: Double(max(progress.total, 1)))
      HStack(spacing: 4) {
        Text("Tiempo:")
        Text(progress.started, style: .timer).monospacedDigit()
      }
      .foregroundColor(.secondary)
      Text("No saques la tarjeta hasta que termine.").font(.callout).foregroundColor(.secondary)
      Spacer(minLength: 0)
      HStack {
        Spacer()
        Button("Detener", action: stop).keyboardShortcut(.cancelAction)
      }
    }
  }
}

private struct SummaryPanel: View {
  @ObservedObject var model: ImporterModel

  var body: some View {
    let s = model.summary
    let ok = s.failed.isEmpty && s.problem == nil && !s.cancelled
    VStack(alignment: .leading, spacing: 10) {
      Label(s.cancelled ? "Conversión detenida" : ok ? "¡Listo!" : "Terminado, con problemas",
            systemImage: ok ? "checkmark.circle.fill" : "exclamationmark.triangle.fill")
        .font(.title3.weight(.semibold))
        .foregroundColor(ok ? .green : .orange)
      Text(counts(s))
      if let added = s.added {
        Text("Canal nuevo: \(added.number) · \(model.plan.name)")
      }
      if let problem = s.problem { Text(problem).foregroundColor(.orange) }
      if !s.failed.isEmpty {
        ScrollView {
          VStack(alignment: .leading, spacing: 2) {
            ForEach(s.failed, id: \.self) { Text("• \($0)").font(.callout).foregroundColor(.secondary) }
          }
          .frame(maxWidth: .infinity, alignment: .leading)
        }
        .frame(maxHeight: 70)
      }
      Text("Después mete la tarjeta en la tele: los canales nuevos aparecen al encenderla.")
        .font(.callout)
        .foregroundColor(.secondary)
      Spacer(minLength: 0)
      HStack {
        Button("Importar más", action: model.backHome)
        Spacer()
        Button("Expulsar la tarjeta y salir", action: model.ejectAndQuit).keyboardShortcut(.defaultAction)
      }
    }
  }

  private func counts(_ s: Conversion.Summary) -> String {
    var parts = ["\(s.converted) \(s.converted == 1 ? "capítulo convertido" : "capítulos convertidos")"]
    if s.skipped > 0 { parts.append("\(s.skipped) ya \(s.skipped == 1 ? "estaba" : "estaban")") }
    if !s.failed.isEmpty { parts.append("\(s.failed.count) con error") }
    return parts.joined(separator: " · ")
  }
}
