#include "I2CBus.h"

#include <Arduino.h>
#include <Wire.h>

#include "Constants.h"

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

void I2CBus::sendPicCommand(uint8_t command, const char* name) {
  uint8_t result;
  {
    Guard bus;
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
