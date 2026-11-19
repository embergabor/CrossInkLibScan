#pragma once

#include <cstdint>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Calm UI "Go to": a small bottom sheet over the page with the target
// position and a bar. Left/Right step by 1, Up/Down by 10 (hold to repeat),
// Confirm jumps, Back returns to the page unchanged.
//
// Books with stable page numbers pick a page (PageResult); others pick a
// whole percent (PercentResult). Both match upstream's selector, so the
// reader's handlers do the jump.
class CalmGoToActivity final : public Activity {
 public:
  // Percent mode.
  CalmGoToActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, float percent);
  // Page mode: page is 1-based.
  CalmGoToActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, uint32_t page, uint32_t pageCount);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
  bool allowPowerAsConfirmInReaderMode() const override { return true; }
  bool requiresFreshBackdrop() const override { return true; }

 private:
  bool pageMode = false;
  int32_t value = 0;
  int32_t minimum = 0;
  int32_t maximum = 100;
  bool ignoreConfirmRelease = true;
  ButtonNavigator navigator;

  void adjust(int delta);
};
