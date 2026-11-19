#include "CalmContentsActivity.h"

#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <utility>

#include "CalmDraw.h"
#include "CalmLayout.h"
#include "fontIds.h"

namespace {
constexpr int INDENT = 16;      // per nesting level
constexpr int MAX_INDENT_LEVELS = 3;
}  // namespace

CalmContentsActivity::CalmContentsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           std::shared_ptr<Epub> epub, const int currentSpineIndex)
    : Activity("CalmContents", renderer, mappedInput), epub(std::move(epub)), currentSpineIndex(currentSpineIndex) {}

void CalmContentsActivity::onEnter() {
  Activity::onEnter();
  itemCount = epub ? epub->getTocItemsCount() : 0;
  currentItem = epub ? epub->getTocIndexForSpineIndex(currentSpineIndex) : -1;
  selected = std::clamp(currentItem, 0, std::max(0, itemCount - 1));
  ignoreConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  requestUpdate();
}

void CalmContentsActivity::moveTo(const int index) {
  if (itemCount <= 0) return;
  const int next = std::clamp(index, 0, itemCount - 1);
  if (next == selected) return;
  selected = next;
  requestUpdate();
}

void CalmContentsActivity::choose() {
  const auto item = epub->getTocItem(selected);
  if (item.spineIndex < 0) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
  } else {
    setResult(ChapterResult{item.spineIndex, item.anchor});
  }
  finish();
}

void CalmContentsActivity::loop() {
  using Button = MappedInputManager::Button;
  if (ignoreConfirmRelease) {
    if (!mappedInput.isPressed(Button::Confirm)) ignoreConfirmRelease = false;
    return;
  }
  if (mappedInput.wasReleased(Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }
  if (itemCount <= 0) return;
  if (mappedInput.wasReleased(Button::Confirm)) {
    choose();
    return;
  }
  if (mappedInput.wasReleased(Button::Left)) {
    moveTo(selected - pageRows);
    return;
  }
  if (mappedInput.wasReleased(Button::Right)) {
    moveTo(selected + pageRows);
    return;
  }
  navigator.onRelease({Button::Down, Button::Down}, [this] { moveTo(selected + 1 < itemCount ? selected + 1 : 0); });
  navigator.onRelease({Button::Up, Button::Up},
                      [this] { moveTo(selected > 0 ? selected - 1 : itemCount - 1); });
  navigator.onContinuous({Button::Down, Button::Down}, [this] { moveTo(selected + pageRows); });
  navigator.onContinuous({Button::Up, Button::Up}, [this] { moveTo(selected - pageRows); });
}

void CalmContentsActivity::render(RenderLock&&) {
  int t = 0, r = 0, b = 0, l = 0;
  renderer.getOrientedViewableTRBL(&t, &r, &b, &l);
  const int width = renderer.getScreenWidth() - l - r;
  const int bottom = renderer.getScreenHeight() - b;
  const int x = l + calm::SIDE_MARGIN;
  const int innerWidth = width - 2 * calm::SIDE_MARGIN;

  renderer.clearScreen();
  renderer.drawText(BITTER_16_FONT_ID, x, t + (calm::HEADER_HEIGHT - renderer.getLineHeight(BITTER_16_FONT_ID)) / 2,
                    tr(STR_CALM_CONTENTS), true, EpdFontFamily::BOLD);
  renderer.fillRect(l, t + calm::HEADER_HEIGHT - 1, width, 1);

  const int footerLine = renderer.getLineHeight(SMALL_FONT_ID);
  const int listTop = t + calm::HEADER_HEIGHT + 6;
  const int listBottom = bottom - footerLine - 16;
  constexpr int rowHeight = calm::SHEET_ROW_HEIGHT;
  pageRows = std::max(1, (listBottom - listTop) / rowHeight);

  if (itemCount <= 0) {
    renderer.drawCenteredText(UI_12_FONT_ID, listTop + 40, tr(STR_CALM_NO_CONTENTS));
    renderer.displayBuffer();
    return;
  }

  const int page = selected / pageRows;
  const int pageCount = (itemCount + pageRows - 1) / pageRows;
  const int first = page * pageRows;
  const int last = std::min(itemCount, first + pageRows);
  const int uiLine = renderer.getLineHeight(UI_12_FONT_ID);
  int y = listTop;
  for (int i = first; i < last; i++) {
    const auto item = epub->getTocItem(i);
    const bool isSelected = i == selected;
    const bool isCurrent = i == currentItem;
    const int level = std::clamp<int>(item.level > 0 ? item.level - 1 : 0, 0, MAX_INDENT_LEVELS);
    const int textX = x + level * INDENT;
    if (isSelected) {
      calm::fillPill(renderer, l + calm::PILL_INSET, y + 2, width - 2 * calm::PILL_INSET, rowHeight - 4);
    }
    const auto style = isCurrent ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const std::string title =
        renderer.truncatedText(UI_12_FONT_ID, item.title.c_str(), innerWidth - (textX - x) - 16, style);
    renderer.drawText(UI_12_FONT_ID, textX, y + (rowHeight - uiLine) / 2, title.c_str(), !isSelected, style);
    if (isCurrent) {
      // A small dot at the end of the row marks where you are.
      constexpr int dot = 6;
      renderer.fillRoundedRect(x + innerWidth - dot, y + (rowHeight - dot) / 2, dot, dot, dot / 2,
                               isSelected ? Color::White : Color::Black);
    }
    if (!isSelected && i + 1 < last && i + 1 != selected) {
      renderer.fillRect(textX, y + rowHeight - 1, innerWidth - (textX - x), 1);
    }
    y += rowHeight;
  }

  if (pageCount > 1) {
    char footer[32];
    snprintf(footer, sizeof(footer), tr(STR_CALM_PAGE_OF), page + 1, pageCount);
    renderer.drawCenteredText(SMALL_FONT_ID, bottom - footerLine - 8, footer);
  }
  renderer.displayBuffer();
}
