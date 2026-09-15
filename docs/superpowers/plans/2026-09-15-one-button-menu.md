# One-Button Menu System Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the BOOT button's two-gesture scheme with a navigable on-device menu covering range, altitude format, radar centre and network details, backed by a portal-managed list of up to 8 named locations.

**Architecture:** Two pure, natively-testable modules (a location-list parser and a menu navigation model) sit under two Arduino-dependent modules (NVS persistence and LGFX rendering). This split exists so the fiddly logic — comma parsing and scroll-window arithmetic — gets real unit tests, which the repo has never had. The button gains a hold gesture; the menu owns the button once the radar is up.

**Tech Stack:** PlatformIO, Arduino-ESP32 (ESP32-C3), LovyanGFX, WiFiManager, Preferences (NVS), Unity (new, native test env only).

**Spec:** `docs/superpowers/specs/2026-09-15-one-button-menu-design.md`

## Global Constraints

- Target board is `esp32-c3-devkitm-1`; every firmware change must pass `pio run -e supermini`.
- No dynamic allocation in firmware paths. Fixed arrays only.
- C++17 (`-std=gnu++17`), matching existing `build_flags`.
- Existing NVS namespace is `planeradar`. NVS keys are **15 characters maximum**.
- Maximum 8 locations; location names are **14 visible characters** plus NUL.
- Hold threshold `kMenuHoldMs` = 600 ms. Menu idle timeout `kMenuIdleMs` = 15000 ms.
- Menu shows **5 rows** at a time.
- Follow existing naming: `kConstantName`, `s_static_var`, `g_global_var`, functions `lowerCamelCase`.
- This repo has **no host build and no existing tests**. Tasks 1 and 3 add a `native` PlatformIO env for pure logic only. Everything else is verified with `pio run` plus the stated manual hardware check.

---

## File Structure

**New — pure, no Arduino dependency (native-testable):**
- `include/services/location_list.h` / `src/services/location_list.cpp` — `Location`, `LocationList`, `parseLocationList()`. Owns the `name, lat, lon` text format.
- `include/ui/menu_model.h` / `src/ui/menu_model.cpp` — `Window` (scroll state) and the navigation arithmetic.

**New — Arduino-dependent:**
- `include/services/locations.h` / `src/services/locations.cpp` — NVS load/save of the raw list text, the selected index, and applying a selection to the radar centre.
- `include/ui/menu.h` / `src/ui/menu.cpp` — page table, rendering, idle timeout, and the activation handlers.

**Modified:**
- `platformio.ini` — add `[env:native]`.
- `include/config.h` — `kMenuHoldMs`, `kMenuIdleMs`.
- `include/services/wifi_setup.h` / `src/services/wifi_setup.cpp` — hold gesture; locations textarea in the portal.
- `include/services/radar_location.h` / `src/services/radar_location.cpp` — numeric `saveCoords()`.
- `include/ui/radar_range.h` / `src/ui/radar_range.cpp` — `rangeSetIndex()`, `setFlightLevels()`, extended `unitsReset()`.
- `src/main.cpp` — route button events to the menu; stop calling `bootButtonPollLongPress()`.
- `README.md` — controls table and settings docs.

---

### Task 1: Native test environment and the location-list parser

**Files:**
- Modify: `platformio.ini`
- Create: `include/services/location_list.h`
- Create: `src/services/location_list.cpp`
- Test: `test/test_location_list/test_location_list.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `services::kMaxLocations` (8), `services::kLocationNameSize` (15), `struct services::Location { char name[15]; double lat; double lon; }`, `struct services::LocationList { Location items[8]; size_t count; }`, and `size_t services::parseLocationList(const char* text, LocationList* out, char* err, size_t err_size)`.

- [ ] **Step 1: Add the native test environment**

Append to `platformio.ini`. `build_src_filter` is the important part — it stops PlatformIO trying to compile Arduino-dependent sources for the host.

```ini

; Host-side unit tests for pure logic only: `pio test -e native`
[env:native]
platform = native
test_framework = unity
build_flags =
  -std=gnu++17
  -I include
build_src_filter =
  +<services/location_list.cpp>
```

- [ ] **Step 2: Write the failing test**

Create `test/test_location_list/test_location_list.cpp`:

```cpp
#include <unity.h>

#include <cstring>

#include "services/location_list.h"

using services::LocationList;
using services::parseLocationList;

void setUp(void) {}
void tearDown(void) {}

void test_parses_three_fields(void) {
  LocationList list;
  char err[96];
  const size_t n = parseLocationList("Home, 52.3676, 4.9041", &list, err, sizeof(err));
  TEST_ASSERT_EQUAL_UINT(1, n);
  TEST_ASSERT_EQUAL_STRING("Home", list.items[0].name);
  TEST_ASSERT_EQUAL_DOUBLE(52.3676, list.items[0].lat);
  TEST_ASSERT_EQUAL_DOUBLE(4.9041, list.items[0].lon);
  TEST_ASSERT_EQUAL_STRING("", err);
}

void test_parses_multiple_lines_and_negatives(void) {
  LocationList list;
  char err[96];
  const size_t n = parseLocationList(
      "Home, 52.3676, 4.9041\nGatwick, 51.1537, -0.1821\n", &list, err, sizeof(err));
  TEST_ASSERT_EQUAL_UINT(2, n);
  TEST_ASSERT_EQUAL_STRING("Gatwick", list.items[1].name);
  TEST_ASSERT_EQUAL_DOUBLE(-0.1821, list.items[1].lon);
}

void test_skips_blank_lines(void) {
  LocationList list;
  char err[96];
  const size_t n = parseLocationList("\n\nHome, 1, 2\n\n", &list, err, sizeof(err));
  TEST_ASSERT_EQUAL_UINT(1, n);
}

void test_name_may_contain_a_comma(void) {
  LocationList list;
  char err[96];
  parseLocationList("Paris, France, 48.85, 2.35", &list, err, sizeof(err));
  TEST_ASSERT_EQUAL_STRING("Paris, France", list.items[0].name);
  TEST_ASSERT_EQUAL_DOUBLE(48.85, list.items[0].lat);
}

void test_truncates_long_names_to_14_chars(void) {
  LocationList list;
  char err[96];
  parseLocationList("AbcdefghijklmnopQRS, 1, 2", &list, err, sizeof(err));
  TEST_ASSERT_EQUAL_STRING("Abcdefghijklmn", list.items[0].name);
  TEST_ASSERT_EQUAL_UINT(14, strlen(list.items[0].name));
}

void test_skips_malformed_line_but_keeps_the_rest(void) {
  LocationList list;
  char err[96];
  const size_t n =
      parseLocationList("Home, 1, 2\nbroken line\nGatwick, 3, 4", &list, err, sizeof(err));
  TEST_ASSERT_EQUAL_UINT(2, n);
  TEST_ASSERT_EQUAL_STRING("Gatwick", list.items[1].name);
  TEST_ASSERT_TRUE(strstr(err, "line 2") != nullptr);
}

void test_rejects_out_of_range_coordinates(void) {
  LocationList list;
  char err[96];
  const size_t n = parseLocationList("Bad, 91.0, 0\nGood, 10, 20", &list, err, sizeof(err));
  TEST_ASSERT_EQUAL_UINT(1, n);
  TEST_ASSERT_EQUAL_STRING("Good", list.items[0].name);
  TEST_ASSERT_TRUE(strstr(err, "line 1") != nullptr);
}

void test_rejects_trailing_junk_in_a_number(void) {
  LocationList list;
  char err[96];
  const size_t n = parseLocationList("Bad, 12abc, 0", &list, err, sizeof(err));
  TEST_ASSERT_EQUAL_UINT(0, n);
}

void test_caps_at_eight_locations(void) {
  LocationList list;
  char err[96];
  const char* text =
      "a, 1, 1\nb, 1, 1\nc, 1, 1\nd, 1, 1\ne, 1, 1\n"
      "f, 1, 1\ng, 1, 1\nh, 1, 1\ni, 1, 1\n";
  const size_t n = parseLocationList(text, &list, err, sizeof(err));
  TEST_ASSERT_EQUAL_UINT(8, n);
  TEST_ASSERT_TRUE(strstr(err, "8") != nullptr);
}

void test_empty_input_yields_no_locations(void) {
  LocationList list;
  char err[96];
  TEST_ASSERT_EQUAL_UINT(0, parseLocationList("", &list, err, sizeof(err)));
  TEST_ASSERT_EQUAL_UINT(0, parseLocationList(nullptr, &list, err, sizeof(err)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_parses_three_fields);
  RUN_TEST(test_parses_multiple_lines_and_negatives);
  RUN_TEST(test_skips_blank_lines);
  RUN_TEST(test_name_may_contain_a_comma);
  RUN_TEST(test_truncates_long_names_to_14_chars);
  RUN_TEST(test_skips_malformed_line_but_keeps_the_rest);
  RUN_TEST(test_rejects_out_of_range_coordinates);
  RUN_TEST(test_rejects_trailing_junk_in_a_number);
  RUN_TEST(test_caps_at_eight_locations);
  RUN_TEST(test_empty_input_yields_no_locations);
  return UNITY_END();
}
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `pio test -e native`
Expected: FAIL — compilation error, `services/location_list.h: No such file or directory`.

- [ ] **Step 4: Write the header**

Create `include/services/location_list.h`:

```cpp
#pragma once

#include <cstddef>

namespace services {

constexpr size_t kMaxLocations = 8;
/** 14 visible characters plus NUL. */
constexpr size_t kLocationNameSize = 15;

struct Location {
  char name[kLocationNameSize];
  double lat;
  double lon;
};

struct LocationList {
  Location items[kMaxLocations];
  size_t count;
};

/**
 * Parse "name, lat, lon" lines into out.
 *
 * The last two commas on a line are the separators, so names may contain
 * commas. Blank lines are skipped. A malformed or out-of-range line is
 * skipped and described in err; other lines still parse. At most
 * kMaxLocations are kept.
 *
 * Returns the number of locations parsed. err receives the first problem
 * encountered, or an empty string if there was none.
 */
size_t parseLocationList(const char* text, LocationList* out, char* err,
                         size_t err_size);

}  // namespace services
```

- [ ] **Step 5: Write the implementation**

Create `src/services/location_list.cpp`:

```cpp
#include "services/location_list.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace services {

namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r'; }

const char* skipSpaces(const char* p, const char* end) {
  while (p < end && isSpace(*p)) {
    ++p;
  }
  return p;
}

/** Copy [begin,end) with surrounding spaces trimmed, truncated to size-1. */
void copyTrimmed(const char* begin, const char* end, char* out, size_t size) {
  begin = skipSpaces(begin, end);
  while (end > begin && isSpace(end[-1])) {
    --end;
  }
  size_t n = static_cast<size_t>(end - begin);
  if (n > size - 1) {
    n = size - 1;
  }
  memcpy(out, begin, n);
  out[n] = '\0';
}

/** strtod over a trimmed copy. False on an empty field or trailing junk. */
bool parseDouble(const char* begin, const char* end, double* out) {
  char buf[32];
  copyTrimmed(begin, end, buf, sizeof(buf));
  if (buf[0] == '\0') {
    return false;
  }
  char* tail = nullptr;
  const double value = strtod(buf, &tail);
  if (tail == buf) {
    return false;
  }
  while (*tail != '\0' && isSpace(*tail)) {
    ++tail;
  }
  if (*tail != '\0') {
    return false;
  }
  *out = value;
  return true;
}

/** Rightmost occurrence of c in [begin,end), or nullptr. */
const char* findLast(const char* begin, const char* end, char c) {
  for (const char* p = end; p > begin; --p) {
    if (p[-1] == c) {
      return p - 1;
    }
  }
  return nullptr;
}

void setError(char* err, size_t err_size, const char* fmt, int line_no) {
  if (err == nullptr || err_size == 0 || err[0] != '\0') {
    return;  // keep the first error only
  }
  snprintf(err, err_size, fmt, line_no);
}

}  // namespace

size_t parseLocationList(const char* text, LocationList* out, char* err,
                         size_t err_size) {
  out->count = 0;
  if (err != nullptr && err_size > 0) {
    err[0] = '\0';
  }
  if (text == nullptr) {
    return 0;
  }

  int line_no = 0;
  const char* p = text;
  while (*p != '\0') {
    const char* nl = strchr(p, '\n');
    const char* line_end = (nl != nullptr) ? nl : p + strlen(p);
    ++line_no;

    const char* start = skipSpaces(p, line_end);
    if (start < line_end) {
      if (out->count >= kMaxLocations) {
        setError(err, err_size, "Only 8 locations kept; line %d onwards dropped",
                 line_no);
        break;
      }

      const char* lon_comma = findLast(start, line_end, ',');
      const char* lat_comma =
          (lon_comma != nullptr) ? findLast(start, lon_comma, ',') : nullptr;

      double lat = 0.0;
      double lon = 0.0;
      if (lat_comma == nullptr ||
          !parseDouble(lat_comma + 1, lon_comma, &lat) ||
          !parseDouble(lon_comma + 1, line_end, &lon)) {
        setError(err, err_size, "Skipped line %d: expected \"name, lat, lon\"",
                 line_no);
      } else if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) {
        setError(err, err_size,
                 "Skipped line %d: lat must be -90..90, lon -180..180", line_no);
      } else {
        Location* item = &out->items[out->count];
        copyTrimmed(start, lat_comma, item->name, kLocationNameSize);
        if (item->name[0] == '\0') {
          setError(err, err_size, "Skipped line %d: name is empty", line_no);
        } else {
          item->lat = lat;
          item->lon = lon;
          ++out->count;
        }
      }
    }

    if (nl == nullptr) {
      break;
    }
    p = nl + 1;
  }

  return out->count;
}

}  // namespace services
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `pio test -e native`
Expected: PASS — 10 tests, 0 failures.

- [ ] **Step 7: Verify the firmware build is untouched**

Run: `pio run -e supermini`
Expected: SUCCESS. `location_list.cpp` compiles into the firmware but nothing calls it yet.

- [ ] **Step 8: Commit**

```bash
git add platformio.ini include/services/location_list.h src/services/location_list.cpp test/
git commit -m "Add native test env and the location list parser"
```

---

### Task 2: Location store, portal textarea, and setting setters

**Files:**
- Create: `include/services/locations.h`
- Create: `src/services/locations.cpp`
- Modify: `include/services/radar_location.h`, `src/services/radar_location.cpp`
- Modify: `include/ui/radar_range.h`, `src/ui/radar_range.cpp`
- Modify: `src/services/wifi_setup.cpp`

**Interfaces:**
- Consumes: `services::parseLocationList()`, `services::LocationList` from Task 1.
- Produces: `services::locations::init()`, `count()`, `at(size_t)`, `selectedIndex()`, `select(size_t)`, `rawText()`, `saveFromPortal(const char*, char*, size_t)`, `clear()`, and `kNoSelection` (`0xFF`). Also `services::location::saveCoords(double, double)`, `ui::radar::rangeSetIndex(uint8_t)`, `ui::radar::setFlightLevels(bool)`.

- [ ] **Step 1: Add a numeric coordinate save**

In `include/services/radar_location.h`, add above `saveFromStrings`:

```cpp
/** Validate, persist to NVS, and update runtime values. */
bool saveCoords(double lat, double lon);
```

In `src/services/radar_location.cpp`, extract the existing persistence out of `saveFromStrings()` into `saveCoords()`, then have `saveFromStrings()` parse its two strings and delegate. Keep the existing validation bounds. The existing NVS keys (`kKeyLat`, `kKeyLon`) do not change.

- [ ] **Step 2: Add the range and altitude setters**

In `include/ui/radar_range.h`, below `rangeNext()`:

```cpp
/** Set the preset by index and save. Out-of-range values are ignored. */
void rangeSetIndex(uint8_t index);
/** Set the altitude tag format and save. */
void setFlightLevels(bool on);
```

In `src/ui/radar_range.cpp`:

```cpp
void rangeSetIndex(uint8_t index) {
  if (index >= kRangePresetCount) {
    return;
  }
  s_range_index = index;
  saveRangeIndex();
}

void setFlightLevels(bool on) {
  s_flight_levels = on;
  saveFlightLevels();
  Serial.printf("Altitude tags: %s\n", s_flight_levels ? "flight levels" : "feet");
}
```

- [ ] **Step 3: Write the locations header**

Create `include/services/locations.h`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

#include "services/location_list.h"

namespace services::locations {

constexpr uint8_t kNoSelection = 0xFF;

/** Load the saved list and selection from NVS. Call once after boot. */
void init();

size_t count();
/** Null when index is out of range. */
const Location* at(size_t index);

/** kNoSelection when nothing is selected or the list is empty. */
uint8_t selectedIndex();
/** Apply a location as the radar centre and remember the choice. */
bool select(size_t index);

/** The stored list text, for prefilling the portal field. Never null. */
const char* rawText();

/**
 * Validate and persist portal text. Always stores whatever parsed, so one
 * bad line does not lose the others. err receives the first problem, or an
 * empty string. Returns false when err was written.
 */
bool saveFromPortal(const char* text, char* err, size_t err_size);

/** Erase the list and selection (used by the Wi-Fi reset). */
void clear();

}  // namespace services::locations
```

- [ ] **Step 4: Write the locations implementation**

Create `src/services/locations.cpp`:

```cpp
#include "services/locations.h"

#include <Arduino.h>
#include <Preferences.h>

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

const char* rawText() { return s_raw; }

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
```

- [ ] **Step 5: Clear locations on Wi-Fi reset**

Locations live in their own module, so add the call to `resetWifiCredentials()` in `src/services/wifi_setup.cpp` — the existing function that already clears location and units. Add `#include "services/locations.h"` at the top of that file, then:

```cpp
void resetWifiCredentials() {
  markForceConfigPortal();
  eraseWifiCredentials();
  services::location::clear();
  services::locations::clear();
  ui::radar::unitsReset();
  Serial.println("WiFi credentials, location, and units cleared");
}
```

- [ ] **Step 6: Add the portal textarea**

In `src/services/wifi_setup.cpp`, beside the existing parameters:

```cpp
constexpr int kLocationsParamLen = 512;
constexpr char kLocationsAttrs[] =
    " placeholder=\"Home, 52.3676, 4.9041\" rows=\"8\" style=\"width:100%\"";
WiFiManagerParameter s_param_locations(
    "locations", "Saved locations — one per line: name, lat, lon", "",
    kLocationsParamLen, kLocationsAttrs);
```

In `refreshPortalParamDefaults()`:

```cpp
s_param_locations.setValue(services::locations::rawText(), kLocationsParamLen);
```

In `onPortalParamsSaved()`:

```cpp
char loc_err[96];
services::locations::saveFromPortal(s_param_locations.getValue(), loc_err,
                                    sizeof(loc_err));
```

In `attachPortalParams()`:

```cpp
wm.addParameter(&s_param_locations);
```

- [ ] **Step 7: Initialise the store at boot**

In `src/main.cpp` `setup()`, immediately after `services::location::init();`:

```cpp
services::locations::init();
```

Add `#include "services/locations.h"`.

- [ ] **Step 8: Build**

Run: `pio run -e supermini`
Expected: SUCCESS.

- [ ] **Step 9: Verify on hardware**

Flash, open the portal, paste three locations, save. Reopen the portal and confirm the text comes back. Paste a line with a bad latitude and confirm the other lines survive and the serial log names the bad line.

- [ ] **Step 10: Commit**

```bash
git add include/services/locations.h src/services/locations.cpp \
        include/services/radar_location.h src/services/radar_location.cpp \
        include/ui/radar_range.h src/ui/radar_range.cpp \
        src/services/wifi_setup.cpp src/main.cpp
git commit -m "Add the portal-managed location store"
```

---

### Task 3: Menu navigation model

**Files:**
- Create: `include/ui/menu_model.h`
- Create: `src/ui/menu_model.cpp`
- Test: `test/test_menu_model/test_menu_model.cpp`
- Modify: `platformio.ini`

**Interfaces:**
- Consumes: nothing.
- Produces: `ui::menu::kVisibleRows` (5), `struct ui::menu::Window { size_t first; size_t highlight; }`, `ui::menu::windowFocus(size_t row_count, size_t index, Window* w)`, `ui::menu::windowNext(size_t row_count, Window* w)`.

- [ ] **Step 1: Add menu_model to the native env**

In `platformio.ini`, extend the native env's filter:

```ini
build_src_filter =
  +<services/location_list.cpp>
  +<ui/menu_model.cpp>
```

- [ ] **Step 2: Write the failing test**

Create `test/test_menu_model/test_menu_model.cpp`:

```cpp
#include <unity.h>

#include "ui/menu_model.h"

using ui::menu::Window;
using ui::menu::windowFocus;
using ui::menu::windowNext;

void setUp(void) {}
void tearDown(void) {}

void test_next_moves_highlight_without_scrolling_short_lists(void) {
  Window w{0, 0};
  windowNext(3, &w);
  TEST_ASSERT_EQUAL_UINT(1, w.highlight);
  TEST_ASSERT_EQUAL_UINT(0, w.first);
}

void test_next_wraps_to_top_and_resets_the_window(void) {
  Window w{4, 8};
  windowNext(9, &w);
  TEST_ASSERT_EQUAL_UINT(0, w.highlight);
  TEST_ASSERT_EQUAL_UINT(0, w.first);
}

void test_window_scrolls_by_one_past_the_last_visible_row(void) {
  Window w{0, 4};  // last visible row of a 5-row window
  windowNext(9, &w);
  TEST_ASSERT_EQUAL_UINT(5, w.highlight);
  TEST_ASSERT_EQUAL_UINT(1, w.first);
}

void test_window_never_scrolls_past_the_end(void) {
  Window w{0, 0};
  for (int i = 0; i < 8; ++i) {
    windowNext(9, &w);
  }
  TEST_ASSERT_EQUAL_UINT(8, w.highlight);
  TEST_ASSERT_EQUAL_UINT(4, w.first);  // 9 rows - 5 visible
}

void test_focus_scrolls_to_reveal_a_later_row(void) {
  Window w{0, 0};
  windowFocus(9, 7, &w);
  TEST_ASSERT_EQUAL_UINT(7, w.highlight);
  TEST_ASSERT_EQUAL_UINT(3, w.first);
}

void test_focus_on_a_short_list_keeps_the_window_at_zero(void) {
  Window w{2, 2};
  windowFocus(4, 3, &w);
  TEST_ASSERT_EQUAL_UINT(3, w.highlight);
  TEST_ASSERT_EQUAL_UINT(0, w.first);
}

void test_focus_clamps_an_out_of_range_index(void) {
  Window w{0, 0};
  windowFocus(3, 99, &w);
  TEST_ASSERT_EQUAL_UINT(2, w.highlight);
}

void test_empty_list_is_safe(void) {
  Window w{3, 3};
  windowFocus(0, 0, &w);
  TEST_ASSERT_EQUAL_UINT(0, w.highlight);
  TEST_ASSERT_EQUAL_UINT(0, w.first);
  windowNext(0, &w);
  TEST_ASSERT_EQUAL_UINT(0, w.highlight);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_next_moves_highlight_without_scrolling_short_lists);
  RUN_TEST(test_next_wraps_to_top_and_resets_the_window);
  RUN_TEST(test_window_scrolls_by_one_past_the_last_visible_row);
  RUN_TEST(test_window_never_scrolls_past_the_end);
  RUN_TEST(test_focus_scrolls_to_reveal_a_later_row);
  RUN_TEST(test_focus_on_a_short_list_keeps_the_window_at_zero);
  RUN_TEST(test_focus_clamps_an_out_of_range_index);
  RUN_TEST(test_empty_list_is_safe);
  return UNITY_END();
}
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `pio test -e native -f test_menu_model`
Expected: FAIL — `ui/menu_model.h: No such file or directory`.

- [ ] **Step 4: Write the header**

Create `include/ui/menu_model.h`:

```cpp
#pragma once

#include <cstddef>

namespace ui::menu {

/** Rows visible at once on the 240x240 round display. */
constexpr size_t kVisibleRows = 5;

/** Scroll state for one list: which row is first on screen, which is highlighted. */
struct Window {
  size_t first = 0;
  size_t highlight = 0;
};

/** Highlight `index` (clamped), scrolling the window the minimum needed. */
void windowFocus(size_t row_count, size_t index, Window* w);

/** Move the highlight down one row. Wrapping resets the window to the top. */
void windowNext(size_t row_count, Window* w);

}  // namespace ui::menu
```

- [ ] **Step 5: Write the implementation**

Create `src/ui/menu_model.cpp`:

```cpp
#include "ui/menu_model.h"

namespace ui::menu {

void windowFocus(size_t row_count, size_t index, Window* w) {
  if (row_count == 0) {
    w->first = 0;
    w->highlight = 0;
    return;
  }
  if (index >= row_count) {
    index = row_count - 1;
  }
  w->highlight = index;

  if (row_count <= kVisibleRows) {
    w->first = 0;
    return;
  }
  if (index < w->first) {
    w->first = index;
  } else if (index >= w->first + kVisibleRows) {
    w->first = index - kVisibleRows + 1;
  }
  const size_t max_first = row_count - kVisibleRows;
  if (w->first > max_first) {
    w->first = max_first;
  }
}

void windowNext(size_t row_count, Window* w) {
  if (row_count == 0) {
    w->first = 0;
    w->highlight = 0;
    return;
  }
  const size_t next = w->highlight + 1;
  if (next >= row_count) {
    w->first = 0;
    w->highlight = 0;
    return;
  }
  windowFocus(row_count, next, w);
}

}  // namespace ui::menu
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `pio test -e native`
Expected: PASS — both suites, 18 tests total, 0 failures.

- [ ] **Step 7: Commit**

```bash
git add platformio.ini include/ui/menu_model.h src/ui/menu_model.cpp test/test_menu_model/
git commit -m "Add the menu navigation model"
```

---

### Task 4: Hold gesture on the BOOT button

**Files:**
- Modify: `include/config.h`
- Modify: `include/services/wifi_setup.h`, `src/services/wifi_setup.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `config::kMenuHoldMs` (600), `config::kMenuIdleMs` (15000), `bootButtonPollHold()`, `bootButtonConsumeHold()`. Narrows the existing `bootButtonConsumeTap()` to presses shorter than `kMenuHoldMs`.

- [ ] **Step 1: Add the constants**

In `include/config.h`, after `kBootTapMinMs`:

```cpp
/** Hold this long to activate a menu row. Shorter presses are clicks. */
constexpr unsigned long kMenuHoldMs = 600UL;
/** Close the menu after this long with no button activity. */
constexpr unsigned long kMenuIdleMs = 15000UL;
```

- [ ] **Step 2: Declare the new button API**

In `include/services/wifi_setup.h`, after `bootButtonConsumeTap()`:

```cpp
/** Call each loop iteration; latches a hold once the press passes kMenuHoldMs. */
void bootButtonPollHold();
/** Latched hold (fires once per press, while the button is still down). */
bool bootButtonConsumeHold();
```

- [ ] **Step 3: Add the ISR state**

In `src/services/wifi_setup.cpp`, beside the existing button globals:

```cpp
volatile bool s_boot_hold_pending = false;
volatile bool s_boot_hold_fired = false;
```

- [ ] **Step 4: Narrow the tap window in the ISR**

Replace the release branch of `onBootButtonIsr()`:

```cpp
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
```

- [ ] **Step 5: Add the poll and consumer**

In `src/services/wifi_setup.cpp`, next to `bootButtonConsumeTap()`:

```cpp
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
```

- [ ] **Step 6: Build**

Run: `pio run -e supermini`
Expected: SUCCESS. Nothing calls the new functions yet, so behaviour is unchanged except that presses longer than 600 ms no longer cycle the range.

- [ ] **Step 7: Verify on hardware**

Flash. Confirm a quick tap still cycles the range and a 1-second press now does nothing. Confirm holding 3 seconds still resets Wi-Fi (`bootButtonPollLongPress()` is still wired up at this point).

- [ ] **Step 8: Commit**

```bash
git add include/config.h include/services/wifi_setup.h src/services/wifi_setup.cpp
git commit -m "Add a hold gesture to the BOOT button"
```

---

### Task 5: Menu rendering with Root, Range and Altitude

**Files:**
- Create: `include/ui/menu.h`, `src/ui/menu.cpp`
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `ui::menu::Window`, `windowFocus()`, `windowNext()` (Task 3); `bootButtonPollHold()`, `bootButtonConsumeHold()` (Task 4); `ui::radar::rangeSetIndex()`, `setFlightLevels()` (Task 2).
- Produces: `ui::menu::isOpen()`, `open()`, `close()`, `handleClick()`, `handleHold()`, `tick()`.

- [ ] **Step 1: Write the header**

Create `include/ui/menu.h`:

```cpp
#pragma once

namespace ui::menu {

bool isOpen();
/** Draw the root page and take over the button. */
void open();
/** Leave the menu. The caller redraws the radar. */
void close();
/** Move the highlight down one row and redraw. */
void handleClick();
/** Activate the highlighted row. */
void handleHold();
/** Call each loop iteration while open; closes the menu on idle timeout. */
void tick();

}  // namespace ui::menu
```

- [ ] **Step 2: Write the renderer and the first three pages**

Create `src/ui/menu.cpp`. Pages are a table, so adding a setting later is a table entry rather than new control flow. `Location`, `Network` and `Reset` are stubbed here and filled in by Tasks 6 and 7.

```cpp
#include "ui/menu.h"

#include <Arduino.h>
#include <lgfx/v1/lgfx_fonts.hpp>

#include <cstdio>
#include <cstring>

#include "config.h"
#include "hardware/display.h"
#include "hardware/display_font.h"
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

constexpr auto& kGfxRow = fonts::FreeSans9pt7b;
constexpr float kRowVlw = 0.95f;

constexpr int kCenterX = config::kDisplayWidth / 2;
constexpr int kTitleY = 30;
constexpr int kFirstRowY = 58;
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
void activateRow(size_t index);

void draw() {
  tft.fillScreen(kColorBg);
  applyRowStyle();

  tft.setTextDatum(textdatum_t::middle_center);
  tft.setTextColor(kColorTitle, kColorBg);
  tft.drawString(pageTitle(), kCenterX, kTitleY);

  const size_t total = rowCount();
  const size_t last = (total < s_window.first + kVisibleRows)
                          ? total
                          : s_window.first + kVisibleRows;

  for (size_t i = s_window.first; i < last; ++i) {
    const int slot = static_cast<int>(i - s_window.first);
    const int y = kFirstRowY + slot * kRowHeight;
    const bool selected = (i == s_window.highlight);

    if (selected) {
      tft.fillRoundRect(kRowInsetX - 8, y - kRowHeight / 2 + 2,
                        config::kDisplayWidth - 2 * (kRowInsetX - 8),
                        kRowHeight - 4, 4, kColorHighlight);
    }

    char label[32];
    char value[32];
    rowLabel(i, label, sizeof(label));
    rowValue(i, value, sizeof(value));

    tft.setTextColor(selected ? kColorText : kColorText, selected ? kColorHighlight : kColorBg);
    tft.setTextDatum(textdatum_t::middle_left);
    tft.drawString(label, kRowInsetX, y);

    if (value[0] != '\0') {
      const bool tick = rowIsCurrent(i);
      tft.setTextColor(tick ? kColorTick : kColorValue,
                       selected ? kColorHighlight : kColorBg);
      tft.setTextDatum(textdatum_t::middle_right);
      tft.drawString(value, config::kDisplayWidth - kRowInsetX, y);
    }
  }

  // Scroll cues: only when rows exist outside the window.
  tft.setTextDatum(textdatum_t::middle_center);
  tft.setTextColor(kColorValue, kColorBg);
  if (s_window.first > 0) {
    tft.drawString("^", kCenterX, kFirstRowY - kRowHeight / 2 - 4);
  }
  if (last < total) {
    tft.drawString("v", kCenterX,
                   kFirstRowY + static_cast<int>(kVisibleRows) * kRowHeight - 8);
  }
}

void goTo(Page page, size_t focus_index) {
  s_page = page;
  s_window = Window{};
  windowFocus(rowCount(), focus_index, &s_window);
  draw();
}

// ---- Page data -------------------------------------------------------------

constexpr size_t kRootRowCount = 6;
const char* const kRootLabels[kRootRowCount] = {
    "Range", "Altitude", "Location", "Network", "Reset Wi-Fi", "Exit"};

constexpr size_t kAltitudeRowCount = 3;  // Flight levels, Feet, Up

size_t rowCount() {
  switch (s_page) {
    case Page::Root:
      return kRootRowCount;
    case Page::Range:
      return kRangePresetCount + 1;  // presets plus Up
    case Page::Altitude:
      return kAltitudeRowCount;
    default:
      return 1;  // Tasks 6 and 7 replace these
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
  switch (s_page) {
    case Page::Root:
      snprintf(out, out_size, "%s", kRootLabels[index]);
      return;
    case Page::Range:
      if (index < kRangePresetCount) {
        radar::formatRing3Label(out, out_size, kRangePresets[index].ring3_km,
                                radar::useMiles());
      } else {
        snprintf(out, out_size, "%s", "^ Up");
      }
      return;
    case Page::Altitude:
      if (index == 0) {
        snprintf(out, out_size, "%s", "Flight levels");
      } else if (index == 1) {
        snprintf(out, out_size, "%s", "Feet");
      } else {
        snprintf(out, out_size, "%s", "^ Up");
      }
      return;
    default:
      snprintf(out, out_size, "%s", "^ Up");
      return;
  }
}

void rowValue(size_t index, char* out, size_t out_size) {
  out[0] = '\0';
  if (s_page == Page::Root) {
    switch (index) {
      case 0:
        radar::formatCurrentRing3Label(out, out_size);
        return;
      case 1:
        snprintf(out, out_size, "%s", radar::flightLevels() ? "FL" : "ft");
        return;
      default:
        return;
    }
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
      return (index == 0) == radar::flightLevels() && index < 2;
    default:
      return false;
  }
}

void activateRow(size_t index) {
  switch (s_page) {
    case Page::Root:
      switch (index) {
        case 0: goTo(Page::Range, radar::rangeIndex()); return;
        case 1: goTo(Page::Altitude, radar::flightLevels() ? 0 : 1); return;
        case 2: goTo(Page::Location, 0); return;
        case 3: goTo(Page::Network, 0); return;
        case 4: goTo(Page::Reset, 0); return;
        default: close(); return;
      }
    case Page::Range:
      if (index < kRangePresetCount) {
        radar::rangeSetIndex(static_cast<uint8_t>(index));
        draw();  // apply and stay on the page
      } else {
        goTo(Page::Root, 0);
      }
      return;
    case Page::Altitude:
      if (index < 2) {
        radar::setFlightLevels(index == 0);
        draw();
      } else {
        goTo(Page::Root, 1);
      }
      return;
    default:
      goTo(Page::Root, 0);
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
```

Note: `kRangePresets`, `kRangePresetCount`, `formatRing3Label()` and `useMiles()` already exist in `ui/radar_range.h`.

- [ ] **Step 3: Route the button to the menu**

In `src/main.cpp`, add `#include "ui/menu.h"`, delete `onRangeTap()` (range now lives in the menu), and replace `handleBootButton()`:

```cpp
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

  if (bootButtonConsumeTap() || bootButtonConsumeHold()) {
    g_radar_visible = false;  // radar redraws when the menu closes
    ui::menu::open();
  }
}
```

Note `bootButtonPollLongPress()` is no longer called here — the runtime Wi-Fi reset moves into the menu in Task 7. It remains live inside `wifi_setup.cpp`'s connect and portal loops, so holding BOOT at power-on still wipes credentials.

- [ ] **Step 4: Pause the radar while the menu is open**

In `src/main.cpp` `loop()`, directly after `wifiLoop();`:

```cpp
  if (ui::menu::isOpen()) {
    ui::menu::tick();
    delay(10);
    return;  // ADS-B paused so a fetch cannot stall a button press
  }
```

- [ ] **Step 5: Build**

Run: `pio run -e supermini`
Expected: SUCCESS.

- [ ] **Step 6: Verify on hardware**

Flash. Confirm: a click from the radar opens the menu; a hold from the radar also opens it; clicking moves the highlight and wraps past Exit; the root page scrolls with the `^`/`v` cues since it has six rows; Range and Altitude open on the current value, apply immediately and stay on the page; `^ Up` returns to root; Exit returns to the radar; leaving it alone for 15 seconds returns to the radar.

- [ ] **Step 7: Commit**

```bash
git add include/ui/menu.h src/ui/menu.cpp src/main.cpp
git commit -m "Add the on-device menu with range and altitude pages"
```

---

### Task 6: Location and Network pages

**Files:**
- Modify: `src/ui/menu.cpp`

**Interfaces:**
- Consumes: `services::locations::count()`, `at()`, `selectedIndex()`, `select()` (Task 2).
- Produces: nothing new; fills in the `Page::Location` and `Page::Network` branches.

- [ ] **Step 1: Add the includes**

At the top of `src/ui/menu.cpp`:

```cpp
#include <WiFi.h>

#include "services/locations.h"
```

- [ ] **Step 2: Give the two pages their row counts**

Replace the `default:` arm of `rowCount()`:

```cpp
    case Page::Location: {
      const size_t n = services::locations::count();
      return (n == 0 ? 1 : n) + 1;  // entries (or the empty notice) plus Up
    }
    case Page::Network:
      return 1;  // Up; SSID and IP are drawn as static text
    default:
      return 1;
```

- [ ] **Step 3: Give them labels**

Add to `rowLabel()` before `default:`:

```cpp
    case Page::Location: {
      const size_t n = services::locations::count();
      if (n == 0) {
        snprintf(out, out_size, "%s", index == 0 ? "No locations set" : "^ Up");
        return;
      }
      if (index < n) {
        snprintf(out, out_size, "%s", services::locations::at(index)->name);
      } else {
        snprintf(out, out_size, "%s", "^ Up");
      }
      return;
    }
```

- [ ] **Step 4: Tick the selected location**

Add to `rowIsCurrent()` before `default:`:

```cpp
    case Page::Location:
      return index < services::locations::count() &&
             index == services::locations::selectedIndex();
```

- [ ] **Step 5: Handle activation**

Add to `activateRow()` before `default:`:

```cpp
    case Page::Location: {
      const size_t n = services::locations::count();
      if (n > 0 && index < n) {
        services::locations::select(index);
        draw();  // apply and stay
      } else {
        goTo(Page::Root, 2);
      }
      return;
    }
    case Page::Network:
      goTo(Page::Root, 3);
      return;
```

- [ ] **Step 6: Draw the Network page body**

The Network page is static text plus one row, so give `draw()` a special case. At the very top of `draw()`, after `applyRowStyle()`:

```cpp
  if (s_page == Page::Network) {
    tft.setTextDatum(textdatum_t::middle_center);
    tft.setTextColor(kColorTitle, kColorBg);
    tft.drawString(pageTitle(), kCenterX, kTitleY);

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

    tft.setTextColor(kColorText, kColorBg);
    tft.drawString(ssid, kCenterX, 96);
    if (ip[0] != '\0') {
      tft.drawString(ip, kCenterX, 126);
    }

    tft.fillRoundRect(kRowInsetX - 8, 166, config::kDisplayWidth - 2 * (kRowInsetX - 8),
                      kRowHeight - 4, 4, kColorHighlight);
    tft.setTextColor(kColorText, kColorHighlight);
    tft.drawString("^ Up", kCenterX, 166 + (kRowHeight - 4) / 2);
    return;
  }
```

Values are read when the page opens and do not refresh; re-enter the page for current details.

- [ ] **Step 7: Build**

Run: `pio run -e supermini`
Expected: SUCCESS.

- [ ] **Step 8: Verify on hardware**

Flash with three or more locations saved in the portal. Confirm: the Location page lists them with a tick on the selected one; activating one recentres the radar on exit; with eight saved the list scrolls and the cues appear; with none saved it shows `No locations set`; the Network page shows the SSID and IP, and `No Wi-Fi` when disconnected.

- [ ] **Step 9: Commit**

```bash
git add src/ui/menu.cpp
git commit -m "Add the location and network menu pages"
```

---

### Task 7: Reset Wi-Fi page and documentation

**Files:**
- Modify: `src/ui/menu.cpp`
- Modify: `README.md`

**Interfaces:**
- Consumes: `wifiResetCredentialsAndReboot()` from `services/wifi_setup.h`.
- Produces: nothing.

- [ ] **Step 1: Add the include**

At the top of `src/ui/menu.cpp`:

```cpp
#include "services/wifi_setup.h"
```

- [ ] **Step 2: Give the page two rows**

In `rowCount()`, replace the final `default:` arm:

```cpp
    case Page::Reset:
      return 2;  // No (first, so a stray hold is harmless), then Yes
    default:
      return 1;
```

- [ ] **Step 3: Label them**

In `rowLabel()`, before `default:`:

```cpp
    case Page::Reset:
      snprintf(out, out_size, "%s", index == 0 ? "^ No, go back" : "Yes, erase");
      return;
```

- [ ] **Step 4: Handle activation**

In `activateRow()`, before `default:`:

```cpp
    case Page::Reset:
      if (index == 0) {
        goTo(Page::Root, 4);
      } else {
        Serial.println("Menu: erasing Wi-Fi and rebooting");
        wifiResetCredentialsAndReboot();  // does not return
      }
      return;
```

- [ ] **Step 5: Draw the warning text**

In `draw()`, alongside the `Page::Network` special case:

```cpp
  if (s_page == Page::Reset) {
    tft.setTextDatum(textdatum_t::middle_center);
    tft.setTextColor(kColorTitle, kColorBg);
    tft.drawString(pageTitle(), kCenterX, kTitleY);
    tft.setTextColor(0xFC40, kColorBg);  // amber warning
    tft.drawString("Erase Wi-Fi, location", kCenterX, 82);
    tft.drawString("and settings?", kCenterX, 106);
    // Rows fall through to the normal list renderer below, starting lower.
  }
```

Then in the row loop, offset the Reset page's rows so they sit under the warning: change `const int y = kFirstRowY + slot * kRowHeight;` to

```cpp
    const int first_row_y = (s_page == Page::Reset) ? 148 : kFirstRowY;
    const int y = first_row_y + slot * kRowHeight;
```

- [ ] **Step 6: Build**

Run: `pio run -e supermini`
Expected: SUCCESS.

- [ ] **Step 7: Verify on hardware**

Flash. Confirm the Reset page opens with `^ No, go back` highlighted, that `No` returns to root, and that `Yes, erase` wipes and reboots into the setup portal. Separately confirm holding BOOT for 3 seconds during the connect screen still wipes credentials.

- [ ] **Step 8: Update the README**

Replace the Controls table:

```markdown
## Controls (BOOT, GPIO 9, active LOW)

| Action | Effect |
|--------|--------|
| **Click** | On the radar: open the menu. In the menu: move down one row |
| **Hold** (0.6 s) | On the radar: open the menu. In the menu: activate the highlighted row |
| **Hold 3 s at power-on** | Clear Wi‑Fi, location, and settings; reboot into the setup portal |

The menu closes itself after 15 seconds with no input. ADS-B polling pauses
while it is open.

### Menu

| Page | Does |
|------|------|
| Range | Pick a range preset |
| Altitude | Flight levels (`030`) or feet (`3000 ft`) |
| Location | Centre the radar on a saved location |
| Network | Show the current SSID and IP address |
| Reset Wi-Fi | Erase Wi-Fi, location and settings, with a confirm step |
```

Then, under the settings documentation, add:

```markdown
### Saved locations

Enter up to 8 in the Wi-Fi portal, one per line as `name, lat, lon`:

```
Home, 52.3676, 4.9041
Gatwick, 51.1537, -0.1821
```

Names are truncated to 14 characters. A malformed line is skipped and reported;
the others still save. Select a location from the device menu.
```

Finally, update the BOOT row of the config-keys table to:

```markdown
| BOOT | `kBootPin`, `kBootResetHoldMs`, `kBootTapMinMs`, `kMenuHoldMs`, `kMenuIdleMs` |
```

- [ ] **Step 9: Run the full check**

Run: `pio test -e native && pio run -e supermini`
Expected: both PASS.

- [ ] **Step 10: Commit**

```bash
git add src/ui/menu.cpp README.md
git commit -m "Add the Wi-Fi reset menu page and document the menu"
```

---

## Notes for the reviewer

- **PR #1 should be closed, not merged.** Its double-tap gesture conflicts with click-to-open, and the Network page supersedes its content. Its `statusScreenIpAddress()` rendering is worth lifting if the Network page needs polish.
- **The one genuinely uncertain area is layout arithmetic.** `kFirstRowY`, `kRowHeight` and the Network/Reset Y offsets are calculated for a 240×240 circle with a 9 pt font, but have not been seen on glass. Expect to nudge them in Task 5's hardware check; they are all constants at the top of `menu.cpp`.
- **`pio test -e native` covers parsing and scroll arithmetic only.** Rendering, NVS and the ISR are verified by hand, as noted in each task.
