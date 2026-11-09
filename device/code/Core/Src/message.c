#include "message.h"
#include "battery.h"

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

/* Bluetooth link RSSI in dBm (0 = unknown). */
int8_t BlueRSSI = 0;

/* Recording session state, used for the TIME status packet. */
bool Recording = false;
uint32_t RecordStartTick = 0;

/* Wall clock time received from the app ("DT YYYY-MM-DDTHH:MM:SS").
 * DateTimeTick is the HAL tick at the moment it was received, so the current
 * time is DateTime + (HAL_GetTick() - DateTimeTick). */
bool TimeSet = false;
DateTime_t DateTime = {0};
uint32_t DateTimeTick = 0;

/* Position received from the app ("LAT ##.#####" / "LNG ##.#####"). Ignored
 * when the device has its own GNSS fix. */
bool LatSet = false;
bool LngSet = false;
float Latitude = 0.0f;
float Longitude = 0.0f;

static bool ParseDateTime(const char *str, DateTime_t *dt);
static const char *BlueQuality(int8_t rssi);

void HandleAPI(const API_Status *status){
    if (status->frame_type == 0x89 && status->data_length > 0) {
      printf("API TX Status: frame ID=0x%02X status=0x%02X\r\n",
             status->frame_id, (uint8_t)status->data[0]);
    } else if (status->frame_type == 0x8A &&
               status->data_length > 0) {
      printf("API Modem Status: status=0x%02X\r\n",
             (uint8_t)status->data[0]);
    }
    else if (status->frame_type == 0xAD) {
      ReadBluetooth((const uint8_t *)status->data, status->data_length);
    }
}


void ReadBluetooth(const uint8_t *data, uint16_t length){
	if (data[0] != 1){
		return; //not from bluetooth on xbee
	}
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
			DateTimeTick = HAL_GetTick();
			TimeSet = true;
		}
	}
	//latitude from app, disregarded if device has a GNSS fix
	else if (!strncmp(msg, "LAT ", 4)){
		if (!GNSSFix){
			char *end;
			float lat = strtof(msg + 4, &end);
			if (end != msg + 4 && lat >= -90.0f && lat <= 90.0f){
				Latitude = lat;
				LatSet = true;
			}
		}
	}
	//longitude from app, disregarded if device has a GNSS fix
	else if (!strncmp(msg, "LNG ", 4)){
		if (!GNSSFix){
			char *end;
			float lng = strtof(msg + 4, &end);
			if (end != msg + 4 && lng >= -180.0f && lng <= 180.0f){
				Longitude = lng;
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

/* Maps a Bluetooth RSSI (dBm) to the GREAT/GOOD/POOR scale in the API doc. */
static const char *BlueQuality(int8_t rssi){
	if (rssi == 0) return "GOOD"; //unknown, but the STATUS request got through
	if (rssi >= -60) return "GREAT";
	if (rssi >= -80) return "GOOD";
	return "POOR";
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
	if (Recording){
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

	//bluetooth signal quality
	length = (uint8_t)snprintf(msg, sizeof(msg), "BLUE %s", BlueQuality(BlueRSSI));
	SendBLE((const uint8_t *)msg, length);

	//optional packets, ask the app for anything the device doesn't know yet
	if (!TimeSet){
		length = (uint8_t)snprintf(msg, sizeof(msg), "REQUEST TIME");
		SendBLE((const uint8_t *)msg, length);
	}
	if (!GNSSFix && !(LatSet && LngSet)){
		length = (uint8_t)snprintf(msg, sizeof(msg), "REQUEST POS");
		SendBLE((const uint8_t *)msg, length);
	}
}


bool SendATCommand(const char * command){
	uint8_t data[5];
	data[0] = 0x08;
	data[1] = API_GetNextFrameId();
	data[2] = command[0];
	data[3] = command[1];
	data[4] = 0;
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
