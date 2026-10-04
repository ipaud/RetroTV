import AppKit
import SwiftUI

struct ImporterApp: App {
  @NSApplicationDelegateAdaptor(AppDelegate.self) private var delegate
  @StateObject private var model = ImporterModel()

  var body: some Scene {
    WindowGroup("RetroTV Importar") {
      ImporterView(model: model).onAppear {
        delegate.model = model
        model.appeared()
      }
    }
    .commands { CommandGroup(replacing: .newItem) {} }  // one window
  }
}

@MainActor final class AppDelegate: NSObject, NSApplicationDelegate {
  weak var model: ImporterModel? {
    didSet { if let model, !opened.isEmpty { model.drop(opened, onto: nil); opened = [] } }
  }
  private var opened: [URL] = []  // dropped on the icon before the window was up

  /// A folder or videos dropped on the app's icon (Finder, Dock): a new channel, as on the window.
  func application(_ application: NSApplication, open urls: [URL]) {
    if let model { model.drop(urls, onto: nil) } else { opened += urls }
  }

  func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }

  // Quitting mid-import stops it: the episodes already done stay, and importing the same folder again
  // with the same name carries on from there.
  func applicationWillTerminate(_ notification: Notification) { model?.cancel() }
}
