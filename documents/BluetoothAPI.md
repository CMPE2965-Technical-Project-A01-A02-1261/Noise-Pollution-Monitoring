# documentation for how the device will communicate with the app over bluetooth

### Status message
"STATUS" sent from app to device
device responds with 4 - 6 packets (Battery, recording time, cellular, and gnss,
optional packet to request time and position from device)

"BATT ###" battery percentage message
"CELL #" number of cell signal bars
"GNSS ##" number of GPS sattelites providing lock
"TIME HH:MM:SS" duration device has been recording this session (0 if not recording) 

"REQUEST TIME" device asks app for current time

"DT YYYY-MM-DDTHH:MM:SS"

"REQUEST POS" device asks app for position

"LAT ##.#####" latitude and longitude sent to device, disregarded if device has GNSS position
"LNG ##.#####"

### Bluetooth signal strength
Not sent by the device, the XBee has no way to read its own BLE signal strength.
The app reads the RSSI of the connection itself and displays it as
GREAT (>= -60 dBm), GOOD (>= -80 dBm) or POOR (< -80 dBm).
