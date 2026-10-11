#include <Arduino.h>

#include "Utils.h"
#include "Button.h"

namespace {

const int pin = 5;
EventBits_t bit;
volatile unsigned long lastTopSwitch = 0;

void topSwitchTask(void* pparams) {
  while (1) {
    Utils::waitFor(bit);

    // Wait 30ms and check we're still pressed to avoid bouce on release being registered.
    vTaskDelay(pdMS_TO_TICKS(30));
    if (digitalRead(pin) == HIGH) {
      continue;
    }

    // Test - pulse output pin.
    digitalWrite(8, HIGH);
    delay(100);
    digitalWrite(8, LOW);
  }
}

IRAM_ATTR void topSwitchHandler() {
  static BaseType_t higherPriority;
  unsigned long now = millis();

  // Debounce.
  if (now - lastTopSwitch > 100) {
    lastTopSwitch = now;
    xEventGroupSetBitsFromISR(Utils::mainEventGroup, bit, &higherPriority);
  }
}

}  // namespace

void TopButton::init() {
  bit = Utils::getNextEventBit();
  Utils::startTask(topSwitchTask, "topSwitch");

  pinMode(pin, INPUT_PULLUP);
  attachInterrupt(pin, topSwitchHandler, FALLING);
}
