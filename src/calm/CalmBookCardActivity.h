#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"

// A book's details card, opened by holding Confirm on a Library row.
// Drawn as a bordered modal over the Library, which stays visible around it.
// Up/Down pick an action, Confirm runs it, Back closes the card.
//
// Reports back through OptionSelectionResult::index as an Outcome.
class CalmBookCardActivity final : public Activity {
 public:
  enum class Outcome : uint8_t { Open = 0, Changed = 1, Deleted = 2 };

  // anchorTop/anchorBottom: screen rows of the selected book, which the card
  // sits beside (below it, or above it near the bottom) rather than covering.
  CalmBookCardActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path, std::string title,
                       std::string detail, std::string progress, int anchorTop, int anchorBottom);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool blocksGlobalInput() const override { return true; }

 private:
  enum class Action : uint8_t { Open, ToggleFinished, Delete, CancelDelete, ConfirmDelete };

  std::string path;
  std::string title;
  std::string detail;
  std::string progress;
  int anchorTop = 0;
  int anchorBottom = 0;
  std::vector<Action> actions;
  int selected = 0;
  bool completed = false;
  bool ignoreConfirmRelease = true;
  bool confirmingDelete = false;

  const char* actionLabel(Action action) const;
  void run(Action action);
  void close(Outcome outcome);
  void showActions(bool confirmDelete);
  void deleteBook();
};
