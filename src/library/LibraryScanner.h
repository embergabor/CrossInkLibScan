#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "LibraryIndex.h"

// Walks the SD card's book folder once and builds a LibraryIndex from each book's
// metadata. The heavy work (metadata extraction) reuses Epub::load(), so a scan
// also warms each book's on-SD cache: books scanned here open later without the
// reader's "Indexing..." pass.
//
// The scan is split so the UI stays responsive: begin() does the cheap recursive
// enumeration, then each step() parses exactly one book (~tens of ms) and appends
// a record. An activity calls step() once per loop tick and renders progress in
// between; a headless caller can loop step() to completion.
class LibraryScanner {
 public:
  // Scan from the SD-card root by default, the same place the File Browser reads
  // from, so books are found wherever the user keeps them (the device has no
  // enforced /books folder). Recursion skips hidden dirs like .crosspoint.
  static constexpr char DEFAULT_BOOKS_ROOT[] = "/";

  LibraryScanner() = default;

  // Enumerates books under booksRoot and opens the index for building.
  bool begin(const std::string& booksRoot = DEFAULT_BOOKS_ROOT);

  size_t total() const { return bookPaths.size(); }
  size_t processed() const { return nextIndex; }
  bool done() const { return nextIndex >= bookPaths.size(); }

  // Parses the next book and appends its record. outTitle receives the book's
  // title (or filename fallback) for progress display. Returns false when there
  // is nothing left to process.
  bool step(std::string& outTitle);

  // Finalises the index (offset table + footer, atomic replace). Call once
  // done() is true. Returns false on I/O failure.
  bool finish();

  // Aborts an in-progress scan, discarding the partial index.
  void abort();

 private:
  void enumerate(const std::string& root);
  // Removes per-book caches that the previous scan indexed but this scan no
  // longer finds (i.e. books deleted from /books since the last scan). Only
  // library-owned caches are considered, so caches for books opened from
  // elsewhere are never touched.
  void pruneDeletedCaches();

  std::vector<std::string> bookPaths;
  std::vector<std::string> previousCacheDirs;  // epub_* basenames from the prior index
  size_t nextIndex = 0;
  LibraryIndex index;
  bool building = false;
};
