#include "Wireless.h"

#include <Arduino.h>
#include <WiFi.h>

#define SSID "Wario"
#define PASSWORD "mansion1"

bool Wireless::connectWiFi() {
  unsigned long start;
  int count, n, max_rssi, network;
  bool result = false;

  // Must come before anything that starts the WiFi driver: the driver's
  // low-level init only switches config storage to RAM if persistent is
  // already false, otherwise every WiFi.begin() writes SSID/password to NVS.
  WiFi.persistent(false);

  // Power up: initialises and starts the WiFi driver (esp_wifi_init() +
  // esp_wifi_start()) in station mode.
  WiFi.mode(WIFI_STA);

  Serial.print("Scanning ... ");
  start = millis();
  count = WiFi.scanNetworks(false, false, false, 100, 0, SSID);
  Serial.printf("%d networks found in %lu ms\n", count, millis() - start);

  max_rssi = -999;
  network = -1;
  for (n = 0; n < count; n++) {
    if (WiFi.RSSI(n) > max_rssi) {
      max_rssi = WiFi.RSSI(n);
      network = n;
    }
  }
  if (network == -1) {
    Serial.println("No usable network found");
    goto exit;
  }

  while (WiFi.status() != WL_CONNECTED) {
    WiFi.begin(SSID, PASSWORD, WiFi.channel(network), WiFi.BSSID(network));
    Serial.print("Connecting WiFi ...");

    start = millis();
    while (WiFi.status() != WL_CONNECTED) {
      if (millis() - start > 5000) {
        Serial.println("Timed out connecting to access point");
        goto exit;
      }
      Serial.print(".");
      delay(500);
    }
  }
  Serial.println();
  Serial.printf("Connected as %s to access point %s\n", WiFi.localIP().toString().c_str(), WiFi.BSSIDstr().c_str());
  result = true;

exit:
  return result;
}

void Wireless::disconnectWiFi() {
  WiFi.disconnect();

  // Power down: WIFI_OFF stops and deinitialises the driver (esp_wifi_stop()
  // + esp_wifi_deinit()), which ESP-IDF requires before deep sleep.
  WiFi.mode(WIFI_OFF);

  Serial.println("WiFi disconnected, radio off");
}
