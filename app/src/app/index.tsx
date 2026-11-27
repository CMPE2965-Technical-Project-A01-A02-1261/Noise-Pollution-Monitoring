import { useEffect, useRef, useState } from "react";
import { Alert, Pressable, ScrollView, StyleSheet, Text, TextInput, View } from "react-native";
import Status, { type StatusTone } from "../components/status";
import { GetStatus } from "../services/GetStatus";
import type { XBeeApiFrame, XBeeBluetoothDevice } from "../services/BLEService";
import {
  isPositionRequest,
  isTimeRequest,
  parseStatusMessage,
  type DeviceStatus,
} from "../services/statusMessages";

const SIGNAL_POLL_INTERVAL_MS = 5000;
const CELL_BARS_MAX = 5;

/** GREAT/GOOD/POOR scale from the Bluetooth API doc. */
function bluetoothQuality(rssi: number): { label: string; bars: number; tone: StatusTone } {
  if (rssi >= -60) return { label: "Great", bars: 3, tone: "good" };
  if (rssi >= -80) return { label: "Good", bars: 2, tone: "warning" };
  return { label: "Poor", bars: 1, tone: "bad" };
}

function batteryTone(percent: number): StatusTone {
  if (percent < 20) return "bad";
  if (percent < 50) return "warning";
  return "good";
}

function cellTone(bars: number): StatusTone {
  if (bars === 0) return "bad";
  if (bars <= 2) return "warning";
  return "good";
}

function gnssTone(satellites: number): StatusTone {
  if (satellites === 0) return "bad";
  if (satellites < 4) return "warning";
  return "good";
}

/**
 * The Recording card. REC says whether the device is recording; TIME is the
 * session length. Firmware without REC is judged from TIME alone.
 */
function recordingCard({ recording, recordingTime }: DeviceStatus): {
  value?: string;
  detail?: string;
  tone?: StatusTone;
} {
  const isRecording = recording ?? (recordingTime === undefined ? undefined : recordingTime !== null);
  if (isRecording === undefined) {
    return {};
  }
  if (!isRecording) {
    return { value: "Stopped", detail: "Press the device button to record" };
  }
  return {
    value: "Recording",
    detail: recordingTime ? `${recordingTime} elapsed` : undefined,
    tone: "good",
  };
}

function formatHex(value: number): string {
  return `0x${value.toString(16).padStart(2, "0").toUpperCase()}`;
}

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
  const [signalStrength, setSignalStrength] = useState<number | null>(null);
  const [deviceStatus, setDeviceStatus] = useState<DeviceStatus>({});
  const [statusUpdatedAt, setStatusUpdatedAt] = useState<Date | null>(null);
  const [showRawFrames, setShowRawFrames] = useState(false);
  /** True once the phone's position has been sent to the device on this connection. */
  const [positionSent, setPositionSent] = useState(false);
  const unsubscribeFromFrames = useRef<(() => void) | undefined>(undefined);
  const isSendingPosition = useRef(false);
  const hasShownLocationError = useRef(false);

  useEffect(() => () => unsubscribeFromFrames.current?.(), []);

  const refreshSignalStrength = async () => {
    const { readXBeeSignalStrength } = await import("../services/BLEService");
    setSignalStrength(await readXBeeSignalStrength());
  };

  // Keep the Bluetooth signal strength current while a device is connected.
  useEffect(() => {
    if (!connectedDevice) {
      return;
    }

    let cancelled = false;
    const poll = async () => {
      const { readXBeeSignalStrength } = await import("../services/BLEService");
      const rssi = await readXBeeSignalStrength();
      if (!cancelled) {
        setSignalStrength(rssi);
      }
    };

    void poll();
    const interval = setInterval(() => void poll(), SIGNAL_POLL_INTERVAL_MS);
    return () => {
      cancelled = true;
      clearInterval(interval);
      setSignalStrength(null);
    };
  }, [connectedDevice]);

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
    setDeviceStatus({});
    setPositionSent(false);
    setStatusUpdatedAt(null);
    try {
      const { connectToXBee, sendPositionToXBee, sendTimeToXBee, subscribeToXBeeFrames } =
        await import("../services/BLEService");
      hasShownLocationError.current = false;

      // Reply to "REQUEST POS" with the phone's position. Only one reply at a
      // time, since the device asks again on every status refresh.
      const sendPhonePosition = async () => {
        if (isSendingPosition.current) {
          return;
        }
        isSendingPosition.current = true;
        try {
          const { getPhoneLocation } = await import("../services/PhoneLocation");
          const { latitude, longitude } = await getPhoneLocation();
          await sendPositionToXBee(latitude, longitude);
          setPositionSent(true);
        } catch (error) {
          console.error("Could not send the position to the XBee:", error);
          if (!hasShownLocationError.current) {
            hasShownLocationError.current = true;
            Alert.alert(
              "Couldn't send location",
              error instanceof Error ? error.message : "Unable to read the phone's location."
            );
          }
        } finally {
          isSendingPosition.current = false;
        }
      };

      unsubscribeFromFrames.current?.();
      unsubscribeFromFrames.current = subscribeToXBeeFrames((frame) => {
        setReceivedFrames((frames) => [frame, ...frames].slice(0, 20));
        if (frame.data !== undefined && isTimeRequest(frame.data)) {
          void sendTimeToXBee().catch((error: unknown) =>
            console.error("Could not send the time to the XBee:", error)
          );
        }
        if (frame.data !== undefined && isPositionRequest(frame.data)) {
          void sendPhonePosition();
        }
        const update = frame.data !== undefined ? parseStatusMessage(frame.data) : null;
        if (update) {
          setDeviceStatus((status) => ({ ...status, ...update }));
          setStatusUpdatedAt(new Date());
        }
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
      await Promise.all([GetStatus(), refreshSignalStrength()]);
    } finally {
      setIsSending(false);
    }
  };

  return (
    <ScrollView contentContainerStyle={styles.container} keyboardShouldPersistTaps="handled">
      <Text style={styles.heading}>Bluetooth connection</Text>
      {connectedDevice ? (
        <>
          <Text style={styles.connectionStatus}>Connected to {connectedDevice.name}</Text>

          <View style={styles.statusHeader}>
            <Text style={styles.sectionHeading}>Device status</Text>
            {statusUpdatedAt && (
              <Text style={styles.updatedText}>
                Updated {statusUpdatedAt.toLocaleTimeString()}
              </Text>
            )}
          </View>
          <View style={styles.statusGrid}>
            <Status
              label="Battery"
              value={deviceStatus.battery !== undefined ? `${deviceStatus.battery}%` : undefined}
              detail={
                deviceStatus.battery !== undefined && deviceStatus.battery < 20
                  ? "Low battery"
                  : undefined
              }
              tone={deviceStatus.battery !== undefined ? batteryTone(deviceStatus.battery) : undefined}
              meter={
                deviceStatus.battery !== undefined
                  ? { kind: "fill", fraction: deviceStatus.battery / 100 }
                  : undefined
              }
            />
            <Status
              label="Cellular"
              value={
                deviceStatus.cellBars !== undefined
                  ? `${deviceStatus.cellBars} ${deviceStatus.cellBars === 1 ? "bar" : "bars"}`
                  : undefined
              }
              detail={deviceStatus.cellBars === 0 ? "No signal" : undefined}
              tone={deviceStatus.cellBars !== undefined ? cellTone(deviceStatus.cellBars) : undefined}
              meter={
                deviceStatus.cellBars !== undefined
                  ? { kind: "bars", filled: deviceStatus.cellBars, total: CELL_BARS_MAX }
                  : undefined
              }
            />
            <Status
              label="GPS"
              value={
                deviceStatus.satellites !== undefined
                  ? `${deviceStatus.satellites} ${deviceStatus.satellites === 1 ? "satellite" : "satellites"}`
                  : undefined
              }
              detail={
                deviceStatus.satellites === undefined
                  ? undefined
                  : deviceStatus.satellites === 0
                    ? positionSent
                      ? "Position sent"
                      : "No fix"
                    : deviceStatus.satellites < 4
                      ? "Weak fix"
                      : "Locked"
              }
              tone={
                deviceStatus.satellites === undefined
                  ? undefined
                  : deviceStatus.satellites === 0 && positionSent
                    ? "good"
                    : gnssTone(deviceStatus.satellites)
              }
            />
            <Status label="Recording" {...recordingCard(deviceStatus)} />
            <Status
              label="Bluetooth"
              value={signalStrength !== null ? bluetoothQuality(signalStrength).label : undefined}
              detail={signalStrength !== null ? `${signalStrength} dBm` : undefined}
              tone={signalStrength !== null ? bluetoothQuality(signalStrength).tone : undefined}
              meter={
                signalStrength !== null
                  ? { kind: "bars", filled: bluetoothQuality(signalStrength).bars, total: 3 }
                  : undefined
              }
            />
          </View>

          <Pressable
            accessibilityRole="button"
            disabled={isSending || isScanning || isConnecting}
            onPress={requestStatus}
            style={[styles.button, styles.statusButton]}
          >
            <Text style={styles.buttonText}>
              {isSending ? "Requesting status..." : "Refresh status"}
            </Text>
          </Pressable>
        </>
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
        <View style={styles.framesSection}>
          <Pressable
            accessibilityRole="button"
            accessibilityState={{ expanded: showRawFrames }}
            onPress={() => setShowRawFrames((shown) => !shown)}
          >
            <Text style={styles.rawToggle}>
              {showRawFrames ? "Hide" : "Show"} raw messages ({receivedFrames.length})
            </Text>
          </Pressable>
          {showRawFrames &&
            (receivedFrames.length === 0 ? (
              <Text style={styles.helperText}>No messages received yet.</Text>
            ) : (
              receivedFrames.map((frame, index) => (
                <View key={`${frame.type}-${frame.id ?? "none"}-${index}`} style={styles.frameCard}>
                  <Text style={styles.frameTitle}>
                    {frame.typeName} ({formatHex(frame.type)})
                  </Text>
                  {frame.id !== undefined && <Text style={styles.frameText}>Frame ID: {frame.id}</Text>}
                  {frame.status !== undefined && (
                    <Text style={styles.frameText}>
                      Status: {frame.status} ({formatHex(frame.status)})
                    </Text>
                  )}
                  {frame.source !== undefined && <Text style={styles.frameText}>Source: {frame.source}</Text>}
                  {frame.data !== undefined && (
                    <Text selectable style={styles.frameText}>Data: {frame.data}</Text>
                  )}
                </View>
              ))
            ))}
        </View>
      )}
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
  statusHeader: {
    alignItems: "baseline",
    flexDirection: "row",
    justifyContent: "space-between",
  },
  updatedText: {
    color: "#888",
    fontSize: 12,
  },
  statusGrid: {
    flexDirection: "row",
    flexWrap: "wrap",
    gap: 10,
  },
  rawToggle: {
    color: "#208AEF",
    fontSize: 15,
    fontWeight: "600",
    marginBottom: 8,
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
    marginBottom: 24,
    marginTop: 16,
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
