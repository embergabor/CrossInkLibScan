#include "CalmPolicy.h"

#include <Logging.h>

#include "CrossPointSettings.h"

namespace calm {

void applySettingsPolicy(CrossPointSettings& settings) {
#if CROSSINK_CALM_UI
  // Calm builds do not ship the Lexend Deca bitmaps; its font IDs are aliased
  // to Bitter in main.cpp. Store the family that is actually drawn.
  if (settings.fontFamily == CrossPointSettings::LEXENDDECA) settings.fontFamily = CrossPointSettings::BITTER;

  // Reader chrome: the page and its number, nothing else.
  ReaderStatusBarConfig top;
  top.progressBar = CrossPointSettings::HIDE_PROGRESS;
  settings.topReaderStatusBar = top;

  ReaderStatusBarConfig bottom;
  bottom.progressBar = CrossPointSettings::HIDE_PROGRESS;
  bottom.slots[ReaderStatusBarConfig::CENTER] = ReaderStatusBarItem::ChapterPageCount;
  settings.bottomReaderStatusBar = bottom;

  // Upstream's "Recently opened" sort lists only books already opened, so a
  // fresh card looks empty. Calm lists every book: open on Title (A-Z) with
  // EPUB metadata, which also triggers the first scan.
  // Values mirror LibraryActivity::Sort { DateAdded, Title, ..., RecentlyRead=4 }.
  constexpr uint8_t kSortTitle = 1;
  constexpr uint8_t kSortRecentlyRead = 4;
  if (settings.librarySortMethod == kSortRecentlyRead) {
    settings.librarySortMethod = kSortTitle;
    settings.librarySortDescending = 0;
  }
  settings.libraryUseMetadata = 1;

  LOG_DBG("CALM", "Settings policy applied");
#else
  (void)settings;
#endif
}

}  // namespace calm
