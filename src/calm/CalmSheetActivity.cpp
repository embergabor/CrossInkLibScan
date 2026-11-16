#include "CalmSheetActivity.h"

#include <HalPowerManager.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

#include "CalmDraw.h"
#include "CalmLayout.h"
#include "SdCardFontSystem.h"
#include "SettingsList.h"
#include "activities/reader/EpubReaderMenuModel.h"
#include "fontIds.h"

namespace {

constexpr uint8_t LINE_HEIGHT_PRESETS[] = {90, 100, 110, 120, 135, 150};
constexpr uint8_t ALIGNMENTS[] = {CrossPointSettings::JUSTIFIED, CrossPointSettings::LEFT_ALIGN,
                                  CrossPointSettings::BOOK_STYLE};
constexpr uint8_t REFRESHES[] = {CrossPointSettings::REFRESH_1,  CrossPointSettings::REFRESH_5,
                                 CrossPointSettings::REFRESH_10, CrossPointSettings::REFRESH_15,
                                 CrossPointSettings::REFRESH_30, CrossPointSettings::REFRESH_NEVER};

// Narrow / Normal / Wide reading margins, in pixels (horizontal, vertical).
struct MarginPreset {
  uint8_t horizontal;
  uint8_t vertical;
};
constexpr MarginPreset MARGINS[] = {{10, 8}, {20, 12}, {32, 20}};

template <typename T, size_t N>
int nearestIndex(const T (&values)[N], const int current) {
  int best = 0;
  for (size_t i = 1; i < N; i++) {
    if (std::abs(values[i] - current) < std::abs(values[best] - current)) best = static_cast<int>(i);
  }
  return best;
}

int wrapIndex(const int index, const int delta, const int count) { return ((index + delta) % count + count) % count; }

}  // namespace

CalmSheetActivity::CalmSheetActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Context context)
    : Activity("CalmSheet", renderer, mappedInput), ctx(std::move(context)) {}

void CalmSheetActivity::onEnter() {
  Activity::onEnter();
  // Edit the global reader settings, not this book's overrides.
  if (ctx.beginGlobalEdit) ctx.beginGlobalEdit(ctx.readerContext);
  loadChoices();
  ignoreConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  requestUpdate();
}

void CalmSheetActivity::loadChoices() {
  // Discovery is deferred until something lists fonts; the sheet does.
  sdFontSystem.ensureRegistry();
  const SdCardFontRegistry* registry = &sdFontSystem.registry();
  sizes = buildFontSizeSetting(registry).enumRawValues;

  fonts.clear();
  // Built-in Bitter, then every SD-card family. Calm builds ship no Lexend Deca.
  fonts.push_back({tr(STR_BITTER), CrossPointSettings::BITTER});
  const auto& families = registry->getFamilies();
  fonts.reserve(families.size() + 1);
  for (size_t i = 0; i < families.size(); i++) {
    fonts.push_back({families[i].name, static_cast<uint8_t>(CrossPointSettings::BUILTIN_FONT_COUNT + i)});
  }
}

int CalmSheetActivity::currentFontIndex() const {
  if (SETTINGS.sdFontFamilyName[0] != '\0') {
    for (size_t i = 1; i < fonts.size(); i++) {
      if (fonts[i].label == SETTINGS.sdFontFamilyName) return static_cast<int>(i);
    }
  }
  return 0;
}

int CalmSheetActivity::marginPreset() const {
  int best = 0;
  for (int i = 1; i < 3; i++) {
    if (std::abs(MARGINS[i].horizontal - SETTINGS.screenMarginHorizontal) <
        std::abs(MARGINS[best].horizontal - SETTINGS.screenMarginHorizontal))
      best = i;
  }
  return best;
}

const char* CalmSheetActivity::rowLabel(const Row which) const {
  switch (which) {
    case Row::Size:
      return tr(STR_CALM_SIZE);
    case Row::Font:
      return tr(STR_CALM_FONT);
    case Row::AntiAliasing:
      return tr(STR_CALM_ANTIALIASING);
    case Row::LineSpacing:
      return tr(STR_CALM_LINE_SPACING);
    case Row::Alignment:
      return tr(STR_CALM_ALIGNMENT);
    case Row::Margins:
      return tr(STR_CALM_MARGINS);
    case Row::FullRefresh:
      return tr(STR_CALM_FULL_REFRESH);
    default:
      return "";
  }
}

std::string CalmSheetActivity::valueLabel(const Row which) const {
  char buffer[48];
  switch (which) {
    case Row::Size:
      return fontSizePointLabel(SETTINGS.readerFontPointSize);
    case Row::Font:
      return fonts.empty() ? std::string() : fonts[currentFontIndex()].label;
    case Row::AntiAliasing:
      return SETTINGS.textAntiAliasing ? tr(STR_CALM_ON) : tr(STR_CALM_OFF);
    case Row::LineSpacing:
      snprintf(buffer, sizeof(buffer), "%d.%02d", SETTINGS.lineHeightPercent / 100, SETTINGS.lineHeightPercent % 100);
      return buffer;
    case Row::Alignment:
      switch (SETTINGS.paragraphAlignment) {
        case CrossPointSettings::LEFT_ALIGN:
          return tr(STR_ALIGN_LEFT);
        case CrossPointSettings::BOOK_STYLE:
          return tr(STR_CALM_BOOK_STYLE);
        default:
          return tr(STR_CALM_JUSTIFIED);
      }
    case Row::Margins: {
      static constexpr StrId names[] = {StrId::STR_CALM_NARROW, StrId::STR_CALM_NORMAL, StrId::STR_WIDE};
      return I18N.get(names[marginPreset()]);
    }
    case Row::FullRefresh: {
      static constexpr StrId pages[] = {StrId::STR_PAGES_1, StrId::STR_PAGES_5, StrId::STR_PAGES_10,
                                        StrId::STR_PAGES_15, StrId::STR_PAGES_30};
      if (SETTINGS.refreshFrequency >= CrossPointSettings::REFRESH_NEVER) return tr(STR_NEVER);
      return I18N.get(pages[SETTINGS.refreshFrequency]);
    }
    default:
      return {};
  }
}

void CalmSheetActivity::step(const Row which, const int delta) {
  switch (which) {
    case Row::Size: {
      if (sizes.empty()) return;
      const auto it = std::find(sizes.begin(), sizes.end(), SETTINGS.readerFontPointSize);
      const int index = it == sizes.end() ? 0 : static_cast<int>(it - sizes.begin());
      const int next = std::clamp(index + delta, 0, static_cast<int>(sizes.size()) - 1);
      if (next == index) return;
      SETTINGS.readerFontPointSize = sizes[next];
      break;
    }
    case Row::Font: {
      if (fonts.size() < 2) return;
      const int next = wrapIndex(currentFontIndex(), delta, static_cast<int>(fonts.size()));
      // The upstream setter switches family and snaps the point size to one
      // the new family actually has.
      buildFontFamilySetting(&sdFontSystem.registry()).valueSetter(fonts[next].settingIndex);
      sizes = buildFontSizeSetting(&sdFontSystem.registry()).enumRawValues;
      break;
    }
    case Row::AntiAliasing:
      // Smooth gray glyph edges; Off gives crisper, faster page turns.
      SETTINGS.textAntiAliasing = SETTINGS.textAntiAliasing ? 0 : 1;
      break;
    case Row::LineSpacing: {
      const int index = nearestIndex(LINE_HEIGHT_PRESETS, SETTINGS.lineHeightPercent);
      const int next = std::clamp(index + delta, 0, static_cast<int>(std::size(LINE_HEIGHT_PRESETS)) - 1);
      if (next == index && LINE_HEIGHT_PRESETS[index] == SETTINGS.lineHeightPercent) return;
      SETTINGS.lineHeightPercent = LINE_HEIGHT_PRESETS[next];
      break;
    }
    case Row::Alignment: {
      const int index = nearestIndex(ALIGNMENTS, SETTINGS.paragraphAlignment);
      SETTINGS.paragraphAlignment = ALIGNMENTS[wrapIndex(index, delta, std::size(ALIGNMENTS))];
      break;
    }
    case Row::Margins: {
      const int next = std::clamp(marginPreset() + delta, 0, 2);
      SETTINGS.screenMarginHorizontal = MARGINS[next].horizontal;
      SETTINGS.screenMarginVertical = MARGINS[next].vertical;
      break;
    }
    case Row::FullRefresh: {
      const int index = nearestIndex(REFRESHES, SETTINGS.refreshFrequency);
      const int next = std::clamp(index + delta, 0, static_cast<int>(std::size(REFRESHES)) - 1);
      if (next == index) return;
      SETTINGS.refreshFrequency = REFRESHES[next];
      break;
    }
    default:
      return;
  }
  changed = true;
  requestUpdate();
}

void CalmSheetActivity::closeWithAction(const int action) {
  if (changed && ctx.saveGlobal) ctx.saveGlobal(ctx.readerContext);
  if (ctx.endGlobalEdit) ctx.endGlobalEdit(ctx.readerContext);
  MenuResult menu;
  menu.action = action;
  menu.orientation = SETTINGS.orientation;
  menu.settingsChanged = changed;
  menu.changeMask = changed ? ReaderSettingsChangeMask::Relayout : ReaderSettingsChangeMask::None;
  ActivityResult result;
  result.data = menu;
  result.isCancelled = action < 0;
  setResult(std::move(result));
  finish();
}

void CalmSheetActivity::closeSheet() { closeWithAction(-1); }

void CalmSheetActivity::loop() {
  using Button = MappedInputManager::Button;
  if (ignoreConfirmRelease) {
    if (!mappedInput.isPressed(Button::Confirm)) ignoreConfirmRelease = false;
    return;
  }
  if (mappedInput.wasReleased(Button::Back)) {
    closeSheet();
    return;
  }
  const auto current = static_cast<Row>(row);
  if (mappedInput.wasReleased(Button::Confirm)) {
    if (current != Row::Actions) {
      closeSheet();
      return;
    }
    switch (static_cast<ActionChip>(chip)) {
      case ActionChip::Library:
        closeWithAction(static_cast<int>(EpubReaderMenuAction::GO_HOME));
        return;
      case ActionChip::Contents:
        closeWithAction(static_cast<int>(EpubReaderMenuAction::SELECT_CHAPTER));
        return;
      default:
        closeWithAction(static_cast<int>(EpubReaderMenuAction::GO_TO_PERCENT));
        return;
    }
  }
  const int rowCount = static_cast<int>(Row::Count);
  if (mappedInput.wasReleased(Button::Down)) {
    row = std::min(row + 1, rowCount - 1);
    requestUpdate();
  } else if (mappedInput.wasReleased(Button::Up)) {
    row = std::max(row - 1, 0);
    requestUpdate();
  } else if (mappedInput.wasReleased(Button::Left) || mappedInput.wasReleased(Button::Right)) {
    const int delta = mappedInput.wasReleased(Button::Right) ? 1 : -1;
    if (current == Row::Actions) {
      chip = wrapIndex(chip, delta, static_cast<int>(ActionChip::Count));
      requestUpdate();
    } else {
      step(current, delta);
    }
  }
}

void CalmSheetActivity::render(RenderLock&&) {
  int t = 0, r = 0, b = 0, l = 0;
  renderer.getOrientedViewableTRBL(&t, &r, &b, &l);
  const int width = renderer.getScreenWidth() - l - r;
  const int bottom = renderer.getScreenHeight() - b;
  const int x = l + calm::SIDE_MARGIN;
  const int innerWidth = width - 2 * calm::SIDE_MARGIN;

  const int settingRows = static_cast<int>(Row::Count) - 1;
  constexpr int stripHeight = 58;
  const int sheetHeight = stripHeight + 8 + settingRows * calm::SHEET_ROW_HEIGHT + 6;
  const int sheetY = bottom - sheetHeight;
  // Rounded top corners; the top of the page stays visible above.
  calm::drawSheet(renderer, l, sheetY, width, sheetHeight);

  // Top strip: [‹ Library]   title / position   [Contents] [Go to %]
  const bool stripFocused = row == static_cast<int>(Row::Actions);
  // The third button shows the page and opens Go to %, as Calm UI's page button.
  const std::string libraryLabel = std::string("\xE2\x80\xB9 ") + tr(STR_LIBRARY);
  char pageLabel[24];
  snprintf(pageLabel, sizeof(pageLabel), "%d / %d", ctx.chapterPage, ctx.chapterPageCount);
  const char* buttonText[] = {libraryLabel.c_str(), tr(STR_CALM_CONTENTS), pageLabel};
  constexpr int buttonPadding = 12;
  int buttonWidth[3];
  for (int i = 0; i < 3; i++)
    buttonWidth[i] = renderer.getTextWidth(UI_10_FONT_ID, buttonText[i], EpdFontFamily::BOLD) + 2 * buttonPadding;
  const int buttonY = sheetY + (stripHeight - calm::CONTROL_HEIGHT) / 2 + 2;
  int buttonX[3];
  buttonX[0] = x;
  buttonX[2] = x + innerWidth - buttonWidth[2];
  buttonX[1] = buttonX[2] - 8 - buttonWidth[1];
  for (int i = 0; i < 3; i++) {
    const bool focused = stripFocused && chip == i;
    calm::drawButton(renderer, buttonX[i], buttonY, buttonWidth[i], calm::CONTROL_HEIGHT, focused);
    calm::drawCenteredLabel(renderer, UI_10_FONT_ID, buttonX[i], buttonY, buttonWidth[i], calm::CONTROL_HEIGHT,
                            buttonText[i], focused, EpdFontFamily::BOLD);
  }

  // Centre: book title over "page / pages · book %" and a battery glyph.
  const int centreX = buttonX[0] + buttonWidth[0] + 8;
  const int centreWidth = buttonX[1] - 8 - centreX;
  const int smallLine = renderer.getLineHeight(SMALL_FONT_ID);
  const int titleLine = renderer.getLineHeight(UI_10_FONT_ID);
  const int centreTop = sheetY + (stripHeight - titleLine - smallLine) / 2 + 2;
  const std::string title =
      renderer.truncatedText(UI_10_FONT_ID, ctx.title.c_str(), centreWidth, EpdFontFamily::BOLD);
  const int titleWidth = renderer.getTextWidth(UI_10_FONT_ID, title.c_str(), EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, centreX + (centreWidth - titleWidth) / 2, centreTop, title.c_str(), true,
                    EpdFontFamily::BOLD);
  char position[40];
  snprintf(position, sizeof(position), "%d%%", static_cast<int>(ctx.bookProgress + 0.5f));
  const int positionWidth = renderer.getTextWidth(SMALL_FONT_ID, position);
  const int infoWidth = positionWidth + 8 + calm::BATTERY_WIDTH;
  const int infoX = centreX + (centreWidth - infoWidth) / 2;
  const int infoY = centreTop + titleLine;
  renderer.drawText(SMALL_FONT_ID, infoX, infoY, position);
  calm::drawBattery(renderer, infoX + positionWidth + 8, infoY + (smallLine - calm::BATTERY_HEIGHT) / 2,
                    powerManager.getBatteryPercentage());
  renderer.fillRect(l, sheetY + stripHeight, width, 1);

  // Setting rows: label on the left, a rounded ‹ value › stepper on the right.
  constexpr int stepperWidth = 214;
  constexpr int stepperHeight = 36;
  const int uiLine = renderer.getLineHeight(UI_12_FONT_ID);
  int y = sheetY + stripHeight + 8;
  for (int i = 1; i < static_cast<int>(Row::Count); i++) {
    const auto which = static_cast<Row>(i);
    const bool focused = row == i;
    renderer.drawText(UI_12_FONT_ID, x, y + (calm::SHEET_ROW_HEIGHT - uiLine) / 2, rowLabel(which), true,
                      focused ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    const std::string value = valueLabel(which);
    calm::drawStepper(renderer, x + innerWidth - stepperWidth, y + (calm::SHEET_ROW_HEIGHT - stepperHeight) / 2,
                      stepperWidth, stepperHeight, value.c_str(), focused);
    y += calm::SHEET_ROW_HEIGHT;
  }
  renderer.displayBuffer();
}
