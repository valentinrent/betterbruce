import Foundation
import CoreBluetooth

class BluetoothManager: NSObject, ObservableObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    @Published var isConnected = false
    @Published var receivedData = ""
    @Published var devices: [CBPeripheral] = []

    private var centralManager: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var serialCharacteristic: CBCharacteristic?

    let serviceUUID = CBUUID(string: "4371ec0b-3d43-49f9-b731-7c72a4a7bb91")
    let characteristicUUID = CBUUID(string: "d555ed97-bf2a-4f46-b3eb-d1fcdd7325e9")

    override init() {
        super.init()
        centralManager = CBCentralManager(delegate: self, queue: nil)
    }

    func scan() {
        if centralManager.state == .poweredOn {
            print("Scanning for all devices...")
            // Scan for all devices since the custom 128-bit UUID might be
            // truncated in the ESP32's advertising packet.
            centralManager.scanForPeripherals(withServices: nil, options: nil)
        }
    }

    func connect(`to` device: CBPeripheral) {
        centralManager.stopScan()
        peripheral = device
        peripheral?.delegate = self
        centralManager.connect(device, options: nil)
    }

    func sendCommand(_ command: String) {
        guard let peripheral = peripheral, let characteristic = serialCharacteristic else {
            print("Not connected.")
            return
        }

        let cmdWithNewline = command + "\n"
        if let data = cmdWithNewline.data(using: .utf8) {
            peripheral.writeValue(data, for: characteristic, type: .withResponse)
        }
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
        // Since we are scanning for all devices, let's filter the list by the
        // expected custom firmware name "Bruc" or "Bruce"
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
                self.receivedData += string
                print("Received: \(string)", terminator: "")
            }
        }
    }
}
