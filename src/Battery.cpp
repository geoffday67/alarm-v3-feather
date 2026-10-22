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
  if (!max17048ReadMillivolts(millivolts)) {
    Serial.println("Battery: gauge voltage read failed");
    return UNKNOWN_MILLIVOLTS;
  }
  return millivolts;
}

bool Battery::isLow() {
  int millivolts = readMillivolts();
  if (millivolts == UNKNOWN_MILLIVOLTS) {
    return false;
  }

  bool low = millivolts <= LOW_MILLIVOLTS;
  Serial.printf("Battery: %d mV%s\n", millivolts, low ? " (LOW)" : "");
  return low;
}
