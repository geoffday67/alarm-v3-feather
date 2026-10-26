#include <Arduino.h>

#include "Alarm.h"
#include "AlarmSequence.h"
#include "Battery.h"
#include "Button.h"
#include "Clock.h"
#include "Constants.h"
#include "Display.h"
#include "I2CBus.h"
#include "Music.h"
#include "Sleep.h"
#include "TimeSync.h"
#include "Utils.h"
#include "esp_sleep.h"

#if INCLUDE_vTaskSuspend != 1
#error "INCLUDE_vTaskSuspend is not 1 -- portMAX_DELAY will NOT block indefinitely"
#endif

// Cold boot only (power-on, reset, reflash). Runs after Display::init(),
// which powerOnInit() needs, but before Clock::init() and TopButton::init():
// the clock task mustn't do a partial refresh before Display::powerOnInit()
// has seeded the panel, or call Display::showTime() concurrently with the
// one below.
void powerOnInit() {
  Display::powerOnInit();
  Serial.println("Display power-on initialised");

  Clock::powerOnInit();
  Serial.println("Clock power-on initialised");

  Battery::powerOnInit();
  Serial.println("Battery power-on initialised");

  TopButton::powerOnInit();
  Serial.println("Top button power-on initialised");

  // After Clock::powerOnInit(): the sync re-arms the RTC tick.
  TimeSync::powerOnInit();
  Serial.println("Time sync power-on initialised");

  time_t utc;
  if (!Clock::readUtc(utc)) {
    Serial.println("RTC time not valid (VL set)");
  }
  int batteryPercent = Battery::readPercent();
  Display::showTime(utc, batteryPercent);
  TimeSync::logLastSync();
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting");

  // Local time zone for display. The RTC and all internal times are UTC.
  // TZ lives in RAM, so it's set on every boot including deep-sleep wakes.
  setenv("TZ", TIMEZONE_POSIX, 1);
  tzset();

  Utils::init();
  Serial.println("Utils initialised");

  Sleep::init();
  Serial.println("Sleep management initialised");

  // IO20 (I2C_PWR / NEOPIXEL_I2C_POWER) enables the onboard regulator that
  // feeds the board's own SDA/SCL pull-up resistors (and the NeoPixel and
  // STEMMA QT connector). The Arduino IDE's Adafruit board package does this
  // automatically via initVariant().
  I2CBus::begin(I2C_SDA_PIN, I2C_SCL_PIN, 100000);
  Serial.println("I2C bus initialised");

  // Display must be set up before powerOnInit() and Clock::init(), which
  // both use the panel.
  Display::init();
  Serial.println("Display initialised");

  Battery::init();
  Serial.println("Battery initialised");

  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1) {
    // Woken by one (or both) of the external pins. Logged only; each
    // module's init() handles its own pending interrupt.
    uint64_t wakePins = esp_sleep_get_ext1_wakeup_status();
    if (Clock::isWakeSource(wakePins)) {
      Serial.println("Woken by the clock");
    }
    if (wakePins & (1ULL << TOP_SWITCH_PIN)) {
      Serial.println("Woken by the button");
    }
  } else {
    // Power-on, reset, reflash etc.: configure external hardware once.
    powerOnInit();
  }

  // Every boot: set up each module's tasks and interrupts. Music and the
  // alarm sequence first: the clock and button tasks may call them as soon
  // as their interrupts are attached (e.g. the press that woke us).
  Music::init();
  Serial.println("Music initialised");

  AlarmSequence::init();
  Serial.println("Alarm sequence initialised");

  Clock::init();
  Serial.println("Clock initialised");

  TopButton::init();
  Serial.println("Top button initialised");

  Alarm::init();
  Serial.println("Alarm initialised");

  TimeSync::init();
  Serial.println("Time sync initialised");

  // Block until nothing is holding off sleep (e.g. the RTC tick that woke us
  // has been handled, TF cleared and the display updated).
  Sleep::waitUntilAllowed();

  Serial.printf("Sleeping\n\n\n");
  Serial.flush();

  // Every module gets its pins ready for deep sleep and returns the ext1
  // mask bits of any wake sources it owns (0 if none). All wake sources are
  // active-low.
  uint64_t wakeMask = 0;
  wakeMask |= Display::prepareForSleep();
  wakeMask |= Clock::prepareForSleep();
  wakeMask |= Battery::prepareForSleep();
  wakeMask |= TopButton::prepareForSleep();
  wakeMask |= TimeSync::prepareForSleep();
  esp_sleep_enable_ext1_wakeup(wakeMask, ESP_EXT1_WAKEUP_ANY_LOW);

  esp_deep_sleep_start();
}

// Never reached: setup() always ends in deep sleep, and each wake is a fresh
// boot into setup().
void loop() {
}
