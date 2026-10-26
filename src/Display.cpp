#include "Display.h"

#include <Adafruit_GFX.h>
#include <Arduino.h>
#include <SPI.h>

#include "Display_EPD_W21.h"
#include "Display_EPD_W21_spi.h"
#include "Constants.h"
#include <Fonts/FreeSansBold12pt7b.h>

#include "Fonts/ArialBold50pt.h"
#include "I2CBus.h"
#include "Sleep.h"
#include "driver/gpio.h"
#include "Moomin.h"
#include "Alarm.h"
#include "Battery.h"

namespace {

// Logical (landscape) screen dimensions as seen once EPD_ROTATION is applied.
#define SCREEN_WIDTH EPD_HEIGHT
#define SCREEN_HEIGHT EPD_WIDTH

// How often to poll BUSY during a refresh, and how long to wait before
// giving up. Measured: partial ~0.57 s, full ~4.3 s (the spec's 0.3 s and
// 2 s are at 25 C; the waveforms lengthen in the cold). The timeouts are
// only a guard so a disconnected or faulty panel can't hold off deep sleep
// forever.
constexpr TickType_t BUSY_POLL_TICKS = pdMS_TO_TICKS(10);
constexpr uint32_t PARTIAL_BUSY_TIMEOUT_MS = 2000;
constexpr uint32_t FULL_BUSY_TIMEOUT_MS = 10000;

static_assert(EPD_FULL_REFRESH_EVERY >= 1, "EPD_FULL_REFRESH_EVERY must be at least 1");

// Updates since the last full refresh. Survives deep sleep; reset by the
// full refresh in powerOnInit() on every cold boot.
RTC_DATA_ATTR uint32_t updatesSinceFullRefresh = 0;

// Native 1-bit panel framebuffer (this panel has no usable grayscale mode -
// the vendor's "4 gray" trick looks poor and can't be redrawn fast enough
// for text that changes every second/minute anyway).
uint8_t epdBuffer[EPD_ARRAY];

// GFX canvas that text is rendered into, in logical landscape orientation.
// Its buffer is then copied into epdBuffer, remapped for EPD_FLIP_H/EPD_ROTATION.
GFXcanvas1 canvas(SCREEN_WIDTH, SCREEN_HEIGHT);

// Fixed on-canvas cursor for the text, computed once from "00:00:00". The
// font's digits all have the same advance (38 px) so every time lands in
// exactly the same spot.
int textCursorX, textCursorY;

// Low-battery message, centred in the clear band between the time's digits
// (bottom edge y = 88) and the bottom of the screen. 222 x 18 px in this font; all caps,
// so no descenders.
const char* const LOW_BATTERY_TEXT = "CHARGE BATTERY";
constexpr int LOW_BATTERY_BAND_TOP = 89;
constexpr int LOW_BATTERY_BAND_BOTTOM = SCREEN_HEIGHT;  // exclusive

// Maps logical landscape (x,y) coordinates to the panel's native portrait
// buffer according to EPD_FLIP_H/EPD_ROTATION.
void epdTransform(int x, int y, int* bx, int* by) {
#if EPD_FLIP_H
  y = SCREEN_HEIGHT - 1 - y;
#endif
#if EPD_ROTATION == 0
  *bx = x;
  *by = y;
#elif EPD_ROTATION == 1
  *bx = EPD_WIDTH - 1 - y;
  *by = x;
#elif EPD_ROTATION == 2
  *bx = EPD_WIDTH - 1 - x;
  *by = EPD_HEIGHT - 1 - y;
#elif EPD_ROTATION == 3
  *bx = y;
  *by = EPD_HEIGHT - 1 - x;
#endif
}

void epdSetPixel(uint8_t* buf, int x, int y, bool black) {
  if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) {
    return;
  }
  int bx, by;
  epdTransform(x, y, &bx, &by);

  int byteIndex = by * (EPD_WIDTH / 8) + (bx / 8);
  uint8_t mask = 0x80 >> (bx % 8);
  if (black) {
    buf[byteIndex] &= ~mask;
  } else {
    buf[byteIndex] |= mask;
  }
}

void computeLayout(const char* referenceText) {
  canvas.setFont(&ArialBold50pt);
  canvas.setTextColor(1);

  int16_t x1, y1;
  uint16_t w, h;
  canvas.getTextBounds(referenceText, 0, 0, &x1, &y1, &w, &h);
  textCursorX = (SCREEN_WIDTH - w) / 2 - x1;
  textCursorY = (SCREEN_HEIGHT - h) / 2 - y1;
}

// Clears the canvas to white.
void clearCanvas() {
  canvas.fillScreen(0);
}

// Draws text at the fixed time cursor.
void drawTime(const char* text) {
  canvas.setFont(&ArialBold50pt);
  canvas.setTextSize(1);
  canvas.setTextColor(1);
  canvas.setCursor(textCursorX, textCursorY);
  canvas.print(text);
}

// Draws the low-battery message.
void drawLowBattery() {
  canvas.setFont(&FreeSansBold12pt7b);
  canvas.setTextSize(1);
  canvas.setTextColor(1);

  int16_t x1, y1;
  uint16_t w, h;
  canvas.getTextBounds(LOW_BATTERY_TEXT, 0, 0, &x1, &y1, &w, &h);
  int bandHeight = LOW_BATTERY_BAND_BOTTOM - LOW_BATTERY_BAND_TOP;
  canvas.setCursor((SCREEN_WIDTH - w) / 2 - x1, LOW_BATTERY_BAND_TOP + (bandHeight - h) / 2 - y1);
  canvas.print(LOW_BATTERY_TEXT);
}

// Copies the canvas into the native panel buffer.
void copyCanvasToBuffer() {
  for (int y = 0; y < SCREEN_HEIGHT; y++) {
    for (int x = 0; x < SCREEN_WIDTH; x++) {
      epdSetPixel(epdBuffer, x, y, canvas.getPixel(x, y));
    }
  }
}

// EPD_Dis_PartAll()'s body up to triggering the update, without its
// internal spin on BUSY; waitWhileBusy() does that wait instead.
void startPartialUpdate(const uint8_t* datas) {
  EPD_W21_RST_0;
  delay(10);
  EPD_W21_RST_1;
  delay(10);
  EPD_W21_WriteCMD(0x3C);  // BorderWaveform
  EPD_W21_WriteDATA(0x80);

  EPD_W21_WriteCMD(0x24);  // write B/W image to RAM
  for (unsigned int i = 0; i < EPD_ARRAY; i++) {
    EPD_W21_WriteDATA(datas[i]);
  }

  EPD_W21_WriteCMD(0x22);  // Display Update Control
  EPD_W21_WriteDATA(0xFF);
  EPD_W21_WriteCMD(0x20);  // Activate Display Update Sequence
}

// Full panel init, then white written to both RAM planes (as
// EPD_SetRAMValue_BaseMap() with a white image) and a full refresh
// triggered, without the vendor driver's internal spin on BUSY during the
// refresh; waitWhileBusy() does that wait instead. The init is needed every
// time: each partial update resets the panel and only sets it up for a
// partial refresh. EPD_HW_Init() still spins on BUSY itself, but only for the
// few ms of its reset.
//
// Only ever white, so a periodic full refresh followed by a partial is the
// same sequence as at power-on, and content is always drawn with a partial
// refresh. (Black drawn by a full refresh faded to grey within ~0.5 s, but
// that was seen on a panel later found to be damaged.)
void startFullWhiteUpdate() {
  EPD_HW_Init();

  EPD_W21_WriteCMD(0x24);  // write B/W image to RAM
  for (unsigned int i = 0; i < EPD_ARRAY; i++) {
    EPD_W21_WriteDATA(0xFF);
  }
  EPD_W21_WriteCMD(0x26);  // write previous-image RAM
  for (unsigned int i = 0; i < EPD_ARRAY; i++) {
    EPD_W21_WriteDATA(0xFF);
  }

  EPD_W21_WriteCMD(0x22);  // Display Update Control
  EPD_W21_WriteDATA(0xF7);
  EPD_W21_WriteCMD(0x20);  // Activate Display Update Sequence
}

// Returns false on timeout.
bool waitWhileBusy(uint32_t timeoutMs) {
  uint32_t start = millis();
  while (isEPD_W21_BUSY) {
    if (millis() - start > timeoutMs) {
      return false;
    }
    vTaskDelay(BUSY_POLL_TICKS);
  }
  return true;
}

// Full refresh to a blank (white) screen, then the panel's RAM-retaining
// deep sleep, as after every update. Leaves both RAM planes white, the
// starting point partial refreshes need. Returns false on BUSY timeout.
bool clearToWhite() {
  startFullWhiteUpdate();
  bool finished = waitWhileBusy(FULL_BUSY_TIMEOUT_MS);
  if (!finished) {
    Serial.println("eInk: BUSY timeout (full refresh)");
  }
  EPD_DeepSleep();
  return finished;
}

}  // namespace

// Interface inputs held through deep sleep, and their levels.
struct HeldPin {
  int pin;
  uint8_t level;
};
const HeldPin heldPins[] = {
    {EPD_RST_PIN, HIGH},   // not in reset, so the panel stays in mode-1 deep sleep
    {EPD_CS_PIN, HIGH},    // deselected
    {EPD_DC_PIN, LOW},
    {EPD_SCK_PIN, LOW},
    {EPD_MOSI_PIN, LOW},
};

void Display::init() {
  // After a deep-sleep wake these pads are still held. Set each one up as an
  // output at its held level before releasing it, so it doesn't drop to the
  // pad default in between (gpio_hold_dis() note in driver/gpio.h). On a cold
  // boot the holds are already off and this is just pin setup.
  for (const HeldPin& p : heldPins) {
    pinMode(p.pin, OUTPUT);
    digitalWrite(p.pin, p.level);
    gpio_hold_dis((gpio_num_t)p.pin);
  }
  pinMode(EPD_BUSY_PIN, INPUT);

  SPI.begin(EPD_SCK_PIN, -1, EPD_MOSI_PIN, EPD_CS_PIN);
  SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));

  computeLayout("00:00:00");
}

uint64_t Display::prepareForSleep() {
  // Take SCK/SDI back from the SPI peripheral so they can be driven as GPIOs.
  SPI.endTransaction();
  SPI.end();

  // Per-pad hold works for digital pads in deep sleep on the C6
  // (SOC_GPIO_SUPPORT_HOLD_SINGLE_IO_IN_DSLP = 1), so gpio_hold_en() alone
  // is enough - gpio_deep_sleep_hold_en() doesn't exist on this chip.
  for (const HeldPin& p : heldPins) {
    pinMode(p.pin, OUTPUT);
    digitalWrite(p.pin, p.level);
    gpio_hold_en((gpio_num_t)p.pin);
  }

  return 0;
}

void Display::powerOnInit() {
  // Seeds both RAM planes (white); required once before any partial
  // refreshes, or partial updates drift/ghost against stale RAM.
  clearToWhite();
  updatesSinceFullRefresh = 0;
}

void Display::showTime(time_t utc, bool lowBattery) {
  struct tm local;
  localtime_r(&utc, &local);

  char text[16];
  snprintf(text, sizeof(text), "%02d:%02d:%02d", local.tm_hour, local.tm_min, local.tm_sec);

  Sleep::inhibit();

  clearCanvas();
  drawTime(text);
  if (lowBattery) {
    drawLowBattery();
  }
  copyCanvasToBuffer();

  // Every EPD_FULL_REFRESH_EVERY-th update first clears the panel to white
  // with a full refresh, to clear the ghosting left by partial ones. The
  // time is then drawn with a partial refresh either way, as after boot.
  bool full = ++updatesSinceFullRefresh >= EPD_FULL_REFRESH_EVERY;
  bool cleared = true;
  if (full) {
    updatesSinceFullRefresh = 0;
    cleared = clearToWhite();
  }

  // Skipped if the full refresh timed out: the panel may still be busy.
  if (cleared) {
    startPartialUpdate(epdBuffer);
    if (!waitWhileBusy(PARTIAL_BUSY_TIMEOUT_MS)) {
      Serial.println("eInk: BUSY timeout");
    }
  }

  // Deep sleep mode 1 (0x10 = 0x01) retains both RAM planes, so after an
  // ESP32 deep-sleep wake the next partial refresh still has the previous
  // image to work against, and no full refresh is needed.
  EPD_DeepSleep();
  Serial.printf("Display: %s %s%s\n", text, local.tm_isdst > 0 ? "(DST)" : "", full ? " (full refresh)" : "");

  Sleep::release();
}

void Display::showFrontLight() {
  // Light on. The PIC fades it in and turns it off itself after its own
  // on-time, so nothing is timed here and the chip can deep sleep with the
  // light still on. The Sleep inhibit only covers the I2C transaction.
  Sleep::inhibit();

  // TESTING
  // Moomin::pulse();

  // uint8_t hour, minute;
  // Alarm::readSetTime(hour, minute);
  // Serial.printf("Alarm set to %02d:%02d\n", hour, minute);

  I2CBus::sendPicCommand(I2C_COMMAND_EINK_LIGHT_ON, "light on");

  Sleep::release();
}

void Display::holdFrontLightOn() {
  // The PIC holds the light on through its own sleep, so the chip can deep
  // sleep with it on. The Sleep inhibit only covers the I2C transaction.
  Sleep::inhibit();
  I2CBus::sendPicCommand(I2C_COMMAND_EINK_LIGHT_ON_HOLD, "light on (hold)");
  Sleep::release();
}

void Display::frontLightOff() {
  Sleep::inhibit();
  I2CBus::sendPicCommand(I2C_COMMAND_EINK_LIGHT_OFF, "light off");
  Sleep::release();
}
