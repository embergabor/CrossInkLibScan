#pragma once

// SIMULATOR-only. When CROSSINK_SIMULATOR_LIBRARY_SHOTS is set, builds the
// library index, then jumps into the Library UI and drills Picker -> Series ->
// a series' books, saving a framebuffer BMP of each screen under
// ./fs_/screenshots/. Lets the new UI be captured headlessly. No-op otherwise.
void runSimulatorLibraryShotsTick();
