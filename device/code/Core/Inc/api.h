#ifndef API_H
#define API_H
#include "string.h"
#include "stdio.h"

#include "stm32g0xx_hal.h"
#include "stdbool.h"

typedef struct
{
	uint8_t frame_type;
	uint8_t frame_id; /* Zero for frames that do not carry a frame ID. */
	uint16_t data_length;
	char data[256]; /* Frame data after the type and any frame ID. */
} API_Status;

bool SendBLE(const uint8_t *msg, uint8_t msg_length);
bool SendCell(const uint8_t, uint8_t);
bool SendSMS(const uint8_t *msg, uint8_t msg_length, const char number[10]);
void API_RxBluetooth(const uint8_t *data, uint16_t length);

uint16_t MakeFrame(uint8_t *frame, uint8_t length, const uint8_t *data);

void API_ProcessByte(uint8_t byte);
bool API_ReadStatus(API_Status *status);
uint8_t API_GetDroppedStatusCount(void);
void API_ResetParser(void);

void StatusResponse(void);
bool SendFrame (const uint8_t *frame, uint16_t frame_length);

#endif
