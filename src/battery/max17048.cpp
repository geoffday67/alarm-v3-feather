#include "max17048.h"

#include <Wire.h>

#include "../I2CBus.h"

bool max17048ReadReg(uint8_t reg, uint16_t& value) {
  I2CBus::Guard bus;
  Wire.beginTransmission(MAX17048_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {  // repeated start, no stop condition
    return false;
  }
  if (Wire.requestFrom((uint8_t)MAX17048_ADDR, (uint8_t)2) != 2) {
    return false;
  }
  uint8_t msb = Wire.read();
  uint8_t lsb = Wire.read();
  value = ((uint16_t)msb << 8) | lsb;
  return true;
}

bool max17048WriteReg(uint8_t reg, uint16_t value) {
  I2CBus::Guard bus;
  Wire.beginTransmission(MAX17048_ADDR);
  Wire.write(reg);
  Wire.write((uint8_t)(value >> 8));
  Wire.write((uint8_t)(value & 0xFF));
  return Wire.endTransmission() == 0;
}

bool max17048IsPresent(uint16_t& version) {
  if (!max17048ReadReg(MAX17048_REG_VERSION, version)) {
    return false;
  }
  return (version & 0xFFF0) == 0x0010;
}

bool max17048DisableSleep() {
  // CONFIG first: writing SLEEP = 0 only forces an exit from sleep while
  // EnSleep is still set.
  uint16_t config;
  if (!max17048ReadReg(MAX17048_REG_CONFIG, config)) {
    return false;
  }
  if (config & MAX17048_CONFIG_SLEEP) {
    if (!max17048WriteReg(MAX17048_REG_CONFIG, config & ~MAX17048_CONFIG_SLEEP)) {
      return false;
    }
  }

  // MODE is write-only, so it's written unconditionally. 0 = EnSleep off
  // and no quick-start (which would discard the gauge's SOC estimate).
  return max17048WriteReg(MAX17048_REG_MODE, 0x0000);
}

bool max17048ForceHibernate() {
  return max17048WriteReg(MAX17048_REG_HIBRT, 0xFFFF);
}

bool max17048DisableResetComparator() {
  uint16_t vreset;
  if (!max17048ReadReg(MAX17048_REG_VRESET_ID, vreset)) {
    return false;
  }
  if (vreset & MAX17048_VRESET_DIS) {
    return true;  // already off
  }
  return max17048WriteReg(MAX17048_REG_VRESET_ID, vreset | MAX17048_VRESET_DIS);
}

bool max17048ReadMillivolts(uint16_t& millivolts) {
  uint16_t raw;
  if (!max17048ReadReg(MAX17048_REG_VCELL, raw)) {
    return false;
  }
  // 78.125 uV = 5/64 mV
  millivolts = (uint16_t)(((uint32_t)raw * 5 + 32) / 64);
  return true;
}
