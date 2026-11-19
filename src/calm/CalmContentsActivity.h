#pragma once

#include <Epub.h>

#include <memory>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Calm UI contents: the book's table of contents as one flat list in the
// Library's style. Nested entries are indented, nothing collapses, and the
// chapter being read is bold. Up/Down move (hold to page), Left/Right page,
// Confirm jumps there, Back returns to the page.
//
// Reports a ChapterResult, like upstream's chapter list, so the reader's
// existing handler does the jump.
class CalmContentsActivity final : public Activity {
 public:
  CalmContentsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::shared_ptr<Epub> epub,
                       int currentSpineIndex);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
  bool allowPowerAsConfirmInReaderMode() const override { return true; }

 private:
  std::shared_ptr<Epub> epub;
  int currentSpineIndex = 0;
  int currentItem = -1;  // TOC entry of the chapter being read
  int itemCount = 0;
  int selected = 0;
  int pageRows = 1;
  bool ignoreConfirmRelease = true;
  ButtonNavigator navigator;

  void moveTo(int index);
  void choose();
};
