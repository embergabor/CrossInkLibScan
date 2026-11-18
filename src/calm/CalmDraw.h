#pragma once

#include <GfxRenderer.h>
#include <Icon.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "fontIds.h"

// Drawing vocabulary shared by the calm screens, modeled on Calm UI for
// KOReader: rounded outlined buttons, segmented controls whose active choice is
// filled black, and (because the X4 has no touch) a rounded focus ring or a
// rounded filled pill to show where the cursor is.
namespace calm {

constexpr int RADIUS = 8;            // buttons, pills, steppers
constexpr int SHEET_RADIUS = 14;     // top corners of bottom sheets
constexpr int FOCUS_RING_GAP = 3;    // space between a control and its focus ring
constexpr int FOCUS_RING_WIDTH = 2;
constexpr int CONTROL_HEIGHT = 32;   // header buttons and segmented controls

constexpr int BATTERY_WIDTH = 20;
constexpr int BATTERY_HEIGHT = 10;

// A quiet battery glyph: 1 px outline, a 2 px nub, and a fill proportional to
// the charge. (x, y) is the outline's top-left corner.
inline void drawBattery(const GfxRenderer& renderer, const int x, const int y, const uint16_t percent,
                        const bool black = true) {
  const int bodyWidth = BATTERY_WIDTH - 2;
  renderer.drawRect(x, y, bodyWidth, BATTERY_HEIGHT, black);
  renderer.fillRect(x + bodyWidth, y + 3, 2, BATTERY_HEIGHT - 6, black);
  const int innerWidth = bodyWidth - 4;
  const int fill = std::clamp<int>(innerWidth * std::min<uint16_t>(percent, 100) / 100, percent > 0 ? 1 : 0,
                                   innerWidth);
  if (fill > 0) renderer.fillRect(x + 2, y + 2, fill, BATTERY_HEIGHT - 4, black);
}

// FreeInk icons: rows top to bottom, MSB first, bit 0 = ink.
inline void drawIcon(const GfxRenderer& renderer, const freeink::Icon& icon, const int x, const int y,
                     const bool black = true) {
  const int stride = (icon.w + 7) / 8;
  for (int row = 0; row < icon.h; row++) {
    for (int col = 0; col < icon.w; col++) {
      const uint8_t byte = icon.bits[row * stride + col / 8];
      if ((byte & (0x80 >> (col % 8))) == 0) renderer.drawPixel(x + col, y + row, black);
    }
  }
}

// A rounded selection pill (list rows, sheet rows). White content goes on top.
inline void fillPill(const GfxRenderer& renderer, const int x, const int y, const int w, const int h,
                     const int radius = RADIUS) {
  renderer.fillRoundedRect(x, y, w, h, radius, Color::Black);
}

// A rounded button. Filled = active/pressed (white label), else outlined.
inline void drawButton(const GfxRenderer& renderer, const int x, const int y, const int w, const int h,
                       const bool filled) {
  if (filled)
    renderer.fillRoundedRect(x, y, w, h, RADIUS, Color::Black);
  else
    renderer.drawRoundedRect(x, y, w, h, 1, RADIUS, true);
}

// The keyboard cursor on a control that may itself be filled (an active
// segment): a thicker ring just outside it.
inline void drawFocusRing(const GfxRenderer& renderer, const int x, const int y, const int w, const int h) {
  renderer.drawRoundedRect(x - FOCUS_RING_GAP - FOCUS_RING_WIDTH, y - FOCUS_RING_GAP - FOCUS_RING_WIDTH,
                           w + 2 * (FOCUS_RING_GAP + FOCUS_RING_WIDTH), h + 2 * (FOCUS_RING_GAP + FOCUS_RING_WIDTH),
                           FOCUS_RING_WIDTH, RADIUS + FOCUS_RING_GAP + FOCUS_RING_WIDTH, true);
}

// Text centered in a box, in the label color a filled box needs.
inline void drawCenteredLabel(const GfxRenderer& renderer, const int fontId, const int x, const int y, const int w,
                              const int h, const char* text, const bool onBlack,
                              const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  const int textWidth = renderer.getTextWidth(fontId, text, style);
  const int textY = y + (h - renderer.getLineHeight(fontId)) / 2;
  renderer.drawText(fontId, x + (w - textWidth) / 2, textY, text, !onBlack, style);
}

// A bottom sheet: white panel with rounded top corners and a 1 px outline,
// plus a white band above it so its edge never cuts through text.
inline void drawSheet(const GfxRenderer& renderer, const int x, const int y, const int w, const int h) {
  renderer.fillRect(x, y - 6, w, 6, false);
  renderer.fillRoundedRect(x, y, w, h, SHEET_RADIUS, true, true, false, false, Color::White);
  renderer.drawRoundedRect(x, y, w, h, 1, SHEET_RADIUS, true, true, false, false, true);
}

// A stepper: a rounded box with ‹ value › for Left/Right adjustment. Focused
// steppers are filled.
inline void drawStepper(const GfxRenderer& renderer, const int x, const int y, const int w, const int h,
                        const char* value, const bool focused) {
  drawButton(renderer, x, y, w, h, focused);
  const int line = renderer.getLineHeight(UI_12_FONT_ID);
  const int textY = y + (h - line) / 2;
  renderer.drawText(UI_12_FONT_ID, x + 10, textY, "\xE2\x80\xB9", !focused);  // ‹
  const int arrowWidth = renderer.getTextWidth(UI_12_FONT_ID, "\xE2\x80\xBA");
  renderer.drawText(UI_12_FONT_ID, x + w - 10 - arrowWidth, textY, "\xE2\x80\xBA", !focused);  // ›
  const std::string fitted = renderer.truncatedText(UI_12_FONT_ID, value, w - 2 * (10 + arrowWidth + 6));
  drawCenteredLabel(renderer, UI_12_FONT_ID, x, y, w, h, fitted.c_str(), focused);
}

// Small uppercase section label, as Calm UI's "SIZE", "FONT", "SLEEP".
inline void drawSectionLabel(const GfxRenderer& renderer, const int x, const int y, const char* text) {
  renderer.drawText(SMALL_FONT_ID, x, y, text, true, EpdFontFamily::BOLD);
}

// Greedy word wrap into at most maxLines lines; the last line is ellipsized.
inline std::vector<std::string> wrapText(const GfxRenderer& renderer, const int fontId, const std::string& text,
                                  const int maxWidth, const size_t maxLines, const EpdFontFamily::Style style) {
  std::vector<std::string> lines;
  std::string line;
  size_t pos = 0;
  while (pos < text.size() && lines.size() < maxLines) {
    size_t next = text.find(' ', pos);
    if (next == std::string::npos) next = text.size();
    const std::string word = text.substr(pos, next - pos);
    const std::string candidate = line.empty() ? word : line + " " + word;
    if (line.empty() || renderer.getTextWidth(fontId, candidate.c_str(), style) <= maxWidth) {
      line = candidate;
      pos = next + 1;
      continue;
    }
    lines.push_back(line);
    line.clear();
  }
  if (!line.empty() && lines.size() < maxLines) {
    lines.push_back(line);
  } else if (!lines.empty() && (!line.empty() || pos < text.size())) {
    // Text left over: mark the cut on the last visible line.
    std::string rest = line;
    if (pos < text.size()) rest += (rest.empty() ? "" : " ") + text.substr(pos);
    lines.back() = renderer.truncatedText(fontId, (lines.back() + " " + rest).c_str(), maxWidth, style);
  }
  for (auto& l : lines) l = renderer.truncatedText(fontId, l.c_str(), maxWidth, style);
  return lines;
}

}  // namespace calm
