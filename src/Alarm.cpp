#include "Alarm.h"

#include <Arduino.h>
#include <stdlib.h>
#include <time.h>

#include "Constants.h"

namespace {

constexpr int POSITIONS = 12;

// UTC of the previous isDue() call. Survives deep sleep; reset to 0 (none)
// on a cold boot, since RTC_DATA_ATTR variables are reloaded then.
RTC_DATA_ATTR time_t lastWakeUtc = 0;

// The ladder is assumed to divide VSENSOR into equal steps, with the first
// position (hour 0 / minute 00) at 0 V and the last (hour 11 / minute 55) at
// VSENSOR.
constexpr int LADDER_TOP_MV = 3300;
constexpr int STEP_MV = LADDER_TOP_MV / (POSITIONS - 1);  // 300 mV

// ADC samples averaged per reading (datasheet Table 5-5 suggests averaging
// to improve DNL).
constexpr int SAMPLES = 8;

// Largest accepted distance from a step. The ADC's calibrated total error at
// 12 dB attenuation (0-3300 mV range) is +/-40 mV (datasheet Table 5-6), and
// VSENSOR's regulator tolerance adds to that because the ADC is not
// ratiometric to the ladder supply. Must be under half a step so a reading
// can only match one position.
constexpr int TOLERANCE_MV = 100;
static_assert(2 * TOLERANCE_MV < STEP_MV, "TOLERANCE_MV must be under half a step");

int readMillivolts(uint8_t pin) {
  uint32_t sum = 0;
  for (int i = 0; i < SAMPLES; i++) {
    sum += analogReadMilliVolts(pin);
  }
  return (sum + SAMPLES / 2) / SAMPLES;
}

// Position index 0-11 nearest mv, or -1 if mv is more than TOLERANCE_MV from
// it.
int toPosition(int mv) {
  int index = (mv + STEP_MV / 2) / STEP_MV;
  if (index > POSITIONS - 1) {
    index = POSITIONS - 1;
  }
  return abs(mv - index * STEP_MV) <= TOLERANCE_MV ? index : -1;
}

// The latest instant at or before utc when local time was hour:minute:00.
// tm_isdst = -1 lets mktime() work out whether DST applies on that day.
time_t latestOccurrence(time_t utc, int hour, int minute) {
  for (int daysBack = 0;; daysBack++) {
    struct tm local;
    localtime_r(&utc, &local);
    local.tm_mday -= daysBack;  // mktime() normalises day 0 or below
    local.tm_hour = hour;
    local.tm_min = minute;
    local.tm_sec = 0;
    local.tm_isdst = -1;
    time_t occurrence = mktime(&local);
    if (occurrence <= utc) {
      return occurrence;
    }
  }
}

}  // namespace

void Alarm::init() {
  pinMode(HOUR_LADDER_PIN, INPUT);
  pinMode(MINUTE_LADDER_PIN, INPUT);
}

bool Alarm::readSetTime(uint8_t& hour, uint8_t& minute) {
  int hourMv = readMillivolts(HOUR_LADDER_PIN);
  int minuteMv = readMillivolts(MINUTE_LADDER_PIN);

  int hourIndex = toPosition(hourMv);
  int minuteIndex = toPosition(minuteMv);

  if (hourIndex < 0 || minuteIndex < 0) {
    Serial.printf("Alarm: switch reading not matched (hour %d mV, minute %d mV)\n", hourMv, minuteMv);
    return false;
  }

  hour = hourIndex;
  minute = minuteIndex * 5;
  return true;
}

bool Alarm::isDue(time_t utc) {
  uint8_t hour, minute;
  if (!readSetTime(hour, minute)) {
    return false;
  }

  time_t last = lastWakeUtc;
  lastWakeUtc = utc;

  if (last == 0 || (hour == 0 && minute == 0)) {
    return false;
  }

  time_t alarm = latestOccurrence(utc, hour, minute);
  return last < alarm && alarm <= utc;
}
