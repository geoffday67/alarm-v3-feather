#pragma once

#include <stdint.h>

// LiPo state of charge, from the Feather's MAX17048
// fuel gauge (its ModelGauge state-of-charge estimate, with the cell voltage
// alongside for logging). The only interface to the gauge: nothing outside
// Battery.cpp uses battery/max17048.h directly.
//
// Uses the shared I2C bus and takes the bus lock itself, so don't call
// anything here while holding an I2CBus::Guard.
namespace Battery {

// readPercent()'s value when the gauge couldn't be read.
constexpr int UNKNOWN_PERCENT = -1;

// readMillivolts()'s value when the gauge couldn't be read.
constexpr int UNKNOWN_MILLIVOLTS = -1;

// Call on every boot, including each deep-sleep wake. Nothing to do at
// present: the gauge runs independently of the ESP32.
void init();

// Call once on a cold boot (power-on, reset, reflash), after
// I2CBus::begin(). Checks the gauge is present, makes sure it can't go into
// sleep mode while the I2C lines are low during the ESP32's deep sleep (see
// max17048DisableSleep()), puts it in hibernate permanently (see
// max17048ForceHibernate()) and turns off its reset comparator (see
// max17048DisableResetComparator()). The gauge keeps these settings for as
// long as it has the cell; reconnecting the cell resets both chips, so this
// runs again.
void powerOnInit();

// Nothing to prepare: the gauge is left running, in hibernate.
// Returns 0, as the battery has no wake sources (the return value is the
// ext1 wake mask, as for the other modules' prepareForSleep()).
uint64_t prepareForSleep();

// Reads the gauge and returns the cell voltage in mV, or UNKNOWN_MILLIVOLTS
// if it can't be read. This is the MAX17048's VCELL: the voltage between its
// VDD and GND pins (VDD is on BAT on the Feather), the average of four ADC
// conversions. Resolution 1.25 mV; error +/-7.5 mV at 3.6 V and 25 C, +/-20 mV
// over -20 to +70 C; range 2.5-5 V. Updated every 45 s (forced hibernate),
// so it can be up to 45 s old (datasheet Electrical Characteristics and VCELL
// Register). Logs only on failure.
int readMillivolts();

// Reads the gauge and returns the state of charge, 0-100 %, or
// UNKNOWN_PERCENT if it can't be read (logged). Logs the state of charge and
// voltage. The gauge's ModelGauge
// estimate from its built-in generic LiPo model, updated every 45 s (forced
// hibernate). Just after the cell is connected the estimate comes from the
// voltage at that moment, so it can be off if the cell was under load or
// charging; the gauge corrects it over time (datasheet: "ModelGauge heals
// error automatically").
int readPercent();
}  // namespace Battery
