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

  // The calm Library shows titles and authors from EPUB metadata.
  settings.libraryUseMetadata = 1;

  LOG_DBG("CALM", "Settings policy applied");
#else
  (void)settings;
#endif
}

}  // namespace calm
