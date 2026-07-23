#pragma once

#include <cstdint>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Library entry point: a small picker over the metadata library built by a scan.
// Rows: All Books, Authors, Series (each with a count) and Scan/Rescan Library.
// Selecting a grouping opens the matching list; Scan (re)builds the on-SD index.
class LibraryActivity final : public Activity {
 public:
  LibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Library", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Row : uint8_t { AllBooks, Authors, Series, FileBrowser, Scan, kCount };
  static constexpr int kRowCount = static_cast<int>(Row::kCount);

  void loadCounts();
  void activateRow(Row row);

  ButtonNavigator buttonNavigator;
  size_t selectorIndex = 0;

  uint32_t bookCount = 0;
  uint32_t authorCount = 0;
  uint32_t seriesCount = 0;
};
