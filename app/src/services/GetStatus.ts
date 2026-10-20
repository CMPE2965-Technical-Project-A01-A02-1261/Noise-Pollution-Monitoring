
import { Alert } from "react-native";
import { adoptSystemXBeeConnection, sendDataToXBee } from "./BLEService";

export const GetStatus = async (): Promise<void> => {
    console.log("GetStatus function called");

    try {
        const device = await adoptSystemXBeeConnection();
        if (!device) {
            Alert.alert(
                "XBee 3 not connected",
                "Connect the XBee 3 Global to the phone, then try again."
            );
            return;
        }

        await sendDataToXBee(device, "STATUS");
    } catch (error) {
        console.error("Unable to send STATUS over BLE:", error);
        Alert.alert(
            "Bluetooth request failed",
            "If you are running Expo Go, install an app development build to use BLE."
        );
    }
}