#pragma once

// Not called "Network": arduino-esp32's WiFi.h includes the core's own
// "Network.h" (which a project header of that name would shadow) and the core
// defines a global NetworkManager object named Network.
namespace Wireless {
// Powers up the WiFi driver and radio, then connects. Returns true once
// connected. Whatever the result, call disconnectWiFi() afterwards so the
// radio is fully off again.
bool connectWiFi();

// Disconnects and powers down the WiFi driver and radio completely
// (esp_wifi_stop() + esp_wifi_deinit()). Safe to call when WiFi is already
// off. Must be done before deep sleep if WiFi was started on this wake.
void disconnectWiFi();
}
