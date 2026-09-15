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
