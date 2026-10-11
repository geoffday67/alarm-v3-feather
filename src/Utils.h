#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#define WAIT_ALL pdTRUE
#define WAIT_ONE pdFALSE
#define CLEAR_ON_EXIT pdTRUE
#define NO_CLEAR pdFALSE

namespace Utils {
void init();
extern EventGroupHandle_t mainEventGroup;
EventBits_t getNextEventBit();
TaskHandle_t startTask(TaskFunction_t pcode, const char* const pname, void* const pparameters = 0);
void waitFor(EventBits_t);
}  // namespace Utils
