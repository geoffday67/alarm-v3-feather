#include "Utils.h"

EventGroupHandle_t Utils::mainEventGroup;
static EventBits_t nextBit;

void Utils::init() {
  nextBit = 1;
  mainEventGroup = xEventGroupCreate();
}

EventBits_t Utils::getNextEventBit() {
  EventBits_t result = nextBit;
  nextBit <<= 1;
  return result;
}

void Utils::waitFor(EventBits_t bit) {
  xEventGroupWaitBits(mainEventGroup, bit, CLEAR_ON_EXIT, WAIT_ALL, portMAX_DELAY);
}

TaskHandle_t Utils::startTask(TaskFunction_t pcode, const char* const pname, void* const pparameters) {
  TaskHandle_t handle;
  xTaskCreate(
      pcode,
      pname,
      8192,
      pparameters,
      1,
      &handle);
  return handle;
}