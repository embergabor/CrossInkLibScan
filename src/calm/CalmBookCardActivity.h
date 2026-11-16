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

  CalmBookCardActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path, std::string title,
                       std::string detail, std::string progress);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool blocksGlobalInput() const override { return true; }

 private:
  enum class Action : uint8_t { Open, ToggleFinished, Delete };

  std::string path;
  std::string title;
  std::string detail;
  std::string progress;
  std::vector<Action> actions;
  int selected = 0;
  bool completed = false;
  bool ignoreConfirmRelease = true;

  const char* actionLabel(Action action) const;
  void run(Action action);
  void close(Outcome outcome);
  void confirmDelete();
};
