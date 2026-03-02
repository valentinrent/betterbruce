import SwiftUI
import CoreBluetooth

struct ContentView: View {
    @StateObject private var bluetoothManager = BluetoothManager()
    @State private var commandInput = "ls /"
    @State private var selectedTab = 0

    var body: some View {
        VStack(spacing: 0) {
            if !bluetoothManager.isConnected {
                VStack(spacing: 20) {
                    Text("Bruce macOS Companion")
                        .font(.largeTitle)
                        .bold()

                    Text("Searching for Bruce...")
                        .foregroundColor(.secondary)

                    List(bluetoothManager.devices, id: \.identifier) { device in
                        HStack {
                            Text(device.name ?? "Unknown Device")
                            Spacer()
                            Button("Connect") {
                                bluetoothManager.connect(to: device)
                            }
                        }
                    }
                }
                .padding()
            } else {
                TabView(selection: $selectedTab) {
                    // TAB 1: FILE MANAGER
                    FileManagerView(bluetoothManager: bluetoothManager)
                        .tabItem {
                            Label("Files", systemImage: "folder")
                        }
                        .tag(0)

                    // TAB 2: TERMINAL
                    VStack(spacing: 10) {
                        HStack {
                            Text("Connected to \(bluetoothManager.selectedDevice?.name ?? "Bruce")")
                                .foregroundColor(.green)
                                .bold()
                            Spacer()
                            Button("Disconnect") {
                                bluetoothManager.disconnect()
                            }
                        }
                        .padding(.horizontal)

                        HStack {
                            TextField("Command", text: $commandInput)
                                .textFieldStyle(RoundedBorderTextFieldStyle())

                            Button("Send") {
                                bluetoothManager.sendCommand(commandInput)
                            }
                        }
                        .padding(.horizontal)

                        ScrollView {
                            Text(bluetoothManager.terminalOutput)
                                .font(.system(.body, design: .monospaced))
                                .frame(maxWidth: .infinity, alignment: .leading)
                                .padding()
                                .background(Color.black.opacity(0.8))
                                .foregroundColor(.green)
                                .cornerRadius(8)
                        }
                        .padding(.horizontal)

                        Button("Clear Terminal") {
                            bluetoothManager.terminalOutput = ""
                        }
                        .padding(.bottom)
                    }
                    .tabItem {
                        Label("Terminal", systemImage: "terminal")
                    }
                    .tag(1)
                }
            }
        }
        .frame(minWidth: 700, minHeight: 500)
    }
}
