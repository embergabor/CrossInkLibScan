#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "library/LibraryScanner.h"

// Runs a full library scan, one book per loop tick so the e-ink UI stays
// responsive, and renders progress. Back aborts a running scan; when the scan
// finishes it shows a summary until the user backs out. The parent reloads its
// counts from the freshly built index when this activity finishes.
class ScanLibraryActivity final : public Activity {
 public:
  ScanLibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ScanLibrary", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  // Skip the idle loop delay while scanning so books are processed back to back.
  bool skipLoopDelay() override;

 private:
  LibraryScanner scanner;
  std::string currentTitle;
  bool started = false;
  bool finished = false;
  bool failed = false;
  // Newest-first feed of recently imported "Author - Title" lines.
  std::vector<std::string> feed;
  size_t scannedCount = 0;  // books successfully parsed and indexed
  uint8_t spinnerFrame = 0;
};
