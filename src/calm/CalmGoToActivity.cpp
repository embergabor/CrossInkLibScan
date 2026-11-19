#include "CalmGoToActivity.h"

#include <I18n.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "CalmDraw.h"
#include "CalmLayout.h"
#include "fontIds.h"

CalmGoToActivity::CalmGoToActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const float percent)
    : Activity("CalmGoTo", renderer, mappedInput),
      value(std::clamp<int32_t>(static_cast<int32_t>(std::lround(percent)), 0, 100)) {}

CalmGoToActivity::CalmGoToActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const uint32_t page,
                                   const uint32_t pageCount)
    : Activity("CalmGoTo", renderer, mappedInput),
      pageMode(true),
      minimum(1),
      maximum(static_cast<int32_t>(std::max<uint32_t>(1, pageCount))) {
  value = std::clamp(static_cast<int32_t>(page), minimum, maximum);
}

void CalmGoToActivity::onEnter() {
  Activity::onEnter();
  ignoreConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  requestUpdate();
}

void CalmGoToActivity::adjust(const int delta) {
  const int32_t next = std::clamp(value + delta, minimum, maximum);
  if (next == value) return;
  value = next;
  requestUpdate();
}

void CalmGoToActivity::loop() {
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
  if (mappedInput.wasReleased(Button::Confirm)) {
    if (pageMode)
      setResult(PageResult{static_cast<uint32_t>(value)});
    else
      setResult(PercentResult{static_cast<float>(value)});
    finish();
    return;
  }
  navigator.onPressAndContinuous({Button::Right, Button::Right}, [this] { adjust(1); });
  navigator.onPressAndContinuous({Button::Left, Button::Left}, [this] { adjust(-1); });
  navigator.onPressAndContinuous({Button::Up, Button::Up}, [this] { adjust(10); });
  navigator.onPressAndContinuous({Button::Down, Button::Down}, [this] { adjust(-10); });
}

void CalmGoToActivity::render(RenderLock&&) {
  int t = 0, r = 0, b = 0, l = 0;
  renderer.getOrientedViewableTRBL(&t, &r, &b, &l);
  const int width = renderer.getScreenWidth() - l - r;
  const int bottom = renderer.getScreenHeight() - b;
  const int x = l + calm::SIDE_MARGIN;
  const int innerWidth = width - 2 * calm::SIDE_MARGIN;

  const int titleLine = renderer.getLineHeight(UI_12_FONT_ID);
  const int valueLine = renderer.getLineHeight(BITTER_16_FONT_ID);
  const int hintLine = renderer.getLineHeight(SMALL_FONT_ID);
  constexpr int pad = 14;
  constexpr int barHeight = 10;
  const int sheetHeight = pad + titleLine + 12 + valueLine + 14 + barHeight + 14 + hintLine + pad + 6;
  const int sheetY = bottom - sheetHeight;
  calm::drawSheet(renderer, l, sheetY, width, sheetHeight);

  int y = sheetY + pad;
  renderer.drawText(UI_12_FONT_ID, x, y, tr(STR_CALM_GO_TO), true, EpdFontFamily::BOLD);
  y += titleLine + 12;

  char label[40];
  if (pageMode)
    snprintf(label, sizeof(label), tr(STR_CALM_PAGE_OF), static_cast<int>(value), static_cast<int>(maximum));
  else
    snprintf(label, sizeof(label), "%d%%", static_cast<int>(value));
  const int labelWidth = renderer.getTextWidth(BITTER_16_FONT_ID, label, EpdFontFamily::BOLD);
  renderer.drawText(BITTER_16_FONT_ID, x + (innerWidth - labelWidth) / 2, y, label, true, EpdFontFamily::BOLD);
  y += valueLine + 14;

  // The bar: a rounded track with the chosen position filled.
  renderer.drawRoundedRect(x, y, innerWidth, barHeight, 1, barHeight / 2, true);
  const int span = std::max<int32_t>(1, maximum - minimum);
  const int fill = static_cast<int>((static_cast<int64_t>(innerWidth) * (value - minimum)) / span);
  if (fill > 0) renderer.fillRoundedRect(x, y, std::max(fill, barHeight), barHeight, barHeight / 2, Color::Black);
  y += barHeight + 14;

  renderer.drawCenteredText(SMALL_FONT_ID, y, tr(STR_CALM_GO_TO_HINT));
  renderer.displayBuffer();
}
