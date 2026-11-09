/*
 * Arduino.h
 *
 * Minimal Arduino compatibility layer for STM32 HAL. Provides just enough of
 * the Arduino core API for the SparkFun BQ27441 library to compile unchanged.
 */
#ifndef ARDUINO_COMPAT_H
#define ARDUINO_COMPAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "stm32g0xx_hal.h"

typedef uint8_t byte;
typedef bool boolean;

#ifndef constrain
#define constrain(amt, low, high) \
  ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#endif

static inline void delay(uint32_t ms) { HAL_Delay(ms); }
static inline uint32_t millis(void) { return HAL_GetTick(); }

#endif /* ARDUINO_COMPAT_H */
