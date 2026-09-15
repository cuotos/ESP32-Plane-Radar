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
