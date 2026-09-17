/**
 * Plane Radar — WiFi setup, then radar UI on the round GC9A01 display.
 */

#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "hardware/display.h"
#include "services/adsb_client.h"
#include "services/locations.h"
#include "services/radar_location.h"
#include "services/wifi_setup.h"
#include "ui/menu.h"
#include "ui/radar_display.h"
#include "ui/radar_range.h"
#include "ui/status_screens.h"

namespace {

bool g_radar_visible = false;
unsigned long g_wifi_down_since = 0;
unsigned long g_last_reconnect_ms = 0;
unsigned long g_last_adsb_fetch_ms = 0;
/** Last drawn settings, so a portal save triggers a full redraw. */
uint8_t g_drawn_range_index = 0xFF;
uint8_t g_drawn_location_index = 0xFF;

void showRadarIfConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    g_radar_visible = false;
    return;
  }
  ui::radarDisplayDraw();
  g_radar_visible = true;
  g_drawn_range_index = ui::radar::rangeIndex();
  g_drawn_location_index = services::locations::selectedIndex();
}

/** The portal can change range or centre without going through the menu. */
bool settingsChangedSinceDraw() {
  return ui::radar::rangeIndex() != g_drawn_range_index ||
         services::locations::selectedIndex() != g_drawn_location_index;
}

void handleBootButton() {
  bootButtonPollHold();

  if (ui::menu::isOpen()) {
    if (bootButtonConsumeTap()) {
      ui::menu::handleClick();
    }
    if (bootButtonConsumeHold()) {
      ui::menu::handleHold();
    }
    return;
  }

  // Click or hold both open the menu, so no gesture is dead on the radar.
  if (bootButtonConsumeTap() || bootButtonConsumeHold()) {
    g_radar_visible = false;  // radar redraws once the menu closes
    ui::menu::open();
  }
}

void fetchAndDrawAircraft() {
  const float fetch_km = ui::radar::fetchRadiusKm();
  if (!services::adsb::fetchUpdate(services::location::lat(),
                                   services::location::lon(), fetch_km)) {
    handleBootButton();
    return;
  }
  ui::radarDisplayRefreshAircraft();
  handleBootButton();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("Plane Radar");

  bootButtonInit();
  displayInit();
  if (wifiShowsSetupScreenOnBoot()) {
    statusScreenPortal();
  }
  services::location::init();
  services::locations::init();
  ui::radar::rangeInit();
  services::adsb::setPollFn(wifiLoop);

  if (wifiSetupConnect()) {
    showRadarIfConnected();
  }
}

void loop() {
  handleBootButton();
  wifiLoop();

  if (ui::menu::isOpen()) {
    ui::menu::tick();
    delay(10);
    return;  // ADS-B paused so a fetch cannot stall a button press
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (g_radar_visible) {
      Serial.println("WiFi lost — will reconnect");
      g_radar_visible = false;
    }

    if (g_wifi_down_since == 0) {
      g_wifi_down_since = millis();
    }

    const unsigned long down_ms = millis() - g_wifi_down_since;
    if (down_ms >= config::kWifiDownGraceMs &&
        millis() - g_last_reconnect_ms >= config::kWifiReconnectIntervalMs) {
      g_last_reconnect_ms = millis();
      if (wifiReconnect()) {
        g_wifi_down_since = 0;
        showRadarIfConnected();
      }
    }
  } else {
    g_wifi_down_since = 0;
    if (!g_radar_visible) {
      showRadarIfConnected();
    } else if (settingsChangedSinceDraw()) {
      showRadarIfConnected();  // redraw rings and centre after a portal save
    } else if (millis() - g_last_adsb_fetch_ms >= config::kAdsbFetchIntervalMs) {
      g_last_adsb_fetch_ms = millis();
      fetchAndDrawAircraft();
    }
  }

  delay(10);
}
