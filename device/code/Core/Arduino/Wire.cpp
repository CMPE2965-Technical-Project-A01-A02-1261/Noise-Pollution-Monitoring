/*
 * Wire.cpp
 *
 * Arduino TwoWire implementation backed by STM32 HAL blocking I2C calls.
 */
#include "Wire.h"

extern "C" I2C_HandleTypeDef hi2c1;

TwoWire Wire(&hi2c1);

/* After a HAL timeout the peripheral can be left mid-transfer, driving SCL
 * and SDA low with BUSY set; the handle is marked ready, but every later
 * transfer then times out too. Clearing PE resets the I2C state machine and
 * releases the lines (RM0444: PE must stay low for 3 APB cycles; the read
 * back covers that). */
static void ResetPeripheral(I2C_HandleTypeDef *hi2c) {
  __HAL_I2C_DISABLE(hi2c);
  while (hi2c->Instance->CR1 & I2C_CR1_PE) {
  }
  __HAL_I2C_ENABLE(hi2c);
}

TwoWire::TwoWire(I2C_HandleTypeDef *hi2c)
    : _hi2c(hi2c), _txAddress(0), _txLength(0), _txOverflow(false),
      _rxLength(0), _rxIndex(0) {}

void TwoWire::begin(void) {
  ResetPeripheral(_hi2c);
  _txLength = 0;
  _txOverflow = false;
  _rxLength = 0;
  _rxIndex = 0;
}

void TwoWire::end(void) {}

void TwoWire::beginTransmission(uint16_t address) {
  _txAddress = address;
  _txLength = 0;
  _txOverflow = false;
}

uint8_t TwoWire::endTransmission(bool sendStop) {
  /* HAL_I2C_Master_Transmit always ends with a STOP; repeated-start writes are
   * not needed by the BQ27441 library. */
  (void)sendStop;

  if (_txOverflow) {
    _txLength = 0;
    return 1;
  }

  HAL_StatusTypeDef status =
      HAL_I2C_Master_Transmit(_hi2c, (uint16_t)(_txAddress << 1), _txBuffer,
                              (uint16_t)_txLength, WIRE_TIMEOUT_MS);
  _txLength = 0;

  if (status == HAL_OK) {
    return 0;
  }
  ResetPeripheral(_hi2c);
  if (status == HAL_TIMEOUT) {
    return 5;
  }
  if (HAL_I2C_GetError(_hi2c) & HAL_I2C_ERROR_AF) {
    return 2;
  }
  return 4;
}

size_t TwoWire::requestFrom(uint16_t address, size_t quantity, bool sendStop) {
  (void)sendStop;

  if (quantity > BUFFER_LENGTH) {
    quantity = BUFFER_LENGTH;
  }

  _rxIndex = 0;
  _rxLength = 0;

  if (quantity == 0) {
    return 0;
  }

  if (HAL_I2C_Master_Receive(_hi2c, (uint16_t)(address << 1), _rxBuffer,
                             (uint16_t)quantity, WIRE_TIMEOUT_MS) == HAL_OK) {
    _rxLength = quantity;
  } else {
    ResetPeripheral(_hi2c);
  }
  return _rxLength;
}

size_t TwoWire::write(uint8_t data) {
  if (_txLength >= BUFFER_LENGTH) {
    _txOverflow = true;
    return 0;
  }
  _txBuffer[_txLength++] = data;
  return 1;
}

size_t TwoWire::write(const uint8_t *data, size_t quantity) {
  size_t written = 0;
  while (written < quantity && write(data[written])) {
    written++;
  }
  return written;
}

int TwoWire::available(void) { return (int)(_rxLength - _rxIndex); }

int TwoWire::read(void) {
  if (_rxIndex >= _rxLength) {
    return -1;
  }
  return _rxBuffer[_rxIndex++];
}

int TwoWire::peek(void) {
  if (_rxIndex >= _rxLength) {
    return -1;
  }
  return _rxBuffer[_rxIndex];
}
