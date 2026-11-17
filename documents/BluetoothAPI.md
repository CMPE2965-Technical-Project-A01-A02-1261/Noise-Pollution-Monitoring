# documentation for how the device will communicate with the app over bluetooth

### Status message
"STATUS" sent from app to device
device responds with 5 - 7 packets (Battery, recording time, recording state, cellular,
and gnss, optional packet to request time and position from device)

"BATT ###" battery percentage message
"CELL #" number of cell signal bars
"GNSS ##" number of GPS sattelites providing lock
"TIME HH:MM:SS" duration device has been recording this session (0 if not recording) 
"REC #" whether the device is recording: 1 if recording, 0 if not (recording is
started and stopped with the button on the device)

"REQUEST TIME" device asks app for current time

"DT YYYY-MM-DDTHH:MM:SS" current date and time in UTC (not local time); the device uses it
to timestamp readings sent to the server until it has a GNSS fix

"REQUEST POS" device asks app for position

"LAT ##.#####" latitude and longitude sent to device, disregarded if device has GNSS position
"LNG ##.#####"

### Bluetooth signal strength
Not sent by the device, the XBee has no way to read its own BLE signal strength.
The app reads the RSSI of the connection itself and displays it as
GREAT (>= -60 dBm), GOOD (>= -80 dBm) or POOR (< -80 dBm).
