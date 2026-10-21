#pragma once

// Alarm music: plays MUSIC_FILE from the LittleFS partition through the
// MAX98357 I2S amplifier (I2S_BLCK, I2S_LRC, I2S_DIN). The amplifier is in
// shutdown except during playback (I2S_ENABLE, see Music.cpp).
//
// Playback runs in its own task, which feeds the MP3 data to the
// BackgroundAudio decoder; the decoder itself runs in the library's I2S task.
// play() and stop() only signal the music task, so they return at once and
// are safe from any task (not from an ISR). From play() until playback ends
// (end of file or stop()) a Sleep inhibit is held, so the chip can't go into
// deep sleep mid-playback.
namespace Music {

// Call on every boot, including each deep-sleep wake, before play() or
// stop(). Drives I2S_ENABLE low (amplifier shut down) and starts the (idle)
// music task. Doesn't touch LittleFS or I2S.
void init();

// Starts playing the file from the beginning, once. Does nothing if already
// playing.
void play();

// Stops playback. Does nothing if not playing.
void stop();

// True from play() until playback has ended, by reaching the end of the file
// or by a stop() taking effect (within about 10 ms).
bool isPlaying();

}  // namespace Music
