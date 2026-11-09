
#ifndef MESSAGE_H
#define MESSAGE_H

#include "api.h"
#include "main.h"

typedef struct {
	uint16_t year;
	uint8_t month;
	uint8_t day;
	uint8_t hour;
	uint8_t minute;
	uint8_t second;
} DateTime_t;

extern volatile bool GetCell;
extern volatile uint8_t Cell;

extern bool GNSSFix;
extern uint8_t GNSS;
extern int8_t BlueRSSI;

extern bool Recording;
extern uint32_t RecordStartTick;

extern bool TimeSet;
extern DateTime_t DateTime;
extern uint32_t DateTimeTick;

extern bool LatSet;
extern bool LngSet;
extern float Latitude;
extern float Longitude;

bool SendBLE(const uint8_t *msg, uint8_t msg_length);
bool SendATCommand(const char * command);
bool SendCell(const uint8_t, uint8_t);
bool SendSMS(const uint8_t *msg, uint8_t msg_length, const char number[10]);
void ReadBluetooth(const uint8_t *data, uint16_t length);

void HandleAPI(const API_Status *status);


void StatusResponse(void);




#endif
