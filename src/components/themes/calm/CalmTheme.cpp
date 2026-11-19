#include "CalmTheme.h"

#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>

#include <memory>

#include "components/UITheme.h"
#include "fontIds.h"

Rect CalmTheme::drawPopup(const GfxRenderer& renderer, const char* message, const bool preserveBackdrop) const {
  // Same geometry as BaseTheme::drawPopup, so fillPopupProgress() lines up;
  // only the paint differs: a white card, a black outline, black text.
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int marginX = metrics.popupMarginX;
  const int marginY = metrics.popupMarginY;
  const int frame = metrics.popupFrameThickness;
  const int y = static_cast<int>(renderer.getScreenHeight() * metrics.popupTopOffsetRatio);
  const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, message, EpdFontFamily::BOLD);
  const int textHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int w = textWidth + marginX * 2;
  const int h = textHeight + marginY * 2;
  const int x = (renderer.getScreenWidth() - w) / 2;

  // A white band around the card keeps it clear of the text behind it.
  constexpr int band = 6;
  const int backupX = x - frame - band;
  const int backupY = y - frame - band;
  const int backupW = w + 2 * (frame + band);
  const int backupH = h + 2 * (frame + band);
  // Only the popup's rectangle is kept, briefly, not another framebuffer.
  // Allocation failure skips the feedback without damaging the page.
  std::unique_ptr<uint8_t[]> backdrop;
  if (preserveBackdrop) {
    const size_t bytes = renderer.getRegionByteSize(backupX, backupY, backupW, backupH);
    if (bytes != 0) backdrop = makeUniqueNoThrow<uint8_t[]>(bytes);
    if (!backdrop || !renderer.copyRegionToBuffer(backupX, backupY, backupW, backupH, backdrop.get(), bytes)) {
      LOG_ERR("GUI", "Unable to preserve loading popup backdrop");
      return Rect{x, y, w, h};
    }
  }

  const int radius = metrics.popupCornerRadius;
  renderer.fillRoundedRect(backupX, backupY, backupW, backupH, radius + frame + band, Color::White);
  renderer.drawRoundedRect(x - frame, y - frame, w + 2 * frame, h + 2 * frame, frame, radius + frame, true);
  renderer.drawText(UI_12_FONT_ID, x + (w - textWidth) / 2, y + marginY + metrics.popupTextBaselineOffsetY, message,
                    true, EpdFontFamily::BOLD);
  renderer.displayBuffer();
  if (backdrop) {
    renderer.copyBufferToRegion(backupX, backupY, backupW, backupH, backdrop.get(),
                                renderer.getRegionByteSize(backupX, backupY, backupW, backupH));
  }
  return Rect{x, y, w, h};
}
