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
      if (lat_comma == nullptr || !parseDouble(lat_comma + 1, lon_comma, &lat) ||
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
