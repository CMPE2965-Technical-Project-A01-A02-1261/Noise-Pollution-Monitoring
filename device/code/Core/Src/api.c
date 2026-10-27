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
static uint16_t rx_payload_length;
static uint8_t rx_checksum;
static uint8_t rx_frame_type;
static uint8_t rx_frame_id;
static char rx_data[256];
static bool rx_payload_too_large;

static volatile API_Status status_queue[API_STATUS_QUEUE_SIZE];
static volatile uint8_t status_queue_head;
static volatile uint8_t status_queue_tail;
static volatile uint8_t dropped_status_count;

static bool API_FrameHasId(uint8_t frame_type)
{
	return frame_type == 0x88 || frame_type == 0x89 ||
	       frame_type == 0x8B || frame_type == 0x97;
}

static void API_QueueFrame(void)
{
	uint8_t next_head = (uint8_t)((status_queue_head + 1u) % API_STATUS_QUEUE_SIZE);
	if (next_head == status_queue_tail)
	{
		dropped_status_count++;
		return;
	}

	status_queue[status_queue_head].frame_type = rx_frame_type;
	status_queue[status_queue_head].frame_id = rx_frame_id;
	status_queue[status_queue_head].data_length = rx_payload_length;
	for (uint16_t i = 0; i < rx_payload_length; i++)
	{
		status_queue[status_queue_head].data[i] = rx_data[i];
	}
	status_queue_head = next_head;
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
			if (rx_payload_length < sizeof(rx_data))
			{
				rx_data[rx_payload_length] = (char)byte;
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
				API_QueueFrame();
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
	status->data_length = status_queue[status_queue_tail].data_length;
	for (uint16_t i = 0; i < status->data_length; i++)
	{
		status->data[i] = status_queue[status_queue_tail].data[i];
	}
	status_queue_tail = (uint8_t)((status_queue_tail + 1u) % API_STATUS_QUEUE_SIZE);
	return true;
}

uint8_t API_GetDroppedStatusCount(void)
{
	return dropped_status_count;
}

void API_RxBluetooth(const uint8_t *data, uint16_t length){
	if (data[0] != 1){
		return; //not from bluetooth on xbee
	}
	//extract msg from rest of data array
	char *msg = (char *)(data+1);
	length --;
	//status message received
	if (strncmp(msg, "STATUS", length)){

	}
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
