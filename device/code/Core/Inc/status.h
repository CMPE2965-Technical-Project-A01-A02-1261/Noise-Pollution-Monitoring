#ifndef STATUS_H
#define STATUS_H

#include <stdbool.h>

/* Status LEDs, shown for STATUS_SHOW_MS after a status button press (and
 * once at boot):
 *   Battery    1-4 LEDs lit for 0-25 / 25-50 / 50-75 / 75-100 % charge,
 *              all off if the fuel gauge isn't found
 *   Bluetooth  blue on while the app is connected
 *   GNSS       red = no fix, yellow = 1-4 satellites, green = 5 or more
 *   Cellular   red = no signal, yellow = 1-2 bars, green = 3-5 bars */
#define STATUS_SHOW_MS 5000u

/* Start (or restart) the display. Reads the fuel gauge over I2C and asks
 * the XBee for the signal strength, so call it from the main loop, not an
 * interrupt. */
void Status_Show(void);

/* Call every main loop: updates the LEDs while shown, turns them off after. */
void Status_Process(void);

/* Bluetooth connection state, from the XBee's BLE connect/disconnect modem
 * status frames and from messages arriving from the app. */
void Status_SetBluetoothConnected(bool connected);
bool Status_IsBluetoothConnected(void);

#endif
