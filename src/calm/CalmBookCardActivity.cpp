#include "CalmBookCardActivity.h"

#include <HalStorage.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <utility>

#include "CalmDraw.h"
#include "CalmLayout.h"
#include "RecentBooksStore.h"
#include "activities/home/BookActions.h"
#include "fontIds.h"


CalmBookCardActivity::CalmBookCardActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path,
                                           std::string title, std::string detail, std::string progress,
                                           const int anchorTop, const int anchorBottom)
    : Activity("CalmBookCard", renderer, mappedInput),
      path(std::move(path)),
      title(std::move(title)),
      detail(std::move(detail)),
      progress(std::move(progress)),
      anchorTop(anchorTop),
      anchorBottom(anchorBottom) {}

void CalmBookCardActivity::onEnter() {
  Activity::onEnter();
  completed = BookActions::isBookCompleted(path);
  showActions(false);
  // The long press that opened the card is still held.
  ignoreConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  requestUpdate();
}

const char* CalmBookCardActivity::actionLabel(const Action action) const {
  switch (action) {
    case Action::Open:
      return progress.empty() ? tr(STR_CALM_OPEN) : tr(STR_CALM_CONTINUE);
    case Action::ToggleFinished:
      return completed ? tr(STR_CALM_MARK_UNREAD) : tr(STR_CALM_MARK_FINISHED);
    case Action::Delete:
    case Action::ConfirmDelete:
      return tr(STR_DELETE);
    case Action::CancelDelete:
      return tr(STR_CANCEL);
  }
  return "";
}

void CalmBookCardActivity::close(const Outcome outcome) {
  setResult(OptionSelectionResult{static_cast<uint8_t>(outcome)});
  finish();
}

void CalmBookCardActivity::run(const Action action) {
  switch (action) {
    case Action::Open:
      close(Outcome::Open);
      return;
    case Action::ToggleFinished: {
      bool nowCompleted = completed;
      if (!BookActions::toggleBookCompleted(path, title, nowCompleted)) {
        LOG_ERR("CALM", "Cannot change finished state for %s", path.c_str());
        return;
      }
      close(Outcome::Changed);
      return;
    }
    case Action::Delete:
      showActions(true);
      return;
    case Action::CancelDelete:
      showActions(false);
      return;
    case Action::ConfirmDelete:
      deleteBook();
      return;
  }
}

void CalmBookCardActivity::showActions(const bool confirmDelete) {
  confirmingDelete = confirmDelete;
  if (confirmDelete) {
    // Cancel first, so a stray Confirm never deletes.
    actions = {Action::CancelDelete, Action::ConfirmDelete};
    selected = 0;
  } else {
    actions = {Action::Open, Action::ToggleFinished, Action::Delete};
    selected = 0;
  }
  requestUpdate();
}

void CalmBookCardActivity::deleteBook() {
  BookActions::clearFileMetadata(path);
  if (!Storage.remove(path.c_str())) {
    LOG_ERR("CALM", "Failed to delete %s", path.c_str());
    showActions(false);
    return;
  }
  library::invalidateLibraryIndex();
  RECENT_BOOKS.removeByPath(path);
  close(Outcome::Deleted);
}

void CalmBookCardActivity::loop() {
  using Button = MappedInputManager::Button;
  if (ignoreConfirmRelease) {
    if (!mappedInput.isPressed(Button::Confirm)) ignoreConfirmRelease = false;
    return;
  }
  if (mappedInput.wasReleased(Button::Back)) {
    if (confirmingDelete) {
      showActions(false);
      return;
    }
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }
  if (mappedInput.wasReleased(Button::Confirm)) {
    run(actions[selected]);
    return;
  }
  const int count = static_cast<int>(actions.size());
  if (mappedInput.wasReleased(Button::Down) || mappedInput.wasReleased(Button::Right)) {
    selected = (selected + 1) % count;
    requestUpdate();
  } else if (mappedInput.wasReleased(Button::Up) || mappedInput.wasReleased(Button::Left)) {
    selected = (selected + count - 1) % count;
    requestUpdate();
  }
}

void CalmBookCardActivity::render(RenderLock&&) {
  int t = 0, r = 0, b = 0, l = 0;
  renderer.getOrientedViewableTRBL(&t, &r, &b, &l);
  const int screenWidth = renderer.getScreenWidth() - l - r;
  const int screenBottom = renderer.getScreenHeight() - b;

  constexpr int pad = 12;
  constexpr int buttonHeight = 40;
  constexpr int buttonGap = 8;
  const int cardX = l + calm::SIDE_MARGIN;
  const int cardWidth = screenWidth - 2 * calm::SIDE_MARGIN;
  const int innerX = cardX + pad;
  const int innerWidth = cardWidth - 2 * pad;

  // Serif title, as in Calm UI's card.
  const auto titleLines = calm::wrapText(renderer, BITTER_14_FONT_ID, title, innerWidth, 3, EpdFontFamily::BOLD);
  const int titleLine = renderer.getLineHeight(BITTER_14_FONT_ID);
  const int detailLine = renderer.getLineHeight(UI_10_FONT_ID);
  // Confirming a delete swaps the status line for the question, so the card
  // keeps one height and leaves nothing stale behind when it toggles back.
  const char* state = confirmingDelete ? tr(STR_CALM_DELETE_BOOK) : completed ? tr(STR_CALM_FINISHED) : progress.c_str();
  const bool hasState = state && *state;
  const int bodyHeight = static_cast<int>(titleLines.size()) * titleLine + (detail.empty() ? 0 : 4 + detailLine) +
                         (hasState ? 2 + detailLine : 0);
  const int cardHeight = pad + bodyHeight + pad + buttonHeight + pad;

  // Beside the selected row: below it in the top half, above it otherwise.
  // Without an anchor (the cover grid) it is a centered modal.
  const int listTop = t + calm::HEADER_HEIGHT + 8;
  const bool anchored = anchorTop >= 0;
  int cardY = !anchored ? (t + screenBottom - cardHeight) / 2
              : (anchorTop + anchorBottom) / 2 < (t + screenBottom) / 2 ? anchorBottom + 10
                                                                        : anchorTop - 10 - cardHeight;
  cardY = std::clamp(cardY, listTop, screenBottom - calm::FOOTER_HEIGHT - cardHeight);

  // Clear every Library row the card overlaps (whole rows, never half a line
  // of text), keeping the selected row beside it visible.
  const int rowsTop = t + calm::HEADER_HEIGHT;
  const int clearTop = std::max(rowsTop, rowsTop + (cardY - 4 - rowsTop) / calm::ROW_HEIGHT * calm::ROW_HEIGHT);
  const int below = cardY + cardHeight + 4 - rowsTop;
  const int clearBottom = std::min(screenBottom - calm::FOOTER_HEIGHT,
                                   rowsTop + (below + calm::ROW_HEIGHT - 1) / calm::ROW_HEIGHT * calm::ROW_HEIGHT);
  // The card sits wholly below or above the selected row and both of those
  // edges are row boundaries, so this never erases the selected book.
  if (anchored && clearBottom > clearTop) {
    renderer.fillRect(l, clearTop, screenWidth, clearBottom - clearTop, false);
  } else if (!anchored) {
    // A white band keeps the modal clear of the covers behind it.
    renderer.fillRoundedRect(cardX - 6, cardY - 6, cardWidth + 12, cardHeight + 12, calm::SHEET_RADIUS + 6,
                             Color::White);
  }
  renderer.fillRoundedRect(cardX, cardY, cardWidth, cardHeight, calm::SHEET_RADIUS, Color::White);
  renderer.drawRoundedRect(cardX, cardY, cardWidth, cardHeight, calm::CARD_BORDER, calm::SHEET_RADIUS, true);

  int y = cardY + pad;
  for (const auto& line : titleLines) {
    renderer.drawText(BITTER_14_FONT_ID, innerX, y, line.c_str(), true, EpdFontFamily::BOLD);
    y += titleLine;
  }
  if (!detail.empty()) {
    y += 4;
    const std::string text = renderer.truncatedText(UI_10_FONT_ID, detail.c_str(), innerWidth);
    renderer.drawText(UI_10_FONT_ID, innerX, y, text.c_str());
    y += detailLine;
  }
  if (hasState) {
    y += 2;
    renderer.drawText(UI_10_FONT_ID, innerX, y, state, true, EpdFontFamily::BOLD);
  }

  y = cardY + cardHeight - pad - buttonHeight;
  // Actions side by side as rounded buttons; the focused one is filled.
  const int count = static_cast<int>(actions.size());
  const int buttonWidth = (innerWidth - (count - 1) * buttonGap) / count;
  for (int i = 0; i < count; i++) {
    const int bx = innerX + i * (buttonWidth + buttonGap);
    const bool focused = i == selected;
    calm::drawButton(renderer, bx, y, buttonWidth, buttonHeight, focused);
    calm::drawCenteredLabel(renderer, UI_10_FONT_ID, bx, y, buttonWidth, buttonHeight, actionLabel(actions[i]),
                            focused);
  }
  renderer.displayBuffer();
}
