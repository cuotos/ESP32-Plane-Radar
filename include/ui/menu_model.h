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
