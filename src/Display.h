#pragma once

#include <stdint.h>
#include <time.h>

// GDEY029T94-FL03 (SSD1680) e-paper panel showing the time.
//
// Not thread-safe: only one task may call showTime() at a time. On a cold
// boot setup() calls powerOnInit() and its own showTime() before
// Clock::init() starts the clock task; from then on only the clock task
// calls it.
namespace Display {

// Configures the panel's GPIOs and SPI. Call on every boot, including each
// deep-sleep wake, before powerOnInit() or any showTime(). Releases the pad
// holds set by prepareForSleep() without glitching the pins. Doesn't talk to
// the panel.
void init();

// Call once on a cold boot (power-on, reset, reflash), after init() and
// before any showTime(). Full panel init and a full refresh to a blank
// screen, which seeds both RAM planes so partial
// refreshes work from then on. Must also be repeated if the panel ever loses
// power. Blocks the caller (~2 s), spinning on BUSY inside the vendor driver.
void powerOnInit();

// Drives the panel's interface inputs to fixed levels and holds them through
// the chip's deep sleep (RES# and CS# high, SCK/SDI/D/C low). Otherwise those
// pins float once the HP domain powers down: a floating RES# can reset the
// SSD1680 out of its deep sleep, and floating CMOS inputs draw through-current.
// Call just before esp_deep_sleep_start(), with no update in progress.
// Returns 0: the display has no wake sources (the return value is the ext1
// wake mask, as for the other modules' prepareForSleep()).
uint64_t prepareForSleep();

// Converts utc to local time (using the TZ set in setup()), renders it as
// "HH:MM:SS", plus a low-battery message below it if lowBattery is set,
// and refreshes the panel, then puts the panel into its own deep sleep.
// Holds a Sleep inhibit for the duration, so the chip can't go into deep
// sleep mid-update.
//
// Always a fast partial refresh. Relies on the init and RAM contents left by
// powerOnInit() or the previous update, which survive the ESP32's deep sleep
// because the panel is left in its RAM-retaining deep sleep (see
// prepareForSleep()). Blocks the caller until the panel finishes, but polls
// BUSY with vTaskDelay(), so other tasks keep running.
void showTime(time_t utc, bool lowBattery);

// Turns the eInk front light on. The PIC turns it off itself after its own
// on-time, so this returns as soon as the command is sent.
void showFrontLight();
}  // namespace Display
