#include "CalmOpening.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <algorithm>

#include "CalmDraw.h"
#include "CalmLayout.h"
#include "RecentBooksStore.h"
#include "fontIds.h"

namespace calm {
namespace {

struct OpeningBook {
  std::string path;
  std::string title;
  std::string detail;
  bool active = false;
};

// One small record for the book being opened: a few short strings, set once
// per open, so it lives in static storage rather than on any activity.
OpeningBook opening;

std::string fileTitle(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  const size_t dot = name.find_last_of('.');
  if (dot != std::string::npos && dot > 0) name.resize(dot);
  return name;
}

}  // namespace

void setOpeningBook(const std::string& path, const std::string& title, const std::string& detail) {
  opening.path = path;
  opening.title = title;
  opening.detail = detail;
}

void beginOpening(const std::string& path) {
  if (opening.path != path) {
    opening.path = path;
    opening.title.clear();
    opening.detail.clear();
    for (const auto& book : RECENT_BOOKS.getBooks()) {
      if (book.path == path) {
        opening.title = book.title;
        opening.detail = book.author;
        break;
      }
    }
  }
  opening.active = true;
}

void endOpening() { opening.active = false; }

bool isOpening() { return opening.active; }

void drawOpeningCard(GfxRenderer& renderer, const char* message, const std::string& fallbackTitle,
                     const std::string& fallbackAuthor) {
  const std::string title =
      !opening.title.empty() ? opening.title : !fallbackTitle.empty() ? fallbackTitle : fileTitle(opening.path);
  const std::string& detail = !opening.detail.empty() ? opening.detail : fallbackAuthor;

  int t = 0, r = 0, b = 0, l = 0;
  renderer.getOrientedViewableTRBL(&t, &r, &b, &l);
  const int screenWidth = renderer.getScreenWidth() - l - r;
  const int screenHeight = renderer.getScreenHeight() - t - b;

  constexpr int pad = 18;
  const int cardWidth = screenWidth - 2 * (calm::SIDE_MARGIN + 12);
  const int cardX = l + (screenWidth - cardWidth) / 2;
  const int innerWidth = cardWidth - 2 * pad;

  const auto titleLines = wrapText(renderer, BITTER_14_FONT_ID, title, innerWidth, 3, EpdFontFamily::BOLD);
  const int titleLine = renderer.getLineHeight(BITTER_14_FONT_ID);
  const int detailLine = renderer.getLineHeight(UI_10_FONT_ID);
  const int messageLine = renderer.getLineHeight(UI_10_FONT_ID);
  const int cardHeight = pad + static_cast<int>(titleLines.size()) * titleLine +
                         (detail.empty() ? 0 : 6 + detailLine) + 14 + 1 + 12 + messageLine + pad;
  const int cardY = t + (screenHeight - cardHeight) * 2 / 5;

  // A white band around the card keeps it clear of whatever is behind it.
  renderer.fillRoundedRect(cardX - 6, cardY - 6, cardWidth + 12, cardHeight + 12, SHEET_RADIUS + 6, Color::White);
  renderer.drawRoundedRect(cardX, cardY, cardWidth, cardHeight, CARD_BORDER, SHEET_RADIUS, true);

  int y = cardY + pad;
  for (const auto& line : titleLines) {
    const int w = renderer.getTextWidth(BITTER_14_FONT_ID, line.c_str(), EpdFontFamily::BOLD);
    renderer.drawText(BITTER_14_FONT_ID, cardX + (cardWidth - w) / 2, y, line.c_str(), true, EpdFontFamily::BOLD);
    y += titleLine;
  }
  if (!detail.empty()) {
    y += 6;
    const std::string text = renderer.truncatedText(UI_10_FONT_ID, detail.c_str(), innerWidth);
    const int w = renderer.getTextWidth(UI_10_FONT_ID, text.c_str());
    renderer.drawText(UI_10_FONT_ID, cardX + (cardWidth - w) / 2, y, text.c_str());
    y += detailLine;
  }
  y += 14;
  renderer.fillRect(cardX + pad, y, innerWidth, 1);
  y += 1 + 12;
  const int w = renderer.getTextWidth(UI_10_FONT_ID, message, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, cardX + (cardWidth - w) / 2, y, message, true, EpdFontFamily::BOLD);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

}  // namespace calm
