import React, { useEffect, useRef, useState } from "react";
import { View, Text, Button, FlatList, TouchableOpacity } from "react-native";
import { Device, Subscription } from "react-native-ble-plx";
import base64 from "react-native-base64";

import { useBLE } from "../functionality/BLEContext";
import { TEMP_SERVICE_UUID, TEMP_CHAR_UUID } from "../functionality/BLEProtocols";

import { addDoc, collection, serverTimestamp } from "firebase/firestore";
import { db } from "../firebase/firebaseConfig";
import { useAuth } from "../auth/AuthContext";

export default function DeviceScanner() {
  const { startScan, stopScan, connect, disconnect, scanning, connectedDevice } =
    useBLE();

  const { user } = useAuth(); // 🔐 logged-in user

  const [devices, setDevices] = useState<Device[]>([]);
  const seenIds = useRef(new Set<string>());

  const [tempText, setTempText] = useState<string>("--");
  const notifySubRef = useRef<Subscription | null>(null);

  useEffect(() => {
    return () => {
      stopScan();
      if (notifySubRef.current) {
        notifySubRef.current.remove();
        notifySubRef.current = null;
      }
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const onDeviceFound = (device: Device) => {
    if (!device.name) return;
    if (seenIds.current.has(device.id)) return;

    seenIds.current.add(device.id);
    setDevices((prev) => [...prev, device]);
  };

  const startTemperatureNotifications = async (device: Device) => {
    if (notifySubRef.current) {
      notifySubRef.current.remove();
      notifySubRef.current = null;
    }

    notifySubRef.current = device.monitorCharacteristicForService(
      TEMP_SERVICE_UUID,
      TEMP_CHAR_UUID,
      async (error, characteristic) => {
        if (error) {
          console.error("Temp notify error:", error);
          return;
        }
        if (!characteristic?.value) return;

        const decoded = base64.decode(characteristic.value);
        console.log("TEMP RX:", decoded);

        // Show on UI
        setTempText(decoded);

        // 🔥 Save to Firebase (per user)
        if (user) {
          try {
            await addDoc(
              collection(db, "users", user.uid, "temperature_readings"),
              {
                value: decoded,
                deviceId: device.id,
                timestamp: serverTimestamp(),
              }
            );
          } catch (err) {
            console.error("Firestore write failed:", err);
          }
        }
      }
    );
  };

  const handleConnect = async (deviceId: string) => {
    try {
      const dev = await connect(deviceId);
      setTempText("--");
      await startTemperatureNotifications(dev);
    } catch (e) {
      console.error("Connect failed:", e);
    }
  };

  const handleDisconnect = async () => {
    try {
      if (notifySubRef.current) {
        notifySubRef.current.remove();
        notifySubRef.current = null;
      }
      setTempText("--");
      await disconnect();
    } catch (e) {
      console.error("Disconnect failed:", e);
    }
  };

  return (
    <View style={{ padding: 16 }}>
      <Text style={{ fontSize: 20, fontWeight: "bold" }}>BLE Devices</Text>

      <View style={{ marginTop: 10, gap: 8 }}>
        <Button
          title={scanning ? "Scanning..." : "Start Scan"}
          onPress={() => startScan(onDeviceFound)}
          disabled={scanning}
        />
        <Button title="Stop Scan" onPress={stopScan} />
      </View>

      <View style={{ marginTop: 14 }}>
        {connectedDevice ? (
          <>
            <Text style={{ fontSize: 16 }}>
              ✅ Connected: {connectedDevice.name ?? connectedDevice.id}
            </Text>
            <Text style={{ marginTop: 8, fontSize: 18 }}>
              Temperature: {tempText}
            </Text>
            <View style={{ marginTop: 10 }}>
              <Button title="Disconnect" onPress={handleDisconnect} />
            </View>
          </>
        ) : (
          <Text style={{ fontSize: 16, marginTop: 10 }}>Not connected</Text>
        )}
      </View>

      <FlatList
        style={{ marginTop: 14 }}
        data={devices}
        keyExtractor={(item) => item.id}
        renderItem={({ item }) => (
          <TouchableOpacity
            style={{
              padding: 12,
              marginTop: 8,
              borderWidth: 1,
              borderRadius: 8,
              borderColor: "#ccc",
            }}
            onPress={() => handleConnect(item.id)}
          >
            <Text style={{ fontWeight: "600" }}>{item.name}</Text>
            <Text style={{ color: "gray" }}>ID: {item.id}</Text>
            <Text style={{ color: "gray" }}>RSSI: {item.rssi ?? "?"}</Text>
          </TouchableOpacity>
        )}
      />
    </View>
  );
}
