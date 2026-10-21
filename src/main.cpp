#include <Arduino.h>
#include <LittleFS.h>

#include "Alarm.h"
#include "Button.h"
#include "Clock.h"
#include "Constants.h"
#include "Display.h"
#include "Display_EPD_W21.h"
#include "Display_EPD_W21_spi.h"
#include "EventManager.h"
#include "I2CBus.h"
#include "Music.h"
#include "Sleep.h"
#include "Utils.h"
#include "Wireless.h"
#include "esp_partition.h"
#include "esp_sleep.h"

#if INCLUDE_vTaskSuspend != 1
#error "INCLUDE_vTaskSuspend is not 1 -- portMAX_DELAY will NOT block indefinitely"
#endif

// Same as startPartialUpdate() in Display.cpp, but targets an arbitrary sub-window
// instead of the whole frame, mirroring EPD_Dis_Part()'s window setup.
// part_column = native Y extent (no alignment needed), part_line = native X
// extent (must be a multiple of 8, same as x_start).
void epdStartPartialUpdateWindowed(unsigned int x_start, unsigned int y_start, unsigned int part_column,
                                   unsigned int part_line, const uint8_t* data) {
  unsigned int xs = x_start / 8;
  unsigned int xe = xs + part_line / 8 - 1;
  unsigned int ys = y_start;
  unsigned int ye = ys + part_column - 1;

  EPD_W21_RST_0;
  delay(10);
  EPD_W21_RST_1;
  delay(10);
  EPD_W21_WriteCMD(0x3C);  // BorderWaveform
  EPD_W21_WriteDATA(0x80);

  EPD_W21_WriteCMD(0x44);  // RAM x address start/end
  EPD_W21_WriteDATA(xs);
  EPD_W21_WriteDATA(xe);
  EPD_W21_WriteCMD(0x45);  // RAM y address start/end
  EPD_W21_WriteDATA(ys % 256);
  EPD_W21_WriteDATA(ys / 256);
  EPD_W21_WriteDATA(ye % 256);
  EPD_W21_WriteDATA(ye / 256);

  EPD_W21_WriteCMD(0x4E);  // RAM x address counter
  EPD_W21_WriteDATA(xs);
  EPD_W21_WriteCMD(0x4F);  // RAM y address counter
  EPD_W21_WriteDATA(ys % 256);
  EPD_W21_WriteDATA(ys / 256);

  EPD_W21_WriteCMD(0x24);  // write B/W image to RAM
  unsigned int count = part_column * part_line / 8;
  for (unsigned int i = 0; i < count; i++) {
    EPD_W21_WriteDATA(data[i]);
  }

  EPD_W21_WriteCMD(0x22);  // Display Update Control
  EPD_W21_WriteDATA(0xFF);
  EPD_W21_WriteCMD(0x20);  // Activate Display Update Sequence
}

// Blocks until BUSY clears (fine here - this is a one-off diagnostic, not
// the production non-blocking path) and returns how long that took.
unsigned long waitBusyTimed() {
  unsigned long start = micros();
  while (isEPD_W21_BUSY) {
  }
  return micros() - start;
}

void runPartialUpdateTimingTest() {
  static uint8_t testBuf[EPD_ARRAY];
  memset(testBuf, 0x00, sizeof(testBuf));  // content doesn't matter, only size

  struct TestCase {
    const char* label;
    unsigned int partColumn;  // native Y extent (rows)
    unsigned int partLine;    // native X extent (must be multiple of 8)
  };
  TestCase cases[] = {
      {"Full frame (296 x 128)", EPD_HEIGHT, EPD_WIDTH},
      {"Short + full width (32 x 128)", 32, EPD_WIDTH},
      {"Full height + narrow (296 x 32)", EPD_HEIGHT, 32},
      {"Arrow-sized (32 x 32)", 32, 32},
  };

  Serial.println("=== Partial update timing test ===");
  for (auto& tc : cases) {
    epdStartPartialUpdateWindowed(0, 0, tc.partColumn, tc.partLine, testBuf);
    unsigned long busyUs = waitBusyTimed();
    EPD_DeepSleep();
    Serial.printf("%-32s -> BUSY for %.1f ms\n", tc.label, busyUs / 1000.0);
    delay(1000);
  }
  Serial.println("=== Timing test complete ===");
}

void showDiagnostics() {
  const esp_partition_t* p = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, NULL);
  if (p) {
    Serial.printf("App partition: %d KB, sketch uses %d KB\n", p->size / 1024, ESP.getSketchSize() / 1024);
  }

  Serial.printf("LittleFS: %d KB used of %d KB\n",
                LittleFS.usedBytes() / 1024, LittleFS.totalBytes() / 1024);

  File root = LittleFS.open("/");
  File file = root.openNextFile();
  if (!file) {
    Serial.println("LittleFS: no files");
    return;
  }
  while (file) {
    Serial.printf("  %s (%d bytes)\n", file.name(), file.size());
    file = root.openNextFile();
  }
}

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

  TopButton::powerOnInit();
  Serial.println("Top button power-on initialised");

  // Connect WiFi and get the time via NTP.
  if (Wireless::connectWiFi()) {
    Clock::syncNetworkTime();
  }
  Wireless::disconnectWiFi();

  time_t utc;
  if (!Clock::readUtc(utc)) {
    Serial.println("RTC time not valid (VL set)");
  }
  Display::showTime(utc);
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

  // Every boot: set up each module's tasks and interrupts. Music first: the
  // clock and button tasks may call it as soon as their interrupts are
  // attached (e.g. the press that woke us).
  Music::init();
  Serial.println("Music initialised");

  Clock::init();
  Serial.println("Clock initialised");

  TopButton::init();
  Serial.println("Top button initialised");

  Alarm::init();
  Serial.println("Alarm initialised");

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
  wakeMask |= TopButton::prepareForSleep();
  esp_sleep_enable_ext1_wakeup(wakeMask, ESP_EXT1_WAKEUP_ANY_LOW);

  esp_deep_sleep_start();

  if (!LittleFS.begin(true, "/littlefs", 10, "littlefs")) {
    Serial.println("LittleFS mount failed");
    return;
  }
  Serial.println("LittleFS initialised");

  showDiagnostics();
}

void loop() {
  return;

  // Section for testing I2C link to the PIC16F15225.
  // if (millis() - lastI2CCommandSent >= 2000) {
  //   lastI2CCommandSent = millis();
  //   uint8_t command = nextI2CCommandIsLightOn ? I2C_COMMAND_EINK_LIGHT_ON : I2C_COMMAND_EINK_LIGHT_OFF;
  //   Wire.beginTransmission(PIC_I2C_ADDRESS);
  //   Wire.write(command);
  //   uint8_t result = Wire.endTransmission();
  //   if (result == 0) {
  //     Serial.printf("I2C: sent light %s\n", nextI2CCommandIsLightOn ? "ON" : "OFF");
  //   } else {
  //     Serial.printf("I2C: send failed, endTransmission() = %d\n", result);
  //   }
  //   nextI2CCommandIsLightOn = !nextI2CCommandIsLightOn;
  // }

  // Testing Moomin light.
  // Wire.beginTransmission(PIC_I2C_ADDRESS);
  // Wire.write(I2C_COMMAND_PULSE_MOOMIN);
  // uint8_t result = Wire.endTransmission();
  // if (result == 0) {
  //   Serial.printf("I2C: sent Moomin pulse\n");
  // } else {
  //   Serial.printf("I2C: send Moomin pulse failed, endTransmission() = %d\n", result);
  // }
  // delay(2000);

  // uint8_t alarmHour, alarmMinute;
  // if (Alarm::readSetTime(alarmHour, alarmMinute)) {
  //   Serial.printf("Alarm set to %02u:%02u\n", alarmHour, alarmMinute);
  // }
  // delay(1000);

  EventManager.processEvents();
}