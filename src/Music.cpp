#include "Music.h"

#include <Arduino.h>
#include <BackgroundAudioMP3.h>
#include <LittleFS.h>

#include <atomic>

#include "Constants.h"
#include "PhilipsI2SAudio.h"
#include "Sleep.h"
#include "Utils.h"

namespace {

constexpr const char* MUSIC_FILE = "/morning.mp3";

// Decoder output gain, 0.0-16.0. Set by ear with the 4 ohm speaker in its
// enclosure and the bass-filtered file. A full-scale sample comes out at
// 3.59 Vrms x GAIN (MAX98357 at its default 9 dB: 2.1 dBV + 9 dB), so 0.2
// gives 0.72 Vrms, 0.13 W into 4 ohms. The amp reaches 1% THD+N at about
// 1.3 W into 4 ohms at 3.7 V (datasheet), i.e. GAIN about 0.64.
constexpr float GAIN = 0.2f;

// How often the music task tops up the decoder's 8 KB input buffer and checks
// for stop(). 8 KB lasts about 200 ms even at 320 kbit/s.
constexpr TickType_t FEED_INTERVAL = pdMS_TO_TICKS(10);

// After the decoder has used up the file, the I2S DMA buffers still hold
// 10 x 576 stereo samples (BackgroundAudioMP3 sets 5 buffers of one 1152-sample
// MP3 frame; ESP32I2SAudio splits each in two to stay under 1023 words):
// 131 ms at 44.1 kHz, 261 ms at 22.05 kHz.
constexpr TickType_t DRAIN_TIME = pdMS_TO_TICKS(300);

// I2S_ENABLE drives the amplifier's SD_MODE through 560k, against SD_MODE's
// internal 100k (92-108k) pull-down: 0.46-0.53 V when high, between the B0
// (0.355 V max) and B1 (0.65 V min) trip points, which selects
// (left/2 + right/2). Low is shutdown (0.6 uA). In deep sleep the pin floats
// and the pull-down keeps the amplifier shut down.

void ampEnable() {
  digitalWrite(I2S_ENABLE, HIGH);
}

void ampDisable() {
  digitalWrite(I2S_ENABLE, LOW);
}

PhilipsI2SAudio audio(I2S_BLCK, I2S_LRC, I2S_DIN);
BackgroundAudioMP3 mp3(audio);

EventBits_t playBit;
EventBits_t stopBit;

// True from play() until the music task has finished with the file.
std::atomic<bool> active{false};

bool mountFileSystem() {
  static bool mounted = false;
  if (!mounted) {
    // formatOnFail false: a failed mount must not wipe the uploaded music.
    mounted = LittleFS.begin(false, "/littlefs", 10, "littlefs");
    if (!mounted) {
      Serial.println("Music: LittleFS mount failed");
    }
  }
  return mounted;
}

void playFile() {
  if (!mountFileSystem()) {
    return;
  }
  File file = LittleFS.open(MUSIC_FILE, "r");
  if (!file) {
    Serial.printf("Music: can't open %s\n", MUSIC_FILE);
    return;
  }

  // Discard anything left in the input buffer by a previous stop().
  mp3.flush();
  mp3.setGain(GAIN);

  // Amplifier on before I2S starts. Its turn-on (tON, 7.5 ms max) is covered
  // by the silence begin() fills the DMA buffers with (10 x 576 samples,
  // 131 ms at the 44.1 kHz it starts at) ahead of any music.
  ampEnable();
  if (!mp3.begin()) {
    Serial.println("Music: decoder start failed");
    ampDisable();
    file.close();
    return;
  }
  Serial.printf("Music: playing %s\n", MUSIC_FILE);

  uint8_t buf[512];
  bool eof = false;
  while (true) {
    while (!eof && mp3.availableForWrite() >= sizeof(buf)) {
      int len = file.read(buf, sizeof(buf));
      if (len > 0) {
        mp3.write(buf, len);
      }
      if (len < (int)sizeof(buf)) {
        eof = true;
      }
    }

    if (eof && mp3.done()) {
      vTaskDelay(DRAIN_TIME);
      Serial.println("Music: finished");
      break;
    }

    EventBits_t bits = xEventGroupWaitBits(Utils::mainEventGroup, stopBit, CLEAR_ON_EXIT, WAIT_ONE, FEED_INTERVAL);
    if (bits & stopBit) {
      Serial.println("Music: stopped");
      break;
    }
  }

  // Amplifier off before I2S stops, so it's already in shutdown when the
  // clocks go away.
  ampDisable();
  mp3.end();
  file.close();
}

void musicTask(void* pparams) {
  while (1) {
    Utils::waitFor(playBit);
    playFile();

    // Cleared before the release, so a play() from here on takes a fresh
    // inhibit rather than being ignored.
    active = false;
    Sleep::release();
  }
}

}  // namespace

void Music::init() {
  pinMode(I2S_ENABLE, OUTPUT);
  ampDisable();

  playBit = Utils::getNextEventBit();
  stopBit = Utils::getNextEventBit();
  Utils::startTask(musicTask, "music");
}

void Music::play() {
  bool expected = false;
  if (!active.compare_exchange_strong(expected, true)) {
    return;  // already playing, or about to
  }

  // Taken here rather than in the task so the chip can't sleep before the
  // task gets to run.
  Sleep::inhibit();

  // A stop() that arrived after the last playback ended would otherwise end
  // this one straight away.
  xEventGroupClearBits(Utils::mainEventGroup, stopBit);
  xEventGroupSetBits(Utils::mainEventGroup, playBit);
}

void Music::stop() {
  if (active) {
    xEventGroupSetBits(Utils::mainEventGroup, stopBit);
  }
}

bool Music::isPlaying() {
  return active;
}
