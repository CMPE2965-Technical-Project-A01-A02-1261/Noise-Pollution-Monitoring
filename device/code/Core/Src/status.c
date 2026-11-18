#include "status.h"

#include "battery.h"
#include "main.h"
#include "message.h"

/* GNSS satellites in the fix needed for green. */
#define STATUS_GNSS_GOOD_SATS 5u
/* Cellular bars (0-5, from the XBee DB reading) needed for green. */
#define STATUS_CELL_GOOD_BARS 3u

typedef enum { LEVEL_RED, LEVEL_YELLOW, LEVEL_GREEN } Level;

static const uint16_t battery_pins[4] = {
    LED_BAT_25_Pin, LED_BAT_50_Pin, LED_BAT_75_Pin, LED_BAT_100_Pin,
};

static volatile bool bluetooth_connected;
static bool showing;
static uint32_t show_start;
static uint8_t battery_leds; /* 0-4, read once per press */

void Status_SetBluetoothConnected(bool connected) {
	bluetooth_connected = connected;
}

bool Status_IsBluetoothConnected(void) {
	return bluetooth_connected;
}

/* Number of battery LEDs to light: always at least one while the gauge is
 * present, so 0-25 % shows one LED and 75-100 % shows all four. */
static uint8_t BatteryLeds(void) {
	if (!Battery_IsConnected()) {
		return 0;
	}
	uint16_t soc = Battery_GetSOC();
	if (soc <= 25) return 1;
	if (soc <= 50) return 2;
	if (soc <= 75) return 3;
	return 4;
}

static void SetPin(uint16_t pin, bool on) {
	HAL_GPIO_WritePin(GPIOC, pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* Red/green LED pair: yellow is both on. */
static void SetLevel(uint16_t red_pin, uint16_t green_pin, Level level) {
	SetPin(red_pin, level != LEVEL_GREEN);
	SetPin(green_pin, level != LEVEL_RED);
}

static Level GnssLevel(void) {
	uint8_t sats = GNSSFix ? GNSS : 0;
	if (sats == 0) return LEVEL_RED;
	if (sats < STATUS_GNSS_GOOD_SATS) return LEVEL_YELLOW;
	return LEVEL_GREEN;
}

static Level CellLevel(void) {
	uint8_t bars = Cell;
	if (bars == 0) return LEVEL_RED;
	if (bars < STATUS_CELL_GOOD_BARS) return LEVEL_YELLOW;
	return LEVEL_GREEN;
}

static void AllOff(void) {
	HAL_GPIO_WritePin(GPIOC, LED_BAT_25_Pin | LED_BAT_50_Pin | LED_BAT_75_Pin |
	                         LED_BAT_100_Pin | LED_BT_Pin | LED_GNSS_R_Pin |
	                         LED_GNSS_G_Pin | LED_CELL_R_Pin | LED_CELL_G_Pin,
	                  GPIO_PIN_RESET);
}

void Status_Show(void) {
	battery_leds = BatteryLeds();
	/* Refresh the signal strength. The AI/DB replies update Cell in the
	 * PA15 EXTI callback, and the LED follows while it is shown. GNSS takes
	 * the XBee off cellular, so don't ask then; the last value is shown. */
	if (!GNSSPending) {
		SendATCommand("AI");
	}
	show_start = HAL_GetTick();
	showing = true;
	Status_Process();
}

void Status_Process(void) {
	if (!showing) {
		return;
	}
	if (HAL_GetTick() - show_start >= STATUS_SHOW_MS) {
		showing = false;
		AllOff();
		return;
	}
	for (uint8_t i = 0; i < 4; i++) {
		SetPin(battery_pins[i], i < battery_leds);
	}
	SetPin(LED_BT_Pin, bluetooth_connected);
	SetLevel(LED_GNSS_R_Pin, LED_GNSS_G_Pin, GnssLevel());
	SetLevel(LED_CELL_R_Pin, LED_CELL_G_Pin, CellLevel());
}
