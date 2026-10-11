#include "Clock.h"

#include <Arduino.h>

#include "Constants.h"
#include "Display.h"
#include "Sleep.h"
#include "Utils.h"
#include "driver/rtc_io.h"
#include "rtc/pcf8563_timer.h"

namespace {

EventBits_t bit;

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
