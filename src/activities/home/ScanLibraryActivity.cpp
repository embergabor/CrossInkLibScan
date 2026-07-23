#include "ScanLibraryActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "MappedInputManager.h"
#include "components/CompactHeader.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr size_t MAX_FEED = 30;               // enough feed lines to fill the screen (render clips to fit)
constexpr char SPINNER[] = {'|', '/', '-', '\\'};
}  // namespace

void ScanLibraryActivity::onEnter() {
  Activity::onEnter();
  feed.clear();
  scannedCount = 0;
  spinnerFrame = 0;
  started = scanner.begin();
  failed = !started;
  finished = false;
  // An empty library finishes immediately.
  if (started && scanner.done()) {
    finished = scanner.finish();
    failed = !finished;
  }
  requestUpdate();
}

void ScanLibraryActivity::onExit() {
  Activity::onExit();
  if (started && !finished && !failed) {
    scanner.abort();
  }
}

void ScanLibraryActivity::loop() {
  // The scan blocks the single core while parsing each book, so input is only
  // polled between books. A press+release during a parse is never polled (the
  // edge is lost), so check the held state instead: holding Back cancels after
  // the current book finishes. finishAfterBackPress() swallows the release so
  // the Library screen underneath doesn't also act on it.
  if (mappedInput.isPressed(MappedInputManager::Button::Back)) {
    finishAfterBackPress();
    return;
  }
  if (finished || failed) return;

  if (!scanner.done()) {
    scanner.step(currentTitle);
    // Only count and list books that actually parsed; failures are skipped.
    if (scanner.lastImportSucceeded()) {
      scannedCount++;
      const std::string& author = scanner.lastAuthor();
      const std::string& title = scanner.lastTitle();
      feed.insert(feed.begin(), author.empty() ? title : author + " - " + title);
      if (feed.size() > MAX_FEED) feed.resize(MAX_FEED);
    }
    spinnerFrame++;         // advance the spinner every book, including skips
    requestUpdate();        // refresh once per book
  } else {
    finished = scanner.finish();
    failed = !finished;
    requestUpdate();
  }
}

void ScanLibraryActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int x = metrics.contentSidePadding;
  const int contentTop = CompactHeader::contentTop(metrics);

  CompactHeader::drawTitle(renderer, tr(STR_LIBRARY_SCANNING));

  char line[80];
  if (failed) {
    renderer.drawText(UI_12_FONT_ID, x, contentTop + 24, tr(STR_LIBRARY_EMPTY));
  } else if (finished) {
    std::snprintf(line, sizeof(line), "%s (%zu)", tr(STR_LIBRARY_SCAN_DONE), scannedCount);
    renderer.drawText(UI_12_FONT_ID, x, contentTop + 24, line);
  } else {
    // Progress line: books successfully scanned out of total found, plus a
    // per-book spinner as a "still running" indicator.
    std::snprintf(line, sizeof(line), "%c   %zu / %zu", SPINNER[spinnerFrame % sizeof(SPINNER)], scannedCount,
                  scanner.total());
    renderer.drawText(UI_12_FONT_ID, x, contentTop + 20, line);

    // Scrolling feed: most recent import on top, older ones below.
    constexpr int feedLineHeight = 28;
    const int feedTop = contentTop + 60;
    const int feedBottom =
        renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing;
    const int maxRows = std::max(0, (feedBottom - feedTop) / feedLineHeight);
    const int rows = std::min(static_cast<int>(feed.size()), maxRows);
    for (int i = 0; i < rows; ++i) {
      renderer.drawText(UI_10_FONT_ID, x, feedTop + i * feedLineHeight, feed[i].c_str());
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

bool ScanLibraryActivity::skipLoopDelay() { return started && !finished && !failed; }
