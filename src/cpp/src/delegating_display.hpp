#pragma once

#include <string>
#include <vector>

#include "display.hpp"

namespace fm {

// それ自体は何もせず、その時点で指している「本物の」バックエンドへ
// 全ての呼び出しを転送するだけのDisplay――存在意義は純粋に、^Ogの
// 「セッション途中でTerminalDisplayとGraphicalDisplayをシームレスに
// 切り替える」機能(docs/ideas/IDEAS.mdとApplication自身のクラス
// コメント参照)のために、Application・全`*Mode`クラス・ContentView・
// LineEditorをそれぞれ新しいDisplay参照に対して再構築する必要を
// 無くすため: それらは全て、起動時にただ1つのDelegatingDisplay
// (app/main.cppが所有)に対して一度だけ構築され、どちらの本物の
// バックエンドがアクティブかを切り替える操作は、このオブジェクト
// 1つに対するsetBackend()呼び出し1回だけで済み、既に`Display &`を
// 保持している全ての側からは見えない。
//
// これは何かの逐語的な移植ではない(legacyのusilicにはこの種の間接層は
// 存在しないし必要も無い、そもそもDisplayの実装が複数存在したことが
// 無いため)。代替案――GUIモードが切り替わるたびにApplicationオブジェ
// クト全体(とその所有する全ContentView)を新しいDisplayに対して
// 解体・再構築する――だと、切り替えのたびに各モードのメモリ上の状態
// (現在のディレクトリ一覧、開いているファイルのカーソル位置、
// 稼働中のShellセッションのpty、等)を失うか、苦労して糸を通し直す
// 必要が生じる。この方式なら、そうした状態は一切移動する必要が無い。
//
// 意図的に除外: このクラスは「どの」バックエンドがアクティブで
// あるべきか、いつ切り替えるべきかについて一切意見を持たない――
// それは完全にapp/main.cppの仕事(^Ogの実行ループ処理についての
// そちら自身のコメント参照)。
class DelegatingDisplay : public Display {
 public:
  // backendは、このオブジェクトより長生きしなければならない、あるいは
  // 後のsetBackend()がそれを置き換えるまでは――DelegatingDisplayは
  // backendを自分では決して所有/破棄しない(最初のものも所有しない
  // のと同じ)。
  explicit DelegatingDisplay(Display &backend);

  void setBackend(Display &backend);
  Display &backend() const;

  void write(const std::string &text) override;
  void putChar(int c) override;
  void flush() override;

  void moveCursor(int row, int col) override;
  void cursorLeft() override;
  void saveCursor() override;
  void restoreCursor() override;
  void setVisibleCursor(int row, int col) override;
  void hideVisibleCursor() override;

  void clearScreen() override;
  void clearToEndOfLine() override;
  void scrollForward() override;
  void scrollReverse() override;

  void setAttribute(Attribute attr) override;
  std::string attributeEscape(Attribute attr) const override;
  void setColor(int fg, int bg) override;

  int rows() const override;
  int columns() const override;
  void setSize(int rows, int columns) override;

  // Display::isGraphical()自身のコメント参照――backend()(現在の実際の
  // バックエンド)へ転送するので、^Ogによる切り替えを挟んでも常に
  // 最新の値を返す。
  bool isGraphical() const override { return backend().isGraphical(); }

  std::string keySequence(Key key) const override;

  bool outputEnabled() const override;
  void setOutputEnabled(bool enabled) override;

  bool smoothScrollEnabled() const override;
  void setSmoothScrollEnabled(bool enabled) override;

  bool modeLineVisible() const override;
  void setModeLineVisible(bool visible) override;

  bool hasNativeScrollbar() const override;

  void open() override;
  void close() override;

  void leaveForExternalProgram() override;
  void reenterFromExternalProgram() override;
  void leaveForSuspend() override;
  void setVerticalTextHint(bool vertical) override;
  void setSerifSuppressed(bool suppressed) override;
  void setSerifEverywhereProvider(std::function<bool()> provider) override;
  void beginContentEdgeCapture() override;
  bool drawsRuby() const override;
  void setPendingRuby(const std::string &reading, int span, bool vertical) override;
  void endContentEdgeCapture(int side) override;
  void setContentImages(const std::vector<ContentImage> &images) override;
  void setImageProvider(ImageProvider provider) override;
  int cellPixelWidth() const override;
  int cellPixelHeight() const override;

  void renderScrolled(const std::vector<ScrolledLine> &lines, int topIndex, int pixelOffsetY,
                       const std::vector<ModeLineSegment> &modeLineSegments) override;

 private:
  bool serifSuppressed_ = false;  // setSerifSuppressed()参照
  std::function<bool()> serifEverywhereProvider_;  // setSerifEverywhereProvider()参照
  Display *backend_;
  ImageProvider imageProvider_;  // setImageProvider()(裏の画面を切り替えても渡し直す、setBackend()参照)
};

}  // namespace fm
