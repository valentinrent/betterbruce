import SwiftUI
import CoreBluetooth
import UniformTypeIdentifiers

struct FileItem: Identifiable, Hashable {
    let id = UUID()
    let name: String
    let isDirectory: Bool
    let size: Int64
    let path: String
}

class FileManagerModel: ObservableObject {
    @Published var files: [FileItem] = []
    @Published var currentPath: String = "/sd/"
    @Published var isLoading: Bool = false
    @Published var errorMessage: String? = nil

    private let bluetoothManager: BluetoothManager

    init(bluetoothManager: BluetoothManager) {
        self.bluetoothManager = bluetoothManager
    }

    func loadDirectory(path: String) {
        var cleanPath = path
        if cleanPath != "/" && !cleanPath.hasSuffix("/") {
            cleanPath += "/"
        }

        DispatchQueue.main.async {
            self.isLoading = true
            self.errorMessage = nil
            self.currentPath = cleanPath
        }

        bluetoothManager.executeCommand("ls \(cleanPath)") { [weak self] response in
            guard let self = self else { return }
            self.parseAndSetFiles(response: response, parentPath: cleanPath)
        }
    }

    // Response parsing:
    // Ex: "config.conf\t701"
    //     "BruceJS\t<DIR>"
    private func parseAndSetFiles(response: String, parentPath: String) {
        // Strip out ANSI codes
        var cleanResponse = response
        if let regex = try? NSRegularExpression(pattern: "\u{001B}\\[[0-9;]*[a-zA-Z]", options: .caseInsensitive) {
            cleanResponse = regex.stringByReplacingMatches(in: response, options: [], range: NSRange(location: 0, length: response.utf16.count), withTemplate: "")
        }

        // Split and filter
        let lines = cleanResponse.components(separatedBy: .newlines)
            .map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }
            .filter { !$0.isEmpty }

        var newFiles: [FileItem] = []

        for line in lines {
            let parts = line.components(separatedBy: "\t")
            guard parts.count >= 1 else { continue }

            let name = parts[0].trimmingCharacters(in: .whitespaces)
            // Sometimes the prompt or extra characters can bleed into the line
            if name.hasPrefix("ERROR:") || name.contains("does not exist") || name.hasPrefix(">") { continue }
            if name == "#" || name.hasPrefix("Bruce") { continue } // Ignore empty prompt-like outputs


            var isDir = false
            var size: Int64 = 0

            if parts.count >= 2 {
                let second = parts[1].trimmingCharacters(in: .whitespacesAndNewlines)
                if second == "<DIR>" {
                    isDir = true
                } else if let s = Int64(second) {
                    size = s
                }
            }

            let fullPath = (parentPath.hasSuffix("/") ? parentPath : parentPath + "/") + name
            newFiles.append(FileItem(name: name, isDirectory: isDir, size: size, path: fullPath))
        }

        DispatchQueue.main.async {
            self.files = newFiles.sorted(by: { a, b in
                if a.isDirectory == b.isDirectory {
                    return a.name.lowercased() < b.name.lowercased()
                }
                return a.isDirectory
            })
            self.isLoading = false
        }
    }

    func refresh() {
        loadDirectory(path: currentPath)
    }

    func delete(file: FileItem) {
        DispatchQueue.main.async { self.isLoading = true }
        let cmd = file.isDirectory ? "rmdir \"\(file.path)\"" : "rm \"\(file.path)\""
        bluetoothManager.executeCommand(cmd) { [weak self] response in
            self?.refresh() // Refresh regardless to confirm
        }
    }

    func rename(file: FileItem, to newName: String) {
        DispatchQueue.main.async { self.isLoading = true }
        let newPath = (currentPath.hasSuffix("/") ? currentPath : currentPath + "/") + newName
        bluetoothManager.executeCommand("storage rename \"\(file.path)\" \"\(newPath)\"") { [weak self] response in
            self?.refresh()
        }
    }

    func createDirectory(name: String) {
        DispatchQueue.main.async { self.isLoading = true }
        let fullPath = (currentPath.hasSuffix("/") ? currentPath : currentPath + "/") + name
        bluetoothManager.executeCommand("mkdir \"\(fullPath)\"") { [weak self] response in
            self?.refresh()
        }
    }

    func navigateUp() {
        var parts = currentPath.split(separator: "/").map(String.init).filter { !$0.isEmpty }
        guard parts.count > 1 else {
            loadDirectory(path: "/") // root
            return
        }
        parts.removeLast()
        let newPath = "/" + parts.joined(separator: "/") + "/"
        loadDirectory(path: newPath)
    }

    // Very rudimentary file upload using storage write.
    // LITE_VERSION firmware does not support this, but if we assume non LITE:
    // It takes storage write <path> <size> and reads raw bytes.
    func upload(fileURL: URL) {
        guard let data = try? Data(contentsOf: fileURL) else {
            DispatchQueue.main.async { self.errorMessage = "Failed to read local file" }
            return
        }

        let pathOnDevice = (currentPath.hasSuffix("/") ? currentPath : currentPath + "/") + fileURL.lastPathComponent
        DispatchQueue.main.async { self.isLoading = true }

        // Command requires exact size
        let cmd = "storage write \"\(pathOnDevice)\" \(data.count)"
        // Since the ESP firmware does `_readFileFromSerial(fileSize + 2)`, we should send cmd, then bytes.

        bluetoothManager.sendCommand(cmd)

        // Wait a tiny bit then send bytes to not overwhelm the BLE stack
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) {
            // we should technically use executeCommand, but sendCommand is low level enough
            // Let's send a fake completion handler to await for 1 sec
            self.bluetoothManager.executeCommand(String(data: data, encoding: .isoLatin1) ?? "") { [weak self] _ in
                self?.refresh()
            }
        }
    }

    // Download uses cat, limited to small files.
    func download(file: FileItem, to localDirectory: URL) {
        DispatchQueue.main.async { self.isLoading = true }
        bluetoothManager.executeCommand("cat \"\(file.path)\"") { [weak self] response in
            guard let self = self else { return }
            // Filter response (remove echo and command line)

            let destURL = localDirectory.appendingPathComponent(file.name)
            do {
                try response.write(to: destURL, atomically: true, encoding: .utf8)
                DispatchQueue.main.async {
                    self.isLoading = false
                    // maybe show success
                }
            } catch {
                DispatchQueue.main.async {
                    self.errorMessage = "Failed to save file: \(error.localizedDescription)"
                    self.isLoading = false
                }
            }
        }
    }
}
