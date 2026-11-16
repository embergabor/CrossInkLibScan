#include "CalmSheetActivity.h"

#include <HalPowerManager.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

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
    case Row::LineSpacing:
      snprintf(buffer, sizeof(buffer), "%d.%02d", SETTINGS.lineHeightPercent / 100, SETTINGS.lineHeightPercent % 100);
      return buffer;
    case Row::Alignment:
      switch (SETTINGS.paragraphAlignment) {
        case CrossPointSettings::LEFT_ALIGN:
          return tr(STR_ALIGN_LEFT);
        case CrossPointSettings::BOOK_STYLE:
          return tr(STR_BOOK_S_STYLE);
        default:
          return tr(STR_JUSTIFY);
      }
    case Row::Margins: {
      static constexpr StrId names[] = {StrId::STR_CALM_NARROW, StrId::STR_CALM_NORMAL, StrId::STR_WIDE};
      return I18N.get(names[marginPreset()]);
    }
    case Row::FullRefresh: {
      static constexpr StrId pages[] = {StrId::STR_PAGES_1, StrId::STR_PAGES_5, StrId::STR_PAGES_10,
                                        StrId::STR_PAGES_15, StrId::STR_PAGES_30};
      if (SETTINGS.refreshFrequency >= CrossPointSettings::REFRESH_NEVER) return tr(STR_NEVER);
      snprintf(buffer, sizeof(buffer), tr(STR_CALM_EVERY_PAGE_N), I18N.get(pages[SETTINGS.refreshFrequency]));
      return buffer;
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
  const int stripHeight = calm::HEADER_HEIGHT + 12;
  const int infoHeight = 40;
  const int sheetHeight = stripHeight + infoHeight + settingRows * calm::SHEET_ROW_HEIGHT;
  const int sheetY = bottom - sheetHeight;

  // The top of the page stays visible above the sheet.
  renderer.fillRect(l, sheetY, width, sheetHeight, false);
  renderer.fillRect(l, sheetY, width, 2);

  // Row 0: Library · Contents · Go to %, as three equal chips.
  const char* chips[] = {tr(STR_LIBRARY), tr(STR_CALM_CONTENTS), tr(STR_GO_TO_PERCENT)};
  const int chipWidth = innerWidth / 3;
  const int chipHeight = stripHeight - 16;
  const int chipY = sheetY + 8;
  const int uiLine = renderer.getLineHeight(UI_12_FONT_ID);
  for (int i = 0; i < 3; i++) {
    const int cx = x + i * chipWidth;
    const bool focused = row == static_cast<int>(Row::Actions) && chip == i;
    if (focused) renderer.fillRect(cx, chipY, chipWidth, chipHeight);
    const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, chips[i]);
    renderer.drawText(UI_12_FONT_ID, cx + (chipWidth - textWidth) / 2, chipY + (chipHeight - uiLine) / 2, chips[i],
                      !focused);
  }
  renderer.fillRect(l, sheetY + stripHeight - 1, width, 1);

  // Info line: book title on the left, position and battery on the right.
  char position[64];
  snprintf(position, sizeof(position), "%d / %d  \xC2\xB7  %d%%  \xC2\xB7  %u%%", ctx.chapterPage,
           ctx.chapterPageCount, static_cast<int>(ctx.bookProgress + 0.5f),
           static_cast<unsigned>(powerManager.getBatteryPercentage()));
  const int infoY = sheetY + stripHeight + (infoHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
  const int positionWidth = renderer.getTextWidth(UI_10_FONT_ID, position);
  renderer.drawText(UI_10_FONT_ID, x + innerWidth - positionWidth, infoY, position);
  const std::string title = renderer.truncatedText(UI_10_FONT_ID, ctx.title.c_str(),
                                                   innerWidth - positionWidth - calm::CHIP_GAP, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, x, infoY, title.c_str(), true, EpdFontFamily::BOLD);

  // Setting rows: label on the left, "‹ value ›" on the right.
  int y = sheetY + stripHeight + infoHeight;
  const int labelOffset = (calm::SHEET_ROW_HEIGHT - uiLine) / 2;
  for (int i = 1; i < static_cast<int>(Row::Count); i++) {
    const auto which = static_cast<Row>(i);
    const bool selected = row == i;
    renderer.fillRectDither(l, y, width, 1, LightGray);
    if (selected) renderer.fillRect(l, y, width, calm::SHEET_ROW_HEIGHT);
    renderer.drawText(UI_12_FONT_ID, x, y + labelOffset, rowLabel(which), !selected);
    const std::string value = "\xE2\x80\xB9  " + valueLabel(which) + "  \xE2\x80\xBA";  // ‹ value ›
    const int valueWidth = renderer.getTextWidth(UI_12_FONT_ID, value.c_str());
    renderer.drawText(UI_12_FONT_ID, x + innerWidth - valueWidth, y + labelOffset, value.c_str(), !selected);
    y += calm::SHEET_ROW_HEIGHT;
  }
  renderer.displayBuffer();
}
