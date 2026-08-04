#include "delegating_display.hpp"

namespace fm {

DelegatingDisplay::DelegatingDisplay(Display &backend) : backend_(&backend) {}

void DelegatingDisplay::setBackend(Display &backend) { backend_ = &backend; }
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

int DelegatingDisplay::rows() const { return backend_->rows(); }
int DelegatingDisplay::columns() const { return backend_->columns(); }
void DelegatingDisplay::setSize(int rows, int columns) { backend_->setSize(rows, columns); }

std::string DelegatingDisplay::keySequence(Key key) const { return backend_->keySequence(key); }

bool DelegatingDisplay::outputEnabled() const { return backend_->outputEnabled(); }
void DelegatingDisplay::setOutputEnabled(bool enabled) { backend_->setOutputEnabled(enabled); }

bool DelegatingDisplay::smoothScrollEnabled() const { return backend_->smoothScrollEnabled(); }
void DelegatingDisplay::setSmoothScrollEnabled(bool enabled) { backend_->setSmoothScrollEnabled(enabled); }

void DelegatingDisplay::open() { backend_->open(); }
void DelegatingDisplay::close() { backend_->close(); }

void DelegatingDisplay::renderScrolled(const std::vector<std::string> &lines, int topIndex, int pixelOffsetY,
                                        const std::string &modeLineText) {
  backend_->renderScrolled(lines, topIndex, pixelOffsetY, modeLineText);
}

}  // namespace fm
