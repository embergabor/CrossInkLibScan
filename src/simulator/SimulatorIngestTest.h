#pragma once

// SIMULATOR-only. When the CROSSINK_SIMULATOR_INGEST_TEST environment variable is
// set, runs a one-shot library scan over /books, rebuilds the on-SD LibraryIndex,
// reads it back, and logs every record. Lets the batch-ingest path be validated
// headlessly (no SDL interaction) the way runSimulatorSmokeTestTick validates UI.
// No-op on non-simulator builds and when the env var is unset.
void runSimulatorIngestTestTick();
