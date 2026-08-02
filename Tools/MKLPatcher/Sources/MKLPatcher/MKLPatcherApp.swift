import SwiftUI

@main
struct MKLPatcherApp: App {
    var body: some Scene {
        WindowGroup {
            ContentView()
                .frame(minWidth: 660, minHeight: 470)
        }
        .windowResizability(.contentMinSize)
    }
}

