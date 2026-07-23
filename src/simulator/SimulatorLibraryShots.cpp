#include "SimulatorLibraryShots.h"

#ifdef SIMULATOR

#include <Arduino.h>  // millis()
#include <GfxRenderer.h>
#include <Logging.h>

#include <cstdlib>
#include <memory>
#include <string>

#include "I18n.h"
#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "activities/home/LibraryGroupsActivity.h"
#include "activities/home/LibraryListActivity.h"
#include "library/LibraryIndex.h"
#include "library/LibraryScanner.h"
#include "util/ScreenshotUtil.h"

extern ActivityManager activityManager;
extern GfxRenderer renderer;
extern MappedInputManager mappedInputManager;

namespace {
enum class Kind : uint8_t { BuildAndOpen, ShowSeriesGroups, ShowSeriesBooks, ShowAllBooks, Shot, Done };

struct Event {
  uint32_t at;  // ms since first tick
  Kind kind;
};

// Each screen is pushed directly (no button injection) then captured after it
// has had time to render.
constexpr Event SCRIPT[] = {
    {300, Kind::BuildAndOpen},      // Library picker
    {1000, Kind::Shot},
    {1200, Kind::ShowSeriesGroups}, // Series list
    {1900, Kind::Shot},
    {2100, Kind::ShowSeriesBooks},  // one series' books, ordered by index
    {2800, Kind::Shot},
    {3000, Kind::ShowAllBooks},     // flat all-books list
    {3700, Kind::Shot},
    {3900, Kind::Done},
};

void buildIndexAndOpenLibrary() {
  LibraryIndex::remove();
  LibraryScanner scanner;
  if (scanner.begin()) {
    std::string title;
    while (scanner.step(title)) {
    }
    scanner.finish();
  }
  activityManager.goToLibrary();
}
}  // namespace

void runSimulatorLibraryShotsTick() {
  static bool enabled = std::getenv("CROSSINK_SIMULATOR_LIBRARY_SHOTS") != nullptr;
  if (!enabled) return;

  static uint32_t startMs = 0;
  static size_t nextEvent = 0;
  if (startMs == 0) startMs = millis();
  if (nextEvent >= (sizeof(SCRIPT) / sizeof(SCRIPT[0]))) return;

  const uint32_t elapsed = millis() - startMs;
  const Event& ev = SCRIPT[nextEvent];
  if (elapsed < ev.at) return;
  nextEvent++;

  switch (ev.kind) {
    case Kind::BuildAndOpen:
      buildIndexAndOpenLibrary();
      break;
    case Kind::ShowSeriesGroups:
      activityManager.replaceActivity(
          std::make_unique<LibraryGroupsActivity>(renderer, mappedInputManager, LibraryGroupsActivity::Group::Series));
      break;
    case Kind::ShowSeriesBooks:
      activityManager.replaceActivity(std::make_unique<LibraryListActivity>(
          renderer, mappedInputManager, "Jack Reacher", LibraryListActivity::Filter::BySeries, "Jack Reacher"));
      break;
    case Kind::ShowAllBooks:
      activityManager.replaceActivity(std::make_unique<LibraryListActivity>(
          renderer, mappedInputManager, tr(STR_LIBRARY_ALL_BOOKS), LibraryListActivity::Filter::All, std::string{}));
      break;
    case Kind::Shot:
      ScreenshotUtil::takeScreenshot(renderer);
      LOG_INF("LSHOT", "Captured screen (event %zu)", nextEvent);
      break;
    case Kind::Done:
      LOG_INF("LSHOT", "Library shots done");
      break;
  }
}

#else

void runSimulatorLibraryShotsTick() {}

#endif
