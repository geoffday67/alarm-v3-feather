#include "Button.h"

#include <Arduino.h>

#include "Constants.h"
#include "Music.h"
#include "Sleep.h"
#include "Utils.h"
#include "driver/rtc_io.h"

namespace {

#define BUTTON_DEBOUNCE_MS 10

static_assert(pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS) > 0, "BUTTON_DEBOUNCE_MS is shorter than one FreeRTOS tick");

EventBits_t bit;

void topSwitchTask(void* pparams) {
  while (1) {
    Utils::waitFor(bit);

    // TESTING: toggle the music instead of the front light. Restore with
    // Display::showFrontLight() and #include "Display.h".
    if (Music::isPlaying()) {
      Music::stop();
    } else {
      Music::play();
    }

    // Release debounce. The interrupt has been disabled since the ISR ran, so
    // none of the press bounce gets through. The button counts as released
    // once no LOW (pressed) sample has been seen for BUTTON_DEBOUNCE_MS; any
    // LOW during the release bounce restarts the window. Polls with
    // vTaskDelay() so lower-priority tasks (including idle) still run while
    // the button is held.
    TickType_t lastLowSeen = xTaskGetTickCount();
    while (xTaskGetTickCount() - lastLowSeen < pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS)) {
      vTaskDelay(pdMS_TO_TICKS(5));
      if (digitalRead(TOP_SWITCH_PIN) == LOW) {
        lastLowSeen = xTaskGetTickCount();
      }
    }

    // Re-arm only now the pin is stably HIGH (released): with ONLOW,
    // re-enabling while it's still LOW would re-fire the ISR immediately.
    enableInterrupt(TOP_SWITCH_PIN);

    // Done. Released after re-enabling: if the pin goes LOW again the ISR
    // fires and re-inhibits. Even if the sleeping task got in first, ext1 is
    // level-triggered so the chip would wake straight back up.
    Sleep::release();
  }
}

IRAM_ATTR void topSwitchHandler() {
  BaseType_t higherPriority = pdFALSE;

  // Stop re-triggering until the timer flag is cleared.
  disableInterrupt(TOP_SWITCH_PIN);

  // Hold off deep sleep until the button press is processed.
  Sleep::inhibit();

  // Signal to the blocked task that it can continue.
  xEventGroupSetBitsFromISR(Utils::mainEventGroup, bit, &higherPriority);

  portYIELD_FROM_ISR(higherPriority);
}

}  // namespace

void TopButton::powerOnInit() {
}

uint64_t TopButton::prepareForSleep() {
  detachInterrupt(digitalPinToInterrupt(TOP_SWITCH_PIN));

  // The pull-up must be enabled through the RTC IO driver to stay active
  // while the HP domain is off. rtc_gpio_init() is needed first - see
  // Clock::prepareForSleep().
  rtc_gpio_init((gpio_num_t)TOP_SWITCH_PIN);
  rtc_gpio_set_direction((gpio_num_t)TOP_SWITCH_PIN, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en((gpio_num_t)TOP_SWITCH_PIN);
  rtc_gpio_pulldown_dis((gpio_num_t)TOP_SWITCH_PIN);

  return 1ULL << TOP_SWITCH_PIN;
}

void TopButton::init() {
  bit = Utils::getNextEventBit();
  Utils::startTask(topSwitchTask, "topSwitch");

  // Release the pin from LP control so we can use it.
  rtc_gpio_deinit((gpio_num_t)TOP_SWITCH_PIN);
  pinMode(TOP_SWITCH_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(TOP_SWITCH_PIN), topSwitchHandler, ONLOW);  // FALLING?
}
