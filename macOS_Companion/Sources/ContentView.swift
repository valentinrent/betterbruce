import SwiftUI
import CoreBluetooth

struct ContentView: View {
    @StateObject private var bluetoothManager = BluetoothManager()
    @State private var commandInput = "ls /"

    var body: some View {
        VStack(spacing: 20) {
            Text("Bruce macOS Companion")
                .font(.largeTitle)
                .bold()

            if !bluetoothManager.isConnected {
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
            } else {
                Text("Connected!")
                    .foregroundColor(.green)
                    .bold()

                HStack {
                    TextField("Command", text: $commandInput)
                        .textFieldStyle(RoundedBorderTextFieldStyle())

                    Button("Send") {
                        bluetoothManager.sendCommand(commandInput)
                    }
                }

                ScrollView {
                    Text(bluetoothManager.receivedData)
                        .font(.system(.body, design: .monospaced))
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .padding()
                        .background(Color.black.opacity(0.8))
                        .foregroundColor(.green)
                        .cornerRadius(8)
                }

                Button("Clear Terminal") {
                    bluetoothManager.receivedData = ""
                }
            }
        }
        .padding()
        .frame(minWidth: 500, minHeight: 400)
    }
}
