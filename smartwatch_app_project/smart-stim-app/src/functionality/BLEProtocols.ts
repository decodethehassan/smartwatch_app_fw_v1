// Central place for all BLE UUID definitions

export const NUS_PROTOCOL = {
  SERVICE_UUID: "6e400001-b5a3-f393-e0a9-e50e24dcca9e",
  RX_UUID: "6e400002-b5a3-f393-e0a9-e50e24dcca9e", // write
  TX_UUID: "6e400003-b5a3-f393-e0a9-e50e24dcca9e", // notify
};

export const ESP32_PROTOCOL = {
  SERVICE_UUID: "4fafc201-1fb5-459e-8fcc-c5c9c331914b",
  RX_UUID: "beb5483e-36e1-4688-b7f5-ea07361b26a8",
  TX_UUID: "beb5483f-36e1-4688-b7f5-ea07361b26a8",
};

// ===== AS6221 Temperature Custom Service (from your Zephyr FW) =====
export const TEMP_SERVICE_UUID = "12345678-1234-5678-1234-56789abcdef0";
export const TEMP_CHAR_UUID = "abcdef01-1234-5678-1234-56789abcdef0";
