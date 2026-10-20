import { BleManager, Device, Subscription } from 'react-native-ble-plx';
import { Buffer } from 'buffer';

let manager: BleManager | undefined;

function getManager(): BleManager {
    if (!manager) {
        manager = new BleManager();
    }
    return manager;
}

// Explicit XBee 3 Global GATT Registry Consts
const XBEE_SERVICE_UUID = '53da53b9-0447-425a-b9ea-9837505eb59a';
const XBEE_REQ_CHAR_UUID = '7dddca00-3e05-4651-9254-44074792c590';
const XBEE_RES_CHAR_UUID = 'f9279ee9-2cd0-410c-81cc-adf11e4e5aea';

export async function adoptSystemXBeeConnection() {
    try {
        // 1. Fetch devices currently connected to the OS with the XBee service
        console.log("Looking for connected XBee modules...");
        const systemConnectedDevices = await getManager().connectedDevices([XBEE_SERVICE_UUID]);

        if (systemConnectedDevices.length === 0) {
            console.warn("No connected XBee 3 found. Make sure it's connected to the phone first.");
            return null;
        }

        // Select the first matched XBee 3 unit
        const xbeeDevice: Device = systemConnectedDevices[0];
        console.log(`Found device: ${xbeeDevice.name || 'XBee 3'} [${xbeeDevice.id}]`);

        // 2. Adopt the native device link to your application sandbox environment
        const connectedDevice = await xbeeDevice.connect();

        // 3. Critically map the internal GATT tree profiles
        await connectedDevice.discoverAllServicesAndCharacteristics();
        console.log("XBee 3 completely bridged and ready.");

        // 4. Setup a real-time message listener stream for incoming API frames
        setupXBeeListener(connectedDevice);

        return connectedDevice;
    } catch (error) {
        console.error("Failed to adopt XBee system connection:", error);
        throw error;
    }
}

function setupXBeeListener(device: Device) {
    // Listen for 'Indicate' notifications coming from the XBee firmware processing layer
    const subscription: Subscription = device.monitorCharacteristicForService(
        XBEE_SERVICE_UUID,
        XBEE_RES_CHAR_UUID,
        (error, characteristic) => {
            if (error) {
                console.error("XBee streaming error:", error);
                return;
            }
            if (characteristic?.value) {
                // XBee returns payloads wrapped natively as Base64 strings
                const rawBytes = Buffer.from(characteristic.value, 'base64');
                console.log("Received raw frame from XBee:", rawBytes);

                // Convert to text string if using transparent layout mode instead of complete API frames
                const textData = rawBytes.toString('utf-8');
                console.log("Decoded text:", textData);
            }
        }
    );

    return subscription;
}

// Function helper to dispatch data packets/AT commands directly to the XBee 3 unit
export async function sendDataToXBee(device: Device, payloadString: string) {
    try {
        const base64Data = Buffer.from(payloadString).toString('base64');

        await device.writeCharacteristicWithResponseForService(
            XBEE_SERVICE_UUID,
            XBEE_REQ_CHAR_UUID,
            base64Data
        );
        console.log("Packet sent successfully to XBee!");
    } catch (err) {
        console.error("Failed to write to XBee:", err);
        throw err;
    }
}