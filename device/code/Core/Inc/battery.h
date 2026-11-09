/*
 * battery.h
 *
 * C interface to the BQ27441 LiPo fuel gauge on I2C1 (PB8 SCL / PB9 SDA).
 * Wraps the SparkFun BQ27441 Arduino library (C++), so it can be called from
 * main.c and the rest of the C code base.
 */
#ifndef BATTERY_H
#define BATTERY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint16_t voltage_mV;         /* Battery voltage */
  int16_t current_mA;          /* Average current (+ charging, - discharging) */
  int16_t power_mW;            /* Average power */
  uint16_t soc_percent;        /* State of charge, 0-100 */
  uint8_t soh_percent;         /* State of health, 0-100 */
  uint16_t remaining_mAh;      /* Remaining capacity */
  uint16_t full_mAh;           /* Full charge capacity */
  int16_t temperature_dC;      /* Gauge temperature in 0.1 deg C */
  bool charging;
  bool discharging;
} Battery_Status;

/*
 * Verifies the BQ27441 responds on I2C1. If design_capacity_mAh is non-zero
 * it is written to the gauge (only needed after the gauge loses power).
 * MX_I2C1_Init() must have been called first. Returns true if the gauge
 * was found.
 */
bool Battery_Init(uint16_t design_capacity_mAh);

/* True once Battery_Init() has found the gauge. */
bool Battery_IsConnected(void);

/* Reads all measurements. Returns false if the gauge is not connected. */
bool Battery_Read(Battery_Status *status);

uint16_t Battery_GetSOC(void);
uint16_t Battery_GetVoltage(void);
int16_t Battery_GetCurrent(void);

#ifdef __cplusplus
}
#endif

#endif /* BATTERY_H */
