#pragma once

#include <LibraryBuilder.h>
#include <LibraryIndexFile.h>

#include <array>
#include <cstdint>
#include <string>

#include "RecentBooksStore.h"
#include "activities/Activity.h"
#include "CalmLayout.h"
#include "util/ButtonNavigator.h"

// Calm UI home screen: every book on the card as one quiet list.
//
// Layout (portrait, 480x800 on X4): a header with "Library", the sort and
// Settings, then rows of a small cover beside title, author/series and
// progress. The selected row is a filled pill, because without touch the
// cursor has to be unmistakable.
//
// Covers come from the EPUB's own cover image, scaled once into the book's
// cache folder. All missing covers are made in one pass with a progress
// screen when the Library opens after a scan; after that they are only read.
//
// The header's view control switches to a 3 x 3 grid of larger covers with
// the title and progress under each.
//
// Buttons: Up/Down move through books (hold to page). Back (or Up past the
// first book) focuses the header, where Left/Right pick Sort, View or
// Settings and Confirm activates it; Back again returns to the book you were
// on. In the list Left/Right page; in the grid they move between covers.
// Confirm opens the book and a long Confirm opens its card (Continue
// reading, Mark finished, Delete).
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

  enum class Cover : uint8_t { None, Ready };

  struct Row {
    std::string title;
    std::string detail;    // author, then series and its number
    std::string progress;  // "42%", "Finished", or empty for unread books
    std::string path;
    std::string coverPath;  // cached thumbnail BMP (EPUB only)
    Cover cover = Cover::None;
  };

  // Header focus stops: the four sort segments, the list/grid control, then the Settings cog.
  static constexpr int HEADER_VIEW_STOP = static_cast<int>(Sort::Count);
  static constexpr int HEADER_SETTINGS_STOP = static_cast<int>(Sort::Count) + 1;
  static constexpr int HEADER_STOPS = static_cast<int>(Sort::Count) + 2;

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
  int lastListSelection = 0;  // where Back from the header returns
  int headerFocus = 0;
  int pageRows = 1;
  int rowHeight = 72;  // calm::ROW_HEIGHT stretched to fill the list area
  bool grid = false;   // cover grid instead of the list
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
  void setSort(Sort next);
  void openSelected();
  void openBookCard();
  void reload();
  void prepareCovers();
  void drawCoverProgress(int done, int total) const;

  static const char* sortLabel(Sort sort);
  void drawScanScreen(const char* message) const;
  void drawHeader() const;
  void drawRow(int slot, const Row& row, bool selected) const;
  void drawCover(const Row& row, int x, int y, bool selected) const;
  void drawGridCell(int slot, const Row& row, bool selected) const;
  void setGrid(bool on);
  int coverWidth() const;
  int coverHeight() const;
  int columns() const { return grid ? calm::GRID_COLUMNS : 1; }

  static bool scanCancelRequested(void* context);
  static void onScanProgress(void* context, const library::BuildProgress& progress);
};
