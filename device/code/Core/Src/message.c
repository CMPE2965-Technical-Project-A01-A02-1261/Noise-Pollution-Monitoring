#include "message.h"
#include "battery.h"
#include "clock.h"
#include "sound.h"
#include "status.h"
#include "uplink.h"

#include <math.h>
#include <stdlib.h>

bool GetBatt = false;
uint8_t Batt = 0;
volatile bool GetCell = false;
volatile uint8_t Cell = 0;
bool GetGNSS = false;
uint8_t GNSS = 0;

/* GNSS fix state. GNSS holds the number of satellites in the fix; GNSSFix is
 * set by whatever reads the XBee's GNSS receiver once it has a position. */
bool GNSSFix = false;
/* True while a GNSS one shot request is outstanding on the XBee. */
volatile bool GNSSPending = false;
int32_t GNSSAltitude_mm = 0;
uint32_t GNSSLockTime = 0; /* seconds since 2000-01-01 00:00:00 */

/* Time from the cellular network (XBee "DT"). NetworkTimeTick is the HAL
 * tick of the last valid answer; the main loop uses it to decide when to ask
 * again. NetworkTimeRaw keeps the last value received, for debugging. */
volatile bool NetworkTimeValid = false;
volatile uint32_t NetworkTimeTick = 0;
volatile uint32_t NetworkTimeRaw = 0;

/* When the current recording session started (set by the B1 handler), for
 * the TIME status packet. Whether it is recording is Sound_IsRunning(). */
uint32_t RecordStartTick = 0;

/* Wall clock time (UTC) received from the app ("DT YYYY-MM-DDTHH:MM:SS").
 * DateTimeTick is the HAL tick at the moment it was received, so the current
 * time is DateTime + (HAL_GetTick() - DateTimeTick). It also sets the Clock
 * used to timestamp readings. */
bool TimeSet = false;
DateTime_t DateTime = {0};
uint32_t DateTimeTick = 0;

/* Position received from the app ("LAT ##.#####" / "LNG ##.#####"). Ignored
 * when the device has its own GNSS fix. */
bool LatSet = false;
bool LngSet = false;
int32_t LatitudeE7 = 0;
int32_t LongitudeE7 = 0;

static bool fallback_set = false;
static int32_t fallback_lat_e7;
static int32_t fallback_lon_e7;

void Position_SetFallback(int32_t lat_e7, int32_t lon_e7){
	fallback_lat_e7 = lat_e7;
	fallback_lon_e7 = lon_e7;
	fallback_set = true;
}

bool Position_Get(int32_t *lat_e7, int32_t *lon_e7){
	if (LatSet && LngSet){
		*lat_e7 = LatitudeE7;
		*lon_e7 = LongitudeE7;
		return true;
	}
	if (fallback_set){
		*lat_e7 = fallback_lat_e7;
		*lon_e7 = fallback_lon_e7;
		return true;
	}
	return false;
}

static bool ParseDateTime(const char *str, DateTime_t *dt);
static void HandleGNSS(const char *data, uint16_t length, uint32_t tick);
static void HandleNetworkTime(const char *data, uint16_t length, uint32_t tick);

void HandleAPI(const API_Status *status){
    if (status->frame_type == 0x89 && status->data_length > 0) {
      printf("API TX Status: frame ID=0x%02X status=0x%02X\r\n",
             status->frame_id, (uint8_t)status->data[0]);
    } else if (status->frame_type == 0x8A &&
               status->data_length > 0) {
      printf("API Modem Status: status=0x%02X\r\n",
             (uint8_t)status->data[0]);
      //0x32 = BLE connect, 0x33 = BLE disconnect, for the Bluetooth LED
      if ((uint8_t)status->data[0] == 0x32) {
        Status_SetBluetoothConnected(true);
      } else if ((uint8_t)status->data[0] == 0x33) {
        Status_SetBluetoothConnected(false);
      }
    }
    else if (status->frame_type == 0xAD) {
      ReadBluetooth((const uint8_t *)status->data, status->data_length, status->tick);
    }
    //AT command response: "DT" is the time from the cellular network
    else if (status->frame_type == 0x88 && status->data_length >= 3 &&
             status->data[0] == 'D' && status->data[1] == 'T') {
      HandleNetworkTime(status->data, status->data_length, status->tick);
    }
    //GNSS request response: type, status (0 = success)
    else if (status->frame_type == 0xBD && status->data_length >= 2) {
      if (status->data[0] == GNSS_ONE_SHOT_START && status->data[1] != 0) {
        GNSSPending = false; //request rejected, no 0xBF will follow
      }
    }
    //GNSS one shot result
    else if (status->frame_type == 0xBF) {
      HandleGNSS(status->data, status->data_length, status->tick);
    }
    //socket frames for the server uplink, plus TX status for socket sends
    if (status->frame_type == 0x89 || (status->frame_type >= 0xC0 &&
                                       status->frame_type <= 0xCF)) {
      Uplink_HandleFrame(status);
    }
}

static int32_t ReadBE32(const char *p){
	return (int32_t)(((uint32_t)(uint8_t)p[0] << 24) |
	                 ((uint32_t)(uint8_t)p[1] << 16) |
	                 ((uint32_t)(uint8_t)p[2] << 8) |
	                 (uint32_t)(uint8_t)p[3]);
}

/* Earliest and latest network time accepted, in seconds since 2000. Before
 * the XBee gets the time from the network it can report 0 or a time just
 * after its own epoch. */
#define NETWORK_TIME_MIN_S 788918400u   /* 2025-01-01 */
#define NETWORK_TIME_MAX_S 3155760000u  /* 2100-01-01 */

/* The XBee's "DT" is the time the cell tower reports, which is local time,
 * not UTC (there is no AT command for UTC). Local time is UTC plus this
 * offset. Saskatchewan is UTC-6 all year (no daylight saving); change this
 * if the device is used in another time zone. */
#define NETWORK_TIME_UTC_OFFSET_S (-6 * 3600)

/* AT "DT" response: name(2) status(1) value, the value being local seconds
 * since 2000-01-01 00:00:00, big endian. */
static void HandleNetworkTime(const char *data, uint16_t length, uint32_t tick){
	if ((uint8_t)data[2] != 0u){
		return; //error, e.g. no time from the network yet
	}
	uint16_t n = (uint16_t)(length - 3u);
	if (n == 0u || n > 4u){
		return;
	}
	uint32_t secs = 0;
	for (uint16_t i = 0; i < n; i++){
		secs = (secs << 8) | (uint8_t)data[3u + i];
	}
	NetworkTimeRaw = secs;
	if (secs < NETWORK_TIME_MIN_S || secs > NETWORK_TIME_MAX_S){
		return;
	}
	Clock_Set((int64_t)secs + CLOCK_UNIX_2000 - NETWORK_TIME_UTC_OFFSET_S, tick);
	NetworkTimeTick = tick;
	NetworkTimeValid = true;
}

/* GNSS One Shot Response (0xBF) payload:
 * status(1) lock time(4) latitude(4) longitude(4) altitude(4) satellites(1)
 * all big endian, lat/lng in degrees * 10^7, altitude in mm. */
static void HandleGNSS(const char *data, uint16_t length, uint32_t tick){
	GNSSPending = false;
	if (length < 18){
		return;
	}

	uint8_t result = (uint8_t)data[0];
	if (result == 0x00){ //valid
		GNSSLockTime = (uint32_t)ReadBE32(data + 1);
		LatitudeE7 = ReadBE32(data + 5);
		LongitudeE7 = ReadBE32(data + 9);
		GNSSAltitude_mm = ReadBE32(data + 13);
		GNSS = (uint8_t)data[17];
		LatSet = true;
		LngSet = true;
		GNSSFix = true;
		//the fix is reported as soon as it is obtained, so the lock time
		//is the time this frame arrived
		Clock_Set((int64_t)GNSSLockTime + CLOCK_UNIX_2000, tick);
	}
	else if (result == 0x01 || result == 0x02){ //invalid or timed out
		GNSSFix = false;
		GNSS = 0;
	}
	//0x03 canceled: keep the previous fix
}

/* Starts a GNSS one shot location request (frame 0x3D). The XBee switches
 * from cellular to GNSS priority until it gets a fix or timeout_s expires,
 * then reports the result in a 0xBF frame. Returns false if not sent. */
bool RequestGNSS(uint16_t timeout_s){
	if (GNSSPending){
		return false;
	}
	uint8_t data[5];
	data[0] = 0x3D;
	data[1] = API_GetNextFrameId();
	data[2] = GNSS_ONE_SHOT_START;
	data[3] = (uint8_t)(timeout_s >> 8);
	data[4] = (uint8_t)(timeout_s & 0xFF);
	if (!Frame(data, sizeof(data))){
		return false;
	}
	GNSSPending = true;
	return true;
}


void ReadBluetooth(const uint8_t *data, uint16_t length, uint32_t tick){
	if (data[0] != 1){
		return; //not from bluetooth on xbee
	}
	//a message from the app means it is connected, even if the XBee's
	//BLE connect status was missed
	Status_SetBluetoothConnected(true);
	//extract msg from rest of data array into a null terminated buffer
	char msg[64];
	length--;
	if (length >= sizeof(msg)){
		return; //too long to be a valid message
	}
	memcpy(msg, data + 1, length);
	msg[length] = '\0';
	//strip trailing line endings in case the app sends them
	while (length > 0 && (msg[length - 1] == '\r' || msg[length - 1] == '\n')){
		msg[--length] = '\0';
	}

	//status message received
	if (!strcmp(msg, "STATUS")){
		StatusResponse();
	}
	//current date and time from app, "DT YYYY-MM-DDTHH:MM:SS"
	else if (!strncmp(msg, "DT ", 3)){
		DateTime_t dt;
		if (ParseDateTime(msg + 3, &dt)){
			DateTime = dt;
			DateTimeTick = tick;
			TimeSet = true;
			Clock_Set(Clock_FromCivil(dt.year, dt.month, dt.day,
			                          dt.hour, dt.minute, dt.second), tick);
		}
	}
	//latitude from app, disregarded if device has a GNSS fix
	else if (!strncmp(msg, "LAT ", 4)){
		if (!GNSSFix){
			char *end;
			double lat = strtod(msg + 4, &end);
			if (end != msg + 4 && lat >= -90.0 && lat <= 90.0){
				LatitudeE7 = (int32_t)lround(lat * 1e7);
				LatSet = true;
			}
		}
	}
	//longitude from app, disregarded if device has a GNSS fix
	else if (!strncmp(msg, "LNG ", 4)){
		if (!GNSSFix){
			char *end;
			double lng = strtod(msg + 4, &end);
			if (end != msg + 4 && lng >= -180.0 && lng <= 180.0){
				LongitudeE7 = (int32_t)lround(lng * 1e7);
				LngSet = true;
			}
		}
	}
}

/* Parses "YYYY-MM-DDTHH:MM:SS". Returns false if the string is malformed. */
static bool ParseDateTime(const char *str, DateTime_t *dt){
	unsigned year, month, day, hour, minute, second;
	if (sscanf(str, "%4u-%2u-%2uT%2u:%2u:%2u",
	           &year, &month, &day, &hour, &minute, &second) != 6){
		return false;
	}
	if (month < 1 || month > 12 || day < 1 || day > 31 ||
	    hour > 23 || minute > 59 || second > 59){
		return false;
	}
	dt->year = (uint16_t)year;
	dt->month = (uint8_t)month;
	dt->day = (uint8_t)day;
	dt->hour = (uint8_t)hour;
	dt->minute = (uint8_t)minute;
	dt->second = (uint8_t)second;
	return true;
}



bool SendBLE(const uint8_t *msg, uint8_t msg_length){
	if (msg_length > UINT8_MAX){
		return false;
	}

	uint8_t data[UINT8_MAX];
	data[0] = 0x2D;
	data[1] = API_GetNextFrameId();
	data[2] = 0x01; //bluetooth
	uint8_t data_length = (uint8_t)(3 + msg_length);
	for (uint16_t i = 0; i < msg_length; i++)
	{
		data[i + 3] = msg[i];
	}

	return Frame(data, data_length);
}

void StatusResponse(void){
	char msg[24];
	uint8_t length;

	//battery state of charge from the BQ27441 fuel gauge (0 if not found)
	uint16_t batt = Battery_GetSOC();
	if (batt > 100) batt = 100;
	length = (uint8_t)snprintf(msg, sizeof(msg), "BATT %u", (unsigned)batt);
	SendBLE((const uint8_t *)msg, length);

	//recording time, HH:MM:SS since the session started (0 if not recording)
	bool recording = Sound_IsRunning();
	if (recording){
		uint32_t elapsed = (HAL_GetTick() - RecordStartTick) / 1000u;
		length = (uint8_t)snprintf(msg, sizeof(msg), "TIME %02lu:%02lu:%02lu",
		                           (unsigned long)(elapsed / 3600u),
		                           (unsigned long)((elapsed / 60u) % 60u),
		                           (unsigned long)(elapsed % 60u));
	}
	else{
		length = (uint8_t)snprintf(msg, sizeof(msg), "TIME 0");
	}
	SendBLE((const uint8_t *)msg, length);

	//recording state, 1 while recording, 0 when stopped
	length = (uint8_t)snprintf(msg, sizeof(msg), "REC %u", recording ? 1u : 0u);
	SendBLE((const uint8_t *)msg, length);

	//cellular bars, the AI/DB responses are handled in the EXTI callback
	GetCell = false;
	while (!SendATCommand("AI"));
	uint32_t starttime = HAL_GetTick();
	while (!GetCell && (HAL_GetTick() - starttime) < 500);
	length = (uint8_t)snprintf(msg, sizeof(msg), "CELL %u", (unsigned)Cell);
	SendBLE((const uint8_t *)msg, length);

	//number of GNSS satellites providing a lock (0 if no fix)
	length = (uint8_t)snprintf(msg, sizeof(msg), "GNSS %u",
	                           GNSSFix ? (unsigned)GNSS : 0u);
	SendBLE((const uint8_t *)msg, length);

	//optional packets, ask the app for anything the device doesn't know yet
	if (!Clock_IsSet()){
		length = (uint8_t)snprintf(msg, sizeof(msg), "REQUEST TIME");
		SendBLE((const uint8_t *)msg, length);
	}
	if (!GNSSFix && !(LatSet && LngSet)){
		length = (uint8_t)snprintf(msg, sizeof(msg), "REQUEST POS");
		SendBLE((const uint8_t *)msg, length);
	}
}


/* Queries an AT register. No parameter is sent: with one, the XBee would
 * set the register instead of reading it. */
bool SendATCommand(const char * command){
	uint8_t data[4];
	data[0] = 0x08;
	data[1] = API_GetNextFrameId();
	data[2] = command[0];
	data[3] = command[1];
	return Frame(data, sizeof(data));
}

bool SendSMS(const uint8_t *msg, uint8_t msg_length, const char number[10])
{
	if (number == NULL || (msg_length > 0 && msg == NULL) ||
	    msg_length > UINT8_MAX - 23)
	{
		return false;
	}
	uint8_t data[UINT8_MAX];
	data[0] = 0x1F;
	data[1] = API_GetNextFrameId();
	data[2] = 0x00;
	for (uint8_t i = 0; i < 10; i++)
	{
		data[i + 3] = (uint8_t)number[i];
	}
	for (uint8_t i = 13; i < 23; i++)
	{
		data[i] = 0;
	}

	uint8_t data_length = (uint8_t)(23u + msg_length);
	for (uint16_t i = 0; i < msg_length; i++)
	{
		data[i + 23] = msg[i];
	}

	return Frame(data, data_length);

}
