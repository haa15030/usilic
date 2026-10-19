// GUI無しのビルド(2026-10-05、.steering/20261005-windows-gitbash/): SDL2の無い環境(MSYS2の
// MSYS環境等)で、graphical_display.cppの代わりにビルドする(Makefileの USILIC_NO_GUI)。
// create()はnullptrを返す(-gは使えない)ので、他のメンバ関数が呼ばれることは無い。リンクを
// 通すためだけの空の定義で、tools/gen_graphical_display_stub.pyが作る(手で直さない)。
#pragma GCC diagnostic ignored "-Wunused-parameter"

#include "graphical_display.hpp"

namespace fm {

struct GraphicalDisplay::Impl {};

GraphicalDisplay::GraphicalDisplay() : impl_(std::make_unique<Impl>()) {}
GraphicalDisplay::~GraphicalDisplay() = default;

std::unique_ptr<GraphicalDisplay> GraphicalDisplay::create(int rows, int columns, const std::string &fontPath,
                                                             int fontPointSize) { return nullptr; }
void GraphicalDisplay::write(const std::string &text) {}
void GraphicalDisplay::putChar(int c) {}
void GraphicalDisplay::putByte(unsigned char c) {}
void GraphicalDisplay::commitGlyph(const std::string &utf8Bytes, int width) {}
void GraphicalDisplay::setContentOffsetX(int pixels, int rowCount) {}
void GraphicalDisplay::renderCellBackground(int row, int col) {}
void GraphicalDisplay::renderCell(int row, int col) {}
void GraphicalDisplay::renderCursor() {}
void GraphicalDisplay::flush() {}
void GraphicalDisplay::moveCursor(int row, int col) {}
void GraphicalDisplay::setVisibleCursor(int row, int col) {}
void GraphicalDisplay::hideVisibleCursor() {}
void GraphicalDisplay::cursorLeft() {}
void GraphicalDisplay::saveCursor() {}
void GraphicalDisplay::restoreCursor() {}
void GraphicalDisplay::clearScreen() {}
void GraphicalDisplay::clearToEndOfLine() {}
void GraphicalDisplay::scrollForward() {}
void GraphicalDisplay::scrollReverse() {}
void GraphicalDisplay::setFontFamilyCandidates(std::vector<std::string> serif, std::vector<std::string> sansSerif) {}
void GraphicalDisplay::setAttribute(Attribute attr) {}
void GraphicalDisplay::setColor(int fg, int bg) {}
std::string GraphicalDisplay::attributeEscape(Attribute) const { return {}; }
int GraphicalDisplay::rows() const { return {}; }
int GraphicalDisplay::columns() const { return {}; }
void GraphicalDisplay::setSize(int rows, int columns) {}
void GraphicalDisplay::handleWindowResized(int newPixelWidth, int newPixelHeight) {}
void GraphicalDisplay::setLineHeightRatios(double horizontal, double vertical) {}
void GraphicalDisplay::setVerticalLineSpacing(bool vertical) {}
std::pair<int, double> GraphicalDisplay::adjustLineHeight(int deltaPixels) { return {}; }
void GraphicalDisplay::reapplyLineHeight() {}
void GraphicalDisplay::reapplyLineHeightKeepingExtra() {}
int GraphicalDisplay::horizontalRows() const { return {}; }
std::pair<int, int> GraphicalDisplay::adjustLetterSpacing(int deltaPixels) { return {}; }
void GraphicalDisplay::setLetterSpacingExtras(int horizontal, int vertical) {}
std::pair<int, int> GraphicalDisplay::letterSpacingExtras() { return {}; }
int GraphicalDisplay::horizontalColumns() const { return {}; }
bool GraphicalDisplay::consumeSizeChanged() { return {}; }
void GraphicalDisplay::adjustFontSize(int deltaPoints) {}
bool GraphicalDisplay::setFont(FontKind kind, const std::string &path) { return {}; }
#ifdef _WIN32
std::vector<GraphicalDisplay::ScannedFontFace> GraphicalDisplay::scanFontFiles(
    const std::vector<std::string> &directories) { return {}; }
#endif
std::string GraphicalDisplay::currentFontPath(FontKind kind) const { return {}; }
void GraphicalDisplay::cycleColorPreset() {}
std::string GraphicalDisplay::keySequence(Key) const { return {}; }
bool GraphicalDisplay::outputEnabled() const { return {}; }
void GraphicalDisplay::setOutputEnabled(bool enabled) {}
bool GraphicalDisplay::smoothScrollEnabled() const { return {}; }
void GraphicalDisplay::setSmoothScrollEnabled(bool enabled) {}
void GraphicalDisplay::setVerticalTextHint(bool vertical) {}
void GraphicalDisplay::setScrolledCursor(std::optional<std::pair<int, int>> lineAndColumn) {}
void GraphicalDisplay::setPendingRuby(const std::string &reading, int span, bool vertical) {}
void GraphicalDisplay::renderRuby(int row, int col) {}
void GraphicalDisplay::drawRubyText(int x, int y, const std::string &ruby, int span, bool vertical, Color color,
                                    bool serif) {}
void GraphicalDisplay::beginContentEdgeCapture() {}
void GraphicalDisplay::endContentEdgeCapture(int side) {}
void GraphicalDisplay::renderContentEdge() {}
void GraphicalDisplay::setContentImages(const std::vector<ContentImage> &images) {}
void GraphicalDisplay::setImageProvider(ImageProvider provider) {}
SDL_Texture *GraphicalDisplay::contentImageTexture(const std::string &key, int *width, int *height) { return {}; }
void GraphicalDisplay::renderContentImages(const std::vector<ContentImage> &images) {}
bool GraphicalDisplay::modeLineVisible() const { return {}; }
void GraphicalDisplay::setModeLineVisible(bool visible) {}
void GraphicalDisplay::open() {}
void GraphicalDisplay::close() {}
bool GraphicalDisplay::pumpEvents() { return {}; }
bool GraphicalDisplay::consumeNeedsRedraw() { return {}; }
std::optional<GraphicalDisplay::MouseClick> GraphicalDisplay::consumeMouseClick() { return {}; }
std::optional<GraphicalDisplay::HoverEvent> GraphicalDisplay::consumeHoverEvent() { return {}; }
std::vector<std::string> GraphicalDisplay::consumeDroppedFiles() { return {}; }
std::optional<GraphicalDisplay::KeyEvent> GraphicalDisplay::consumeKeyEvent() { return {}; }
std::optional<GraphicalDisplay::DragEvent> GraphicalDisplay::consumeDragEvent() { return {}; }
std::optional<GraphicalDisplay::WheelEvent> GraphicalDisplay::consumeWheelEvent() { return {}; }
std::optional<GraphicalDisplay::ScrollbarEvent> GraphicalDisplay::consumeScrollbarEvent() { return {}; }
int GraphicalDisplay::cellWidthPixels() const { return {}; }
int GraphicalDisplay::cellHeightPixels() const { return {}; }
int GraphicalDisplay::marginYPixels() const { return {}; }
void GraphicalDisplay::setColors(Color foreground, Color background) {}
GraphicalDisplay::Color GraphicalDisplay::foregroundColor() const { return {}; }
GraphicalDisplay::Color GraphicalDisplay::backgroundColor() const { return {}; }
int GraphicalDisplay::fontPointSize() const { return {}; }
void GraphicalDisplay::setWindowPosition(int x, int y) {}
void GraphicalDisplay::outerFrame(int &x, int &y, int &width, int &height) const {}
bool GraphicalDisplay::usableBoundsContaining(int x, int y, int &left, int &top, int &right, int &bottom) { return {}; }
void GraphicalDisplay::moveWindowBy(int dx, int dy) {}
void GraphicalDisplay::setWindowSize(int width, int height) {}
void GraphicalDisplay::matchWindowSize(int outerWidth, int outerHeight) {}
void GraphicalDisplay::setVisible(bool visible) {}
void GraphicalDisplay::raise() {}
void GraphicalDisplay::setHandCursor(bool active) {}
void GraphicalDisplay::setPointingCursor(bool active) {}
void GraphicalDisplay::renderTextRun(int y, const std::string &line, Color fg, int startCol,
                                      const std::vector<Display::AttributeRun> &attributes) {}
int GraphicalDisplay::textRunDisplayWidth(const std::string &text) const { return {}; }
int GraphicalDisplay::scrollbarStripLeft() const { return {}; }
void GraphicalDisplay::drawScrollbar(double effectiveTop, int totalLines, int contentRows, int trackRows) {}
void GraphicalDisplay::updateScrollPosition(int topIndex, int totalLines, int contentRows, int trackRows) {}
void GraphicalDisplay::drawIdleScrollbarImage() {}
void GraphicalDisplay::renderScrolled(const std::vector<Display::ScrolledLine> &lines, int topIndex, int pixelOffsetY,
                                       const std::vector<Display::ModeLineSegment> &modeLineSegments) {}

}  // namespace fm
