#include "Clock.h"

#include <Arduino.h>
#include <NetworkUdp.h>
#include <time.h>

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

// NTP is done with a single UDP exchange rather than the IDF/lwIP SNTP client
// (configTime()), which in the IDF defaults waits a random 0-5 s before its
// first request (CONFIG_LWIP_SNTP_STARTUP_DELAY / _MAXIMUM_STARTUP_DELAY) and
// retries only after lwIP's 15 s SNTP_RECV_TIMEOUT. Here the request goes out
// immediately, with a short timeout and quick retries, and the reply's
// timestamps are used directly - with round-trip compensation - to time the
// RTC write.
constexpr const char* NTP_SERVER = "pool.ntp.org";
constexpr uint16_t NTP_SERVER_PORT = 123;
constexpr uint16_t NTP_LOCAL_PORT = 2390;
constexpr int NTP_ATTEMPTS = 3;
constexpr int64_t NTP_REPLY_TIMEOUT_US = 1000000;
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

// One request/reply. On success returns the server's time (us since 1900)
// at the local esp_timer instant localUs, corrected for half the network
// round trip, and the round trip itself.
bool queryNtp(int64_t& serverUs, int64_t& localUs, int64_t& roundTripUs) {
  NetworkUDP udp;
  if (!udp.begin(NTP_LOCAL_PORT)) {
    Serial.println("NTP: UDP begin failed");
    return false;
  }

  // Client request: LI = 0, VN = 4, Mode = 3. A random transmit timestamp
  // acts as a nonce - the server echoes it as the reply's originate
  // timestamp, so a stale or spoofed reply can be rejected.
  uint8_t pkt[NTP_PACKET_SIZE] = {0};
  pkt[0] = 0x23;
  uint32_t nonceHi = esp_random();
  uint32_t nonceLo = esp_random();
  writeBE32(pkt + 40, nonceHi);
  writeBE32(pkt + 44, nonceLo);

  if (!udp.beginPacket(NTP_SERVER, NTP_SERVER_PORT)) {
    Serial.println("NTP: DNS lookup or socket failed");
    udp.stop();
    return false;
  }
  udp.write(pkt, NTP_PACKET_SIZE);
  if (!udp.endPacket()) {
    Serial.println("NTP: send failed");
    udp.stop();
    return false;
  }
  int64_t t1 = esp_timer_get_time();

  // parsePacket() is non-blocking (recvfrom with MSG_DONTWAIT). Polling every
  // 1 ms tick adds up to 1 ms of uncertainty to t4.
  while (esp_timer_get_time() - t1 < NTP_REPLY_TIMEOUT_US) {
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

    bool valid = (r[0] & 0x07) == 4                // Mode = server
                 && (r[0] >> 6) != 3               // LI 3 = server clock not synchronised
                 && r[1] >= 1 && r[1] <= 15        // stratum (0 = kiss-o'-death)
                 && readBE32(r + 24) == nonceHi    // originate = our transmit
                 && readBE32(r + 28) == nonceLo;
    if (!valid) {
      Serial.println("NTP: invalid reply ignored");
      continue;
    }

    int64_t t2 = ntpToUs(r + 32);  // server receive
    int64_t t3 = ntpToUs(r + 40);  // server transmit
    roundTripUs = (t4 - t1) - (t3 - t2);
    serverUs = t3 + roundTripUs / 2;
    localUs = t4;
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
    uint8_t year, month, day, weekday, hour, minute, second;
    if (!pcf8563ReadDateTime(year, month, day, weekday, hour, minute, second)) {
      Serial.println("RTC time not valid (VL set)");
    }

    // Flash the LED.
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
    digitalWrite(LED_BUILTIN, LOW);

    // Clear the timer flag, this makes the pin high again.
    pcf8563ClearTimerFlag();

    // Enable the interrupt again as we've reset the interrupting pin.
    enableInterrupt(RTC_INTERRUPT_PIN);

    // Blocks this task until the panel has finished (~0.3 s). Display holds
    // its own sleep inhibit, so this would be safe even outside the one
    // taken in clockHandler(). A tick arriving meanwhile is handled on the
    // next loop.
    Display::showTime(hour, minute, second);

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

void Clock::init() {
  bit = Utils::getNextEventBit();
  Utils::startTask(clockTask, "clock");

  if (pcf8563Init(/*debug*/ true)) {
    Serial.println("RTC module initialised");
  } else {
    Serial.println("RTC MODULE NOT FOUND!!");
  }

  // Release the pin from LP control so we can use it.
  rtc_gpio_deinit((gpio_num_t)RTC_INTERRUPT_PIN);
  pinMode(RTC_INTERRUPT_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(RTC_INTERRUPT_PIN), clockHandler, ONLOW);

  pinMode(LED_BUILTIN, OUTPUT);
}

bool Clock::syncNetworkTime() {
  int64_t serverUs = 0, localUs = 0, roundTripUs = 0;
  bool ok = false;
  for (int attempt = 0; attempt < NTP_ATTEMPTS && !ok; attempt++) {
    ok = queryNtp(serverUs, localUs, roundTripUs);
  }
  if (!ok) {
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
                     utc.tm_wday,        // both 0 = Sunday
                     utc.tm_hour, utc.tm_min, utc.tm_sec);
  int64_t writeUs = esp_timer_get_time() - writeStart;

  // Logged to check against RTC_WRITE_US.
  Serial.printf("RTC set from NTP: %04d-%02d-%02d %02d:%02d:%02d UTC "
                "(round trip %lld us, RTC write took %lld us)\n",
                utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                utc.tm_hour, utc.tm_min, utc.tm_sec, roundTripUs, writeUs);
  return true;
}
