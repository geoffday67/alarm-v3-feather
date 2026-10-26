#include "AlarmSequence.h"

#include <Arduino.h>

#include "Display.h"
#include "Moomin.h"
#include "Music.h"
#include "PDLC.h"
#include "Sleep.h"
#include "Utils.h"
#include "freertos/semphr.h"

namespace {

enum class Stage { Idle, Ringing, FilmClear };

// After the music is stopped, how long the film stays clear without a
// second press.
constexpr TickType_t FILM_TIMEOUT = pdMS_TO_TICKS(30000);

// Guards stage and filmClearSince, and makes each transition (stage check
// plus its actions) atomic with respect to the others.
SemaphoreHandle_t lock;

Stage currentStage = Stage::Idle;

// Tick count when the music was stopped (entry to FilmClear).
TickType_t filmClearSince;

// Wake the sequence task to re-check the endings and its timeout. Neither
// carries any state: the stage (read under the lock) and Music::isPlaying()
// are the authority, so a stale or extra bit just costs one more check.
EventBits_t stageChangedBit;   // set by start() and handlePress()
EventBits_t musicFinishedBit;  // set by Music when playback ends

class Locked {
 public:
  Locked() { xSemaphoreTake(lock, portMAX_DELAY); }
  ~Locked() { xSemaphoreGive(lock); }
  Locked(const Locked&) = delete;
  Locked& operator=(const Locked&) = delete;
};

// Any stage -> Idle. Call with the lock held.
void finish(const char* reason) {
  Serial.printf("Alarm: %s, film and light off\n", reason);
  PDLC::off();
  Display::frontLightOff();
  currentStage = Stage::Idle;
  Serial.println("Alarm: finished");
  Sleep::release();
}

// Ends the sequence if one of its own endings has come.
void checkEndings() {
  Locked locked;
  switch (currentStage) {
    case Stage::Idle:
      break;
    case Stage::Ringing:
      if (!Music::isPlaying()) {
        finish("music ended");
      }
      break;
    case Stage::FilmClear:
      if (xTaskGetTickCount() - filmClearSince >= FILM_TIMEOUT) {
        finish("no second press");
      }
      break;
  }
}

// How long the sequence task can wait before an ending could be due by time
// alone: what's left of FILM_TIMEOUT in FilmClear, otherwise forever (the
// other endings come with a bit).
TickType_t timeUntilTimeout() {
  Locked locked;
  if (currentStage != Stage::FilmClear) {
    return portMAX_DELAY;
  }
  TickType_t elapsed = xTaskGetTickCount() - filmClearSince;
  return elapsed >= FILM_TIMEOUT ? 0 : FILM_TIMEOUT - elapsed;
}

// Handles the endings that happen by themselves: the music finishing in
// Ringing, and FILM_TIMEOUT passing in FilmClear. Sleeps until a bit is set
// or the timeout is due. A bit set between timeUntilTimeout() and the wait
// stays set, so the wait returns at once rather than missing it.
void alarmSequenceTask(void* pparams) {
  while (1) {
    xEventGroupWaitBits(Utils::mainEventGroup, stageChangedBit | musicFinishedBit, CLEAR_ON_EXIT, WAIT_ONE,
                        timeUntilTimeout());
    checkEndings();
  }
}

}  // namespace

void AlarmSequence::init() {
  lock = xSemaphoreCreateMutex();
  stageChangedBit = Utils::getNextEventBit();
  musicFinishedBit = Utils::getNextEventBit();
  Music::signalWhenFinished(musicFinishedBit);
  Utils::startTask(alarmSequenceTask, "alarmSequence");
}

void AlarmSequence::start() {
  Locked locked;
  if (currentStage != Stage::Idle) {
    Serial.println("Alarm: already running");
    return;
  }

  // Released by finish().
  Sleep::inhibit();

  Serial.println("Alarm: started");
  Music::play();
  Moomin::pulse();
  PDLC::on();
  Display::holdFrontLightOn();
  currentStage = Stage::Ringing;

  xEventGroupSetBits(Utils::mainEventGroup, stageChangedBit);
}

bool AlarmSequence::handlePress() {
  Locked locked;
  switch (currentStage) {
    case Stage::Idle:
      return false;
    case Stage::Ringing:
      Serial.println("Alarm: press, stopping music");
      Music::stop();
      filmClearSince = xTaskGetTickCount();
      currentStage = Stage::FilmClear;
      // So the sequence task starts timing FILM_TIMEOUT.
      xEventGroupSetBits(Utils::mainEventGroup, stageChangedBit);
      return true;
    case Stage::FilmClear:
      finish("second press");
      return true;
  }
  return false;
}
