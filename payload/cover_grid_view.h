/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once
#include <stdint.h>

/* Opens the grid from a Main or Music menu controller. */
void cfw_cover_grid_view_open(void *controller);
/* "Album · Artist" of the selected cover, UTF-8. */
const char *cfw_cover_grid_view_caption(void);
void cfw_ui_register_cover_grid(void);

/* Grid geometry inside the canvas, shared with the host tests. */
enum {
    CFW_GRID_COLUMNS = 4,
    CFW_GRID_COVER = 77,
    CFW_GRID_GAP = 2,
    CFW_GRID_MARGIN = 3,
    CFW_GRID_RING = 2,
    CFW_GRID_PITCH = CFW_GRID_COVER + CFW_GRID_GAP,
    /* Visible height of the bottom bar holding the caption, cloned from the status
     * bar by patching/ui/compiler.py (CAPTION_TOP); its shadow rows fall below the
     * screen. */
    CFW_GRID_CAPTION = 22,
    /* Rows that fit entirely above the caption strip; a third peeks below. */
    CFW_GRID_FULL_ROWS = 2,
};

/* First visible row after moving the selection to index. */
static inline int cfw_cover_grid_top_row(int top, int index)
{
    int row = index / CFW_GRID_COLUMNS;
    if (row < top)
        return row;
    if (row >= top + CFW_GRID_FULL_ROWS)
        return row - CFW_GRID_FULL_ROWS + 1;
    return top;
}
