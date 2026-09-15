#include "ui/menu.h"

#include <Arduino.h>
#include <WiFi.h>
#include <lgfx/v1/lgfx_fonts.hpp>

#include <cstdio>
#include <cstring>

#include "config.h"
#include "hardware/display.h"
#include "hardware/display_font.h"
#include "services/locations.h"
#include "services/wifi_setup.h"
#include "ui/menu_model.h"
#include "ui/radar_range.h"

namespace ui::menu {

namespace {

constexpr uint16_t kColorBg = 0x0000;
constexpr uint16_t kColorText = 0xFFFF;
constexpr uint16_t kColorTitle = 0x5D7F;      // muted blue
constexpr uint16_t kColorHighlight = 0x1C9F;  // filled blue bar
constexpr uint16_t kColorValue = 0x8C71;      // dimmed grey-blue
constexpr uint16_t kColorTick = 0x3E68;       // green
constexpr uint16_t kColorWarn = 0xFC40;       // amber

constexpr auto& kGfxRow = fonts::FreeSans9pt7b;
constexpr float kRowVlw = 0.95f;

constexpr int kCenterX = config::kDisplayWidth / 2;
constexpr int kTitleY = 30;
constexpr int kFirstRowY = 58;
constexpr int kResetFirstRowY = 140;
constexpr int kNetworkFirstRowY = 152;
constexpr int kRowHeight = 26;
constexpr int kRowInsetX = 40;

enum class Page { Root, Range, Altitude, Location, Network, Reset };

Page s_page = Page::Root;
Window s_window;
bool s_open = false;
unsigned long s_last_input_ms = 0;

void applyRowStyle() {
  if (displayFontIsSmooth()) {
    displayFontSetSmoothSize(tft, kRowVlw);
  } else {
    displayFontSetBitmap(tft, &kGfxRow);
  }
}

size_t rowCount();
void rowLabel(size_t index, char* out, size_t out_size);
void rowValue(size_t index, char* out, size_t out_size);
bool rowIsCurrent(size_t index);
const char* pageTitle();

void drawTitle() {
  tft.setTextDatum(textdatum_t::middle_center);
  tft.setTextColor(kColorTitle, kColorBg);
  tft.drawString(pageTitle(), kCenterX, kTitleY);
}

void drawNetworkBody() {
  const bool connected = WiFi.status() == WL_CONNECTED;
  char ssid[33];
  char ip[24];
  if (connected) {
    snprintf(ssid, sizeof(ssid), "%s", WiFi.SSID().c_str());
    snprintf(ip, sizeof(ip), "%s", WiFi.localIP().toString().c_str());
  } else {
    snprintf(ssid, sizeof(ssid), "%s", "No Wi-Fi");
    ip[0] = '\0';
  }
  tft.setTextDatum(textdatum_t::middle_center);
  tft.setTextColor(kColorText, kColorBg);
  tft.drawString(ssid, kCenterX, 96);
  if (ip[0] != '\0') {
    tft.drawString(ip, kCenterX, 126);
  }
}

void drawResetBody() {
  tft.setTextDatum(textdatum_t::middle_center);
  tft.setTextColor(kColorWarn, kColorBg);
  tft.drawString("Erase Wi-Fi, location", kCenterX, 82);
  tft.drawString("and settings?", kCenterX, 106);
}

void draw() {
  tft.fillScreen(kColorBg);
  applyRowStyle();
  drawTitle();

  if (s_page == Page::Network) {
    drawNetworkBody();
  } else if (s_page == Page::Reset) {
    drawResetBody();
  }

  int first_row_y = kFirstRowY;
  if (s_page == Page::Reset) {
    first_row_y = kResetFirstRowY;
  } else if (s_page == Page::Network) {
    first_row_y = kNetworkFirstRowY;
  }
  const size_t total = rowCount();
  const size_t last = (total < s_window.first + kVisibleRows)
                          ? total
                          : s_window.first + kVisibleRows;

  for (size_t i = s_window.first; i < last; ++i) {
    const int slot = static_cast<int>(i - s_window.first);
    const int y = first_row_y + slot * kRowHeight;
    const bool selected = (i == s_window.highlight);
    const uint16_t bg = selected ? kColorHighlight : kColorBg;

    if (selected) {
      tft.fillRoundRect(kRowInsetX - 8, y - kRowHeight / 2 + 2,
                        config::kDisplayWidth - 2 * (kRowInsetX - 8),
                        kRowHeight - 4, 4, kColorHighlight);
    }

    char label[40];
    char value[24];
    rowLabel(i, label, sizeof(label));
    rowValue(i, value, sizeof(value));

    tft.setTextColor(kColorText, bg);
    tft.setTextDatum(textdatum_t::middle_left);
    tft.drawString(label, kRowInsetX, y);

    if (value[0] != '\0') {
      tft.setTextColor(rowIsCurrent(i) ? kColorTick : kColorValue, bg);
      tft.setTextDatum(textdatum_t::middle_right);
      tft.drawString(value, config::kDisplayWidth - kRowInsetX, y);
    }
  }

  // Scroll cues: only when rows exist outside the window.
  tft.setTextDatum(textdatum_t::middle_center);
  tft.setTextColor(kColorValue, kColorBg);
  if (s_window.first > 0) {
    tft.drawString("^", kCenterX, first_row_y - kRowHeight / 2 - 4);
  }
  if (last < total) {
    tft.drawString("v", kCenterX,
                   first_row_y + static_cast<int>(kVisibleRows) * kRowHeight - 8);
  }
}

void goTo(Page page, size_t focus_index) {
  s_page = page;
  s_window = Window{};
  windowFocus(rowCount(), focus_index, &s_window);
  draw();
}

constexpr size_t kRootRowCount = 6;
const char* const kRootLabels[kRootRowCount] = {
    "Range", "Altitude", "Location", "Network", "Reset Wi-Fi", "Exit"};

/** Rows before the trailing "^ Up" and "Exit" pair. */
size_t contentRowCount() {
  switch (s_page) {
    case Page::Range:
      return radar::kRangePresetCount;
    case Page::Altitude:
      return 2;  // Flight levels, Feet
    case Page::Location: {
      const size_t n = services::locations::count();
      return (n == 0) ? 1 : n;  // entries, or the "none set" notice
    }
    case Page::Network:
      return 0;  // SSID and IP are static text, not rows
    default:
      return 0;
  }
}

/** Root page index to return to when leaving this page via "^ Up". */
size_t parentRow() {
  switch (s_page) {
    case Page::Range:    return 0;
    case Page::Altitude: return 1;
    case Page::Location: return 2;
    case Page::Network:  return 3;
    case Page::Reset:    return 4;
    default:             return 0;
  }
}

size_t rowCount() {
  switch (s_page) {
    case Page::Root:
      return kRootRowCount;  // last row is already Exit
    case Page::Reset:
      return 3;  // No (first, so a stray hold is harmless), Yes, Exit
    default:
      return contentRowCount() + 2;  // content, then Up and Exit
  }
}

const char* pageTitle() {
  switch (s_page) {
    case Page::Root:
      return "MENU";
    case Page::Range:
      return "RANGE";
    case Page::Altitude:
      return "ALTITUDE";
    case Page::Location:
      return "LOCATION";
    case Page::Network:
      return "NETWORK";
    case Page::Reset:
      return "RESET WI-FI";
  }
  return "MENU";
}

void rowLabel(size_t index, char* out, size_t out_size) {
  out[0] = '\0';

  if (s_page == Page::Root) {
    snprintf(out, out_size, "%s", kRootLabels[index]);
    return;
  }
  if (s_page == Page::Reset) {
    const char* labels[3] = {"^ No, go back", "Yes, erase", "Exit"};
    snprintf(out, out_size, "%s", labels[index]);
    return;
  }

  // Every other page ends with "^ Up" then "Exit".
  const size_t content = contentRowCount();
  if (index >= content) {
    snprintf(out, out_size, "%s", (index == content) ? "^ Up" : "Exit");
    return;
  }

  switch (s_page) {
    case Page::Range:
      radar::formatRing3Label(out, out_size, radar::kRangePresets[index].ring3_km,
                              radar::useMiles());
      return;
    case Page::Altitude:
      snprintf(out, out_size, "%s", index == 0 ? "Flight levels" : "Feet");
      return;
    case Page::Location:
      if (services::locations::count() == 0) {
        snprintf(out, out_size, "%s", "No locations set");
      } else {
        snprintf(out, out_size, "%s", services::locations::at(index)->name);
      }
      return;
    default:
      return;
  }
}

void rowValue(size_t index, char* out, size_t out_size) {
  out[0] = '\0';
  if (s_page == Page::Root) {
    if (index == 0) {
      radar::formatCurrentRing3Label(out, out_size);
    } else if (index == 1) {
      snprintf(out, out_size, "%s", radar::flightLevels() ? "FL" : "ft");
    }
    return;
  }
  if (rowIsCurrent(index)) {
    snprintf(out, out_size, "%s", "*");
  }
}

bool rowIsCurrent(size_t index) {
  switch (s_page) {
    case Page::Range:
      return index == radar::rangeIndex();
    case Page::Altitude:
      return index < 2 && ((index == 0) == radar::flightLevels());
    case Page::Location:
      return index < services::locations::count() &&
             index == services::locations::selectedIndex();
    default:
      return false;
  }
}

void activateRow(size_t index) {
  if (s_page == Page::Root) {
    switch (index) {
      case 0: goTo(Page::Range, radar::rangeIndex()); return;
      case 1: goTo(Page::Altitude, radar::flightLevels() ? 0 : 1); return;
      case 2: goTo(Page::Location, 0); return;
      case 3: goTo(Page::Network, 0); return;
      case 4: goTo(Page::Reset, 0); return;
      default: close(); return;
    }
  }

  if (s_page == Page::Reset) {
    if (index == 0) {
      goTo(Page::Root, parentRow());
    } else if (index == 1) {
      Serial.println("Menu: erasing Wi-Fi and rebooting");
      wifiResetCredentialsAndReboot();  // does not return
    } else {
      close();
    }
    return;
  }

  const size_t content = contentRowCount();
  if (index >= content) {
    if (index == content) {
      goTo(Page::Root, parentRow());
    } else {
      close();
    }
    return;
  }

  switch (s_page) {
    case Page::Range:
      radar::rangeSetIndex(static_cast<uint8_t>(index));
      draw();  // apply and stay on the page
      return;
    case Page::Altitude:
      radar::setFlightLevels(index == 0);
      draw();
      return;
    case Page::Location:
      if (services::locations::count() > 0) {
        services::locations::select(index);
        draw();
      }
      return;
    default:
      return;
  }
}

}  // namespace

bool isOpen() { return s_open; }

void open() {
  s_open = true;
  s_last_input_ms = millis();
  goTo(Page::Root, 0);
}

void close() {
  s_open = false;
  Serial.println("Menu closed");
}

void handleClick() {
  s_last_input_ms = millis();
  windowNext(rowCount(), &s_window);
  draw();
}

void handleHold() {
  s_last_input_ms = millis();
  activateRow(s_window.highlight);
}

void tick() {
  if (s_open && millis() - s_last_input_ms >= config::kMenuIdleMs) {
    close();
  }
}

}  // namespace ui::menu
