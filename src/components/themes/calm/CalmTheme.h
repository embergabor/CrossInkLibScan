#pragma once

#include "components/themes/roundedraff/RoundedRaffTheme.h"

// Calm UI's look for the upstream screens a calm build still reaches (More
// settings, File transfer, Calibre, sync login, the SD-card updater, the
// keyboard and every popup). It is RoundedRaff's structure with the calm
// screens' proportions: 12 px margins, 8 px radii, a 1 px header rule, and
// popups as white cards with a black outline instead of black slabs.
// Selected automatically in CROSSINK_CALM_UI builds.
namespace CalmThemeMetrics {
// RoundedRaff's metrics, with the calm values noted in CalmLayout.h / CalmDraw.h.
constexpr ThemeMetrics values = {.batteryWidth = 15,
                                 .batteryHeight = 12,
                                 .topPadding = 0,
                                 .batteryBarHeight = 45,
                                 .headerHeight = 70,
                                 .verticalSpacing = 10,
                                 .previewPadding = 12,
                                 .previewHeightPercent = 30,
                                 .contentSidePadding = 12,
                                 .listRowHeight = 44,
                                 .listWithSubtitleRowHeight = 69,
                                 .listRowGap = 6,
                                 .listRowRadius = 8,
                                 .listInset = 6,
                                 .listSidePadding = 12,
                                 .listSelectionStyle = 0,
                                 .listScrollWidth = 4,
                                 .listScrollSide = 0,
                                 .listTitleBold = true,
                                 .headerSidePadding = 12,
                                 .headerUnderlineSize = 1,
                                 .headerTitleAlign = 0,
                                 .headerBatterySide = 0,
                                 .headerBatteryDetached = true,
                                 .menuRowHeight = 42,
                                 .menuSpacing = 6,
                                 .tabSpacing = 10,
                                 .tabBarHeight = 50,
                                 .tabBarAppearance = ThemeTabBarAppearance::Pill,
                                 .scrollBarWidth = 4,
                                 .scrollBarRightOffset = 5,
                                 .homeTopPadding = 75,
                                 // Compact cover tile so the full roundedraff home menu fits above button hints.
                                 .homeCoverHeight = 260,
                                 .homeCoverTileHeight = 300,
                                 .homeRecentBooksCount = 1,
                                 .homeContinueReadingInMenu = true,
                                 .homeMenuTopOffset = 20,
                                 .buttonHintsHeight = 40,
                                 .sideButtonHintsWidth = 30,
                                 .progressBarHeight = 16,
                                 .progressBarMarginTop = 1,
                                 .statusBarHorizontalMargin = 5,
                                 .statusBarVerticalMargin = 19,
                                 .keyboardKeyHeight = 56,
                                 .keyboardKeySpacing = 10,
                                 .keyboardCenteredText = false,
                                 .keyboardVerticalOffset = 0,
                                 .keyboardTextFieldWidthPercent = 85,
                                 .keyboardWidthPercent = 94,
                                 .popupTopOffsetRatio = 0.12f,
                                 .popupMarginX = 20,
                                 .popupMarginY = 14,
                                 .popupFrameThickness = 2,
                                 .popupCornerRadius = 14,
                                 .popupTextBold = true,
                                 .popupTextInverted = true,
                                 .popupTextBaselineOffsetY = -2,
                                 .popupProgressBarHeight = 4,
                                 .popupProgressDrawOutline = true,
                                 .popupProgressClampPercent = true,
                                 .popupProgressFillInverted = true,
                                 .popupProgressOutlineInverted = true,
                                 .optionPopupItemSpacing = 6,
                                 .optionPopupInnerPadding = 24,
                                 .optionPopupSelectionHPadding = 20,
                                 .optionPopupSelectionVPadding = 10,
                                 .optionPopupTitleGap = 16,
                                 .optionPopupUseSmallFont = false,
                                 .optionPopupOptionFontBold = true,
                                 .optionPopupSelectionRadius = 8,
                                 .optionPopupSelectionLight = false,
                                 .optionPopupDrawAllRows = true,
                                 .optionPopupDialogSideMargin = 20,
                                 .optionPopupTitleSeparator = true,
                                 .textFieldHorizontalPadding = 8,
                                 .textFieldNormalThickness = 2,
                                 .textFieldCursorThickness = 3,
                                 .textFieldLineEndOffset = -1};
}  // namespace CalmThemeMetrics

class CalmTheme : public RoundedRaffTheme {
 public:
  Rect drawPopup(const GfxRenderer& renderer, const char* message, bool preserveBackdrop = false) const override;
};
