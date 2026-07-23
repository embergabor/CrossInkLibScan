#pragma once

#include <HalStorage.h>

#include <cstdint>
#include <string>
#include <vector>

// On-SD index of every book found during a library scan. The index lives on the
// SD card (not in RAM) so it scales to large libraries on the ESP32-C3's ~380 KB
// of RAM: only the file header and a single decoded record are ever resident.
//
// File layout (little-endian, written via lib/Serialization):
//   [Blob]    recordCount records, each: path,title,author,series (length-prefixed
//             strings) then seriesIndex (float)
//   [Offsets] recordCount u32 byte-offsets, one per record, at offsetsStart
//   [Footer]  offsetsStart(u32) recordCount(u32) version(u8) magic(u32)
//
// The footer (not a header) holds the counts so the whole file is written in one
// forward-only append pass — no seek-back to patch a header, which keeps the
// build robust on SdFat write handles. Records are streamed to disk while their
// start offsets are collected in a small RAM vector (4 bytes/book), flushed as
// the offset table before the footer. Reads decode the footer, then seek via the
// offset table for O(1) random access by row.
class LibraryIndex {
 public:
  struct Record {
    std::string path;
    std::string title;
    std::string author;
    std::string series;
    float seriesIndex = 0.0f;
  };

  LibraryIndex() = default;
  ~LibraryIndex() { close(); }
  LibraryIndex(const LibraryIndex&) = delete;
  LibraryIndex& operator=(const LibraryIndex&) = delete;

  // Directory + file paths on the SD card.
  static const char* dirPath();
  static const char* filePath();
  static bool exists();
  static bool remove();

  // ---- Build API (writes a fresh index) ----
  // Opens a temp file for writing and reserves the header. Returns false on I/O
  // or allocation failure.
  bool beginBuild();
  // Appends one record. Returns false if a write fails.
  bool addRecord(const Record& record);
  // Finalises: writes the offset table + header, then atomically replaces the
  // live index file. Returns false on failure (temp file is cleaned up).
  bool endBuild();
  // Aborts an in-progress build, discarding the temp file.
  void abortBuild();

  // ---- Read API ----
  bool open();
  void close();
  bool isOpen() const { return readOpen; }
  uint32_t count() const { return recordCount; }
  // Decodes the record at `row` (0-based). Returns false on out-of-range or I/O.
  bool recordAt(uint32_t row, Record& out);

 private:
  static constexpr uint32_t MAGIC = 0x494C5843;  // 'CXLI'
  static constexpr uint8_t VERSION = 1;

  HalFile file;
  bool readOpen = false;
  bool building = false;
  uint32_t recordCount = 0;
  uint32_t offsetsStart = 0;
  std::vector<uint32_t> buildOffsets;  // record start offsets, collected during build
};
