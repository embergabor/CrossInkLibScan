#pragma once

#include <CalmUi.h>

class CrossPointSettings;

namespace calm {

// Pins the settings that the calm UI does not expose, so a settings file
// written by stock CrossInk cannot bring back chrome the calm screens never
// offer a way to remove. Call once after SETTINGS has been loaded.
// No-op unless CROSSINK_CALM_UI is enabled.
void applySettingsPolicy(CrossPointSettings& settings);

}  // namespace calm
