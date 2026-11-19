#pragma once

// Calm UI spacing for the X4's 480x800 panel (~220 ppi). Kobo-style generous
// margins do not fit at this size: 12 px leaves 95% of the width for content.
namespace calm {

constexpr int SIDE_MARGIN = 12;
constexpr int HEADER_HEIGHT = 52;
constexpr int FOOTER_HEIGHT = 24;
constexpr int ROW_HEIGHT = 72;
// Library covers: 2:3, with 6 px above and below inside the row.
constexpr int COVER_WIDTH = 40;
constexpr int COVER_HEIGHT = 60;
constexpr int COVER_GAP = 12;  // between the cover and the text
// Library grid: 3 x 3 covers per page.
constexpr int GRID_COLUMNS = 3;
constexpr int GRID_ROWS = 3;
constexpr int GRID_COVER_WIDTH = 112;
constexpr int GRID_COVER_HEIGHT = 168;
constexpr int PILL_INSET = 6;  // selection pills sit this far in from the screen edge
constexpr int CHIP_PADDING = 6;
constexpr int CHIP_GAP = 10;

// Modal cards and the reading sheet.
constexpr int CARD_BORDER = 2;
constexpr int CARD_PADDING = 16;
constexpr int ACTION_ROW_HEIGHT = 48;
constexpr int SHEET_ROW_HEIGHT = 48;
// White band above a sheet's top rule, so the rule never cuts through text.
constexpr int SHEET_GAP = 6;

}  // namespace calm
