#include "LibraryListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#include "MappedInputManager.h"
#include "components/CompactHeader.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// Formats a series position: whole numbers as "3", fractional as "2.5".
std::string formatSeriesIndex(float value) {
  char buf[16];
  if (value == std::floor(value)) {
    std::snprintf(buf, sizeof(buf), "#%d", static_cast<int>(value));
  } else {
    std::snprintf(buf, sizeof(buf), "#%.1f", value);
  }
  return buf;
}
}  // namespace

LibraryListActivity::LibraryListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title,
                                         Filter filter, std::string filterValue)
    : Activity("LibraryList", renderer, mappedInput),
      title(std::move(title)),
      filter(filter),
      filterValue(std::move(filterValue)) {}

void LibraryListActivity::onEnter() {
  Activity::onEnter();

  rows.clear();
  cachedVisible = -1;
  selectorIndex = 0;

  if (index.open()) {
    LibraryIndex::Record r;
    std::vector<std::pair<float, uint32_t>> seriesRows;  // (seriesIndex, row) for ordering
    for (uint32_t i = 0; i < index.count(); ++i) {
      if (!index.recordAt(i, r)) continue;
      bool match = false;
      switch (filter) {
        case Filter::All:
          match = true;
          break;
        case Filter::ByAuthor:
          match = (r.author == filterValue);
          break;
        case Filter::BySeries:
          match = (r.series == filterValue);
          break;
      }
      if (!match) continue;
      if (filter == Filter::BySeries) {
        seriesRows.emplace_back(r.seriesIndex, i);
      } else {
        rows.push_back(i);
      }
    }
    // Volumes within a series read in reading order.
    if (filter == Filter::BySeries) {
      std::stable_sort(seriesRows.begin(), seriesRows.end(),
                       [](const auto& a, const auto& b) { return a.first < b.first; });
      rows.reserve(seriesRows.size());
      for (const auto& p : seriesRows) rows.push_back(p.second);
    }
  }

  requestUpdate();
}

void LibraryListActivity::onExit() {
  Activity::onExit();
  index.close();
  rows.clear();
}

const LibraryIndex::Record& LibraryListActivity::recordFor(int visibleIndex) {
  if (visibleIndex != cachedVisible) {
    if (visibleIndex < 0 || static_cast<size_t>(visibleIndex) >= rows.size() ||
        !index.recordAt(rows[visibleIndex], cachedRec)) {
      cachedRec = LibraryIndex::Record{};
    }
    cachedVisible = visibleIndex;
  }
  return cachedRec;
}

std::string LibraryListActivity::subtitleFor(int visibleIndex) {
  const LibraryIndex::Record& r = recordFor(visibleIndex);
  switch (filter) {
    case Filter::BySeries:
      return formatSeriesIndex(r.seriesIndex);
    case Filter::ByAuthor:
      return r.series.empty() ? std::string{} : r.series + " " + formatSeriesIndex(r.seriesIndex);
    case Filter::All:
    default:
      return r.author;
  }
}

void LibraryListActivity::loop() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = CompactHeader::contentTop(metrics);
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int pageItems = std::max(1, contentHeight / metrics.listWithSubtitleRowHeight);

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!rows.empty() && selectorIndex < rows.size()) {
      onSelectBook(recordFor(static_cast<int>(selectorIndex)).path);
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  const int listSize = static_cast<int>(rows.size());
  buttonNavigator.onNextRelease([this, listSize] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this, listSize] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });
  buttonNavigator.onNextContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });
  buttonNavigator.onPreviousContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });
}

void LibraryListActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  CompactHeader::drawTitle(renderer, title.c_str());

  const int contentTop = CompactHeader::contentTop(metrics);
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (rows.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_LIBRARY_EMPTY));
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(rows.size()),
        static_cast<int>(selectorIndex), [this](int index) { return recordFor(index).title; },
        [this](int index) { return subtitleFor(index); },
        [this](int index) { return UITheme::getFileIcon(recordFor(index).path); });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
