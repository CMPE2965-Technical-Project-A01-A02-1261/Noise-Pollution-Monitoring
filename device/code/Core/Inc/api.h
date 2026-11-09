#ifndef API_H
#define API_H
#include "string.h"
#include "stdio.h"

#include "stm32g0xx_hal.h"
#include "stdbool.h"
#include <stddef.h>

typedef struct
{
	uint8_t frame_type;
	uint8_t frame_id; /* Zero for frames that do not carry a frame ID. */
	uint16_t data_length;
	char data[256]; /* Frame data after the type and any frame ID. */
} API_Status;


void API_ProcessByte(uint8_t byte);
bool API_ReadStatus(volatile API_Status *status);
uint8_t API_GetDroppedStatusCount(void);
uint8_t API_GetNextFrameId(void);
void API_ResetParser(void);

uint16_t MakeFrame(uint8_t *frame, uint8_t length, const uint8_t *data);

bool SendFrame (const uint8_t *frame, uint16_t frame_length);
bool Frame(const uint8_t *data, uint8_t length);

#endif
