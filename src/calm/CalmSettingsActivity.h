#pragma once

#include <cstdint>
#include <string>

#include "activities/Activity.h"

// Calm UI settings: the handful of things not on the reading sheet, as one
// list in the Library's style.
//
//   File transfer, Calibre wireless, Refresh library   (Confirm runs)
//   Sleep screen, Time to sleep                        (Left/Right change)
//   More settings                                      (upstream's full screen)
//
// Back returns to the Library.
class CalmSettingsActivity final : public Activity {
 public:
  CalmSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Row : uint8_t {
    FileTransfer = 0,
    Calibre,
    RefreshLibrary,
    SleepScreen,
    SleepTimeout,
    More,
    Count
  };

  int selected = 0;
  bool changed = false;
  bool ignoreConfirmRelease = true;

  const char* rowLabel(Row row) const;
  std::string valueLabel(Row row) const;
  void activate(Row row);
  void step(Row row, int delta);
  void saveIfChanged();
};
