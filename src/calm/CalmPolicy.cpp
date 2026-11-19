#include "CalmPolicy.h"

#include <Logging.h>

#include "CrossPointSettings.h"

namespace calm {

void applySettingsPolicy(CrossPointSettings& settings) {
#if CROSSINK_CALM_UI
  // Calm builds do not ship the Lexend Deca bitmaps; its font IDs are aliased
  // to Bitter in main.cpp. Store the family that is actually drawn.
  if (settings.fontFamily == CrossPointSettings::LEXENDDECA) settings.fontFamily = CrossPointSettings::BITTER;

  // Reader chrome: none. Every pixel of the X4's page goes to text; position
  // and battery live in the reading sheet.
  ReaderStatusBarConfig top;
  top.progressBar = CrossPointSettings::HIDE_PROGRESS;
  settings.topReaderStatusBar = top;

  ReaderStatusBarConfig bottom;
  bottom.progressBar = CrossPointSettings::HIDE_PROGRESS;
  settings.bottomReaderStatusBar = bottom;

  // Upstream's 5 px reading margins are cramped at 220 ppi. A settings file
  // still on that default starts on the sheet's "Normal" preset instead.
  if (settings.screenMarginHorizontal == 5 && settings.screenMarginVertical == 5) {
    settings.screenMarginHorizontal = 20;
    settings.screenMarginVertical = 12;
  }

  // Upstream's 100% line height is dense next to Calm UI's reading page; an
  // untouched default starts at the sheet's 1.20 step instead.
  if (settings.lineHeightPercent == 100) settings.lineHeightPercent = 120;

  // No hyphenation, as in Calm UI for KOReader. The sheet has no toggle.
  settings.hyphenationEnabled = 0;

  // Upstream screens draw with the calm theme (UITheme); it is built on
  // RoundedRaff, and code that checks the stored theme should agree.
  settings.uiTheme = CrossPointSettings::UI_THEME::ROUNDEDRAFF;

  // The calm Library shows titles and authors from EPUB metadata.
  settings.libraryUseMetadata = 1;

  LOG_DBG("CALM", "Settings policy applied");
#else
  (void)settings;
#endif
}

}  // namespace calm
