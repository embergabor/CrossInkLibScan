#include "CalmSettingsActivity.h"

#include <AppVersion.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <iterator>

#include "CalmDraw.h"
#include "CalmLayout.h"
#include "activities/settings/KOReaderSettingsActivity.h"
#include "activities/settings/SdFirmwareUpdateActivity.h"
#include "activities/settings/SettingsActivity.h"
#include "fontIds.h"

namespace {

// A short, calm subset of upstream's sleep screens.
struct SleepChoice {
  uint8_t mode;
  StrId label;
};
constexpr SleepChoice SLEEP_SCREENS[] = {{CrossPointSettings::COVER, StrId::STR_COVER},
                                         {CrossPointSettings::LIGHT, StrId::STR_LIGHT},
                                         {CrossPointSettings::DARK, StrId::STR_DARK},
                                         {CrossPointSettings::BLANK, StrId::STR_NONE_OPT}};

constexpr uint8_t SLEEP_MINUTES[] = {2, 5, 10, 15, 30, CrossPointSettings::SLEEP_TIMEOUT_NEVER_MINUTES};

int sleepScreenIndex() {
  for (size_t i = 0; i < std::size(SLEEP_SCREENS); i++) {
    if (SLEEP_SCREENS[i].mode == SETTINGS.sleepScreen) return static_cast<int>(i);
  }
  return -1;  // a mode only upstream's full Settings offers
}

int sleepMinutesIndex() {
  int best = 0;
  for (size_t i = 1; i < std::size(SLEEP_MINUTES); i++) {
    if (std::abs(SLEEP_MINUTES[i] - SETTINGS.sleepTimeoutMinutes) <
        std::abs(SLEEP_MINUTES[best] - SETTINGS.sleepTimeoutMinutes))
      best = static_cast<int>(i);
  }
  return best;
}

}  // namespace

CalmSettingsActivity::CalmSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("CalmSettings", renderer, mappedInput) {}

void CalmSettingsActivity::onEnter() {
  Activity::onEnter();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  selected = 0;
  changed = false;
  ignoreConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  requestUpdate();
}

void CalmSettingsActivity::onExit() {
  saveIfChanged();
  Activity::onExit();
}

void CalmSettingsActivity::saveIfChanged() {
  if (!changed) return;
  if (!SETTINGS.saveToFile()) LOG_ERR("CALM", "Cannot save settings");
  changed = false;
}

const char* CalmSettingsActivity::rowLabel(const Row row) const {
  switch (row) {
    case Row::FileTransfer:
      return tr(STR_CALM_FILE_TRANSFER);
    case Row::Calibre:
      return tr(STR_CALM_CALIBRE);
    case Row::ProgressSync:
      return tr(STR_CALM_PROGRESS_SYNC);
    case Row::RefreshLibrary:
      return tr(STR_CALM_REFRESH_LIBRARY);
    case Row::SdUpdate:
      return tr(STR_CALM_SD_UPDATE);
    case Row::SleepScreen:
      return tr(STR_CALM_SLEEP_SCREEN);
    case Row::SleepTimeout:
      return tr(STR_CALM_TIME_TO_SLEEP);

    case Row::More:
      return tr(STR_CALM_MORE_SETTINGS);
    default:
      return "";
  }
}

std::string CalmSettingsActivity::valueLabel(const Row row) const {
  switch (row) {
    case Row::SleepScreen: {
      const int index = sleepScreenIndex();
      return index < 0 ? std::string(tr(STR_MORE)) : std::string(I18N.get(SLEEP_SCREENS[index].label));
    }
    case Row::SleepTimeout: {
      if (SETTINGS.sleepTimeoutMinutes >= CrossPointSettings::SLEEP_TIMEOUT_NEVER_MINUTES) return tr(STR_NEVER);
      char buffer[24];
      snprintf(buffer, sizeof(buffer), tr(STR_CALM_MINUTES), static_cast<unsigned>(SETTINGS.sleepTimeoutMinutes));
      return buffer;
    }

    default:
      return {};
  }
}

void CalmSettingsActivity::step(const Row row, const int delta) {
  if (row == Row::SleepScreen) {
    const int count = static_cast<int>(std::size(SLEEP_SCREENS));
    const int index = sleepScreenIndex();
    const int next = index < 0 ? 0 : ((index + delta) % count + count) % count;
    SETTINGS.sleepScreen = SLEEP_SCREENS[next].mode;
  } else if (row == Row::SleepTimeout) {
    const int next =
        std::clamp(sleepMinutesIndex() + delta, 0, static_cast<int>(std::size(SLEEP_MINUTES)) - 1);
    if (SLEEP_MINUTES[next] == SETTINGS.sleepTimeoutMinutes) return;
    SETTINGS.sleepTimeoutMinutes = SLEEP_MINUTES[next];
  } else {
    return;
  }
  changed = true;
  requestUpdate();
}

void CalmSettingsActivity::activate(const Row row) {
  switch (row) {
    case Row::FileTransfer:
      saveIfChanged();
      activityManager.goToFileTransfer();
      return;
    case Row::Calibre:
      saveIfChanged();
      activityManager.goToCalibreWireless();
      return;
    case Row::ProgressSync:
    case Row::SdUpdate: {
      saveIfChanged();
      // Upstream's screens: KOReader sync login, and the SD-card firmware picker.
      std::unique_ptr<Activity> screen;
      if (row == Row::ProgressSync)
        screen = makeUniqueNoThrow<KOReaderSettingsActivity>(renderer, mappedInput);
      else
        screen = makeUniqueNoThrow<SdFirmwareUpdateActivity>(renderer, mappedInput);
      if (!screen) {
        LOG_ERR("CALM", "Cannot allocate settings screen %d", static_cast<int>(row));
        return;
      }
      startActivityForResult(std::move(screen), [this](const ActivityResult&) {
        SETTINGS.saveToFile();
        requestUpdate();
      });
      return;
    }
    case Row::RefreshLibrary:
      saveIfChanged();
      // The Library rescans on entry once the index is marked stale.
      library::invalidateLibraryIndex();
      activityManager.goToLibrary();
      return;
    case Row::More: {
      saveIfChanged();
      // Upstream's full Settings: it returns Home (the Library) when closed.
      auto full = makeUniqueNoThrow<SettingsActivity>(renderer, mappedInput);
      if (!full) {
        LOG_ERR("CALM", "Cannot allocate full Settings");
        return;
      }
      startActivityForResult(std::move(full), [this](const ActivityResult&) { requestUpdate(); });
      return;
    }
    default:
      // Value rows: Confirm steps forward, like Right.
      step(row, 1);
      return;
  }
}

void CalmSettingsActivity::loop() {
  using Button = MappedInputManager::Button;
  if (ignoreConfirmRelease) {
    if (!mappedInput.isPressed(Button::Confirm)) ignoreConfirmRelease = false;
    return;
  }
  if (mappedInput.wasReleased(Button::Back)) {
    saveIfChanged();
    activityManager.goToLibrary();
    return;
  }
  const auto row = static_cast<Row>(selected);
  if (mappedInput.wasReleased(Button::Confirm)) {
    activate(row);
    return;
  }
  const int count = static_cast<int>(Row::Count);
  if (mappedInput.wasReleased(Button::Down)) {
    selected = (selected + 1) % count;
    requestUpdate();
  } else if (mappedInput.wasReleased(Button::Up)) {
    selected = (selected + count - 1) % count;
    requestUpdate();
  } else if (mappedInput.wasReleased(Button::Right)) {
    step(row, 1);
  } else if (mappedInput.wasReleased(Button::Left)) {
    step(row, -1);
  }
}

void CalmSettingsActivity::render(RenderLock&&) {
  int t = 0, r = 0, b = 0, l = 0;
  renderer.getOrientedViewableTRBL(&t, &r, &b, &l);
  const int width = renderer.getScreenWidth() - l - r;
  const int height = renderer.getScreenHeight() - t - b;
  const int x = l + calm::SIDE_MARGIN;
  const int innerWidth = width - 2 * calm::SIDE_MARGIN;

  renderer.clearScreen();
  renderer.drawText(BITTER_16_FONT_ID, x, t + (calm::HEADER_HEIGHT - renderer.getLineHeight(BITTER_16_FONT_ID)) / 2,
                    tr(STR_SETTINGS_TITLE), true, EpdFontFamily::BOLD);
  renderer.fillRect(l, t + calm::HEADER_HEIGHT - 1, width, 1);

  constexpr int stepperWidth = 170;
  constexpr int stepperHeight = 36;
  const int uiLine = renderer.getLineHeight(UI_12_FONT_ID);
  const int sectionLine = renderer.getLineHeight(SMALL_FONT_ID);
  int y = t + calm::HEADER_HEIGHT + 10;
  for (int i = 0; i < static_cast<int>(Row::Count); i++) {
    const auto row = static_cast<Row>(i);
    const bool isSelected = selected == i;
    if (row == Row::FileTransfer || row == Row::SleepScreen) {
      calm::drawSectionLabel(renderer, x, y + 4,
                             row == Row::FileTransfer ? tr(STR_CALM_SECTION_BOOKS) : tr(STR_CALM_SECTION_SLEEP));
      y += sectionLine + 8;
    } else if (row == Row::SdUpdate) {
      y += 14;
    }
    const std::string value = valueLabel(row);
    const int labelY = y + (calm::SHEET_ROW_HEIGHT - uiLine) / 2;
    if (value.empty()) {
      // Action rows: a rounded pill marks the cursor.
      if (isSelected)
        calm::fillPill(renderer, l + calm::PILL_INSET, y + 2, width - 2 * calm::PILL_INSET,
                       calm::SHEET_ROW_HEIGHT - 4);
      renderer.drawText(UI_12_FONT_ID, x, labelY, rowLabel(row), !isSelected);
      const char* chevron = "\xE2\x80\xBA";  // ›
      const int chevronWidth = renderer.getTextWidth(UI_12_FONT_ID, chevron);
      renderer.drawText(UI_12_FONT_ID, x + innerWidth - chevronWidth, labelY, chevron, !isSelected);
    } else {
      // Value rows: label plus a stepper that fills when focused.
      renderer.drawText(UI_12_FONT_ID, x, labelY, rowLabel(row), true,
                        isSelected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
      calm::drawStepper(renderer, x + innerWidth - stepperWidth, y + (calm::SHEET_ROW_HEIGHT - stepperHeight) / 2,
                        stepperWidth, stepperHeight, value.c_str(), isSelected);
    }
    y += calm::SHEET_ROW_HEIGHT;
    if (row == Row::RefreshLibrary || row == Row::SleepTimeout) {
      renderer.fillRect(x, y + 6, innerWidth, 1);
      y += 12;
    }
  }

  const int footerLine = renderer.getLineHeight(SMALL_FONT_ID);
  renderer.drawCenteredText(SMALL_FONT_ID, t + height - footerLine - 12, AppVersion::versionLabel());
  renderer.displayBuffer();
}
