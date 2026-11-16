#include "CalmLibraryActivity.h"

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
#include "CalmLayout.h"
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
    reload();
  }
  requestUpdate();
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
  pageRows = std::clamp(listHeight / calm::ROW_HEIGHT, 1, MAX_PAGE_ROWS);
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
  renderer.drawCenteredText(UI_10_FONT_ID, top + height / 2 + lineHeight / 2, tr(STR_CANCEL));
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
    selection = std::clamp(next, -1, bookCount - 1);
    if (selection >= 0) loadPage(selection / pageRows);
  }
  requestUpdate();
}

void CalmLibraryActivity::cycleSort() {
  sort = static_cast<Sort>((static_cast<uint8_t>(sort) + 1) % static_cast<uint8_t>(Sort::Count));
  SETTINGS.librarySortMethod = sortToSettings(sort);
  SETTINGS.librarySortDescending = sort == Sort::Recent ? 1 : 0;
  if (!SETTINGS.saveToFile()) LOG_ERR("CALM", "Cannot save Library sort");
  {
    RenderLock lock;
    // Keep the header focused so repeated Confirm presses keep cycling.
    reload();
    selection = -1;
  }
  requestUpdate();
}

void CalmLibraryActivity::openSelected() {
  if (selection < 0 || selection >= bookCount) return;
  const Row& row = rows[selection - loadedPage * pageRows];
  const std::string path = row.path;
  index.close();
  onSelectBook(path);
}

void CalmLibraryActivity::openBookCard() {
  if (selection < 0 || selection >= bookCount) return;
  const Row row = rows[selection - loadedPage * pageRows];
  auto card = makeUniqueNoThrow<CalmBookCardActivity>(renderer, mappedInput, row.path, row.title, row.detail,
                                                      row.progress);
  if (!card) {
    LOG_ERR("CALM", "Cannot allocate book card");
    return;
  }
  startActivityForResult(std::move(card), [this, path = row.path](const ActivityResult& result) {
    if (result.isCancelled) return;
    const auto* choice = std::get_if<OptionSelectionResult>(&result.data);
    if (!choice) return;
    switch (static_cast<CalmBookCardActivity::Outcome>(choice->index)) {
      case CalmBookCardActivity::Outcome::Open:
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
      if (headerItem == HeaderItem::Sort)
        cycleSort();
      else
        activityManager.goToSettings();
    } else {
      openSelected();
    }
    return;
  }

  if (mappedInput.wasReleased(Button::Back)) {
    // Library is home: Back returns to the top of the list.
    if (selection != 0 && bookCount > 0) moveSelection(0);
    return;
  }

  if (selection < 0) {
    if (mappedInput.wasReleased(Button::Left) || mappedInput.wasReleased(Button::Right)) {
      headerItem = headerItem == HeaderItem::Sort ? HeaderItem::Settings : HeaderItem::Sort;
      requestUpdate();
      return;
    }
  } else {
    if (mappedInput.wasReleased(Button::Left)) {
      moveSelection(std::max(0, selection - pageRows));
      return;
    }
    if (mappedInput.wasReleased(Button::Right)) {
      moveSelection(std::min(bookCount - 1, selection + pageRows));
      return;
    }
  }

  navigator.onRelease({Button::Down, Button::Down}, [this] { moveSelection(selection + 1); });
  navigator.onRelease({Button::Up, Button::Up}, [this] { moveSelection(selection - 1); });
  navigator.onContinuous({Button::Down, Button::Down},
                         [this] { moveSelection(std::min(bookCount - 1, selection + pageRows)); });
  navigator.onContinuous({Button::Up, Button::Up}, [this] { moveSelection(std::max(-1, selection - pageRows)); });
}

const char* CalmLibraryActivity::sortLabel() const {
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
  const int titleY = top + (calm::HEADER_HEIGHT - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
  renderer.drawText(UI_12_FONT_ID, x, titleY, tr(STR_LIBRARY), true, EpdFontFamily::BOLD);

  // Right side: "<sort>   Settings". The focused one is inverted.
  const char* labels[2] = {sortLabel(), tr(STR_SETTINGS_TITLE)};
  const int pad = calm::CHIP_PADDING;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int chipY = top + (calm::HEADER_HEIGHT - lineHeight) / 2 - 3;
  int cursor = left + width - calm::SIDE_MARGIN;
  for (int i = 1; i >= 0; i--) {
    const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, labels[i]);
    const int chipX = cursor - textWidth - 2 * pad;
    const bool focused = selection < 0 && static_cast<int>(headerItem) == i;
    if (focused) renderer.fillRect(chipX, chipY, textWidth + 2 * pad, lineHeight + 6);
    renderer.drawText(UI_10_FONT_ID, chipX + pad, chipY + 3, labels[i], !focused);
    cursor = chipX - calm::CHIP_GAP;
  }
  renderer.fillRect(left, top + calm::HEADER_HEIGHT - 1, width, 1);
}

void CalmLibraryActivity::drawRow(const int slot, const Row& row, const bool selected) const {
  const int y = top + calm::HEADER_HEIGHT + slot * calm::ROW_HEIGHT;
  const int x = left + calm::SIDE_MARGIN;
  const int innerWidth = width - 2 * calm::SIDE_MARGIN;
  if (selected) renderer.fillRect(left, y, width, calm::ROW_HEIGHT);

  const int titleY = y + calm::ROW_PADDING_TOP;
  const std::string title = renderer.truncatedText(UI_12_FONT_ID, row.title.c_str(), innerWidth, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, x, titleY, title.c_str(), !selected, EpdFontFamily::BOLD);

  const int detailY = titleY + renderer.getLineHeight(UI_12_FONT_ID);
  int detailWidth = innerWidth;
  if (!row.progress.empty()) {
    const int progressWidth = renderer.getTextWidth(UI_10_FONT_ID, row.progress.c_str());
    renderer.drawText(UI_10_FONT_ID, x + innerWidth - progressWidth, detailY, row.progress.c_str(), !selected);
    detailWidth -= progressWidth + calm::CHIP_GAP;
  }
  if (!row.detail.empty()) {
    const std::string detail = renderer.truncatedText(UI_10_FONT_ID, row.detail.c_str(), detailWidth);
    renderer.drawText(UI_10_FONT_ID, x, detailY, detail.c_str(), !selected);
  }
  if (!selected) renderer.fillRectDither(left, y + calm::ROW_HEIGHT - 1, width, 1, LightGray);
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
    drawRow(slot, rows[slot], selection == loadedPage * pageRows + slot);
  }

  const int pages = (bookCount + pageRows - 1) / pageRows;
  if (pages > 1) {
    char footer[24];
    snprintf(footer, sizeof(footer), "%d / %d", loadedPage + 1, pages);
    renderer.drawCenteredText(SMALL_FONT_ID,
                              top + height - calm::FOOTER_HEIGHT + (calm::FOOTER_HEIGHT -
                                                                    renderer.getLineHeight(SMALL_FONT_ID)) / 2,
                              footer);
  }
  renderer.displayBuffer();
}
