#ifndef API_H
#define API_H
#include "string.h"
#include "stdio.h"

#include "stm32g0xx_hal.h"
#include "stdbool.h"
#include <stddef.h>

/* Largest frame data the XBee sends (socket receive payloads are up to
 * 1500 bytes, plus the socket ID and status bytes). */
#define API_MAX_DATA 1504u

typedef struct
{
	uint8_t frame_type;
	uint8_t frame_id; /* Zero for frames that do not carry a frame ID. */
	uint16_t data_length;
	uint32_t tick; /* HAL tick when the frame finished arriving */
	char data[API_MAX_DATA]; /* Frame data after the type and any frame ID. */
} API_Status;


void API_ProcessByte(uint8_t byte);
bool API_ReadStatus(API_Status *status);
uint8_t API_GetDroppedStatusCount(void);
uint8_t API_GetNextFrameId(void);
void API_ResetParser(void);

uint16_t MakeFrame(uint8_t *frame, uint8_t length, const uint8_t *data);

bool SendFrame (const uint8_t *frame, uint16_t frame_length);
bool Frame(const uint8_t *data, uint8_t length);

#endif
