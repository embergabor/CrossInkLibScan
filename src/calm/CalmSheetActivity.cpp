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

// Narrow / Normal / Wide reading margins, in pixels.
constexpr uint8_t SIDE_MARGINS[] = {10, 20, 32};
constexpr uint8_t TOP_BOTTOM_MARGINS[] = {8, 12, 20};
constexpr StrId MARGIN_NAMES[] = {StrId::STR_CALM_NARROW, StrId::STR_CALM_NORMAL, StrId::STR_WIDE};

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
    : Activity("CalmSheet", renderer, mappedInput), ctx(std::move(context)) {
  if (ctx.initialRow > 0 && ctx.initialRow < static_cast<int>(Row::Count)) row = ctx.initialRow;
}

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

int CalmSheetActivity::sideMarginPreset() const {
  return nearestIndex(SIDE_MARGINS, SETTINGS.screenMarginHorizontal);
}

int CalmSheetActivity::topBottomMarginPreset() const {
  return nearestIndex(TOP_BOTTOM_MARGINS, SETTINGS.screenMarginVertical);
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
    case Row::SideMargins:
      return tr(STR_CALM_SIDE_MARGINS);
    case Row::TopBottomMargins:
      return tr(STR_CALM_TOP_BOTTOM_MARGINS);
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
    case Row::SideMargins:
      return I18N.get(MARGIN_NAMES[sideMarginPreset()]);
    case Row::TopBottomMargins:
      return I18N.get(MARGIN_NAMES[topBottomMarginPreset()]);
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
    case Row::SideMargins: {
      const int index = sideMarginPreset();
      const int next = std::clamp(index + delta, 0, 2);
      if (next == index && SIDE_MARGINS[index] == SETTINGS.screenMarginHorizontal) return;
      SETTINGS.screenMarginHorizontal = SIDE_MARGINS[next];
      break;
    }
    case Row::TopBottomMargins: {
      const int index = topBottomMarginPreset();
      const int next = std::clamp(index + delta, 0, 2);
      if (next == index && TOP_BOTTOM_MARGINS[index] == SETTINGS.screenMarginVertical) return;
      SETTINGS.screenMarginVertical = TOP_BOTTOM_MARGINS[next];
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

void CalmSheetActivity::closeWithAction(const int action, const bool reopen) {
  if (changed && ctx.saveGlobal) ctx.saveGlobal(ctx.readerContext);
  if (ctx.endGlobalEdit) ctx.endGlobalEdit(ctx.readerContext);
  MenuResult menu;
  menu.action = action;
  menu.orientation = SETTINGS.orientation;
  menu.settingsChanged = changed;
  menu.changeMask = changed ? ReaderSettingsChangeMask::Relayout : ReaderSettingsChangeMask::None;
  // The reader reopens the sheet on this row once the new layout is on screen.
  menu.reopenDrawer = reopen;
  // drawerValue doubles as a percent target for GO_TO_PERCENT, so it carries
  // the row only when reopening.
  menu.drawerValue = reopen ? static_cast<int16_t>(row) : -1;
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
      // Apply now and come back, so the page shows the new setting.
      if (changed) closeWithAction(-1, true);
      return;
    }
    switch (static_cast<ActionChip>(chip)) {
      case ActionChip::Contents:
        closeWithAction(static_cast<int>(EpubReaderMenuAction::SELECT_CHAPTER));
        return;
      case ActionChip::Sync:
        closeWithAction(static_cast<int>(EpubReaderMenuAction::SYNC));
        return;
      default:
        closeWithAction(static_cast<int>(ctx.stablePageCount > 0 ? EpubReaderMenuAction::GO_TO_STABLE_PAGE
                                                                  : EpubReaderMenuAction::GO_TO_PERCENT));
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
      chip = wrapIndex(chip, delta, chipCount());
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

  // Header: the book's title, wrapped to at most three lines, with the
  // battery level on the right.
  char battery[8];
  snprintf(battery, sizeof(battery), "%u%%", static_cast<unsigned>(powerManager.getBatteryPercentage()));
  const int batteryTextWidth = renderer.getTextWidth(UI_10_FONT_ID, battery);
  const int infoWidth = batteryTextWidth + 6 + calm::BATTERY_WIDTH;
  const int titleLine = renderer.getLineHeight(UI_12_FONT_ID);
  const auto titleLines =
      calm::wrapText(renderer, UI_12_FONT_ID, ctx.title, innerWidth - infoWidth - 16, 3, EpdFontFamily::BOLD);
  constexpr int headerPadTop = 10;
  constexpr int headerPadBottom = 8;
  const int headerHeight = headerPadTop + std::max<int>(1, titleLines.size()) * titleLine + headerPadBottom;
  constexpr int actionHeight = 52;
  const int settingRows = static_cast<int>(Row::Count) - 1;
  const int sheetHeight = headerHeight + actionHeight + settingRows * calm::SHEET_ROW_HEIGHT + 12;
  const int sheetY = bottom - sheetHeight;
  // Rounded top corners; the top of the page stays visible above.
  calm::drawSheet(renderer, l, sheetY, width, sheetHeight);

  int lineY = sheetY + headerPadTop;
  for (const auto& line : titleLines) {
    renderer.drawText(UI_12_FONT_ID, x, lineY, line.c_str(), true, EpdFontFamily::BOLD);
    lineY += titleLine;
  }
  const int infoX = x + innerWidth - infoWidth;
  const int smallLine = renderer.getLineHeight(UI_10_FONT_ID);
  const int infoY = sheetY + headerPadTop + (titleLine - smallLine) / 2;
  renderer.drawText(UI_10_FONT_ID, infoX, infoY, battery);
  calm::drawBattery(renderer, infoX + batteryTextWidth + 6, infoY + (smallLine - calm::BATTERY_HEIGHT) / 2,
                    powerManager.getBatteryPercentage());
  renderer.fillRect(x, sheetY + headerHeight, innerWidth, 1);

  // Action row: Contents, the position (which opens Go to), and Sync when
  // logged in. Books with page numbers show the book's page, others the
  // chapter's page and the book's percent.
  const bool actionsFocused = row == static_cast<int>(Row::Actions);
  char pageLabel[32];
  if (ctx.stablePageCount > 0) {
    snprintf(pageLabel, sizeof(pageLabel), tr(STR_CALM_PAGE_SHORT), static_cast<unsigned>(ctx.stablePage),
             static_cast<unsigned>(ctx.stablePageCount));
  } else {
    snprintf(pageLabel, sizeof(pageLabel), "%d / %d \xC2\xB7 %d%%", ctx.chapterPage, ctx.chapterPageCount,
             static_cast<int>(ctx.bookProgress + 0.5f));
  }
  const char* buttonText[] = {tr(STR_CALM_CONTENTS), pageLabel, tr(STR_CALM_SYNC)};
  constexpr int buttonGap = 10;
  const int count = chipCount();
  // The position gets the most room; Contents and Sync share the rest.
  const int positionWidth = count == 3 ? (innerWidth - 2 * buttonGap) * 2 / 5 : (innerWidth - buttonGap) / 2;
  const int sideWidth = count == 3 ? (innerWidth - 2 * buttonGap - positionWidth) / 2 : positionWidth;
  const int buttonY = sheetY + headerHeight + (actionHeight - calm::CONTROL_HEIGHT - 4) / 2 + 2;
  int bx = x;
  for (int i = 0; i < count; i++) {
    const bool focused = actionsFocused && chip == i;
    const int buttonWidth = i == static_cast<int>(ActionChip::Position) ? positionWidth : sideWidth;
    calm::drawButton(renderer, bx, buttonY, buttonWidth, calm::CONTROL_HEIGHT + 4, focused);
    calm::drawCenteredLabel(renderer, UI_10_FONT_ID, bx, buttonY, buttonWidth, calm::CONTROL_HEIGHT + 4,
                            buttonText[i], focused, EpdFontFamily::BOLD);
    bx += buttonWidth + buttonGap;
  }

  // Setting rows: label on the left, a rounded ‹ value › stepper on the right.
  constexpr int stepperWidth = 214;
  constexpr int stepperHeight = 36;
  const int uiLine = renderer.getLineHeight(UI_12_FONT_ID);
  int y = sheetY + headerHeight + actionHeight;
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
