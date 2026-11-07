/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "../../../payload/native/bindings.h"
/* Native layout assertions assume 32-bit pointers; this fixture runs on the host. */
#define _Static_assert(...)
#include "../../../payload/osos.h"
#include "../../../payload/cover_grid.h"
#include "../../../payload/cover_grid_view.h"
#include "cover_grid_screen_ui.h"
#undef _Static_assert
#define CFW_PATCH_H
#define PATCH_ARM

static int menu, opens;

void cfw_cover_grid_view_open(void *value)
{
    assert(value == &menu);
    opens++;
}

const char *cfw_cover_grid_view_caption(void)
{
    return "Album \xc2\xb7 Artist";
}

#include "../../../payload/cover_grid.c"

static void check_geometry(void)
{
    /* Four 77 px covers with 2 px gaps and 3 px margins fill the 320 px width. */
    assert(2 * CFW_GRID_MARGIN + CFW_GRID_COLUMNS * CFW_GRID_COVER +
               (CFW_GRID_COLUMNS - 1) * CFW_GRID_GAP ==
           320);
    /* Two full rows fit above the caption bar; the third peeks in. */
    int height = 217 - CFW_GRID_CAPTION;
    /* Shifted inward at the screen's edges, the lifted cover still hides the cover
     * beneath it, and a second-row lift with its edge and shadow stays above the
     * caption bar. */
    assert(CFW_GRID_EDGE - (CFW_GRID_MARGIN - CFW_GRID_LIFT) <= 2 * CFW_GRID_LIFT);
    assert(CFW_GRID_MARGIN + (CFW_GRID_FULL_ROWS - 1) * CFW_GRID_PITCH +
               CFW_GRID_COVER + CFW_GRID_LIFT + CFW_GRID_EDGE + CFW_GRID_SHADOW + 1 <=
           height);
    assert(CFW_GRID_MARGIN + CFW_GRID_FULL_ROWS * CFW_GRID_PITCH - CFW_GRID_GAP <=
           height);
    assert(CFW_GRID_MARGIN + (CFW_GRID_FULL_ROWS + 1) * CFW_GRID_PITCH - CFW_GRID_GAP >
           height);
    /* The view scrolls only once the selection leaves the full rows. */
    assert(cfw_cover_grid_top_row(0, 0) == 0);
    assert(cfw_cover_grid_top_row(0, 7) == 0);
    assert(cfw_cover_grid_top_row(0, 8) == 1);
    assert(cfw_cover_grid_top_row(1, 8) == 1);
    assert(cfw_cover_grid_top_row(1, 3) == 0);
    assert(cfw_cover_grid_top_row(5, 41) == 9);
    assert(cfw_cover_grid_top_row(5, 20) == 5);
}

int main(void)
{
    check_geometry();

    /* Only the Cover Grid row opens the grid; Cover Flow keeps its own handler. */
    assert(cfw_cover_grid_menu_action(&menu, "CFW_CoverGrid", OSOS_ACTION_SUPPORT_QUERY));
    assert(opens == 0);
    assert(cfw_cover_grid_menu_action(&menu, "CFW_CoverGrid", 0));
    assert(opens == 1);
    assert(!cfw_cover_grid_menu_action(&menu, "HandleCoverFlowSelected", 0));
    assert(!cfw_cover_grid_menu_action(&menu, "HandleMusicHilited", 0));
    assert(opens == 1);

    /* The caption is served through the Settings model. */
    uintptr_t value = 0;
    assert(cfw_cover_grid_property(OSOS_PROPERTY_STRING, CFW_CoverGridView_Caption,
                                   &value));
    assert(!strcmp((const char *)value, "Album \xc2\xb7 Artist"));
    assert(!cfw_cover_grid_property(OSOS_RESOURCE_BMAP, CFW_CoverGridView_Caption,
                                    &value));
    assert(!cfw_cover_grid_property(OSOS_PROPERTY_STRING, CFW_CoverGridView_Title,
                                    &value));

    puts("Cover Grid: menu action, caption and geometry passed");
    return 0;
}
