#pragma once

// Calm UI: a reduced, button-first interface for the original Xteink X4.
// Enabled only by the `calm` / `calm-simulator` PlatformIO environments, so
// every other build keeps upstream behavior unchanged.
#ifndef CROSSINK_CALM_UI
#define CROSSINK_CALM_UI 0
#endif

#if CROSSINK_CALM_UI != 0 && CROSSINK_CALM_UI != 1
#error "CROSSINK_CALM_UI must be 0 or 1"
#endif
