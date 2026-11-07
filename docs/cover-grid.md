# Cover Grid

Cover Grid is its own row in the main and Music menus, declared as a
`<music-entry>` placed directly after Cover Flow. Like Album Artists, it is
shown or hidden in Settings › Main Menu and Music Menu; by default it is visible
in the Music menu only. The entry has no cloned native screen: its `chosen`
event names `CFW_CoverGrid`, which the main and Music menu action handler
(`cfw_music_action`) answers by pushing the grid's canvas screen. Its
`delayedselected` event uses Cover Flow's `HandleMusicHilited`, so the main
menu preview matches Cover Flow's. The grid runs Cover Flow's own library query
(music-only album groups sorted by artist) and does not open for an empty
library, as Cover Flow does not. It reopens on the last selected album while
that album still exists.

## Layout

The grid is four columns of 77 by 77 covers with 2 px gaps and 3 px outer
margins, filling the 320 px width. The selected cover's 2 px ring is drawn in
the surrounding gap, so it never covers artwork. Two rows fit above the caption
bar and a third peeks in below them. The view scrolls by whole rows once the
selection leaves the two full rows.

## Artwork

Covers stream in through Cover Flow's native artwork session. The view owns
one session, which follows the music query the grid opened. The controller
attaches it on activation and stops it on deactivation. Its worker loads covers
in Cover Flow's 128 by 128 RGB565 format (1055) from disk, off the drawing path.
The window reaches a few albums behind its center and well past the visible rows
ahead, so the grid centers it on the first visible album. When the worker
finishes a visible cover, the session's listener redraws the grid.

Drawing only looks up covers the session already holds. Each one is downscaled
to 77 by 77 with a box filter, at most four per frame, and written into the
RGB565 or ARGB8888 UI surface inside the canvas's clip. Albums without a loaded
cover show Cover Flow's placeholder, downscaled once. Covers outside the clip
are skipped, so a scrolling caption redraws only its strip. Moving the
selection within the visible rows redraws the two affected covers, and a newly
loaded cover redraws only itself.

The twelve visible covers and the placeholder are cached by artwork ID. The
native UI keeps the view alive after the grid is left, until the grid next
opens, so leaving the grid (including for an album's song list) stops the
worker. Once the view has not drawn for a second, the frame client frees the
cache and the session with its 128 px covers. The native UI builds the view at
boot, so the cache is allocated only when the view draws: first when the grid
opens, then again after each release. Activation restarts the session. Returning to the
grid therefore streams its covers in again. The model is released with the
view.

## Caption and selection

The selected album's name and artist appear in a bottom bar cloned from the
Now Playing status bar: its background image and a marquee title bound to a
Settings model property, so long captions scroll and themes restyle both bars
alike. Center selects the album on the shared browser and pushes the native
song list in its plain layout, titled with the album's name. The UI restores
each screen's saved list position after a transition, so the grid clears the
Songs screen's saved row for that push, as native code does for its Extras and
Settings screens.
