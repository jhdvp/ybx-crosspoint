# YBX Reader home

Intent: a reader returning to the device resumes a local book first. The home
must feel like the PocketBook/desktop YBX clients, while fitting X3/X4 buttons
and one-bit screens. Continue-reading is the focal element; the library and
catalogs follow it in keyboard order.

Domain: current book, saved position, recently read shelf, SD library, connected
catalogs. The signature is the three-bar YBX mark above a cover-and-progress
hero and a small recent shelf, followed by hard-edged navigation.

Palette: paper, ink, cover grayscale/dither, paper-colored mark bars and inverted
focus. No yellow, shadows or fake status colours on the monochrome reader.
Hierarchy: bold book title, smaller author/section labels, explicit focus. The
host's registered UI fonts supply script fallback; no new embedded font payload.
Depth: two-pixel outlines and inversion; no rounded cards. Only the mark rounds.
Spacing: twelve-pixel section/control gaps, theme-derived side padding and font
heights, oriented safe bounds. Portrait stacks the shelf below the hero;
landscape places them beside each other. Navigation is one compact footer row: Library, Catalogs and More. Secondary
actions live in the existing OptionPopup; recent covers are directly selectable,
with plain title/progress captions instead of duplicate title buttons.

Reuse UiAppHost, FreeInkUI bookCard/button, RecentBooksStore, BookProgress and
HomeCoverCache. At most three recent local books; missing files are pruned by
the existing flow. Decode thumbnails from SD without a PSRAM allocation or a
second framebuffer. Home does not own Wi-Fi or fetch cloud account/stats data.

Avoid the defaults: a grid of unrelated apps, invented reading streaks, and
phone-sized touch tabs that cannot be reached with physical buttons. All old
home actions remain available; other device profiles keep the upstream home.
