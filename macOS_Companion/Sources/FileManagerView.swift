import SwiftUI
import UniformTypeIdentifiers

struct FileManagerView: View {
    @StateObject private var model: FileManagerModel
    @Environment(\.colorScheme) var colorScheme

    init(bluetoothManager: BluetoothManager) {
        _model = StateObject(wrappedValue: FileManagerModel(bluetoothManager: bluetoothManager))
    }

    var body: some View {
        HStack(spacing: 0) {
            // Sidebar
            VStack(alignment: .leading) {
                Text("Locations")
                    .font(.caption)
                    .fontWeight(.bold)
                    .foregroundColor(.secondary)
                    .padding(.horizontal)
                    .padding(.top)

                List {
                    Button(action: { model.loadDirectory(path: "/sd") }) {
                        Label("SD Card", systemImage: "sdcard")
                    }
                    .buttonStyle(.plain)

                    Button(action: { model.loadDirectory(path: "/littlefs") }) {
                        Label("Internal Storage", systemImage: "internaldrive")
                    }
                    .buttonStyle(.plain)
                }
                .listStyle(.sidebar)
            }
            .frame(width: 200)

            Divider()

            // Main Content
            VStack(spacing: 0) {
                // Toolbar
                HStack {
                    Button(action: { model.navigateUp() }) {
                        Image(systemName: "chevron.up")
                    }
                    .disabled(model.currentPath == "/" || model.currentPath == "/sd" || model.currentPath == "/littlefs")

                    Text(model.currentPath)
                        .font(.headline)
                        .padding(.leading, 8)

                    Spacer()

                    if model.isLoading {
                        ProgressView()
                            .scaleEffect(0.8)
                    }

                    Button(action: { model.refresh() }) {
                        Image(systemName: "arrow.clockwise")
                    }
                }
                .padding()
                .background(colorScheme == .dark ? Color(NSColor.windowBackgroundColor) : Color.white)

                Divider()

                // File Grid
                ScrollView {
                    LazyVGrid(columns: [GridItem(.adaptive(minimum: 120, maximum: 160))], spacing: 20) {
                        ForEach(model.files) { file in
                            FileCellView(file: file, model: model)
                        }
                    }
                    .padding()
                }
                .background(colorScheme == .dark ? Color(NSColor.underPageBackgroundColor) : Color(NSColor.controlBackgroundColor))
                // Drop Dest setup for macOS UI Native feeling
                .onDrop(of: [.fileURL], isTargeted: nil) { providers in
                    for provider in providers {
                        _ = provider.loadObject(ofClass: URL.self) { url, _ in
                            if let url = url {
                                model.upload(fileURL: url)
                            }
                        }
                    }
                    return true
                }
            }
        }
        .onAppear {
            model.loadDirectory(path: "/sd/")
        }
    }
}

struct FileCellView: View {
    let file: FileItem
    @ObservedObject var model: FileManagerModel

    @State private var isHovering = false

    var body: some View {
        VStack {
            ZStack {
                Image(systemName: file.isDirectory ? "folder.fill" : "doc.fill")
                    .resizable()
                    .scaledToFit()
                    .frame(width: 60, height: 60)
                    .foregroundColor(file.isDirectory ? .blue : .secondary)

                if isHovering {
                    RoundedRectangle(cornerRadius: 8)
                        .fill(Color.black.opacity(0.1))
                }
            }
            .frame(width: 100, height: 100)

            Text(file.name)
                .font(.callout)
                .lineLimit(2)
                .multilineTextAlignment(.center)
        }
        .padding(8)
        .background(isHovering ? Color.primary.opacity(0.1) : Color.clear)
        .cornerRadius(8)
        .onHover { hovering in
            isHovering = hovering
        }
        .onTapGesture(count: 2) {
            if file.isDirectory {
                model.loadDirectory(path: file.path)
            }
        }
        .contextMenu {
            if !file.isDirectory {
                Button("Download") {
                    let panel = NSSavePanel()
                    panel.nameFieldStringValue = file.name
                    if panel.runModal() == .OK, let url = panel.url {
                        model.download(file: file, to: url.deletingLastPathComponent())
                    }
                }
            }
            Button("Delete", role: .destructive) {
                model.delete(file: file)
            }
        }
    }
}
