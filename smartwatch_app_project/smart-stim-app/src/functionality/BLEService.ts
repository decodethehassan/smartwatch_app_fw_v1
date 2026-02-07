import { BleManager, Device } from "react-native-ble-plx";

class BLEService {
  private manager = new BleManager();
  private connectedDevice: Device | null = null;

  startScan(
    onDeviceFound: (device: Device) => void,
    onError: (error: any) => void
  ) {
    this.manager.startDeviceScan(null, { allowDuplicates: false }, (error, device) => {
      if (error) {
        onError(error);
        return;
      }
      if (device) onDeviceFound(device);
    });
  }

  stopScan() {
    this.manager.stopDeviceScan();
  }

  async connect(deviceId: string): Promise<Device> {
    const device = await this.manager.connectToDevice(deviceId, { timeout: 10000 });
    const readyDevice = await device.discoverAllServicesAndCharacteristics();
    this.connectedDevice = readyDevice;
    return readyDevice;
  }

  async disconnect(): Promise<void> {
    if (!this.connectedDevice) return;
    await this.manager.cancelDeviceConnection(this.connectedDevice.id);
    this.connectedDevice = null;
  }

  getConnectedDevice(): Device | null {
    return this.connectedDevice;
  }
}

export const bleService = new BLEService();
