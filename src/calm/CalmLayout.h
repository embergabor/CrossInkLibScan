#pragma once

// Calm UI spacing for the X4's 480x800 panel (~220 ppi). Kobo-style generous
// margins do not fit at this size: 12 px leaves 95% of the width for content.
namespace calm {

constexpr int SIDE_MARGIN = 12;
constexpr int HEADER_HEIGHT = 44;
constexpr int FOOTER_HEIGHT = 24;
constexpr int ROW_HEIGHT = 64;
constexpr int ROW_PADDING_TOP = 4;
constexpr int CHIP_PADDING = 6;
constexpr int CHIP_GAP = 10;

// Modal cards and the reading sheet.
constexpr int CARD_BORDER = 2;
constexpr int CARD_PADDING = 16;
constexpr int ACTION_ROW_HEIGHT = 48;
constexpr int SHEET_ROW_HEIGHT = 52;

}  // namespace calm
