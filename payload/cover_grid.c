/* SPDX-License-Identifier: GPL-3.0-only */

#include "cover_grid.h"
#include "cover_grid_screen_ui.h"
#include "cover_grid_view.h"
#include "osos.h"
#include "cfw.h"

int cfw_cover_grid_property(uint32_t type, uint32_t property, uintptr_t *out)
{
    if (type != OSOS_PROPERTY_STRING || property != CFW_CoverGridView_Caption)
        return 0;
    *out = (uintptr_t)cfw_cover_grid_view_caption();
    return 1;
}

/* The Cover Grid row sits next to Cover Flow in the Main and Music menus. */
int cfw_cover_grid_menu_action(void *controller, const char *action, uint32_t argument)
{
    if (!cfw_string_equal(action, "CFW_CoverGrid"))
        return 0;
    if (argument != OSOS_ACTION_SUPPORT_QUERY)
        cfw_cover_grid_view_open(controller);
    return 1;
}
