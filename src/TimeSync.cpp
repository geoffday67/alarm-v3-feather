#include "TimeSync.h"

#include <Arduino.h>
#include <time.h>

#include "Clock.h"
#include "Wireless.h"

namespace {

// What schedules the sync:
//   true  - daily at SYNC_HOUR_UTC:00 UTC. Normal use.
//   false - every TEST_SYNC_PERIOD_S, on multiples of it since the Unix
//           epoch. For testing.
constexpr bool SYNC_DAILY = true;
constexpr time_t TEST_SYNC_PERIOD_S = 5 * 60;

// UTC, like every time inside the firmware: 03:00 GMT in winter, 04:00 BST
// in summer. Either way it's clear of the UK clock changes (01:00 UTC).
constexpr int SYNC_HOUR_UTC = 3;

constexpr time_t SYNC_PERIOD_S = SYNC_DAILY ? 24 * 60 * 60 : TEST_SYNC_PERIOD_S;
constexpr time_t SYNC_OFFSET_S = SYNC_DAILY ? SYNC_HOUR_UTC * 60 * 60 : 0;

// After a failed sync (no network, no NTP reply) it's retried on each of the
// next MAX_RETRIES ticks. If those all fail too it waits for the next
// scheduled time: the RTC keeps good enough time on its own for another day.
constexpr int MAX_RETRIES = 10;

// A successful sync finishing up to this long before a scheduled time counts
// as that time's sync. Covers the scheduled sync itself when the RTC was
// running fast (the tick fires early, and the corrected time lands just
// before the scheduled time), and a cold boot just before it.
constexpr time_t SLOT_TOLERANCE_S = 60;
static_assert(SLOT_TOLERANCE_S < SYNC_PERIOD_S, "SLOT_TOLERANCE_S must be less than the sync period");

// UTC at or after which syncIfDue() syncs. Survives deep sleep; set by
// powerOnInit() on every cold boot before it's first read.
RTC_DATA_ATTR time_t nextSyncUtc = 0;

// Retries left after the current scheduled sync fails. Survives deep sleep;
// starts at MAX_RETRIES on a cold boot (RTC_DATA_ATTR variables are reloaded
// then) and is reset to it whenever a new scheduled time is set.
RTC_DATA_ATTR int retriesLeft = MAX_RETRIES;

// UTC of the last successful sync, or 0 if none since the last cold boot
// (RTC_DATA_ATTR variables are reloaded then). Survives deep sleep.
RTC_DATA_ATTR time_t lastSuccessUtc = 0;

// The first scheduled sync time strictly after t. t is never before 2000
// (the RTC's year is 0-99 from 2000), so the division is of a positive
// number and truncation is floor.
time_t firstSyncTimeAfter(time_t t) {
  return ((t - SYNC_OFFSET_S) / SYNC_PERIOD_S + 1) * SYNC_PERIOD_S + SYNC_OFFSET_S;
}

void logUtc(const char* label, time_t t) {
  struct tm utc;
  gmtime_r(&t, &utc);
  Serial.printf("%s %04d-%02d-%02d %02d:%02d:%02d UTC\n", label,
                utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                utc.tm_hour, utc.tm_min, utc.tm_sec);
}

// Next sync at the first scheduled time after t, with a full set of retries.
void scheduleAfter(time_t t) {
  nextSyncUtc = firstSyncTimeAfter(t);
  retriesLeft = MAX_RETRIES;
}

// Connects, syncs, disconnects, then schedules the next attempt from the RTC
// time afterwards (the new time if the sync worked).
bool syncNow() {
  bool synced = false;
  if (Wireless::connectWiFi()) {
    synced = Clock::syncNetworkTime();
  }
  Wireless::disconnectWiFi();

  // If the RTC still isn't valid (a failed sync after power loss), its time
  // is arbitrary but it's still counting seconds, so scheduling from it
  // still gives the next attempt roughly a day later.
  time_t utc;
  Clock::readUtc(utc);

  if (synced) {
    lastSuccessUtc = utc;
    scheduleAfter(utc + SLOT_TOLERANCE_S);
    logUtc("Time sync done, next at", nextSyncUtc);
  } else if (retriesLeft > 0) {
    // Already due, so the next tick (whose time is later) tries again.
    nextSyncUtc = utc;
    Serial.printf("Time sync failed, retry %d of %d at the next tick\n", MAX_RETRIES - retriesLeft + 1,
                  MAX_RETRIES);
    retriesLeft--;
  } else {
    scheduleAfter(utc);
    logUtc("Time sync failed, retries used up, next at", nextSyncUtc);
  }
  return synced;
}

}  // namespace

void TimeSync::init() {
}

void TimeSync::powerOnInit() {
  syncNow();
}

uint64_t TimeSync::prepareForSleep() {
  return 0;
}

bool TimeSync::syncIfDue(time_t utc) {
  if (utc < nextSyncUtc) {
    return false;
  }
  return syncNow();
}

void TimeSync::logLastSync() {
  if (lastSuccessUtc == 0) {
    Serial.println("Last sync: none since power-on");
    return;
  }
  logUtc("Last sync:", lastSuccessUtc);
}
