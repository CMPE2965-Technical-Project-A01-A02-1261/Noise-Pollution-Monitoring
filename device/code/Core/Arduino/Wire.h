/*
 * Wire.h
 *
 * Arduino TwoWire (I2C master) API implemented on top of the STM32 HAL.
 * The global `Wire` object drives I2C1 (PB8 = SCL, PB9 = SDA), which is
 * configured by MX_I2C1_Init() in main.c. MX_I2C1_Init() must run before any
 * Wire call.
 */
#ifndef ARDUINO_COMPAT_WIRE_H
#define ARDUINO_COMPAT_WIRE_H

#include "Arduino.h"

#ifndef BUFFER_LENGTH
#define BUFFER_LENGTH 32
#endif

/* Per-transfer HAL timeout in milliseconds. */
#ifndef WIRE_TIMEOUT_MS
#define WIRE_TIMEOUT_MS 10
#endif

class TwoWire {
public:
  explicit TwoWire(I2C_HandleTypeDef *hi2c);

  /* The HAL peripheral is already initialised by CubeMX, so these only reset
   * internal state. */
  void begin(void);
  void end(void);

  void beginTransmission(uint16_t address);
  /* Returns 0 on success, 1 data too long, 2 NACK on address, 4 other error,
   * 5 timeout (same codes as Arduino). */
  uint8_t endTransmission(bool sendStop = true);

  /* Returns the number of bytes received (0 on failure). */
  size_t requestFrom(uint16_t address, size_t quantity, bool sendStop = true);

  size_t write(uint8_t data);
  size_t write(const uint8_t *data, size_t quantity);

  int available(void);
  int read(void);
  int peek(void);

private:
  I2C_HandleTypeDef *_hi2c;
  uint16_t _txAddress;
  uint8_t _txBuffer[BUFFER_LENGTH];
  size_t _txLength;
  bool _txOverflow;
  uint8_t _rxBuffer[BUFFER_LENGTH];
  size_t _rxLength;
  size_t _rxIndex;
};

extern TwoWire Wire;

#endif /* ARDUINO_COMPAT_WIRE_H */
