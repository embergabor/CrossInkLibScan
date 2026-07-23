#include "LibraryGroupsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <memory>

#include "LibraryListActivity.h"
#include "MappedInputManager.h"
#include "components/CompactHeader.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "library/LibraryIndex.h"

LibraryGroupsActivity::LibraryGroupsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Group group)
    : Activity("LibraryGroups", renderer, mappedInput), group(group) {}

void LibraryGroupsActivity::loadGroups() {
  groups.clear();

  LibraryIndex index;
  if (!index.open()) return;

  LibraryIndex::Record r;
  for (uint32_t i = 0; i < index.count(); ++i) {
    if (!index.recordAt(i, r)) continue;
    const std::string& value = (group == Group::Authors) ? r.author : r.series;
    // Books with no series are simply absent from the Series grouping.
    if (group == Group::Series && value.empty()) continue;

    auto it = std::find_if(groups.begin(), groups.end(), [&](const Entry& e) { return e.value == value; });
    if (it == groups.end()) {
      groups.push_back(Entry{value, 1});
    } else {
      it->count++;
    }
  }
  index.close();

  std::sort(groups.begin(), groups.end(), [](const Entry& a, const Entry& b) { return a.value < b.value; });
}

std::string LibraryGroupsActivity::displayName(const Entry& e) const {
  if (e.value.empty()) return tr(STR_LIBRARY_NO_AUTHOR);
  return e.value;
}

void LibraryGroupsActivity::onEnter() {
  Activity::onEnter();
  loadGroups();
  selectorIndex = 0;
  requestUpdate();
}

void LibraryGroupsActivity::onExit() {
  Activity::onExit();
  groups.clear();
}

void LibraryGroupsActivity::openGroup(const Entry& e) {
  const auto filter = (group == Group::Authors) ? LibraryListActivity::Filter::ByAuthor
                                                : LibraryListActivity::Filter::BySeries;
  startActivityForResult(
      std::make_unique<LibraryListActivity>(renderer, mappedInput, displayName(e), filter, e.value),
      [this](const ActivityResult&) { requestUpdate(true); });
}

void LibraryGroupsActivity::loop() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = CompactHeader::contentTop(metrics);
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int pageItems = std::max(1, contentHeight / metrics.listWithSubtitleRowHeight);

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!groups.empty() && selectorIndex < groups.size()) {
      openGroup(groups[selectorIndex]);
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  const int listSize = static_cast<int>(groups.size());
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

void LibraryGroupsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  CompactHeader::drawTitle(renderer, group == Group::Authors ? tr(STR_LIBRARY_AUTHORS) : tr(STR_LIBRARY_SERIES));

  const int contentTop = CompactHeader::contentTop(metrics);
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (groups.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_LIBRARY_EMPTY));
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(groups.size()),
        static_cast<int>(selectorIndex), [this](int index) { return displayName(groups[index]); }, nullptr, nullptr,
        [this](int index) { return std::to_string(groups[index].count); });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
