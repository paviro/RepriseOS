/* SPDX-License-Identifier: GPL-3.0-only */

#include "album_artists.h"
#include "cover_grid.h"
#include "osos.h"
#include "patch.h"

/* Main and Music menu actions: payload rows first, then the native dispatcher. */
PATCH_ARM int cfw_music_action(void *controller, const char *action, uint32_t argument)
{
    if (cfw_cover_grid_menu_action(controller, action, argument) ||
        cfw_album_artists_menu_action(controller, action, argument))
        return 1;
    return osos_music_action(controller, action, argument);
}
