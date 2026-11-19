#include "CalmLibraryActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <I18n.h>
#include <LibraryFormat.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "CalmBookCardActivity.h"
#include "CalmDraw.h"
#include "CalmOpening.h"
#include "CalmLayout.h"
#include "components/icons/listIcons.h"
#include "activities/home/BookActions.h"
#include "activities/home/RecentBookProgress.h"
#include "fontIds.h"

namespace {

constexpr uint16_t NO_ROW = 0xFFFF;

// Settings persist the sort as upstream LibraryActivity::Sort values so the
// stock Library and the calm one agree on what the card was last showing.
constexpr uint8_t UPSTREAM_SORT_TITLE = 1;
constexpr uint8_t UPSTREAM_SORT_AUTHOR_LAST = 2;
constexpr uint8_t UPSTREAM_SORT_RECENTLY_READ = 4;
constexpr uint8_t UPSTREAM_SORT_SERIES = 5;

CalmLibraryActivity::Sort sortFromSettings(const uint8_t value) {
  switch (value) {
    case UPSTREAM_SORT_TITLE:
      return CalmLibraryActivity::Sort::Title;
    case UPSTREAM_SORT_AUTHOR_LAST:
      return CalmLibraryActivity::Sort::Author;
    case UPSTREAM_SORT_SERIES:
      return CalmLibraryActivity::Sort::Series;
    default:
      return CalmLibraryActivity::Sort::Recent;
  }
}

uint8_t sortToSettings(const CalmLibraryActivity::Sort sort) {
  switch (sort) {
    case CalmLibraryActivity::Sort::Title:
      return UPSTREAM_SORT_TITLE;
    case CalmLibraryActivity::Sort::Author:
      return UPSTREAM_SORT_AUTHOR_LAST;
    case CalmLibraryActivity::Sort::Series:
      return UPSTREAM_SORT_SERIES;
    default:
      return UPSTREAM_SORT_RECENTLY_READ;
  }
}

// Inverse of LibraryBuilder's parseSeriesPosition(): the stored u32 flips the
// IEEE-754 sign half so unsigned order matches numeric order.
bool decodeSeriesPosition(const uint32_t stored, float& out) {
  if (stored == library::CLIX_UNKNOWN_SERIES_POSITION) return false;
  const uint32_t bits = (stored & 0x80000000u) ? (stored ^ 0x80000000u) : ~stored;
  memcpy(&out, &bits, sizeof(out));
  return std::isfinite(out);
}

// Written next to a book's cache when it has no usable cover, so the Library
// does not reopen the EPUB on every visit.
std::string noCoverMarker(const Epub& epub) { return epub.getCachePath() + "/calm_nocover"; }

// Present once every book in the current index has a cover of that size (or
// is known to have none). A rescan removes them, so new books get covers on
// the next visit. The list and the grid each have their own size.
constexpr const char* COVERS_DONE_LIST = "/.crosspoint/calm_covers_done";
constexpr const char* COVERS_DONE_GRID = "/.crosspoint/calm_covers_done_grid";

enum class CoverNeed : uint8_t { NotEpub, Have, Missing };

CoverNeed coverNeed(const std::string& path, const int w, const int h) {
  if (!FsHelpers::hasEpubExtension(path)) return CoverNeed::NotEpub;
  const Epub epub(path, "/.crosspoint");
  if (Storage.exists(epub.getThumbBmpPath(w, h).c_str()) || Storage.exists(noCoverMarker(epub).c_str()))
    return CoverNeed::Have;
  return CoverNeed::Missing;
}

std::string fileStem(const std::string& path) {
  const auto slash = path.find_last_of('/');
  std::string name = path.substr(slash == std::string::npos ? 0 : slash + 1);
  const auto dot = name.find_last_of('.');
  if (dot != std::string::npos && dot != 0) name.resize(dot);
  return name;
}

}  // namespace

CalmLibraryActivity::CalmLibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Library", renderer, mappedInput) {}

void CalmLibraryActivity::onEnter() {
  {
    RenderLock lock;
    Activity::onEnter();
    renderer.setOrientation(GfxRenderer::Orientation::Portrait);
    computeLayout();
    sort = sortFromSettings(SETTINGS.librarySortMethod);
    grid = SETTINGS.calmLibraryGrid != 0;
    computeLayout();
    selection = 0;
    loadedPage = -1;
    ignoreConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
    confirmHeld = false;
  }
  if (library::libraryIndexNeedsRefresh() || !Storage.exists(library::libraryIndexPath())) {
    RenderLock lock;
    drawScanScreen(tr(STR_LIBRARY_SCANNING));
  }
  {
    RenderLock lock;
    ensureIndex();
    prepareCovers();
    reload();
  }
  requestUpdate();
}

void CalmLibraryActivity::drawCoverProgress(const int done, const int total) const {
  renderer.clearScreen();
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int centerY = top + height / 2;
  renderer.drawCenteredText(UI_12_FONT_ID, centerY - 2 * lineHeight, tr(STR_CALM_PREPARING_COVERS), true,
                            EpdFontFamily::BOLD);
  char count[32];
  snprintf(count, sizeof(count), tr(STR_CALM_COVER_COUNT), static_cast<unsigned>(done), static_cast<unsigned>(total));
  renderer.drawCenteredText(UI_10_FONT_ID, centerY - lineHeight + 4, count);
  // A rounded progress bar.
  const int barWidth = width - 8 * calm::SIDE_MARGIN;
  const int barX = left + (width - barWidth) / 2;
  const int barY = centerY + 8;
  constexpr int barHeight = 12;
  renderer.drawRoundedRect(barX, barY, barWidth, barHeight, 1, barHeight / 2, true);
  const int fill = total > 0 ? (barWidth - 4) * done / total : 0;
  if (fill > 0) renderer.fillRoundedRect(barX + 2, barY + 2, std::max(fill, barHeight - 4), barHeight - 4,
                                         (barHeight - 4) / 2, Color::Black);
  renderer.drawCenteredText(UI_10_FONT_ID, barY + barHeight + lineHeight, tr(STR_CALM_BACK_TO_SKIP));
  renderer.displayBuffer(done == 0 ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
}

void CalmLibraryActivity::prepareCovers() {
  // Called with the render lock held. Covers are made once; afterwards the
  // marker skips even the per-book existence checks.
  const char* doneMarker = grid ? COVERS_DONE_GRID : COVERS_DONE_LIST;
  if (!index.isOpen() || Storage.exists(doneMarker)) return;
  const int count = index.bookCount();
  const int w = coverWidth();
  const int h = coverHeight();

  // Pass 1: count what is missing (cheap: two file checks per book).
  int missing = 0;
  library::ClixRecord record{};
  std::string path;
  for (int ordinal = 0; ordinal < count; ordinal++) {
    if (!index.readRecord(static_cast<uint16_t>(ordinal), record) || !index.readPath(record, path)) continue;
    if (coverNeed(path, w, h) == CoverNeed::Missing) missing++;
  }

  // Pass 2: make them, with progress and Back to skip (the rest show a blank
  // book and are tried again on the next boot).
  bool skipped = false;
  int done = 0;
  if (missing > 0) drawCoverProgress(0, missing);
  for (int ordinal = 0; ordinal < count && done < missing; ordinal++) {
    if (scanCancelRequested(this)) {
      skipped = true;
      break;
    }
    if (!index.readRecord(static_cast<uint16_t>(ordinal), record) || !index.readPath(record, path)) continue;
    if (coverNeed(path, w, h) != CoverNeed::Missing) continue;
    Epub epub(path, "/.crosspoint");
    if (!epub.generateThumbBmpFromSource(w, h, &renderer, SETTINGS.getReaderFontId())) {
      LOG_INF("CALM", "No cover for %s", path.c_str());
      FsFile marker;
      if (Storage.openFileForWrite("CALM", noCoverMarker(epub), marker)) marker.close();
    }
    done++;
    drawCoverProgress(done, missing);
  }
  if (skipped) {
    LOG_INF("CALM", "Cover preparation skipped at %d of %d", done, missing);
    // Wait for Back to be let go so the Library does not read it as a press.
    while (mappedInput.isPressed(MappedInputManager::Button::Back)) {
      delay(10);
      mappedInput.update();
    }
    return;
  }
  FsFile marker;
  if (Storage.openFileForWrite("CALM", doneMarker, marker)) marker.close();
  LOG_INF("CALM", "Covers ready: made %d", done);
}

void CalmLibraryActivity::onExit() {
  index.close();
  for (auto& row : rows) row = Row{};
  Activity::onExit();
}

void CalmLibraryActivity::computeLayout() {
  int t = 0, r = 0, b = 0, l = 0;
  renderer.getOrientedViewableTRBL(&t, &r, &b, &l);
  top = t;
  left = l;
  width = renderer.getScreenWidth() - l - r;
  height = renderer.getScreenHeight() - t - b;
  const int listHeight = height - calm::HEADER_HEIGHT - calm::FOOTER_HEIGHT;
  if (grid) {
    // pageRows counts books per page in both views.
    pageRows = calm::GRID_COLUMNS * calm::GRID_ROWS;
    rowHeight = listHeight / calm::GRID_ROWS;
    return;
  }
  pageRows = std::clamp(listHeight / calm::ROW_HEIGHT, 1, MAX_PAGE_ROWS);
  // Share the leftover height between the rows so the list fills the screen.
  rowHeight = std::max(calm::ROW_HEIGHT, listHeight / pageRows);
}

int CalmLibraryActivity::coverWidth() const { return grid ? calm::GRID_COVER_WIDTH : calm::COVER_WIDTH; }

int CalmLibraryActivity::coverHeight() const { return grid ? calm::GRID_COVER_HEIGHT : calm::COVER_HEIGHT; }

void CalmLibraryActivity::setGrid(const bool on) {
  if (on == grid) return;
  grid = on;
  SETTINGS.calmLibraryGrid = on ? 1 : 0;
  if (!SETTINGS.saveToFile()) LOG_ERR("CALM", "Cannot save Library view");
  {
    RenderLock lock;
    computeLayout();
    // The other view's covers are made once, the first time it is shown.
    prepareCovers();
    reload();
    selection = -1;  // stay on the header, like a sort change
  }
  requestUpdate();
}

bool CalmLibraryActivity::ensureIndex() {
  if (library::libraryIndexNeedsRefresh() || !index.open(library::libraryIndexPath()) ||
      index.header().metadataEnabled == 0) {
    return rebuildIndex();
  }
  scanFailed = false;
  return true;
}

bool CalmLibraryActivity::rebuildIndex() {
  index.close();
  // New books may arrive with this scan; let them get covers.
  if (Storage.exists(COVERS_DONE_LIST)) Storage.remove(COVERS_DONE_LIST);
  if (Storage.exists(COVERS_DONE_GRID)) Storage.remove(COVERS_DONE_GRID);
  library::BuildStats stats;
  library::BuildCallbacks callbacks;
  callbacks.context = this;
  callbacks.cancelRequested = &CalmLibraryActivity::scanCancelRequested;
  callbacks.progress = &CalmLibraryActivity::onScanProgress;
  const bool built = library::buildLibraryIndex("/", stats, /*readMetadata=*/true, &callbacks);
  // A cancelled or failed build leaves the previous index installed; show it.
  const bool opened = index.open(library::libraryIndexPath()) || index.openForReconciliation(library::libraryIndexPath());
  scanFailed = !built && !opened;
  if (!built) LOG_ERR("CALM", "Library scan did not complete");
  return opened;
}

bool CalmLibraryActivity::scanCancelRequested(void* context) {
  auto* self = static_cast<CalmLibraryActivity*>(context);
  // The build blocks the main loop, so poll input here.
  self->mappedInput.update();
  return self->mappedInput.isPressed(MappedInputManager::Button::Back) ||
         self->mappedInput.wasPressed(MappedInputManager::Button::Back);
}

void CalmLibraryActivity::onScanProgress(void* context, const library::BuildProgress& progress) {
  const auto* self = static_cast<const CalmLibraryActivity*>(context);
  if (progress.phase == library::BuildPhase::Organizing) {
    self->drawScanScreen(tr(STR_LIBRARY_ORGANIZING));
    return;
  }
  char message[96];
  snprintf(message, sizeof(message), tr(STR_LIBRARY_SCAN_COUNT), static_cast<unsigned>(progress.books));
  self->drawScanScreen(message);
}

void CalmLibraryActivity::drawScanScreen(const char* message) const {
  renderer.clearScreen();
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  renderer.drawCenteredText(UI_12_FONT_ID, top + height / 2 - lineHeight, message);
  renderer.drawCenteredText(UI_10_FONT_ID, top + height / 2 + lineHeight / 2, tr(STR_CALM_BACK_TO_CANCEL));
  renderer.displayBuffer();
}

void CalmLibraryActivity::reload() {
  bookCount = index.isOpen() ? index.bookCount() : 0;
  if (sort == Sort::Recent) resolveRecents();
  selection = bookCount == 0 ? -1 : std::clamp(selection, -1, bookCount - 1);
  loadedPage = -1;
  loadPage(selection < 0 ? 0 : selection / pageRows);
}

void CalmLibraryActivity::resolveRecents() {
  recentCount = 0;
  if (!index.isOpen() || bookCount == 0) return;
  const auto& books = RECENT_BOOKS.getBooks();
  // recentRowsFor() resolves at most MAX_IDENTITY_LOOKUPS identities per call.
  for (size_t start = 0; start < books.size(); start += library::LibraryIndexFile::MAX_IDENTITY_LOOKUPS) {
    const size_t count = std::min(books.size() - start, library::LibraryIndexFile::MAX_IDENTITY_LOOKUPS);
    library::BookIdentity ids[library::LibraryIndexFile::MAX_IDENTITY_LOOKUPS]{};
    uint16_t out[library::LibraryIndexFile::MAX_IDENTITY_LOOKUPS]{};
    for (size_t i = 0; i < count; i++) {
      const auto& path = books[start + i].path;
      // Size 0: the hash alone decides, so no second file handle is needed.
      ids[i] = {library::clixPathHash(path.data(), path.size()), 0};
    }
    if (!index.recentRowsFor(ids, count, out)) {
      LOG_ERR("CALM", "Cannot resolve recent books");
      break;
    }
    for (size_t i = 0; i < count && recentCount < recentRows.size(); i++) {
      if (out[i] == NO_ROW || out[i] >= bookCount) continue;
      if (std::find(recentRows.begin(), recentRows.begin() + recentCount, out[i]) != recentRows.begin() + recentCount)
        continue;
      recentRows[recentCount++] = out[i];
    }
  }
  // The rest of the list walks date-added newest first; record where the
  // recent books sit in that walk so they are skipped there.
  for (uint8_t i = 0; i < recentCount; i++)
    excludedDescPositions[i] = static_cast<uint16_t>(bookCount - 1 - recentRows[i]);
  std::sort(excludedDescPositions.begin(), excludedDescPositions.begin() + recentCount);
}

library::SortOrder CalmLibraryActivity::indexOrder() const {
  switch (sort) {
    case Sort::Title:
      return library::SortOrder::TitleAsc;
    case Sort::Author:
      return library::SortOrder::AuthorAsc;
    case Sort::Series:
      return library::SortOrder::SeriesAsc;
    default:
      return library::SortOrder::RecentDesc;
  }
}

uint16_t CalmLibraryActivity::ordinalForRow(const int row) {
  if (row < 0 || row >= bookCount) return NO_ROW;
  if (sort != Sort::Recent) return index.ordinalForRow(indexOrder(), static_cast<uint16_t>(row));
  if (row < recentCount) return index.ordinalForRow(library::SortOrder::RecentAsc, recentRows[row]);
  // The (row - recentCount)-th newest book that is not already listed above.
  int position = row - recentCount;
  for (uint8_t i = 0; i < recentCount; i++) {
    if (excludedDescPositions[i] <= position) position++;
  }
  if (position >= bookCount) return NO_ROW;
  return index.ordinalForRow(library::SortOrder::RecentDesc, static_cast<uint16_t>(position));
}

bool CalmLibraryActivity::readRow(const int row, Row& out) {
  library::ClixRecord record{};
  const uint16_t ordinal = ordinalForRow(row);
  std::string author;
  if (ordinal == NO_ROW || !index.readRecord(ordinal, record) || !index.readDisplayText(record, out.title, author) ||
      !index.readPath(record, out.path)) {
    LOG_ERR("CALM", "Cannot read library row %d", row);
    return false;
  }
  if (out.title.empty()) out.title = fileStem(out.path);

  out.detail = author;
  std::string series;
  if (index.readSeries(record, series) && !series.empty()) {
    if (!out.detail.empty()) out.detail += " \xC2\xB7 ";  // middle dot
    out.detail += series;
    uint32_t stored = 0;
    float position = 0;
    if (index.readSeriesPosition(record, stored) && decodeSeriesPosition(stored, position)) {
      char number[16];
      if (position == std::floor(position))
        snprintf(number, sizeof(number), " %d", static_cast<int>(position));
      else
        snprintf(number, sizeof(number), " %.1f", static_cast<double>(position));
      out.detail += number;
    }
  }

  out.progress.clear();
  if (BookActions::isBookCompleted(out.path)) {
    out.progress = tr(STR_CALM_FINISHED);
  } else {
    // Only books with reading history carry a progress cache.
    const auto& recents = RECENT_BOOKS.getBooks();
    const auto it = std::find_if(recents.begin(), recents.end(),
                                 [&out](const RecentBook& book) { return book.path == out.path; });
    if (it != recents.end()) {
      const float percent = FsHelpers::hasEpubExtension(it->path) ? RecentBookProgress::loadCachedEpubPercent(*it)
                                                                  : RecentBookProgress::loadPercent(*it);
      out.progress = RecentBookProgress::formatPercent(percent);
    }
  }

  out.coverPath.clear();
  out.cover = Cover::None;
  if (FsHelpers::hasEpubExtension(out.path)) {
    const Epub epub(out.path, "/.crosspoint");
    out.coverPath = epub.getThumbBmpPath(coverWidth(), coverHeight());
    if (Storage.exists(out.coverPath.c_str())) out.cover = Cover::Ready;
  }
  return true;
}

void CalmLibraryActivity::loadPage(const int page) {
  if (page == loadedPage) return;
  loadedPage = page;
  pageRowCount = 0;
  const int first = page * pageRows;
  for (int slot = 0; slot < pageRows && first + slot < bookCount; slot++) {
    if (!readRow(first + slot, rows[slot])) break;
    pageRowCount++;
  }
}

void CalmLibraryActivity::moveSelection(const int next) {
  if (bookCount == 0) {
    selection = -1;
    return;
  }
  {
    // loadPage() rewrites the rows render() is drawing.
    RenderLock lock;
    if (next < 0 && selection >= 0) {
      headerFocus = static_cast<int>(sort);  // arrive on the active sort
      lastListSelection = selection;
    }
    selection = std::clamp(next, -1, bookCount - 1);
    if (selection >= 0) loadPage(selection / pageRows);
  }
  requestUpdate();
}

void CalmLibraryActivity::setSort(const Sort next) {
  if (next == sort) return;
  sort = next;
  SETTINGS.librarySortMethod = sortToSettings(sort);
  SETTINGS.librarySortDescending = sort == Sort::Recent ? 1 : 0;
  if (!SETTINGS.saveToFile()) LOG_ERR("CALM", "Cannot save Library sort");
  {
    RenderLock lock;
    // Stay on the header so the next Left/Right keeps browsing sorts.
    reload();
    selection = -1;
    lastListSelection = 0;  // a new order starts from its first book
  }
  requestUpdate();
}

void CalmLibraryActivity::openSelected() {
  if (selection < 0 || selection >= bookCount) return;
  const Row& row = rows[selection - loadedPage * pageRows];
  const std::string path = row.path;
  calm::setOpeningBook(path, row.title, row.detail);
  index.close();
  onSelectBook(path);
}

void CalmLibraryActivity::openBookCard() {
  if (selection < 0 || selection >= bookCount) return;
  const Row row = rows[selection - loadedPage * pageRows];
  const int slot = selection - loadedPage * pageRows;
  const int rowTop = top + calm::HEADER_HEIGHT + (slot / columns()) * rowHeight;
  // In the grid the card sits over the covers as a centered modal.
  auto card = makeUniqueNoThrow<CalmBookCardActivity>(renderer, mappedInput, row.path, row.title, row.detail,
                                                      row.progress, grid ? -1 : rowTop,
                                                      grid ? -1 : rowTop + rowHeight);
  if (!card) {
    LOG_ERR("CALM", "Cannot allocate book card");
    return;
  }
  startActivityForResult(std::move(card), [this, path = row.path, title = row.title,
                                          detail = row.detail](const ActivityResult& result) {
    if (result.isCancelled) return;
    const auto* choice = std::get_if<OptionSelectionResult>(&result.data);
    if (!choice) return;
    switch (static_cast<CalmBookCardActivity::Outcome>(choice->index)) {
      case CalmBookCardActivity::Outcome::Open:
        calm::setOpeningBook(path, title, detail);
        index.close();
        onSelectBook(path);
        return;
      case CalmBookCardActivity::Outcome::Deleted: {
        RenderLock lock;
        index.close();
        ensureIndex();
        reload();
        return;
      }
      case CalmBookCardActivity::Outcome::Changed: {
        RenderLock lock;
        loadedPage = -1;
        loadPage(selection < 0 ? 0 : selection / pageRows);
        return;
      }
    }
  });
}

void CalmLibraryActivity::loop() {
  using Button = MappedInputManager::Button;


  // Confirm: release opens, a long hold opens the book card instead.
  if (ignoreConfirmRelease) {
    if (!mappedInput.isPressed(Button::Confirm)) ignoreConfirmRelease = false;
    return;
  }
  if (mappedInput.isPressed(Button::Confirm)) {
    confirmHeld = true;
    if (selection >= 0 && mappedInput.getHeldTime() >= LONG_PRESS_MS) {
      // The card takes over input; when it closes, wait for this hold (if any)
      // to end so its release is not read as a fresh press here.
      confirmHeld = false;
      ignoreConfirmRelease = true;
      openBookCard();
    }
    return;
  }
  if (confirmHeld && mappedInput.wasReleased(Button::Confirm)) {
    confirmHeld = false;
    if (selection < 0) {
      if (headerFocus == HEADER_SETTINGS_STOP)
        activityManager.goToSettings();
      else if (headerFocus == HEADER_VIEW_STOP)
        setGrid(!grid);
      else
        setSort(static_cast<Sort>(headerFocus));
    } else {
      openSelected();
    }
    return;
  }

  if (mappedInput.wasReleased(Button::Back)) {
    // Library is home, so Back toggles between the list and the header (sorts
    // and Settings), returning to the same book.
    if (bookCount == 0) return;
    if (selection >= 0) {
      lastListSelection = selection;
      moveSelection(-1);
    } else {
      moveSelection(std::clamp(lastListSelection, 0, bookCount - 1));
    }
    return;
  }

  if (selection < 0) {
    const bool left = mappedInput.wasReleased(Button::Left);
    if (left || mappedInput.wasReleased(Button::Right)) {
      headerFocus = (headerFocus + (left ? HEADER_STOPS - 1 : 1)) % HEADER_STOPS;
      requestUpdate();
      return;
    }
  } else {
    // List: Left/Right page. Grid: they step between covers.
    const int sideStep = grid ? 1 : pageRows;
    if (mappedInput.wasReleased(Button::Left)) {
      moveSelection(std::max(0, selection - sideStep));
      return;
    }
    if (mappedInput.wasReleased(Button::Right)) {
      moveSelection(std::min(bookCount - 1, selection + sideStep));
      return;
    }
  }

  // Up/Down move a row: one book in the list, one line of covers in the grid.
  // Up from the top line reaches the header.
  navigator.onRelease({Button::Down, Button::Down}, [this] {
    const int next = selection < 0 ? std::clamp(lastListSelection, 0, bookCount - 1) : selection + columns();
    moveSelection(std::min(next, bookCount - 1));
  });
  navigator.onRelease({Button::Up, Button::Up}, [this] {
    if (selection < 0) return;
    moveSelection(selection - columns() < 0 ? -1 : selection - columns());
  });
  navigator.onContinuous({Button::Down, Button::Down},
                         [this] { moveSelection(std::min(bookCount - 1, selection + pageRows)); });
  navigator.onContinuous({Button::Up, Button::Up}, [this] { moveSelection(std::max(-1, selection - pageRows)); });
}

const char* CalmLibraryActivity::sortLabel(const Sort sort) {
  switch (sort) {
    case Sort::Title:
      return tr(STR_LIBRARY_TITLE);
    case Sort::Author:
      return tr(STR_LIBRARY_AUTHOR);
    case Sort::Series:
      return tr(STR_LIBRARY_SERIES);
    default:
      return tr(STR_CALM_SORT_RECENT);
  }
}

void CalmLibraryActivity::drawHeader() const {
  const int x = left + calm::SIDE_MARGIN;
  const int controlY = top + (calm::HEADER_HEIGHT - calm::CONTROL_HEIGHT) / 2;
  const bool headerFocused = selection < 0;
  constexpr int ringRoom = calm::FOCUS_RING_GAP + calm::FOCUS_RING_WIDTH;
  static_assert(calm::SIDE_MARGIN >= ringRoom, "focus ring needs the margin");

  // Right to left: Settings (cog), then the list/grid toggle, then the sort.
  // Both buttons are square; inset by the focus ring so it is never clipped.
  const int cogX = left + width - calm::SIDE_MARGIN - calm::CONTROL_HEIGHT;
  // Tighter than CHIP_GAP: the header has to fit the title and three controls in 456 px.
  constexpr int gap = 6;
  const int viewX = cogX - gap - calm::CONTROL_HEIGHT;

  // Sort segments take the bold (active) width, so nothing shifts as the
  // sort changes, plus 3..10 px a side depending on the room left.
  const int segments = static_cast<int>(Sort::Count);
  int segmentWidths[static_cast<int>(Sort::Count)];
  int textTotal = 0;
  for (int i = 0; i < segments; i++) {
    segmentWidths[i] = renderer.getTextWidth(UI_10_FONT_ID, sortLabel(static_cast<Sort>(i)), EpdFontFamily::BOLD);
    textTotal += segmentWidths[i];
  }
  // The serif title steps down a size when the controls need the room.
  constexpr int minPadding = 3;
  const int minimumSortX = viewX - gap - (textTotal + 2 * minPadding * segments);
  int titleFont = BITTER_14_FONT_ID;
  int titleWidth = renderer.getTextWidth(titleFont, tr(STR_LIBRARY), EpdFontFamily::BOLD);
  if (x + titleWidth + gap + ringRoom > minimumSortX) {
    titleFont = BITTER_12_FONT_ID;
    titleWidth = renderer.getTextWidth(titleFont, tr(STR_LIBRARY), EpdFontFamily::BOLD);
  }
  const int titleY = top + (calm::HEADER_HEIGHT - renderer.getLineHeight(titleFont)) / 2;
  renderer.drawText(titleFont, x, titleY, tr(STR_LIBRARY), true, EpdFontFamily::BOLD);
  const int titleRight = x + titleWidth;

  // Settings: a rounded square with a cog.
  calm::drawButton(renderer, cogX, controlY, calm::CONTROL_HEIGHT, calm::CONTROL_HEIGHT, false);
  calm::drawIcon(renderer, icon_cog_24, cogX + (calm::CONTROL_HEIGHT - icon_cog_24.w) / 2,
                 controlY + (calm::CONTROL_HEIGHT - icon_cog_24.h) / 2);
  if (headerFocused && headerFocus == HEADER_SETTINGS_STOP)
    calm::drawFocusRing(renderer, cogX, controlY, calm::CONTROL_HEIGHT, calm::CONTROL_HEIGHT);

  // View: a rounded square showing the current view, list lines or a 2 x 2
  // grid; Confirm switches to the other.
  calm::drawButton(renderer, viewX, controlY, calm::CONTROL_HEIGHT, calm::CONTROL_HEIGHT, false);
  {
    const int gx = viewX + (calm::CONTROL_HEIGHT - 14) / 2;
    const int gy = controlY + (calm::CONTROL_HEIGHT - 14) / 2;
    if (grid) {
      for (int cell = 0; cell < 4; cell++) renderer.fillRect(gx + (cell % 2) * 8, gy + (cell / 2) * 8, 6, 6, true);
    } else {
      for (int bar = 0; bar < 3; bar++) renderer.fillRect(gx, gy + 1 + bar * 5, 14, 2, true);
    }
  }
  if (headerFocused && headerFocus == HEADER_VIEW_STOP)
    calm::drawFocusRing(renderer, viewX, controlY, calm::CONTROL_HEIGHT, calm::CONTROL_HEIGHT);

  // Sort: a segmented control. The active sort is filled; the header cursor is
  // a ring around one segment.
  const int room = viewX - gap - (titleRight + gap + ringRoom);
  const int segmentPadding = std::clamp((room - textTotal) / (2 * segments), minPadding, 10);
  int total = 0;
  for (int i = 0; i < segments; i++) {
    segmentWidths[i] += 2 * segmentPadding;
    total += segmentWidths[i];
  }
  int segX = viewX - gap - total;
  renderer.drawRoundedRect(segX, controlY, total, calm::CONTROL_HEIGHT, 1, calm::RADIUS, true);
  for (int i = 0; i < segments; i++) {
    const bool active = static_cast<int>(sort) == i;
    const int w = segmentWidths[i];
    if (active) {
      renderer.fillRoundedRect(segX, controlY, w, calm::CONTROL_HEIGHT, calm::RADIUS, i == 0, i == segments - 1,
                               i == 0, i == segments - 1, Color::Black);
    } else if (i > 0 && static_cast<int>(sort) != i - 1) {
      renderer.fillRect(segX, controlY + 6, 1, calm::CONTROL_HEIGHT - 12);  // divider
    }
    calm::drawCenteredLabel(renderer, UI_10_FONT_ID, segX, controlY, w, calm::CONTROL_HEIGHT,
                            sortLabel(static_cast<Sort>(i)), active,
                            active ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    if (headerFocused && headerFocus == i) calm::drawFocusRing(renderer, segX, controlY, w, calm::CONTROL_HEIGHT);
    segX += w;
  }
  renderer.fillRect(left, top + calm::HEADER_HEIGHT - 1, width, 1);
}

void CalmLibraryActivity::drawCover(const Row& row, const int x, const int y, const bool selected) const {
  // Bitmaps only draw their ink, so give the cover white paper first (it may
  // sit on the black selection pill).
  const int w = coverWidth();
  const int h = coverHeight();
  renderer.fillRect(x, y, w, h, false);
  bool drawn = false;
  if (row.cover == Cover::Ready) {
    FsFile file;
    if (Storage.openFileForRead("CALM", row.coverPath, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok)
        drawn = renderer.drawBitmap(bitmap, x, y, w, h);
      file.close();
    }
  }
  if (!drawn) {
    // No cover (yet): a quiet blank book, so every row lines up.
    const int inset = w / 7;
    renderer.fillRect(x + inset, y + h / 6, w - 2 * inset, 2, true);
    renderer.fillRect(x + inset, y + h / 6 + 6, w - 3 * inset, 2, true);
  }
  // A 1 px frame keeps light covers from melting into the page (or the pill).
  renderer.drawRect(x, y, w, h, true);
  if (selected) renderer.drawRect(x - 1, y - 1, w + 2, h + 2, false);
}

void CalmLibraryActivity::drawGridCell(const int slot, const Row& row, const bool selected) const {
  const int innerWidth = width - 2 * calm::SIDE_MARGIN;
  const int cellWidth = innerWidth / calm::GRID_COLUMNS;
  const int cellX = left + calm::SIDE_MARGIN + (slot % calm::GRID_COLUMNS) * cellWidth;
  const int cellY = top + calm::HEADER_HEIGHT + (slot / calm::GRID_COLUMNS) * rowHeight;
  const int coverX = cellX + (cellWidth - calm::GRID_COVER_WIDTH) / 2;
  const int coverY = cellY + 8;
  drawCover(row, coverX, coverY, false);
  if (selected) {
    // The cursor: a thick rounded ring around the cover.
    calm::drawFocusRing(renderer, coverX, coverY, calm::GRID_COVER_WIDTH, calm::GRID_COVER_HEIGHT);
  }

  // Title (two lines at most), then progress, under the cover.
  const int textWidth = cellWidth - 8;
  const int textX = cellX + 4;
  int y = coverY + calm::GRID_COVER_HEIGHT + 8;
  const int titleLine = renderer.getLineHeight(UI_10_FONT_ID);
  const auto style = selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  for (const auto& line : calm::wrapText(renderer, UI_10_FONT_ID, row.title, textWidth, 2, style)) {
    const int w = renderer.getTextWidth(UI_10_FONT_ID, line.c_str(), style);
    renderer.drawText(UI_10_FONT_ID, textX + (textWidth - w) / 2, y, line.c_str(), true, style);
    y += titleLine;
  }
  if (!row.progress.empty()) {
    const int w = renderer.getTextWidth(SMALL_FONT_ID, row.progress.c_str(), EpdFontFamily::BOLD);
    renderer.drawText(SMALL_FONT_ID, textX + (textWidth - w) / 2, y + 1, row.progress.c_str(), true,
                      EpdFontFamily::BOLD);
  }
}

void CalmLibraryActivity::drawRow(const int slot, const Row& row, const bool selected) const {
  const int y = top + calm::HEADER_HEIGHT + slot * rowHeight;
  const int x = left + calm::SIDE_MARGIN;
  const int innerWidth = width - 2 * calm::SIDE_MARGIN;
  // Selected: a rounded pill inset from the screen edge, 2 px off each rule.
  if (selected) calm::fillPill(renderer, left + calm::PILL_INSET, y + 2, width - 2 * calm::PILL_INSET, rowHeight - 4);

  const int coverY = y + (rowHeight - calm::COVER_HEIGHT) / 2;  // list covers only
  drawCover(row, x, coverY, selected);

  // Title, author/series and progress, centered as a block beside the cover.
  const int textX = x + calm::COVER_WIDTH + calm::COVER_GAP;
  const int textWidth = innerWidth - calm::COVER_WIDTH - calm::COVER_GAP - 4;
  const int titleLine = renderer.getLineHeight(UI_12_FONT_ID);
  const int detailLine = renderer.getLineHeight(UI_10_FONT_ID);
  const int smallLine = renderer.getLineHeight(SMALL_FONT_ID);
  const bool hasDetail = !row.detail.empty();
  const bool hasProgress = !row.progress.empty();
  const int blockHeight = titleLine + (hasDetail ? detailLine : 0) + (hasProgress ? smallLine + 2 : 0);
  int textY = y + (rowHeight - blockHeight) / 2;

  const std::string title = renderer.truncatedText(UI_12_FONT_ID, row.title.c_str(), textWidth, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, textX, textY, title.c_str(), !selected, EpdFontFamily::BOLD);
  textY += titleLine;
  if (hasDetail) {
    const std::string detail = renderer.truncatedText(UI_10_FONT_ID, row.detail.c_str(), textWidth);
    renderer.drawText(UI_10_FONT_ID, textX, textY, detail.c_str(), !selected);
    textY += detailLine;
  }
  if (hasProgress) renderer.drawText(SMALL_FONT_ID, textX, textY + 2, row.progress.c_str(), !selected, EpdFontFamily::BOLD);

  // Hairline between rows, starting under the text so covers stand free.
  if (!selected) renderer.fillRect(textX, y + rowHeight - 1, x + innerWidth - textX, 1);
}

void CalmLibraryActivity::render(RenderLock&&) {
  renderer.clearScreen();
  drawHeader();

  if (bookCount == 0) {
    renderer.drawCenteredText(UI_12_FONT_ID, top + height / 2 - renderer.getLineHeight(UI_12_FONT_ID),
                              scanFailed ? tr(STR_LIBRARY_SCAN_FAILED) : tr(STR_LIBRARY_EMPTY));
    renderer.displayBuffer();
    return;
  }

  for (int slot = 0; slot < pageRowCount; slot++) {
    const bool selected = selection == loadedPage * pageRows + slot;
    if (grid)
      drawGridCell(slot, rows[slot], selected);
    else
      drawRow(slot, rows[slot], selected);
  }

  const int pages = (bookCount + pageRows - 1) / pageRows;
  {
    char footer[24];
    snprintf(footer, sizeof(footer), "%d / %d", loadedPage + 1, pages);
    renderer.drawCenteredText(SMALL_FONT_ID,
                              top + height - calm::FOOTER_HEIGHT + (calm::FOOTER_HEIGHT -
                                                                    renderer.getLineHeight(SMALL_FONT_ID)) / 2,
                              footer);
  }
  renderer.displayBuffer();
}
