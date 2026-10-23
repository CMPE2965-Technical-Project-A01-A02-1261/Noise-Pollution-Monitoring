
import { Alert } from "react-native";

export const GetStatus = async (): Promise<void> => {
    try {
        const { sendStatusToXBee } = await import("./BLEService");
        await sendStatusToXBee();
    } catch (error) {
        console.error("Unable to send STATUS over BLE:", error);
        Alert.alert(
            "Bluetooth request failed",
            error instanceof Error
                ? error.message
                : "Check the Bluetooth connection and try again."
        );
    }
};