#pragma once

#include <stdint.h>
#include <time.h>

// Keeps the PCF8563 set from NTP: once at cold boot, then daily at a fixed
// UTC time (SYNC_HOUR_UTC in TimeSync.cpp). A failed sync is retried on
// each of the next 10 ticks (MAX_RETRIES), then left until the next
// scheduled time. Owns the schedule and the WiFi connect/disconnect around each
// sync; the NTP exchange and RTC write are Clock::syncNetworkTime().
//
// The schedule (next sync time and retries left) is kept in RTC memory
// across deep sleep.
namespace TimeSync {
// Call on every boot, including each deep-sleep wake. Nothing to do at
// present.
void init();

// Call once on a cold boot (power-on, reset, reflash), after
// Clock::powerOnInit(). Syncs straight away and schedules the next sync.
// Blocks for the WiFi connect and NTP exchange (see syncIfDue()).
void powerOnInit();

// Nothing to prepare: WiFi is always off again by the time a sync returns.
// Returns 0, as there are no wake sources (the return value is the ext1
// wake mask, as for the other modules' prepareForSleep()).
uint64_t prepareForSleep();

// Call once per RTC tick from the clock task, with the time just read from
// the RTC (valid or not). If a sync is due, connects WiFi, sets the RTC from
// NTP, powers WiFi down and schedules the next one; otherwise returns at
// once. Returns true if the RTC was set. Blocks while syncing: WiFi scan and
// connect (up to ~5 s after the scan) plus Clock::syncNetworkTime() (up to
// ~6 s). Must only be called from the clock task, the only task that uses
// the RTC once tasks are running.
bool syncIfDue(time_t utc);

// Writes the last successful sync (the RTC time just after it was set, in
// UTC) to the serial log, or that there's been none since the last cold
// boot. Kept in RTC memory across deep sleep. Called alongside each
// Display::showTime() so the log shows it with every displayed time.
void logLastSync();
}  // namespace TimeSync
