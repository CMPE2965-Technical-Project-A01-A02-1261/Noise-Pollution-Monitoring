/*
 * battery.cpp
 *
 * C wrapper around the SparkFun BQ27441 Arduino library.
 */
#include "battery.h"

#include "SparkFunBQ27441.h"

static bool battery_connected = false;

extern "C" bool Battery_Init(uint16_t design_capacity_mAh) {
  battery_connected = lipo.begin();

  if (battery_connected && design_capacity_mAh != 0) {
    lipo.setCapacity(design_capacity_mAh);
  }

  return battery_connected;
}

extern "C" bool Battery_IsConnected(void) { return battery_connected; }

extern "C" bool Battery_Read(Battery_Status *status) {
  if (!battery_connected || status == nullptr) {
    return false;
  }

  status->voltage_mV = lipo.voltage();
  status->current_mA = lipo.current(AVG);
  status->power_mW = lipo.power();
  status->soc_percent = lipo.soc(FILTERED);
  status->soh_percent = lipo.soh(PERCENT);
  status->remaining_mAh = lipo.capacity(REMAIN);
  status->full_mAh = lipo.capacity(FULL);
  /* Gauge reports 0.1 K; convert to 0.1 deg C. */
  status->temperature_dC =
      (int16_t)((int32_t)lipo.temperature(BATTERY) - 2732);

  uint16_t flags = lipo.flags();
  status->charging = status->current_mA > 0;
  status->discharging = (flags & BQ27441_FLAG_DSG) != 0;

  return true;
}

extern "C" uint16_t Battery_GetSOC(void) {
  return battery_connected ? lipo.soc(FILTERED) : 0;
}

extern "C" uint16_t Battery_GetVoltage(void) {
  return battery_connected ? lipo.voltage() : 0;
}

extern "C" int16_t Battery_GetCurrent(void) {
  return battery_connected ? lipo.current(AVG) : 0;
}
