#include "Clock.h"

#include <Arduino.h>
#include <NetworkUdp.h>
#include <time.h>

#include "Alarm.h"
#include "Constants.h"
#include "Display.h"
#include "Sleep.h"
#include "Utils.h"
#include "driver/rtc_io.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "rtc/pcf8563_timer.h"

namespace {

EventBits_t bit;

// PCF8563 timer interrupt period: 60 s in normal use, 10 s while debugging.
constexpr uint8_t TICK_PERIOD_S = 10;

// NTP is done with a single UDP exchange rather than the IDF/lwIP SNTP client
// (configTime()), which in the IDF defaults waits a random 0-5 s before its
// first request (CONFIG_LWIP_SNTP_STARTUP_DELAY / _MAXIMUM_STARTUP_DELAY) and
// retries only after lwIP's 15 s SNTP_RECV_TIMEOUT. Here the request goes out
// immediately, with quick retries, and the reply's timestamps are used
// directly - with round-trip compensation - to time the RTC write.
//
// Each retry goes to a different pool hostname. lwIP's resolver caches a name
// for its DNS TTL and NetworkUDP uses only the first address returned, so
// repeated lookups of one name would keep hitting the same (possibly
// unresponsive or rate-limiting) server.
constexpr const char* NTP_SERVERS[] = {
    "0.uk.pool.ntp.org",
    "1.uk.pool.ntp.org",
    "2.uk.pool.ntp.org",
};
constexpr int NTP_ATTEMPTS = sizeof(NTP_SERVERS) / sizeof(NTP_SERVERS[0]);
constexpr uint16_t NTP_SERVER_PORT = 123;
constexpr uint16_t NTP_LOCAL_PORT = 2390;
// With no reply, the next request goes out this long after the previous one.
// All requests share one socket, so a late reply to an earlier one is still
// accepted.
constexpr int64_t NTP_RESEND_US = 1000000;
// After the last request, replies to any of them are awaited this long.
constexpr int64_t NTP_FINAL_WAIT_US = 3000000;
constexpr int NTP_PACKET_SIZE = 48;

// Seconds from the NTP epoch (1900-01-01) to the Unix epoch (1970-01-01).
constexpr int64_t NTP_TO_UNIX_S = 2208988800LL;

// The PCF8563's first seconds increment comes 0.507813-0.507935 s after STOP
// is released (datasheet 8.10, Table 26). Midpoint of that range.
constexpr int64_t RTC_FIRST_TICK_US = 507874;

// Measured duration of pcf8563SetDateTime() (average). STOP is released by
// the last of its nine register writes, so the write is started this much
// earlier.
constexpr int64_t RTC_WRITE_US = 3200;

uint32_t readBE32(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

void writeBE32(uint8_t* p, uint32_t v) {
  p[0] = v >> 24;
  p[1] = v >> 16;
  p[2] = v >> 8;
  p[3] = v;
}

// NTP 32.32 fixed-point timestamp -> microseconds since 1900. A clear top bit
// in the seconds field means NTP era 1 (from 2036-02-07), per RFC 4330 sec. 3.
int64_t ntpToUs(const uint8_t* p) {
  uint64_t secs = readBE32(p);
  uint64_t frac = readBE32(p + 4);
  if (!(secs & 0x80000000ULL)) {
    secs += 0x100000000ULL;
  }
  return (int64_t)(secs * 1000000ULL + ((frac * 1000000ULL) >> 32));
}

// Days from 1970-01-01 to the given proleptic Gregorian date (Howard
// Hinnant's days_from_civil). Used instead of timegm(), which isn't standard C.
int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);                        // 0-399
  const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;  // 0-365
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;            // 0-146096
  return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

// One outstanding client request: its server, nonce and local send time.
struct NtpRequest {
  const char* host;
  uint32_t nonceHi;
  uint32_t nonceLo;
  int64_t t1;
};

// Sends a client request to host on udp's socket, filling in req.
bool sendNtpRequest(NetworkUDP& udp, const char* host, NtpRequest& req) {
  // Client request: LI = 0, VN = 4, Mode = 3. A random transmit timestamp
  // acts as a nonce - the server echoes it as the reply's originate
  // timestamp, so a stale or spoofed reply can be rejected.
  uint8_t pkt[NTP_PACKET_SIZE] = {0};
  pkt[0] = 0x23;
  req.nonceHi = esp_random();
  req.nonceLo = esp_random();
  writeBE32(pkt + 40, req.nonceHi);
  writeBE32(pkt + 44, req.nonceLo);

  if (!udp.beginPacket(host, NTP_SERVER_PORT)) {
    Serial.printf("NTP: DNS lookup or socket failed for %s\n", host);
    return false;
  }
  udp.write(pkt, NTP_PACKET_SIZE);
  if (!udp.endPacket()) {
    Serial.printf("NTP: send to %s failed\n", host);
    return false;
  }
  req.t1 = esp_timer_get_time();
  req.host = host;
  return true;
}

// Sends up to NTP_ATTEMPTS requests, one per server, NTP_RESEND_US apart,
// stopping at the first valid reply to any of them. On success returns the
// server's time (us since 1900) at the local esp_timer instant localUs,
// corrected for half the network round trip, and the round trip itself.
bool queryNtp(int64_t& serverUs, int64_t& localUs, int64_t& roundTripUs) {
  NetworkUDP udp;
  if (!udp.begin(NTP_LOCAL_PORT)) {
    Serial.println("NTP: UDP begin failed");
    return false;
  }

  NtpRequest reqs[NTP_ATTEMPTS];
  int sent = 0;           // requests in reqs[0..sent)
  int next = 0;           // index of the next server to try
  int64_t deadline = 0;   // when to send the next request, or give up

  while (true) {
    if (esp_timer_get_time() >= deadline) {
      if (next == NTP_ATTEMPTS) {
        break;
      }
      bool ok = sendNtpRequest(udp, NTP_SERVERS[next], reqs[sent]);
      if (ok) {
        sent++;
      }
      next++;
      if (next == NTP_ATTEMPTS && sent == 0) {
        break;
      }
      // After a failed send (e.g. DNS), try the next server straight away.
      int64_t waitUs = next < NTP_ATTEMPTS ? (ok ? NTP_RESEND_US : 0) : NTP_FINAL_WAIT_US;
      deadline = esp_timer_get_time() + waitUs;
      continue;
    }

    // parsePacket() is non-blocking (recvfrom with MSG_DONTWAIT). Polling
    // every 1 ms tick adds up to 1 ms of uncertainty to t4.
    int len = udp.parsePacket();
    if (len <= 0) {
      vTaskDelay(1);
      continue;
    }
    int64_t t4 = esp_timer_get_time();

    uint8_t r[NTP_PACKET_SIZE];
    if (len < NTP_PACKET_SIZE || udp.read(r, NTP_PACKET_SIZE) != NTP_PACKET_SIZE) {
      udp.clear();  // discard the rest of a short or unreadable packet
      continue;
    }

    if (r[1] == 0) {
      // Kiss-o'-death: the reference ID holds a 4-character code, e.g. RATE.
      Serial.printf("NTP: kiss-o'-death %.4s ignored\n", (const char*)(r + 12));
      continue;
    }
    bool valid = (r[0] & 0x07) == 4   // Mode = server
                 && (r[0] >> 6) != 3  // LI 3 = server clock not synchronised
                 && r[1] <= 15;       // stratum
    if (!valid) {
      Serial.println("NTP: invalid reply ignored");
      continue;
    }

    // Match the originate timestamp against every request sent so far.
    int i = 0;
    while (i < sent && !(readBE32(r + 24) == reqs[i].nonceHi && readBE32(r + 28) == reqs[i].nonceLo)) {
      i++;
    }
    if (i == sent) {
      Serial.println("NTP: unmatched reply ignored");
      continue;
    }

    int64_t t1 = reqs[i].t1;
    int64_t t2 = ntpToUs(r + 32);  // server receive
    int64_t t3 = ntpToUs(r + 40);  // server transmit
    roundTripUs = (t4 - t1) - (t3 - t2);
    serverUs = t3 + roundTripUs / 2;
    localUs = t4;
    Serial.printf("NTP: reply to request %d of %d (%s), round trip %lld us\n", i + 1, sent, reqs[i].host, roundTripUs);
    udp.stop();
    return true;
  }

  Serial.println("NTP: no reply");
  udp.stop();
  return false;
}

void clockTask(void* pparams) {
  while (1) {
    Utils::waitFor(bit);

    // Read the time fresh from the RTC on every tick (nothing is carried
    // over between wakes).
    time_t utc;
    bool utcValid = Clock::readUtc(utc);
    if (!utcValid) {
      Serial.println("RTC time not valid (VL set)");
    }

    // Clear the timer flag, this makes the pin high again.
    pcf8563ClearTimerFlag();

    // Enable the interrupt again as we've reset the interrupting pin.
    enableInterrupt(RTC_INTERRUPT_PIN);

    // Skipped while the RTC time can't be trusted, so a bad time is never
    // recorded as the last wake.
    if (utcValid && Alarm::isDue(utc)) {
      Serial.println("Alarm due");
      // TODO: sound the alarm.
    }

    // Blocks this task until the panel has finished (~0.3 s). Display holds
    // its own sleep inhibit, so this would be safe even outside the one
    // taken in clockHandler(). A tick arriving meanwhile is handled on the
    // next loop.
    Display::showTime(utc);

    // Done. Released after re-enabling: if INT is still (or again) low the
    // ISR fires and re-inhibits. Even if the sleeping task got in first,
    // ext1 is level-triggered so the chip would wake straight back up.
    Sleep::release();
  }
}

void IRAM_ATTR clockHandler() {
  BaseType_t higherPriority = pdFALSE;

  // Stop re-triggering until the timer flag is cleared.
  disableInterrupt(RTC_INTERRUPT_PIN);

  // Hold off deep sleep until clockTask has cleared the flag.
  Sleep::inhibit();

  // Signal to the blocked task that it can continue.
  xEventGroupSetBitsFromISR(Utils::mainEventGroup, bit, &higherPriority);

  portYIELD_FROM_ISR(higherPriority);
}

}  // namespace

bool Clock::isWakeSource(uint64_t wakePins) {
  return (wakePins & (1ULL << RTC_INTERRUPT_PIN)) != 0;
}

uint64_t Clock::prepareForSleep() {
  detachInterrupt(digitalPinToInterrupt(RTC_INTERRUPT_PIN));

  // The PCF8563 INT is open-drain, active low. Its pull-up must be enabled
  // through the RTC IO driver to stay active while the HP domain is off.
  //
  // rtc_gpio_init() is needed first: after a power-on reset ESP-IDF gates the
  // LP IO clock off (LPPERI_LP_IO_CK_EN), and rtc_gpio_pullup_en() doesn't turn
  // it back on, so without this the pull-up write to LP_IO is silently lost.
  rtc_gpio_init((gpio_num_t)RTC_INTERRUPT_PIN);
  rtc_gpio_set_direction((gpio_num_t)RTC_INTERRUPT_PIN, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en((gpio_num_t)RTC_INTERRUPT_PIN);
  rtc_gpio_pulldown_dis((gpio_num_t)RTC_INTERRUPT_PIN);

  return 1ULL << RTC_INTERRUPT_PIN;
}

bool Clock::readUtc(time_t& utc) {
  uint8_t year, month, day, weekday, hour, minute, second;
  bool valid = pcf8563ReadDateTime(year, month, day, weekday, hour, minute, second);
  int64_t days = daysFromCivil(2000 + year, month, day);  // RTC year is 0-99 from 2000
  utc = (time_t)(days * 86400 + hour * 3600 + minute * 60 + second);
  return valid;
}

void Clock::powerOnInit() {
  if (pcf8563Configure(TICK_PERIOD_S)) {
    Serial.println("RTC module configured");
  } else {
    Serial.println("RTC MODULE NOT FOUND!!");
  }
}

void Clock::init() {
  bit = Utils::getNextEventBit();
  Utils::startTask(clockTask, "clock");

  // Release the pin from LP control so we can use it.
  rtc_gpio_deinit((gpio_num_t)RTC_INTERRUPT_PIN);
  pinMode(RTC_INTERRUPT_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(RTC_INTERRUPT_PIN), clockHandler, ONLOW);
}

bool Clock::syncNetworkTime() {
  int64_t serverUs = 0, localUs = 0, roundTripUs = 0;
  if (!queryNtp(serverUs, localUs, roundTripUs)) {
    Serial.println("NTP sync failed");
    return false;
  }

  // Unix time (us) at local instant localUs; the ESP32's own clock carries
  // it forward from there.
  int64_t unixUs = serverUs - NTP_TO_UNIX_S * 1000000LL;

  // Release STOP when the true time is RTC_FIRST_TICK_US before the next
  // whole second, writing the current whole second: the RTC then ticks over
  // at the same instant true time does. The write starts RTC_WRITE_US before
  // that release point.
  int64_t nowUnixUs = unixUs + (esp_timer_get_time() - localUs);
  int64_t waitUs = (1000000 - RTC_FIRST_TICK_US - RTC_WRITE_US) - (nowUnixUs % 1000000);
  if (waitUs < 0) {
    waitUs += 1000000;
  }
  int64_t targetUnixUs = nowUnixUs + waitUs;
  int64_t targetLocalUs = localUs + (targetUnixUs - unixUs);

  // Sleep most of the wait, then spin for the last 2 ms.
  int64_t remainingUs = targetLocalUs - esp_timer_get_time();
  if (remainingUs > 2000) {
    vTaskDelay(pdMS_TO_TICKS((remainingUs - 2000) / 1000));
  }
  while (esp_timer_get_time() < targetLocalUs) {
  }

  // The second current at the STOP release point.
  time_t second = (time_t)((targetUnixUs + RTC_WRITE_US) / 1000000);
  struct tm utc;
  gmtime_r(&second, &utc);

  int64_t writeStart = esp_timer_get_time();
  pcf8563SetDateTime(utc.tm_year - 100,  // tm_year is from 1900; RTC year is 0-99 from 2000
                     utc.tm_mon + 1,     // tm_mon 0-11; RTC month 1-12
                     utc.tm_mday,
                     utc.tm_wday,  // both 0 = Sunday
                     utc.tm_hour, utc.tm_min, utc.tm_sec);
  int64_t writeUs = esp_timer_get_time() - writeStart;

  // Logged to check against RTC_WRITE_US.
  Serial.printf(
      "RTC set from NTP: %04d-%02d-%02d %02d:%02d:%02d UTC "
      "(round trip %lld us, RTC write took %lld us)\n",
      utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
      utc.tm_hour, utc.tm_min, utc.tm_sec, roundTripUs, writeUs);
  return true;
}
