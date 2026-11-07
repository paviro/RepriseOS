/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef CFW_MEDIA_MENUS_H
#define CFW_MEDIA_MENUS_H

#include "osos.h"

/* Capture the list row a native media list menu opens for. */
void cfw_media_menu_capture(void *controller, int row, unsigned layout_offset);
/* Append File Info for the captured item, then the native Cancel row. */
void cfw_media_menu_cancel(struct osos_context_menu *list, uint32_t label,
                           const char *action, uint32_t flags);
/* Handle File Info for the item captured by the last media list menu. */
int cfw_media_menu_action(void *controller, const char *action, uint32_t argument);

#endif
