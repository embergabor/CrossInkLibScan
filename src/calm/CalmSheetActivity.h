#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"

// The calm reading sheet: one bottom-half overlay replacing the reader menu.
//
//   Library  ·  Contents  ·  Go to %          (row 0, Left/Right pick, Confirm runs)
//   <title>                        page, %, battery
//   Size / Font / Line spacing / Alignment / Margins / Full refresh
//                                             (Left/Right change the value)
//
// Up/Down move between rows; Back (or Confirm on a setting row) closes the
// sheet. Settings are global: they apply to every book, and the reader lays
// the page out again once the sheet closes.
//
// Results use the stock drawer's MenuResult so EpubReaderActivity's existing
// handler does the relayout, chapter list and percent jump.
class CalmSheetActivity final : public Activity {
 public:
  using Callback = void (*)(void*);

  struct Context {
    std::string title;
    int chapterPage = 0;
    int chapterPageCount = 0;
    float bookProgress = 0.0f;
    // Reader hooks that make edits global rather than per-book overrides.
    Callback beginGlobalEdit = nullptr;
    Callback saveGlobal = nullptr;
    Callback endGlobalEdit = nullptr;
    void* readerContext = nullptr;
  };

  CalmSheetActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Context context);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool requiresFreshBackdrop() const override { return true; }

 private:
  enum class Row : uint8_t { Actions = 0, Size, Font, LineSpacing, Alignment, Margins, FullRefresh, Count };
  enum class ActionChip : uint8_t { Library = 0, Contents, GoToPercent, Count };

  struct FontChoice {
    std::string label;
    uint8_t settingIndex;  // index understood by buildFontFamilySetting()
  };

  Context ctx;
  int row = static_cast<int>(Row::Size);
  int chip = 0;
  bool changed = false;
  bool ignoreConfirmRelease = true;
  std::vector<uint8_t> sizes;
  std::vector<FontChoice> fonts;

  void loadChoices();
  void step(Row which, int delta);
  std::string valueLabel(Row which) const;
  const char* rowLabel(Row which) const;
  void closeWithAction(int action);
  void closeSheet();

  int currentFontIndex() const;
  int marginPreset() const;
};
