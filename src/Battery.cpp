#include "Battery.h"

#include <Arduino.h>

#include "battery/max17048.h"

void Battery::init() {
}

void Battery::powerOnInit() {
  uint16_t version;
  if (!max17048IsPresent(version)) {
    Serial.println("BATTERY GAUGE NOT FOUND!!");
    return;
  }
  Serial.printf("Battery gauge found, version 0x%04X\n", version);

  if (!max17048DisableSleep()) {
    Serial.println("Battery gauge: failed to disable sleep mode");
  }

  if (!max17048ForceHibernate()) {
    Serial.println("Battery gauge: failed to force hibernate");
  }

  if (!max17048DisableResetComparator()) {
    Serial.println("Battery gauge: failed to disable reset comparator");
  }
}

uint64_t Battery::prepareForSleep() {
  return 0;
}

int Battery::readMillivolts() {
  uint16_t millivolts;
  if (!max17048ReadMillivolts(millivolts)){ 
    Serial.println("Battery: gauge voltage read failed");
    return UNKNOWN_MILLIVOLTS;
  }
  return millivolts;
}

int Battery::readPercent() {
  uint8_t percent;
  if (!max17048ReadPercent(percent)) {
    Serial.println("Battery: gauge state-of-charge read failed");
    return UNKNOWN_PERCENT;
  }
  // Voltage for the log only (UNKNOWN_MILLIVOLTS if that read fails).
  Serial.printf("Battery: %u%% (%d mV)\n", percent, readMillivolts());
  return percent;
}

