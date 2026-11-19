#pragma once

#include <string>

class GfxRenderer;

// The calm opening card: while a book opens (and its first chapter is laid
// out), a centered card shows the book's title and author with a short
// status line, in place of upstream's "Loading" and "Indexing" popups.
namespace calm {

// The Library calls this before opening a book, so the card can show the
// title and author it already knows.
void setOpeningBook(const std::string& path, const std::string& title, const std::string& detail);

// The reader calls this as it starts opening `path`; the card stays active
// until endOpening(). Later relayouts get the ordinary popup.
void beginOpening(const std::string& path);
void endOpening();
bool isOpening();

// Draws the card over the current screen and refreshes it. The title and
// author come from setOpeningBook() for the same path, else from the recent
// books list, else from the fallbacks (the EPUB's metadata, or the file name).
void drawOpeningCard(GfxRenderer& renderer, const char* message, const std::string& fallbackTitle = {},
                     const std::string& fallbackAuthor = {});

}  // namespace calm
