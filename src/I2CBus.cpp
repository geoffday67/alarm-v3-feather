#include "I2CBus.h"

#include <Wire.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace {

// Statically allocated so creation can't fail for lack of heap.
StaticSemaphore_t mutexBuffer;
SemaphoreHandle_t mutex = nullptr;

}  // namespace

void I2CBus::begin(int sdaPin, int sclPin, uint32_t frequency) {
  mutex = xSemaphoreCreateMutexStatic(&mutexBuffer);
  Wire.begin(sdaPin, sclPin);
  Wire.setClock(frequency);
}

void I2CBus::lock() {
  configASSERT(mutex != nullptr);  // lock() before begin()
  xSemaphoreTake(mutex, portMAX_DELAY);
}

void I2CBus::unlock() {
  xSemaphoreGive(mutex);
}
