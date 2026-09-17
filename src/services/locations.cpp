#include "services/locations.h"

#include <Arduino.h>
#include <Preferences.h>

#include <cstdio>
#include <cstring>

#include "services/radar_location.h"

namespace services::locations {

namespace {

/** Shared with radar_range; NVS keys must stay under 15 characters. */
constexpr char kPrefsNamespace[] = "planeradar";
constexpr char kPrefsListKey[] = "locs";
constexpr char kPrefsSelectedKey[] = "locSel";

/** 8 lines of "14-char name, lat, lon" plus newlines, with headroom. */
constexpr size_t kRawTextSize = 512;

Preferences s_prefs;
LocationList s_list;
char s_raw[kRawTextSize];
uint8_t s_selected = kNoSelection;

void saveRaw() {
  if (!s_prefs.begin(kPrefsNamespace, false)) {
    Serial.println("Locations: NVS unavailable, change is session-only");
    return;
  }
  s_prefs.putString(kPrefsListKey, s_raw);
  s_prefs.putUChar(kPrefsSelectedKey, s_selected);
  s_prefs.end();
}

/** With no explicit choice, fall back to the first entry so the list always
 *  drives the radar centre. */
void ensureSelection() {
  if (s_selected != kNoSelection || s_list.count == 0) {
    return;
  }
  const Location& first = s_list.items[0];
  if (location::saveCoords(first.lat, first.lon)) {
    s_selected = 0;
    Serial.printf("Location: defaulted to %s\n", first.name);
  }
}

}  // namespace

void init() {
  s_raw[0] = '\0';
  s_list.count = 0;
  s_selected = kNoSelection;

  if (!s_prefs.begin(kPrefsNamespace, true)) {
    return;
  }
  s_prefs.getString(kPrefsListKey, s_raw, sizeof(s_raw));
  s_selected = s_prefs.getUChar(kPrefsSelectedKey, kNoSelection);
  s_prefs.end();

  char err[96];
  parseLocationList(s_raw, &s_list, err, sizeof(err));
  if (s_selected != kNoSelection && s_selected >= s_list.count) {
    s_selected = kNoSelection;  // list shrank since the choice was made
  }
  ensureSelection();
  Serial.printf("Locations: %u loaded\n", static_cast<unsigned>(s_list.count));
}

size_t count() { return s_list.count; }

const Location* at(size_t index) {
  return (index < s_list.count) ? &s_list.items[index] : nullptr;
}

uint8_t selectedIndex() { return s_selected; }

bool select(size_t index) {
  const Location* item = at(index);
  if (item == nullptr) {
    return false;
  }
  if (!location::saveCoords(item->lat, item->lon)) {
    return false;
  }
  s_selected = static_cast<uint8_t>(index);
  saveRaw();
  Serial.printf("Location: %s (%.4f, %.4f)\n", item->name, item->lat, item->lon);
  return true;
}

bool selectByName(const char* name) {
  if (name == nullptr || name[0] == '\0') {
    return false;
  }
  for (size_t i = 0; i < s_list.count; ++i) {
    if (strcmp(s_list.items[i].name, name) == 0) {
      return select(i);
    }
  }
  return false;
}

bool saveFromPortalLines(const char* const* lines, size_t line_count, char* err,
                         size_t err_size) {
  char joined[kRawTextSize];
  size_t used = 0;
  joined[0] = '\0';
  for (size_t i = 0; i < line_count; ++i) {
    const char* line = (lines[i] != nullptr) ? lines[i] : "";
    if (line[0] == '\0') {
      continue;  // blank field, not a blank location
    }
    const int written =
        snprintf(joined + used, sizeof(joined) - used, "%s\n", line);
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(joined) - used) {
      break;  // out of room; keep what fitted
    }
    used += static_cast<size_t>(written);
  }
  return saveFromPortal(joined, err, err_size);
}

bool saveFromPortal(const char* text, char* err, size_t err_size) {
  if (text == nullptr) {
    text = "";
  }
  strncpy(s_raw, text, sizeof(s_raw) - 1);
  s_raw[sizeof(s_raw) - 1] = '\0';

  parseLocationList(s_raw, &s_list, err, err_size);
  if (s_selected != kNoSelection && s_selected >= s_list.count) {
    s_selected = kNoSelection;
  }
  ensureSelection();
  saveRaw();
  if (err[0] != '\0') {
    Serial.printf("Locations: %s\n", err);
    return false;
  }
  return true;
}

void clear() {
  s_raw[0] = '\0';
  s_list.count = 0;
  s_selected = kNoSelection;
  if (s_prefs.begin(kPrefsNamespace, false)) {
    s_prefs.remove(kPrefsListKey);
    s_prefs.remove(kPrefsSelectedKey);
    s_prefs.end();
  }
}

}  // namespace services::locations
