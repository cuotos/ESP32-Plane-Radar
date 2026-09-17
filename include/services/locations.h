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
/** Same, found by name. Blank rows shift indices, so the portal selects by name. */
bool selectByName(const char* name);

/** Join one-per-row portal lines and save them. See saveFromPortal(). */
bool saveFromPortalLines(const char* const* lines, size_t line_count, char* err,
                         size_t err_size);

/**
 * Validate and persist portal text. Always stores whatever parsed, so one
 * bad line does not lose the others. err receives the first problem, or an
 * empty string. Returns false when err was written.
 */
bool saveFromPortal(const char* text, char* err, size_t err_size);

/** Erase the list and selection (used by the WiFi reset). */
void clear();

}  // namespace services::locations
