#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "library/LibraryIndex.h"
#include "util/ButtonNavigator.h"

// Lists books from the on-SD LibraryIndex, optionally filtered to one author or
// one series. Only a vector of matching record indices lives in RAM; each row's
// text is decoded on demand from the index (mirrors RecentBooksActivity's list
// UI, but sourced from the library index instead of the recents store).
class LibraryListActivity final : public Activity {
 public:
  enum class Filter : uint8_t { All, ByAuthor, BySeries };

  LibraryListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title, Filter filter,
                      std::string filterValue);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  const LibraryIndex::Record& recordFor(int visibleIndex);
  std::string subtitleFor(int visibleIndex);

  std::string title;
  Filter filter;
  std::string filterValue;

  ButtonNavigator buttonNavigator;
  LibraryIndex index;
  std::vector<uint32_t> rows;  // matching record indices, in display order
  size_t selectorIndex = 0;

  // Single-entry decode cache so title/subtitle/icon for one row don't each hit
  // the SD card separately.
  int cachedVisible = -1;
  LibraryIndex::Record cachedRec;
};
