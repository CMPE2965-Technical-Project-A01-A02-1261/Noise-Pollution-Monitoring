# documentation for how the device will communicate with the app over bluetooth

### Status message
"STATUS" sent from app to device
device responds with 5 - 7 packets (Battery, recording time, cellular, gnss, and bluetooth,
optional packet to request time and position from device)

"BATT ###" battery percentage message
"CELL #" number of cell signal bars
"GNSS ##" number of GPS sattelites providing lock
"BLUE {GREAT/GOOD/POOR}" bluetooth signal quality
"TIME HH:MM:SS" duration device has been recording this session (0 if not recording) 

"REQUEST TIME" device asks app for current time

"DT YYYY-MM-DDTHH:MM:SS"

"REQUEST POS" device asks app for position

"LAT ##.#####" latitude and longitude sent to device, disregarded if device has GNSS position
"LNG ##.#####"
