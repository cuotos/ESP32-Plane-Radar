#include "services/wifi_setup.h"

#include <WiFi.h>
#include <WiFiManager.h>

#include <cstdio>
#include <cstring>

#include <Preferences.h>
#include <esp_system.h>
#include <esp_wifi.h>

#ifdef WM_MDNS
#include <ESPmDNS.h>
#endif

#include "config.h"
#include "services/locations.h"
#include "services/radar_location.h"
#include "ui/radar_range.h"
#include "ui/status_screens.h"

portMUX_TYPE s_boot_mux = portMUX_INITIALIZER_UNLOCKED;
volatile bool s_boot_tap_pending = false;
volatile bool s_boot_hold_pending = false;
volatile bool s_boot_hold_fired = false;
volatile bool s_boot_is_down = false;
volatile unsigned long s_boot_down_ms = 0;
bool s_long_press_handled = false;
bool s_boot_interrupt_attached = false;

void IRAM_ATTR onBootButtonIsr() {
  const bool down = digitalRead(config::kBootPin) == LOW;
  const unsigned long now = millis();
  portENTER_CRITICAL_ISR(&s_boot_mux);
  if (down) {
    s_boot_is_down = true;
    s_boot_down_ms = now;
  } else if (s_boot_is_down) {
    const unsigned long held = now - s_boot_down_ms;
    // A press that already fired a hold must not also register as a tap.
    if (!s_boot_hold_fired && held >= config::kBootTapMinMs &&
        held < config::kMenuHoldMs) {
      s_boot_tap_pending = true;
    }
    s_boot_hold_fired = false;
    s_boot_is_down = false;
  }
  portEXIT_CRITICAL_ISR(&s_boot_mux);
}

void initBootButton() {
  pinMode(config::kBootPin, INPUT_PULLUP);
  if (s_boot_interrupt_attached) {
    return;
  }
  attachInterrupt(digitalPinToInterrupt(static_cast<uint8_t>(config::kBootPin)),
                  onBootButtonIsr, CHANGE);
  s_boot_interrupt_attached = true;
}

namespace {

/** Separate from planeradar prefs (rangeInit) to avoid NVS handle conflicts. */
constexpr char kWifiPrefsNamespace[] = "wifi";
constexpr char kPrefsForcePortalKey[] = "portal";

bool s_force_config_portal = false;
WiFiManager s_wm;
bool s_wm_configured = false;

void ensureWifiManager();
void startLanWebPortal();
void stopLanWebPortal();
bool wifiLinkUp();

constexpr int kCoordParamLen = 20;
char s_miles_checkbox_attrs[32] = "type=\"checkbox\"";
WiFiManagerParameter s_param_miles("use_miles", "Display distances in miles", "T", 2,
                                   s_miles_checkbox_attrs, WFM_LABEL_AFTER);

char s_runways_checkbox_attrs[32] = "type=\"checkbox\"";
WiFiManagerParameter s_param_runways("show_runways", "Show airport runways", "T", 2,
                                     s_runways_checkbox_attrs, WFM_LABEL_AFTER);

constexpr int kLocationNameParamLen = 15;  // 14 visible characters plus NUL
constexpr char kLocationNameAttrs[] =
    " class=\"ln\" placeholder=\"Name\" maxlength=\"14\"";
constexpr char kLocationLatAttrs[] =
    " class=\"lc\" placeholder=\"Lat\" type=\"number\" step=\"0.000001\"";
constexpr char kLocationLonAttrs[] =
    " class=\"lc\" placeholder=\"Lon\" type=\"number\" step=\"0.000001\"";

/**
 * Each location is three fields. WiFiManager stores ids and labels by pointer,
 * so these must be string literals rather than generated text.
 */
WiFiManagerParameter s_param_loc_name[services::kMaxLocations] = {
    {"l1n", "Location 1", "", kLocationNameParamLen, kLocationNameAttrs},
    {"l2n", "Location 2", "", kLocationNameParamLen, kLocationNameAttrs},
    {"l3n", "Location 3", "", kLocationNameParamLen, kLocationNameAttrs},
    {"l4n", "Location 4", "", kLocationNameParamLen, kLocationNameAttrs},
    {"l5n", "Location 5", "", kLocationNameParamLen, kLocationNameAttrs},
    {"l6n", "Location 6", "", kLocationNameParamLen, kLocationNameAttrs},
    {"l7n", "Location 7", "", kLocationNameParamLen, kLocationNameAttrs},
    {"l8n", "Location 8", "", kLocationNameParamLen, kLocationNameAttrs},
};

WiFiManagerParameter s_param_loc_lat[services::kMaxLocations] = {
    {"l1a", "", "", kCoordParamLen, kLocationLatAttrs, WFM_NO_LABEL},
    {"l2a", "", "", kCoordParamLen, kLocationLatAttrs, WFM_NO_LABEL},
    {"l3a", "", "", kCoordParamLen, kLocationLatAttrs, WFM_NO_LABEL},
    {"l4a", "", "", kCoordParamLen, kLocationLatAttrs, WFM_NO_LABEL},
    {"l5a", "", "", kCoordParamLen, kLocationLatAttrs, WFM_NO_LABEL},
    {"l6a", "", "", kCoordParamLen, kLocationLatAttrs, WFM_NO_LABEL},
    {"l7a", "", "", kCoordParamLen, kLocationLatAttrs, WFM_NO_LABEL},
    {"l8a", "", "", kCoordParamLen, kLocationLatAttrs, WFM_NO_LABEL},
};

WiFiManagerParameter s_param_loc_lon[services::kMaxLocations] = {
    {"l1o", "", "", kCoordParamLen, kLocationLonAttrs, WFM_NO_LABEL},
    {"l2o", "", "", kCoordParamLen, kLocationLonAttrs, WFM_NO_LABEL},
    {"l3o", "", "", kCoordParamLen, kLocationLonAttrs, WFM_NO_LABEL},
    {"l4o", "", "", kCoordParamLen, kLocationLonAttrs, WFM_NO_LABEL},
    {"l5o", "", "", kCoordParamLen, kLocationLonAttrs, WFM_NO_LABEL},
    {"l6o", "", "", kCoordParamLen, kLocationLonAttrs, WFM_NO_LABEL},
    {"l7o", "", "", kCoordParamLen, kLocationLonAttrs, WFM_NO_LABEL},
    {"l8o", "", "", kCoordParamLen, kLocationLonAttrs, WFM_NO_LABEL},
};

char s_flight_levels_checkbox_attrs[32] = "type=\"checkbox\"";
WiFiManagerParameter s_param_flight_levels("flight_levels",
                                           "Altitude as flight levels (030 = 3,000 ft)",
                                           "T", 2, s_flight_levels_checkbox_attrs,
                                           WFM_LABEL_AFTER);

void refreshPortalParamDefaults() {
  for (size_t i = 0; i < services::kMaxLocations; ++i) {
    const services::Location* item = services::locations::at(i);
    char lat_buf[kCoordParamLen + 1] = "";
    char lon_buf[kCoordParamLen + 1] = "";
    if (item != nullptr) {
      snprintf(lat_buf, sizeof(lat_buf), "%.6f", item->lat);
      snprintf(lon_buf, sizeof(lon_buf), "%.6f", item->lon);
    }
    s_param_loc_name[i].setValue(item != nullptr ? item->name : "",
                                 kLocationNameParamLen);
    s_param_loc_lat[i].setValue(lat_buf, kCoordParamLen);
    s_param_loc_lon[i].setValue(lon_buf, kCoordParamLen);
  }
  snprintf(s_miles_checkbox_attrs, sizeof(s_miles_checkbox_attrs), "type=\"checkbox\"%s",
           ui::radar::useMiles() ? " checked" : "");
  s_param_miles.setValue("T", 2);
  snprintf(s_runways_checkbox_attrs, sizeof(s_runways_checkbox_attrs),
           "type=\"checkbox\"%s", ui::radar::showRunways() ? " checked" : "");
  s_param_runways.setValue("T", 2);
  snprintf(s_flight_levels_checkbox_attrs, sizeof(s_flight_levels_checkbox_attrs),
           "type=\"checkbox\"%s", ui::radar::flightLevels() ? " checked" : "");
  s_param_flight_levels.setValue("T", 2);
}

void onPortalParamsSaved() {
  ui::radar::saveMilesFromPortal(s_param_miles.getValue());
  ui::radar::saveRunwaysFromPortal(s_param_runways.getValue());
  ui::radar::saveFlightLevelsFromPortal(s_param_flight_levels.getValue());
  // Recombine each row into the "name, lat, lon" form the parser validates.
  char loc_text[services::kMaxLocations][kCoordParamLen * 2 + 32];
  const char* loc_lines[services::kMaxLocations];
  for (size_t i = 0; i < services::kMaxLocations; ++i) {
    const char* name = s_param_loc_name[i].getValue();
    const char* lat = s_param_loc_lat[i].getValue();
    const char* lon = s_param_loc_lon[i].getValue();
    if (name[0] == '\0' && lat[0] == '\0' && lon[0] == '\0') {
      loc_text[i][0] = '\0';  // wholly blank row, not a broken one
    } else {
      snprintf(loc_text[i], sizeof(loc_text[i]), "%s, %s, %s", name, lat, lon);
    }
    loc_lines[i] = loc_text[i];
  }
  char loc_err[96];
  services::locations::saveFromPortalLines(loc_lines, services::kMaxLocations,
                                           loc_err, sizeof(loc_err));
}

void attachPortalParams(WiFiManager& wm) {
  refreshPortalParamDefaults();
  wm.addParameter(&s_param_miles);
  wm.addParameter(&s_param_runways);
  wm.addParameter(&s_param_flight_levels);
  for (size_t i = 0; i < services::kMaxLocations; ++i) {
    wm.addParameter(&s_param_loc_name[i]);
    wm.addParameter(&s_param_loc_lat[i]);
    wm.addParameter(&s_param_loc_lon[i]);
  }
  wm.setSaveParamsCallback(onPortalParamsSaved);
}

void markForceConfigPortal() {
  s_force_config_portal = true;
  Preferences prefs;
  if (!prefs.begin(kWifiPrefsNamespace, false)) {
    return;
  }
  prefs.putBool(kPrefsForcePortalKey, true);
  prefs.end();
}

bool consumeForceConfigPortal() {
  if (s_force_config_portal) {
    s_force_config_portal = false;
    Preferences prefs;
    if (prefs.begin(kWifiPrefsNamespace, false)) {
      prefs.remove(kPrefsForcePortalKey);
      prefs.end();
    }
    return true;
  }

  Preferences prefs;
  if (!prefs.begin(kWifiPrefsNamespace, true)) {
    return false;
  }
  const bool pending = prefs.getBool(kPrefsForcePortalKey, false);
  prefs.end();
  if (!pending) {
    return false;
  }

  if (prefs.begin(kWifiPrefsNamespace, false)) {
    prefs.remove(kPrefsForcePortalKey);
    prefs.end();
  }
  return true;
}

bool storedWifiCredentials() {
  wifi_mode_t mode = WIFI_MODE_NULL;
  if (esp_wifi_get_mode(&mode) != ESP_OK || mode == WIFI_MODE_NULL) {
    WiFi.mode(WIFI_STA);
    delay(50);
  }

  wifi_config_t conf = {};
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK) {
    return false;
  }
  return conf.sta.ssid[0] != '\0';
}

void eraseWifiCredentials() {
  stopLanWebPortal();
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_OFF);
  delay(100);

  ensureWifiManager();
  WiFi.persistent(true);
  s_wm.resetSettings();
  s_wm.erase();
  WiFi.disconnect(true, true);
  WiFi.persistent(false);

  WiFi.mode(WIFI_OFF);
  delay(100);
}

void resetWifiCredentials() {
  markForceConfigPortal();
  eraseWifiCredentials();
  services::location::clear();
  services::locations::clear();
  ui::radar::unitsReset();
  Serial.println("WiFi credentials, location, and units cleared");
}

void onConfigPortalApStarted(WiFiManager*) {
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  statusScreenPortal();
#ifdef WM_MDNS
  if (MDNS.begin(config::kPortalHostname)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("Setup portal: http://%s.local (or http://%s)\n",
                  config::kPortalHostname, config::kPortalIp);
  } else {
    Serial.printf("Setup portal: http://%s (mDNS unavailable)\n", config::kPortalIp);
  }
#else
  Serial.printf("Setup portal: http://%s\n", config::kPortalIp);
#endif
}

bool wifiLinkUp() {
  return WiFi.status() == WL_CONNECTED &&
         WiFi.localIP() != IPAddress(0, 0, 0, 0);
}

/**
 * WiFiManager has no API for renaming its built-in menu buttons, and its
 * label strings live in the library under .pio (wiped on a clean build), so
 * relabel from the page itself.
 */
constexpr char kPortalHeadHtml[] =
    "<style>"
    ".locrow{display:flex;gap:4px;margin-bottom:8px;align-items:center}"
    ".locrow label{flex:0 0 1.2em;margin:0;font-size:.85em;opacity:.7;text-align:right}"
    ".locrow input{margin:0;min-width:0}"
    ".locrow input.ln{flex:3}"
    ".locrow input.lc{flex:2}"
    "</style>"
    "<script>addEventListener('DOMContentLoaded',function(){"
    "document.querySelectorAll('button').forEach(function(b){"
    "if(b.textContent.trim()==='Configure WiFi')b.textContent='Configure';"
    "});"
    // Each location is three separate WiFiManager params, so the name, lat and
    // lon inputs arrive as siblings separated by <br/>. Pull each trio into one
    // flex row and drop the breaks so they sit on a single line.
    "for(var i=1;i<=8;i++){"
    "var t=[document.getElementById('l'+i+'n'),document.getElementById('l'+i+'a'),"
    "document.getElementById('l'+i+'o')];"
    "if(!t[0]||!t[1]||!t[2])continue;"
    "var r=document.createElement('div');r.className='locrow';"
    // Put the row where the label was, then pull the label in as its first cell.
    "var lb=document.querySelector(\"label[for='l\"+i+\"n']\");"
    "var an=lb||t[0];an.parentNode.insertBefore(r,an);"
    "if(lb){lb.textContent=String(i);r.appendChild(lb);}"
    "t.forEach(function(el){"
    "var p=el.previousSibling;"
    "while(p&&p.nodeName==='BR'){var q=p.previousSibling;p.parentNode.removeChild(p);p=q;}"
    "r.appendChild(el);});"
    "}});</script>";

void ensureWifiManager() {
  if (s_wm_configured) {
    return;
  }
  s_wm.setCustomHeadElement(kPortalHeadHtml);
  s_wm.setConfigPortalTimeout(config::kWifiPortalTimeoutSec);
  s_wm.setAPStaticIPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                           IPAddress(255, 255, 255, 0));
  s_wm.setHostname(config::kPortalHostname);
  s_wm.setAPCallback(onConfigPortalApStarted);
  attachPortalParams(s_wm);
  s_wm_configured = true;
}

void startLanWebPortal() {
  if (!wifiLinkUp() || s_wm.getWebPortalActive() ||
      s_wm.getConfigPortalActive()) {
    return;
  }
  refreshPortalParamDefaults();
  WiFi.mode(WIFI_STA);
  s_wm.setConfigPortalBlocking(false);
#ifdef WM_MDNS
  MDNS.end();
  if (MDNS.begin(config::kPortalHostname)) {
    MDNS.addService("http", "tcp", 80);
  }
#endif
  s_wm.startWebPortal();
  Serial.printf("LAN config: http://%s.local or http://%s\n",
                config::kPortalHostname, WiFi.localIP().toString().c_str());
}

void stopLanWebPortal() {
  if (!s_wm.getWebPortalActive()) {
    return;
  }
  s_wm.stopWebPortal();
#ifdef WM_MDNS
  MDNS.end();
#endif
}

void prepareSta() {
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(WIFI_PS_NONE);
  WiFi.setAutoReconnect(true);
}

void startStaConnect(const String& ssid, const String& pass) {
  prepareSta();
  if (ssid.length() > 0) {
    WiFi.begin(ssid.c_str(), pass.c_str());
  } else {
    WiFi.begin();
  }
}

bool waitForLinkWithUi(const char* ssid_for_ui, unsigned long attempt_ms) {
  const unsigned long deadline = millis() + attempt_ms;
  while (millis() < deadline) {
    if (wifiLinkUp()) {
      return true;
    }
    bootButtonPollLongPress();
    statusScreenConnectingTick();
    delay(config::kWifiConnectingFrameMs);
  }
  return wifiLinkUp();
}

bool tryConnectWithUi(const String& ssid, const String& pass, bool show_ui) {
  if (wifiLinkUp()) {
    return true;
  }

  const char* ui_ssid = ssid.length() > 0 ? ssid.c_str() : "network";
  if (show_ui) {
    statusScreenConnectingBegin(ui_ssid);
  }

  for (uint8_t attempt = 1; attempt <= config::kWifiConnectAttempts; ++attempt) {
    if (attempt > 1) {
      Serial.printf("WiFi connect retry %u/%u\n", attempt,
                    config::kWifiConnectAttempts);
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      delay(400);
    }

    startStaConnect(ssid, pass);

    if (waitForLinkWithUi(ui_ssid, config::kWifiConnectAttemptMs)) {
      return true;
    }
  }

  return false;
}

bool connectSavedNetwork(bool show_ui) {
  wifi_mode_t mode = WIFI_MODE_NULL;
  if (esp_wifi_get_mode(&mode) != ESP_OK || mode == WIFI_MODE_NULL) {
    WiFi.mode(WIFI_STA);
    delay(50);
  }

  wifi_config_t conf = {};
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK) {
    return false;
  }

  if (conf.sta.ssid[0] == '\0') {
    return false;
  }

  // ESP-IDF stores the SSID in a fixed 32-byte field. A maximum-length
  // SSID has no room for a trailing NUL, so copy it to a larger buffer
  // and explicitly terminate it before constructing an Arduino String.
  char ssid_buf[sizeof(conf.sta.ssid) + 1] = {};
  memcpy(ssid_buf, conf.sta.ssid, sizeof(conf.sta.ssid));
  ssid_buf[sizeof(conf.sta.ssid)] = '\0';

  char pass_buf[sizeof(conf.sta.password) + 1] = {};
  memcpy(pass_buf, conf.sta.password, sizeof(conf.sta.password));
  pass_buf[sizeof(conf.sta.password)] = '\0';

  const String ssid(ssid_buf);
  const String pass(pass_buf);

  return tryConnectWithUi(ssid, pass, show_ui);
}

bool openConfigPortal() {
  stopLanWebPortal();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(50);
  statusScreenPortal();
  s_wm.setConfigPortalBlocking(false);
  s_wm.startConfigPortal(config::kPortalApName);
  while (s_wm.getConfigPortalActive()) {
    bootButtonPollLongPress();
    if (s_wm.process()) {
      return true;
    }
    delay(10);
  }
  return wifiLinkUp();
}

}  // namespace

bool wifiShowsSetupScreenOnBoot() {
  if (s_force_config_portal) {
    return true;
  }
  Preferences prefs;
  if (!prefs.begin(kWifiPrefsNamespace, true)) {
    return false;
  }
  const bool pending = prefs.getBool(kPrefsForcePortalKey, false);
  prefs.end();
  return pending;
}

bool wifiBootButtonPressed() {
  return digitalRead(config::kBootPin) == LOW;
}

void bootButtonInit() { initBootButton(); }

bool bootButtonConsumeTap() {
  portENTER_CRITICAL(&s_boot_mux);
  const bool tap = s_boot_tap_pending;
  if (tap) {
    s_boot_tap_pending = false;
  }
  portEXIT_CRITICAL(&s_boot_mux);
  return tap;
}

void bootButtonPollHold() {
  if (!wifiBootButtonPressed()) {
    return;
  }
  portENTER_CRITICAL(&s_boot_mux);
  if (!s_boot_is_down) {
    s_boot_is_down = true;
    s_boot_down_ms = millis();
  }
  const unsigned long down_ms = s_boot_down_ms;
  const bool already_fired = s_boot_hold_fired;
  portEXIT_CRITICAL(&s_boot_mux);

  if (already_fired || millis() - down_ms < config::kMenuHoldMs) {
    return;
  }
  portENTER_CRITICAL(&s_boot_mux);
  s_boot_hold_fired = true;
  s_boot_hold_pending = true;
  portEXIT_CRITICAL(&s_boot_mux);
}

bool bootButtonConsumeHold() {
  portENTER_CRITICAL(&s_boot_mux);
  const bool hold = s_boot_hold_pending;
  if (hold) {
    s_boot_hold_pending = false;
  }
  portEXIT_CRITICAL(&s_boot_mux);
  return hold;
}

void bootButtonPollLongPress() {
  if (wifiBootButtonPressed()) {
    portENTER_CRITICAL(&s_boot_mux);
    if (!s_boot_is_down) {
      s_boot_is_down = true;
      s_boot_down_ms = millis();
    }
    const unsigned long down_ms = s_boot_down_ms;
    portEXIT_CRITICAL(&s_boot_mux);

    if (!s_long_press_handled &&
        millis() - down_ms >= config::kBootResetHoldMs) {
      s_long_press_handled = true;
      Serial.println("BOOT held — resetting WiFi");
      wifiResetCredentialsAndReboot();
    }
  } else {
    portENTER_CRITICAL(&s_boot_mux);
    s_boot_is_down = false;
    portEXIT_CRITICAL(&s_boot_mux);
    s_long_press_handled = false;
  }
}

void wifiResetCredentialsAndReboot() {
  resetWifiCredentials();
  statusScreenWifiReset();
  delay(800);
  esp_restart();
}

bool wifiReconnect() {
  initBootButton();
  Serial.println("WiFi reconnecting...");
  return connectSavedNetwork(true);
}

void wifiLoop() {
  ensureWifiManager();
  if (wifiLinkUp()) {
    if (!s_wm.getWebPortalActive() && !s_wm.getConfigPortalActive()) {
      startLanWebPortal();
    }
    if (s_wm.getWebPortalActive() || s_wm.getConfigPortalActive()) {
      bootButtonPollLongPress();
      s_wm.process();
    }
  } else {
    stopLanWebPortal();
  }
}

bool wifiSetupConnect() {
  initBootButton();
  ensureWifiManager();

  const bool force_portal = consumeForceConfigPortal();
  WiFi.setAutoReconnect(false);

  if (force_portal) {
    eraseWifiCredentials();
    WiFi.mode(WIFI_OFF);
    delay(100);
  }

  if (force_portal) {
    Serial.println("Opening WiFi setup portal (after reset)");
    if (openConfigPortal() && wifiLinkUp()) {
      WiFi.setAutoReconnect(true);
      Serial.printf("Connected: %s  IP %s\n", WiFi.SSID().c_str(),
                    WiFi.localIP().toString().c_str());
      return true;
    }
    Serial.println("WiFi connection failed");
    statusScreenConnectFailed();
    return false;
  }

  Serial.println("Connecting to WiFi (portal opens if needed)...");

  if (wifiLinkUp()) {
    WiFi.setAutoReconnect(true);
    Serial.printf("Connected: %s  IP %s\n", WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str());
    return true;
  }

  if (storedWifiCredentials() && connectSavedNetwork(true)) {
    WiFi.setAutoReconnect(true);
    Serial.printf("Connected: %s  IP %s\n", WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str());
    return true;
  }

  if (storedWifiCredentials()) {
    Serial.println("Saved WiFi could not connect — opening setup portal");
  } else {
    Serial.println("No saved WiFi — opening setup portal");
  }

  if (openConfigPortal() && wifiLinkUp()) {
    WiFi.setAutoReconnect(true);
    Serial.printf("Connected: %s  IP %s\n", WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str());
    return true;
  }

  Serial.println("WiFi connection failed");
  statusScreenConnectFailed();
  return false;
}
