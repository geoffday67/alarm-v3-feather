#include "Display.h"

#include <Adafruit_GFX.h>
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

#include "Display_EPD_W21.h"
#include "Display_EPD_W21_spi.h"
#include "Constants.h"
#include "Fonts/ArialBold50pt.h"
#include "I2CBus.h"
#include "Sleep.h"
#include "driver/gpio.h"

namespace {

// Logical (landscape) screen dimensions as seen once EPD_ROTATION is applied.
#define SCREEN_WIDTH EPD_HEIGHT
#define SCREEN_HEIGHT EPD_WIDTH

// How often to poll BUSY during a partial refresh, and how long to wait
// before giving up. A partial refresh takes ~0.3 s; the timeout is only a
// guard so a disconnected or faulty panel can't hold off deep sleep forever.
constexpr TickType_t BUSY_POLL_TICKS = pdMS_TO_TICKS(10);
constexpr uint32_t BUSY_TIMEOUT_MS = 2000;

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

// Renders text at the fixed cursor into the whole native buffer.
void renderTextToBuffer(const char* text) {
  canvas.fillScreen(0);
  // 5px border at the very edge of the display area, for mount alignment.
  for (int i = 0; i < 5; i++) {
    canvas.drawRect(i, i, SCREEN_WIDTH - 2 * i, SCREEN_HEIGHT - 2 * i, 1);
  }
  canvas.setCursor(textCursorX, textCursorY);
  canvas.print(text);

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

// Returns false on timeout.
bool waitWhileBusy() {
  uint32_t start = millis();
  while (isEPD_W21_BUSY) {
    if (millis() - start > BUSY_TIMEOUT_MS) {
      return false;
    }
    vTaskDelay(BUSY_POLL_TICKS);
  }
  return true;
}

// Sends a single-byte command to the PIC. The I2C bus is already initialised
// during wake (see main.cpp).
void sendPicCommand(uint8_t command, const char* name) {
  uint8_t result;
  {
    I2CBus::Guard bus;
    Wire.beginTransmission(PIC_I2C_ADDRESS);
    Wire.write(command);
    result = Wire.endTransmission();
  }
  if (result == 0) {
    Serial.printf("PIC: %s sent\n", name);
  } else {
    Serial.printf("PIC: %s failed, endTransmission() = %d\n", name, result);
  }
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
  // Blank screen apart from the alignment border.
  renderTextToBuffer("");

  EPD_HW_Init();
  // Seeds both RAM planes with the current image; required once before
  // any partial refreshes, or partial updates drift/ghost against stale RAM.
  EPD_SetRAMValue_BaseMap(epdBuffer);

  // RAM-retaining deep sleep, as after every update (see showTime()).
  EPD_DeepSleep();
}

void Display::showTime(time_t utc) {
  struct tm local;
  localtime_r(&utc, &local);

  char text[16];
  snprintf(text, sizeof(text), "%02d:%02d:%02d", local.tm_hour, local.tm_min, local.tm_sec);

  Sleep::inhibit();

  renderTextToBuffer(text);

  startPartialUpdate(epdBuffer);
  if (!waitWhileBusy()) {
    Serial.println("eInk: BUSY timeout");
  }

  // Deep sleep mode 1 (0x10 = 0x01) retains both RAM planes, so after an
  // ESP32 deep-sleep wake the next partial refresh still has the previous
  // image to work against, and no full refresh is needed.
  EPD_DeepSleep();
  Serial.printf("Display: %s %s\n", text, local.tm_isdst > 0 ? "(DST)" : "");

  Sleep::release();
}

void Display::showFrontLight() {
  // Light on, wait, light off. The PIC only fades between the two levels; the
  // on time is timed here. Holds a Sleep inhibit so the chip can't go into
  // deep sleep with the light left on. Blocks the caller for
  // SCREEN_LIGHT_TIME, but with vTaskDelay(), so other tasks keep running.
  Sleep::inhibit();

  sendPicCommand(I2C_COMMAND_EINK_LIGHT_ON, "light on");
  vTaskDelay(pdMS_TO_TICKS(SCREEN_LIGHT_TIME));
  sendPicCommand(I2C_COMMAND_EINK_LIGHT_OFF, "light off");

  Sleep::release();
}
