#include "man_mode.hpp"

#include "child_process.hpp"
#include "pipe_buffer.hpp"

namespace fm {

ManMode::ManMode(ContentView &view, History &history, const Configuration &configuration,
                  Charset::Encoding encoding, int width)
    : view_(view), history_(history), configuration_(configuration), encoding_(encoding), width_(width) {
  // 理由はFileMode自身の全く同じ呼び出し参照: 素のページ内容は実際の
  // CUI(put_str()自身のf_markロジック)では行全体の反転表示カーソル
  // ハイライトを一切受けない――実際のユーザー報告(「選べないのに
  // 行がハイライト表示」、2026-08-15)により、ManModeにはFileMode
  // とは違ってこの呼び出しが一度も与えられていなかったことが判明し、
  // manページ内をカーソルが動くたびにContentView自身の既定(true)の
  // ハイライトバーが表示され続けていた。そこにはDirectory/LOCATEの
  // 項目のように実際に「選択可能」なものは何も無いにもかかわらず。
  view_.setCursorHighlightEnabled(false);
}

std::optional<std::vector<ContentView::Line>> ManMode::runAndWrap(const std::string &name) const {
  ProgramCommand command = configuration_.commandFor(ConfiguredRole::Man);
  if (command.argv.empty()) return std::nullopt;

  std::vector<std::string> argv = command.argv;
  argv.push_back(name);
  ChildProcess::Result result = ChildProcess::run(argv);
  if (!result.ok) return std::nullopt;

  PipeBuffer pipe(encoding_, width_, 16L * 1024 * 1024);
  pipe.feed(result.output);
  pipe.flush();

  std::vector<ContentView::Line> lines;
  lines.reserve(pipe.lines().size());
  // styled.attributes(2026-08-15、FileMode::readAndWrap()自身の
  // 2026-08-05の修正に合わせている――そちら自身のコメント参照):
  // PipeBufferのnroffオーバーストライク太字/下線検出は既にこれを
  // 生成している。FileModeがこの修正を受けた時、ManModeは同じ修正を
  // 一度も取り込んでいなかったため、`man`自身の太字の節見出しや
  // 下線付きの相互参照が素のテキストとして表示されていた(実際の
  // ユーザー報告+本物のターミナル自身の`man`表示との比較スクリーン
  // ショット、「文字装飾が無い」)。
  for (const auto &styled : pipe.lines()) lines.push_back({styled.text, std::nullopt, std::nullopt, styled.attributes});
  return lines;
}

bool ManMode::loadPage(const std::string &name) {
  if (name.empty()) return false;
  auto lines = runAndWrap(name);
  if (!lines) return false;
  view_.setLines(std::move(*lines));

  currentPage_ = name;
  if (auto pos = history_.positionFor(PositionCategory::ManPage, currentPage_)) {
    int idx = pos->row;
    int last = static_cast<int>(view_.lines().size()) - 1;
    if (idx < 0) idx = 0;
    if (idx > last) idx = (last < 0) ? 0 : last;
    view_.setCursorIndex(idx);
  } else {
    view_.setCursorIndex(0);
  }
  view_.redraw();
  return true;
}

bool ManMode::reflow(int newWidth) {
  if (currentPage_.empty()) return false;
  int savedWidth = width_;
  width_ = newWidth;
  auto lines = runAndWrap(currentPage_);
  if (!lines) {
    width_ = savedWidth;  // `man`の再実行に失敗――現在の表示には触れない
    return false;
  }
  int oldCursor = view_.cursorIndex();
  view_.setLines(std::move(*lines));
  int last = static_cast<int>(view_.lines().size()) - 1;
  if (oldCursor < 0) oldCursor = 0;
  if (oldCursor > last) oldCursor = (last < 0) ? 0 : last;
  view_.setCursorIndex(oldCursor);
  view_.redraw();
  return true;
}

const std::string &ManMode::currentPage() const { return currentPage_; }

void ManMode::setEncoding(Charset::Encoding encoding) { encoding_ = encoding; }

void ManMode::saveCursorPosition() {
  if (currentPage_.empty()) return;
  History::CursorPosition pos;
  pos.row = view_.cursorIndex();
  pos.totalRows = static_cast<int>(view_.lines().size());
  pos.top = 0;
  pos.lineNumber = -1;
  history_.setPosition(PositionCategory::ManPage, currentPage_, pos);
}

}  // namespace fm
