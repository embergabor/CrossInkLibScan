#include "LibraryScanner.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>

namespace {
constexpr char CACHE_DIR[] = "/.crosspoint";
constexpr size_t NAME_BUFFER_SIZE = 256;

// Basename of a book's cache directory, e.g. "epub_1234". Matches the directory
// Epub creates under /.crosspoint, so it identifies the book's on-SD cache.
std::string cacheDirName(const std::string& bookPath) {
  const std::string full = Epub::cachePathForFilePath(bookPath, CACHE_DIR);
  const auto slash = full.rfind('/');
  return slash == std::string::npos ? full : full.substr(slash + 1);
}

// Leaf filename (after the last '/') with any extension stripped, used as a
// title fallback when a book has no parseable metadata.
std::string filenameStem(const std::string& path) {
  const auto slash = path.rfind('/');
  std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
  const auto dot = name.rfind('.');
  if (dot != std::string::npos && dot != 0) {
    name.erase(dot);
  }
  return name;
}
}  // namespace

constexpr char LibraryScanner::DEFAULT_BOOKS_ROOT[];

void LibraryScanner::enumerate(const std::string& root) {
  // Iterative DFS over a directory worklist so only one directory handle is open
  // at a time (SdFat keeps handle pressure low on device).
  std::vector<std::string> dirs;
  dirs.push_back(root);

  char nameBuf[NAME_BUFFER_SIZE];
  while (!dirs.empty()) {
    const std::string dirPath = std::move(dirs.back());
    dirs.pop_back();

    auto dir = Storage.open(dirPath.c_str());
    if (!dir || !dir.isDirectory()) {
      if (dir) dir.close();
      continue;
    }
    dir.rewindDirectory();

    for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
      entry.getName(nameBuf, NAME_BUFFER_SIZE);
      const bool isDir = entry.isDirectory();
      entry.close();

      // Skip hidden entries (e.g. the .crosspoint cache dir).
      if (nameBuf[0] == '.') continue;

      std::string childPath = dirPath;
      if (childPath.empty() || childPath.back() != '/') childPath += '/';
      childPath += nameBuf;

      if (isDir) {
        dirs.push_back(childPath);
      } else if (FsHelpers::hasEpubExtension(std::string_view{nameBuf, std::strlen(nameBuf)})) {
        bookPaths.push_back(std::move(childPath));
      }
    }
    dir.close();
  }
}

bool LibraryScanner::begin(const std::string& booksRoot) {
  bookPaths.clear();
  previousCacheDirs.clear();
  nextIndex = 0;

  // Snapshot the caches the previous scan owned, so finish() can prune the ones
  // this scan no longer sees. Read before the index is rebuilt.
  {
    LibraryIndex previous;
    if (previous.open()) {
      LibraryIndex::Record r;
      previousCacheDirs.reserve(previous.count());
      for (uint32_t i = 0; i < previous.count(); ++i) {
        if (previous.recordAt(i, r)) previousCacheDirs.push_back(cacheDirName(r.path));
      }
      previous.close();
    }
  }

  enumerate(booksRoot);
  FsHelpers::sortFileList(bookPaths);  // stable, natural order for deterministic scans

  if (!index.beginBuild()) {
    LOG_ERR("LSCAN", "Failed to open library index for build");
    return false;
  }
  building = true;
  LOG_INF("LSCAN", "Scanning %zu books under %s", bookPaths.size(), booksRoot.c_str());
  return true;
}

void LibraryScanner::pruneDeletedCaches() {
  if (previousCacheDirs.empty()) return;
  // Guard against wiping caches if enumeration found nothing (e.g. a transient
  // read failure) rather than a genuine mass deletion.
  if (bookPaths.empty()) {
    LOG_INF("LSCAN", "Skipping cache prune: no books found this scan");
    return;
  }

  std::vector<std::string> current;
  current.reserve(bookPaths.size());
  for (const auto& p : bookPaths) current.push_back(cacheDirName(p));

  size_t pruned = 0;
  for (const auto& old : previousCacheDirs) {
    if (std::find(current.begin(), current.end(), old) != current.end()) continue;
    const std::string dir = std::string(CACHE_DIR) + "/" + old;
    if (Storage.exists(dir.c_str()) && Storage.removeDir(dir.c_str())) {
      pruned++;
    }
  }
  if (pruned > 0) LOG_INF("LSCAN", "Pruned %zu deleted-book cache(s)", pruned);
}

bool LibraryScanner::step(std::string& outTitle) {
  if (!building || nextIndex >= bookPaths.size()) return false;

  LibraryIndex::Record rec;
  rec.path = bookPaths[nextIndex++];

  // buildIfMissing=true warms the on-SD cache; skipLoadingCss=true skips the
  // render-only CSS pass we don't need for metadata.
  Epub epub(rec.path, CACHE_DIR);
  epub.load(/*buildIfMissing=*/true, /*skipLoadingCss=*/true);
  rec.title = epub.getTitle();
  rec.author = epub.getAuthor();
  rec.series = epub.getSeries();
  rec.seriesIndex = epub.getSeriesIndex();
  if (rec.title.empty()) {
    rec.title = filenameStem(rec.path);
  }

  index.addRecord(rec);
  outTitle = rec.title;
  return true;
}

bool LibraryScanner::finish() {
  if (!building) return false;
  building = false;
  if (!index.endBuild()) return false;
  pruneDeletedCaches();
  return true;
}

void LibraryScanner::abort() {
  if (!building) return;
  building = false;
  index.abortBuild();
}
