#pragma once

#include <stdint.h>
#include <time.h>

// The alarm time set on the two 12-position rotary switches. Each switch's
// wiper taps a resistor ladder powered from VSENSOR, read by the ADC on
// HOUR_LADDER_PIN and MINUTE_LADDER_PIN.
//
// VSENSOR is switched by IO20, which the board variant raises at boot (the
// I2C pull-ups depend on it too), so the ladders are powered whenever the
// chip is awake and nothing here drives a power pin.
namespace Alarm {
// Call on every boot, including each deep-sleep wake, before
// readSetTime(). Sets both ladder pins as inputs with the internal pull-up
// and pull-down off, so neither loads the ladder.
void init();

// Reads both switches. hour is 0-11 and minute is 0-55 in steps of 5; both
// at 0 means the alarm is off. Returns false, leaving hour and minute
// unchanged, if either reading is more than 100 mV from a
// ladder step (e.g. a switch caught between detents, where the
// break-before-make wiper floats); both readings are then logged.
bool readSetTime(uint8_t& hour, uint8_t& minute);

// Call once per RTC tick with the current (valid) UTC, from one place only:
// it records utc as the last wake time, kept in RTC memory across deep
// sleep. Returns true if the dial time, taken as local time (TZ), fell in
// (last wake, utc] - i.e. it passed since the previous call. Uses the most
// recent occurrence of the dial time as an absolute instant, so it works
// across midnight. Returns false without recording utc if the dials can't be
// read, so the next call still covers the missed interval. Always false on
// the first call after a cold boot (no last wake time yet) and when the
// alarm is off (both dials at 0).
bool isDue(time_t utc);
}  // namespace Alarm
