#pragma once

#include <stdint.h>

// PCF8563 7-bit I2C address
#define PCF8563_ADDR          0x51

// Register addresses (see PCF8563 datasheet, Table 4)
#define PCF8563_REG_CTRL1     0x00
#define PCF8563_REG_CTRL2     0x01
#define PCF8563_REG_CLKOUT    0x0D
#define PCF8563_REG_TMR_CTRL  0x0E
#define PCF8563_REG_TIMER     0x0F

// Control_status_2 (0x01) bit positions — Table 6
#define PCF8563_BIT_TI_TP     (1 << 4)  // 0 = INT level-active with TF, 1 = pulsed
#define PCF8563_BIT_AF        (1 << 3)  // alarm flag
#define PCF8563_BIT_TF        (1 << 2)  // timer flag
#define PCF8563_BIT_AIE       (1 << 1)  // alarm interrupt enable
#define PCF8563_BIT_TIE       (1 << 0)  // timer interrupt enable

// Timer_control (0x0E) bit positions — Table 23
#define PCF8563_BIT_TE        (1 << 7)  // timer enable
#define PCF8563_TD_4096HZ     0x00
#define PCF8563_TD_64HZ       0x01
#define PCF8563_TD_1HZ        0x02
#define PCF8563_TD_1_60HZ     0x03

// Time/date register addresses — Table 4
#define PCF8563_REG_VL_SECONDS   0x02
#define PCF8563_REG_MINUTES      0x03
#define PCF8563_REG_HOURS        0x04
#define PCF8563_REG_DAYS         0x05
#define PCF8563_REG_WEEKDAYS     0x06
#define PCF8563_REG_CENT_MONTHS  0x07
#define PCF8563_REG_YEARS        0x08
#define PCF8563_REG_MIN_ALARM    0x09
#define PCF8563_REG_HOUR_ALARM   0x0A
#define PCF8563_REG_DAY_ALARM    0x0B
#define PCF8563_REG_WEEKDAY_ALARM 0x0C

// Alarm registers (0x09-0x0C) bit 7 — Tables 18-21
#define PCF8563_BIT_AE        (1 << 7)  // 1 = field ignored in the alarm match (power-on default)

// Control_status_1 (0x00) bit positions — Table 5
#define PCF8563_BIT_STOP      (1 << 5)

// CLKOUT_control (0x0D) bit positions — Table 22
#define PCF8563_BIT_FE        (1 << 7)  // 1 = CLKOUT active (power-on default)

// One-time setup. The PCF8563 keeps its registers for as long as it's
// powered, through the ESP32's deep sleep, so this is called on the ESP32's
// cold boot only (Clock::powerOnInit()).
// Requires Wire.begin() and IO20 high (bus pull-ups on VSENSOR).
//   1. Checks the RTC is present (pcf8563IsPresent); returns false if not.
//   2. Clears STOP/TESTC in Control_status_1 if either is set.
//   3. Disables CLKOUT (power-on default is 32.768 kHz, ~0.3 uA extra).
// Doesn't start a tick interrupt (pcf8563StartMinuteAlarmInterrupt() or
// pcf8563StartTimerInterrupt()), check VL (pcf8563IsTimeValid()) or set the
// time.
bool pcf8563Configure();

// ---- Tick interrupts ----
// Two alternative sources for a periodic INT; each start function disables
// the other. Either way INT is level-active (low) and stays asserted until
// the flag that raised it is cleared, as ESP32-C6 LP GPIO deep-sleep wakeup
// requires. Clear the flag after handling each tick, before going back to
// sleep, or the wakeup fires again immediately.

// Asserts INT at the start of every minute (hh:mm:00) using the minute
// alarm. AF is set only when the minutes counter increments to match
// Minute_alarm (datasheet 8.6.5), so the tick is aligned with the minute
// rollover; the countdown timer's source clocks have no documented phase
// relationship to the time counters, so the timer can't guarantee this.
// The alarm matches one minute value, so it must be moved on every tick:
// pcf8563ClearAlarmFlag() then pcf8563ArmMinuteAlarm(). Hour, day and
// weekday alarms are disabled and the countdown timer is stopped. Clears
// AF and TF.
void pcf8563StartMinuteAlarmInterrupt();

// Sets Minute_alarm to the minute after the current one. Doesn't touch AF.
// If the minute rolls over during the update the alarm is set again from
// the new minute: an alarm left equal to the current minute would wait an
// hour, since it only fires on an increment to a match. Call after each
// pcf8563ClearAlarmFlag(), and after pcf8563SetDateTime().
void pcf8563ArmMinuteAlarm();

// Clears the alarm flag (de-asserting INT in minute-alarm mode), leaving TF
// unchanged.
void pcf8563ClearAlarmFlag();

// Asserts INT every periodSeconds (1-255) from the countdown timer (1 Hz
// source, autoreloading). Free-running from when this is called, so not
// aligned to the minute: intended for testing. All alarm matches are
// disabled. Clears AF and TF.
void pcf8563StartTimerInterrupt(uint8_t periodSeconds);

// Clears the timer flag (de-asserting INT in timer mode), leaving AF
// unchanged.
void pcf8563ClearTimerFlag();

// Low-level register access, exposed in case other code needs it
// (e.g. reading VL_seconds, setting the time registers).
void pcf8563WriteReg(uint8_t reg, uint8_t value);
uint8_t pcf8563ReadReg(uint8_t reg);

// Returns true if the PCF8563 ACKs its address and a test value written
// to the (unused) Minute_alarm register reads back correctly. The chip
// has no ID/serial register, so this round trip is the liveness check.
// Leaves the minute alarm disabled (AE_M = 1, its power-on state).
// Requires Wire.begin() and IO20 high (bus pull-ups on VSENSOR).
bool pcf8563IsPresent();

// Returns false if the VL flag is set, i.e. the RTC's time cannot be
// trusted (first power-up, or a power interruption since the last
// time-set). Call this at boot; if false, block on an NTP sync before
// relying on the RTC.
bool pcf8563IsTimeValid();

// Sets the date/time and clears the VL flag, using the datasheet's
// STOP-bit sequence so the multi-byte write can't be corrupted by a
// rollover mid-write. Call this once at first boot, whenever
// pcf8563IsTimeValid() returns false, and after each NTP resync.
// weekday: 0 = Sunday ... 6 = Saturday. year: 0-99 (2000-2099).
void pcf8563SetDateTime(uint8_t year, uint8_t month, uint8_t day,
                         uint8_t weekday, uint8_t hour, uint8_t minute,
                         uint8_t second);

// Reads the current date/time via a single auto-incrementing burst
// read (registers 0x02-0x08). Returns false if the VL flag is set,
// meaning the values read back cannot be trusted (see
// pcf8563IsTimeValid). weekday: 0 = Sunday ... 6 = Saturday.
// year: 0-99 (2000-2099).
bool pcf8563ReadDateTime(uint8_t &year, uint8_t &month, uint8_t &day,
                          uint8_t &weekday, uint8_t &hour, uint8_t &minute,
                          uint8_t &second);
