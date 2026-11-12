#include "main.h"

#include <stddef.h>

extern UART_HandleTypeDef huart1;

static uint8_t frame_count = 1;

/* Frames waiting for the main loop. Slots hold a full XBee frame (socket
 * data can be up to 1500 bytes), so they are filled in place by the parser
 * instead of being copied in the UART interrupt. */
#define API_STATUS_QUEUE_SIZE 6u

typedef enum
{
	API_RX_WAIT_START,
	API_RX_LENGTH_HIGH,
	API_RX_LENGTH_LOW,
	API_RX_FRAME_DATA,
	API_RX_CHECKSUM
} API_RxState;

static API_RxState rx_state;
static uint16_t rx_length;
static uint16_t rx_data_index;
static uint16_t rx_payload_length;
static uint8_t rx_checksum;
static uint8_t rx_frame_type;
static uint8_t rx_frame_id;
static bool rx_payload_too_large;

/* The slot at status_queue_head is never visible to the reader, so the
 * parser writes the incoming frame straight into it and only advances the
 * head once the checksum is good. */
static API_Status status_queue[API_STATUS_QUEUE_SIZE];
static volatile uint8_t status_queue_head;
static volatile uint8_t status_queue_tail;
static volatile uint8_t dropped_status_count;

/* Largest AT response copied for the EXTI handler (it reads 4 bytes). */
#define API_URGENT_MAX_DATA 32u


static bool API_FrameHasId(uint8_t frame_type)
{
	return frame_type == 0x88 || frame_type == 0x89 ||
	       frame_type == 0x8B || frame_type == 0x97 ||
	       frame_type == 0xBD || frame_type == 0xC0 ||
	       frame_type == 0xC2 || frame_type == 0xC3 ||
	       frame_type == 0xCD;
}

static void API_QueueFrame(void)
{
	API_Status *slot = &status_queue[status_queue_head];
	uint8_t next_head = (uint8_t)((status_queue_head + 1u) % API_STATUS_QUEUE_SIZE);

	slot->frame_type = rx_frame_type;
	slot->frame_id = rx_frame_id;
	slot->data_length = rx_payload_length;
	slot->tick = HAL_GetTick();
	if (next_head == status_queue_tail)
	{
		dropped_status_count++;
		return;
	}
	__DMB(); /* slot contents before the head that publishes it */
	status_queue_head = next_head;
}

/* AT command responses are also handed to the EXTI handler right away. */
static void API_PublishUrgentStatus(const API_Status *slot)
{
	uint16_t length = rx_payload_length;
	if (length > API_URGENT_MAX_DATA)
	{
		length = API_URGENT_MAX_DATA;
	}
	api_status.frame_type = rx_frame_type;
	api_status.frame_id = rx_frame_id;
	api_status.data_length = length;
	for (uint16_t i = 0; i < length; i++)
	{
		api_status.data[i] = slot->data[i];
	}
}

void API_ResetParser(void)
{
	rx_state = API_RX_WAIT_START;
	rx_length = 0;
	rx_data_index = 0;
	rx_payload_length = 0;
	rx_checksum = 0;
	rx_frame_type = 0;
	rx_frame_id = 0;
	rx_payload_too_large = false;
}

void API_ProcessByte(uint8_t byte)
{
	switch (rx_state)
	{
	case API_RX_WAIT_START:
		if (byte == 0x7E)
		{
			rx_length = 0;
			rx_data_index = 0;
			rx_payload_length = 0;
			rx_checksum = 0;
			rx_frame_id = 0;
			rx_payload_too_large = false;
			rx_state = API_RX_LENGTH_HIGH;
		}
		break;

	case API_RX_LENGTH_HIGH:
		rx_length = (uint16_t)byte << 8;
		rx_state = API_RX_LENGTH_LOW;
		break;

	case API_RX_LENGTH_LOW:
		rx_length |= byte;
		if (rx_length == 0)
		{
			API_ResetParser();
		}
		else
		{
			rx_state = API_RX_FRAME_DATA;
		}
		break;

	case API_RX_FRAME_DATA:
		if (rx_data_index == 0)
		{
			rx_frame_type = byte;
		}
		else if (rx_data_index == 1 && API_FrameHasId(rx_frame_type))
		{
			rx_frame_id = byte;
		}
		else
		{
			if (rx_payload_length < API_MAX_DATA)
			{
				status_queue[status_queue_head].data[rx_payload_length] = (char)byte;
			}
			else
			{
				rx_payload_too_large = true;
			}
			rx_payload_length++;
		}

		rx_checksum = (uint8_t)(rx_checksum + byte);
		rx_data_index++;
		if (rx_data_index == rx_length)
		{
			rx_state = API_RX_CHECKSUM;
		}
		break;

	case API_RX_CHECKSUM:
		if ((uint8_t)(rx_checksum + byte) == 0xFF)
		{
			if (!rx_payload_too_large)
			{
				/* The frame is in this slot whether or not it fits in
				 * the queue, so the urgent copy works either way. */
				const API_Status *slot = &status_queue[status_queue_head];
				API_QueueFrame();
				if (rx_frame_type == 0x88)
				{
					API_PublishUrgentStatus(slot);
					__HAL_GPIO_EXTI_GENERATE_SWIT(GPIO_PIN_15);
				}
			}
		}
		API_ResetParser();
		break;
	}
}

bool API_ReadStatus(API_Status *status)
{
	if (status == NULL || status_queue_tail == status_queue_head)
	{
		return false;
	}
	__DMB(); /* head read before the slot contents */

	const API_Status *slot = &status_queue[status_queue_tail];
	status->frame_type = slot->frame_type;
	status->frame_id = slot->frame_id;
	status->data_length = slot->data_length;
	status->tick = slot->tick;
	memcpy(status->data, slot->data, slot->data_length);

	__DMB(); /* finish reading the slot before handing it back */
	status_queue_tail = (uint8_t)((status_queue_tail + 1u) % API_STATUS_QUEUE_SIZE);
	return true;
}

uint8_t API_GetDroppedStatusCount(void)
{
	return dropped_status_count;
}

uint8_t API_GetNextFrameId(void)
{
	return frame_count;
}





bool Frame(const uint8_t *data, uint8_t length){

	uint8_t frame[UINT8_MAX + 4];
	uint16_t frame_length = MakeFrame(frame, length, data);

	return SendFrame(frame, frame_length);

}

uint16_t MakeFrame(uint8_t *frame, uint8_t length, const uint8_t *data)
{
	frame[0] = 0x7E;
	frame[1] = 0x00;
	frame[2] = length;
	uint8_t sum = 0;
	for (uint16_t i = 0; i < length; i++)
	{
		frame[i + 3] = data[i];
		sum += data[i];
	}
	frame[length + 3] = (0xFF - (uint8_t)sum);

	// FIX: Parentheses are mandatory here to prevent casting order bugs
	return (uint16_t)(length + 4);
}

bool SendFrame (const uint8_t *frame, uint16_t frame_length){
	HAL_StatusTypeDef status = HAL_UART_Transmit(&huart1, frame, frame_length, HAL_MAX_DELAY);
		if (status == HAL_OK)
		{
			frame_count++;
			if (frame_count == 0)
				frame_count = 1;
			return true;
		}
		return false;
}
