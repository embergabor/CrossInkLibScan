#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"

// The calm reading sheet: one bottom-half overlay replacing the reader menu.
//
//   <title, wrapped>                          battery %   (header)
//   [ Contents ]  [ page / pages · book % ]   (row 0, Left/Right pick, Confirm runs)
//   Size / Font / Antialiasing / Line spacing / Alignment / Margins / Full refresh
//                                             (Left/Right change the value)
//
// Up/Down move between rows. On a setting row, Confirm applies the changes:
// the sheet closes, the reader lays the page out again, and the sheet reopens
// on the same row, so options can be tried one after another. Back closes the
// sheet (applying anything not yet applied). There is no Library button: Back
// in the reader already leaves the book. Settings are global: they apply to every book, and the reader lays
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
    int initialRow = -1;  // row to focus when reopening after Confirm applied changes
  };

  CalmSheetActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Context context);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool requiresFreshBackdrop() const override { return true; }

 private:
  enum class Row : uint8_t {
    Actions = 0,
    Size,
    Font,
    AntiAliasing,
    LineSpacing,
    Alignment,
    Margins,
    FullRefresh,
    Count
  };
  enum class ActionChip : uint8_t { Contents = 0, GoToPercent, Count };

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
  void closeWithAction(int action, bool reopen = false);
  void closeSheet();

  int currentFontIndex() const;
  int marginPreset() const;
};
