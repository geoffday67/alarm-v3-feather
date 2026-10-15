#pragma once

#include <stdint.h>

namespace TopButton {
// Call on every boot, including each deep-sleep wake (after powerOnInit()
// on a cold boot). Starts the button task and attaches the interrupt.
void init();

// Call once on a cold boot, before init(). Nothing to do at present: the
// button has no external state to configure.
void powerOnInit();

// Detaches the interrupt and hands TOP_SWITCH_PIN back to the LP IO with its
// pull-up enabled, so a press can wake the chip from deep sleep. Returns the
// pin's ext1 wake mask bit for esp_sleep_enable_ext1_wakeup(). Call just
// before deep sleep, with nothing still holding a Sleep inhibit.
uint64_t prepareForSleep();
}
