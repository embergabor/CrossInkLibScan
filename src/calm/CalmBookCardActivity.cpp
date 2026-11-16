#include "CalmBookCardActivity.h"

#include <HalStorage.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <Logging.h>
#include <Memory.h>

#include <utility>

#include "CalmLayout.h"
#include "RecentBooksStore.h"
#include "activities/home/BookActions.h"
#include "activities/util/ConfirmationActivity.h"
#include "fontIds.h"

namespace {

// Greedy word wrap into at most maxLines lines; the last line is ellipsized.
std::vector<std::string> wrapText(const GfxRenderer& renderer, const int fontId, const std::string& text,
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

}  // namespace

CalmBookCardActivity::CalmBookCardActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path,
                                           std::string title, std::string detail, std::string progress)
    : Activity("CalmBookCard", renderer, mappedInput),
      path(std::move(path)),
      title(std::move(title)),
      detail(std::move(detail)),
      progress(std::move(progress)) {}

void CalmBookCardActivity::onEnter() {
  Activity::onEnter();
  completed = BookActions::isBookCompleted(path);
  actions = {Action::Open, Action::ToggleFinished, Action::Delete};
  selected = 0;
  // The long press that opened the card is still held.
  ignoreConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  requestUpdate();
}

const char* CalmBookCardActivity::actionLabel(const Action action) const {
  switch (action) {
    case Action::Open:
      return progress.empty() ? tr(STR_OPEN) : tr(STR_CONTINUE_READING);
    case Action::ToggleFinished:
      return completed ? tr(STR_MARK_UNFINISHED) : tr(STR_MARK_FINISHED);
    case Action::Delete:
      return tr(STR_DELETE);
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
      confirmDelete();
      return;
  }
}

void CalmBookCardActivity::confirmDelete() {
  auto dialog = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_CALM_DELETE_BOOK), title);
  if (!dialog) {
    LOG_ERR("CALM", "Cannot allocate delete confirmation");
    return;
  }
  startActivityForResult(std::move(dialog), [this](const ActivityResult& result) {
    if (result.isCancelled) return;
    BookActions::clearFileMetadata(path);
    if (!Storage.remove(path.c_str())) {
      LOG_ERR("CALM", "Failed to delete %s", path.c_str());
      return;
    }
    library::invalidateLibraryIndex();
    RECENT_BOOKS.removeByPath(path);
    close(Outcome::Deleted);
  });
}

void CalmBookCardActivity::loop() {
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
  const int screenHeight = renderer.getScreenHeight() - t - b;

  const int cardX = l + calm::SIDE_MARGIN;
  const int cardWidth = screenWidth - 2 * calm::SIDE_MARGIN;
  const int innerX = cardX + calm::CARD_PADDING;
  const int innerWidth = cardWidth - 2 * calm::CARD_PADDING;

  const auto titleLines = wrapText(renderer, UI_12_FONT_ID, title, innerWidth, 3, EpdFontFamily::BOLD);
  const int titleLine = renderer.getLineHeight(UI_12_FONT_ID);
  const int detailLine = renderer.getLineHeight(UI_10_FONT_ID);
  int contentHeight = static_cast<int>(titleLines.size()) * titleLine;
  if (!detail.empty()) contentHeight += detailLine + 6;
  const char* state = completed ? tr(STR_CALM_FINISHED) : progress.c_str();
  if (state && *state) contentHeight += detailLine;
  const int actionsHeight = static_cast<int>(actions.size()) * calm::ACTION_ROW_HEIGHT;
  const int cardHeight = 2 * calm::CARD_PADDING + contentHeight + calm::CARD_PADDING + actionsHeight;
  const int cardY = t + (screenHeight - cardHeight) / 3;

  // The Library stays in the framebuffer around the card.
  renderer.fillRect(cardX, cardY, cardWidth, cardHeight, false);
  renderer.drawRect(cardX, cardY, cardWidth, cardHeight, calm::CARD_BORDER, true);

  int y = cardY + calm::CARD_PADDING;
  for (const auto& line : titleLines) {
    renderer.drawText(UI_12_FONT_ID, innerX, y, line.c_str(), true, EpdFontFamily::BOLD);
    y += titleLine;
  }
  if (!detail.empty()) {
    y += 6;
    const std::string text = renderer.truncatedText(UI_10_FONT_ID, detail.c_str(), innerWidth);
    renderer.drawText(UI_10_FONT_ID, innerX, y, text.c_str());
    y += detailLine;
  }
  if (state && *state) {
    renderer.drawText(UI_10_FONT_ID, innerX, y, state);
    y += detailLine;
  }

  y = cardY + cardHeight - calm::CARD_BORDER - actionsHeight;
  renderer.fillRect(cardX, y - 1, cardWidth, 1);
  const int labelOffset = (calm::ACTION_ROW_HEIGHT - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
  for (size_t i = 0; i < actions.size(); i++) {
    const bool isSelected = static_cast<int>(i) == selected;
    if (isSelected) {
      renderer.fillRect(cardX + calm::CARD_BORDER, y, cardWidth - 2 * calm::CARD_BORDER, calm::ACTION_ROW_HEIGHT);
    }
    renderer.drawText(UI_12_FONT_ID, innerX, y + labelOffset, actionLabel(actions[i]), !isSelected);
    y += calm::ACTION_ROW_HEIGHT;
  }
  renderer.displayBuffer();
}
