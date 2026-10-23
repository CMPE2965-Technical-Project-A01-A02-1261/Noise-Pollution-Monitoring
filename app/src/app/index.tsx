import { useEffect, useRef, useState } from "react";
import { Alert, Pressable, ScrollView, StyleSheet, Text, TextInput, View } from "react-native";
import { GetStatus } from "../services/GetStatus";
import type { XBeeApiFrame, XBeeBluetoothDevice } from "../services/BLEService";

export default function Index() {
  const [devices, setDevices] = useState<XBeeBluetoothDevice[]>([]);
  const [selectedDevice, setSelectedDevice] = useState<XBeeBluetoothDevice | null>(null);
  const [connectedDevice, setConnectedDevice] = useState<XBeeBluetoothDevice | null>(null);
  const [password, setPassword] = useState("");
  const [isScanning, setIsScanning] = useState(false);
  const [hasScanned, setHasScanned] = useState(false);
  const [isConnecting, setIsConnecting] = useState(false);
  const [isSending, setIsSending] = useState(false);
  const [receivedFrames, setReceivedFrames] = useState<XBeeApiFrame[]>([]);
  const unsubscribeFromFrames = useRef<(() => void) | undefined>(undefined);

  useEffect(() => () => unsubscribeFromFrames.current?.(), []);

  const scanForDevices = async () => {
    setIsScanning(true);
    setDevices([]);
    setSelectedDevice(null);
    setHasScanned(false);
    try {
      const { scanForXBeeDevices } = await import("../services/BLEService");
      const foundDevices = await scanForXBeeDevices();
      setDevices(foundDevices);
      setHasScanned(true);
    } catch (error) {
      Alert.alert(
        "Bluetooth scan failed",
        error instanceof Error ? error.message : "Unable to scan for Bluetooth devices."
      );
    } finally {
      setIsScanning(false);
    }
  };

  const connectToSelectedDevice = async () => {
    if (!selectedDevice) {
      return;
    }
    if (!password.trim()) {
      Alert.alert("Password required", "Enter the XBee BLE password to connect.");
      return;
    }

    setIsConnecting(true);
    setConnectedDevice(null);
    setReceivedFrames([]);
    try {
      const { connectToXBee, subscribeToXBeeFrames } = await import("../services/BLEService");
      unsubscribeFromFrames.current?.();
      unsubscribeFromFrames.current = subscribeToXBeeFrames((frame) => {
        setReceivedFrames((frames) => [frame, ...frames].slice(0, 20));
      });
      await connectToXBee(selectedDevice.id, password);
      setConnectedDevice(selectedDevice);
      setSelectedDevice(null);
      setPassword("");
    } catch (error) {
      unsubscribeFromFrames.current?.();
      unsubscribeFromFrames.current = undefined;
      Alert.alert(
        "Connection failed",
        error instanceof Error ? error.message : "Unable to connect to the XBee device."
      );
    } finally {
      setIsConnecting(false);
    }
  };

  const requestStatus = async () => {
    setIsSending(true);
    try {
      await GetStatus();
    } finally {
      setIsSending(false);
    }
  };

  return (
    <ScrollView contentContainerStyle={styles.container} keyboardShouldPersistTaps="handled">
      <Text style={styles.heading}>Bluetooth connection</Text>
      {connectedDevice ? (
        <Text style={styles.connectionStatus}>Connected to {connectedDevice.name}</Text>
      ) : (
        <Text style={styles.connectionStatus}>No device connected</Text>
      )}

      <Pressable
        accessibilityRole="button"
        disabled={isScanning || isConnecting || isSending}
        onPress={scanForDevices}
        style={styles.button}
      >
        <Text style={styles.buttonText}>{isScanning ? "Scanning..." : "Scan for devices"}</Text>
      </Pressable>

      {devices.length > 0 ? (
        <View style={styles.deviceList}>
          <Text style={styles.sectionHeading}>Available XBee devices</Text>
          {devices.map((device) => (
            <Pressable
              accessibilityRole="button"
              accessibilityState={{ selected: selectedDevice?.id === device.id }}
              key={device.id}
              onPress={() => {
                setSelectedDevice(device);
                setPassword("");
              }}
              style={[
                styles.deviceButton,
                selectedDevice?.id === device.id && styles.selectedDeviceButton,
              ]}
            >
              <Text style={styles.deviceName}>{device.name}</Text>
              <Text style={styles.deviceId}>{device.id}</Text>
            </Pressable>
          ))}
        </View>
      ) : (
        !isScanning && (
          <Text style={styles.helperText}>
            {hasScanned
              ? "No XBee devices found. Make sure the device is powered on and nearby, then scan again."
              : "Scan to find nearby XBee devices."}
          </Text>
        )
      )}

      {selectedDevice && (
        <View style={styles.passwordSection}>
          <Text style={styles.sectionHeading}>Connect to {selectedDevice.name}</Text>
          <TextInput
            accessibilityLabel="XBee BLE password"
            autoCapitalize="none"
            autoCorrect={false}
            onChangeText={setPassword}
            onSubmitEditing={connectToSelectedDevice}
            placeholder="XBee BLE password"
            returnKeyType="done"
            secureTextEntry
            style={styles.passwordInput}
            value={password}
          />
          <Pressable
            accessibilityRole="button"
            disabled={isConnecting || isScanning || isSending}
            onPress={connectToSelectedDevice}
            style={styles.button}
          >
            <Text style={styles.buttonText}>{isConnecting ? "Connecting..." : "Connect"}</Text>
          </Pressable>
        </View>
      )}

      {connectedDevice && (
        <Pressable
          accessibilityRole="button"
          disabled={isSending || isScanning || isConnecting}
          onPress={requestStatus}
          style={[styles.button, styles.statusButton]}
        >
          <Text style={styles.buttonText}>{isSending ? "Requesting status..." : "Get Status"}</Text>
        </Pressable>
      )}

      <View style={styles.framesSection}>
        <Text style={styles.sectionHeading}>Received API frames</Text>
        {receivedFrames.length === 0 ? (
          <Text style={styles.helperText}>No received API frames yet. Request status to see XBee responses.</Text>
        ) : (
          receivedFrames.map((frame, index) => (
            <View
              key={`${frame.type}-${frame.id ?? "none"}-${index}`}
              style={styles.frameCard}
            >
              <Text style={styles.frameTitle}>
                {frame.typeName} (0x{frame.type.toString(16).padStart(2, "0").toUpperCase()})
              </Text>
              {frame.id !== undefined && (
                <Text style={styles.frameText}>Frame ID: {frame.id}</Text>
              )}
              {frame.status !== undefined && (
                <Text style={styles.frameText}>
                  Status: {frame.status} (0x{frame.status.toString(16).padStart(2, "0").toUpperCase()})
                </Text>
              )}
              {frame.source !== undefined && (
                <Text style={styles.frameText}>Source: {frame.source}</Text>
              )}
              {frame.data !== undefined && (
                <Text selectable style={styles.frameText}>Data: {frame.data}</Text>
              )}
            </View>
          ))
        )}
      </View>
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  container: {
    alignItems: "stretch",
    padding: 24,
    paddingTop: 48,
    flexGrow: 1,
  },
  heading: {
    color: "#222",
    fontSize: 24,
    fontWeight: "700",
    marginBottom: 12,
  },
  connectionStatus: {
    color: "#444",
    fontSize: 16,
    marginBottom: 20,
  },
  sectionHeading: {
    color: "#222",
    fontSize: 17,
    fontWeight: "600",
    marginBottom: 10,
  },
  helperText: {
    color: "#666",
    marginTop: 12,
  },
  deviceList: {
    marginTop: 24,
  },
  deviceButton: {
    borderColor: "#aaa",
    borderRadius: 8,
    borderWidth: 1,
    marginBottom: 8,
    padding: 12,
  },
  selectedDeviceButton: {
    borderColor: "#208AEF",
    backgroundColor: "#eef7ff",
  },
  deviceName: {
    color: "#222",
    fontSize: 16,
    fontWeight: "600",
  },
  deviceId: {
    color: "#666",
    fontSize: 12,
    marginTop: 4,
  },
  passwordSection: {
    marginTop: 24,
  },
  passwordInput: {
    alignSelf: "stretch",
    borderColor: "#777",
    borderRadius: 8,
    borderWidth: 1,
    marginBottom: 16,
    padding: 12,
  },
  button: {
    alignItems: "center",
    backgroundColor: "#208AEF",
    borderRadius: 8,
    paddingHorizontal: 20,
    paddingVertical: 12,
  },
  statusButton: {
    marginTop: 24,
  },
  framesSection: {
    marginTop: 32,
  },
  frameCard: {
    borderColor: "#ddd",
    borderRadius: 8,
    borderWidth: 1,
    marginBottom: 8,
    padding: 12,
  },
  frameTitle: {
    color: "#222",
    fontSize: 15,
    fontWeight: "600",
    marginBottom: 4,
  },
  frameText: {
    color: "#444",
    fontSize: 14,
    marginTop: 2,
  },
  buttonText: {
    color: "#fff",
    fontWeight: "600",
  },
});
