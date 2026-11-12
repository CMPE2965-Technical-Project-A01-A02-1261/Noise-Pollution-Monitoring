#ifndef SOUND_H
#define SOUND_H

#include "stm32g0xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

/* Microphone sensitivity (DMM-4026-B-I2S): output RMS in dBFS (relative to
 * full scale peak) for a 94 dB SPL 1 kHz tone. dB SPL = dBFS + 94 - sensitivity. */
#define SOUND_MIC_SENSITIVITY_DBFS (-26.0f)
/* Correction found by comparing against a calibrated reference meter
 * (Spec 3 verification test). Added to every reading. */
#define SOUND_CAL_OFFSET_DB (0.0f)

/* One second of audio, reduced to a single level. */
typedef struct {
	float db;         /* LAeq over the window, dBA */
	float peak_db;    /* largest A-weighted sample in the window, dBA */
	bool clipped;     /* a sample hit digital full scale; db is a lower bound */
	bool valid;       /* false when the mic sent a constant value (missing/dead) */
	uint32_t tick;    /* HAL tick when the window ended */
} Sound_Reading;

bool Sound_Start(I2S_HandleTypeDef *hi2s);
bool Sound_Read(Sound_Reading *reading);
uint32_t Sound_GetSampleRate(void);
uint8_t Sound_GetDroppedCount(void);

#endif
