#include "LibraryIndex.h"

#include <Logging.h>
#include <Serialization.h>

namespace {
constexpr char LIBRARY_DIR[] = "/.crosspoint/library";
constexpr char LIBRARY_FILE[] = "/.crosspoint/library/library.bin";
constexpr char LIBRARY_TMP[] = "/.crosspoint/library/library.tmp";
// offsetsStart(u32) + recordCount(u32) + version(u8) + magic(u32)
constexpr uint32_t FOOTER_SIZE = sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint32_t);
}  // namespace

const char* LibraryIndex::dirPath() { return LIBRARY_DIR; }
const char* LibraryIndex::filePath() { return LIBRARY_FILE; }

bool LibraryIndex::exists() { return Storage.exists(LIBRARY_FILE); }

bool LibraryIndex::remove() {
  if (!Storage.exists(LIBRARY_FILE)) return true;
  return Storage.remove(LIBRARY_FILE);
}

bool LibraryIndex::beginBuild() {
  if (building || readOpen) {
    LOG_ERR("LIB", "beginBuild called while index busy");
    return false;
  }
  if (!Storage.ensureDirectoryExists(LIBRARY_DIR)) {
    LOG_ERR("LIB", "Could not create library dir");
    return false;
  }
  if (!Storage.openFileForWrite("LIB", LIBRARY_TMP, file)) {
    LOG_ERR("LIB", "Could not open temp index for write");
    return false;
  }
  buildOffsets.clear();
  building = true;
  return true;
}

bool LibraryIndex::addRecord(const Record& record) {
  if (!building) return false;
  buildOffsets.push_back(static_cast<uint32_t>(file.position()));
  if (!serialization::tryWriteString(file, record.path) || !serialization::tryWriteString(file, record.title) ||
      !serialization::tryWriteString(file, record.author) || !serialization::tryWriteString(file, record.series) ||
      !serialization::tryWritePod(file, record.seriesIndex)) {
    LOG_ERR("LIB", "Failed writing record for %s", record.path.c_str());
    return false;
  }
  return true;
}

bool LibraryIndex::endBuild() {
  if (!building) return false;
  building = false;

  const uint32_t offStart = static_cast<uint32_t>(file.position());
  const uint32_t records = static_cast<uint32_t>(buildOffsets.size());
  bool ok = true;
  for (const uint32_t off : buildOffsets) {
    ok = ok && serialization::tryWritePod(file, off);
  }
  ok = ok && serialization::tryWritePod(file, offStart);
  ok = ok && serialization::tryWritePod(file, records);
  ok = ok && serialization::tryWritePod(file, VERSION);
  ok = ok && serialization::tryWritePod(file, MAGIC);
  file.close();
  buildOffsets.clear();

  if (!ok) {
    LOG_ERR("LIB", "Failed writing index footer");
    Storage.remove(LIBRARY_TMP);
    return false;
  }

  // Atomically replace the live index with the freshly built temp file.
  Storage.remove(LIBRARY_FILE);
  if (!Storage.rename(LIBRARY_TMP, LIBRARY_FILE)) {
    LOG_ERR("LIB", "Failed renaming temp index into place");
    Storage.remove(LIBRARY_TMP);
    return false;
  }
  LOG_INF("LIB", "Library index built: %u books", records);
  return true;
}

void LibraryIndex::abortBuild() {
  if (!building) return;
  building = false;
  file.close();
  buildOffsets.clear();
  Storage.remove(LIBRARY_TMP);
}

bool LibraryIndex::open() {
  if (readOpen) return true;
  if (!Storage.openFileForRead("LIB", LIBRARY_FILE, file)) {
    return false;
  }
  const size_t size = file.size();
  if (size < FOOTER_SIZE || !file.seek(size - FOOTER_SIZE)) {
    LOG_ERR("LIB", "Index too small or unseekable");
    file.close();
    return false;
  }
  uint8_t version = 0;
  uint32_t magic = 0;
  if (!serialization::tryReadPod(file, offsetsStart) || !serialization::tryReadPod(file, recordCount) ||
      !serialization::tryReadPod(file, version) || !serialization::tryReadPod(file, magic)) {
    LOG_ERR("LIB", "Index footer truncated");
    file.close();
    return false;
  }
  if (magic != MAGIC || version != VERSION) {
    LOG_DBG("LIB", "Index magic/version mismatch (got v%u); rescan needed", version);
    file.close();
    return false;
  }
  readOpen = true;
  return true;
}

void LibraryIndex::close() {
  if (building) {
    abortBuild();
    return;
  }
  if (readOpen) {
    file.close();
    readOpen = false;
  }
}

bool LibraryIndex::recordAt(uint32_t row, Record& out) {
  if (!readOpen || row >= recordCount) return false;
  if (!file.seek(offsetsStart + row * sizeof(uint32_t))) return false;
  uint32_t recordOffset = 0;
  if (!serialization::tryReadPod(file, recordOffset)) return false;
  if (!file.seek(recordOffset)) return false;
  return serialization::tryReadString(file, out.path) && serialization::tryReadString(file, out.title) &&
         serialization::tryReadString(file, out.author) && serialization::tryReadString(file, out.series) &&
         serialization::tryReadPod(file, out.seriesIndex);
}
