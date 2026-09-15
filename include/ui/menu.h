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
