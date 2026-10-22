# Noise Sensor Monitoring app

The Expo mobile companion app connects to an XBee 3 Global over its BLE API service. Enter the BLE password configured on the XBee, then use **Get Status** to send an encrypted `USER_DATA_RELAY_INPUT` API frame to the MicroPython interface.

The app scans for the XBee service, performs the BLE API `apiservice` SRP unlock, and encrypts API frames with the negotiated AES-CTR session key. Bluetooth requires a development build; Expo Go does not include `react-native-ble-plx`.

For iOS builds in Xcode, open `ios/NoiseSensorMonitoring.xcworkspace` rather than the `.xcodeproj` so CocoaPods dependencies are included.
