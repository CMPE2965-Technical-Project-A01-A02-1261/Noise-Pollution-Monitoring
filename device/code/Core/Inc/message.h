
#ifndef MESSAGE_H
#define MESSAGE_H

#include "api.h"
#include "main.h"

extern volatile bool GetCell;
extern volatile uint8_t Cell;

bool SendBLE(const uint8_t *msg, uint8_t msg_length);
bool SendATCommand(const char * command);
bool SendCell(const uint8_t, uint8_t);
bool SendSMS(const uint8_t *msg, uint8_t msg_length, const char number[10]);
void ReadBluetooth(const uint8_t *data, uint16_t length);

void HandleAPI(const API_Status *status);


void StatusResponse(void);




#endif
