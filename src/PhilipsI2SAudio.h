#pragma once

#include <Arduino.h>
#include <ESP32I2SAudio.h>

// BackgroundAudio's I2S output, but sending standard (Philips) I2S.
//
// ESP32I2SAudio::begin() hard-codes I2S_STD_MSB_SLOT_DEFAULT_CONFIG, which is
// left-justified: each word's MSB is on the LRCLK edge. That suits the
// MAX98357B, but the MAX98357A takes standard I2S, with the MSB one BCLK after
// the edge. Given left-justified data, the A reads each word one bit late -
// bits 14-0 plus the next word's MSB - so every sample arrives doubled
// (+6 dB) and anything beyond half scale wraps round.
//
// begin() lets the base class set the channel up as usual, then switches its
// slot format to Philips. The slot format can only be changed while the
// channel is disabled. Nothing has been written yet apart from the silence
// the base class preloads, so the brief stop costs nothing audible.
class PhilipsI2SAudio : public ESP32I2SAudio {
 public:
  using ESP32I2SAudio::ESP32I2SAudio;

  bool begin() override {
    if (!ESP32I2SAudio::begin()) {
      return false;
    }
    i2s_std_slot_config_t slot = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    bool ok = i2s_channel_disable(_tx_handle) == ESP_OK &&
              i2s_channel_reconfig_std_slot(_tx_handle, &slot) == ESP_OK &&
              i2s_channel_enable(_tx_handle) == ESP_OK;
    if (!ok) {
      // BackgroundAudioMP3::begin() ignores this result, so log it here.
      Serial.println("I2S: switch to Philips format failed");
    }
    return ok;
  }
};
