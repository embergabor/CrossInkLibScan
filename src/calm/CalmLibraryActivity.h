#pragma once

#include <LibraryBuilder.h>
#include <LibraryIndexFile.h>

#include <array>
#include <cstdint>
#include <string>

#include "RecentBooksStore.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Calm UI home screen: every book on the card as one quiet list.
//
// Layout (portrait, 480x800 on X4): a 44 px header with "Library", the sort
// and Settings, then 64 px rows of title over author/series/progress. The
// selected row is drawn inverted, because without touch the cursor has to be
// unmistakable.
//
// Buttons: Up/Down move through books (hold to page); moving up past the first
// book focuses the header, where Left/Right pick Sort or Settings and Confirm
// activates it. In the list Left/Right page, Confirm opens the book and a long
// Confirm opens its card (Continue reading, Mark finished, Delete).
//
// Data comes from upstream's on-SD Library index (lib/LibraryIndex); only the
// rows on screen are read, so RAM stays flat however large the card is.
class CalmLibraryActivity final : public Activity {
 public:
  enum class Sort : uint8_t { Recent = 0, Title, Author, Series, Count };

  CalmLibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isHomeActivity() const override { return false; }

 private:
  static constexpr int MAX_PAGE_ROWS = 16;
  static constexpr unsigned long LONG_PRESS_MS = 600;

  struct Row {
    std::string title;
    std::string detail;    // author, then series and its number
    std::string progress;  // "42%", "Finished", or empty
    std::string path;
  };

  // Header focus: the list owns selection >= 0; the header uses these.
  enum class HeaderItem : uint8_t { Sort = 0, Settings = 1 };

  library::LibraryIndexFile index;
  Sort sort = Sort::Recent;
  ButtonNavigator navigator;

  // Recent sort: books opened most recently, as rows of the index's date-added
  // (ascending) permutation, followed by every other book newest-added first.
  std::array<uint16_t, RecentBooksStore::MAX_RECENT_BOOKS> recentRows{};
  std::array<uint16_t, RecentBooksStore::MAX_RECENT_BOOKS> excludedDescPositions{};
  uint8_t recentCount = 0;

  int bookCount = 0;
  int selection = 0;  // -1 = header
  HeaderItem headerItem = HeaderItem::Sort;
  int pageRows = 1;
  int loadedPage = -1;
  int pageRowCount = 0;
  std::array<Row, MAX_PAGE_ROWS> rows;

  bool confirmHeld = false;
  bool ignoreConfirmRelease = false;
  bool scanFailed = false;

  // Layout
  int top = 0;
  int left = 0;
  int width = 0;
  int height = 0;

  void computeLayout();
  bool ensureIndex();
  bool rebuildIndex();
  void resolveRecents();
  library::SortOrder indexOrder() const;
  uint16_t ordinalForRow(int row);
  void loadPage(int page);
  bool readRow(int row, Row& out);
  void moveSelection(int next);
  void cycleSort();
  void openSelected();
  void openBookCard();
  void reload();

  const char* sortLabel() const;
  void drawScanScreen(const char* message) const;
  void drawHeader() const;
  void drawRow(int slot, const Row& row, bool selected) const;

  static bool scanCancelRequested(void* context);
  static void onScanProgress(void* context, const library::BuildProgress& progress);
};
