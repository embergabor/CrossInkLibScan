#include "SimulatorIngestTest.h"

#ifdef SIMULATOR

#include <Logging.h>

#include <cstdlib>
#include <string>

#include "library/LibraryIndex.h"
#include "library/LibraryScanner.h"

void runSimulatorIngestTestTick() {
  static bool done = false;
  if (done) return;
  if (std::getenv("CROSSINK_SIMULATOR_INGEST_TEST") == nullptr) return;
  done = true;

  LOG_INF("INGEST", "=== Library ingest self-test start ===");
  // Note: no explicit index wipe — the scanner rebuilds the index each run and
  // prunes caches for books deleted since the previous scan, so running this
  // test twice (deleting a book between) exercises the prune path.

  LibraryScanner scanner;
  if (!scanner.begin("/books")) {
    LOG_ERR("INGEST", "scan begin failed");
    return;
  }
  std::string title;
  while (scanner.step(title)) {
    LOG_INF("INGEST", "  scanned [%zu/%zu] %s", scanner.processed(), scanner.total(), title.c_str());
  }
  if (!scanner.finish()) {
    LOG_ERR("INGEST", "scan finish failed");
    return;
  }

  // Read the index back and dump every record to prove the round-trip.
  LibraryIndex idx;
  if (!idx.open()) {
    LOG_ERR("INGEST", "index reopen failed");
    return;
  }
  LOG_INF("INGEST", "Index has %u records:", idx.count());
  LibraryIndex::Record r;
  for (uint32_t i = 0; i < idx.count(); ++i) {
    if (idx.recordAt(i, r)) {
      LOG_INF("INGEST", "  #%u title='%s' author='%s' series='%s' #%.2f path=%s", i, r.title.c_str(), r.author.c_str(),
              r.series.c_str(), r.seriesIndex, r.path.c_str());
    } else {
      LOG_ERR("INGEST", "  #%u recordAt failed", i);
    }
  }
  idx.close();
  LOG_INF("INGEST", "=== Library ingest self-test passed ===");
}

#else

void runSimulatorIngestTestTick() {}

#endif
