#include "delegating_display.hpp"

#include <utility>

namespace fm {

DelegatingDisplay::DelegatingDisplay(Display &backend) : backend_(&backend) {}

void DelegatingDisplay::setBackend(Display &backend) {
  backend_ = &backend;
  if (imageProvider_) backend_->setImageProvider(imageProvider_);  // 新しい裏の画面にも(setImageProvider()参照)
  backend_->setSerifSuppressed(serifSuppressed_);  // 端末から-gへ移った時も(setSerifSuppressed()参照)
}
Display &DelegatingDisplay::backend() const { return *backend_; }

void DelegatingDisplay::write(const std::string &text) { backend_->write(text); }
void DelegatingDisplay::putChar(int c) { backend_->putChar(c); }
void DelegatingDisplay::flush() { backend_->flush(); }

void DelegatingDisplay::moveCursor(int row, int col) { backend_->moveCursor(row, col); }
void DelegatingDisplay::cursorLeft() { backend_->cursorLeft(); }
void DelegatingDisplay::saveCursor() { backend_->saveCursor(); }
void DelegatingDisplay::restoreCursor() { backend_->restoreCursor(); }
void DelegatingDisplay::setVisibleCursor(int row, int col) { backend_->setVisibleCursor(row, col); }
void DelegatingDisplay::hideVisibleCursor() { backend_->hideVisibleCursor(); }

void DelegatingDisplay::clearScreen() { backend_->clearScreen(); }
void DelegatingDisplay::clearToEndOfLine() { backend_->clearToEndOfLine(); }
void DelegatingDisplay::scrollForward() { backend_->scrollForward(); }
void DelegatingDisplay::scrollReverse() { backend_->scrollReverse(); }

void DelegatingDisplay::setAttribute(Attribute attr) { backend_->setAttribute(attr); }
std::string DelegatingDisplay::attributeEscape(Attribute attr) const { return backend_->attributeEscape(attr); }
void DelegatingDisplay::setColor(int fg, int bg) { backend_->setColor(fg, bg); }

int DelegatingDisplay::rows() const { return backend_->rows(); }
int DelegatingDisplay::columns() const { return backend_->columns(); }
void DelegatingDisplay::setSize(int rows, int columns) { backend_->setSize(rows, columns); }

std::string DelegatingDisplay::keySequence(Key key) const { return backend_->keySequence(key); }

bool DelegatingDisplay::outputEnabled() const { return backend_->outputEnabled(); }
void DelegatingDisplay::setOutputEnabled(bool enabled) { backend_->setOutputEnabled(enabled); }

bool DelegatingDisplay::smoothScrollEnabled() const { return backend_->smoothScrollEnabled(); }
void DelegatingDisplay::setSmoothScrollEnabled(bool enabled) { backend_->setSmoothScrollEnabled(enabled); }

bool DelegatingDisplay::modeLineVisible() const { return backend_->modeLineVisible(); }
bool DelegatingDisplay::hasNativeScrollbar() const { return backend_->hasNativeScrollbar(); }
void DelegatingDisplay::setModeLineVisible(bool visible) { backend_->setModeLineVisible(visible); }

void DelegatingDisplay::open() { backend_->open(); }
void DelegatingDisplay::close() { backend_->close(); }

void DelegatingDisplay::leaveForExternalProgram() { backend_->leaveForExternalProgram(); }
void DelegatingDisplay::reenterFromExternalProgram() { backend_->reenterFromExternalProgram(); }
void DelegatingDisplay::leaveForSuspend() { backend_->leaveForSuspend(); }
void DelegatingDisplay::setVerticalTextHint(bool vertical) { backend_->setVerticalTextHint(vertical); }
void DelegatingDisplay::setSerifSuppressed(bool suppressed) {
  serifSuppressed_ = suppressed;
  backend_->setSerifSuppressed(suppressed);
}
void DelegatingDisplay::beginContentEdgeCapture() { backend_->beginContentEdgeCapture(); }
bool DelegatingDisplay::drawsRuby() const { return backend_->drawsRuby(); }
void DelegatingDisplay::setPendingRuby(const std::string &reading, int span, bool vertical) {
  backend_->setPendingRuby(reading, span, vertical);
}
void DelegatingDisplay::endContentEdgeCapture(int side) { backend_->endContentEdgeCapture(side); }
void DelegatingDisplay::setContentImages(const std::vector<ContentImage> &images) { backend_->setContentImages(images); }
void DelegatingDisplay::setImageProvider(ImageProvider provider) {
  imageProvider_ = std::move(provider);
  backend_->setImageProvider(imageProvider_);
}
int DelegatingDisplay::cellPixelWidth() const { return backend_->cellPixelWidth(); }
int DelegatingDisplay::cellPixelHeight() const { return backend_->cellPixelHeight(); }

void DelegatingDisplay::renderScrolled(const std::vector<ScrolledLine> &lines, int topIndex, int pixelOffsetY,
                                        const std::vector<ModeLineSegment> &modeLineSegments) {
  backend_->renderScrolled(lines, topIndex, pixelOffsetY, modeLineSegments);
}

}  // namespace fm
