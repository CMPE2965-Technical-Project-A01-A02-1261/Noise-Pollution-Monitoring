#include "main.h"

#include <stddef.h>

extern UART_HandleTypeDef huart1;

static uint8_t frame_count;

#define API_STATUS_QUEUE_SIZE 4u

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
static uint8_t rx_checksum;
static uint8_t rx_frame_type;
static uint8_t rx_frame_id;
static uint8_t rx_status;

static volatile API_Status status_queue[API_STATUS_QUEUE_SIZE];
static volatile uint8_t status_queue_head;
static volatile uint8_t status_queue_tail;
static volatile uint8_t dropped_status_count;

static void API_QueueStatus(uint8_t frame_type, uint8_t frame_id, uint8_t status)
{
	uint8_t next_head = (uint8_t)((status_queue_head + 1u) % API_STATUS_QUEUE_SIZE);
	if (next_head == status_queue_tail)
	{
		dropped_status_count++;
		return;
	}

	status_queue[status_queue_head].frame_type = frame_type;
	status_queue[status_queue_head].frame_id = frame_id;
	status_queue[status_queue_head].status = status;
	status_queue_head = next_head;
}

void API_ResetParser(void)
{
	rx_state = API_RX_WAIT_START;
	rx_length = 0;
	rx_data_index = 0;
	rx_checksum = 0;
	rx_frame_type = 0;
	rx_frame_id = 0;
	rx_status = 0;
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
			rx_checksum = 0;
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
		else if (rx_data_index == 1)
		{
			if (rx_frame_type == 0x8A)
			{
				rx_status = byte;
			}
			else
			{
				rx_frame_id = byte;
			}
		}
		else if (rx_data_index == 2)
		{
			rx_status = byte;
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
			if (rx_frame_type == 0x89 && rx_length == 3)
			{
				API_QueueStatus(rx_frame_type, rx_frame_id, rx_status);
			}
			else if (rx_frame_type == 0x8A && rx_length == 2)
			{
				API_QueueStatus(rx_frame_type, 0, rx_status);
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

	status->frame_type = status_queue[status_queue_tail].frame_type;
	status->frame_id = status_queue[status_queue_tail].frame_id;
	status->status = status_queue[status_queue_tail].status;
	status_queue_tail = (uint8_t)((status_queue_tail + 1u) % API_STATUS_QUEUE_SIZE);
	return true;
}

uint8_t API_GetDroppedStatusCount(void)
{
	return dropped_status_count;
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
	data[1] = 0x81;
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

	uint8_t frame[UINT8_MAX + 4];
	uint16_t frame_length = MakeFrame(frame, data_length, data);
	HAL_StatusTypeDef status = HAL_UART_Transmit(&huart1, frame, frame_length, HAL_MAX_DELAY);
	if (status == HAL_OK)
	{
		frame_count++;
		return true;
	}
	return false;
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
