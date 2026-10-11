#pragma once

#include <time.h>

namespace Clock {
void init();

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
