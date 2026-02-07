import React, { createContext, useContext, useState } from "react";
import { Device } from "react-native-ble-plx";
import { bleService } from "./BLEService";

type BLEContextType = {
  scanning: boolean;
  connectedDevice: Device | null;
  startScan: (cb: (d: Device) => void) => void;
  stopScan: () => void;
  connect: (id: string) => Promise<Device>;
  disconnect: () => Promise<void>;
};

const BLEContext = createContext<BLEContextType | null>(null);

export const BLEProvider = ({ children }: { children: React.ReactNode }) => {
  const [scanning, setScanning] = useState(false);
  const [connectedDevice, setConnectedDevice] = useState<Device | null>(null);

  const startScan = (cb: (d: Device) => void) => {
    setScanning(true);
    bleService.startScan(
      cb,
      (err) => {
        console.error("BLE Scan Error:", err);
        setScanning(false);
      }
    );
  };

  const stopScan = () => {
    bleService.stopScan();
    setScanning(false);
  };

  const connect = async (id: string): Promise<Device> => {
    const device = await bleService.connect(id);
    setConnectedDevice(device);
    return device;
  };

  const disconnect = async () => {
    await bleService.disconnect();
    setConnectedDevice(null);
  };

  return (
    <BLEContext.Provider
      value={{
        scanning,
        connectedDevice,
        startScan,
        stopScan,
        connect,
        disconnect,
      }}
    >
      {children}
    </BLEContext.Provider>
  );
};

export const useBLE = () => {
  const ctx = useContext(BLEContext);
  if (!ctx) throw new Error("useBLE must be used inside BLEProvider");
  return ctx;
};
