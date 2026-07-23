#include "ScanLibraryActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>

#include "MappedInputManager.h"
#include "components/CompactHeader.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr size_t RENDER_EVERY = 5;  // refresh the e-ink progress every N books
}  // namespace

void ScanLibraryActivity::onEnter() {
  Activity::onEnter();
  started = scanner.begin();
  failed = !started;
  finished = false;
  lastRenderedAt = 0;
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
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (finished || failed) return;

  if (!scanner.done()) {
    scanner.step(currentTitle);
    const size_t processed = scanner.processed();
    if (processed - lastRenderedAt >= RENDER_EVERY || processed == scanner.total()) {
      lastRenderedAt = processed;
      requestUpdate();
    }
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

  char line[64];
  if (failed) {
    renderer.drawText(UI_12_FONT_ID, x, contentTop + 24, tr(STR_LIBRARY_EMPTY));
  } else if (finished) {
    std::snprintf(line, sizeof(line), "%s (%zu)", tr(STR_LIBRARY_SCAN_DONE), scanner.total());
    renderer.drawText(UI_12_FONT_ID, x, contentTop + 24, line);
  } else {
    std::snprintf(line, sizeof(line), "%zu / %zu", scanner.processed(), scanner.total());
    renderer.drawText(UI_12_FONT_ID, x, contentTop + 24, line);
    if (!currentTitle.empty()) {
      renderer.drawText(UI_10_FONT_ID, x, contentTop + 56, currentTitle.c_str());
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

bool ScanLibraryActivity::skipLoopDelay() { return started && !finished && !failed; }
