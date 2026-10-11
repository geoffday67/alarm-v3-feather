#pragma once

#include <stdint.h>

// GDEY029T94-FL03 (SSD1680) e-paper panel showing the time.
//
// Not thread-safe: after init(), only one task may call showTime() at a
// time. On a cold boot setup() does the full refresh before Clock::init()
// starts the clock task; from then on only the clock task calls it.
namespace Display {

// Configures the panel's GPIOs and SPI. Call on every boot, including each
// deep-sleep wake, before any showTime(). Releases the pad holds set by
// prepareForSleep() without glitching the pins.
void init();

// Drives the panel's interface inputs to fixed levels and holds them through
// the chip's deep sleep (RES# and CS# high, SCK/SDI/D/C low). Otherwise those
// pins float once the HP domain powers down: a floating RES# can reset the
// SSD1680 out of its deep sleep, and floating CMOS inputs draw through-current.
// Call just before esp_deep_sleep_start(), with no update in progress.
void prepareForSleep();

// Renders "HH:MM:SS" and refreshes the panel, then puts the panel into its
// own deep sleep. Holds a Sleep inhibit for the duration, so the chip can't
// go into deep sleep mid-update.
//
// full = true: full init and refresh (seeds both RAM planes). Needed once
// after a cold boot, before any partial refresh. Blocks the caller,
// spinning on BUSY inside the vendor driver.
//
// full = false: fast partial refresh. Blocks the caller until the panel
// finishes, but polls BUSY with vTaskDelay(), so other tasks keep running.
void showTime(uint8_t hour, uint8_t minute, uint8_t second, bool full = false);

}  // namespace Display
