// 2026-10-21、直接のユーザー依頼("日本語モードでは「Reading...」を含め全メッセージを日本語化、は可能？"、範囲は"モード名と
// TOP/END等は英語のまま"で"よろしく"): mode-lineに出すメッセージ(Application::showTransientNotice()等)の日本語訳。
// 英語の文がまるごと一致すればその訳、無ければ"{}"(名前・数等が入る所)を含む型で訳す。どれにも当たらなければそのまま。
#include "messages_ja.hpp"

#include <string>
#include <utility>
#include <vector>

namespace fm {

namespace {

// まるごと一致する文。
const std::vector<std::pair<std::string, std::string>> &exactMessages() {
  static const std::vector<std::pair<std::string, std::string>> kMessages = {
      {"-g not available over ssh", "ssh 越しでは -g は使えません"},
      {"^G to return from note", "^G で注釈から戻ります"},
      {"Add it to PROGRAM in usilicrc (^Js restarts SHELL)", "usilicrc の PROGRAM に加えてください（^Js で SHELL を再起動）"},
      {"Already defining kbd macro", "既にキーボードマクロを記録しています"},
      {"Already in GUI", "既に GUI です"},
      {"Already in HELP", "既に HELP です"},
      {"Already in KEY", "既に KEY です"},
      {"Can't set bookmark here", "ここにはブックマークを置けません"},
      {"Can't set register here", "ここではレジスタに記録できません"},
      {"Cannot open a terminal window", "端末のウィンドウを開けません"},
      {"Cannot use this font", "このフォントは使えません"},
      {"COPY mode only", "COPY モードだけで使えます"},
      {"CP/MV/RM: not supported with cmd.exe", "CP/MV/RM: cmd.exe では使えません"},
      {"Defining kbd macro...", "キーボードマクロを記録しています..."},
      {"Ends with the parent", "親と一緒に終了します"},
      {"Font list: -g only", "フォントの一覧: -g だけで使えます"},
      {"Keyboard macro defined", "キーボードマクロを記録しました"},
      {"LETTER-SPACING: -g only", "LETTER-SPACING: -g だけで使えます"},
      {"LINE-HEIGHT: -g only", "LINE-HEIGHT: -g だけで使えます"},
      {"Mark set", "マークを置きました"},
      {"No bookmarks", "ブックマークがありません"},
      {"No chapter", "章がありません"},
      {"No command to repeat", "繰り返すコマンドがありません"},
      {"No Epub in URL (usilicrc)", "usilicrc の URL に Epub がありません"},
      {"No fonts (fc-list)", "フォントが見つかりません（fc-list）"},
      {"No GUI available", "GUI が使えません"},
      {"No mark set", "マークがありません"},
      {"No marks", "記録がありません"},
      {"No Open in URL (usilicrc)", "usilicrc の URL に Open がありません"},
      {"No other windows", "ほかのウィンドウがありません"},
      {"No string", "見つかりません"},
      {"Not found: ", "見つかりません: "},
      {"Not available in pager", "ページャーでは使えません"},
      {"Not defining kbd macro", "キーボードマクロを記録していません"},
      {"Not enough buffer", "バッファが足りません"},
      {"Not found in HELP", "HELP に見つかりません"},
      {"KEY needs groff 1.20 or later", "KEY には groff 1.20 以降が必要です"},
      {"Open only", "Open だけです"},
      {"Opened in a new window", "新しいウィンドウで開きました"},
      {"Parent not reachable", "親に届きません"},
      {"Permission denied", "許可がありません"},
      {"Plain text only", "平文だけで使えます"},
      {"Scroll: chapter", "スクロール: 章ごと"},
      {"Scroll: continuous", "スクロール: 連続"},
      {"SHELL busy (the previous command has not finished)", "SHELL は使用中です（前のコマンドが終わっていません）"},
      {"SHELL/prompt only", "SHELL・プロンプトだけで使えます"},
      {"Type e to repeat macro", "e でマクロを繰り返します"},
      {"Wheel: normal", "ホイール: 順"},
      {"Wheel: reversed", "ホイール: 逆"},
      {"Window not reachable", "ウィンドウに届きません"},
      {"Serif", "明朝体"},
      {"^R: only in ^S (FGREP)", "^R は ^S の FGREP でだけ使えます"},
      {"Sans-serif", "ゴシック体"},
      {"Serif (-g only)", "明朝体（-g だけ）"},
      {"Sans-serif (-g only)", "ゴシック体（-g だけ）"},
      {"Jump to register: ", "レジスタの位置へ: "},
      {"Point to register: ", "レジスタへ記録: "},
      // drawPromptLabel()の静的なメッセージ・問い。
      {"Reading...", "読み込み中..."},
      {"Disk usage...", "集計中..."},
      {"Downloading...", "ダウンロード中..."},
      {"Loading archive...", "アーカイブを読み込み中..."},
      {"Making PDF...", "PDF を作成中..."},
      {"Record?(y/n)", "記録しますか？ (y/n)"},
      {"Exit? (y/n) ", "終了しますか？ (y/n) "},
      {"Delete? (y/n) ", "削除しますか？ (y/n) "},
      {"Destination? ", "転送先？"},
      {"Key: ", "キー: "},
      {"Set: ", "置く: "},
      {"Jump: ", "移動: "},
      {"Extension: ", "拡張子: "},
      {"Regexp: ", "正規表現: "},
      {"No String", "見つかりません"},
  };
  return kMessages;
}

// "{}"を含む型(上から順に試す、より長い・細かい型を先に)。
const std::vector<std::pair<std::string, std::string>> &patternMessages() {
  static const std::vector<std::pair<std::string, std::string>> kPatterns = {
      {"LINE-HEIGHT (vertical): {} dots", "LINE-HEIGHT（縦書き）: {} ドット"},
      {"LINE-HEIGHT: {} dots ({})", "LINE-HEIGHT: {} ドット（{}）"},
      {"LETTER-SPACING (vertical): {} dots ({})", "LETTER-SPACING（縦書き）: {} ドット（{}）"},
      {"LETTER-SPACING: {} dots ({} dots per column)", "LETTER-SPACING: {} ドット（1 桁 {} ドット）"},
      {"HELP not found: {} (make install)", "HELP が見つかりません: {}（make install）"},
      {"Cannot write {} to usilicrc", "usilicrc に {} を書き込めません"},
      {"Bookmark \"{}\" set", "ブックマーク「{}」を置きました"},
      {"Bookmark \"{}\" deleted", "ブックマーク「{}」を削除しました"},
      {"Register {} set", "レジスタ {} に記録しました"},
      {"Register {} is empty", "レジスタ {} は空です"},
      {"{} files marked", "{} 個のファイルを選択しました"},
      {"{} file marked", "{} 個のファイルを選択しました"},
      {"{}: command not found", "{}: コマンドが見つかりません"},
      {"No manual entry for {}", "{} のマニュアルページはありません"},
      {"Can't open {}", "{} を開けません"},
      {"Cannot get: {}", "取得できません: {}"},
      {"Cannot run: {}", "実行できません: {}"},
      {"Cannot write: {}", "書き込めません: {}"},
      {"Cannot write {}", "{} に書き込めません"},
      {"Copied: {}", "コピーしました: {}"},
      {"No books: {}", "本がありません: {}"},
      {"No such EPUB: {}", "EPUB がありません: {}"},
      {"No such file or directory: {}", "ファイルまたはディレクトリがありません: {}"},
      {"Not a book: {}", "本ではありません: {}"},
      {"Not a Narou work: {}", "なろうの作品ではありません: {}"},
      {"Recorded: {}", "記録しました: {}"},
      {"Saved: {}", "保存しました: {}"},
  };
  return kPatterns;
}

// `pattern`を"{}"で区切った固定部分。
std::vector<std::string> splitPattern(const std::string &pattern) {
  std::vector<std::string> parts;
  std::size_t start = 0;
  for (std::size_t at; (at = pattern.find("{}", start)) != std::string::npos; start = at + 2)
    parts.push_back(pattern.substr(start, at - start));
  parts.push_back(pattern.substr(start));
  return parts;
}

// `text`が`pattern`の型に合えば、"{}"に当たる部分を順に`values`へ入れてtrue。
bool matchPattern(const std::string &text, const std::string &pattern, std::vector<std::string> &values) {
  std::vector<std::string> parts = splitPattern(pattern);
  values.clear();
  if (text.compare(0, parts.front().size(), parts.front()) != 0) return false;
  std::size_t pos = parts.front().size();
  for (std::size_t i = 1; i < parts.size(); i++) {
    const std::string &literal = parts[i];
    std::size_t at;
    if (i + 1 == parts.size()) {  // 最後の固定部分は末尾に
      if (text.size() < pos + literal.size() || text.compare(text.size() - literal.size(), literal.size(), literal) != 0)
        return false;
      at = text.size() - literal.size();
    } else {
      at = text.find(literal, pos);
      if (at == std::string::npos) return false;
    }
    if (at == pos) return false;  // "{}"は空でない
    values.push_back(text.substr(pos, at - pos));
    pos = at + literal.size();
  }
  return pos == text.size();
}

}  // namespace

namespace {
bool gMessagesJapanese = false;
}  // namespace

void setMessagesJapanese(bool japanese) { gMessagesJapanese = japanese; }

std::string localizeMessage(const std::string &english) {
  return gMessagesJapanese ? localizeMessageJa(english) : english;
}

std::string localizeMessageJa(const std::string &english) {
  for (const auto &[en, ja] : exactMessages())
    if (english == en) return ja;
  std::vector<std::string> values;
  for (const auto &[en, ja] : patternMessages()) {
    if (!matchPattern(english, en, values)) continue;
    std::vector<std::string> parts = splitPattern(ja);
    std::string out = parts.front();
    for (std::size_t i = 1; i < parts.size(); i++) out += (i - 1 < values.size() ? values[i - 1] : "") + parts[i];
    return out;
  }
  return english;
}

}  // namespace fm
