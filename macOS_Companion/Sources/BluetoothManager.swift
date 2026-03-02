import Foundation
import CoreBluetooth

class BluetoothManager: NSObject, ObservableObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    @Published var isConnected = false
    @Published var terminalOutput = ""
    @Published var selectedDevice: CBPeripheral?
    @Published var devices: [CBPeripheral] = []

    // Internal state for command execution
    private var centralManager: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var serialCharacteristic: CBCharacteristic?

    private var responseBuffer = ""
    private var responseTimeoutTimer: Timer?
    private var pendingCompletion: ((String) -> Void)?

    // UUIDs
    let serviceUUID = CBUUID(string: "4371ec0b-3d43-49f9-b731-7c72a4a7bb91")
    let characteristicUUID = CBUUID(string: "d555ed97-bf2a-4f46-b3eb-d1fcdd7325e9")

    override init() {
        super.init()
        centralManager = CBCentralManager(delegate: self, queue: nil)
    }

    func scan() {
        if centralManager.state == .poweredOn {
            print("Scanning for all devices...")
            centralManager.scanForPeripherals(withServices: nil, options: nil)
        }
    }

    func connect(to device: CBPeripheral) {
        centralManager.stopScan()
        selectedDevice = device
        peripheral = device
        peripheral?.delegate = self
        centralManager.connect(device, options: nil)
    }

    func disconnect() {
        if let peripheral = peripheral {
            centralManager.cancelPeripheralConnection(peripheral)
        }
    }

    // High level command execution with completion handler
    func executeCommand(_ command: String, completion: @escaping (String) -> Void) {
        guard let _ = serialCharacteristic else {
            completion("Error: Not connected")
            return
        }

        // Wait for previous command to finish if needed, but for simplicity here we just override
        self.pendingCompletion = completion
        self.responseBuffer = ""

        sendCommand(command)

        // Set a fallback timeout in case we get no response at all
        resetTimeoutTimer(delay: 2.0)
    }

    // Low level raw send
    func sendCommand(_ command: String) {
        guard let peripheral = peripheral, let characteristic = serialCharacteristic else { return }

        let cmdWithNewline = command + "\n"
        if let data = cmdWithNewline.data(using: .utf8) {
            // BLE handles max MTU, CoreBluetooth splits it automatically for withResponse,
            // but for withoutResponse it might need splitting. We use withResponse if supported.
            peripheral.writeValue(data, for: characteristic, type: .withResponse)
        }

        DispatchQueue.main.async {
            self.terminalOutput += "> \(command)\n"
        }
    }

    private func resetTimeoutTimer(delay: TimeInterval = 0.5) {
        responseTimeoutTimer?.invalidate()
        responseTimeoutTimer = Timer.scheduledTimer(withTimeInterval: delay, repeats: false) { [weak self] _ in
            self?.finalizeResponse()
        }
    }

    private func finalizeResponse() {
        guard let completion = pendingCompletion else { return }
        let result = responseBuffer
        responseBuffer = ""
        pendingCompletion = nil
        completion(result)
    }

    // MARK: - CBCentralManagerDelegate

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        if central.state == .poweredOn {
            scan()
        } else {
            print("Bluetooth is not available.")
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral, advertisementData: [String : Any], rssi RSSI: NSNumber) {
        if let name = peripheral.name, name.hasPrefix("Bruc") {
            if !devices.contains(where: { $0.identifier == peripheral.identifier }) {
                DispatchQueue.main.async {
                    self.devices.append(peripheral)
                    print("Discovered: \(name)")
                }
            }
        }
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        print("Connected to \(peripheral.name ?? "Unknown")")
        DispatchQueue.main.async {
            self.isConnected = true
        }
        peripheral.discoverServices([serviceUUID])
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        print("Disconnected")
        DispatchQueue.main.async {
            self.isConnected = false
            self.selectedDevice = nil
        }
    }

    // MARK: - CBPeripheralDelegate

    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard let services = peripheral.services else { return }
        for service in services {
            if service.uuid == serviceUUID {
                peripheral.discoverCharacteristics([characteristicUUID], for: service)
            }
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        guard let characteristics = service.characteristics else { return }
        for characteristic in characteristics {
            if characteristic.uuid == characteristicUUID {
                self.serialCharacteristic = characteristic
                peripheral.setNotifyValue(true, for: characteristic)
                print("Found serial characteristic. Ready!")
            }
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        if let data = characteristic.value, let string = String(data: data, encoding: .utf8) {
            DispatchQueue.main.async {
                self.terminalOutput += string

                if self.pendingCompletion != nil {
                    self.responseBuffer += string
                    self.resetTimeoutTimer(delay: 0.3) // 300ms idle timeout indicates end of output
                }
            }
        }
    }
}
