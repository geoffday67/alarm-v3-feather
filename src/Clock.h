#pragma once

namespace Clock {
void init();
// Gets UTC from NTP (direct UDP request, not the SNTP client) and writes it
// to the PCF8563, which also clears its VL flag. WiFi must already be
// connected. Blocks for up to 3 x 1 s waiting for a reply, plus up to 1 s to
// align the RTC write to the second. Returns false if no valid reply arrived
// (RTC left unchanged).
bool syncNetworkTime();
}
