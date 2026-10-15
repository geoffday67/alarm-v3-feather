#pragma once

#include <stdint.h>
#include <time.h>

// The only interface to the PCF8563 RTC: nothing outside Clock.cpp uses
// rtc/pcf8563_timer.h or RTC_INTERRUPT_PIN directly.
namespace Clock {
// Call on every boot, including each deep-sleep wake (after powerOnInit()
// on a cold boot). Sets up the ESP32 side only: starts the clock task and
// attaches the INT interrupt. If INT is already low (the tick that woke us),
// the interrupt fires at once and the task updates the display. Does not
// touch the PCF8563's configuration. Requires Display::init().
void init();

// Call once on a cold boot (power-on, reset, reflash), before init().
// Configures the PCF8563: CLKOUT off and the periodic timer interrupt
// started, which also clears any pending tick. Doesn't set the time - see
// syncNetworkTime().
void powerOnInit();

// True if the PCF8563 timer interrupt (INT on RTC_INTERRUPT_PIN) was one of
// the ext1 wake sources. wakePins is esp_sleep_get_ext1_wakeup_status().
bool isWakeSource(uint64_t wakePins);

// Detaches the INT interrupt and hands RTC_INTERRUPT_PIN back to the LP IO
// with its pull-up enabled, so it can wake the chip from deep sleep. Returns
// the pin's ext1 wake mask bit for esp_sleep_enable_ext1_wakeup(). Call just
// before deep sleep, with nothing still holding a Sleep inhibit.
uint64_t prepareForSleep();

// Reads the PCF8563 (which holds UTC) and returns it as Unix time. Returns
// false if the VL flag is set, i.e. the time can't be trusted; utc is still
// filled from whatever the registers hold.
bool readUtc(time_t& utc);

// Gets UTC from NTP (direct UDP request, not the SNTP client) and writes it
// to the PCF8563, which also clears its VL flag. WiFi must already be
// connected. Blocks for up to 3 x 1 s waiting for a reply, plus up to 1 s to
// align the RTC write to the second. Returns false if no valid reply arrived
// (RTC left unchanged).
bool syncNetworkTime();
}
