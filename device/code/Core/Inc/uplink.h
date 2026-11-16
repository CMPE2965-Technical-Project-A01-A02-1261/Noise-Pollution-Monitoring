#ifndef UPLINK_H
#define UPLINK_H

#include "api.h"
#include "sound.h"

#include <stdbool.h>
#include <stdint.h>

/* Server the readings go to (plain HTTP POST through an XBee TCP socket). */
#define UPLINK_HOST "thor.cnt.sast.ca"
#define UPLINK_PORT 80u
#define UPLINK_PATH "/~lantiveros1t1251/noise/api/readings.php"
#define UPLINK_DEVICE_ID "NM-0001"

/* How often a batch is sent. Each request is a new TCP connection
 * (Connection: close), which takes a few seconds on LTE-M, so readings are
 * batched instead of sent one per second. */
#define UPLINK_INTERVAL_MS 30000u

void Uplink_AddReading(const Sound_Reading *reading);
void Uplink_Process(void);
void Uplink_HandleFrame(const API_Status *status);
bool Uplink_IsIdle(void);
uint32_t Uplink_GetPendingCount(void);
uint32_t Uplink_GetDroppedCount(void);

#endif
