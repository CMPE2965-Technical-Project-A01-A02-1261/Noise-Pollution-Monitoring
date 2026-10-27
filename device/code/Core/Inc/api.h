#ifndef API_H
#define API_H

#include "stm32g0xx_hal.h"
#include "stdbool.h"

typedef struct
{
	uint8_t frame_type;
	uint8_t frame_id; /* Zero for Modem Status frames (0x8A). */
	uint8_t status;
} API_Status;

bool SendBLE(const uint8_t, uint8_t);
bool SendCell(const uint8_t, uint8_t);
bool SendSMS(const uint8_t *msg, uint8_t msg_length, const char number[10]);

uint16_t MakeFrame(uint8_t *frame, uint8_t length, const uint8_t *data);

void API_ProcessByte(uint8_t byte);
bool API_ReadStatus(API_Status *status);
uint8_t API_GetDroppedStatusCount(void);
void API_ResetParser(void);

#endif
