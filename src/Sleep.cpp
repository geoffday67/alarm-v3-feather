#include "Sleep.h"

#include <atomic>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

// Lock-free on the C6's RV32IMAC core (A extension: amoadd.w), so it can be
// changed from an ISR without a critical section.
std::atomic<int> inhibitCount{0};

// The task blocked in waitUntilAllowed(); notified when the count reaches 0.
TaskHandle_t waiter = nullptr;

}  // namespace

void Sleep::init() {
  waiter = xTaskGetCurrentTaskHandle();
}

void Sleep::inhibit() {
  inhibitCount.fetch_add(1, std::memory_order_acq_rel);
}

void Sleep::release() {
  int previous = inhibitCount.fetch_sub(1, std::memory_order_acq_rel);
  configASSERT(previous > 0);  // release() without a matching inhibit()

  if (previous != 1 || waiter == nullptr) {
    return;
  }

  if (xPortInIsrContext()) {
    BaseType_t higherPriority = pdFALSE;
    vTaskNotifyGiveFromISR(waiter, &higherPriority);
    portYIELD_FROM_ISR(higherPriority);
  } else {
    xTaskNotifyGive(waiter);
  }
}

void Sleep::waitUntilAllowed() {
  // The count is the authority; the notification only wakes us to re-check
  // it. A release() between the load and the take leaves the notification
  // pending, so the take returns at once rather than missing it. A stale or
  // unrelated notification just costs one extra check.
  while (inhibitCount.load(std::memory_order_acquire) != 0) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  }
}
