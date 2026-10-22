
import { Alert } from "react-native";
import { sendStatusToXBee } from "./BLEService";

export const GetStatus = async (password: string): Promise<void> => {
    if (!password.trim()) {
        Alert.alert("Bluetooth password required", "Enter the XBee 3 BLE password to connect.");
        return;
    }

    try {
        await sendStatusToXBee(password);
    } catch (error) {
        console.error("Unable to send STATUS over BLE:", error);
        Alert.alert(
            "Bluetooth request failed",
            error instanceof Error
                ? error.message
                : "Check Bluetooth permissions, the XBee BLE password, and the device connection."
        );
    }
};