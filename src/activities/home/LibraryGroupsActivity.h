#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Lists the distinct authors or series in the library, each with a book count.
// Selecting one opens a LibraryListActivity filtered to that author/series.
// Groups are collected by streaming the on-SD index once on entry.
class LibraryGroupsActivity final : public Activity {
 public:
  enum class Group : uint8_t { Authors, Series };

  LibraryGroupsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Group group);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct Entry {
    std::string value;  // raw author/series string ("" = unknown author)
    uint32_t count = 0;
  };

  void loadGroups();
  std::string displayName(const Entry& e) const;
  void openGroup(const Entry& e);

  Group group;
  ButtonNavigator buttonNavigator;
  std::vector<Entry> groups;
  size_t selectorIndex = 0;
};
