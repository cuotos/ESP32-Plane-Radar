# One-button menu system — design

Date: 2026-09-15
Status: approved for planning
Base: `main`, with PRs #2, #3 and #4 merged (see [Relationship to the open PRs](#relationship-to-the-open-prs))

## Goal

Replace the current two-gesture BOOT button scheme with a navigable on-device
menu, so range, altitude format and radar centre can all be changed without a
phone. Adds a portal-managed list of up to 8 named locations that the device can
switch between.

## Interaction model

One rule everywhere: **click moves the highlight, hold activates it.**

| Gesture | Threshold | Meaning on the radar | Meaning in the menu |
|---|---|---|---|
| Click | 40 ms – 600 ms | Open the menu | Move highlight down one row (wraps) |
| Hold | ≥ 600 ms | Open the menu | Activate the highlighted row |

Both gestures open the menu from the radar. There is deliberately no dead
gesture: a user who holds too long still gets the menu rather than nothing.

The hold fires **when the threshold is crossed**, not on release, so the device
reacts while the button is still down. The highlighted row inverts on the
threshold crossing as confirmation. The release that follows a hold is
swallowed, so it does not also register as a click.

### Leaving the menu

Three ways out, all returning to the radar:

1. Activating **Exit** on the root page. The root page has no "↑ Up" row —
   Exit is its equivalent, and sits in the same last-row position so the
   gesture is the same wherever you are.
2. **Idle timeout** — 15 s with no button activity. Any press resets the timer.

Settings already applied stay applied; the timeout is not a cancel.

### Wi-Fi reset

The runtime "hold 3 s to wipe Wi-Fi" gesture is **removed from the radar**,
because hold now means activate. It moves into the menu as a confirmed action.

The escape hatch survives untouched: `bootButtonPollLongPress()` keeps running
inside the connect and portal loops in `wifi_setup.cpp`, so holding BOOT for 3 s
at power-on (or while the device is trying to connect, or sitting in the portal)
still wipes credentials. `main.cpp` simply stops calling it once the radar is
running. No new code is needed for this — only the removal of one call site.

## Screens

Layout is a compact list: a small uppercase title, then up to five rows. The
highlighted row is drawn as a filled blue bar. Values sit right-aligned on the
root page so the current state is readable without entering anything; a green
tick marks the current value on a settings page.

```
Root           Range           Altitude        Location        Reset Wi-Fi
------         ------          --------        --------        -----------
Range   10 km  5 km            Flight levels ✓ Home          ✓ Erase Wi-Fi,
Altitude   FL  10 km         ✓ Feet            Gatwick         location and
Location Home  15 km            ↑ Up           Peak District   settings?
Network        25 km                           Manchester
Reset Wi-Fi    ↑ Up                            ↑ Up            ↑ No, go back
Exit                                                           Yes, erase
```

Activating a value **applies it and stays on the page**, so several ranges can
be tried in succession. You leave via **↑ Up**.

Each settings page opens with the highlight already on the current value.

The Reset page opens on **↑ No, go back**, so a stray hold cannot wipe the
device. Only the second row erases.

### Network page

Activating **Network** shows the connection details rather than a list:

```
     NETWORK

     OnePlusdp          <- SSID, truncated to fit
     192.168.1.42       <- IP address

       ↑ Up
```

SSID and IP are read live from `WiFi.SSID()` and `WiFi.localIP()` when the page
opens. Not connected shows `No Wi-Fi` in place of both. The page is static apart
from the highlighted `↑ Up` row, so click and hold behave as everywhere else.
This replaces the double-tap gesture in PR #1.

### Scrolling

Root now carries six rows (Range, Altitude, Location, Network, Reset Wi-Fi,
Exit), so it scrolls too. Range (5) and Altitude (3) fit without scrolling.
Location does not: 8 locations plus "↑ Up" is 9 rows, against a 5-row budget.

The Location page therefore renders a **5-row sliding window**. The highlight
moves within the window until it reaches the last visible row, at which point
the window scrolls by one. A chevron (`⌃` / `⌄`) is drawn at the top or bottom
edge when rows exist beyond the window. Every other page uses the same renderer
with a list short enough that the window never slides.

## Settings and storage

All persisted in the existing `planeradar` NVS namespace, alongside `rangeIdx`,
`useMiles` and `showRwys`.

| Setting | Key | Values | Default |
|---|---|---|---|
| Range | `rangeIdx` (existing) | index into `kRangePresets` | 10 km |
| Altitude format | `altFL` | bool — flight levels vs feet | flight levels |
| Locations | `locs` | string blob, see below | empty |
| Selected location | `locSel` | uint8 index, `0xFF` = none | none |

`unitsReset()` clears `altFL`, `locs` and `locSel` along with the existing keys,
so the Wi-Fi reset wipes the lot as the README already promises.

### Altitude format

**Already built** — `altFL`, `ui::radar::flightLevels()` and the
`formatAltitudeTag()` branch all landed in PR #3. The menu only needs to read
and write the existing setting; no storage or formatting work remains.

### Location list

Entered in the Wi-Fi portal as a single textarea, one location per line:

```
Home, 52.3676, 4.9041
Gatwick, 51.1537, -0.1821
Peak District, 53.3333, -1.8000
```

Format is `name, lat, lon`. Rules:

- Maximum 8 lines; extra lines are dropped with a serial warning.
- Name is trimmed, capped at **14 characters** (longer names are truncated on
  save, not silently at draw time — what the portal shows is what the device
  shows).
- `lat` must parse as a double in [-90, 90]; `lon` in [-180, 180].
- Blank lines are skipped. A malformed line is skipped, and the portal reports
  `Skipped line 3: expected "name, lat, lon"` rather than failing the whole save.
- The raw validated text is stored as one NVS string (8 × ~40 bytes ≈ 320 bytes)
  and parsed into a fixed array on boot. No dynamic allocation.

The existing `radar_lat` / `radar_lon` portal fields stay, and keep meaning "the
current radar centre". Selecting a location on the device writes that location's
coordinates through the existing `services::location` save path, so the portal
fields reflect the choice on next load. Users who never create a location keep
today's behaviour exactly.

An empty list shows a single non-activatable `No locations set` row above `↑ Up`.

## Module boundaries

New:

- **`include/ui/menu.h`, `src/ui/menu.cpp`** — the whole menu: page definitions,
  highlight and scroll state, the list renderer, and the timeout. Exposes
  `menuIsOpen()`, `menuOpen()`, `menuHandleClick()`, `menuHandleHold()`,
  `menuTick()` (drives the idle timeout), and `menuClose()`. Knows nothing about
  ADS-B or Wi-Fi; it calls into the setting modules.
- **`include/services/locations.h`, `src/services/locations.cpp`** — the 8-slot
  store: parse, validate, persist, enumerate, and apply a selection. Owns the
  textarea format so neither the portal nor the menu has to know it.

Changed:

- **`radar_range.{h,cpp}`** — gains `setFlightLevels()` so the menu can write the
  setting PR #3 added; `unitsReset()` extended to clear `locs` and `locSel`.
- **`wifi_setup.cpp`** — portal gains the locations textarea (the altitude
  checkbox is already there); `bootButtonConsumeHold()` added next to the
  existing tap consumer.
- **`main.cpp`** — routes button events to the menu, pauses ADS-B while it is
  open, stops calling `bootButtonPollLongPress()`.

`menu.cpp` is the one file at risk of sprawl. Page definitions are kept as a
static table of `{title, rows, on_activate}` rather than a switch per page, so
adding a setting is a table entry, not new control flow.

## Button API

`wifi_setup.cpp` owns the ISR today and keeps doing so.

```c
bool bootButtonConsumeTap();   // existing; now only fires for holds < kMenuHoldMs
bool bootButtonConsumeHold();  // new; fires once when the press crosses kMenuHoldMs
```

The tap window narrows from `[40 ms, 3000 ms)` to `[40 ms, 600 ms)`. A press
longer than 600 ms produces a hold and no tap. `s_boot_hold_fired` suppresses
the release-tap; it clears on the next press.

New constants in `config.h`:

```c
constexpr unsigned long kMenuHoldMs = 600UL;    // hold-to-activate threshold
constexpr unsigned long kMenuIdleMs = 15000UL;  // auto-close after no input
```

## Main loop integration

```
loop():
  if menuIsOpen():
    consume tap  -> menuHandleClick()
    consume hold -> menuHandleHold()
    menuTick()                 # closes on idle timeout
    if just closed: redraw radar
    wifiLoop(); delay(10); return      # ADS-B paused
  ...existing radar path...
```

ADS-B polling pauses while the menu is open, so a 3 s HTTP fetch can never stall
a button press. The radar redraws on close, which refreshes the picture within
one poll interval.

`g_radar_visible` is cleared when the menu opens, so the existing
`if (!g_radar_visible)` path handles the redraw on close with no new drawing
call.

## Error handling

| Case | Behaviour |
|---|---|
| Malformed location line | Skipped, named in the portal response and the serial log; other lines still save |
| More than 8 locations | First 8 kept, rest dropped with a warning |
| Empty location list | Location page shows `No locations set`; radar keeps its current centre |
| Selected location deleted in portal | `locSel` reset to none; radar centre left where it is |
| NVS write fails | Change applies in RAM for this session; serial warning. Matches how the existing settings behave |
| Menu opened before Wi-Fi connects | Not reachable — the menu only runs once the radar is up |
| Wi-Fi drops while the Network page is open | Values are read once when the page opens; they do not refresh. Re-enter the page for current details |

## Testing

There is no test harness in this repo and no host build, so verification is
`pio run` plus hardware checks. Manual checklist:

- Click from radar opens the menu; hold from radar also opens it.
- Click wraps past the last row; hold activates.
- Each settings page opens on the current value and stays put after applying.
- Location page scrolls correctly with 8 entries, and chevrons appear only when
  rows are off-screen.
- Network page shows the live SSID and IP, and `No Wi-Fi` when disconnected.
- Reset Wi-Fi opens on "No"; "Yes, erase" wipes and reboots into the portal.
- Menu closes after 15 s idle and the radar redraws.
- Holding BOOT 3 s at power-on still wipes credentials.
- A malformed portal line is reported and does not lose the other lines.

## Relationship to the open PRs

**Resolved.** PRs #2 (3-digit flight levels), #3 (configurable altitude format)
and #4 (climb/descend arrow) are merged to `main`, rebased so they no longer
carry #1's commit. The altitude sections of this spec are therefore already
built, and the remaining work is the menu, the location store and the button
API change.

**#1 (double-tap → IP screen) should be closed, not merged.** Its gesture
conflicts with the click-to-open model here, and its content is superseded by
the Network page above, which shows the SSID alongside the IP. The status-screen
rendering in #1 is worth lifting when that page is built.

## Out of scope

- On-device editing of coordinates (portal only, by decision).
- Browser geolocation in the portal — blocked by the secure-context rule, since
  the portal is plain HTTP. Would need a separate HTTPS helper page.
- Reordering locations on the device.
- Any second button or touch input.
