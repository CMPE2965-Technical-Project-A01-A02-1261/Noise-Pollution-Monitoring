#include "message.h"

bool GetBatt = false;
uint8_t Batt = 0;
volatile bool GetCell = false;
volatile uint8_t Cell = 0;
bool GetGNSS = false;
uint8_t GNSS = 0;

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
	//extract msg from rest of data array
	char *msg = (char *)(data+1);
	length --;
	//status message received
	if (!strncmp(msg, "STATUS", length)){
		StatusResponse();
	}
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

	//wait for command to be sent
	while (!SendATCommand("AI"));
	uint32_t starttime = HAL_GetTick();

	while (!GetCell && (HAL_GetTick() - starttime) < 500);
	char msg[7];
	uint8_t length = sprintf(msg, "CELL: %d", Cell);
	SendBLE(msg, length);
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
