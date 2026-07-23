#include "LibraryActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "FileBrowserActivity.h"
#include "LibraryGroupsActivity.h"
#include "LibraryListActivity.h"
#include "ScanLibraryActivity.h"
#include "MappedInputManager.h"
#include "components/CompactHeader.h"
#include "components/UITheme.h"

void LibraryActivity::loadCounts() {
  bookCount = 0;
  authorCount = 0;
  seriesCount = 0;

  LibraryIndex index;
  if (!index.open()) return;

  std::vector<std::string> authors;
  std::vector<std::string> series;
  LibraryIndex::Record r;
  for (uint32_t i = 0; i < index.count(); ++i) {
    if (!index.recordAt(i, r)) continue;
    bookCount++;
    if (std::find(authors.begin(), authors.end(), r.author) == authors.end()) authors.push_back(r.author);
    if (!r.series.empty() && std::find(series.begin(), series.end(), r.series) == series.end()) {
      series.push_back(r.series);
    }
  }
  index.close();
  authorCount = static_cast<uint32_t>(authors.size());
  seriesCount = static_cast<uint32_t>(series.size());
}

void LibraryActivity::onEnter() {
  Activity::onEnter();
  loadCounts();
  selectorIndex = 0;
  requestUpdate();
}

void LibraryActivity::onExit() { Activity::onExit(); }

void LibraryActivity::activateRow(Row row) {
  switch (row) {
    case Row::AllBooks:
      startActivityForResult(std::make_unique<LibraryListActivity>(renderer, mappedInput, tr(STR_LIBRARY_ALL_BOOKS),
                                                                   LibraryListActivity::Filter::All, std::string{}),
                             [this](const ActivityResult&) { requestUpdate(true); });
      return;
    case Row::Authors:
      startActivityForResult(
          std::make_unique<LibraryGroupsActivity>(renderer, mappedInput, LibraryGroupsActivity::Group::Authors),
          [this](const ActivityResult&) { requestUpdate(true); });
      return;
    case Row::Series:
      startActivityForResult(
          std::make_unique<LibraryGroupsActivity>(renderer, mappedInput, LibraryGroupsActivity::Group::Series),
          [this](const ActivityResult&) { requestUpdate(true); });
      return;
    case Row::FileBrowser:
      // Open the raw file browser nested under Library; Back returns here.
      startActivityForResult(
          std::make_unique<FileBrowserActivity>(renderer, mappedInput, "/", FileBrowserActivity::Mode::Books,
                                                /*finishAtRootOnBack=*/true),
          [this](const ActivityResult&) { requestUpdate(true); });
      return;
    case Row::Scan:
      startActivityForResult(std::make_unique<ScanLibraryActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) {
                               // Rebuild the counts to reflect the freshly scanned index.
                               loadCounts();
                               requestUpdate(true);
                             });
      return;
    case Row::kCount:
      return;
  }
}

void LibraryActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateRow(static_cast<Row>(selectorIndex));
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }

  buttonNavigator.onNextRelease([this] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), kRowCount);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), kRowCount);
    requestUpdate();
  });
}

void LibraryActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  CompactHeader::drawTitle(renderer, tr(STR_MENU_LIBRARY));

  const int contentTop = CompactHeader::contentTop(metrics);
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  const char* labelsByRow[kRowCount] = {tr(STR_LIBRARY_ALL_BOOKS), tr(STR_LIBRARY_AUTHORS), tr(STR_LIBRARY_SERIES),
                                        tr(STR_BROWSE_FILES),
                                        bookCount > 0 ? tr(STR_LIBRARY_RESCAN) : tr(STR_LIBRARY_SCAN)};
  const uint32_t countsByRow[kRowCount] = {bookCount, authorCount, seriesCount, 0, 0};
  // Only the grouping rows (All Books, Authors, Series) show a count.
  constexpr int kCountedRows = static_cast<int>(Row::Series) + 1;

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, kRowCount, static_cast<int>(selectorIndex),
      [&labelsByRow](int index) { return std::string(labelsByRow[index]); }, nullptr, nullptr,
      [&countsByRow](int index) {
        return index < kCountedRows ? std::to_string(countsByRow[index]) : std::string{};
      });

  const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
