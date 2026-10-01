#include "application.hpp"

#include "epub_vertical.hpp"

#include <poll.h>

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <tuple>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <termios.h>
#include <unistd.h>

#include <cstring>

#include "child_process.hpp"
#include "help_text.hpp"
#include "mode_line.hpp"
#include "pipe_buffer.hpp"
#include "utf8.hpp"

namespace fm {

namespace {
constexpr int kLeaderJ = 0x0a;         // ^J
constexpr int kLeaderX = 0x18;         // ^X
constexpr int kLeaderO = 0x0f;         // ^O
constexpr int kEscape = 0x1b;          // ESC
constexpr int kControlS = 0x13;        // ^S
constexpr int kControlC = 0x03;        // ^C
constexpr int kControlG = 0x07;        // ^G
constexpr int kControlQ = 0x11;        // ^Q
constexpr int kControlUnderscore = 0x1f;  // ^_ (C-_、undo)
constexpr int kControlW = 0x17;        // ^W
constexpr int kCarriageReturn = 0x0d;  // ^M
// 末尾のディレクトリの切り詰め(2026-08-12修正): drawPromptLabel()
// (アクティブに編集中のモードライン)とlistBrowserView_自身の
// ModeLineFieldsProviderの直接入力の分岐(^J^S/^J^Cの候補リストを
// 「閲覧している」モードライン、編集が始まる前)の両方で共有される
// ――実際の報告+参照スクリーンショット(=aa)により、後者は依然として
// 宛先ディレクトリを全長、切り詰めずに表示していたため、候補プレビュー
// テキスト自身の利用可能な幅が、ディレクトリパスがたまたまどれだけ
// 長いかによって伸び縮みしていたことが判明した("戻り値が「...〜」に
// なっておらず、文字列の長さによって入力エリアが伸縮")。drawPromptLabel()
// が既に確立している、まさに同じ「...<末尾16文字>」というルールを、
// 「2つ目の」呼び出し箇所が必要としたので、(複製するのではなく)ここに
// 括り出した。
constexpr int kTrailingPathTailChars = 16;
std::string truncatedTrailingPath(const std::string &path) {
  if (static_cast<int>(path.size()) <= kTrailingPathTailChars) return path;
  return "..." + path.substr(path.size() - kTrailingPathTailChars);
}

// 2026-09-18追加、HELP.txt 2.13章「LIST」の実装(old/src/key.c::
// make_list()、`nc[] = "/<>()|; "`+先頭'.'の除外)。SHELL/PROGRAM/MAN
// プロンプトで、入力バッファ「全体」(カーソル位置の単語ではなく)に
// これらの区切り文字が一つも無く、かつ先頭が'.'でなければ、まだ
// コマンド名自体を入力中とみなして$PATH検索の対象にする――区切り
// 文字(スペースを含む)が一つでも現れた時点でこの判定は偽になり、
// 呼び出し元はカレントディレクトリのファイル名補完にフォールバック
// する。shell.cpp自身にも同じ内容を重複定義してある(2つの別個の
// LineEditorインスタンス=shell_.commandLine()とpromptEditor_をそれぞれ
// 別のTUから扱うため、共有ヘルパーを吊るす共通の所有者が無い――
// truncatedTrailingPath()のarchive_mode.cpp/application.cpp間の重複と
// 同じ理由)。
bool shouldSearchPath(const std::string &text) {
  // 2026-09-18訂正、実機報告(スクリーンショット=gu: "usilic .^M→
  // ^[s^I^I→不良...カレントディレクトリ一覧ではなくプログラム一覧が
  // 正解")を受けての修正: 空のバッファ(何もタイプしていない状態での
  // ^I^I)を無条件にfalse(=カレントディレクトリ検索)扱いしていたのが
  // バグだった――legacyのold/src/key.c::make_list()自身の実際の分岐
  // (`if (*buff != '.') { ...ncを含まなければf_prog=1... } else
  // f_prog=0;`)を素の空文字列(`*buff`はNUL、'.'ではない)に通すと、
  // 続くループは1回も回らずに即座に終わり`c`は0のまま、
  // `f_prog = !c`は1(true)になる――つまり空のバッファは「まだ
  // コマンド名を1文字も入力していない」の極端な場合に過ぎず、legacy
  // 自身はこれも$PATH検索対象に含めている。空文字列を先頭'.'と誤って
  // 同一視していたのが今回の誤り。
  if (!text.empty() && text.front() == '.') return false;
  return text.find_first_of("/<>()|; ") == std::string::npos;
}

// 2026-09-14追加、直接のユーザー依頼("`4K .../.husky/pre-commit`を
// 選択したときはDU: .../.husky/でないと"): DUの一覧中のあるエントリ
// が「実際に属しているフォルダー」を求める――ディレクトリのエントリ
// ならそれ自身、ファイルのエントリならその親ディレクトリ(末尾に
// スラッシュを1本補う)。duView_自身のModeLineFieldsProvider
// (今どのフォルダーを見ているかの表示)とopenSelectedDuEntry()の
// 両方(そのエントリをDirectory/Fileで開いた後、Backでの戻り先を
// 示す注釈)が、同じ「エントリ自身の居場所」という概念を必要とする
// ため、ここに1箇所へ括り出した。
std::string duEntryFolder(const DuEntry &entry) {
  std::string folder =
      entry.isDirectory ? entry.path : std::filesystem::path(entry.path).parent_path().string();
  if (!folder.empty() && folder.back() != '/') folder += '/';
  return folder;
}

// 2026-09-05追加、直接のユーザー依頼("FGREP/EGREPの代わりにgrep -F/
// grep -Eで代用の上、新たにGREP(grep)を新設"): GrepSyntax::Basic
// (プレーンなgrep、正規表現は基本正規表現BRE)の追加に伴い、これまで
// 各呼び出し箇所に個別に散っていた「FGREP/EGREPのラベル/ロールの
// 2択」の三項演算子(GrepSyntax::Fixed ? "FGREP" : "EGREP"等)を、
// GrepSyntax::Basicも扱える1箇所の判定へ括り出す――CutMode自身の
// cutSyntax_はこのGrepSyntax::Basic拡張の対象では「ない」(ユーザー
// 自身の依頼はGREPモードだけを指しており、CUTのFGREP/EGREP方式は
// 元のまま)ので、cutSyntax_を読む個別の呼び出し箇所はここを使わず
// 従来の三項演算子のままにしてある。
// 2026-09-18追記(Command::StartIncrementalGrep自身のコメント参照、
// "GREPとFGREPを無くしたい"): GREP(Basic)自体は本物のGREP検索機能としては
// 廃止され、grepSyntax_が実際にBasicになることはもう無い――以下の
// switchにBasicのcaseを残しているのは、cutSyntax_同様GrepSyntaxの
// 3値enum自体は変えずに残す設計判断のため(型としては引き続き有効な
// 値であり、コンパイル時に全ケースを網羅する必要がある)。
const char *grepSyntaxLabel(GrepSyntax syntax) {
  switch (syntax) {
    case GrepSyntax::Fixed:
      return "FGREP";
    case GrepSyntax::Regex:
      return "EGREP";
    case GrepSyntax::Basic:
      return "GREP";
  }
  return "GREP";
}

// 2026-09-12追加、直接のユーザー依頼("GREP/FGREP/EGREPの違いを明示する
// ために...(fgrep)/(egrep)表示のため"): grepSyntaxLabel()と対になる、
// trailing2欄のプログラムバッジ版。AskUserQuestionで「バッジのみ変更」
// (実際にfgrep/egrepバイナリへは切り替えない――ユーザーのPATH次第で
// grepの実装(ugrep/BSD grep等)自体が暗黙に変わってしまうリスクを
// 避けるため)を選択したので、実際の検索プログラム自体は
// GrepMode::search()側で引き続きgrep -F/-E/(無指定)のまま――ここでは
// 表示文字列だけをgrepSyntax_から導出する。
const char *grepSyntaxBadge(GrepSyntax syntax) {
  switch (syntax) {
    case GrepSyntax::Fixed:
      return "(fgrep)";
    case GrepSyntax::Regex:
      return "(egrep)";
    case GrepSyntax::Basic:
      return "(grep)";
  }
  return "(grep)";
}

// Application::handleCommand()冒頭のin-place GREP自動終了ガード自身の
// コメント参照(2026-09-10追加、直接のユーザー指摘): 明確な「モード
// 切り替え」に当たらない、素のカーソル移動/ページ送り/画面再描画/
// 無認識バイト(Command::None)の間はセッションを継続する――
// GrepInPlaceNext/Previous・マーク系(ToggleMark/MarkAndAdvance/
// MarkAndRetreat)自身は既存のセッション継続の核なので当然含む。
bool isGrepInPlaceSafeCommand(Command command) {
  switch (command) {
    case Command::None:
    case Command::GrepInPlaceNext:
    case Command::GrepInPlacePrevious:
    case Command::ToggleMark:
    case Command::MarkAndAdvance:
    case Command::MarkAndRetreat:
    case Command::MoveUp:
    case Command::MoveDown:
    case Command::PageUp:
    case Command::PageDown:
    case Command::ScrollLineUp:
    case Command::ScrollLineDown:
    case Command::GoToTop:
    case Command::GoToBottom:
    case Command::ExchangeJumpMark:
    case Command::Redraw:
      return true;
    default:
      return false;
  }
}
ConfiguredRole grepSyntaxRole(GrepSyntax syntax) {
  switch (syntax) {
    case GrepSyntax::Fixed:
      return ConfiguredRole::Fgrep;
    case GrepSyntax::Regex:
      return ConfiguredRole::Egrep;
    case GrepSyntax::Basic:
      return ConfiguredRole::Grep;
  }
  return ConfiguredRole::Grep;
}
// GrepSyntax::Basicの検索パターン履歴は、cutSyntax_同様RecentFgrepPattern/
// RecentEgrepPatternの2択のringだけで表現する(RecentGrepPattern用の
// 新しいRingCategoryは追加しない)――grep(BRE)とegrep(ERE)はどちらも
// 「固定文字列ではなく正規表現」という点でFGREPとは性質が異なり、
// パターンの再利用という観点では両者を同じ「正規表現の履歴」として
// 扱うのが自然なため、GrepSyntax::Basicのパターンも既存の
// RecentEgrepPatternを共有する。
RingCategory grepSyntaxRingCategory(GrepSyntax syntax) {
  return syntax == GrepSyntax::Fixed ? RingCategory::RecentFgrepPattern : RingCategory::RecentEgrepPattern;
}

// 標準的なPOSIXのシングルクォートによるシェルエスケープ: '...'で
// 包み、埋め込まれたリテラルのクォートはそれぞれ'\''(クォートを
// 閉じ、エスケープされたクォート、クォートを再度開く)に置き換える
// ――execvp()へargvをそのまま渡し、クォートが一切必要無いCpMvRmMode
// 自身のbuildArgv()とは異なり、shell_.commandLine()向けの本物の
// コマンド「行」を組み立てるためにexecuteCopy()/executeMove()/
// executeRemove()(2026-08-22の再設計、それら自身のコメント参照)が
// 使う。項目13自身の「半角スペースを含むファイル名」の経緯こそが、
// これが単純にargvの各要素をクォート無しでスペースで結合するわけ
// にはいかない理由である――スペース、globの特殊文字、あるいは
// クォートを含むファイル名は、そうしないと、この行を読む実際の
// シェルによって分割/誤解釈されてしまう。
std::string shellQuoteArg(const std::string &arg) {
  std::string quoted = "'";
  for (char c : arg) {
    if (c == '\'')
      quoted += "'\\''";
    else
      quoted += c;
  }
  quoted += "'";
  return quoted;
}

std::string shellQuoteArgv(const std::vector<std::string> &argv) {
  std::string line;
  for (const auto &arg : argv) {
    if (!line.empty()) line += " ";
    line += shellQuoteArg(arg);
  }
  return line;
}

// `ls -alF`(Configuration::commandFor(ConfiguredRole::Ls)自身の
// デフォルト、configuration.cpp参照)がDirectoryEntry::nameの末尾に
// 付与する型マーカー(ディレクトリ'/'、実行可能'*'、シンボリックリンク
// '@'、ソケット'='、FIFO'|')を剥がす――実在のパスとして使う時
// (PasteTransientTargets自身のコメント参照、ARC展開前後の差分判定)
// にだけ必要で、isMarked()自身のentries_[index].nameとの照合には
// 元の(型マーカー付きの)表記のまま使う。
std::string stripLsFTypeMarker(const std::string &name) {
  if (!name.empty() && std::string("/*@=|").find(name.back()) != std::string::npos) return name.substr(0, name.size() - 1);
  return name;
}

// COPYモードのconfirmMark()自身のコメント参照(2026-09-07追加、直接の
// ユーザー報告⑫: "クリップボードが共有エリアに繋がってない？（emacs
// でペーストできない）"): それまでのCOPY/PASTEは、このアプリ自身の
// promptEditor_(ヤンクバッファ)経由でしか通用しない、アプリ内だけの
// クリップボードだった――OSのシステムクリップボード(macOSの
// NSPasteboard、`pbcopy`/`pbpaste`経由でアクセス可能)には一切繋がって
// いなかったため、確定した選択をemacs等の「別の」アプリケーションへ
// 貼り付けることができなかった。ChildProcessOptions::inputを使い、
// テキストを子プロセスの標準入力へ書き込む(これは既にfork_arc()等の
// パイプライン入力で使われている、デッドロックしない専用writerプロセス
// 経由の仕組み)。macOSでは`pbcopy`、Linuxでは`xclip`/`xsel`のどちらかが
// 入っていればそれを使う(どちらも入っていなければ黙って何もしない
// ――システムクリップボード連携自体は「あれば嬉しい」機能であり、
// 無くてもアプリ内のCOPY/PASTEは今まで通り機能する)。
void copyTextToSystemClipboard(const std::string &text) {
  ChildProcessOptions opts;
  opts.input = &text;
#ifdef __APPLE__
  ChildProcess::run({"pbcopy"}, opts);
#else
  ChildProcess::Result result = ChildProcess::run({"xclip", "-selection", "clipboard"}, opts);
  if (!result.ok) ChildProcess::run({"xsel", "--clipboard", "--input"}, opts);
#endif
}

// startCpMvRmShellRun()/pollPendingCpMvRmShellRun()自身の終了ステータス
// のチャンネル: 実際のcp/mv/rmは今やshell_の実際のセッション内で
// 非同期に実行される(executeCopy()自身のコメント参照)ため、そこから
// 直接ステータスを読み取れるChildProcess::Resultが無い――これを
// 注入するコマンド行に付け加えることで、シェル自身に終了コードを
// もう1行の(本物の、スクロールバックに見える)出力行として出力させ、
// pollPendingCpMvRmShellRun()が次のプロンプトが再び現れた後にそれを
// 走査する。宛先ディレクトリへ移動するかどうかを決める前に、本物の
// 失敗(例えば不正な宛先)を成功から見分けられるようにする代償として、
// 小さく、明示された代償(ユーザー自身のSHELLの記録に1行余分に加わる)
// である。
const char kCpMvRmExitMarker[] = "usilic-cpmvrm-exit:";

// あらゆる「結果」形式のモードライン(CUTのセグメントの内容、そして
// ――2026-08-13以降、このポートの今や定まったモードラインの「形式」を
// モードごとに再導出するのではなく共有ロジックへ集約するというユーザー
// 依頼に従い――MAN/LOCATE/DU/アーカイブエントリの内容も)の`trailing`
// フィールドに表示されるバイト数。legacyのdisp_window()自身の汎用的な
// print_arg()が埋めるバッファサイズのフィールド(menu.cのメイン再描画
// ループは、disp_window()自身を呼ぶ前に、モードごとの特別扱いではなく、
// 実質全てのモードについてh.bp->sizeでprint_arg()を呼ぶ)を反映している
// ――各Modeクラス自身の既にパース済みのstd::vector<Line>と並行して
// 別個の生バイト数を追跡するのではなく、現在ロードされている各行の
// バイト長(元のバッファ内の全ての本物の行が持っていた改行分の+1を
// 含む)の単純な合計としてここで再現している、なぜなら2つは構築上
// 常に一致するため(全ての行はまさにその数のソースバイトから「来た」)。
// 元々はcutContentView_自身のプロバイダだけがこれを計算していた
// (期待される正確なモードラインのテキストによる直接のユーザーの再現
// でそこで検証済み――それ自身のコメント参照);MAN/LOCATE/DU/アーカイブ
// の内容が、4つ目/5つ目のコピーの代わりに同一の計算を必要とした時点
// で括り出した。
// 2026-08-15: 今は薄いラッパーである――実際の計算がなぜそちらへ移動
// したのかについてはContentView::totalByteCount()自身のコメント参照
// (このファイルの無名名前空間にアクセスできない別の翻訳単位である
// DirectoryMode/ArchiveModeも、それを共有できるように)。
std::size_t contentByteCount(const ContentView &view) { return view.totalByteCount(); }

// Archive内での^R自身の拡張子の許可リスト(2026-08-20追加、直接の
// ユーザーの指定: "ARC対応拡張子（.tar/.tar.gz/.tgz/.tar.bz2/.tbz2/
// .tar.xz/.txz/.7z/.zip/.rar/.lzh/.lha/.dmg/.iso/.cab/.msi）")。意図的
// に、Configuration::programForSuffix()から導出するのではなく、リテラル
// に列挙してある: このうちいくつか(.rar/.7z/.dmg/.iso/.cab/.msi)は、
// このポートにはまだ実際のArchiveKind/展開のサポートが全く無い
// (ArchiveKind自身のコメント参照)――ユーザー自身の明示的な依頼は、
// そのサポートが存在するようになる「前」でも、このフィルタリングされた
// 履歴リストがそれらを構わず表示することである(特に.7z/seven7zの
// ケース: "無い場合は「brew install seven7z」を...インストが終わり
// 次第ARCで表示")、archiveMode_.loadArchive()が現在扱わないものを
// このリストが黙って隠すことではない。複合拡張子(.tar.gz等)は、
// このポートの他の場所にあるConfiguration自身の拡張子マッチングと
// 同じ形で、リテラルな末尾として照合される。
bool hasArchiveCompatibleSuffix(const std::string &path) {
  static const std::vector<std::string> kSuffixes = {
      ".tar.gz", ".tar.bz2", ".tar.xz", ".tgz", ".tbz2", ".txz", ".tar",
      ".7z",     ".zip",     ".rar",    ".lzh", ".lha",  ".dmg", ".iso", ".cab", ".msi"};
  for (const auto &suffix : kSuffixes) {
    if (path.size() >= suffix.size() && path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0)
      return true;
  }
  return false;
}

// ^X^F(RecentFile)の候補リストにある、アーカイブと認識されたパスへの
// "(tar)"/"(zip)"等のバッジ――2026-08-20追加、直接のユーザー依頼
// ("FILE:ARCで閲覧したutf8.tarに「(tar)」を付けて欲しい（旧=dp）"):
// legacy自身のT_hfile候補リストは既にこれを表示している(=dp、参照
// スクリーンショット)が、このポートのListBrowserModeにはそれが一切
// 無かった――アーカイブであろうとなかろうと、全てのエントリが素の
// パスとしてレンダリングされていた。ArchiveMode自身のarchiveCommand_
// バッジと正確に一致する(同じConfiguration::programForSuffix()の
// 参照、同じ"(<program>)"の形)――hasArchiveCompatibleSuffix()自身の
// 静的な許可リスト(^R自身のフィルタ、意図的にまだ実際のArchiveKind
// サポートが無い拡張子も含む)とは異なり、これはarchiveMode_.
// loadArchive()が「今まさに」実際に開けるパスにのみバッジを付ける、
// バッジを表示することは「これは拡張子的にそう見える」というだけで
// なく「これはアーカイブとして開ける」ことを意味するため。
std::string archiveBadgeFor(const Configuration &configuration, const std::string &path) {
  ArchiveKind kind = ArchiveKind::None;
  auto command = configuration.programForSuffix(path, &kind);
  if (!command || kind == ArchiveKind::None || command->argv.empty()) return "";
  return "(" + command->argv.front() + ")";
}

// ListBrowserMode::show()自身の任意の第2引数向けの、並行するバッジの
// ベクター――`category`がRingCategory::RecentFile(チェックする価値の
// ある本物のファイルシステムパスであることが「あり得る」唯一の
// ring、DIR/MAN/LOCATE/FGREP/EGREP/COMMANDのエントリは決してアーカイブ
// パスにならない)でない限り空(バッジは一切無し)。
std::vector<std::string> fileHistoryBadges(const Configuration &configuration, RingCategory category,
                                            const std::vector<std::string> &items) {
  std::vector<std::string> badges;
  if (category != RingCategory::RecentFile) return badges;
  badges.reserve(items.size());
  // 2026-09-29: .epubは開き方で分ける――EPUBモードの「パス」は"(epub)"、ARCで開いた
  // 「パス/」は"(unzip)"(Application::openEpubAsArchive()参照)。
  for (const auto &item : items) {
    bool epubArchive = item.size() > 1 && item.back() == '/' && isEpubFile(item.substr(0, item.size() - 1));
    if (epubArchive) badges.push_back(archiveBadgeFor(configuration, item.substr(0, item.size() - 1)));
    else if (isEpubFile(item)) badges.push_back("(epub)");
    else badges.push_back(archiveBadgeFor(configuration, item));
  }
  return badges;
}

// Command::JumpToPathPrefix自身の切り詰めの算術――legacyのm_dir()の
// デフォルトケース(`menu.c`)から1対1で移植: `for (i = m - T_0 + 1,
// cp = dir; i && (cp = index(cp, '/')); i--, cp++) ; if (cp) *cp =
// '\0';`。「先頭の」'/'自体を最初のマッチとして数えることが、数字0が
// 空文字列ではなく"/"に着地する理由である(実際のlegacyバイナリに
// 対してPTYで検証済み: それぞれネストしたcwdから、^J0/^J1/^J2で
// "/"/"/Users/"/"/Users/haa15030/"へchdirする)。`path`が`digit+1`より
// 少ないスラッシュしか持たない場合、legacy自身の`cp`はNULLになり、
// `dir`は完全に「変更されないまま」残る(切り詰められもせず、空にも
// されない)――ここでは`path`をそのまま返すことでそれを反映している;
// 呼び出し元自身のenterDirectory()呼び出しは、それが結局何であれ、
// 他のどんな到達不能なパスに対してもそうするのとまさに同じように、
// 自然にno-opになるか失敗する。
std::string truncateToPathPrefix(const std::string &path, int digit) {
  int remaining = digit + 1;
  std::size_t pos = 0;
  while (remaining > 0) {
    std::size_t slash = path.find('/', pos);
    if (slash == std::string::npos) return path;
    pos = slash + 1;
    remaining--;
  }
  return path.substr(0, pos);
}

// 2026-09-20、直接のユーザー依頼("今のKEYをHELPに置き換え...正味の
// ：カットによるHELPとしてほしい")によりHELP自身が全面的に作り直され
// たのに伴い削除された、旧HELPの番号付き見出し正規表現("^(  |　)[0-9]
// +\\."、GrepSyntax::Regex)は、この関数の以前のコメント(git履歴参照)
// が詳しく説明していた通りlegacy自身のHELP.jpの構造(章番号+ピリオド)
// に合わせたものだった。当初はこれを全角「：」の固定文字列検索
// (GrepSyntax::Fixed)へ切り替えたが、QUICK.txt自身が「key：meaning」
// 形式の通常の説明文(NOTATION章等)でも「：」を大量に使っていたため、
// 見出しでも何でもない行までことごとくCUTの1エントリにされてしまって
// いた。
//
// 2026-09-15修正、直接のユーザー指摘("今のHELPでは...全角「：」で
// CUTしていると思うんだが、それをタブコードによるCUTに変更してほしい"):
// パターンを本物の見出し行「だけ」に確実に絞り込むため、タブ文字
// 単体へ切り替えた。QUICK.txt自身は本当にCUTの1エントリにしたい行
// (80行、grep -Pc '\t$'で数えられる)の末尾にだけ既にタブを1つ追加
// 済みで、それ以外(NOTATION章の「key：meaning」等)にはタブが無い。
// 生のタブバイトはPipeBuffer::wrapLine()がU+2060(WORD JOINER)+
// スペースへ展開してしまう(detectOverstrike_有効時、そのものの
// コメント参照)ため、この見た目上の「：」はそのまま行の途中に残り
// つつ、実際の分割点は行末の(不可視の)U+2060になる――CutMode::
// split()自身のconvertTabsForSearch()が、渡された生のタブ1文字を
// 同じU+2060へ変換してから検索するので、この定数はそのまま生の
// '\t'でよい(^Q^Iで打ち込む本物のCUT検索と全く同じ変換経路を共有)。
const char *const kHelpPattern = "\t";

// tempRootDirectory_自身のコメント(application.hpp)参照: 明示的な
// `tempRootDirectory`(main()自身の/tmp/.usilic.<pid>、引数付きで
// 起動された実行)が優先される;引数無しの素の実行はここに""を渡し、
// 2026-08-18より前にこのクラスの他の「全ての」経路が既に専ら使って
// いたのと同じ`<homeDirectory>/.usilic`にフォールバックする。
std::string resolveTempRoot(const std::string &homeDirectory, const std::string &tempRootDirectory) {
  return tempRootDirectory.empty() ? (homeDirectory + "/.usilic") : tempRootDirectory;
}
}  // namespace

// 2026-09-20追加、openRecentFileEntry()自身のコメント参照: executeCut()
// が積む「パス "パターン"」という合成形のRecentFileエントリを検出・
// 分解する。末尾が引用符で閉じ、その直前に` "`(スペース+引用符)が
// 見つかり、両側とも空でない場合にだけ一致とみなす――ただの
// ファイルパスに偶然二重引用符が含まれるような病的なケースを
// (完璧ではないにせよ)大まかに避けるための、最も単純な形式チェック。
// application.hpp自身の宣言コメント参照(main.cppからも呼ぶため、
// 匿名namespaceではなくここ(fm本体)で定義している)。
//
// 2026-09-14拡張、直接のユーザー依頼("README改めTUTORIALとして...
// FILE履歴には真っ新起動の時点で~/.local/lib/usilic/TUTORIAL "^
// [0-9]^H[0-9]+\."が登録済み"): この事前登録エントリ自身のパターンが
// 正規表現(`^`/`[0-9]`)であり、当初の実装(GrepSyntax::Fixedに決め
// 打ち)では正しく動作しない。一時期は外側の区切り文字自体(二重引用符/
// スラッシュ)でFixed/Regexを区別していたが、QUICK.txt自身が定める
// C-j bの説明("「ファイル名 "文字列"」で表示され")通り常に二重引用符
// で括る形に戻した後、直接のユーザー依頼("CUTは一律grep仕様。Fixedは
// 廃止。")によりCUT自体がGrepSyntax::Basic一本になったため、
// Fixed/Regexの区別自体が丸ごと不要になった――もう常にBasicを返す。
std::optional<std::tuple<std::string, std::string, GrepSyntax>> parseCutFileHistoryEntry(
    const std::string &entry) {
  if (entry.size() < 4 || entry.back() != '"') return std::nullopt;
  std::size_t openPos = entry.rfind(" \"", entry.size() - 2);
  if (openPos == std::string::npos) return std::nullopt;
  std::string filePath = entry.substr(0, openPos);
  std::string pattern = entry.substr(openPos + 2, entry.size() - (openPos + 2) - 1);
  if (filePath.empty() || pattern.empty()) return std::nullopt;
  return std::make_tuple(filePath, pattern, GrepSyntax::Regex);  // 2026-09-27、CUTは拡張正規表現(startCutPrompt()の呼び出し元参照)
}

// parseCutFileHistoryEntry()自身のコメント参照――その逆変換。CUTは
// 常にGrepSyntax::Basicなので`syntax`自体はもう使わないが、
// 呼び出し側(main.cpp/application.cpp)を単純に保つため引数は残す。
std::string buildCutFileHistoryEntry(const std::string &path, const std::string &pattern, GrepSyntax) {
  return path + " \"" + pattern + "\"";
}

// application.hpp自身の宣言コメント参照: app/main.cpp(真っ新起動時の
// FILE履歴への事前登録)とこのファイル自身(下のexecuteShowUsilicBanner()/
// openSelectedBannerEntry()の「TUTORIAL」行)の両方が使う、唯一の
// 正典。
//
// 2026-09-14訂正: 当初(このコメントの前の版)はユーザー自身が指定した
// 生バイト表記("^　[0-9]^H[0-9]+\.")をそのまま実装していたが、
// openRecentFileEntry()の合成エントリ分岐は実際にはfileMode_.loadFile()
// (オーバーストライク検出が既定でON、PipeBuffer::wrapLine()の
// detectOverstrike_)経由でsourceLinesを組み立てる――src/TUTORIAL
// 自身の見出しの生バイトは実は数字・ピリオド・空白・英字を含む
// 「行全体」が"c\bc"(1文字ごとに同じ文字をバックスペース越しに
// 重ね打ち)というnroff太字表現になっており(python3で直接read()して
// repr()し確認済み、単なる先頭の見出し番号だけが特別というわけでは
// ない)、loadFile()はこの"c\bc"を1文字+Bold属性へ収縮させる――結果
// としてCutMode::split()が実際に見る文字列には\x08が一切残らない。
// 生バイト版のこのパターンは(FGREP/EGREPへの直接シェルアウトである
// CutMode::split()自身の性質上)`fileMode_.loadFileRaw()`のような
// オーバーストライク検出無しの経路でしか一致しないが、それだと今度は
// TUTORIAL全体の太字表示(本文含め全行がこの重ね打ち規約)を失って
// しまい本末転倒(K-24のRaw表示と同じ見た目になる)。そこで代わりに、
// loadFile()収縮後の「見た目通りの」文字列(全角スペース+数字+ドット
// の並び、バックスペース無し)に一致するよう改めた――太字の見た目は
// そのまま保たれる。
//
// 2026-09-14再訂正、直接のユーザー依頼("CUTは一律grep仕様。Fixedは
// 廃止。"): CUTがGrepSyntax::Basic(素のgrep、基本正規表現)一本に
// なったのに合わせ、`+`/`(...)`(拡張正規表現の記法、BREのままだと
// 文字通りにしか解釈されない)を含んでいた旧パターンを撤回。
// 「全角スペース+数字」で始まる行はTUTORIAL内の見出し以外に存在
// しない(本文が全角スペースで始まることは無い)ため、後続の階層
// 表現無しでも見出し行すべてに過不足無く一致する――BREの範囲内で
// 完結する分、ERE前提の旧パターンより移植性も高い。
std::string tutorialCutPattern() { return "^\xE3\x80\x80[0-9]"; }


Application::Application(Display &display, KeyInput &keyInput, History &history, const Configuration &configuration,
                          Charset::Encoding encoding, int width, ShellOptions shellOptions,
                          std::string homeDirectory, std::string tempRootDirectory, std::string workRootDirectory,
                          std::string bootOptions)
    : display_(display),
      keyInput_(keyInput),
      pathResolver_(homeDirectory),
      tempRootDirectory_(resolveTempRoot(homeDirectory, tempRootDirectory)),
      workRootDirectory_(resolveTempRoot(homeDirectory, workRootDirectory)),
      bootOptions_(std::move(bootOptions)),
      errorLogPath_(tempRootDirectory_ + "/error"),
      logFilePath_(tempRootDirectory_ + "/shell"),
      usilicrcPath_(homeDirectory + "/.usilic/usilicrc"),
      historyFilePath_(homeDirectory + "/.usilic/history"),
      tutorialPath_(homeDirectory + "/.local/lib/usilic/TUTORIAL"),
      tutorialBasePath_(homeDirectory + "/.local/lib/usilic/TUTORIAL"),
      encoding_(encoding),
      configuration_(configuration),
      history_(history),
      directoryView_(display),
      fileView_(display),
      grepView_(display),
      cutView_(display),
      cutContentView_(display),
      manView_(display),
      locateView_(display),
      archiveView_(display),
      archiveContentView_(display),
      duView_(display),
      underlineView_(display),
      listBrowserView_(display),
      copyView_(display),
      epubVerticalView_(display),
      epubTocVerticalView_(display),
      promptEditor_(display, encoding),
      directoryMode_(directoryView_, modeStack_, history, configuration),
      fileMode_(fileView_, history, configuration, encoding, width),
      grepMode_(grepView_, configuration),
      cutMode_(cutView_, cutModeStack_, configuration),
      manMode_(manView_, history, configuration, encoding, width),
      programMode_(keyInput, display, history),
      locateMode_(locateView_, configuration),
      archiveMode_(archiveView_, history, configuration),
      shell_(display, [&] {
        shellOptions.rows = display.rows();
        shellOptions.columns = display.columns();
        shellOptions.encoding = encoding;
        return shellOptions;
      }()),
      cpMvRmMode_(configuration, keyInput, &errorLogPath_),
      duMode_(duView_, history, configuration),
      underlineMode_(underlineView_),
      listBrowserMode_(listBrowserView_),
      editMode_(keyInput, configuration),
      execMode_(keyInput, &history),
      copyMode_(copyView_) {
  layoutViews();
  // K-65(^I): Shell自身のコマンドラインのための、実際のファイル
  // システムに裏打ちされた補完、Shellが最後に自己申告したディレク
  // トリ(それ自身のプロンプトのパースが既にこれを追跡している――
  // Shell::currentDirectory()自身のコメント参照)に対して相対的に
  // 解決される。以前は全く未設定のままで、Shell::onTab()自体は完全に
  // 実装されており合成的なプロバイダに対してテスト済み
  // (test_shell.cpp参照)であるにも関わらず、Shellモードでの
  // Tabが黙って何もしないことを意味していた。
  fileMode_.setErrorLogPath(&errorLogPath_);  // FileMode::binaryTypeText()参照(usilicrcのCODE)
  shell_.setCompletionProvider(
      [this](const std::string &partial) { return completeFilesystemPath(partial, shell_.currentDirectory()); });
  // 2026-09-18追加、HELP.txt 2.13章「LIST」の実装(shouldSearchPath()
  // 自身のコメント参照): SHELLプロンプトの入力バッファ全体に区切り
  // 文字が無い間(=まだコマンド名自体を入力中)、Shell::onTab()が
  // こちらを使う。
  shell_.setProgramCompletionProvider(
      [this](const std::string &partial) { return pathProgramCandidates(partial); });
  // K-50(^J^R)自身のRingCategory::ShellCommand履歴(2026-08-04の訂正:
  // 単なる^Rでは「ない」――ToggleHistoryView自身のコメント参照)、
  // それに加えてこのポートの新しい^P/^Nコマンド呼び出し
  // (feedShellByte()自身のコメント参照)。Shell::submitLine()は、
  // 入力されたコマンドの実行に成功した後、(設定されていれば)常に
  // historyRecorder_を呼んできた――しかしここでは誰もそれを一度も
  // 「設定」していなかった、^P/^Nを作っている最中(2026-08-02)に
  // 発見: このカテゴリは永遠に黙って空だった、つまり^J^R自身の
  // ピッカーも、他の点では完全に結線されているにも関わらず、表示
  // すべきものを一切持たなかったことを意味していた――このプロジェクト
  // 全体で繰り返し見つかっている、まさに「結線はされているが一度も
  // 接続されていない」パターン(すぐ上のsetCompletionProvider()自身の
  // 経緯参照)。
  shell_.setHistoryRecorder(
      [this](const std::string &line) { history_.pushRecent(RingCategory::ShellCommand, line); });
  // 2026-08-17修正: このフック(Shell::submitLine()自身のディスパッチ、
  // input_shell()のdo_prog()への引き渡しから移植)は存在していたが、
  // 一度も結線されていなかった――上でsetHistoryRecorder()について
  // 記した、まさに「結線はされているが一度も接続されていない」パターン。
  // 実際の報告("'less CLA'^I→[return]→less画面でなく「cat CLAUDE.md」
  // のような画面...[return]連打で一行ずつ進む")により、SHELLプロンプト
  // で認識されたPROGRAMコマンド(例えば"less CLAUDE.md")をタイプすると、
  // 代わりに実際の、ptyに接続されたzshの子プロセスへ送られていること
  // が判明した――これは確かに`less`を実行するが、Shell自身の出力
  // パイプライン(行指向で、プロンプトを検出する、クラスコメント参照)
  // は、フルスクリーンプログラムの生のカーソル制御/再描画のエスケープ
  // シーケンスを運ぶようには一度も作られておらず、それらをスクロール
  // バックの「テキスト」に文字化けさせてしまい、Shell自身のcommandLine_
  // もキー入力ごとのバイトを子プロセスへ転送することは無い
  // (feedShellByte()のESCについてのコメント参照)――これが「素の
  // Enterがlessに、それ自身の次の行へ進むキー割り当てとして届く」と
  // いう症状をまさに説明する。isRecognizedProgram()/cdWrap()を再利用
  // する(なぜ素のコマンドがcdの接頭辞を必要とするのかについては
  // executeProgram()自身のコメント参照: usilicは決してchdir()せず、
  // この引き渡しはptyに接続されたzshではなく、ChildProcess::
  // runInteractive()経由(新規のfork/exec)で実行されるため、Shellが
  // 現在どこを閲覧しているかではなく、usilic自身のプロセスのcwdを
  // 継承する)が、directoryMode_のではなくshell_.currentDirectory()に
  // 対して解決される。historyRecorder_は意図的にここでは併せて呼ばれ
  // ない: ProgramMode::run()自身のrecordedCommandパラメータが、既に
  // まさに同じRingCategory::ShellCommandのringへ積んでいる。
  shell_.setProgramLauncher([this](const std::string &programToken, const std::string &fullLine) -> bool {
    // 2026-09-15追加、直接のユーザー報告("SHELLで環境変数USILICが
    // 利かない...一度は完動していた")を深掘りした結果判明した回帰:
    // isRecognizedProgram()の"usilic"無条件認識(b26fdf5、2026-09-09、
    // ^Jp/PROGRAMプロンプト向けの修正)は、このラムダ(Shell::
    // submitLine()自身のprogramLauncher_、SHELLプロンプトで「直接
    // 打ち込まれた」コマンドの横取り判定)とexecuteProgram()(^Jpの
    // 判定)の両方で共有されている――が、両者は全く別物である。
    // 「打ち込まれたコマンドをptyへ送らずProgramMode::run()/
    // ChildProcess::runInteractive()(新規fork/exec)へ迂回させる」
    // このラムダの仕組みは、フルスクリーンプログラム(vi/less/man等)
    // がShell自身の行指向パイプラインでは正しく動かないことへの
    // 対処であり、"usilic"自身にはそもそも無関係だった――Shell::
    // start()が既にsetenv("USILIC", ...)で自分自身の再帰起動を
    // ptyへ送るだけで正しく検出できるようにしている(main.cpp自身の
    // tryHandoffToParentShell()参照)。b26fdf5はexecuteProgram()
    // (^Jp)側の正しい修正だったが、isRecognizedProgram()を経由して
    // このラムダにも同じ無条件認識を及ぼしてしまい、以後
    // 「SHELLプロンプトで`usilic ...`/`... | usilic`と直接打つと、
    // ptyのbashへは一切送られずChildProcess::runInteractive()経由の
    // 独立した新規プロセスとして起動される」という回帰を作り込んで
    // いた――その新規プロセスはptyのbash(USILIC=1を持つ)の子では
    // なくusilic自身の直接の子なので、USILIC環境変数を一切持たず、
    // tryHandoffToParentShell()の検出が常に失敗し、-c/-g省略時の
    // 既定(macOSではGUI)へ落ちてしまっていた。ここでは"usilic"
    // だけを明示的に除外し、通常通りptyへ送る(そちらの既存の
    // env変数ベースの検出へ委ねる)――isRecognizedProgram()自体、
    // および^Jp側の挙動には一切手を付けない。
    if (programToken == "usilic") return false;
    if (!isRecognizedProgram(programToken)) return false;
    display_.flush();
    shell_.setBusy(true);
    programMode_.run(cdWrap(shell_.currentDirectory(), fullLine), fullLine);
    shell_.setBusy(false);
    shell_.scrollback().redraw();
    return true;
  });
  // モードラインのフィールド、disp_shell()自身のdisp_window(...,
  // T_shell, name="SHELL", prev, arg, ...)呼び出し(shell.c)から移植:
  // 左側に固定の"SHELL";右側では、trailingフィールドが"(program)
  // shellReturnName_"――"(bash)"/"(zsh)"(disp_shell()自身の
  // sprintf("%c(%s)", K_z, chp->argv[0])から移植した
  // Shell::programName()自身のコメント参照)にshellReturnName_
  // (それ自身のコメント参照――ensureShellActive()/shellRestart()に
  // よってスナップショットされる、前のモード自身の名前)が続く。
  // 実際のユーザー報告(2026-08-02、legacyビルドと並べて比較)により、
  // このポート自身のSHELLモードラインから"(program)"の部分が完全に
  // 欠けていることが判明した。両方の部分が、同じセッション内の別々の
  // Shellへの入場/再起動をまたいで変わり得るため、(DirectoryMode
  // 自身のプロバイダのような)ライブなコールバックにしてある。
  // 2026-08-17修正: `trailing`/`shellReturnName_`は以前は1つの文字列
  // ("(zsh) /path/")へ連結され、途切れない単一の反転表示の走りとして
  // 描画されていた――実際の報告(スクリーンショット=cl)により、それら
  // の間に目に見える区切りが無く、行の右端へのパディングも無いことが
  // 判明した。disp_window()(legacyのdisp.c、直接読んだ)は、これらを、
  // 間に本物のプレーンな隙間を挟んだ(`prev`がそれ自身の`vt_stand()`の
  // 走りを始める前の、arg自身の`vt_endstand(); vt_putchar(' ')`)2つの
  // 「別々の」反転表示の走りとして描き、`prev`を反転表示で出力する前に
  // それを`vt_col(0)`まで明示的にスペースでパディングする――これは
  // まさに、trailing/trailing2/fillTrailingToEdgeが他の場所で既に
  // 提供している形(DirectoryMode自身のプロバイダ参照)である。
  // (shellReturnName_が空――legacyの`*prev == K_z`の場合)前のモードの
  // 名前が全く無い時、legacyはパディングの代わりに太字の右寄せの
  // プログラム名/バージョンにフォールバックする;このポートの既存の
  // `rightWatermark`フォールバックは既にその同じフォールバックを
  // 実装しており、ここでも結線するだけでよかった。
  // Positionフィールド(2026-08-24追加、直接のユーザー依頼+legacy
  // ソースを読んで確認済み、disp.cのprint_line(): SHELLは、disp_shell()
  // が負のlenでprint_line()を呼ぶ「唯一の」モードであり、汎用的な
  // 「合計の幅に合わせてパディングする」分岐ではなく、それ自身の固定
  // 6桁のゼロパディングの分岐を取る――ModeLine::formatFixedWidth()
  // 自身のコメント参照)――同じ依頼が併せて求めたバイト数フィールド
  // については下記参照。
  shell_.scrollback().setUseFixedWidthPosition(true);
  // 2026-08-26 addition (a direct user request, refined twice: "プロン
  // プトがすでに1行使ってるんで「00001/00001 END」が自然では" ->
  // "あなたの理論が正しければ...そもそも間違い"は、当初の狭い修正が
  // 自己矛盾していたことを指摘した): ライブなプロンプト行は、その
  // コンテンツが空の時だけでなく、常に、実際のscrollback_のコンテンツ
  // に上乗せされたそれ自身の追加行として数えられる。なぜこれがShellの
  // scrollback_に特有に限定されていて、共有のデフォルトではないのか
  // についてはContentView::setCountLivePromptAsLine()自身の宣言コメント
  // 参照;setAtLivePromptLine()自体は、ここで再度設定されるのではなく、
  // Shell自身のbeginHistoryBrowsing()/exitHistoryBrowsing()(それら
  // 自身の宣言コメント参照)によって動的にトグルされる。
  shell_.scrollback().setCountLivePromptAsLine(true);
  shell_.scrollback().setModeLineFieldsProvider([this](int) -> ContentView::ModeLineFields {
    // バイト数(2026-08-24追加、直接のユーザー依頼: "可能なら現在行まで
    // のバイト数をサポートしてほしい")――これについてはlegacyの前例は
    // 無く、正真正銘の新機能。
    //
    // 2026-08-26の訂正(直接のユーザーの追跡指摘、FileMode自身の同名の
    // フィールドとその自身の既に定まった仕様、HISTORY.md:11323自身の
    // "実際に読み込まれたバイト数としたい...ページ戻しが起きたとして
    // もすでに読み込んだ分量としてのバイト数は変わらない"を指し示す
    // もの): このフィールドは以前は「カーソル」の行まで(0..cursorIndex)
    // だけを合計しており、そのため^Pで履歴を「後ろへ」閲覧すると、
    // 表示される数が黙って縮んでいた――実際の、注意深く検証された
    // 報告("④の「68」も...加算されていないのでバグ。正しい表記は
    // ②③④とも「102」のはず")により、これがFileModeのページャの
    // バイト数がまさに同じ方法で修正されたのとまさに同じ理由でバグと
    // 読めることが確認された: どちらのフィールドも、「今カーソルが
    // たまたまどこにあるかより前がどれだけあるか」ではなく「これまで
    // に本当にどれだけ蓄積したか」に答えるために存在する――そのため
    // これは今や、cursorIndexパラメータに全く依存せず、FileMode自身の
    // 単調増加という慣習に合わせて、無条件にscrollback_自身の「全ての」
    // 行を常に合計する。scrollback_自身のLine::textが分割された時に
    // 失われる改行バイトの分、行ごとに+1、他の全てのモード自身の
    // バイト数フィールド(contentByteCount()、HELP自身のdoctest)が
    // 使うのと同じ慣習に合わせている。
    std::size_t bytes = 0;
    for (const auto &line : shell_.scrollback().lines()) bytes += line.text.size() + 1;
    bool hasPrev = !shellReturnName_.empty();
    ContentView::ModeLineFields fields;
    fields.name = "SHELL";
    fields.trailing = std::to_string(bytes);
    fields.fillTrailingToEdge = hasPrev;
    fields.rightWatermark = hasPrev ? "" : "usilic v1.5b";
    fields.trailing2 = "(" + shell_.programName() + ")";
    fields.trailing3 = shellReturnName_;
    // 2026-09-20再修正(直接のユーザー指摘、前回の対応は不十分だった):
    // 検索アクティブ中の^Gは検索解除のみでどこへも遷移せず、その場の
    // モード(SHELL)へ留まる――なので trailing3 には、実際に何か別の
    // 場所へのパスを装った値ではなく、リテラルに"SHELL"(=このまま
    // ここに留まる)を出す。FileMode::pagerReturnsToShell_のtrailing3
    // ="SHELL"という既存の前例と同じ考え方。trailing2("(grep)"系
    // バッジ)/nameの差し替えはそのまま維持。
    if (shell_.scrollback().grepInPlaceActive() || shell_.scrollback().grepNoMatchActive()) {
      fields.name = shell_.scrollback().grepInPlaceLabel();
      fields.trailing2 = shell_.scrollback().grepInPlaceProgramBadge();
      fields.trailing3 = "SHELL";
      return fields;
    }
    return fields;
  });
  // リストベースの履歴ピッカー(K-45..K-49、^X^F/^X^D/^R等)向けの
  // モードラインのフィールド: カテゴリ自身("FILE"/"DIR"/"COMMAND"等、
  // legacy自身の" FILE_HISTORY"方式の命名、RingCategory自身のコメント
  // に合わせている)を左寄せのnameフィールドとして、ハイライトされて
  // いる項目自身の文字列をtrailingフィールドとして――実際のユーザー
  // 報告(2026-08-02: "^X^F（FILE）が置き換わってない")により、どの
  // 履歴カテゴリが閲覧されているかを示すものがどこにも無いことが
  // 判明した、これは今も修正しているが、最初の修正(2026-08-02)は
  // 2つのフィールドを逆にした「上に」、カテゴリを括弧で囲んでしまった
  // (括弧は、他の場所でのtrailingのPROGRAM名フィールド、例えば
  // DirectoryMode自身の"(ls)"のために予約されている――legacyとの実際
  // の並列スクリーンショット比較、2026-08-03により、カテゴリ名は
  // パスの後に続く"(FILE)"ではなく、例えば"FILE 1/5 END
  // /path/to/file"のように、括弧無しで「左」端に属することが示された)。
  // cursorIndex、listBrowserCategory_、そして(2026-08-04追加)
  // listBrowserChronological_の全てが別々の呼び出し/トグルをまたいで
  // 変わるので、ライブなコールバックにしてある: ユーザーが^Rを押して
  // 現在のリストをその時系列の「HISTORY」表示へ切り替えた時
  // (ToggleHistoryView自身のコメント参照)、nameフィールドは、カテゴリ
  // 自身の通常のラベルの代わりに固定のリテラル"HISTORY"を表示する、
  // legacyのmp[M_hist]==" HISTORY"が、どの下位のringが表示されている
  // かに関わらずT_hfile/T_hdir等に一様に適用されるのに合わせている。
  //
  // 2026-08-05追加、プロンプトから起動される呼び出しのみ
  // (promptHistoryRecallKind_が設定されている――全体の経緯について
  // はstartPromptHistoryRecall()自身のコメント参照): nameはここでは
  // 「常に」"HISTORY"である(これらのカテゴリがフォールバックできる
  // 別のソート済みビューは無い)、そしてrightWatermarkは「プロンプト」
  // 自身のカテゴリラベル(promptLabelText()、例えば"CUT"/"FGREP"/
  // "MAN")を表示する――実際のユーザーの再現(スクリーンショット⑪、
  // 2026-08-05)が、この正確な形を確認した: "HISTORY" + 位置 +
  // 選択された項目、"CUT"が別に右寄せで、これが「どの」プロンプトの
  // 履歴であるかを明確にする(そのような区別を必要としないFILE/DIR
  // 自身の^X^F/^X^Dで起動される画面とは異なり――そこへどうやって
  // 来たかで既にカテゴリは明らかである)。
  listBrowserView_.setModeLineFieldsProvider([this](int cursorIndex) -> ContentView::ModeLineFields {
    const auto &items = listBrowserMode_.items();
    bool inRange = cursorIndex >= 0 && cursorIndex < static_cast<int>(items.size());
    // 2026-08-05修正: 空のリスト(^J^S/^J^C自身の新しい直接入力の経路
    // ――startRegexPromptList()自身のコメント参照――は、以前の^R専用
    // の経路が一度もそうしなかったよりはるかに頻繁に、itemsが空の
    // 状態でここに着地し得る)は、以前はここで完全に空白のModeLineFields
    // へ抜け出しており、redraw()自身の"--"プレースホルダー行
    // (ContentViewのコメント参照)と一緒に、name/バッジのフィールドを
    // 完全に隠していた――EGREPがまだringに何も無い状態でこのプロバイダ
    // に到達するようになった時点で、実際のdoctestの失敗によって発見
    // された。その場合trailingは単に空である(表示するものが無い);
    // name/バッジのフィールドは、埋まったリストと同じくらい、空の
    // リストでも意味がある。
    std::string trailing = inRange ? items[cursorIndex] : "";
    // 2026-09-18追加、HELP.txt 2.13章「LIST」の実装: 他の全ての分岐
    // (promptHistoryContext_ベースのもの)より先にチェックする――
    // ここでチェックしないと、下のフォールバック(ringCategoryLabel
    // (listBrowserCategory_))へ落ち、無関係な直前のring種別が誤って
    // 表示されてしまう(listBrowserCategory_はこの機能では一切
    // 更新しない――design.md参照)。
    if (tabCompletionContext_) return {"LIST", trailing};
    // RecentFile/RecentDirectory(2026-08-17追加): 意図的に、下の汎用の
    // promptHistoryRecallKind_の分岐より「前に」チェックする――この
    // 2つは、GREP/CUT/MANが(タイプする機能のために)使うのと同じ直接
    // 入力の仕組みを通じて到達するが、それらのモードラインの形は、
    // このプロバイダが既にそれらに対して持っていた「古い」プレーンな
    // ピッカーのレンダリング(ringCategoryLabel()相当の
    // promptLabelText()経由でname="FILE"/"DIR"、パスへのPathMiddleの
    // 切り詰め、^Rが時系列ビューへ切り替えた時だけ"HISTORY"+バッジ)
    // に合わせ続ける必要がある――どこかへ戻る検索を実行するのではなく
    // パスを直接開く画面には意味を成さない、GREP/CUT/MAN自身の
    // 「trailing2としての宛先ディレクトリ」という形では「ない」。
    if (promptHistoryContext_ &&
        (promptHistoryContext_->kind == PromptKind::RecentFile || promptHistoryContext_->kind == PromptKind::RecentDirectory)) {
      std::string name = listBrowserChronological_ ? "HISTORY" : promptLabelText(promptHistoryContext_->kind);
      // fillTrailingToEdge=true(2026-08-18修正、実際の報告+スクリーン
      // ショット=cr: "一文字も入力されていないと不良のまま"――項目348の
      // drawPromptLabel()のfill-to-edgeの修正は、編集が始まった「後」
      // の画面だけをカバーする;この読み取り専用の候補リストの状態
      // は、代わりにContentView::renderModeLine()が「この」プロバイダ
      // 経由でレンダリングするが、1文字もまだタイプされていない間は、
      // 候補パス自身の末尾から画面の右端までの同じベージュの隙間を、
      // 埋められないまま残していた)。この種類にはtrailing2は存在
      // しない(上のこの分岐自身のコメント参照)ので、
      // renderModeLine()自身のfillTrailingToEdgeのロジックが、
      // それ自身の反転表示の走りの中で、`trailing`自体(候補パス)を
      // 端まで丸ごとスペースでパディングする――まさにDirectoryModeの
      // "(ls)" trailingフィールドが既に使っているのと同じ仕組み。
      return {name, trailing, /*fillTrailingToEdge=*/true,
              /*rightWatermark=*/listBrowserChronological_ ? promptLabelText(promptHistoryContext_->kind) : "",
              /*trailing2=*/"", /*trailingPlain=*/false, /*trailing2RightJustified=*/false,
              /*trailing2Kind=*/ContentView::TrailingKind::QueryEcho,
              /*trailingKind=*/ContentView::TrailingKind::PathMiddle};
    }
    if (promptHistoryContext_) {
      // 2026-08-11修正: 実際の報告+legacyソースを読んで(mode.cの
      // T_hfgrep/T_hegrep対プレーンなM_fgrep/M_locate/M_cut2)――直接
      // 入力(^J^S/^J^C、startRegexPromptList())は、本物の^R呼び出しと
      // 「同じ」画面ではなく、異なる表示になる: (promptLabelText()
      // 経由の)実際のモード名(例えば"CUT"――CUTのregex亜種がそのring
      // を借用しているために誤って"EGREP"と言ってしまうであろう
      // ringCategoryLabel()ではなく)が左に、そして(この検索がDirectory
      // モードへ戻る場合は)宛先ディレクトリが右にハイライトされる、
      // 本物の^Rが使う固定のリテラル"HISTORY"という名前+ハイライト
      // されないモード名のバッジの代わりに。!listBrowserChronological_
      // (2026-08-29修正、正確な変更前/後のフィールド値を伴う直接の
      // ユーザー報告――"モード名：「EGREP」[HISTORY]、戻り値：
      // 「...usilic/CLAUDE.md」[EGREP]"): isDirectEntry単独は、
      // GrepPattern/CutPatternの候補リストセッションの全生存期間に
      // わたって、^Rがそれを時系列の使用履歴ビューへトグルした「後」
      // も含めてtrueのままである(2026-08-28自身の修正はそのトグルを
      // 機能的に動作させたが、toggleListBrowserHistory()自身のコメント
      // 参照、いったんトグルされたら「この」レンダリングの分岐が
      // それを依然として「直接入力」として扱うのをやめるようには一度
      // も更新しなかった)――そのため、下の"HISTORY"の分岐
      // (name="HISTORY"、rightWatermark=promptLabelText(kind)、
      // legacy自身のT_hfgrep/T_hegrepの形に一致させ、上のRecentFile/
      // RecentDirectory「自身」の兄弟の分岐がそれ自身の
      // `listBrowserChronological_ ? "HISTORY" : ...`というチェック
      // 経由で既に行っているのとまさに同じもの)へ流れ落ちる代わりに、
      // この分岐(宛先パス方式)が時系列モードでも取られ続けていた。
      if (promptHistoryContext_->isDirectEntry && !listBrowserChronological_) {
        PromptKind kind = promptHistoryContext_->kind;
        // 2026-08-28修正、直接のユーザー報告("CLAUDE.md上で^J^S→
        // モードラインに戻り値がない"): これは以前は、Directoryモード
        // へ戻る時だけしか宛先を表示しなかった――まさにdrawPromptLabel()
        // 自身のdestinationModeのswitchの狭い特別扱い(そのコメント参照:
        // "「宛先がDirectoryの場合のみ」に制限するのは同じくらい恣意的
        // だった")が既に訂正して脱していたが、ここでは一度も反映され
        // ていなかった。previousModeName()は既にFile(実際のファイル
        // パスを返す)を汎用的に扱う、これは実際の編集が始まった時に
        // まさに「この同じ」画面が示すもの(drawPromptLabel()、
        // refreshDirectEntryModeLine()/activatePrompt()経由――
        // previousModeName()自身のFileモードについてのコメント参照)と
        // 一致する――ここでそれを再利用することで、読み取り専用の候補
        // リストの画面とライブ編集の画面が、一方が表示する宛先を他方が
        // 省いてしまうのではなく、一貫性を保つ。
        ApplicationMode destinationMode = ApplicationMode::Directory;
        if (kind == PromptKind::GrepPattern) destinationMode = grepReturnMode_;
        else if (kind == PromptKind::CutPattern) destinationMode = cutReturnMode_;
        // 2026-09-20追加、直接のユーザー報告(^Rの再設計に伴う"^J^Pが
        // 戻り値無し"): ProgramCommandがこの分岐(isDirectEntry &&
        // !listBrowserChronological_、旧"COMMAND"改めソート済み
        // "PROGRAM"ビュー)へ到達するのは今日のsortDirectEntryList
        // 拡張が初めて――以前は他の全てのカテゴリ同様chronological
        // 固定だったので、この分岐自体が到達不能だった。ここに来る
        // 時点でcurrentMode_は既にApplicationMode::ListBrowserへ
        // 切り替わっている(この関数の少し下のcurrentMode_代入参照)
        // ため、GrepPattern/CutPatternと違って専用の戻り先変数を
        // 持たないProgramCommandがすぐ下の`else`(旧`currentMode_`)へ
        // 落ちると、previousModeName(ListBrowser)の空文字列フォール
        // バック(すぐ下のコメント、EGREP自身の同種の過去のバグ参照)
        // に当たって戻り値が消えていた。startPromptHistoryRecall()が
        // 切り替え「前」のcurrentMode_をpromptHistoryContext_->returnMode
        // へ既に退避済みなので、それを使う。
        else if (kind == PromptKind::ProgramCommand) destinationMode = promptHistoryContext_->returnMode;
        else destinationMode = currentMode_;
        // 2026-09-11追加、直接のユーザー報告(drawPromptLabel()自身の
        // grepReturnListCategory_のコメント参照、"^J^S...戻り値空白"):
        // EGREP(GrepSyntax::Regex)は`startRegexPromptList()`が
        // `listBrowserCategory_`をEGREP自身の候補一覧向けに上書きして
        // しまうため、previousModeName(ListBrowser)の空文字列フォール
        // バックがdrawPromptLabel()側と全く同じ形でここにも現れていた
        // ――FGREPの方は`activatePrompt()`だけを呼ぶため
        // `drawPromptLabel()`側の同種の修正だけで足りるが、EGREPは
        // こちらの`listBrowserView_`自身のModeLineFieldsProviderも
        // 経由するため、同じ`grepReturnListCategory_`を使う修正が
        // 別途必要だった。
        std::string destinationName = (destinationMode == ApplicationMode::ListBrowser && kind == PromptKind::GrepPattern)
                                           ? (grepReturnListCategory_ == RingCategory::RecentFile ? "FILE" : "DIR")
                                           : previousModeName(destinationMode);
        std::string trailing2 = destinationName.empty() ? "" : truncatedTrailingPath(destinationName);
        return {promptLabelText(kind), trailing, /*fillTrailingToEdge=*/!trailing2.empty(),
                /*rightWatermark=*/"", trailing2, /*trailingPlain=*/true, /*trailing2RightJustified=*/true};
      }
      // 2026-09-26追加、直接のユーザー依頼(CUT一覧で`^S^R`した時、右端に
      // 非ハイライトの"FGREP"が出る件について"戻り値は①のときの「CUT」と
      // 同じでいい"): 検索パターン(^S/^J^S)の履歴一覧では、右端に種別
      // ("FGREP"/"EGREP")ではなく、確定後に戻る先のモード名を、呼び出し元
      // の入力行(drawPromptLabel()、例: "FGREP 1/24 色 ... CUT")と同じ形で
      // 出す――種別自体は入力行の時点で既に見えているので、ここで繰り返す
      // より「どこへ戻るか」の方が役に立つ。上の直接入力一覧と同じ組み立て。
      if (promptHistoryContext_->kind == PromptKind::GrepPattern) {
        std::string destinationName = (grepReturnMode_ == ApplicationMode::ListBrowser)
                                          ? (grepReturnListCategory_ == RingCategory::RecentFile ? "FILE" : "DIR")
                                          : previousModeName(grepReturnMode_);
        if (!destinationName.empty()) {
          // 2026-09-27: ソート側(EGREPの一覧等)は"HISTORY"ではなく"EGREP"/"FGREP"。
          return {listBrowserChronological_ ? "HISTORY" : listBrowserCategoryLabel(), trailing,
                  /*fillTrailingToEdge=*/true, /*rightWatermark=*/"",
                  truncatedTrailingPath(destinationName), /*trailingPlain=*/true, /*trailing2RightJustified=*/true};
        }
      }
      return {listBrowserChronological_ ? "HISTORY" : listBrowserCategoryLabel(), trailing,
              /*fillTrailingToEdge=*/false,
              /*rightWatermark=*/promptLabelText(promptHistoryContext_->kind), /*trailing2=*/"",
              /*trailingPlain=*/false, /*trailing2RightJustified=*/false,
              /*trailing2Kind=*/ContentView::TrailingKind::QueryEcho,
              /*trailingKind=*/ContentView::TrailingKind::PathMiddle};
    }
    std::string name = listBrowserChronological_ ? "HISTORY" : listBrowserCategoryLabel();
    // 2026-09-11追加、直接のユーザー依頼("^Sと^[iをFILE/DIR以外へも
    // 反映してほしい"): ManMode等自身の同じ分岐のコメント参照。他の
    // 3つの分岐(promptHistoryContext_が設定されている間)は、いずれも
    // プロンプト/候補一覧オーバーレイが開いている間だけ到達し、その
    // 間currentMode_はListBrowserにはならない(^S/^[iはプロンプトが
    // 開いていない時にしか起動できない)ため、実際にin-placeが起動
    // し得るのはこの最後の(プレーンなHISTORY/RESULTS閲覧)分岐だけ。
    if (listBrowserView_.grepInPlaceActive() || listBrowserView_.grepNoMatchActive()) {
      ContentView::ModeLineFields fields{listBrowserView_.grepInPlaceLabel(), trailing, /*fillTrailingToEdge=*/true,
                                          /*rightWatermark=*/"", listBrowserView_.grepInPlaceProgramBadge()};
      fields.trailing3 = name;
      return fields;
    }
    // trailingKind=PathMiddle(2026-08-17修正、=bw): この`trailing`は、
    // ハイライトされているFILE/DIR/COMMAND項目自身であり――端末幅を
    // 容易に超え得る完全な絶対パス(あるいはシェルコマンド、その末尾
    // もまた最も重要になりがちである)である。TrailingKindが存在する
    // 前は、`trailing`には切り詰めのオプションが一切無かったので、
    // 実際の再現(^J^L→検索→LOCATEの結果を開く→^X^F)により、他の全て
    // の長いフィールドが既にやっているような省略ではなく、それが
    // 「完全に」消える(モードラインが素の"FILE 1/1 END"にフォール
    // バックし、パスが全く無い)ことが判明した。
    return {name, trailing, /*fillTrailingToEdge=*/false,
            /*rightWatermark=*/"", /*trailing2=*/"",
            /*trailingPlain=*/false, /*trailing2RightJustified=*/false,
            /*trailing2Kind=*/ContentView::TrailingKind::QueryEcho,
            /*trailingKind=*/ContentView::TrailingKind::PathMiddle};
  });
  // 2026-08-13修正: 実際の報告――空の履歴ring(例えば新規インストール
  // 直後の最初の^J^L/^Jm等、あるいはまだ何も記録されていない実際の
  // ^R)は、ここでContentView自身の「デフォルト」の空リスト
  // プレースホルダー、素の"--"を表示していた。そのデフォルトはCUT
  // 自身のコンテンツビュー(実際の分割されたコンテンツ内の、実際の
  // ラベルの無い「領域」、CutMode::split()自身のコメント参照)向けに
  // 存在するものであり――領域を持つコンテンツではなくHISTORY/RESULTS
  // のリストであるlistBrowserView_にとっては意味的に間違っている。
  // (ユーザー自身の訂正による)「「--」はCUTのみ」――代わりに
  // GrepMode::search()自身の"No String"という慣習(HISTORY.md自身の
  // その決定の記録)を再利用する、ただしパターンの接頭辞は無い
  // (EGREP/FGREP検索にあるような、単一の「パターン」という概念は
  // FILE/DIR/MAN/PROGRAM/LOCATE/COMMANDのringには存在しない)――
  // GrepModeの検索ごとに動的なバージョンとは異なり、ここでは更新
  // すべき変化するテキストが無いので、静的に一度だけ設定する。
  {
    static const std::string kNoString = "No String";
    listBrowserView_.setEmptyPlaceholder(
        ContentView::Line{kNoString, std::nullopt, std::nullopt,
                           {AttributeRun{0, static_cast<int>(kNoString.size()), Display::Attribute::Underline}}},
        /*marked=*/false);
  }
  // CutContent(現在選択されているCUTセグメント自身のコンテンツ――
  // HELP自身の画面も含む)は、行全体を反転表示するカーソルハイライト
  // を決して得ない、FileMode自身のsetCursorHighlightEnabled(false)
  // (そのコンストラクタのコメント参照)に合わせている――実際のCUI/GUI
  // 並列スクリーンショット(2026-08-04)で確認済み: legacyのHELP画面は、
  // CutContent(プレーンなファイルのコンテンツと同様)がDirectoryMode
  // 自身の一覧のような選択可能なリストではないため、現在のセクション
  // 自身の見出し行("1.  Introduction")を、反転表示ではなくプレーンな
  // テキストで一番上に表示する。以前はContentViewのデフォルト(true)
  // のままだった、これはcutView_(セグメントの「ラベル」リスト自体は
  // DirectoryModeと同様、選択可能なリストで「ある」)には正しいが、
  // cutContentView_には間違っている。
  cutContentView_.setCursorHighlightEnabled(false);
  // 縦書き(setEpubVerticalView()参照)も横書きの本文と同じく、ふだんはブロックカーソル
  epubVerticalView_.setCursorHighlightEnabled(false);
  epubTocVerticalView_.setVerticalText(true);  // ContentView::setVerticalText()参照
  epubVerticalView_.setVerticalText(true);
  epubTocVerticalView_.setCursorHighlightEnabled(false);  // 選んでいる章は列の反転表示(refreshEpubTocVertical())
  // CutContent向けのモードラインのフィールド: cutOrigin_がHelpの時は
  // 左側に固定の"HELP"(legacy自身のhelp splitに対するリテラルな名前
  // に合わせている、上のShell自身のフィールドが固定のリテラル
  // "SHELL"であるのと並行)、そうでなければ「起点となった」ファイル
  // 自身の素の名前(File起点のCUTは常にfileView_の現在のコンテンツを
  // 分割する、executeCut()自身のコメント参照――FileMode自身の「素の
  // ファイル名、フルパスではない」という慣習に合わせている);右側は
  // セグメント自身のバイトサイズ(FileMode自身のバイトサイズtrailing
  // フィールドを反映)に続けて、「現在選択されている」セグメント自身の
  // ラベル(それが分割された区切り行――例えばHELPの番号付き見出しの
  // "1.  Introduction"、CutSegment自身のコメント参照)。
  // 実際の2026-08-04のユーザー報告/スクリーンショットは、legacyの
  // 実際のHELP表示を"HELP 01/48 47% 2618  1. Introduction"として示した:
  // バイト数("2618"、このプロバイダの同日の以前の改訂で既に移植済み)
  // と現在の章自身のラベルの「両方」が、一方が他方を置き換えるので
  // はなく、同じ行に現れる、数字だけを最初に読んだのを信用するの
  // ではなく、スクリーンショットを2回目直接見て確認済み。mode.c自身
  // のT_cutケース(sprintf(..., " \042%s\042", cutp->str))とmenu.c
  // 自身のinit_cut(hp->bp, ap->name, cutp->str, cutp->bp)呼び出しを
  // 合わせると、cutp->strが「選択されている」セグメントリストの
  // エントリ自身のラベルテキストであることが分かる――ここではバイト
  // 数の後に、「同じ」trailingフィールド内に追加される
  // (ModeLineFieldsにはこれ専用の別のスロットは無い: rightWatermark
  // は、汎用の2つ目のtrailingフィールドではなく、disp_window()の
  // "usilic v1.5b"というプログラム名のフォールバック専用に予約されて
  // いる、それ自身のコメント参照、そのためここでそれを転用するのは
  // 別種の不一致になる)。「この」プロバイダが最初に書かれた時点では、
  // CutContentだけに限定されていた(セグメントの「ラベル」リスト自体
  // であるcutView_ではない): disp_window()自身のswitchは、M_cut/
  // M_cut2を、M_prog/M_lfile/M_ldirの「モードラインで文字列を入力
  // する」処理とグループ化しており、これは意味的に異なる
  // (編集可能なプロンプトのような)モードラインの形であり、HELP自身
  // (name="HELP"は固定のリテラルであり、その「文字列入力」の分岐に
  // 全く紐付いていない)がそうするのと同じようには、このポートの
  // 単純なname/trailingのModeLineFieldsへ明白にはマッピングされて
  // いなかった――当てずっぽうにするのではなく、追って対応する課題
  // として残した。その追跡は今や下にある(2026-08-05): (当てずっぽう
  // ではなく期待される正確なモードラインのテキストによる)直接の
  // ユーザーの再現が、cutView_「自身」のname/trailingの形を独立に
  // 確認した。
  cutContentView_.setModeLineFieldsProvider([this](int) -> ContentView::ModeLineFields {
    // CutOrigin::Directory(2026-08-12修正): このプロバイダは、以前は
    // Helpか「Fileだと仮定する」(fileMode_.currentFile())のどちらか
    // しか扱っていなかった――これが書かれた時点では、Directoryからの
    // CUTのサポートは全く無かった。実際の報告+参照スクリーンショット
    // (=ai)により、Directory起点の分割について、fileMode_.currentFile()
    // が本当に空であること(FileModeは一切関与していなかった)が判明
    // し、そのため`name`は空文字列になっていた――
    // "モードラインにモード名（CUT）が無く"。すぐ下のcutView_自身の
    // Directory起点の選択(導出された名前ではなく、リテラルな"CUT"
    // ――名付けるべき単一のファイルが存在しない)を反映している。
    std::string name;
    if (cutOrigin_ == CutOrigin::Help) {
      name = "HELP";
    } else if (cutOrigin_ == CutOrigin::Directory) {
      name = "CUT";
      // 2026-09-15追加、直接のユーザー方針("CUT、F/EGREPは[MAN、
      // LOCATE、DU等に]関与（検索）できる"): Man/Locate/Du起点も
      // Directoryと同じく「名付けるべき単一のファイルが存在しない」
      // ―― ただしMAN/LOCATE/DU自身は「所在地の宣言はモード名で
      // なされる」という同じ慣習(DuMode/LocateModeのModeLineFields
      // Provider自身のコメント参照)に揃え、CUTの汎用ラベルではなく
      // 各モード自身の名前を出す。
    } else if (cutOrigin_ == CutOrigin::Man) {
      name = "MAN";
    } else if (cutOrigin_ == CutOrigin::Locate) {
      name = "LOCATE";
    } else if (cutOrigin_ == CutOrigin::Du) {
      name = "DU";
    } else {
      name = std::filesystem::path(fileMode_.currentFile()).filename().string();
    }
    std::string trailing = std::to_string(contentByteCount(cutContentView_));
    std::string label;
    if (const CutSegment *segment = cutMode_.selectedSegment()) {
      std::size_t start = segment->label.find_first_not_of(' ');
      if (start != std::string::npos) label = segment->label.substr(start);
    }
    // HELP(2026-08-23修正、実際の報告+参照: "HELP" "01/48 TOP"
    // "2194" "1.  Introdduction                  ")――バイト数と現在の
    // 章自身のラベルは、このプロバイダが以前は全ての起点について
    // やっていたように1つの文字列に連結されるのではなく、2つの
    // 「別々の」フィールド(trailing/trailing2、FileModeのバイトサイズ/
    // ディレクトリの分割が既に使っているのと同じペア)であり、ラベル
    // は行の端までパディングされる(これが最初に書かれた時点での実際
    // のスクリーンショットは、それらが同じ「行」にあることを示して
    // いたが、それはどちらの形でも成り立つ――これが確認済みの追跡
    // 対応である)。他の全ての起点(実際のFile/Directory起点のCUT)は
    // 正確に元のまま残してある: 影響を受けておらず、同じ2フィールド
    // の処理を求めているとは独立に確認されていない。
    // 2026-09-11追加、直接のユーザー依頼("^Sと^[iをFILE/DIR以外へも
    // 反映してほしい"): ManMode等自身の同じ分岐のコメント参照。name
    // (起点により"HELP"/"CUT"/"MAN"/"LOCATE"/"DU"/ファイル名のいずれか)
    // を"GREP"が乗っ取るので、その見えなくなった識別情報をtrailing3へ
    // 退避する。
    if (cutContentView_.grepInPlaceActive() || cutContentView_.grepNoMatchActive()) {
      ContentView::ModeLineFields fields{cutContentView_.grepInPlaceLabel(), trailing, /*fillTrailingToEdge=*/true,
                                          /*rightWatermark=*/"", cutContentView_.grepInPlaceProgramBadge()};
      fields.trailing3 = name;
      return fields;
    }
    if (cutOrigin_ == CutOrigin::Help) {
      return {name, trailing, /*fillTrailingToEdge=*/true, /*rightWatermark=*/"", label};
    }
    // 2026-09-27変更、直接のユーザー依頼("選択後の戻り値が、一覧で選んだ行の
    // 表示になっているんだけど、それは画面の冒頭行でもあるので重複...
    // 「CUT: 検索文字列（←パディング→）」にしてみて"): 選んだ見出し(=この
    // 画面の先頭行)ではなく、分割に使った検索文字列を右端まで埋めて出す。
    (void)label;
    return {name, trailing, /*fillTrailingToEdge=*/true, /*rightWatermark=*/"", "CUT: " + cutMode_.pattern()};
  });
  // cutView_自体(セグメントの「ラベル」リスト、CutContentの1つ上の
  // レベル――それ自身のクラスコメント参照)向けのモードラインの
  // フィールド: 左側に固定の"CUT"(あるいはcutOrigin_==Helpについて
  // "HELP"、CutContent自身のリテラルな名前の慣習に合わせている);
  // 右側は、新規の「最上位」の分割については起点となったファイルの
  // 「完全な」パス(2026-08-05のユーザー確認済みの例: doc/HELP.jpを
  // "man"で分割すると、trailingは"/Users/haa15030/git/usilic/src/doc/
  // HELP.jp"を示す――これはFileMode/CutContent自身の「素のファイル
  // 名」という慣習とは異なり、「完全な」パスであることに注意)、
  // あるいは「ネストした」分割(splitSelected()自身の再分割――なぜ
  // それが「親」レベルのパターンなのかについてはCutMode::
  // parentPattern()自身のコメント参照、例えば"man"のマッチを"usilic"で
  // 再分割した後の"CUT:man"であり、"CUT:usilic"ではない)については
  // "CUT:<親パターン>"。HELP自身の最上位(ネストしていない)trailingは
  // ここでは意図的に空のままにしてある――File起点のケースとは異なり、
  // まだそれについて具体的に確認済みの値が存在しない(他の場所での
  // 実際のスクリーンショットは、legacyがそこでもディレクトリパスの
  // trailingを使うことを示していたが、どのディレクトリかは独立には
  // 確認されていなかった);当てずっぽうにするのではなく、追って対応
  // する課題として残した。
  cutView_.setModeLineFieldsProvider([this](int) -> ContentView::ModeLineFields {
    // Log(2026-08-18、ESC l): プレーンなCUT検索とは異なる、独自の
    // モードラインの識別、同じ下層のsplit()エンジンへの、特定の専用の
    // 入り口について"CUT"以外の名前を表示するというHELP自身の既に
    // 確立した前例に合わせている――同じ"ESC l"キーへの、取り消された
    // 2026-08-05の試みとは異なり、なぜこれが安全なのかについては
    // executeStartLog()自身のコメント参照。
    // Man/Locate/Du(2026-09-15追加、cutContentView_自身の同一の
    // 2026-09-15修正のコメント参照): 「所在地の宣言はモード名で
    // なされる」という同じ慣習に揃えている。
    std::string name = (cutOrigin_ == CutOrigin::Help)     ? "HELP"
                        : (cutOrigin_ == CutOrigin::Log)    ? "LOG"
                        : (cutOrigin_ == CutOrigin::Man)    ? "MAN"
                        : (cutOrigin_ == CutOrigin::Locate) ? "LOCATE"
                        : (cutOrigin_ == CutOrigin::Du)     ? "DU"
                        : (cutOrigin_ == CutOrigin::Epub)   ? "EPUB"
                                                             : "CUT";
    if (cutOrigin_ == CutOrigin::Epub && !cutMode_.parentPattern()) {  // openEpub()参照
      ContentView::ModeLineFields fields{name, std::to_string(contentByteCount(fileView_)), /*fillTrailingToEdge=*/true,
                                          /*rightWatermark=*/"", ""};
      // 2026-09-29修正、直接のユーザー報告("CUT一覧の戻り値が(ls)じゃない"): 戻り値欄は
      // ^Gの戻り先。章の一覧の^Gは(ls)へ戻る(returnFromCut())ので、本のパスではなく
      // (ls)のディレクトリを出す。
      fields.trailing3 = directoryMode_.currentDirectory();
      return fields;
    }
    // HELP自身の最上位(ネストしていない)モードライン(2026-08-23
    // 修正、実際の報告+参照: "HELP" "01/26 TOP" "<バイト数>"
    // "/Users/.../    ")――このプロバイダが元々出荷していた「まだ
    // 具体的な値が存在しない」プレースホルダー(すぐ上のクラス
    // コメント参照)を置き換える;ディレクトリはHELPがどこから呼び
    // 出されたかであり――directoryMode_.currentDirectory()、下の実際
    // のDirectory起点のCUTの分岐が既に使っているのと「同じ」値――
    // このポートの他の全ての2フィールドのtrailingのペアと同様に
    // (すぐ上のcutContentView_自身の同一の2026-08-23修正参照)行の端
    // までパディングされる。
    //
    // バイト数は2026-08-24に訂正、直接のユーザー報告: これは
    // helpText_.size()(HELP.jpファイル「全体」)を表示していた、当時
    // は「cutView_自体は、ファイル全体ではなく「1つの」セグメントの
    // コンテンツしか一度も保持しない」ため必要だと考えられていた――
    // それは逆であった: このプロバイダはcutView_自体に登録されており、
    // TOCの時点では、cutView_自身のlines()は、下層のファイルのどんな
    // セグメントでもなく、レンダリングされた目次「そのもの」である
    // ――contentByteCount(cutView_)(下のFile/Directory起点の分岐が
    // 既に使っているのと「同じ」「実際に画面に映っているものの
    // バイト数」という慣習)がまさにそれを与え、ユーザー自身の参照
    // 値(413、TOC一覧自身のサイズであり、HELP.jpのはるかに大きい
    // 合計ではない)に一致する。
    if (cutOrigin_ == CutOrigin::Help && !cutMode_.parentPattern()) {
      return {name, std::to_string(contentByteCount(cutView_)), /*fillTrailingToEdge=*/true, /*rightWatermark=*/"",
              directoryMode_.currentDirectory()};
    }
    // バイト数(2026-08-24修正、実際の報告+参照: "CUT" "01/13 TOP"
    // "435" "/Users/.../CLAUDE.md         "――このプロバイダには以前は
    // バイト数のフィールドが全く無く、`trailing`にはパス/パターン
    // だけだった。File/Directory起点は、実際の既にロード済みの
    // ContentView(fileView_/directoryView_――executeCut()は、この画面
    // が実際にはfileView_を「表示」することは無いにも関わらず、
    // File起点の分割を内部的にFileMode経由で通すので、それは依然と
    // して正しいコンテンツを保持している)を分割するので、それらの
    // バイト数は、他の全てのモード自身のバイトサイズフィールドが
    // 既に使っているのと「同じ」contentByteCount()である。ネストした
    // 再分割(parentPatternが設定されている)とLogはどちらも古い単一
    // フィールドの形を保っている――どちらについても確認済みの参照は
    // まだ無い。
    if (!cutMode_.parentPattern() && (cutOrigin_ == CutOrigin::File || cutOrigin_ == CutOrigin::Directory)) {
      // 2026-09-29修正、直接のユーザー報告("戻り値がHISTORY.mdのまま...^Cfは(ls)
      // からしか発動できない。なので戻り値も(ls)でないと"): 戻り値欄は^Gの戻り先
      // なので、ファイルCUT(^Cf、cutReturnMode_ == Directory)ではCUTした
      // ファイルではなく(ls)のディレクトリを出す。^Cb(FILEへ戻る)は従来どおり。
      std::string path = (cutOrigin_ == CutOrigin::File && cutReturnMode_ != ApplicationMode::Directory)
                             ? fileMode_.currentFile()
                             : directoryMode_.currentDirectory();
      std::size_t bytes = contentByteCount(cutOrigin_ == CutOrigin::File ? fileView_ : directoryView_);
      // 2026-09-27追加、直接のユーザー依頼("CUTのモードライン、検索結果一覧の
      // 画面では(EGREP)があるほうがいい"): in-place GREPと同じ並び(バッジ、
      // その後に戻り値)。
      ContentView::ModeLineFields fields{name, std::to_string(bytes), /*fillTrailingToEdge=*/true,
                                          /*rightWatermark=*/"", grepSyntaxBadge(cutSyntax_)};
      fields.trailing3 = path;
      return fields;
    }
    // 2026-09-15追加、すぐ上のFile/Directoryの分岐と同じ形――Man/
    // Locate/Du自身の「今どこにいるか」を表す値(manMode_.currentPage()/
    // locateMode_.query()/duMode_.currentPath())をpath欄に使う。
    if (!cutMode_.parentPattern() &&
        (cutOrigin_ == CutOrigin::Man || cutOrigin_ == CutOrigin::Locate || cutOrigin_ == CutOrigin::Du)) {
      std::string path = cutOrigin_ == CutOrigin::Man      ? manMode_.currentPage()
                          : cutOrigin_ == CutOrigin::Locate ? locateMode_.query()
                                                             : duMode_.currentPath();
      std::size_t bytes = contentByteCount(cutOrigin_ == CutOrigin::Man ? manView_
                                            : cutOrigin_ == CutOrigin::Locate ? locateView_
                                                                              : duView_);
      ContentView::ModeLineFields fields{name, std::to_string(bytes), /*fillTrailingToEdge=*/true,
                                          /*rightWatermark=*/"", grepSyntaxBadge(cutSyntax_)};  // 上の同日のコメント参照
      fields.trailing3 = path;
      return fields;
    }
    std::string trailing;
    if (auto parent = cutMode_.parentPattern()) {
      trailing = "CUT:" + *parent;
    } else if (cutOrigin_ == CutOrigin::Log) {
      trailing = logFilePath_;
    }
    return {name, trailing, /*fillTrailingToEdge=*/false, /*rightWatermark=*/"", /*trailing2=*/""};
  });

  // 2026-08-11修正: grepView_には(上の他の全ての外出先ビューが既に
  // 持っているのに)モードラインフィールドのプロバイダが一切無かった
  // ので、実際のFGREP/EGREP検索のモードラインは、ContentView::
  // renderModeLine()自身の「プロバイダ無し」の分岐にフォールバック
  // していた――単なる素の位置テキストであり、実際のCUIスクリーン
  // ショットが示す"FGREP"/"(fgrep)"/検索元ディレクトリのフィールド
  // を欠いていた。DirectoryMode自身の"(ls)"の慣習(そのプロバイダの
  // コメント参照)を反映しているが、実際に実行されたどのgrepプログラム
  // についてでもあり、検索が実行された「元」のディレクトリを表示する
  // (grepReturnMode_ == Fileの場合のfileMode_.currentFile()自身の
  // ディレクトリはここでは別途追跡していないので、これは一般的な
  // Directory検索のケースについてのみtrailing2を埋める――開いている
  // ファイルのコンテンツに対する検索は、単にname/(egrep)のフィールド
  // だけを表示する、legacy自身のT_grepケースがそのパスについてもディ
  // レクトリフィールドを一度も埋めないのに合わせている)。
  // 2026-08-20修正、直接のユーザー依頼("DIR/FILE/FGREP/EGREP:モード
  // ラインに「バイト数」が無いモードは反映して"――これが拡張する元々
  // の"全モードのモードラインに...バイト数を追加してほしい"依頼に
  // ついてはContentView::totalByteCount()自身のコメント参照):
  // `trailing`は以前は"(fgrep)"/"(egrep)"というプログラムのバッジを
  // 保持しており、これがバイト数フィールドを一切持たない唯一の外出先
  // ビューになっていた(DIR/MAN/LOCATE/DU/ARCは全て既に`trailing`に
  // totalByteCount()を表示している)。バッジ自体は依然として有用
  // (他の全てのモード自身の"(<program>)"という慣習に合っている)
  // なので、削除するのではなくtrailing3(それ自身のコメント参照)へ
  // 移す。
  // 縦書き(rebuildEpubVertical()参照): 名前欄は本のファイル名、戻り値欄は"EPUB 縦書き"。
  epubVerticalView_.setModeLineFieldsProvider([this](int) -> ContentView::ModeLineFields {
    return {std::filesystem::path(fileMode_.currentFile()).filename().string(),
            std::to_string(contentByteCount(fileView_)), /*fillTrailingToEdge=*/true, /*rightWatermark=*/"",
            "EPUB 縦書き"};
  });

  // 縦書きの章の一覧(refreshEpubTocVertical()参照): 横書きの章の一覧と同じ"EPUB"、
  // 戻り値欄は^Gの戻り先の(ls)のディレクトリ。
  epubTocVerticalView_.setModeLineFieldsProvider([this](int) -> ContentView::ModeLineFields {
    ContentView::ModeLineFields fields{"EPUB", std::to_string(contentByteCount(fileView_)),
                                        /*fillTrailingToEdge=*/true, /*rightWatermark=*/"", "縦書き"};
    fields.trailing3 = directoryMode_.currentDirectory();  // 戻り値欄は^Gの戻り先の(ls)(cutView_の同じ欄参照)
    return fields;
  });

  grepView_.setModeLineFieldsProvider([this](int) -> ContentView::ModeLineFields {
    std::string name = grepSyntaxLabel(grepSyntax_);
    const auto &cmd = configuration_.commandFor(grepSyntaxRole(grepSyntax_));
    std::string program = cmd.argv.empty() ? "" : cmd.argv.front();
    std::string badge = program.empty() ? "" : "(" + program + ")";
    // 2026-08-24修正、直接のユーザー報告("HELPの1.Introductionページ
    // 上で^S→戻り値不良、正しくは「CUT:1.  Introduction」"): これは
    // 以前はDirectory起点の検索についてのみ戻り先のフィールドを
    // 埋めており、他の全てのgrepReturnMode_(File、CutContent――例えば
    // HELPのある章自身のコンテンツの中から実行されたFGREP、あるいは
    // 他のどんなCUTセグメントのコンテンツからでも)については空欄の
    // ままにしていた。CutContentはcutContentView_自身の"CUT:<セグメント
    // ラベル>"の慣習(先頭のスペースを取り除いた同じsegment->label、
    // それ自身のコメント参照)を反映する;Fileはすぐ上の
    // cutView_自身のFile起点のパスフィールド(fileMode_.currentFile())
    // を反映する。
    std::string path;
    if (grepReturnMode_ == ApplicationMode::Directory) {
      path = directoryMode_.currentDirectory();
    } else if (grepReturnMode_ == ApplicationMode::CutContent) {
      path = cutContentReturnLabel();
    } else if (grepReturnMode_ == ApplicationMode::File) {
      // 2026-09-19再訂正、直接のユーザー報告+旧版との並べ比較
      // ("CLAUDE.mdを選択→^Ssrc^M...GREPが二文字あるので"、旧版は
      // 「/Users/haa15030/git/usilic/CLAUDE.md」だった):
      // 2026-08-28の訂正(直下、取り消し線扱いで残す)は、`name`が既に
      // 画面左上に出ている検索種別のラベル("GREP"/"FGREP"/"EGREP")
      // そのものを、ここでもう一度"<name>: <pattern>"として繰り返す
      // 実装だったため、grepSyntax_がBasic("GREP")の時に限って
      // "GREP" ... "GREP: src"と、画面上に全く同じ単語が2度も並ぶ
      // 結果になっていた――EGREP/FGREPではnameとtrailing3の文字列が
      // 異なる("EGREP" vs "EGREP: ^-"も実は同じ語の重複だが、ハイフン
      // 記号のパターンに気を取られて見過ごされていた)。ユーザーが
      // 実際に旧版のCLAUDE.mdを対象にしたFGREPを並べて確認した結果、
      // 旧版はここに検索パターンではなく検索対象「ファイル自身の
      // パス」を出していたと判明し、そちらを正解として2026-08-28の
      // 判断を上書きしてこちらへ差し戻す。
      // (2026-08-28の判断の撤回: 「ファイル自身のパスは既に暗黙の
      // もの」という当時の理屈は、旧版との直接比較という新しい証拠の
      // 前ではもう成り立たない――比較無しの推測に基づく判断だった。)
      // なお着地後の画面(openSelectedGrepMatch()自身のFile分岐、
      // fileMode_.setSearchAnnotation(match->text))はこことは別個の
      // 画面で、今回の指摘の対象外(マッチ行自身のテキストを出す方が
      // 引き続き有用――そちらは変更していない)。
      path = fileMode_.currentFile();
    } else if (grepReturnMode_ == ApplicationMode::Man) {
      // 2026-09-19追加、直接のユーザー報告("^Jmls^M^Ssort^M→モード
      // ライン不良、正しくは「MAN: ls       」"): Group1/Group2の
      // 棲み分け方針(「後者群は前者群からも起動できる」)により、
      // MANの中からのFGREP/EGREP/GREPは既にviewForMode()/executeGrep()
      // 経由で実際に動いてはいたが、ここでの起点ラベルにMan分岐が
      // 一切無く、Directory/CutContent/Fileのどれでもない場合は
      // pathが空文字のまま――trailing3が丸ごと空欄になっていた。
      // Fileの分岐に倣い"MAN: <manで開いていたページ名>"を示す。
      path = "MAN: " + manMode_.currentPage();
    } else if (grepReturnMode_ == ApplicationMode::Locate) {
      // 上と同じ穴、LOCATE版。executeGrep()自身のコメントが既に
      // "Locateも含む"と明言している通り、LOCATEからのGREP自体は
      // 前から動いていたが、この起点ラベルの分岐だけが無かった。
      path = "LOCATE: " + locateMode_.query();
    } else if (grepReturnMode_ == ApplicationMode::Du) {
      // 2026-09-19六訂正、直接のユーザー報告("doc^M→戻り値不良。正しく
      // はDU: /Users/haa150...ic/CLAUDE.md"): 当初は集計対象のルート
      // (duMode_.currentPath())を表示していたが、previousModeName()
      // 自身のDuケースの同日の同種の修正と揃え、実際の戻り先(DUの
      // duReturnStack_自身の恒久的な基点)を表示するようにする――
      // Shellの分岐(すぐ下)がまさにそうしているのと同じくpreviousModeName()
      // へ委譲する。
      path = previousModeName(ApplicationMode::Du);
    } else if (grepReturnMode_ == ApplicationMode::Shell) {
      // 2026-09-19追加、直接のユーザー質問("SHELL画面でのGREPは可能
      // なの？"): 実際にdoctestで確認――SHELLのscrollbackから
      // startGrepPrompt()経由でGREPを起動でき、検索も正しく実行
      // される(既にviewForMode(Shell)がshell_.scrollback()を返す、
      // executeGrep()自身のコメント参照)。ただしこの起点ラベルの
      // 分岐だけがMan/Locate/Duと同様に無かった。previousModeName()
      // 自身のShellケースがまさに返すリテラル"SHELL"を再利用する
      // (Shell自身にMan/Locate/DuのようなQuery/Pageに相当する固有の
      // 状態は無い)。
      path = previousModeName(ApplicationMode::Shell);
    } else if (grepReturnMode_ == ApplicationMode::ListBrowser) {
      // 2026-09-11追加、直接のユーザー報告("usilic -c で起動→^X^F^J^S
      // ^Uusilic^M→検索結果はusilicが一件だけ。戻り値もそれ自体が無い"):
      // 上と同じ穴、投入・確定済みのEGREP結果一覧(ApplicationMode::Grep、
      // この関数自身)版。`^X^F`/`^X^D`自身の候補一覧から起動された
      // EGREP/FGREPが実際に検索を実行しGrepモードへ切り替わった後も、
      // grepReturnMode_はApplicationMode::ListBrowserのままであり、
      // 上のどの分岐にも該当しないためpathが空のままだった――
      // drawPromptLabel()/listBrowserView_自身のModeLineFieldsProvider
      // 向けに新設したgrepReturnListCategory_(項目443参照)をここでも
      // 再利用し、"FILE"/"DIR"を表示する。
      path = grepReturnListCategory_ == RingCategory::RecentFile ? "FILE" : "DIR";
    }
    // 2026-08-23修正、直接のユーザー報告("FGREPのモードラインで戻り値
    // の位置が(fgrep)と逆"): trailing2/trailing3は、このまさに「プログ
    // ラムのバッジ+戻り先」という形を共有する他の全てのモードに対して
    // 逆になっていた(SHELL自身の項目401の修正: trailing2="(zsh)"という
    // プログラムのバッジ、trailing3=戻り先のバッジ、常に自動的に右端
    // までパディングされる――trailing3自身のコメント参照)。プログラム
    // のバッジはバイト数のすぐ後(trailing2、MAN自身のプレーンな
    // "(man)"もまさにそのスロットに合わせている)に属し、戻り先のパス
    // は「最後」(trailing3)に属する。ここではtruncatedTrailingPath()
    // を通していない――trailing3にはそれ自身の切り詰めオプションが
    // 無い(それ自身のコメント参照: 余裕があれば全体を表示し、そうで
    // なければ完全に落とす)、これは、同じこのパスに対する「旧」
    // trailing2Kind::Path設定も同様にやっていたこと(完全なパスを表示
    // するために、あえて汎用的な16文字の末尾キャップから「外れて」
    // いた)である――ここで事前に切り詰めていたら、それより狭いとは
    // いえ、それからの実際の挙動の後退になっていたはずである。
    // 2026-08-28修正、直接のユーザー報告("戻り値に右端までのパディング
    // がない"、HISTORY.md:9473の一般的なルールを参照している: "基本的
    // に右端まで埋まっていないモードラインは全部不良"): これは、他の
    // 全てのモードのプロバイダとは異なり、明確に誤りだった――trailing3
    // (戻り先のパス)はここで描画される「最後の」フィールドなので、
    // renderModeLine()自身のfillTrailingToEdgeのロジックが本当の右端
    // までパディングする対象になる、他の場所のtrailing2と同じ仕組み。
    ContentView::ModeLineFields fields{name, std::to_string(grepView_.totalByteCount()),
                                        /*fillTrailingToEdge=*/true, /*rightWatermark=*/"", badge};
    fields.trailing3 = path;
    return fields;
  });
  // grepView_自身の空の結果のプレースホルダー("<pattern>: No String"、
  // "No String"は下線付き――ContentView::setEmptyPlaceholder()自身の
  // コメント参照)は、ここで静的にではなく、GrepMode::search()自身に
  // よって、検索ごとに動的に設定される(それが、たった今検索された
  // パターンを実際に知っている唯一の場所である)。

  // 2026-08-13: manView_/locateView_/duView_/archiveContentView_には、
  // 今までModeLineFieldsProviderが全く無かった――ユーザーの所感
  // ("モードラインの現状を見て...フォーマットはすでに決まってる...
  // 共有ルーチン化もできるのでは")がきっかけとなり、このApplicationが
  // 所有する全てのContentViewを、既に登録済みの他の8つのプロバイダに
  // 対して監査したところ、これら4つが本当に何も無い(位置テキストだけ
  // で、name/trailing/バッジが無い)ことが判明した――項目280-290が
  // それぞれ別々のビューで1つずつ見つけていたのと同種の隙間で、以前
  // は一度に「全て」を体系的にチェックしたことは無かった。`name`は
  // legacy自身のap->nameの永続性を反映している(PROMPT入場時点で
  // m_locate()/m_man()によってモード自身の名前文字列に設定され、結果
  // 画面がレンダリングされる前に二度と上書きされない――old/src/menu.c
  // のm_locate()を直接読んで確認済み、CUT/GREP自身の、下と上にある
  // リテラルな"CUT"/"EGREP"という名前についても既に独立に確立済み)
  // ――FileModeの素のファイル名のような、コンテンツから導出される
  // 名前では「ない」(legacyでは、Fileのap->nameは本当にファイル名で
  // ある、それはその1つのモードの遷移がそれをどう埋めるかにおける
  // 実際の違いであり、見落としではない)。`trailing`はcontentByteCount()
  // を再利用する――legacy自身のdisp_window()が、実質全てのモードの
  // 結果画面について、print_arg()のh.bp->size経由で表示する(menu.cの
  // メイン再描画ループ、モードごとの特別扱いではない)のと同じ汎用の
  // バイト数フィールドであり、上のcutContentView_について既に確立済み
  // でユーザーに検証済みである。これら4つのどれも、Directoryの
  // "(ls)"+"usilic v1.5b"やFileのバイトサイズ+ディレクトリのペアの
  // ような、trailing2/rightWatermarkに自然に対応するものを持たない
  // (ここではバイト数と組にする2つ目の概念が無い)ので、どちらも設定
  // されていない――これはcutContentView_自身の前例に合わせたもので
  // あり、部分的な実装ではない。Directory/File/CUTについてはそう
  // だったように、実際のCUIスクリーンショットに対してまだ独立には
  // 確認されていない(このポートには直接比較のために実行できるlegacy
  // バイナリが無い)、ソース由来のものとしてフラグ付けしてある――
  // ソースを読まずに当てずっぽうで作ったのではなく、この回を「机上の
  // 構想...実地検証必須」と自ら位置付けたユーザー自身の枠組みに従った、
  // それなりに確信度の高い一次案。
  // 2026-08-16修正: trailing2は空だった(「自然に対応するtrailing2は
  // 無い」という上の今は古くなったコメント参照)――legacy自身の
  // old/src/sub.cのprint_arg()のT_manケースは、同じ日に独立して
  // T_dirの"(ls)"と同じ"%d (%s)"というバイト数+ラベルの形を与えられ
  // た(CLAUDE.md項目305/306参照)、なので結局"(man)"が本当の対応物
  // であり、下のDirectoryModeのtrailing/trailing2の分割に合わせている
  // (2つの反転表示のセグメントの間の正しいプレーンなスペースの隙間
  // で既にレンダリングされることも確認済み、このペアについての
  // legacy自身の単一の走りでのレンダリングとは異なり――同じ回の
  // disp.cのdisp_window()の修正参照)。
  manView_.setModeLineFieldsProvider([this](int) -> ContentView::ModeLineFields {
    if (manMode_.currentPage().empty()) return {};
    // 2026-09-15修正、直接のユーザー指摘("戻り値は？MANもNGだった
    // ような…"): HELP.jp自身のmode-line図([6]="以前のモード名または
    // ファイル名...^Gでそこへ戻ります")に照らすと、MAN自身の画面は
    // これまでこのフィールドを一切持っていなかった――DU(trailing3=
    // currentPath())/LOCATE(trailing2="LOCATE: query")/ARC(trailing3=
    // currentArchive_)は同種の戻り値をとうに持っていたのに、MANだけ
    // 取り残されていた。今やMAN自身の戻り先はGroup1方針により常に
    // 基点のDirectoryなので、そこ(directoryMode_.currentDirectory())
    // をtrailing3へ表示する――DU自身の同じ無接頭辞の慣習(nameが既に
    // "MAN"だと言っているので"MAN: "接頭辞は付けない)に揃えた。
    // あわせてfillTrailingToEdgeもtrueへ(HISTORY.md:9473「基本的に
    // 右端まで埋まっていないモードラインは全部不良」――これも同じ
    // 見落としだった)。
    ContentView::ModeLineFields fields{"MAN", std::to_string(contentByteCount(manView_)),
                                        /*fillTrailingToEdge=*/true, /*rightWatermark=*/"",
                                        /*trailing2=*/"(man)"};
    // 2026-09-20再修正(直接のユーザー指摘、前回の対応は不十分だった):
    // 検索アクティブ中の^Gは検索解除のみでどこへも遷移せず、その場の
    // モード(MAN)へ留まる――なのでtrailing3には、実際に別の場所への
    // パスを装った値(previousModeName()の計算結果)ではなく、リテラルに
    // "MAN"(=このまま留まる)を出す。Shell自身の同じ分岐のコメント
    // 参照。name/trailing2の差し替えは維持。
    if (manView_.grepInPlaceActive() || manView_.grepNoMatchActive()) {
      fields.name = manView_.grepInPlaceLabel();
      fields.trailing2 = manView_.grepInPlaceProgramBadge();
      fields.trailing3 = "MAN";
      return fields;
    }
    // 2026-09-19四訂正、直接のユーザー提案("フラグはなるべく使いたく
    // ない...旧版のようにスタックへの積み降ろしでは対応できない？"):
    // application.hpp自身のmanReturnStack_宣言コメント参照。フラグに
    // よる特別扱いはもう不要――manReturnStack_.back()がGrepの時、
    // previousModeName(Grep)自身が(すぐ下に新設した通り)
    // "<GREP種別>: <pattern>"を返すようになったので、他の全ての
    // 戻り先(File/Directory)と全く同じ経路で済む。
    // 2026-09-21、直接のユーザー依頼(MAN一覧の仕様統一): 名前は開いた
    // ページ名、戻り値はリテラルな"MAN"(現在地の情報は内部で保持して
    // いるが表示しない)。Grep結果へ着地した場合だけ従来の形のまま。
    if (manReturnStack_.back() != ApplicationMode::Grep) {
      fields.name = manMode_.currentPage();
      fields.trailing3 = "MAN";
      return fields;
    }
    fields.trailing3 = previousModeName(manReturnStack_.back());
    return fields;
  });
  // 2026-08-14修正、それから同日に訂正: 実際の報告("^J^L→'.txt'
  // [return]→戻り値「LOCATE: .txt」が無い")により、GREP自身の結果
  // 画面(その戻り値/trailing2の慣習は、マッチ経由でどこかへ「着地」
  // すると検索文字列を表示する、DirectoryMode::setSearchAnnotation()の
  // 呼び出し元参照)とは異なり、LOCATE自身の結果画面には、実際に何が
  // 検索されたかを表示する場所がどこにも無いことが指摘された。
  // 「最初の」修正は、"LOCATE: <query>"を`name`フィールド自体に折り
  // 込んでいた(素のリテラル"LOCATE"を置き換える形で)――直接のユー
  // ザーの訂正(スクリーンショット=aw)は、これが間違った「スロット」
  // だと指摘した: 「LOCATE: .txt」はモード名ではなく戻り値、モード名
  // は「LOCATE」で。`name`は素のリテラルに戻された;同じ"MODE: query"
  // という文字列は、代わりに今`trailing2`に入っており、他の全ての
  // 検索結果画面自身の戻り値の慣習にまさに一致している(LOCATE自身の
  // 結果画面に適用されただけで、最終的なDirectory/File宛先だけでは
  // ない、openSelectedLocateResult()の同一の文字列参照)。
  // 2026-08-16修正: fillTrailingToEdgeがfalseだったため、trailing2
  // ("LOCATE: <query>")は、行の右端までスペースでパディングされる
  // のではなく、それ自身のテキストのすぐ後で止まっていた――実際の
  // 報告+機能している例との直接比較("戻り値がディレクトリ名の時は
  // 左詰め、空きもちゃんとスペースで埋めるてるのに、なぜ「LOCATE:
  // .txt」でそうならないのか")により、他の「全ての」ディレクトリ形式
  // のtrailing2(DirectoryMode自身の"(ls)"/検索注釈のフォールバック、
  // GrepModeの検索元ディレクトリ、等)は既にこのパディングを得ている
  // が、これが唯一の不整合な取り残しだったと指摘された。`trailing`
  // (バイト数)に関わらず安全: trailingをパディングする分岐は、
  // trailing2が「空」の時にだけ発火するが、ここでは決してそうでは
  // ない。
  locateView_.setModeLineFieldsProvider([this](int) -> ContentView::ModeLineFields {
    if (locateMode_.query().empty()) return {};
    // 2026-09-19四訂正、直接のユーザー報告+提案("あらたな重複発見"
    // から「フラグはなるべく使いたくない...スタックへの積み降ろし
    // では対応できない？」まで): クエリ文字列はもう表示せず、実際の
    // ^Gの戻り先(application.hpp自身のlocateReturnStack_宣言コメント
    // 参照)を表示する。previousModeName(Grep)自身が"<GREP種別>:
    // <pattern>"を返すようになった(すぐ下に新設)ので、フラグによる
    // 特別扱いは不要――manView_と全く同じ形。
    //
    // 2026-09-20再訂正、直接のユーザー報告("...※※※※             "
    // より"...※※※※※※※※※※※"でよろしく、理由も無いのになぜ
    // パディングするんだろう"): この値は以前trailing2に入っていた
    // ため、trailing2Kindのデフォルト(QueryEcho)経由でtruncateFieldByKind()
    // が「実際の余裕(budget)に関わらず16文字で頭打ち」にしてしまって
    // いた――同じ値を(man)同種のMAN/DU自身のtrailing3(withTruncatedTrailing3()
    // 経由、実際の利用可能幅をそのまま使い切る、上限16文字などという
    // 恣意的な制限は無い)へ移すのを、この値がまだtrailing2にとどまって
    // いた2026-08-14当時の一時的な都合(trailing3という概念自体が
    // まだ存在しなかった)以来ずっと見送っていた――MAN/DUと全く同じ
    // 形に揃える。
    ContentView::ModeLineFields fields{"LOCATE", std::to_string(contentByteCount(locateView_)),
                                        /*fillTrailingToEdge=*/true, /*rightWatermark=*/"", /*trailing2=*/""};
    // 2026-09-20再修正(Shell自身の同じ分岐のコメント参照、直接のユーザー
    // 指摘): 検索アクティブ中の^Gは検索解除のみでどこへも遷移しないため、
    // trailing3にはリテラルに"LOCATE"(=このまま留まる)を出す。
    if (locateView_.grepInPlaceActive() || locateView_.grepNoMatchActive()) {
      fields.name = locateView_.grepInPlaceLabel();
      fields.trailing2 = locateView_.grepInPlaceProgramBadge();
      fields.trailing3 = "LOCATE";
      return fields;
    }
    fields.trailing3 = previousModeName(locateReturnStack_.back());
    return fields;
  });
  duView_.setModeLineFieldsProvider([this](int) -> ContentView::ModeLineFields {
    if (duMode_.currentPath().empty()) return {};
    // 2026-09-10修正、直接のユーザー報告("^Jd直後の画面に戻り値が
    // 無い"): 以前はDUモード自身の画面が、集計対象のフォルダーを
    // 一切表示していなかった(nameは常にリテラルな"DU"のみ)――
    // Directory/File/Archive等、他の全モードが自分自身の「今どこに
    // いるか」を必ず表示するのとは対照的だった。
    //
    // 2026-09-14再修正、直接のユーザー報告(実機での正解モードライン
    // 提示付き): "DU" "0001/1145 TOP" "85332"
    // "/Users/haa15030/git/usilic/"――このDU自身の画面では、名前
    // フィールドが既に"DU"と言っているので、trailing3は生のパスの
    // まま("DU: "接頭辞は付けない、ArchiveMode自身のtrailing3=
    // currentArchive_という無接頭辞の慣習と同じ、archive_mode.cpp
    // 参照)。"DU: "付きの表記が要るのは、DUで開いたエントリを
    // Directory/Fileモード側で表示する「戻り先」注釈の方
    // (openSelectedDuEntry()参照、あちらはnameが"DU"を名乗っていない
    // 別モードなのでラベルが要る)。
    //
    // 「パディングが無い」(HISTORY.md:9473「基本的に右端まで埋まって
    // いないモードラインは全部不良」そのもの): 前回の修正は
    // fillTrailingToEdge=falseのまま出していた。LOCATE自身の同種の
    // プロバイダ(すぐ上)に合わせtrueへ。
    //
    // 2026-09-15修正、直接のユーザー訂正("DU画面でハイライト行を
    // 移動させると戻り値も変化する。正しくは^Jdを実行した場所
    // （/Users/haa15030/git/usilic/）のままでいい"): 直前の版は
    // duMode_.selectedEntry()を使い、カーソル移動のたびにduEntryFolder()
    // で再計算していたが、これは撤回された仕様だった――DU自身の画面の
    // trailing3は、集計対象のルート(duMode_.currentPath())に固定
    // でよく、ハイライト中のエントリに連動させる必要は無い(その連動は
    // openSelectedDuEntry()側――実際にエントリを開いた「後」の戻り先
    // 表示でだけ必要、setSearchOrigin()自身のコメント参照)。
    //
    // 2026-09-19六訂正、直接のユーザー報告+実機の正解モードライン
    // ("^Jd→戻り値不良。正しくは/Users/haa15030/git/usilic/CLAUDE.md"
    // ――DUがFileから発動された場合の例、CLAUDE.mdはduReturnStack_の
    // 恒久的な基点): 上のコメントが説明する「集計対象のルートを表示」
    // という2026-09-10/09-14/09-15の3度にわたる決定を、ユーザー自身が
    // 今回さらに撤回する。manView_/locateView_と全く同じ形に揃え、
    // 「集計対象」ではなく「実際の^Gの戻り先」(duReturnStack_)を表示
    // する――GREPの結果からマッチへ着地していればduReturnStack_.back()
    // がGrepになり(previousModeName(Grep)経由で"GREP: <pattern>")、
    // そうでなければ恒久的な基点(File/Directory)の生のパス。
    ContentView::ModeLineFields fields{"DU", std::to_string(contentByteCount(duView_)),
                                        /*fillTrailingToEdge=*/true, /*rightWatermark=*/"",
                                        /*trailing2=*/""};
    // 2026-09-20再修正(Shell自身の同じ分岐のコメント参照、直接のユーザー
    // 指摘): 検索アクティブ中の^Gは検索解除のみでどこへも遷移しないため、
    // trailing3にはリテラルに"DU"(=このまま留まる)を出す。
    if (duView_.grepInPlaceActive() || duView_.grepNoMatchActive()) {
      fields.name = duView_.grepInPlaceLabel();
      fields.trailing2 = duView_.grepInPlaceProgramBadge();
      fields.trailing3 = "DU";
      return fields;
    }
    fields.trailing3 = previousModeName(duReturnStack_.back());
    return fields;
  });
  // 2026-09-20新設、Command::StartUnderlineView自身の宣言コメント
  // 参照(`^Ou`)。Du/Locateとは異なり、この一覧は常にDirectoryから
  // 直接発動し(検索プロンプトも、他モード経由の着地も無い)、
  // duReturnStack_/locateReturnStack_のような「^Gの実際の戻り先が
  // 途中で変わり得る」複雑さが無いので、trailing3は素直に
  // directoryMode_.currentDirectory()(この一覧の唯一の戻り先)を
  // そのまま出す――ArchiveMode自身のtrailing3=currentArchive_という
  // 無接頭辞の慣習と同じ理屈(duView_自身の同種のコメント参照)。
  underlineView_.setModeLineFieldsProvider([this](int) -> ContentView::ModeLineFields {
    if (underlineMode_.entries().empty()) return {};
    ContentView::ModeLineFields fields{"USILIC", std::to_string(contentByteCount(underlineView_)),
                                        /*fillTrailingToEdge=*/true, /*rightWatermark=*/"",
                                        /*trailing2=*/""};
    fields.trailing3 = directoryMode_.currentDirectory();
    return fields;
  });
  // archiveContentView_は、すぐ上で使われている素のバイト数だけの形
  // ではなく、fileView_自身の形(展開された単一のエントリの生の
  // コンテンツ、name=リテラルなモードの単語ではなく素のファイル名
  // ――なぜこれが、本物のファイルシステムではなくアーカイブの中から
  // 得られているだけの、正真正銘の「ファイル」なのかについては
  // openSelectedArchiveEntry()自身のコメント参照)を反映する:
  // trailing=バイトサイズ(fillTrailingToEdge、fileView_と同じ慣習)。
  //
  // 2026-08-19修正、実際の報告+
  // スクリーンショット: "モードラインは下記でよろしく。「国際報道
  // ２０..._web_fl.ts」「1/1 END」「111」「ARC: /Users/...rc/utf8.tar」"。
  // `entry->name`はアーカイブ相対のパス(例えば"utf8/国際報道...")で
  // あり、このコメント自身の「元々の」主張とは違って、実際には素の
  // ファイル名ではない――ディレクトリのネストが無いフラットな
  // アーカイブ(このポート自身の小さなテストフィクスチャ)についてのみ
  // 真であり、ここでの再現アーカイブのような実際のものについては
  // 決してそうではない。basename()は、FileMode自身のプロバイダが実際
  // のファイルシステムパスについて既にそうしているのと同じ方法で、
  // その接頭辞を取り除くようになった。trailing2は、単なる素のパス
  // ではなく、今や"ARC: <path>"(固定のリテラルなラベルとアーカイブ
  // 自身のパスを組み合わせた単一のハイライトされた走り)になっている。
  //
  // splitLayoutWhenCrowded(2026-08-19の同日の追跡、直接のユーザー
  // 提案: "モードラインの長過ぎモード名および戻り値対処としては...
  // 『「1/1 END」「111」』をまずは中央へ→残りのエリア半分ずつを
  // 「モード名」「戻り値」で分け合った上で..."): このプロジェクト
  // 自身のテストアーカイブの中の実際のCJKファイル名は、name+trailing2
  // を合わせるとtrailing(バイト数)と一緒には決して収まらないほど
  // 長い――fitModeLineFields()の通常の段階は、何かの切り詰め版を表示
  // する代わりに、trailingを完全に黙って落としていた。置き換えとなる
  // レイアウトについてはModeLineFields::splitLayoutWhenCrowded/
  // renderSplitModeLine()自身のコメント参照。
  archiveContentView_.setModeLineFieldsProvider([this](int) -> ContentView::ModeLineFields {
    const ArchiveEntry *entry = archiveMode_.selectedEntry();
    if (!entry) return {};
    std::string basename = std::filesystem::path(entry->name).filename().string();
    // ここではtrailing2Kind=Path(QueryEchoではない)なので、アーカイブ
    // 自身のパスが長い場合でも、それなりに長い末尾を依然として表示
    // できる――なぜパスがQueryEchoの固定16文字の末尾キャップから
    // 外れているのかについてはContentView::TrailingKind自身のコメント
    // 参照。
    ContentView::ModeLineFields fields{basename,
                                        std::to_string(contentByteCount(archiveContentView_)),
                                        /*fillTrailingToEdge=*/true,
                                        /*rightWatermark=*/"",
                                        "ARC: " + archiveMode_.currentArchive(),
                                        /*trailingPlain=*/false,
                                        /*trailing2RightJustified=*/false,
                                        /*trailing2Kind=*/ContentView::TrailingKind::Path};
    fields.splitLayoutWhenCrowded = true;
    // 2026-09-11追加、直接のユーザー依頼("^Sと^[iをFILE/DIR以外へも
    // 反映してほしい"): ManMode等自身の同じ分岐のコメント参照。name
    // (通常はエントリ自身のbasename)を"GREP"が乗っ取るので、その
    // 見えなくなった識別情報をtrailing3へ退避する(FileMode自身の
    // currentFile_と同じ理屈)。
    if (archiveContentView_.grepInPlaceActive() || archiveContentView_.grepNoMatchActive()) {
      fields.name = archiveContentView_.grepInPlaceLabel();
      fields.trailing2 = archiveContentView_.grepInPlaceProgramBadge();
      fields.trailing3 = entry->name;
      return fields;
    }
    return fields;
  });

  // usilicrc自身のGREPセクション(2026-08-05修正、2026-08-28に改訂):
  // 実際のCUIスクリーンショットと実際のlegacy-usilicへの直接のPTYテスト
  // により、^J^S(EGREP)と^J^C(CUTのregex亜種)は、完全な新規インス
  // トールでも直接、空でない候補リストに入ることが確認された――
  // usilicrc自身のGREPセクションからのまさに10個のパターン。「元々の」
  // 修正(2026-08-05)は、構築時に無条件に全てのシード値をRecentEgrep
  // Patternへ積むことでそれを実現した――単純だが、legacy自身が別々に
  // 保っている2つのものを混同していた(old/src/str.c自身のconf_grep():
  // usilicrcのパターンは静的なLIST *lgへロードされ、hp->ss.p.hegrep
  // (実際に検索が「投入」された時だけsave_history()が増やすring、
  // menu.c:833)とは完全に別個である)。シード値をRecentEgrepPattern
  // へ積むことで、^R(M_hist、legacy「自身」の「実際の使用履歴」
  // ビュー)は、何も検索されたことが無いセッションでも、これらの静的
  // なパターンを表示してしまっていた――実際のユーザー報告
  // ("登録済みのパターンは履歴ではなく検索に使用するための文字列候補
  // に過ぎない...初めて発動したEGREPで検索文字列一覧が出た時点では、
  // いくら^Rしたって『No String』なのは当たり前")が、この混同を直接
  // 捕まえた。シード投入はここでは削除され、RecentEgrepPattern自体は
  // 本物の使用履歴だけのままにしている――legacy自身のlg/hegrepの分割
  // に合わせ、そこでの^Rがまさに示すべきとおり、実際の検索が起きる
  // までは空である。
  // (2026-09-11追記): grepSeedPatterns()自体、それを使っていたEGREP/
  // CUT-Regexの直接入力の候補リストがstartGrepPrompt()自身のコメント
  // 参照の統一で廃止されたのに伴い、完全に削除した――usilicrcの
  // GREPセクションは引き続き構文としては認識される(既存設定ファイル
  // との互換性のため)が、内容は読み飛ばされるだけで、もうどこにも
  // マージされない。
}

std::string Application::ringCategoryLabel(RingCategory category) const {
  switch (category) {
    case RingCategory::ShellCommand:
      // 2026-08-04訂正時点ではlegacyのK-50(^J^R、M_cmd、" COMMAND")、
      // usilicrc自身の286行目("COMMAND : Select program executed at
      // SHELL")に従い"COMMAND"だった。"HISTORY"では「ない」――そちらは
      // M_hist自身の時系列ビューのトグル(ToggleHistoryViewのコメント
      // 参照)のために予約されており、これはどのRingCategoryが閲覧
      // されているかとは直交し、これも含めて「どれにでも」適用される。
      // このポートの以前の版はこの2つを混同していた(どちらも漠然と
      // 「履歴」らしい形をしていたため)、単なる^R自身の無関係な挙動に
      // ついての直接のユーザーの説明がその混同を浮かび上がらせた後、
      // 2026-08-04に訂正された。
      //
      // 2026-09-20改訂、直接のユーザー依頼(^Rの再設計、"1つの共有ring
      // が全部をまとめて記録しているのなら、いままでCOMMANDとして
      // 分類されていたカテゴリー名をPROGRAMに置き換えるだけで足りる"):
      // ^J^R自体を廃止し^J^Pへ統合した(Command::StartCommandHistory
      // 自身の削除コメント参照)ことで、このカテゴリの「ソート済み」
      // ビューの主要な入口はPROGRAM(^Jp/^J^P、PromptKind::
      // ProgramCommand)になった――promptLabelText(ProgramCommand)も
      // 既に"PROGRAM"を返す(この関数と表示上分岐する2つの別経路が
      // 同じ概念に別の名前を与えてしまう食い違いを避けるため、こちら
      // も揃える)。SHELL自身の素の^R(startShellHistoryRecall())は
      // 引き続きこの同じラベルを経由するので、"PROGRAM"呼びは
      // SHELLの文脈からも見えるが、記録内容自体(実行したシェル
      // コマンド/cd/PROGRAMモードのコマンドの共有ring、RingCategory::
      // ShellCommand自身の宣言コメント参照)が元々混在している以上、
      // どちらの入口から見ても同じ1つの概念として一貫した名前で
      // 見せる方が、入口ごとに違う名前(COMMAND対PROGRAM)で同じ
      // データを指すより分かりやすいと判断した。
      return "PROGRAM";
    case RingCategory::RecentFile:
      return "FILE";
    case RingCategory::RecentDirectory:
      return "DIR";
    case RingCategory::RecentManPage:
      return "MAN";
    case RingCategory::RecentLocateQuery:
      return "LOCATE";
    case RingCategory::RecentFgrepPattern:
      return "FGREP";
    case RingCategory::RecentEgrepPattern:
      return "EGREP";
    case RingCategory::Usilic:
      return "USILIC";
  }
  return "";
}

// コンストラクタとhandleResize()の両方で共有される: 所有している
// 全てのContentViewのビューポートとプロンプトフィールドを、display_
// の「現在の」ジオメトリに合わせてサイズ調整する。最下行はプロンプト
// のテキスト入力(search/cut/man/locate/duのパターン)のために予約
// されている――legacyの相当物は、まさにこのためにdisp_window()が
// 手を付けずにおいたモードライン行である(なぜその行のレンダリング
// もまたContentViewの仕事ではないのかについてはContentView自身の
// クラスコメント参照)。
void Application::layoutViews() {
  // 2026-09-12追加、項目⑫: モードラインが非表示の間は、通常その行の
  // ために予約している1行を全ビューへ返す(display_.modeLineVisible()
  // 自身のコメント参照)。promptEditor_の配置(この関数末尾)は意図的に
  // 変えていない――legacyから、モードライン行自身をプロンプト入力欄
  // としても兼用する慣習を引き継いでおり、モードライン非表示中に
  // プロンプトが起動された場合は、そのビューの最終行を一時的に
  // 借りるだけで済む(プロンプト終了後の通常のredraw()で元に戻る)。
  int contentHeight =
      display_.modeLineVisible() ? std::max(1, display_.rows() - 1) : std::max(1, display_.rows());
  directoryView_.setViewport(0, contentHeight);
  fileView_.setViewport(0, contentHeight);
  grepView_.setViewport(0, contentHeight);
  cutView_.setViewport(0, contentHeight);
  cutContentView_.setViewport(0, contentHeight);
  manView_.setViewport(0, contentHeight);
  locateView_.setViewport(0, contentHeight);
  archiveView_.setViewport(0, contentHeight);
  archiveContentView_.setViewport(0, contentHeight);
  duView_.setViewport(0, contentHeight);
  underlineView_.setViewport(0, contentHeight);
  listBrowserView_.setViewport(0, contentHeight);
  copyView_.setViewport(0, contentHeight);
  epubVerticalView_.setViewport(0, contentHeight);
  epubTocVerticalView_.setViewport(0, contentHeight);
  promptEditor_.setField(display_.rows() - 1, 0, std::max(1, display_.columns()));
}

namespace {
// 2026-08-30、直接のユーザー依頼("仮にmv等でファイルが存在しなくても
// 在籍していたディレクトリ（が無い場合は親）で起動して欲しい"):
// pathの親ディレクトリから遡って、最初に実在する祖先ディレクトリを
// 返す。lastOpenNameは常に絶対パスとして保存される(FileMode::
// currentFile()/DirectoryMode::currentDirectory()/ArchiveMode::
// currentArchive()いずれも、std::filesystem::absolute()経由で構築
// された既存の絶対パスをそのまま使い続けているだけ)想定なので、
// 念のための保険として相対パスならfallbackへ逃げる(相対パスのまま
// parent_path()を辿ると空パスで不動点に達し、無限ループの危険は
// 無いが正しい祖先を指す保証も無いため)。絶対パスなら"/"自身が
// 必ず実在するのでループは必ず止まる(parent==pになった時点で
// 不動点=ルートに達したと判定して抜ける、C++17のparent_path()の
// 仕様通り)。
std::string nearestExistingAncestorDirectory(const std::string &path, const std::string &fallback) {
  if (path.empty()) return fallback;
  std::filesystem::path p = std::filesystem::path(path).parent_path();
  if (!p.is_absolute()) return fallback;
  while (true) {
    std::error_code ec;
    if (std::filesystem::exists(p, ec) && !ec) return p.string();
    std::filesystem::path parent = p.parent_path();
    if (parent == p) break;  // ルートで不動点――これ以上遡れない
    p = parent;
  }
  return fallback;
}
}  // namespace

bool Application::start(const std::string &directory, bool consultHistory) {
  currentMode_ = ApplicationMode::Directory;
  return directoryMode_.enterDirectory(directory, consultHistory);
}

std::string Application::currentResumableName() const {
  if (currentMode_ == ApplicationMode::File) return fileMode_.currentFile();
  // 2026-09-29追加、直接のユーザー報告("同ファイル参照中(戻り値は「CUT: ^<」)
  // そのまま^X^C^Mでusilicを再起動しても同じ画面にならない"): ファイルCUT
  // (^Cf/FILE履歴の「パス "パターン"」)の一覧・区間の中身なら、その合成
  // エントリを保存する――startResumingSession()が開き直す。
  if ((currentMode_ == ApplicationMode::Cut || currentMode_ == ApplicationMode::CutContent) &&
      cutOrigin_ == CutOrigin::File && !cutPositionKey_.empty())
    return cutPositionKey_;
  // archiveParentStack_.empty()の場合だけ保存する理由:
  // ArchiveMode::currentArchive()は、ネストしたアーカイブへ降下した
  // 後は本物の実在パスではなく使い捨てのmkdtemp()一時パス(次回起動
  // 時にはまず存在しない)を指すようになる――descendToNestedArchive()
  // 自身のコメント参照。ネストしていない(トップレベルのアーカイブを
  // 見ている)間だけ、currentArchive()は最初にloadArchive()へ渡した
  // 本物のファイルパスと一致し続け、そのまま安全に保存・復元できる。
  if (currentMode_ == ApplicationMode::Archive && archiveParentStack_.empty()) {
    const std::string &archive = archiveMode_.currentArchive();
    return isEpubFile(archive) ? archive + "/" : archive;  // ARCで見ていた.epub(openEpubAsArchive()参照)
  }
  return directoryMode_.currentDirectory();
}

bool Application::startResumingSession(const std::string &lastOpenName, const std::string &fallbackDirectory,
                                        bool consultHistory) {
  // 2026-09-25追加、直接のユーザー報告("INFOで^X^C^Mすると次回起動時
  // INFOではなく~/.usilic/usilic（平文テキスト）が表示される"): INFOは
  // bannerFilePath()へ書き出した合成ファイルをFILEモードで見せている
  // だけなので、INFO表示中に終了するとcurrentResumableName()がその
  // パスを返し、次回はただのテキストとして開き直されていた。ここで
  // 見分け、戻り先ディレクトリ(直近のディレクトリ、lastOpenNameが空の
  // 場合と同じ扱い)を開いてからINFOを作り直す。
  if (!lastOpenName.empty() && lastOpenName == bannerFilePath()) {
    if (!startResumingSession("", fallbackDirectory, consultHistory)) return false;
    executeShowUsilicBanner();
    return true;
  }
  std::error_code ec;
  // currentResumableName()自身の同日のコメント参照: ファイルCUTは、ファイルの
  // ディレクトリの(ls)からFILE履歴で開き直したのと同じ形で作り直し、区間の
  // 中身を見ていたならそれも開く(saveResumableCutState()が保存した行へ)。
  if (auto composite = parseCutFileHistoryEntry(lastOpenName)) {
    const std::string &cutFile = std::get<0>(*composite);
    if (std::filesystem::is_regular_file(cutFile, ec) && !ec) {
      std::string parent = std::filesystem::path(cutFile).parent_path().string();
      if (parent.empty()) parent = fallbackDirectory;
      if (directoryMode_.enterDirectory(parent, consultHistory)) {
        currentMode_ = ApplicationMode::Directory;
        if (!openRecentFileEntry(lastOpenName)) currentMode_ = ApplicationMode::Directory;  // 開けなければ(ls)のまま
        return true;
      }
    }
  }
  // ARCで見ていた.epub(「パス/」、openEpubAsArchive()参照)
  if (lastOpenName.size() > 1 && lastOpenName.back() == '/' && isEpubFile(lastOpenName.substr(0, lastOpenName.size() - 1))) {
    std::string epub = lastOpenName.substr(0, lastOpenName.size() - 1);
    if (std::filesystem::is_regular_file(epub, ec) && !ec &&
        directoryMode_.enterDirectory(std::filesystem::path(epub).parent_path().string(), consultHistory)) {
      if (!openEpubAsArchive(epub)) currentMode_ = ApplicationMode::Directory;
      return true;
    }
  }
  if (!lastOpenName.empty() && std::filesystem::is_regular_file(lastOpenName, ec) && !ec) {
    std::string parent = std::filesystem::path(lastOpenName).parent_path().string();
    if (parent.empty()) parent = fallbackDirectory;
    // .epubはEPUBモードで(ARCHIVEの別名で.zip扱いなので、loadArchive()より先に)。
    // 2026-10-01(openEpub()の同日のコメント参照): (ls)や横書きの途中の画面を見せないよう、
    // 画面を消して最下行に"Reading Kat.epub..."を出し、開き終えるまで出力を止める。
    if (isEpubFile(lastOpenName) && !configuration_.isExec(lastOpenName)) {
      bool wasEnabled = display_.outputEnabled();
      if (wasEnabled) {
        display_.clearScreen();
        display_.moveCursor(std::max(0, display_.rows() - 1), 0);
        display_.write("Reading " + std::filesystem::path(lastOpenName).filename().string() + "...");
        display_.flush();
      }
      display_.setOutputEnabled(false);
      bool entered = directoryMode_.enterDirectory(parent, consultHistory);
      if (entered) {
        currentMode_ = ApplicationMode::Directory;
        if (!openEpub(lastOpenName)) currentMode_ = ApplicationMode::Directory;
      }
      display_.setOutputEnabled(wasEnabled);
      if (entered) {
        if (wasEnabled) {
          activeView().redraw();
          activeView().syncVisibleCursor();
          display_.flush();
        }
        return true;
      }
    }
    if (directoryMode_.enterDirectory(parent, consultHistory)) {
      // loadArchive()を先に試す(loadFile()より先): アーカイブファイル
      // 自身もstd::filesystem::is_regular_file()には該当するので、
      // openSelectedEntry()自身の判定順序(アーカイブ判定が通常
      // ファイルとしての読み込みより優先される)に合わせる。
      if (archiveMode_.loadArchive(lastOpenName)) {
        archiveParentStack_.clear();  // openSelectedEntry()自身のコメント参照――全く新しいトップレベルの入場
        currentMode_ = ApplicationMode::Archive;
        return true;
      }
      if (fileMode_.loadFile(lastOpenName)) {
        currentMode_ = ApplicationMode::File;
        fileReturnMode_ = ApplicationMode::Directory;  // openSelectedEntry()自身のコメント参照
        return true;
      }
      // アーカイブとしてもプレーンファイルとしても開けなかった(壊れた
      // アーカイブ等)――既に入場済みのparentのままDirectoryモードで
      // 構わない。
      return true;
    }
  }
  // lastOpenName自体が実在しない(mv/rm等)場合でも、その在籍していた
  // ディレクトリ(既に無ければ最も近い実在する祖先)で起動する。
  //
  // 2026-09-16追加、実機報告("src/debian-key...tar.xzの^[^Mで^G中断
  // 不能により別のターミナルでusilic^Mするとkillはされるものの~/
  // からの開始となる（src/が正解。...DIR履歴への登録が必要？）"):
  // lastOpenNameはSessionState::lastOpenNameとして「終了時にしか」
  // 保存されない(main.cpp自身のconsultHistoryOnStart分岐のコメント
  // 参照)ため、`kill -9`(2秒待っても応答無しの旧プロセスに対する
  // 起動ポリシーのkill機構自身のフォールバック)で終了したセッション
  // の後は常に空のままになる。その場合、これまでは無条件に
  // fallbackDirectory(呼び出し元シェルの起動時cwd――今回のように別
  // ターミナルから起動すれば、直前のセッションの在籍先とは何の関係
  // も無い)へ落ちていた。一方DirectoryMode::enterDirectory()自身の
  // history_.pushRecent(RingCategory::RecentDirectory, ...)は、
  // History::setOnRingPushed()経由で訪問の「たびに」ライブ書き込み
  // される(kill -9で失われない)ので、lastOpenNameが空の場合は、
  // これの直近訪問先(実在すれば)をfallbackDirectory自体より優先する。
  std::string fallback = fallbackDirectory;
  if (lastOpenName.empty()) {
    const auto &recentDirs = history_.recentEntries(RingCategory::RecentDirectory);
    if (!recentDirs.empty()) {
      std::error_code existsEc;
      if (std::filesystem::exists(recentDirs.back(), existsEc) && !existsEc) fallback = recentDirs.back();
    }
  }
  return start(nearestExistingAncestorDirectory(lastOpenName, fallback), consultHistory);
}

bool Application::startPagerFileList(const std::vector<std::string> &paths) {
  if (paths.empty()) return false;
  // 2026-09-04追加、直接のユーザー依頼("usilic ~/git/usilic/src/
  // HELP.txt をサポートして"): HELP.txt/QUICK.txt(isHelpMarkupPagerFile()
  // 自身のコメント参照)は、下のloadStream()(遅延読み込み、<indentN>/
  // <i>/<b>/<u>markupをリテラルなタグのまま表示してしまう)ではなく
  // FileMode::loadFile()(全読み込み+file_mode.cpp自身のreadAndWrap()
  // 経由でのexpandHelpMarkup()展開)へ振り分ける。ページャー専用の
  // 複数ファイルナビゲーション(n/p/x/f)・^Gの no-op化は、単発の
  // 参照文書CLI起動という用途では実質恩恵が無いための割り切り
  // ――pagerFileList_を空のままにしておくことで、この起動はもはや
  // 「ページャーモード」を名乗らない(hasActiveStream()がfalseになる
  // ので、^Gは通常のFileモードと同じくディレクトリへ戻る)。
  if (paths.size() == 1 && isHelpMarkupPagerFile(paths.front())) {
    if (!fileMode_.loadFile(paths.front())) return false;
    currentMode_ = ApplicationMode::File;
    fileReturnMode_ = ApplicationMode::Directory;
    pagerFileList_.clear();
    pagerFileIndex_ = 0;
    activeView().redraw();
    return true;
  }
  // FileMode::loadFile()ではなく::open()(2026-08-25): CLIのページャの
  // 起動は遅延ロードする――FileMode::loadStream()自身のコメント参照
  // ――なのでこれは、loadFile()がそうするようにファイル全体を積極的
  // に読むのではなく、loadStream()に既に開かれたfdを渡す。
  int fd = ::open(paths.front().c_str(), O_RDONLY);
  if (fd < 0) return false;
  currentMode_ = ApplicationMode::File;
  fileReturnMode_ = ApplicationMode::Directory;  // fileMode_.hasActiveStream()の間は未使用(その時Backはno-op、
                                                  // それ自身のhandleCommand()分岐参照)、一貫性のため残してある
  // 2026-09-06追加、直接のユーザー報告("80x24ターミナルだと開いた
  // 直後「00001/00024」のはずだが「00001/00050」になっている...
  // 最初の1画面は瞬時に出てほしい"): 初期予算はrows*2(画面2枚分)
  // ではなくrows(画面1枚分)にする――独立したCLIページャー起動
  // (pagerReturnsToShell()ではない、ここも含む3箇所)は元々ペーシング
  // の対象外で瞬時に全部読み込む(このコメントの「瞬時に」自体は既に
  // 満たされている)ため、この変更で困るのはあくまで「開いた直後の
  // 総行数の見た目」だけ――rows*2のままだと、開いた瞬間から画面外の
  // 2画面目分まで数字上は既に読み込み済みに見えてしまい、実際に
  // 見えている範囲(1画面)と食い違っていた。スクロールして画面外へ
  // 出た分の先読みは、以後Application::pollPagerExitNotice()自身の
  // 「topIndex_が動いた」判定が引き続きrows*2で行うので、ここを
  // rowsに縮めても後続のスクロールの先読み量自体は変わらない。
  fileMode_.loadStream(fd, /*ownsFd=*/true, paths.front(), display_.rows());
  pagerFileList_ = paths;
  pagerFileIndex_ = 0;
  // start()自身のDirectoryMode::enterDirectory()(それ自身が再描画
  // する)とは異なり、FileMode::loadStream()/loadFile()はそうしない
  // ――他の「全ての」呼び出し元は、キー入力自身のhandleCommand()の
  // 内部からそれらに到達しており、それを取り囲むメインループの
  // イテレーションが、どのみちその後に一度は既に再描画する。これは
  // main.cpp自身の「一度限りの」起動時の呼び出しであり、そのループの
  // 完全に外側にあるので、この明示的な呼び出しが無ければ最初の
  // フレームは決して描画されないことになる(2026-08-25、実際のPTY
  // キャプチャがまさにそれを捕まえた: ロードされたファイルのコンテンツ
  // とモードラインはメモリ上ではどちらも正しかったが、端末は
  // TerminalDisplay自身のセットアップが残していったどんな空白の
  // 初期フレームであれ、それを表示し続けていた)。
  activeView().redraw();
  return true;
}

bool Application::startPagerStdin() {
  currentMode_ = ApplicationMode::File;
  fileReturnMode_ = ApplicationMode::Directory;
  pagerFileList_.clear();
  pagerFileIndex_ = 0;
  // fd 0自体であり、所有はしていない(プロセスの途中でそれを閉じる
  // のはこのクラスの仕事ではない)――これがキーボード入力とどうして
  // 決して衝突しないのかについてはFileMode::loadStream()自身のコメント
  // 参照(KeyInputは別途開かれた"/dev/tty"から読む)。displayNameは無い
  // (legacyはパイプされたソースについては空のnameフィールドを表示
  // する、PTYキャプチャで確認済み)。
  // startPagerFileList()自身の同日の同種の修正のコメント参照
  // ("00001/00050"ではなく"00001/00024"にしてほしいという直接の
  // ユーザー報告)――ここも初期予算はrows*2ではなくrowsにする。
  bool ok = fileMode_.loadStream(0, /*ownsFd=*/false, "", display_.rows());
  activeView().redraw();  // これがなぜここで必要なのかについてはstartPagerFileList()自身のコメント参照
  return ok;
}

bool Application::navigateToPagerFile(int newIndex) {
  if (currentMode_ != ApplicationMode::File) return false;
  if (newIndex < 0 || newIndex >= static_cast<int>(pagerFileList_.size())) return false;
  if (newIndex == pagerFileIndex_) return false;
  // 他の「全ての」Fileモードの退出(goBack()、JumpNext/Previous自身の
  // Backのステップ、等)が行うのと同じ「ファイルを離れる」際の記帳
  // ――FileMode::saveCursorPosition()自身のコメント参照。
  fileMode_.saveCursorPosition();
  int fd = ::open(pagerFileList_[newIndex].c_str(), O_RDONLY);
  if (fd < 0) return false;
  // startPagerFileList()自身の同日の同種の修正のコメント参照――
  // ここも初期予算はrows*2ではなくrowsにする。
  fileMode_.loadStream(fd, /*ownsFd=*/true, pagerFileList_[newIndex], display_.rows());
  pagerFileIndex_ = newIndex;
  // 2026-08-25のバグ修正: ContentView自身のmove/scrollメソッド
  // (内部で自分自身をredraw()する)とは異なり、FileMode::
  // loadStream()/loadFile()はそうしない――実際のPTYキャプチャにより、
  // この特定の経路(既にページャのファイルを見ている間のn/p/x/f/
  // '['/']')が、目に見える画面の変化を一切伴わずに、fileMode_自身の
  // 内部状態を黙って更新していた(直接の検査で確認済み: currentFile()
  // は「確かに」変わっていた)ことが判明した、この呼び出し連鎖の他の
  // 何も、たまたま再描画を引き起こしていなかったため。
  activeView().redraw();
  return true;
}

// get_m()のswitch(m)から移植、usilicrc自身のK-?のデフォルトのキー
// 割り当てに対して直接クロスチェック済み(2026-07-31の訂正――このコメント
// の以前の版は、'j'/'k'は^N/^Pを、scroll.cのmove_row()が処理するのと
// 同じM_dn/M_upのカーソル移動へ別名で対応させていると主張していたが、
// usilicrcは実際には2つを意図的に「異なる」コマンドとして文書化して
// いる: 'j'/'k'に対してK-8/K-9「Move window one line down/up」対、
// ^N/^Pに対してK-10/K-11「Move cursor one line down/up」――legacyの
// Cソースをgrepして確認済み: 'j'/'k'は、usilicrcの設定可能なキー
// テーブルを通じてだけ現れ、str.c/key.c/mode.cのどこにもリテラルな
// キー割り当てとしては全く現れず、しかもmove_row()のカーソル相対の
// 挙動には一致しない名前("move window")の下にある)。Left/Right矢印
// ==Up/Downは依然として実際のlegacyの癖である(単一列のリストでは
// 奇妙に読めるが、move_row()のswitchがやっていることそのもの)、
// そして下で正しく移植されている、j/kの訂正とは無関係。M_forw/
// M_back/M_kforw/M_kback(space/b/^Vとそれらの矢印隣接のキー割り当て)
// はm_page()へマップされる;M_cr(^Mのみ――なぜここに^Jが折り込まれ
// て「いない」のかについてはクラスコメント参照)はm_cr()へ;M_bl
// (^G)はm_bl()へ;M_fgrep(^S)はfgrep検索プロンプトを開始する。
// ^Jリーダーのシーケンス(例えばEGREP用の^J^S、CUT用の^Jc/^J^C)は
// 1バイトからは解決できず、代わりにfeedByte()によって処理される。
Command Application::resolveCommand(int byte, std::optional<KeyInput::SpecialKey> special) {
  if (special) {
    switch (*special) {
      case KeyInput::SpecialKey::Up:
      case KeyInput::SpecialKey::Left:
        return Command::MoveUp;
      case KeyInput::SpecialKey::Down:
      case KeyInput::SpecialKey::Right:
        return Command::MoveDown;
      case KeyInput::SpecialKey::PageUp:
        return Command::PageUp;
      case KeyInput::SpecialKey::PageDown:
        return Command::PageDown;
    }
    return Command::None;
  }

  switch (byte) {
    case 'j':
      return Command::ScrollLineDown;
    case 'k':
      return Command::ScrollLineUp;
    case 0x0e:  // ^N
      return Command::MoveDown;
    case 0x10:  // ^P
      return Command::MoveUp;
    case ' ':
    case 0x06:  // ^F (M_forw2, usilicrc's own alias for Space/M_forw)
    case 'b':
    case 0x02:  // ^B (M_back2, alias for b/M_back)
    case 0x16:  // ^V
      return (byte == 'b' || byte == 0x02) ? Command::PageUp : Command::PageDown;
    case kCarriageReturn:
      return Command::Enter;
    case kControlG:
      return Command::Back;
    // 2026-08-16修正: 入れ替え――直接のユーザー報告("「;」「:」の
    // 機能が逆")が、kKeyTable自身のM-0/M-1エントリ(項目177、
    // usilicrc自身のM-?マクロセクションから逐語的に取った: `;`(M-0)
    // = ^G ^P Enter = "Back, cursor up"、`:`(M-1) = ^G ^N Enter =
    // "Back, cursor down")が、JumpNext/JumpPrevious自身の宣言について
    // の2026-08-03のコメントが「ユーザーによって直接確認済み」だと
    // 主張していた逆(';'=MoveDown、':'=MoveUp)である「この」
    // ディスパッチと食い違っていることまで遡って突き止めた。
    // kKeyTable自身のマッピングは(当てずっぽうではなく)usilicrc自身
    // から直接取ってきたものなので、ここではlegacyで検証済みの正典
    // である;以前の「確認済み」だったディスパッチはこの2つが逆に
    // なっていた。JumpNext/JumpPrevious自身の本体(それぞれBack+
    // MoveDown+Enter/Back+MoveUp+Enter)は変わっていない――「どちらの」
    // 文字が「どちらの」コマンドに届くかだけが入れ替わっており、
    // これはexecuteSelectedKeyBinding()自身のKEYリストの再生
    // (^G/^P/^N/^Mを直接送り込み、決してこのswitchを一切通らない)
    // が既に正しいまま手を付けられずにいることも保つ、最小限の修正
    // である。
    case ';':
      return Command::JumpPrevious;
    case ':':
      return Command::JumpNext;
    // 2026-09-19変更、直接のユーザー依頼("現状の^SでFGREP、^[iで
    // インクリメンタルサーチを廃止して、^Sでリテラルなインクリメンタル
    // サーチに一本化してほしい"): プレーンな^S(K-31)は、Enterで確定
    // する一括のリテラル検索(2026-09-05〜2026-09-18の
    // Command::StartGrep、削除済み)ではなく、以前ESC i専用だった
    // インクリメンタルサーチを起動する――Command::StartIncrementalGrep
    // 自身の宣言コメント参照。
    case kControlS:
      return Command::StartIncrementalGrep;
    // 2026-09-20更新、直接のユーザー依頼("^Hで新HELP、SHELLを含む
    // プロンプトからは^J^H"): 旧K-2("KEY")は退役し、^H自身がStartHelp
    // (下のleaderJPending_側の'h'ケースと同じコマンド)を指すように
    // なった――^H単体と^J^Hはどちらも同じ新HELPへ到達する。
    case 0x08:  // ^H
      return Command::StartHelp;
    // 2026-09-08、直接のユーザー依頼によるキー再編("^Je⇒e（←less
    // 同様）")で^Jeから移行: 他のプレフィックス無しのページャー単一
    // 文字コマンド(b/j/k/q等、このswitch自身の他のcase参照)と同じ
    // 扱いになった。
    case 'e':
      return Command::StartEdit;
    case 0x80:  // ^Space(M_mark)――KeyInputはリテラルなNUL/Ctrl-Spaceを0x80として報告する、そのクラスコメント参照
    case '.':   // 2026-08-23: 同じプレーンなトグル、移動は無い――ToggleMark自身のコメント参照
      return Command::ToggleMark;
    case ']':  // 現在のエントリをマークし、その後下へ移動――Command::MarkAndAdvance自身のコメント参照
      return Command::MarkAndAdvance;
    case '[':  // 現在のエントリをマークし、その後上へ移動
      return Command::MarkAndRetreat;
    case 0x12:  // ^R(M_hist)――ToggleHistoryView自身のコメント参照
      return Command::ToggleHistoryView;
    case 0x0c:  // ^L(M_clear)
      return Command::Redraw;
    case 0x09:  // ^I(M_usilic、K-0、"USILIC"/今は"INFO")――2026-08-27に^Tから割り当て直した、kKeyTable自身のコメント参照
      return Command::ShowUsilicBanner;
    case 0x14:  // ^T――Command::StartTutorial自身のコメント参照(TUTORIAL専用の直接キー)
      return Command::StartTutorial;
    case 0x1a:  // ^Z(M_susp)――Command::Suspend自身のコメント参照
      return Command::Suspend;
    case 'q':  // M_quit――usilicrcはこれをページャの文脈に限定している;handleCommand()自身のゲート参照
      return Command::QuitPager;
    case 'n':  // Command::PagerNextFile自身のコメント参照
      return Command::PagerNextFile;
    case 'p':
      return Command::PagerPreviousFile;
    case 'x':
      return Command::PagerFirstFile;
    case 'f':
      return Command::PagerLastFile;
    default:
      return Command::None;
  }
}

bool Application::dispatchByte(int byte, std::optional<KeyInput::SpecialKey> special) {
  // awaitingLocateUpdateDb()自身のコメント参照: promptKind_より前でさえ
  // チェックされる(drawPromptLabel()自身のstaticMessage経路はそもそも
  // promptKind_を一切設定しない――これは本物の編集可能なプロンプトで
  // はない)。^Gは、実行中のどの時点でも常に即座に勝ち、updatedbを
  // バックグラウンドで切り離されたまま実行させ続ける(ユーザー自身の
  // 明示的な仕様に従う: "常時^G（←^Cではなく）で^J^Cへ移行")。他の
  // 全てのバイトは、このメッセージが表示されている間は単に無視される、
  // 元の設計自身の範囲に合わせている(ここで他の対話は一度も可能では
  // なかった)。
  if (awaitingLocateUpdateDb()) {
    if (!special && byte == kControlG) {
      locateUpdateDbPid_ = -1;
      startLocatePrompt();
      return true;
    }
    return false;
  }
  // 2026-09-19追加、直接のユーザー依頼("LOCATEと同じく^Gでduを中断し、
  // 構築中断時点のDUリストへ移動")、すぐ上のawaitingLocateUpdateDb()
  // ゲートと同じ形――ただしLOCATEの「バックグラウンドで切り離した
  // まま実行させ続ける」ではなく、実際にterminate()し、その時点
  // までにduPendingOutput_へ蓄積された分でDU一覧を構築する
  // (awaitingDu()/pollDuCompletion()/finishDuAsync()自身のコメント
  // 参照、runDuOnCurrentFolder()自身のコメントの通り)。
  if (awaitingDu()) {
    if (!special && byte == kControlG) {
      ChildProcess::terminate(duPid_);
      duPendingOutput_ += ChildProcess::readAvailable(duOutputFd_);
      close(duOutputFd_);
      duOutputFd_ = -1;
      duPid_ = -1;
      finishDuAsync();
      return true;
    }
    return false;
  }
  // 2026-09-20追加、直接のユーザー報告("debian-keyring...tar.xzを
  // 踏んだら反応が無くなった。LOCATE同様、^Gで読み込みを中断できる
  // ようにして")、すぐ上のawaitingDu()ゲートと全く同じ形。
  if (awaitingArchiveLoad()) {
    if (!special && byte == kControlG) {
      ChildProcess::terminate(archiveLoadPid_);
      archiveLoadPendingOutput_ += ChildProcess::readAvailable(archiveLoadOutputFd_);
      close(archiveLoadOutputFd_);
      archiveLoadOutputFd_ = -1;
      archiveLoadPid_ = -1;
      archiveLoadAborted_ = true;  // finishArchiveLoadAsync()自身の2026-09-25のコメント参照
      finishArchiveLoadAsync();
      archiveLoadAborted_ = false;
      return true;
    }
    return false;
  }
  // 2026-09-10追加、直接のユーザー報告(usilic -c ~/git/usilicで再現:
  // "^[iでの検索さなかでは^N^Pが利かない"): ESC i(インクリメンタル
  // サーチ)はEnterを押すまでプロンプトが開いたままなので、この直後の
  // `promptKind_ != PromptKind::None`のチェックに先に捕まり、下の
  // grepInPlaceActive_向けの^N/^P横取り(この関数の少し下、通常の
  // 送信済み^Sセッション――既にプロンプトが閉じている――向けに書か
  // れたもの)に一度も到達できなかった。ライブタイピング中でも次/前の
  // マッチへジャンプしたいのは自然な要求(まだパターン編集中でも
  // プレビューしたい)なので、このチェックをプロンプト経路より前へ
  // 出す――他の全てのバイトは引き続き通常通りfeedPromptByte()へ渡る
  // (ここで横取りするのは0x0e/0x10の2バイトだけ)。
  if (grepInPlaceActive_ && incrementalGrepActive_ && promptKind_ == PromptKind::GrepPattern && !special &&
      (byte == 0x0e || byte == 0x10)) {
    bool result = handleCommand(byte == 0x0e ? Command::GrepInPlaceNext : Command::GrepInPlacePrevious);
    // jumpToGrepInPlaceMatch()自身のredraw()が、liveIncrementalGrepSearch()
    // 自身のコメント通り画面最終行(生きたプロンプトの行)を通常の
    // モードラインで上書きしてしまう――そちらと同じ組で描き直す。
    // 2026-09-11修正、直接のユーザー報告("^[iの文字列入力がGREPの隣
    // から始まっているが、それだと^N^Pで循環が起きたかどうかを判別
    // できない"): 空文字列ではなくgrepInPlaceMatchLabel()(「マッチ
    // 番号/総マッチ数」)を渡し、ライブ編集中も入力欄がそちらの直後
    // から始まるようにする――grepInPlaceActive_はこの分岐に入る条件
    // で既にtrueなので、ここでは常に非空。
    drawPromptLabel(promptKind_, grepInPlaceMatchLabel());
    promptEditor_.redraw();
    return result;
  }
  // 2026-09-18追加、HELP.txt 2.13章「LIST」の実装、実機報告(③「^Nで
  // 非ハイライトにはなるものの編集不能」)を受けて追加: feedTabCompletionListByte()
  // 自身の宣言コメント参照。tabCompletionContext_が有効な間は、下の
  // promptKind_/currentMode_==Shellの通常分岐(SHELLからの^Jp usilic
  // 起動時はpromptKind_がNoneのまま、かつcurrentMode_も既にListBrowser
  // へ切り替わっているため、どちらの通常分岐にも一度も引っかからず、
  // ^H等が汎用コマンド割り当てへ誤って落ちていた)より先にここで
  // 一括して処理する。
  if (tabCompletionContext_) return feedTabCompletionListByte(byte, special);
  if (promptKind_ != PromptKind::None) return feedPromptByte(byte);
  if (currentMode_ == ApplicationMode::Shell) return feedShellByte(byte, special);
  // COPY(^O^C): それ自身の小さな自己完結したキーの語彙
  // (feedCopyByte()自身のコメント参照)――上のShellと同じ優先度で
  // ここまで早くチェックされているので、このオーバーレイが有効な間、
  // resolveCommand()の通常のpage/scrollのキー割り当てや下の^Oリーダー
  // のテーブルは誤って発火しない。
  if (currentMode_ == ApplicationMode::Copy) return feedCopyByte(byte, special);
  // in-place GREP(2026-09-09追加、jumpToGrepInPlaceMatch()自身の
  // コメント参照): セッション中だけ、通常のresolveCommand()の^N/^P
  // (MoveDown/MoveUp)より先に横取りする――Shell/Copyの上の2分岐と
  // 同じ優先度・同じ理由。
  if (grepInPlaceActive_ && !special && (byte == 0x0e || byte == 0x10)) {
    return handleCommand(byte == 0x0e ? Command::GrepInPlaceNext : Command::GrepInPlacePrevious);
  }
  // GrepPattern/CutPattern/ManName自身の「Select ...」リスト
  // (startRegexPromptList()自身のコメント参照): 他の全てのListBrowser
  // の使い方(FILE/DIR/SHELL/PROGRAMのピッカー、全て純粋なナビゲート+
  // Enterで選択)とは異なり、legacy自身のEGREP/CUT-regex/MANのリストは、
  // タイプすることでハイライトされた候補を直接編集する、生きた
  // コンボボックスである――feedRegexListByte()自身のコメント参照。
  // 下の汎用のresolveCommand()のディスパッチより前にチェックされる
  // ので、普通の文字(resolveCommand()ならそうでなければpage/scroll
  // コマンドへ解決していたであろう、他の全ての場所で使われるのと
  // 同じテーブル)は代わりにこちらに届く。
  // ManNameは2026-08-15追加: 実際の報告("^J^M→「No String」はいい
  // けどモードラインに入力できない")――key.c自身のinput_key()は、
  // M_man2(K-44自身のリスト編集のケース)を、M_cut2/M_locate/
  // T_locate/M_fgrepとまさに同じswitchのケースにグループ化している
  // (それ自身の`k = vt_col(0) / 2`という分岐参照)、これがGREP/CUTに
  // 固有ではないことを裏付けている。PROGRAM自身のM_prog2は、その
  // 同じswitchの「別の」ケースにグループ化されており(幅の計算だけを
  // 共有していて、この編集の挙動を共有するとは確認されていない)、
  // この報告の対象ではなかったので、意図的にここには含めず、当て
  // ずっぽうにはしていない。
  // RecentFile/RecentDirectoryは2026-08-17追加(K-46/K-48、^X^F/^X^D):
  // 実際の報告(スクリーンショット=cn、"文字入力エリアが無い")+
  // legacyのkey.cを読んで、T_hfile/T_hdirもまさにこの同じf_enter=1
  // のswitchのケースにグループ化されていることを確認済み
  // (startListBrowser()自身のコメント参照)――このポートは、GREP/
  // CUT/MANとは異なり、それらを、この生きた編集可能な経路ではなく、
  // 「古い」プレーンな「ナビゲートのみ」のListBrowserModeの経路に
  // 乗せていた。
  if (currentMode_ == ApplicationMode::ListBrowser && promptHistoryContext_ &&
      (promptHistoryContext_->kind == PromptKind::GrepPattern || promptHistoryContext_->kind == PromptKind::CutPattern ||
       promptHistoryContext_->kind == PromptKind::ManName || promptHistoryContext_->kind == PromptKind::RecentFile ||
       promptHistoryContext_->kind == PromptKind::RecentDirectory)) {
    return feedRegexListByte(byte, special);
  }

  // 2026-09-25追加、直接のユーザー依頼によるキー再編("^Cをプレフィックス
  // にするのは？ ^Cb：バッファをCUT、^Cf：ファイルをCUT。CUTのCは^CのC"):
  // ^Cは他の接頭キー(^J/^X/^O/ESC)と違い、ここ(通常の画面)だけで接頭
  // キーとして働く――SHELL画面ではシェルへの割り込みとしてそのまま
  // 送る必要があり(feedShellByte()は通らない)、プロンプトでは入力の
  // 中断(feedPromptByteInner())なので、どちらにも足していない。
  if (leaderCPending_) {
    leaderCPending_ = false;
    if (special) return false;
    switch (byte) {
      case 'b':  // ^C b : CUT(バッファ分割、旧^Jc)
        return handleCommand(Command::StartCut);
      case 'f':  // ^C f : CUT(ファイル分割、旧^J^C)
        return handleCommand(Command::StartCutFile);
      default:
        return false;  // 認識されない^Cシーケンス
    }
  }
  if (!special && byte == kControlC && !escPending_ && !leaderJPending_ && !leaderJOPending_ && !leaderXPending_ &&
      !leaderOPending_ && !gotoLineGPending_ && !gotoLineDigitsPending_) {
    leaderCPending_ = true;
    return true;  // シーケンス継続中、no-opではない
  }

  if (escPending_ || leaderJPending_ || leaderJOPending_ || leaderXPending_ || leaderOPending_ ||
      gotoLineGPending_ || gotoLineDigitsPending_ ||
      (!special && (byte == kLeaderJ || byte == kLeaderX || byte == kLeaderO || byte == kEscape))) {
    return dispatchLeaderByte(byte, special);
  }

  // 実験的なスペースの上書き(2026-08-23、Command::QuickLook自身の
  // コメント参照): resolveCommand()自体の中ではなく、ここで適用され
  // ている、それ(resolveCommand())は、それ自身のdocコメントと
  // test_application.cppの、インスタンス無しの約20個の
  // `Application::resolveCommand(...)`呼び出しが既に想定している、
  // 同じ純粋/静的なbyte->Commandのマッピングのままにしておく――そこ
  // にインスタンス状態の読み取りを追加すると、非静的になり、1つの
  // 実験的な機能のためにそれら全てを壊してしまう。この正確なディス
  // パッチの地点に届く、プレーンで接頭辞の無い' 'バイトだけを上書き
  // する(下のresolveCommand()の通常のPageDownケースへ変わらず届き
  // 続ける^F/^V/本物のPageDownキーではない、そのため長い一覧には常に
  // ページ送りする手段が依然として残る)。今は無条件(以前のカットは
  // 代わりに「CP/MV/RMの選択を進める」ことを意味させるためpendingCpMvRm_
  // で分岐していた――2026-08-23に削除、直接のユーザーの追跡:
  // "ハイライト表示での[space]は、やはりCP/MV/RM（とARC）で混乱を
  // 招く"――Spaceは適用されるあらゆる場所でQuick Lookを意味する、
  // それだけ;マーキングは今や'.'/'['/']'に限られている、それら自身
  // のコメント参照、なのでもはやここに混乱すべきモード条件付きの
  // 意味は無い)。
  if (byte == ' ' && !special && handleCommand(Command::QuickLook)) return true;

  // 縦書きの本の中の横書きの範囲(奥付等)でも同じ(2026-09-29、直接のユーザー報告"←なら
  // カーソルは移動するが奥付ページに変わった後はさっきの画面との間で行き来ループ"――横書きの
  // ←は「上へ」なので範囲の前へ出て縦書きへ戻り、次の←でまた入っていた。右から左へ読む
  // 本なのでKindle等と同じく←で先へ)。
  if (special && ((currentMode_ == ApplicationMode::File && (epubVerticalActive() || epubHorizontalSection_)) ||
                  (currentMode_ == ApplicationMode::Cut && epubTocVerticalActive()))) {
    // 縦書き(handleEpubVerticalCommand()参照)は右から左へ読むので、←で先へ、→で戻る
    if (*special == KeyInput::SpecialKey::Left) return handleCommand(Command::MoveDown);
    if (*special == KeyInput::SpecialKey::Right) return handleCommand(Command::MoveUp);
  }
  return handleCommand(resolveCommand(byte, special));
}

// なぜこれが存在するのかについてはdispatchByte()自身のコメント参照。
// 意図的に小さく厳選された一群のバイトが、リストのナビゲートのみ
// (選択を動かす^N/^P/矢印、そのまま投入するEnter、完全にキャンセル
// して戻る^G、再描画する^L)のままにしてある――筋肉の記憶が持ち越さ
// れるよう、これらについてはresolveCommand()自身のキー割り当てに
// 合わせている。他の「全ての」バイト(普通にタイプされた文字、
// DEL/^H)は「編集を開始する」として扱われる: 「現在ハイライトされて
// いる」候補自身のテキストで(空白ではなく――これが、まっさらから
// 新しいものをタイプするだけでなく、「既存の」候補を編集することを
// 可能にする)事前に埋められた、通常の編集可能なGrepPattern/
// CutPatternのプロンプト(activatePrompt()自身の既に正しい、プレーン
// でライブなカーソルのレンダリング)へ遷移し、それからすぐにこの
// 同じバイトをfeedPromptByte()経由で再び送り込むので、別個の「最初
// からやり直す」ステップを必要とするのではなく、遷移が連続している
// ように感じられる。
//
// リーダーのシーケンス(^J/^X/^O/ESC、2026-08-11修正)も、ここでは
// 「編集を開始する」として扱われるのではなく、コマンドとしてディス
// パッチされる――実際の報告(^J^W、「SHELL: insert buffer content」/
// K-64、既にshellInsertBufferContent()として完全に実装されており、
// 他の「全ての」ContentViewに裏打ちされた画面から到達可能)により、
// それが黙って候補自身のパターンの中でリテラルな"JW"というテキスト
// になってしまうことが判明した。key.c自身のkey_dir()/edit_line()の
// switchから移植: `case K_w: if (f_M != T_input) { c = T_w; break; }
// default: c = k + N_char;` -- ^W(そして同様に拡張すれば、リーダーで
// 開始されるどのコマンドも)は、`T_input`(プレーンなテキスト入力
// フィールド、このポートのactivatePrompt()に駆動される編集状態)を
// 除く全ての場所でそのコマンドの意味へ解決される――このリスト閲覧の
// 状態は、まだT_inputでは「ない」(それは下で編集が始まってから初め
// て始まる)ので、リーダーのバイトは他の全ての場所と全く同じように
// 振る舞うべきである。トップレベルのdispatchByte()(currentMode_/
// promptHistoryRecallKind_が変わっていないため、まっすぐ「この」
// 関数へ戻って再帰してしまうであろう)ではなく、dispatchLeaderByte()
// へ直接ルーティングされる――シーケンスを「開始する」バイト
// (kLeaderJ自体)と、leaderXPending_方式のフラグのいずれかが以前の
// 呼び出しで既に設定されている間の「継続」バイトの両方をカバーする。
bool Application::feedRegexListByte(int byte, std::optional<KeyInput::SpecialKey> special) {
  bool isNav = special == KeyInput::SpecialKey::Up || special == KeyInput::SpecialKey::Down;
  if (!special) {
    switch (byte) {
      case 0x0e:  // ^N
      case 0x10:  // ^P
      case kCarriageReturn:
      case kControlG:
      case 0x0c:  // ^L
        isNav = true;
        break;
      // ^V(2026-09-18追加、フォローアップ8: feedTabCompletionListByte()
      // 自身の同種の修正(フォローアップ3)参照――PageDown。SHELL自身の
      // ^R COMMAND履歴呼び戻し(startPromptHistoryRecall()、
      // PromptKind::ShellCommand)が今回この関数を経由するようになった
      // ため、同じページ送りをここにも揃える。
      case 0x16:
        isNav = true;
        break;
      // ^R(2026-08-18修正、直接の報告+スクリーンショット=cz/=da):
      // 元々このホワイトリストには無かったので、まだ単に「閲覧して
      // いる」だけ(まだ何もタイプしていない)状態でプレーンな^Rを
      // 押すと、下の「編集を開始する」ロジックへ流れ落ちてしまって
      // いた――そのロジックはバイト0x12を普通にタイプされた文字として
      // 扱い、ハイライトされている候補で事前に埋められた「本物の」
      // プロンプトを有効化し、それから0x12をfeedPromptByte()経由で
      // 再び送り込むが、そこではそれ自身の無関係な`if (byte == 0x12)
      // startPromptHistoryRecall();`(K-49、ここの他の全ての
      // PromptKindも頼っている、正当な「タイプ中に^Rでリストを再度
      // 開く」機能)が「再び」発火する、しかし今回はstartPromptHistoryRecall()
      // 自身の「デフォルト」の`directEntry = false`経由であり――
      // RecentFile/RecentDirectoryが「常に」directEntryであるにも
      // 関わらず(startPromptHistoryRecall()自身のコメント参照)、
      // promptHistoryRecallIsDirectEntry_を黙ってfalseへ叩き戻して
      // しまう、これがまさに、その直後の次の^G(=da、"一覧が無い"――
      // promptHistoryRecallIsDirectEntry_がもうtrueと読めなくなった
      // ため、黙って汎用の「空のプロンプトを再度開く」分岐へフォール
      // バックしていた)で、少し後にreturnFromListBrowser()自身の
      // =cx/=cyの修正を壊していたものである。それは途中で
      // sortDirectEntryList=falseも強制していたので、結果として得ら
      // れる画面は、実際のトグルではなく、古いdrawPromptLabel()以前
      // のレンダリング経路を通した無条件の時系列「HISTORY」ビューを
      // 表示していた(=cz、"文字列入力エリアが無い"――この迂回路は
      // refreshDirectEntryModeLine()を一切通らない)。元々はRecentFile/
      // RecentDirectoryだけに限定されていた、GrepPattern/CutPattern/
      // ManNameの既存の「閲覧中の^Rはまさに同じ迂回路経由で候補リスト
      // を再起動する」は「報告されておらず、おそらく無害で、このバグ
      // ではない」という想定だった。直接のユーザー報告+正確な再現
      // (2026-08-28、"CLAUDE.md画面で^J^S^R"...^Gして...行数情報も
      // 無い)により、GrepPatternについて特にそうではないことが証明
      // された: まさに同じ破損(promptHistoryRecallIsDirectEntry_が
      // 黙ってfalseへ叩き戻される)が、それ自身のその後の^Gを同様に
      // 壊す(検索が実際に始まった場所ではなく、間違った画面の上に
      // 描かれた迷子の編集可能なEGREPプロンプトに着地する)。
      // toggleListBrowserHistory()は今やGrepPattern/CutPatternについ
      // ても本物のソート済み<->時系列のトグルを行う(2026-08-28、直接
      // のユーザーの追跡: "前回使用した正規表現は何だっけ？...EGREPの
      // 検索文字列一覧からも発動してくれないと困る"――ここでの^Rは、
      // このポート自身のソート済みで重複排除された直接入力のビューと、
      // 生の時系列ログが、GREP/CUTについても2つの実際の、異なるもので
      // ある以上、単に無害というだけでなく本当に有用である――その関数
      // 自身の更新されたコメント参照)。ここではCutPatternへも拡張:
      // (古い破損する迂回路ではなく)この本物のトグルに対して、既存の
      // 「CUT ^Rでパターンを呼び戻しても同じファイルを再び対象にし
      // 続ける」テストを再チェックし、依然として通ることを確認した
      // ――そこで^Rが押される時点で、たった今投入されたばかりのパター
      // ンは、ソート済みビューと時系列ビューの両方に既に存在している
      // (LIFOの^Gが起こるより前、投入時にringへ積まれる)ので、その
      // テスト自身のEnterで選択するというアサーションにとっては、
      // どちらのビューが表示されているかは問題にならない。
      case 0x12:
        if (promptHistoryContext_ && (promptHistoryContext_->kind == PromptKind::RecentFile ||
                                       promptHistoryContext_->kind == PromptKind::RecentDirectory ||
                                       promptHistoryContext_->kind == PromptKind::GrepPattern ||
                                       promptHistoryContext_->kind == PromptKind::CutPattern)) {
          isNav = true;
        }
        break;
      // ^S(2026-09-11追加、直接のユーザー依頼: "やりたいのはLOCATEと
      // 同じ。Resの3文字が`...`にあるやつ"): RecentFile/RecentDirectory
      // (^X^F/^X^D)限定で、in-place GREPを起動するコマンド
      // (resolveCommand()自身の対応表参照――2026-09-19の一本化以後は
      // Command::StartIncrementalGrep、それ自身の宣言コメント参照)へ
      // 流す――isGrepInPlaceEligibleMode()
      // は既にListBrowserを含んでいる(startGrepPrompt()自身の
      // grepReturnMode_=currentMode_採用がそのまま効く)ので、ここで
      // 「編集を開始する」下の分岐(=このホワイトリストの外)へ流れ
      // 落ちるのを防ぎさえすれば、Directory/File/LOCATE等と全く同じ
      // in-place検索(マッチ数表示・`^N`/`^P`・省略された"..."自体の
      // ハイライト)がそのまま動く。GrepPattern/CutPattern/ManName
      // (候補自体が検索パターン/コマンド名であり、^Sで「その候補一覧を
      // 検索する」という意味を成さない、あるいは既にその場での
      // パターン編集自体が主目的)には広げない――^Sは元々そちらでは
      // 単なる「クォート無し制御バイトの拒否」(ビープのみ、何も起き
      // ない)だった以上、RecentFile/RecentDirectoryだけへの追加は
      // 純粋な機能追加であり退行の余地が無い。
      case kControlS:
        // 2026-09-11修正、直接のユーザー報告("usilic -c（引数無し）で
        // 起動→^X^D→^O^S：モード名FGREPのはずなのにGREP..."): この
        // switch自体は下のescPending_/leaderOPending_等のリーダー
        // ペンディングチェック「より前」に位置するため、`^O^S`
        // (FGREP)/`^J^S`(EGREP)の「2バイト目」の^Sが、まだleaderOPending_/
        // leaderJPending_が立っている状態でここに先に捕まってしまい、
        // 本来`dispatchLeaderByte()`が解決すべき`^O^S`→Command::StartFgrep
        // /`^J^S`→Command::StartEgrepへ一度も渡らず、代わりに単独の
        // `^S`(当時のCommand::StartGrep、Basic)として誤って解釈されて
        // いた
        // (モードラインに"FGREP"ではなく"GREP"が出る、という形で発覚)。
        // リーダーが1つでも保留中の間は、このin-place GREP起動自体を
        // 完全に譲る(下の共通のリーダーチェックへ普通に流れ落ちて
        // dispatchLeaderByte()に解決させる)必要がある。
        bool anyLeaderPending = escPending_ || leaderJPending_ || leaderJOPending_ || leaderXPending_ ||
                                leaderOPending_ || gotoLineGPending_ || gotoLineDigitsPending_;
        if (!anyLeaderPending && promptHistoryContext_ &&
            (promptHistoryContext_->kind == PromptKind::RecentFile ||
             promptHistoryContext_->kind == PromptKind::RecentDirectory)) {
          isNav = true;
          // ^N/^P/Enter/^G/^L/^R(すぐ上のcase群)は候補一覧を「引き続き
          // 閲覧する」操作であり、promptHistoryContext_(kind/returnMode
          // 等)がまだ必要なので保持したままにする――しかし^Sは候補一覧
          // の閲覧そのものを離れ、全く別のGREP-in-placeセッションを
          // 開始する。ここでクリアしないと、listBrowserView_自身の
          // ModeLineFieldsProvider(このファイル冒頭のApplication自身の
          // コンストラクタ参照)が、promptHistoryContext_のkindが依然
          // RecentFile/RecentDirectoryのままであることを理由に、
          // activateGrepInPlace()が設定したgrepInPlaceActive_を無視して
          // 「まだ候補を閲覧中」の古い表示(name="FILE"/"DIR"、(grep)
          // バッジ無し)を優先してしまう(実際に発生を確認: マッチ数は
          // 正しいのに"GREP .../(grep)"ではなく"FILE 1/5 ..."と表示
          // されていた)。
          promptHistoryContext_ = std::nullopt;
        }
        break;
    }
  }
  // ^O^M/^[^M(2026-09-27、openListEntrySpecial()参照): リーダーキーの途中の
  // ^Mは「決定」ではなく、そのリーダーの続きとして扱う。
  if (isNav && !special && byte == kCarriageReturn && (escPending_ || leaderOPending_)) isNav = false;
  if (isNav) return handleCommand(resolveCommand(byte, special));
  // ^SPC(2026-09-27): ^X^F/^X^Dの一覧では'.'/'['/']'は打った文字として
  // パスの編集になるので、選択は^SPCだけで行う(list_marks.hpp参照)。
  if (!special && byte == 0x80 && promptHistoryContext_ &&
      (promptHistoryContext_->kind == PromptKind::RecentFile ||
       promptHistoryContext_->kind == PromptKind::RecentDirectory) &&
      !(escPending_ || leaderJPending_ || leaderJOPending_ || leaderXPending_ || leaderOPending_)) {
    return handleCommand(Command::ToggleMark);
  }
  if (escPending_ || leaderJPending_ || leaderJOPending_ || leaderXPending_ || leaderOPending_ ||
      gotoLineGPending_ || gotoLineDigitsPending_ ||
      (!special && (byte == kLeaderJ || byte == kLeaderX || byte == kLeaderO || byte == kEscape))) {
    return dispatchLeaderByte(byte, special);
  }
  if (special) return false;  // 処理されない特殊キー――テキストとして送り込む意味のあるものが無い

  PromptKind kind = promptHistoryContext_->kind;
  const std::string *selected = listBrowserMode_.selectedItem();
  std::string seed = selected ? *selected : promptHistoryContext_->textBeforeRecall;
  // 下でpromptHistoryContext_がクリアされ、これがそもそも「候補リスト」
  // であることをやめる「前に」取得する――activatePrompt()自身のコメント
  // 参照(直接のユーザーの訂正: "[bs]→「1/10 TOP」が消滅"、つまり、
  // このリストがちょうど閲覧されている間ずっと見えていたこの位置
  // 表示は、編集が始まった瞬間に消えるべきではない)。
  std::string positionText;
  int candidateCount = static_cast<int>(listBrowserMode_.items().size());
  if (candidateCount > 0) positionText = ModeLine::format(listBrowserView_.cursorIndex() + 1, candidateCount);
  // pendingSearchFromDirectEntryList_自身のコメント参照: クリアする
  // 「前に」取得するので、この編集が実際に投入された時点で
  // executeGrep()/executeCut()がそれを依然として見られる。
  pendingSearchFromDirectEntryList_ = promptHistoryContext_->isDirectEntry;
  ApplicationMode returnMode = promptHistoryContext_->returnMode;
  // 2026-09-18追加(フォローアップ8、PromptKind::ShellCommand自身の
  // 宣言コメント+feedTabCompletionListByte()自身の同種の修正参照):
  // SHELL宛先(このPromptKindが有効なのはSHELL自身の^R COMMAND履歴
  // 呼び戻しがShellCommandプロンプト編集中に押された場合のみ)の間は
  // currentMode_を意図的にListBrowserのまま保つ――他は元々問題無かった
  // ため退行を避け従来通り即座に切り替える。
  if (returnMode != ApplicationMode::Shell) currentMode_ = returnMode;
  // 2026-08-11修正: これは以前は、編集が始まった瞬間に画面上の候補
  // リストを、下層の一覧全体で置き換える形で、activeView()(「宛先」の
  // Directory/Fileビュー)を再描画していた――実際の報告+参照スクリーン
  // ショットにより、これが、legacy自身のM_egrep/M_cut画面である生き
  // た編集可能な「コンボボックス」ではなく、まさに^Jc自身のプレーンな
  // fgrepプロンプト(一覧全体の上に空のプロンプト)のように読めること
  // が判明した: 候補リストは完全に見えたままで、モードライン自身の
  // 行だけが、現在選択されている候補の上で編集可能なフィールドに
  // 変わる。currentMode_は上ですぐにreturnModeへ切り替えられている
  // (executeCut()/executeGrep()は投入時点でそれが正しいことを必要と
  // する)が、プロンプトが解決するまでそのモード自身のビューは何も
  // 再描画されない(promptActive()が他の全てのredraw()呼び出しを
  // ゲートしている――例えばrunGuiPhase()自身の
  // !app.promptActive()チェック参照)ので、ここでlistBrowserView_自身
  // のコンテンツを画面に残しておくことは安全である。
  listBrowserView_.redraw();
  // 2026-09-11修正、直接のユーザー報告("CLAUDE.mdを選択→^X^F：戻り値
  // 「.../CLAUDE.md」→^S：「.../git/usilic/」"――^Sそのものは
  // RecentFile/RecentDirectoryの「タイプしてパスを直接編集する」既存
  // 仕様(このライブ編集への遷移そのもの、legacy T_hfile/T_hdir由来)
  // の対象外キーでは全くなく、以前は`promptHistoryContext_`をここで
  // 即座にクリアしていたため、直後のactivatePrompt()→drawPromptLabel()
  // 自身のPromptKind::RecentFile/RecentDirectory向けdestinationMode
  // 計算(`promptHistoryContext_ ? promptHistoryContext_->returnMode :
  // ApplicationMode::Directory`)が、常にnulloptを読んで無条件に
  // `ApplicationMode::Directory`へ落ち、モードラインの「戻り値」欄に
  // 常にDirectory基準の値(たまたま起点がDirectory自身だった場合は
  // 偶然正しく見えるだけ)を表示してしまっていた。上で既に
  // ローカル変数`returnMode`へ退避済みなので、クリア自体を
  // activatePrompt()の「後」へ遅らせるだけで、drawPromptLabel()が
  // 実行される時点でもまだ本来のreturnMode(例: File)を読めるように
  // なる。
  activatePrompt(kind, seed, positionText);
  promptHistoryContext_ = std::nullopt;
  return feedPromptByte(byte);
}

// この関数自身の宣言側のコメント(application.hpp)参照。
int Application::resolveGotoLineRow(ContentView &view, int targetRawLine) {
  for (int attempt = 0; attempt < 8; attempt++) {
    const std::vector<ContentView::Line> &lines = view.lines();
    int total = static_cast<int>(lines.size());
    if (total == 0) return 0;
    if (!lines[0].rawLineNumber) return std::clamp(targetRawLine - 1, 0, total - 1);
    for (int row = 0; row < total; row++) {
      if (lines[row].rawLineNumber && *lines[row].rawLineNumber >= targetRawLine) return row;
    }
    // まだ目的の生の行に届いていない(幅超過の折り返しでここまでの
    // 表示行が水増しされている)――pagerにもっと読ませてから再挑戦
    // する。伸びなければファイル末尾に達しているので最後の行へ
    // クランプする。
    preloadPagerAhead(total + std::max(display_.rows(), 20), /*bypassPacing=*/true);
    if (static_cast<int>(view.lines().size()) == total) return total - 1;
  }
  return static_cast<int>(view.lines().size()) - 1;
}

// この関数自身の宣言側のコメント(application.hpp)参照。
// dispatchByte()自身の以前の本体から変わっていない――呼び出し元だけ
// が変わった(dispatchByte()自体、それに加えて今はfeedShellByte()も)。
bool Application::dispatchLeaderByte(int byte, std::optional<KeyInput::SpecialKey> special) {
  // 2026-09-08追加、直接のユーザー要望("^[Nはおよそ行数あり画面なら
  // どこでも機能でもあるのでよろしく"): feedCopyByte()自身の
  // copyGotoLineDigitsPending_/copyGotoLineGPending_と全く同じ仕様
  // (legacy自身のprint_line()、old/src/disp.c、linep==F2の分岐に
  // 合わせた挙動――数字/Enter/Backspace/^G以外は無視して入力を待ち
  // 続ける、Backspaceは1桁削除・空でのBackspaceはキャンセル)だが、
  // CopyMode専用ではなくactiveContentView()へ直接作用する汎用版。
  // application.hpp自身のgotoLineDigitsPending_のコメント参照。
  // setPositionTextOverride()自体は画面へ何も書かない(状態を設定
  // するだけ)――CopyMode::render()が毎回自前でview_.redraw()を
  // 呼んでいるのと同じく、ここでも桁を更新するたびに明示的に
  // redraw()しないと、桁入力中のモードライン表示が画面に一切
  // 反映されない(実機PTYテストで最初に確認した不具合)。
  auto showGotoLineDigits = [&]() {
    ContentView &view = activeContentView();
    // gotoLineFieldWidth_自身のコメント参照(2026-09-08追加、直接の
    // ユーザー要望"「現在行/総行 ??%」エリア幅のまま非ハイライト
    // 入力エリアとして使用して欲しい。実際の見た目は旧を参照")――
    // legacy自身のprint_line()と同じく、元々そこにあった表示の幅まで
    // 空白でパディングする(入力欄の幅を変えない)。
    std::string padded = gotoLineDigits_;
    if (static_cast<int>(padded.size()) < gotoLineFieldWidth_)
      padded += std::string(gotoLineFieldWidth_ - padded.size(), ' ');
    view.setPositionTextOverride(padded, /*plain=*/true);
    view.redraw();
  };
  auto clearGotoLineDisplay = [&]() {
    ContentView &view = activeContentView();
    view.setPositionTextOverride(std::nullopt);
    view.redraw();
    // 2026-09-08追加、直接のユーザー要望("^Lを始め、何かの処理を
    // 終えたあとは必ずカレント行に同カーソルを表示してほしい"):
    // redraw()自体は(意図的に)決してsyncVisibleCursor()を呼ばない
    // ――呼び出し元がここで明示的に行わない限り、実際の端末の
    // キャレットが古い位置に取り残され、ジャンプ先が視覚的に
    // 分かりにくくなる(Command::Redraw自身の既存のsyncVisibleCursor()
    // 呼び出しと同じ理屈)。桁入力の確定(Enter)/キャンセル(^G、
    // 空でのBackspace)どちらの経路でも、これが最後の共通出口。
    view.syncVisibleCursor();
  };
  if (gotoLineDigitsPending_) {
    if (!special && byte >= '0' && byte <= '9') {
      gotoLineDigits_.push_back(static_cast<char>(byte));
      showGotoLineDigits();
      return true;
    }
    if (!special && byte == kCarriageReturn) {
      gotoLineDigitsPending_ = false;
      if (!gotoLineDigits_.empty()) {
        // File modeのpagerは遅延読み込み(ContentView::lines()は
        // まだ画面に表示した分しか持たないことがある)なので、
        // setCursorIndex()の前にpreloadPagerAhead()で目的の行まで
        // 確実に読み込ませる――Command::ScrollLineDown/PageDown自身の
        // 既存の呼び出しパターン(移動の「前」にpreload、移動の
        // 「後」にsuppressPagerRepoll)と同じ形。File以外のモード
        // ではno-op(preloadPagerAhead()自身のガード参照)。
        //
        // 2026-09-08修正、直接のユーザー報告("usilic画面では98行
        // だった行が97として表示されている"): 入力されたNは人間向け
        // の「生の」行番号であって、幅超過で折り返され得る表示行
        // インデックスではない――resolveGotoLineRow()自身のコメント
        // 参照。
        int targetRawLine = std::stoi(gotoLineDigits_);
        ContentView &view = activeContentView();
        preloadPagerAhead(std::max(targetRawLine - 1, 0), /*bypassPacing=*/true);
        if (!view.lines().empty()) {
          view.setCursorIndex(resolveGotoLineRow(view, targetRawLine));
          // 2026-09-08追加、直接のユーザー要望("^[N後のジャンプ先が
          // 今イチ分かりにくい"): Command::Redraw(^L)自身のコメント
          // 参照――ジャンプ先を画面中央に置くことで、そのままでは
          // ジャンプ後もカーソルが画面の同じ相対位置(端寄り)に留まり
          // 得て見失いやすい問題を解消する。
          view.centerOnCursor();
        }
        suppressPagerRepoll(view.topIndex());
      }
      gotoLineDigits_.clear();
      clearGotoLineDisplay();
      return true;
    }
    if (!special && byte == 0x7f) {  // Backspace
      if (!gotoLineDigits_.empty()) {
        gotoLineDigits_.pop_back();
        showGotoLineDigits();
      } else {
        gotoLineDigitsPending_ = false;
        clearGotoLineDisplay();
      }
      return true;
    }
    if (!special && byte == kControlG) {
      gotoLineDigitsPending_ = false;
      gotoLineDigits_.clear();
      clearGotoLineDisplay();
      return true;
    }
    return true;  // 未認識のキーは無視して入力を待ち続ける(legacy自身の`continue`と同じ)
  }
  if (gotoLineGPending_) {
    gotoLineGPending_ = false;
    if (!special && byte == 'g') {
      gotoLineDigitsPending_ = true;
      gotoLineDigits_.clear();
      gotoLineFieldWidth_ = static_cast<int>(activeContentView().naturalPositionText().size());
      showGotoLineDigits();
    }
    return true;
  }
  if (escPending_) {
    escPending_ = false;
    if (special) return false;
    // "^[N"/"^[gg"(goto-line)自身の入口――上のgotoLineDigitsPending_/
    // gotoLineGPending_のコメント参照。数字が直接続けば(^[N)そのまま
    // gotoLineDigitsPending_へ、'g'が続けば(^[gg、Emacs自身のM-g g)
    // まず2バイト目の'g'を待つ状態へ。
    if (byte >= '0' && byte <= '9') {
      gotoLineDigitsPending_ = true;
      gotoLineFieldWidth_ = static_cast<int>(activeContentView().naturalPositionText().size());
      gotoLineDigits_.assign(1, static_cast<char>(byte));
      showGotoLineDigits();
      return true;
    }
    if (byte == 'g') {
      gotoLineGPending_ = true;
      return true;
    }
    switch (byte) {
      case 's':  // ESC s : SHELL
        return handleCommand(Command::StartShell);
      case '<':  // ESC < : バッファの先頭へ移動(M_head)
        return handleCommand(Command::GoToTop);
      case '>':  // ESC > : バッファの末尾へ移動(M_tail)
        return handleCommand(Command::GoToBottom);
      case kCarriageReturn:  // ESC ^M : 「そのまま読む」(K-24、M_code)
        return handleCommand(Command::ReadFileRaw);
      // ESC v : 1ページ戻る(2026-08-14、直接のユーザーの指示による:
      // "旧版のKEYには収録し忘れだが「1ページ戻る」は^[wではなくて、
      // emacs同様^[vで反映してほしい")。以前の回で、old/src/key.c
      // 「自身の」これに対する実際のキー割り当ては、ESC-vではなく
      // 素のESC-w(M_kback)であることが判明していた――想定するのでは
      // なく食い違いとしてユーザーへ報告し戻した――そしてこれは、
      // その代わりにEmacs自身のM-vの慣習を採用するために、その
      // legacyのキー割り当て(legacy自身のKEY一覧における認められた
      // 抜け)から意図的に逸脱するという、ユーザーの明示的な、事情
      // を理解した上での判断である。既に素の'b'/^B(数行下の
      // resolveCommand()自身のspace/'b'のswitch参照)に割り当てられて
      // いるのと同じCommand::PageUpを再利用する――legacyのM_kbackと
      // M_backはどちらも最終的には同じm_page()のページ戻しの挙動へ
      // 到達するので、ここで別個の異なるコマンドを新たに考案する必要
      // は無く、単に既存のものへ到達する別のキーというだけである。
      case 'v':
        return handleCommand(Command::PageUp);
      // ESC l : LOG――なぜこれをここに追加するのが安全なのか(同じ
      // キーへの、取り消された2026-08-05の試み――CLAUDE.mdの経緯の
      // 項目176とは異なり)についてはexecuteStartLog()自身のコメント
      // 参照。
      case 'l':
        return handleCommand(Command::StartLog);
      // ESC ESC(^[^[): kKeyTable自身の見出しコメントには元々
      // "EXIT (^X^C or ^[^[)"と書かれていたが、実際にはこのswitchに
      // 一度もcaseが存在せず、配線されないまま放置されていた既知の
      // ギャップだった(2026-09-09、直接のユーザー報告"^[^[は-g起動
      // のみ有効みたい...元々^X^Cと同じ扱いなので反映して"を受けて
      // 発見)。GraphicalDisplay::run()自身のイベントループがSDLK_ESCAPE
      // を素の0x1bとしてキューへ積むだけなので(application.cpp側の
      // このディスパッチとは無関係)、-gで時々「終了したように見える」
      // 挙動があったとしても、それはこのアプリ自身のコマンドとして
      // ではなく、SDL/OS側の何らかの副作用だったはずである――ここで
      // 初めて実際にCommand::StartExitへ結線する。
      case kEscape:
        return handleCommand(Command::StartExit);
      // ESC P (Shift+P、2026-09-08追加): CP/MV/ARC完了直後の一時
      // マーキングをプロンプトへ貼り付ける――Command::
      // PasteTransientTargets自身のコメント参照。既存のCOPYモード
      // 専用^[p(小文字、copyEscapePending_側の分岐)とは無関係の
      // 別のキー。
      case 'P':
        return handleCommand(Command::PasteTransientTargets);
      // ESC u (2026-09-21、直接のユーザー依頼): USILICのCUT風目次(MAN/DIR/FILE)。
      case 'u':
        return handleCommand(Command::StartUsilicToc);
      // (ESC i: 2026-09-10追加、2026-09-19削除) ESC iは
      // Command::StartIncrementalGrepの元々の専用キーだったが、直接の
      // ユーザー依頼("^[iでインクリメンタルサーチを廃止して、^Sで
      // リテラルなインクリメンタルサーチに一本化してほしい")により、
      // 同じコマンドがプレーンな^S(resolveCommand()自身のkControlSの
      // ケース参照)へ移り、2つのキーを保つ理由が無くなったため削除した
      // ――以後^[iはこのswitchのdefaultへ落ちる未認識シーケンスである
      // (Command::StartIncrementalGrep自身の宣言コメント参照)。
      default:
        return false;  // このスライスでは認識されないESCシーケンス
    }
  }

  if (leaderJPending_) {
    leaderJPending_ = false;
    if (special) return false;
    switch (byte) {
      case 0x0b:  // ^J ^K : 親ディレクトリへ移動(旧^O^K、K-19。2026-09-25のキー再編、^J ^C自身のコメント参照)
        return handleCommand(Command::GoToParentDirectory);
      case kLeaderJ:  // ^J ^J : 最も最近のRecentFileエントリへ移動――Command::GoToRecentFile自身のコメント参照
        return handleCommand(Command::GoToRecentFile);
      case kControlS:  // ^J ^S : EGREP
        return handleCommand(Command::StartEgrep);
      // 2026-09-14再編、直接のユーザー依頼(Command::StartCut自身の
      // コメント参照): ^Jb/^J^Bは廃止し、旧「通常ファイル」CUT
      // (Directory一覧から直接、開かずに分割)は^J^Cへ統合した。
      // 2026-09-25、直接のユーザー依頼によるキー再編("^Cをプレフィックスにするのは？ ^Cb：バッファをCUT、^Cf：ファイルをCUT。CUTのCは^CのC。空いた^J^CがCurrentDirectory、空いた^J^Kが^O^K"、全選択およびバッファ保存の^O/^X移動も同時): CUTは^Cb/^Cfへ移った(dispatchByte()自身の
      // leaderCPending_参照)。^J^Cは旧^J^K(ファイルセレクタ=カレント
      // ディレクトリ)を引き継ぐ。
      case kControlC:  // ^J ^C : カレントディレクトリ(ファイルセレクタ)へ移動(旧^J^K、K-18)
        return handleCommand(Command::GoToFileSelector);
      case 'a':  // ^J a : TUTORIAL/TUTORIAL.en切替(Command::ToggleTutorialLanguage自身のコメント参照)
        return handleCommand(Command::ToggleTutorialLanguage);
      // K-43/K-44、K-41/K-42(CLAUDE.mdの経緯の項目289の追跡): LOCATE/
      // DUとは異なり、この2つのペアについてのusilicrc自身の表現は本当
      // に「Input」/「Select」である――EGREPの^J^Sと同じ形――なので、
      // 各ペアの2番目のキーは今や直接候補リストを開く
      // (startManList()/startProgramList()経由の
      // startPromptHistoryRecall(directEntry=true))、EGREP/CUT-regex
      // 向けにstartRegexPromptList()が既に確立したまさに同じ仕組みを
      // 複製するのではなく再利用する。
      case 'e':   // ^J e : EPUB: 入力(K-98、2026-09-28)
        return handleCommand(Command::StartEpubPrompt);
      case 0x05:  // ^J ^E : EPUB: 一覧(K-99、2026-09-28)
        return handleCommand(Command::StartEpubList);
      case 'm':  // ^J m : MAN(K-43、「Input manual page」)
        return handleCommand(Command::StartMan);
      case kCarriageReturn:  // ^J ^M : MAN(K-44、「Select manual page」;ここでの2番目のバイトは^Mであり、素のEnterとは無関係)
        return handleCommand(Command::StartManList);
      case 'p':  // ^J p : PROGRAM(K-41、「Input program name」)
        return handleCommand(Command::StartProgram);
      case 0x10:  // ^J ^P : PROGRAM(K-42、「Select program name」;ここでの2番目のバイトは^Pであり、素の^P/MoveUpとは無関係)
        return handleCommand(Command::StartProgramList);
      // K-39/K-40(docs/ideas/IDEAS.mdのLOCATEセクション): この2つの
      // キーは、数行下のDU自身の依然として区別されていない'd'/0x04の
      // ペアとは異なり、ここでは意図的に同じコマンドでは「ない」――
      // legacy自身のm_locate()も、それら2つに本当に異なる挙動を与え
      // ていた(CLAUDE.mdの経緯の項目288の調査参照)、ただしこのポート
      // が元々想定していた「Input」/「Select」という形ではない。
      // ^Jlは今や`updatedb`を実行する(legacyには一切無かった機能);
      // ^J^Lは「プロンプトを開く」役割を保っており、今は以前のクエリ
      // が種として入っている(startLocatePrompt()自身のコメント参照)。
      case 'l':       // ^J l : LOCATE(K-39、「Execute `locate'」-> updatedb)
        return handleCommand(Command::LocateUpdateDb);
      case 0x0c:      // ^J ^L : LOCATE(K-40、「Redisplay window」)
        return handleCommand(Command::StartLocate);
      case 'd':       // ^J d : DU(集計を実行)
        return handleCommand(Command::StartDu);
      // 2026-09-09、直接のユーザー依頼により分離――RedisplayDu自身の
      // コメント参照。
      case 0x04:      // ^J ^D : DU(既存の結果を再表示、集計なし)
        return handleCommand(Command::RedisplayDu);
      // (^J ^R: 2026-09-20削除、直接のユーザー依頼(^Rの再設計、
      // "^J^Rを^J^Pに置き換える(^J^Rは廃棄)")。旧Command::
      // StartCommandHistory(M_cmd、"COMMAND"一覧)は^J^P(PromptKind::
      // ProgramCommand、startPromptHistoryRecall()自身の
      // sortDirectEntryList拡張コメント参照)へ統合され、以後^J^Rは
      // このswitchのdefaultへ落ちる未認識シーケンスである。)
      case 0x08:  // ^J ^H : HELP
        return handleCommand(Command::StartHelp);
      case '.':  // ^J . : SHELL、カレントディレクトリを変更
        return handleCommand(Command::ShellChangeDirectory);
      case ',':  // ^J , : SHELL、`ls -l`を実行
        return handleCommand(Command::ShellRunLsL);
      case 'w':  // ^J w : SHELL、アクセス名を挿入
        return handleCommand(Command::ShellInsertName);
      case 0x17:  // ^J ^W : SHELL、バッファのコンテンツを挿入
        return handleCommand(Command::ShellInsertBufferContent);
      // FILE(^Jf/^J^F)は2026-09-08に一度ここへ移行したが、2026-09-09、
      // 直接のユーザー判断により^Xf/^X^Fへ差し戻された――
      // leaderXPending_自身のswitch参照(理由: emacsの`^Xf`
      // (set-fill-column)は使用頻度が極めて低いと判明したため、衝突を
      // 避ける目的での移行自体が不要と判断された)。
      // 2026-09-08、キー再編で^X^Y/^X^Nから移行(全マーク/全解除)。
      // 2026-09-21、直接のユーザー依頼(USILICモード=アクセス履歴、
      // 案B): `^J^U`はファイル/ディレクトリ/manごとのソート一覧、`^Ju`は
      // 種別を問わない純粋な時系列HISTORY(直近が一番下)。従来の
      // マーク済みファイルの一覧(旧`^J^U`)は`^Ou`へ戻した。
      case 0x15:  // ^J ^U : USILIC、種別ごとのソート一覧
        return handleCommand(Command::StartUsilicHistorySorted);
      case 'u':  // ^J u : USILIC、時系列HISTORY
        return handleCommand(Command::StartUsilicHistory);
      // 2026-09-08、直接のユーザー依頼によるキー再編で^Oaから移行
      // (ARCが^Xaから^Oaへ移るための地ならし)。2026-09-20、
      // Command::ToggleHelpLanguage自体が(新HELP自身のコメント参照、
      // usilic.manが日本語専用になったため)廃止されたのに伴い、
      // "^J a"自体も未割り当てに戻した(再割り当てが必要になれば
      // 別途検討する)。
      // ^J^O(ERROR/LOG用の新設ネストしたサブリーダー): 2026-09-08、
      // 直接のユーザー依頼によるキー再編で、廃止された^X^K(usilicrc
      // 自身のF-2)サブリーダーの残り(ERROR/LOGのみ)を引き継ぐ。
      case kLeaderO:  // ^J ^O
        leaderJOPending_ = true;
        return true;  // シーケンス継続中、no-opではない
      // ^J0..^J9 : 現在の画面自身のパス文字列の(digit+1)番目の'/'の
      // 接頭辞へ、Directoryモードの`ls`へ直接ジャンプする。
      // Command::JumpToPathPrefix自身のコメント参照。
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        pendingPathPrefixDigit_ = byte - '0';
        return handleCommand(Command::JumpToPathPrefix);
      default:
        return false;  // このスライスでは認識されない^Jシーケンス
    }
  }

  if (leaderJOPending_) {
    leaderJOPending_ = false;
    if (special) return false;
    switch (byte) {
      case 'e':  // ^J ^O e : ERROR(旧^X^Ke)
        return handleCommand(Command::ShowErrorLog);
      // 2026-09-27、直接のユーザー依頼("^J^Ol改め^J^Os"): ^J^Od(data)と
      // 同じく、開く対象の名前(SHELL)の頭文字に揃える。
      case 's':  // ^J ^O s : SHELLのログ(旧^J^Ol、旧々^X^Kl)
        return handleCommand(Command::ShowShellLog);
      case 'd':  // ^J ^O d : ~/.usilic/data/(K-96、2026-09-27)
        return handleCommand(Command::OpenDataDirectory);
      case 'w':  // ^J ^O w : WORK(K-97、2026-09-27)
        return handleCommand(Command::OpenWorkList);
      default:
        return false;  // このスライスでは認識されない^J^Oシーケンス
    }
  }

  if (leaderXPending_) {
    leaderXPending_ = false;
    if (special) return false;
    switch (byte) {
      // 2026-09-08、直接のユーザー依頼によるキー再編(^Xプレフィックス
      // の廃止)で、DIR以外の全てのバイトを^O/^Jへ移行させたが、EXIT
      // (^X^C)は同日中にユーザー自身の判断で^Xへ差し戻された(^Oxを
      // 撤回)。DIRが最初から意図的にここへ残っているのと同じ理屈
      // ――Emacsの`C-x C-c`(save-buffers-kill-terminal、事実上の終了)
      // が元々同じ「終了」の意味で^X^Cを使っているため、この組も
      // 移動する理由が無い。
      case 'd':  // ^X d : DIR(ディレクトリ名を入力)
        return handleCommand(Command::StartDirectoryInput);
      case kLeaderX:  // ^X ^X : 大きく飛ぶ前の行と行き来(K-16、Command::ExchangeJumpMark参照)
        return handleCommand(Command::ExchangeJumpMark);
      // 2026-09-25、^O^Wから戻した(emacsのwrite-fileと同じ位置、
      // 2026-09-08のキー再編前の元の^X^W。^J ^C自身のコメント参照)。
      case 0x17:  // ^X ^W : バッファを作業ファイル(INFOのWORK)へ保存(K-17)
        return handleCommand(Command::SaveBufferToFile);
      case 0x04:  // ^X ^D : DIR(ディレクトリ名を選択、K-48)
        return handleCommand(Command::StartDirectoryHistory);
      case kControlC:  // ^X ^C : 終了
        return handleCommand(Command::StartExit);
      // 2026-09-09、直接のユーザー依頼により^Jf/^J^Fから差し戻し
      // (leaderJPending_自身のswitchのコメント参照)。emacsの`^Xf`
      // (set-fill-column)は使用頻度が極めて低く、うっかり押しても
      // `^G`で解消できるため、K-45/K-47のlegacy本来のキー(^Xf/^X^F)
      // へ戻すのが実用上望ましいと判断された。
      case 'f':  // ^X f : FILE(ファイル名を入力、K-45)
        return handleCommand(Command::StartFileInput);
      case 0x06:  // ^X ^F : FILE(ファイル名を選択、K-46)
        return handleCommand(Command::StartFileHistory);
      // 2026-09-10、直接のユーザー依頼("次はマクロ（^X(...）を実装
      // して欲しい"――既に実装済みで単に2026-09-08の再編で^Oへ移って
      // いただけと判明した後、"^X(/^X)/^Xeへ戻す(^Oは廃止)"と明示的に
      // 選択)により、^O(/^O)/^Oeから差し戻し。クラスコメント自身の
      // p_km節、pendingLeaderXByte_自身のコメント参照。
      case '(':  // ^X ( : マクロ記録を開始
        return handleCommand(Command::StartMacroRecord);
      case ')':  // ^X ) : マクロ記録を終了
        return handleCommand(Command::StopMacroRecord);
      case 'e':  // ^X e : 記録済みマクロを実行
        return handleCommand(Command::PlayMacro);
      default:
        return false;  // このスライスでは認識されない^Xシーケンス
    }
  }

  if (leaderOPending_) {
    leaderOPending_ = false;
    if (special) return false;
    switch (byte) {
      case 'g':  // ^O g : GUIモードをトグル(このポート自身の追加であり、usilicrcのK-?のキー割り当てではない)
        return handleCommand(Command::ToggleGuiMode);
      case 'h':  // ^O h : モードライン表示/非表示を切り替え(K-25、
                 // 2026-09-12にToggleScrollModeから付け替え。
                 // Command::ToggleModeLineVisibility自身のコメント参照)
        return handleCommand(Command::ToggleModeLineVisibility);
      // 2026-09-08、直接のユーザー依頼によるキー再編("^Ol⇒^Oo（←
      // ファイル名以外はomission）")で'l'から移行。
      case 'o':  // ^O o : `ls`出力の形式を切り替え(K-26、旧^O l)
        return handleCommand(Command::ToggleLsMode);
      // 2026-09-12追加、直接のユーザー依頼("`-l`以外も変更できるように
      // したい...`^Ol`が`-r`トグル"): 上の改名で空いた^Olへ新規に
      // 割り当てる(ToggleLsReverse自身の宣言コメント参照)。
      case 'l':  // ^O l : `ls`のソート順を逆順(-r)にトグル
        return handleCommand(Command::ToggleLsReverse);
      // 2026-09-25のキー再編(^J ^C自身のコメント参照): ^O^Kは^J^Kへ、
      // ^Jy/^Jnは^Oy/^Onへ(「^Oは設定・切替・操作」の切り分け)。
      case 'y':  // ^O y : 全マーク(CP、MV、RM、ARC、旧^Jy)
        return handleCommand(Command::MarkAll);
      case 'n':  // ^O n : 全マーク解除(CP、MV、RM、ARC、旧^Jn)
        return handleCommand(Command::ClearAllMarks);
      case kCarriageReturn:  // ^O ^M : 「行単位で読む」(K-23、M_fp)
        return handleCommand(Command::ReadFileNoWrap);
      // ^O^C / ^O^Y(M_copy/M_paste): usilicrc自身のK-29/K-30の
      // エントリ("F-3 C-c \"Copy string\"" / "F-3 C-y \"Paste string
      // (Command-line)\"")が実際の文書化されたキー割り当てである
      // ――str.cの「コンパイル時に組み込まれた」デフォルトテーブル
      // (`key_c[M_copy] = (K_o<<8)+'c'`、`key_c[M_paste] = (K_o<<8)+'p'`)
      // は、このポートの他の全てのK-nnのキー割り当てとまさに同じよう
      // に、ロード時にusilicrcによって上書きされ、このポートは生の
      // Cのデフォルトではなくusilicrc自身のキー割り当てを出荷する
      // ――これの最初の版が代わりに^Oc/^Opを使った後の実際のユーザー
      // 報告("ペーストの方法がわからない。旧では...^O^Yでペーストな
      // んだが")で確認済み。
      case 0x03:  // ^O ^C(K-29)
        return handleCommand(Command::EnterCopyMode);
      case 0x19:  // ^O ^Y(K-30)
        return handleCommand(Command::PasteClipboard);
      // 2026-09-18削除、直接のユーザー依頼("GREPとFGREPを無くしたい。
      // ^[i→^S（リテラル）、^O^S→^J^S（拡張正規表現）に限定"): 2026-09-05
      // にここへ配線した^O^S(FGREP)を廃止し、素の^S(当時の
      // Command::StartGrep、今はGrepSyntax::Fixed)一本にリテラル検索を
      // 統合した(2026-09-19追記: その素の^S自身も、同日
      // Command::StartIncrementalGrepへ――インクリメンタルサーチへ
      // ――移った、それ自身の宣言コメント参照)――^O^Sは
      // 以後どのコマンドにも解決されない未認識シーケンスとなり、下の
      // defaultへ流れ落ちる。^J^S(EGREP)は変更無し(dispatchLeaderByte()
      // 自身のleaderJPending_のswitch参照)。
      // 2026-09-08、直接のユーザー依頼によるキー再編(^Xプレフィックス
      // の廃止): 以下は全て^Xから移行。
      case 'c':  // ^O c : CP(旧^X c)
        return handleCommand(Command::StartCopy);
      case 'm':  // ^O m : MV(旧^X m)
        return handleCommand(Command::StartMove);
      case 'r':  // ^O r : RM(旧^X r)
        return handleCommand(Command::StartRemove);
      // 2026-09-21、直接のユーザー提案("^Ouを復活では？"): マーク済みの
      // 複数項目を渡して閲覧する機能はCP/MV/RMと同じ^O系。従来のマーク
      // 済みファイル/ディレクトリの一覧(旧^J^Y)をここへ戻した。
      case 'u':  // ^O u : USILIC、マークしたファイル/ディレクトリの一覧
        return handleCommand(Command::StartUnderlineView);
      case 'e':  // ^O e : (ls)の.epubをARCで開く(K-100、2026-09-29)
        return handleCommand(Command::OpenEpubAsArchive);
      case 'a':  // ^O a : ARC展開(旧^X a、K-58)――^O aはここへ移る
                 // 前にHELP/KEYの表示言語切り替えが占めていたが、
                 // それは^J aへ先に移した(kKeyTable自身の"^Ja"コメント
                 // 参照)。
        return handleCommand(Command::StartArchiveExtract);
      case 's':  // ^O s : SHELLを再起動(旧^X s、K-60)
        return handleCommand(Command::RestartShell);
      // 2026-09-08追加、直接のユーザー依頼によるキー再編: SHELLモード
      // 内では素の^H/^L/^Iが行編集機構(バックスペース/未ハンドルで
      // 化ける/Tab補完)に奪われ、それぞれKEY/再描画/INFOへ到達
      // 不能なためのエスケープハッチ。当時^O^Sは既存のFGREPが占めて
      // いたためGREPのハッチだけは'/'を使った(kKeyTable自身の"^O/"
      // コメント参照)――2026-09-18に^O^S自体を廃止した後も、'/'の
      // 方はそのまま残している(2026-09-19の一本化以後は、素の^Sと
      // 同じくインクリメンタルサーチへ配線する――
      // Command::StartIncrementalGrep自身の宣言コメント参照)。
      // 2026-09-20、直接のユーザー依頼("^Hで新HELP、SHELLを含む
      // プロンプトからは^J^H")によりこの"^O^H"(旧K-2/KEY)エスケープ
      // ハッチ自体を削除した――^Jはbyte(bare ^H/^L/^I)とは異なり
      // SHELL自身の行編集機構に奪われないので、"^J^H"はこの下の
      // isShellSafeNavigation経由の一般的な仕組み(shellReturnMode_へ
      // 切り替えてから該当コマンドをディスパッチする、すぐ下のコメント
      // 参照)で既にSHELL内から到達可能――KEY専用の個別ハッチは
      // 不要になった。
      // 2026-09-12修正、直接のユーザー依頼("`^O^L`がSHELLでの`^L`への
      // 対処なら^Sと同じ扱いでいいと思う"): 本来この`^O^L`はSHELLモード
      // 内で素の`^L`(usilic自身の再描画)がSHELL自身の行編集機構に
      // 奪われてしまう分の、"SHELL内から"限定のエスケープハッチとして
      // 追加されたものだった(すぐ上のコメント参照)が、実装には
      // モードガードが一切無く、Directoryモードでも同じくRedrawが
      // 発動してしまっていた。ここへ`currentMode_ ==
      // ApplicationMode::Shell`のガードを追加し、Directoryモードでの
      // `^O^L`は新設のCycleLsSortKey(-t/-S/-k巡回、その宣言コメント
      // 参照)へ回す――^O^S(FGREP)がGREPの元の`^S`割り当てを塞いだ際に
      // GREPのハッチだけ`/`へ移したのと同種の、キー空間の奪い合いへの
      // 対処。
      case 0x0c:  // ^O ^L : 再描画(SHELL内から)/ソートキー巡回(Directory)
        if (currentMode_ == ApplicationMode::Shell) return handleCommand(Command::Redraw);
        return handleCommand(Command::CycleLsSortKey);
      case 0x09:  // ^O ^I : INFO(SHELL内から)
        return handleCommand(Command::ShowUsilicBanner);
      case '/':  // ^O / : 検索(SHELL内から、2026-09-19以後はインクリメンタル)
        return handleCommand(Command::StartIncrementalGrep);
      // 2026-09-08、当初はMeta(ESC)機構経由の"@F"/"@B"(将来のAlt表記
      // の先取り)として実装したが、同日中に「メタキーをAltにする話
      // 自体を見合わせたい」とのユーザー自身の訂正を受けて撤回し、
      // 他のSHELLエスケープハッチと同じ^Oプレフィックス方式へ差し
      // 替えた(kKeyTable自身の"^O^F"/"^O^B"エントリのコメント参照)。
      case 0x06:  // ^O ^F : ページ送り(SHELL内から)
        return handleCommand(Command::PageDown);
      case 0x02:  // ^O ^B : ページ戻し(SHELL内から)
        return handleCommand(Command::PageUp);
      default:
        return false;  // このスライスでは認識されない^Oシーケンス
    }
  }

  if (!special && byte == kLeaderJ) {
    leaderJPending_ = true;
    return true;  // シーケンス継続中、no-opではない
  }
  if (!special && byte == kLeaderX) {
    leaderXPending_ = true;
    return true;  // シーケンス継続中、no-opではない
  }
  if (!special && byte == kLeaderO) {
    leaderOPending_ = true;
    return true;  // シーケンス継続中、no-opではない
  }
  if (!special && byte == kEscape) {
    escPending_ = true;
    return true;  // シーケンス継続中、no-opではない
  }

  return false;  // dispatchByte()/feedShellByte()自身のガードにより到達不能、安全のため網羅しておく
}

// dispatchByte()(本体のロジック、それ自身のbody参照)をp_km記録の
// 記帳で包む――なぜこれがリーダーXのシーケンスの先頭バイトを即座に
// 記録するのではなくpendingLeaderXByte_に保持しておくのかについては
// クラスコメント自身のp_kmセクション参照。2026-09-08、直接のユーザー
// 依頼によるキー再編で^O(/^O)/^OeがStartMacroRecord/StopMacroRecord/
// PlayMacroを一時的に引き継いだのに伴い、この保留のロジック自体も
// 元の^X用から^O用へ付け替えたことがあったが、2026-09-10、直接の
// ユーザー依頼("^X(/^X)/^Xeへ戻す(^Oは廃止)")により^X用へ差し戻した
// ――pendingLeaderXByte_自身のコメント参照。
// 2026-09-25追加、直接のユーザー要望("(stdin)は^Gおよび他のモードが
// 発動した時点で抹消（CP/MV/RM等への配慮）"、"(stdin)はFILE履歴にも
// 残らない一時的な存在なのでそこから離れたら抹消でいい。文字通り何も
// 無いので^Gでも戻れない"): (stdin)ページャ(パイプ入力、^O^Mの
// 「行単位で読む」、PROGRAMの出力、SHELLからのusilicへのパイプ)を
// 表示していたのに、キー1回/コマンド1回の処理の後にFILEモードでなく
// なっていたら、(stdin)を離れたとみなして抹消する。個々のモード切替
// 経路(21箇所以上)を1つずつ潰すのではなく、ここ(feedByte()/
// handleCommand()の最も外側の呼び出し)で前後を比べる――入れ子の
// 呼び出しは最も外側だけが判定する。
bool Application::stdinPagerActive() const {
  return currentMode_ == ApplicationMode::File && fileMode_.isPagerMode() &&
         (fileMode_.pagerReturnsToDirectory() || fileMode_.pagerReturnsToProgram() || fileMode_.pagerReturnsToShell());
}

// stdinPagerActive()自身のコメント参照。他のモード(CUT/COPY/GREP/
// SHELL)が戻り先として「FILE(=この(stdin))」を記録していたら、
// (stdin)自身の^Gの行き先(SHELLから来たものはSHELL、それ以外は
// fileReturnMode_)へ差し替え、(stdin)には二度と戻れないようにする。
void Application::discardStdinPager(bool returnsToShell) {
  ApplicationMode target = returnsToShell ? ApplicationMode::Shell : fileReturnMode_;
  if (target == ApplicationMode::File) target = ApplicationMode::Directory;
  for (ApplicationMode *mode : {&cutReturnMode_, &copyReturnMode_, &grepReturnMode_}) {
    if (*mode == ApplicationMode::File) *mode = target;
  }
  if (shellReturnMode_ == ApplicationMode::File) {
    shellReturnMode_ = (target == ApplicationMode::Shell) ? ApplicationMode::Directory : target;
    shellReturnName_ = previousModeName(shellReturnMode_);
  }
  fileMode_.closeStream();
}

// usilicrcのKEY節(2026-09-25実装、KeyBindings自身のクラスコメント参照):
// 置き換え表を、今どの範囲で効かせてよいか。本体が接頭キーの途中や行番号の
// 入力中、COPY画面の中ならNone(照合を始めない)。プロンプト入力中・SHELL画面・
// 文字入力を受け付ける一覧では、文字キーは文字として入力されるので、接頭キー
// (^X/^J/^O/ESC)で始まる列だけ(LeaderOnly)。それ以外の通常の画面ならAll。
KeyBindings::Context Application::keyBindingContext() const {
  if (escPending_ || leaderJPending_ || leaderJOPending_ || leaderXPending_ || leaderOPending_ || leaderCPending_ ||
      gotoLineGPending_ || gotoLineDigitsPending_)
    return KeyBindings::Context::None;
  if (currentMode_ == ApplicationMode::Copy) return KeyBindings::Context::None;
  if (tabCompletionContext_ || promptKind_ != PromptKind::None || currentMode_ == ApplicationMode::Shell)
    return KeyBindings::Context::LeaderOnly;
  if (currentMode_ == ApplicationMode::ListBrowser && promptHistoryContext_ &&
      (promptHistoryContext_->kind == PromptKind::GrepPattern || promptHistoryContext_->kind == PromptKind::CutPattern ||
       promptHistoryContext_->kind == PromptKind::ManName || promptHistoryContext_->kind == PromptKind::RecentFile ||
       promptHistoryContext_->kind == PromptKind::RecentDirectory))
    return KeyBindings::Context::LeaderOnly;
  return KeyBindings::Context::All;
}

// keyBindingContext()自身のコメント参照。利用者が押したキー列を既定のキー列へ
// 置き換えてから、下のfeedUntranslatedByte()(本体の入口)へ渡す。既定のまま
// (isIdentity())なら素通しで、挙動は一切変わらない。特殊キー(矢印等)はKEY節の
// 対象外なので、溜めているキーがあれば先にそのまま渡してから処理する。
bool Application::feedByte(int byte, std::optional<KeyInput::SpecialKey> special) {
  const KeyBindings &bindings = configuration_.keyBindings();
  if (bindings.isIdentity()) return feedUntranslatedByte(byte, special);
  if (special) {
    std::vector<int> flushed;
    flushed.swap(keyBindingPending_.bytes);
    for (int b : flushed) feedUntranslatedByte(b, std::nullopt);
    return feedUntranslatedByte(byte, special);
  }
  std::vector<int> out = bindings.feed(byte, keyBindingContext(), keyBindingPending_);
  if (out.empty()) return true;  // 溜めている途中、または無効化されたキー
  bool result = false;
  for (int b : out) result = feedUntranslatedByte(b, std::nullopt);
  return result;
}

bool Application::feedCanonicalByte(int byte) { return feedUntranslatedByte(byte, std::nullopt); }

bool Application::feedUntranslatedByte(int byte, std::optional<KeyInput::SpecialKey> special) {
  clearTransientNotice();  // showTransientNotice()自身の宣言コメント参照(どのキーでも消える)
  bool outermost = stdinPagerCheckDepth_ == 0;
  bool wasStdin = outermost && stdinPagerActive();
  bool wasReturnsToShell = wasStdin && fileMode_.pagerReturnsToShell();
  CutLeaveSnapshot cutBefore = cutLeaveSnapshot();
  ++stdinPagerCheckDepth_;
  bool result = feedByteImpl(byte, special);
  --stdinPagerCheckDepth_;
  if (outermost) saveCutPositionIfLeft(cutBefore);
  if (wasStdin && currentMode_ != ApplicationMode::File) discardStdinPager(wasReturnsToShell);
  if (outermost) showBufferLimitNoticeIfHit();
  return result;
}

bool Application::feedByteImpl(int byte, std::optional<KeyInput::SpecialKey> special) {
  bool wasLeaderXPending = leaderXPending_;
  bool wasRecording = recordingMacro_;

  bool result = dispatchByte(byte, special);

  if (wasRecording) {
    if (wasLeaderXPending) {
      if (recordingMacro_) {
        // 依然として記録中――このバイトはStartMacroRecord/
        // StopMacroRecord/PlayMacroへは解決されなかったので、
        // 保留していた先頭の^Xバイトとこのバイトの両方をコミットする。
        if (pendingLeaderXByte_) recordedMacro_.push_back(*pendingLeaderXByte_);
        recordedMacro_.push_back({byte, special});
      }
      // else: 記録がちょうどオフになった(StopMacroRecordが解決した)
      // ――両方のバイトを捨てる、それらは制御シーケンス自体である。
      pendingLeaderXByte_.reset();
    } else if (!special && byte == kLeaderX) {
      pendingLeaderXByte_ = RecordedStep{byte, special};  // 2番目のバイトが解決するまで保留しておく
    } else {
      recordedMacro_.push_back({byte, special});
    }
  }

  return result;
}

bool Application::startMacroRecording() {
  recordingMacro_ = true;
  recordedMacro_.clear();
  pendingLeaderXByte_.reset();
  return true;
}

bool Application::stopMacroRecording() {
  if (!recordingMacro_) return false;
  recordingMacro_ = false;
  return true;
}

// 記録された各バイトを、feedByte()では「なく」dispatchByte()経由で
// 直接再生する、これでアクティブな記録セッションの内部から再生された
// マクロが誤って自己記録されることは決して無い――クラスコメント参照。
bool Application::playRecordedMacro() {
  if (recordedMacro_.empty()) return false;
  for (const auto &step : recordedMacro_) dispatchByte(step.byte, step.special);
  return true;
}

bool Application::isRecordingMacro() const { return recordingMacro_; }
std::size_t Application::recordedMacroLength() const { return recordedMacro_.size(); }

// このルーティングの範囲(と、^[sを本物の端末に結線する際のKeyInput/
// エスケープシーケンスの隙間)についてはクラスコメント参照。Left/
// Rightはコマンドラインのカーソルを動かす;Up/Down/PageUp/PageDown
// (スクロールバックのページ送り/コマンド履歴)は、ここではまだ意図的
// に処理していない。
bool Application::feedShellByte(int byte, std::optional<KeyInput::SpecialKey> special) {
  // 2026-09-18追加、HELP.txt 2.13章「LIST」の実装、Shell::tabArmed_
  // 自身の宣言コメント参照(legacyの`f_list`フラグと同じ役割): TAB
  // 以外の任意のバイトを処理する「前」に、必ずここでリセットする
  // ――この関数自身の以降の分岐がどこで早期returnしても、Tab以外の
  // バイトなら確実にリセットされることを保証するため、関数の最初の
  // 行に置く。
  if (byte != 0x09) shell_.resetTabArm();
  // ^Q(quoted insert、2026-09-10追加): feedPromptByteInner()自身の
  // ^Qのコメント参照。すぐ下の^A/^E/^B/^F/^D/ESC/^U/^K/^Yと全く同じ
  // 「promptEditor_については既に修正済みだが、shell_.commandLine()
  // という別個のLineEditorインスタンスへは一度も反映されていない」
  // 隙間――実際のユーザー報告("SHELLでgrep '^V^I' *.[ch]とやっても
  // ^Iが入らない。^Q^Iでは^Qだけが渡る"): ^Qがどの特殊バイトにも
  // 一致せず下のinsertByte()へ素通しされ(それ自身の反転表示された
  // "@"オフセットのグリフとしてレンダリングされる)、続くTabは
  // すぐ下のkClearTo0x09チェックがshell_.onTab()(ファイル名補完)へ
  // 無条件に渡してしまい、リテラルに挿入する手段が無かった。
  // feedPromptByteInner()と同じ理屈で、他のどの特殊バイト(Tab/^G/
  // リーダーキー等)より「先に」チェックする――一度アームされると、
  // 次のバイトはそれが他に何を意味するかに関わらずリテラルに挿入
  // される。
  // 【この修正「だけ」では実は不十分、PTY実機検証で判明】
  // これでshell_.commandLine()というusilic側のローカルなバッファへ
  // リテラルなTabバイトを組み込めるようにはなるが、SHELLモードは
  // (feedPromptByteInner()側のGREP/CUT等とは違い)組み立てた行を
  // 丸ごと、本物のインタラクティブな子プロセス(zsh/bash、それ自身の
  // 行編集機構ZLE/readlineを持つ)へそのまま送るだけなので、送信された
  // 生のTabバイトを、その「子プロセス自身」のZLE/readlineが(あたかも
  // 人間がその場でTabキーを押したかのように)「そちらの」補完機能の
  // トリガーとして解釈してしまい、結局リテラルには挿入されない
  // (実際のPTY確認: この修正だけを使った^Q^Iでは"grep '<TAB>' *.txt"
  // は"grep 'AAABBB'"のようにTabが跡形も無く消える――子シェル自身の
  // 補完が"何も補完しない"という形で静かにバイトを飲み込むため)。
  // 正しく動作させるには、子シェル自身の同じquoted-insertの慣習
  // (zsh/bashのデフォルトでは^V)もコマンド文字列の中にそのバイトの
  // 直前として一緒に埋め込む必要がある――つまりusilic側は
  // "^Q^V"(usilic自身の局所バッファへリテラルな^Vを挿入、実際には
  // ^Vはここで特別扱いされていないので無印のままでも素通しで挿入
  // される)、続けて"^Q<Tab>"(この修正で初めて可能になる、usilic
  // 自身の局所バッファへのリテラルなTab挿入)と入力する――送信される
  // 文字列に埋め込まれた"^V<Tab>"を、子シェル自身のZLE/readlineが
  // 「本物の対話セッションでユーザーが^V^Iと打った」のと区別が付かず
  // 同じに解釈し、初めてTabがリテラルに挿入される(実際のPTY確認: この
  // 2段構えでgrepが実際に一致を見つけることを確認済み)。この修正
  // 自体は、そのための「usilic側の半分」(局所バッファへの挿入)を
  // 提供するものであり、単独で完結する解決策ではない。
  if (shell_.commandLine().quotedInsertPending()) {
    shell_.commandLine().clearQuotedInsert();
    shell_.commandLine().insertByte(static_cast<unsigned char>(byte));
    shell_.commandLine().redraw();
    return true;
  }
  if (byte == kControlQ) {
    shell_.commandLine().toggleQuotedInsert();
    return true;
  }
  // Shellモードの内部からのグローバルなリーダーキーへのアクセス:
  // ^J/^X/^X^K/^O/ESC。実際のユーザー報告(2026-08-02、成熟した
  // legacyビルドと並べて比較)により、そこでは^X^F/^X^C等はSHELLの
  // 内部から直接動くことが判明した、これはこの機能が存在する前は
  // ここでは黙って何もしていなかった: currentMode_==Shellは、リーダー
  // キーへのアクセスが一切無いまま、全てのバイトをこの関数へ直接
  // ルーティングしていた。
  //
  // ESCも含める(2026-08-17修正、以前の意図的な除外を覆す): 実際の
  // ユーザー報告("^[は今も利かない...いずれも他の文字列入力ルーチン
  // では直っていたのでこれも似たようなケースなの？")により、これが、
  // すぐ上の^A/^E/^B/^F/^D修正と「まさに同じ」「feedPromptByte()で
  // 汎用的に修正されたが、ここでは一度も反映されていない」隙間で
  // あることが指摘された。以前の除外自身の理屈――「本物の対話的な
  // シェルは、それ自身の行エディタ/Metaキー/viモードのために生の
  // ESCを常時必要とする」――は、「この」ポートについては当てはまら
  // ないと判明した: shell_.commandLine()は完全にローカルな
  // LineEditorであり(legacyのshell.c自身の非パススルーの設計に
  // 合わせている、CLAUDE.mdのアーキテクチャの注記参照)、ここでは
  // sendControlByte()(下記、^Cのみ)とsubmitLine()(Enterで、
  // 組み立てられた「行全体」)以外、生のキー入力ごとのバイトを子の
  // ptyへ転送することは一切無い。子のシェルは、usilic自身のエディタ
  // でコマンドを組み立てている間にタイプされたESCをどのみち一度も
  // 見ないので、ここでそれをリーダーバイトとして扱う(^J/^X/^Oに合わ
  // せ、feedPromptByte()のescPending_の処理にも合わせる)ことは、
  // 欠けていたグローバルなコマンドへのアクセスを追加するだけである
  // ――本物のシェルの使用から何かを奪うことはない。(do_prog()経由で
  // 起動される`vim`/`less`のような、正真正銘の生の、対話的な子プロ
  // セスは、これが触れない、完全に別個の、本物のpty経由のパススルー
  // のコードパスである。)
  // ESC f/b/d/DEL: feedPromptByteInner()の同じ横取り参照。下の
  // shellLeaderPendingの処理(SHELLから抜けるモード切り替え)より前に行う。
  if (!special && applyWordEditEscape(shell_.commandLine(), byte)) return true;
  bool shellLeaderPending =
      leaderJPending_ || leaderJOPending_ || leaderXPending_ || leaderOPending_ || escPending_;
  if (!special && (shellLeaderPending || byte == kLeaderJ || byte == kLeaderX || byte == kLeaderO || byte == kEscape)) {
    // 2026-08-26のバグ修正(実際の報告: `df`/`ls`を実行してからESC-<
    // が、SHELLに留まる代わりに予期せずDirectory自身の(ls)へ着地
    // した): ESC-</ESC->/ESC-v(GoToTop/GoToBottom/PageUp)は、
    // Directory固有の意味を一切持たない、プレーンなactiveView()汎用の
    // スクロールコマンドである――それらはShell「自身の」scrollback_
    // に対して完璧に十分定義されている(Command::GoToTop自身の
    // ハンドラ参照、単なるactiveView().goToTop())――しかし、下の
    // 一括の事前切り替え(本当にDirectoryの状態を先に必要とする
    // ^X^F/^X^Dスタイルのコマンドのために追加された)は、どれである
    // かに関わらず、完了した「全ての」リーダーシーケンスについて
    // 無条件に実行されていたので、これらが実際に実行される時点で
    // 既にactiveView()はDirectory自身のビューになっており、代わりに
    // 「それ」をスクロールし、望まれない副作用としてShellから離れて
    // いた。この正確な問題を持つと確認済みの「3つの」バイトだけに
    // 触れる形で、他のあらゆる、切り替えを必要とするかもしれないし
    // しないかもしれないリーダーコマンドを列挙しようとするのではなく、
    // ここでは狭くこれらを除外した。
    // 2026-08-27追加、実際の報告: SHELLの内部からの^J^K
    // (GoToFileSelector)が、^J^Kが実際に約束していること
    // ("現在の[ディレクトリ]の本物の`ls`一覧へ移動する")を行う代わり
    // に、`shellReturnMode_`(SHELLがどこから入場したかというモード)
    // に着地していた――下の一括の事前切り替えが、goToFileSelector()が
    // currentMode_==Shellを見る機会を得るより「前に」実行されていた
    // ので、それがチェックする時点では、currentMode_は既に
    // shellReturnMode_になっており、それ自身の汎用のモードごとの分岐
    // (Shellを含まない)は単に黙って何もせず、その叩き潰された
    // currentMode_を望まれない副作用としてそのまま残していた――
    // まさに、すぐ上のESC-</ESC->/ESC-vの除外が別のコマンドの3つ組
    // について既に一度修正したのと同じ種類のバグである(それ自身の
    // コメントの「本物のタイプミスについては...受け入れる」という
    // トレードオフ参照、これは、それ自身がShellから到達されることを
    // サポートしないハンドラを持つ、他のあらゆる有効なシーケンスも
    // 併せてカバーすると判明した――元々想定していたより広い)。
    // goToFileSelector()が実際のcurrentMode_==Shellを見て、それを
    // 明示的に処理できる(それ自身の新しいShellの分岐参照)よう、
    // ここでは除外した。
    // ^J^C(2026-09-25のキー再編で^J^Kから移った、dispatchLeaderByte()の^J ^C自身のコメント参照)。
    bool isShellSafeGoToFileSelector = leaderJPending_ && byte == kControlC;
    // 2026-09-20追加、直接のユーザー報告("^O^S/^J^Sが利かない"):
    // すぐ上のisShellSafeGoToFileSelectorと全く同じ理屈――FGREP
    // (^O^S)/EGREP(^J^S)は、feedShellByte()自身の新しいプレーンな
    // ^S分岐(すぐ下のkControlS単独ケース)と同じく、Shell自身の
    // scrollbackを検索対象にすべきなのに、この事前切り替えのせいで
    // dispatchLeaderByte()に到達する「前」にcurrentMode_が
    // shellReturnMode_へ書き換えられてしまい、startGrepPrompt()内の
    // grepReturnMode_ = currentMode_が、Shellではなくshellから抜けた
    // 先の無関係なモード(例えばDirectory)を記憶してしまっていた――
    // 結果、検索対象がShellのscrollbackではなくそちらになり、
    // ユーザーからは「^O^S/^J^Sが利かない」ように見えていた。
    // 2026-09-18追記(Command::StartIncrementalGrep自身のコメント参照、
    // GREP/FGREP統合): ^O^S自体はdispatchLeaderByte()のleaderOPending_switchから
    // 廃止済みで、以後は未認識シーケンスとして下のdefaultへ落ちる――
    // leaderOPending_の側の条件は、その未認識シーケンスがShellから
    // 離れる副作用(すぐ下のコメント参照、"本物のタイプミス"には既に
    // 許容されている程度の軽微な副作用)を及ぼさないための、実害の無い
    // 余分な保護として残してある。
    bool isShellSafeGrep = (leaderOPending_ || leaderJPending_) && byte == kControlS;
    bool isShellSafeNavigation = (escPending_ && (byte == '<' || byte == '>' || byte == 'v')) ||
                                 isShellSafeGoToFileSelector || isShellSafeGrep;
    if (shellLeaderPending && !isShellSafeNavigation) {
      // シーケンスを完了させる――ディスパッチする「前に」、ユーザーが
      // まず^Gを押したかのように、まさに正確に、SHELLがそこから入場
      // したモードへ切り替え戻す(legacyに合わせ、KEYモード自身の
      // executeSelectedKeyBinding()の前例にも合わせている): いくつか
      // の下流のコマンド(^X^F向けのstartListBrowser()等)は、それら
      // 自身のcurrentMode_==Directoryスタイルのガードを持っており、
      // 依然としてShellのままだと、そうでなければそれらを完全に拒否
      // してしまう。認識されないシーケンス(本物のタイプミス)は、
      // それでも副作用としてShellから離れてしまう――それのための
      // ロールバックの仕組みを追加するのではなく、稀な、軽微な
      // エッジケースとして受け入れる。
      currentMode_ = shellReturnMode_;
      activeView().redraw();
    }
    return dispatchLeaderByte(byte, special);
  }

  if (special) {
    switch (*special) {
      case KeyInput::SpecialKey::Left:
        shell_.commandLine().moveLeft();
        shell_.commandLine().redraw();
        return true;
      case KeyInput::SpecialKey::Right:
        shell_.commandLine().moveRight();
        shell_.commandLine().redraw();
        return true;
      default:
        return false;
    }
  }

  // 2026-09-20追加、直接のユーザー報告(PTYハーネスでの再現確認:
  // "usilic -c git/usilic→^[s→head C^I^M^Sc^M→FGREPのはずなのに
  // ^N/^PがSHELLの動作となる。^Gでも戻らない"): dispatchByte()自身の
  // grepInPlaceActive_向けの^N/^P横取り(この関数の少し上のコメント
  // 参照、"Shell/Copyの上の2分岐と同じ優先度")は、currentMode_==Shell
  // の間は一度もここへ到達しない――dispatchByte()がcurrentMode_==Shell
  // を見た時点でfeedShellByte()(この関数自体)へ丸ごと委譲してしまう
  // ため。同じ横取りをここに複製する必要がある。しかも
  // activateGrepInPlace()自身がgrepInPlaceOriginMode_==Shellの際に
  // shell_.beginHistoryBrowsing()を呼ぶ(スクロールバックの閲覧状態を
  // 借用するため)ので、in-place検索中は下のhistoryBrowsing()向けの
  // ^N/^P/^G分岐がすぐ下で「代わりに」発火してしまっていた――ここで
  // grepInPlaceActive_を先にチェックすることで、Command::Back自身の
  // wasGrepInPlaceActiveコメント(application.cpp、handleCommand()冒頭)
  // と同じ「一番内側の状態から先にキャンセルする」慣習に揃える。
  if (grepInPlaceActive_ && !special && (byte == 0x0e || byte == 0x10)) {
    return handleCommand(byte == 0x0e ? Command::GrepInPlaceNext : Command::GrepInPlacePrevious);
  }
  if (grepInPlaceActive_ && byte == kControlG) {
    deactivateGrepInPlace();
    return true;
  }

  // 2026-08-26のバグ修正(実際の報告: `df`/`ls`を実行してから、^Gから
  // 組み立てられた汎用のマクロ――"<"/">"等、resolveCommand()自身の
  // K-22/K-11スタイルのキー割り当て参照――が、SHELLに留まる代わりに
  // 予期せずDirectoryモードの(ls)へ着地した): 他の「全ての」キー
  // (下の「閲覧中に他のどのキーでも戻る」ブロック参照)とは異なり、
  // ここでの^Gは、以前はhistoryBrowsing()を先にチェックせず、無条件
  // にShellモードから離れていた――そのため"<"("Select, back, cursor
  // up, select"、押されたどのモードでもリストを後方へ巡回すること
  // を意図している)のようなマクロは、それに埋め込まれた^Gを、本当に
  // Shellではないリストの閲覧の文脈からのプレーンな^G押下がそうする
  // であろう「履歴閲覧から戻る、Shellには留まる」ではなく、「Shell
  // を完全に離れる」として実行していた。「閲覧していない」場合に
  // のみ実際にreturnFromShell()を呼ぶ――他の全てのキー自身の「まず
  // 戻る」という慣習に合わせている――そのため最初の^Gは閲覧を
  // キャンセルし(Escape/Backが慣習的にまず最も内側の文脈をキャン
  // セルするように)、2回目のもの(今は静止している状態から)が
  // 実際にShellを離れることになる。
  if (byte == kControlG) {
    if (shell_.historyBrowsing()) {
      shell_.exitHistoryBrowsing();
      shell_.redrawPromptRow();
      return true;
    }
    return returnFromShell();
  }
  // K-0(^T、ShowUsilicBanner)は、以前は他の場所で既に必要だった
  // ^X^F等と同じ「接頭辞の無いキーがこの関数自身のコマンドライン
  // 編集のフォールバックへ流れ落ちる」という特別扱いの修正を受けて
  // いた(2026-08-18、"SHELLで^Tが利かない")。2026-08-27: ^Iへ割り当て
  // 直された("INFO"へのリネームについてはkKeyTable自身のコメント
  // 参照)――アクティブなSHELLセッションの内部では^I/Tabは既に本物の、
  // ハイジャックしてはならない期待された意味(shell_.onTab()、すぐ下)
  // を持っているので、移すのではなくここでは削除された。INFOはもう
  // 単純にShellモードからは到達できない、意図的なトレードオフである
  // (本物のbashの補完の方が、バナー画面へ到達するもう1つの手段より
  // ここでは明らかに重要である)。
  // 下の^P/^Nと閲覧中のEnterは、legacyのshell.cのM_kup/M_kdnから移植
  // された: カーソルがscrollback_「自身の」lines_(別配列ではなく、
  // 実際のコンテンツが生きている、同じ統一されたプロンプト+出力の
  // 履歴)を直接動く――このデータモデルの半分についてはShell::
  // freezePromptLine()自身のコメント参照。実際のユーザー報告
  // (2026-08-02)は、これの以前の版(合成のRingCategory::
  // ShellCommandに裏打ちされたインデックス、実際に画面に映っている
  // ものからは完全に切り離されている)が「vibe coding」だったと指摘
  // した: legacyの実際の仕組みを先に読むのではなく、既存の分割された
  // scrollback_/commandLine_の設計の上に取り付けるのに最も簡単な
  // ものへ手を伸ばしていた。ContentView::moveUp()/moveDown()は既に、
  // ここで必要とされる、境界でベルを鳴らすカーソル移動という正確な
  // 意味論(他の場所ではプレーンなリストのナビゲートに使われている)
  // を実装しているので、結局^P/^Nはそれら自身の専用の状態を全く
  // 必要としない。
  if (byte == 0x10) {  // ^P
    // 2026-08-25のバグ修正(実際の、注意深く再現された報告: "③を
    // 飛び越えて②へ"――全体の診断についてはShell::historyBrowsing()
    // 自身の宣言コメント参照): 静止状態からの「最初の」^P押下は、
    // それを即座に「越えて」動くのではなく、現在の――既に正しい――
    // 最新のエントリの「上に」着地しなければならない、そのためこれは
    // 「既に」閲覧中の場合にのみmoveUp()を呼ぶ;最初の押下は単に
    // (今は独立した、もはやインデックスから導出されない)フラグを
    // 反転させるだけである。
    if (!shell_.historyBrowsing()) shell_.beginHistoryBrowsing();
    else shell_.scrollback().moveUp();
    shell_.redrawPromptRow();  // moveUp()はcommandLineRow()を空白にすることがある――それ自身のコメント参照
    moveDisplayCursorToShellHistoryLine();
    return true;
  }
  if (byte == 0x0e) {  // ^N
    // 2026-08-26のバグ修正(実際の、注意深く再現された報告: "②へ
    // 行ったきり^Nでも③へ戻らない"): 上の^P自身の2026-08-25の修正が、
    // cursorIndex_をその静止床から動かさずに、最初の押下を正しく
    // 最新のエントリの上に着地させる一方で、^Nにはそこに着いた後、
    // 対称的に「外へ」戻る方法が無かった――scrollback_自身の
    // moveDown()はlines_の「内側」でしか動かないので、既にその最後の
    // インデックス(最新のエントリ、まさに1本のスクロールバック行
    // ――折り返されていない1つのコマンド――が、たった「1回」の^P
    // 押下の後にあなたを座らせる場所)にいる時点で、それは永久に
    // no-opのビープになり、生きたプロンプト行へ戻る経路が全く無くなる
    // (その行自体はlines_のメンバーではない――このクラス自身の
    // scrollback_/commandLine_の分割参照)。^P自身の修正を反映している:
    // 一番下にいて、かつ依然として閲覧中というのは「これ以上下へ
    // 動いて『入る』ものが無いので、代わりに閲覧をやめる」ことを
    // 意味する――exitHistoryBrowsing()は、Enter自身のコマンドでない
    // 場合のフォールバックと「他のどんなキー」の戻る動作が、その同じ
    // 遷移のために既に使っているのとまさに同じ呼び出しである。
    const auto &lines = shell_.scrollback().lines();
    bool atBottom = !lines.empty() && shell_.scrollback().cursorIndex() == static_cast<int>(lines.size()) - 1;
    if (shell_.historyBrowsing() && atBottom) shell_.exitHistoryBrowsing();
    else shell_.scrollback().moveDown();
    shell_.redrawPromptRow();
    moveDisplayCursorToShellHistoryLine();
    return true;
  }
  bool browsingHistory = shell_.historyBrowsing();
  if (byte == kCarriageReturn) {
    if (browsingHistory) {
      // shell.c自身の'\r'のケースから移植: ハイライトされている行の
      // コマンドが、本物の、以前に投入されたプロンプト行
      // (freezePromptLine()が設定するLine::shellCommand)である場合は
      // それを再実行する;それ以外(プレーンな出力テキスト)は、
      // legacy自身の「プロンプト行ではない」場合のフォールスルーで
      // ある――legacy自身の、生の出力テキストをコマンドとして投入
      // しようとする試み(legacy自身が一貫しない扱い方をしている
      // エッジケースであり、正確に再現する価値は無い)まで複製する
      // のではなく、ここでは単に戻るだけに単純化してある。
      const auto &line = shell_.scrollback().lines()[shell_.scrollback().cursorIndex()];
      if (line.shellCommand) {
        shell_.commandLine().setText(*line.shellCommand);
        shell_.submitLine();
      } else {
        shell_.exitHistoryBrowsing();
        shell_.redrawPromptRow();  // 下のbrowsingHistoryガード自身のコメント参照
      }
      return true;
    }
    shell_.submitLine();
    return true;
  }
  // 閲覧中のどんなコンテンツを変更するキーも、まず最初にカーソルを
  // 生きたプロンプトへ戻す、shell.c自身のデフォルトケース
  // ("if (pos != M_tail) { write_comm(pv[1], M_tail, ...); ... }"、
  // 実際のキー入力を処理する方へ流れ落ちる前)に合わせている。^Cは、
  // この地点を過ぎたバイトの中で、commandLine_に触れない(単に割り込み
  // を子プロセスへ転送するだけ)唯一のものであり、同様にそのような
  // ガードを持たないlegacy自身のK_cのケースに合わせている。
  // redrawPromptRow()(下のTab/backspace/insertByteが既にそれ自身で
  // 呼んでいる、単なるcommandLine_.redraw()ではない)がここで特に
  // 必要なのは、exitHistoryBrowsing()自身のgoToBottom()が、たった今、
  // プロンプトの接頭辞のテキストを空白にし得る、scrollback_の完全な
  // 再描画を実行したためである――全体の経緯(実際のユーザー報告、
  // 2026-08-02)についてはredrawPromptRow()自身のコメント参照。
  if (browsingHistory && byte != kControlC) {
    shell_.exitHistoryBrowsing();
    shell_.redrawPromptRow();
  }
  if (byte == 0x12) {  // ^R――startShellHistoryRecall()自身のコメント参照
    return startShellHistoryRecall();
  }
  // 2026-09-20追加、直接のユーザー報告("SHELLプロンプトで^Sとやっても
  // 認識しない（ハイライトのSが表示されるだけ）"): "GREP is possible
  // from SHELL mode"自身のコメント参照――startGrepPrompt()自体は元々
  // Shellを排除していないと2026-09-19に確認済みだったが、その確認は
  // handleCommand(当時のCommand::StartGrep)を直接呼ぶユニットテストに
  // よる
  // もので、実際の生の^Sキー入力がここまで正しくルーティングされる
  // ことまでは確認していなかった――この関数自身、^J/^X/^O/ESC以外の
  // 生バイトをグローバルなコマンドとして一切扱わず、下のinsertByte()
  // へ素通しして(制御バイトとして)コマンドラインへ文字通り挿入して
  // しまうので(すぐ上の^A/^E/^B/^F/^D自身の同種の過去のバグのコメント
  // 参照、まさに同じ症状"ハイライトのADDDD")、^S自体はここに専用の
  // 分岐が無ければ二度と実際の検索発動には届かない。他のモードの
  // プレーンな^S(kControlS、dispatchByte()自身の該当ケース参照)と
  // 揃える――2026-09-19の一本化以後はそちらと同じく
  // Command::StartIncrementalGrep(それ自身の宣言コメント参照)へ配線する
  // (SHELLはisGrepInPlaceEligibleMode()に含まれているので、
  // scrollbackに対するライブ検索がそのまま動く)。
  if (byte == kControlS) {
    return handleCommand(Command::StartIncrementalGrep);
  }
  if (byte == 0x09) {  // Tab
    Shell::TabOutcome outcome = shell_.onTab();
    if (outcome.showList) showTabCompletionList(outcome.candidates, outcome.partial, outcome.prefixBeforeWord);
    return true;
  }
  if (byte == 0x7f || byte == 0x08) {  // DEL/^H
    shell_.commandLine().backspace();
    shell_.commandLine().redraw();
    return true;
  }
  // ^A/^E/^B/^F/^D(2026-08-16修正): feedPromptByte()は、汎用の
  // プロンプトエディタ(promptEditor_、LOCATE/CUT/GREP等が使う)向けに
  // 2026-08-12にこれらが結線された(それ自身のコメント参照)が、SHELL
  // モードはそれ自身の別個のcommandLine() LineEditorを持っており、
  // この修正はそこへは一度も反映されていなかった――実際の報告
  // ("^A^DDDD'cat'...を打とうとしたらハイライトのADDDDだった")に
  // より、2026-08-12の修正が既に説明しているのとまさに同じ症状
  // (各制御バイトが下のinsertByte()へ流れ落ち、それ自身の反転表示
  // された"@"オフセットのグリフとしてレンダリングされる)が、依然と
  // してここで起きていることが判明した。同じバイト値、同じ
  // LineEditorクラス、ただ異なるインスタンス(promptEditor_ではなく
  // shell_.commandLine())と異なるredraw()呼び出し(SHELL自身の
  // プロンプト行は汎用のプロンプトラベルの経路では描画されないので、
  // すぐ上のbackspace自身の対応に合わせたcommandLine().redraw())。
  if (byte == 0x01) {  // ^A: 行頭
    shell_.commandLine().moveHome();
    shell_.commandLine().redraw();
    return true;
  }
  if (byte == 0x05) {  // ^E: 行末
    shell_.commandLine().moveEnd();
    shell_.commandLine().redraw();
    return true;
  }
  if (byte == 0x02) {  // ^B: 1文字戻る
    shell_.commandLine().moveLeft();
    shell_.commandLine().redraw();
    return true;
  }
  if (byte == 0x06) {  // ^F: 1文字進む
    shell_.commandLine().moveRight();
    shell_.commandLine().redraw();
    return true;
  }
  if (byte == 0x04) {  // ^D: 前方削除、ただしフォアグラウンドジョブ実行中はEOFとして子へ転送
    // 2026-09-06追加、実際のユーザー報告("cat^M^Dで終われない
    // (正しい挙動はcat直下の行にプロンプトが戻る)"): shell_.busy()
    // (Shell::foregroundJobRunning()、直前のコミット7b87695/0471b05
    // 参照)がtrueの間、commandLine_は常に空(submitLine()自身の
    // setText("")参照)なので「前方削除」に意味は無く、それでいて
    // ^Dをローカルの編集キーとして飲み込んでしまうと、`cat`のような
    // 素のフォアグラウンドコマンドへEOFが一切届かず、入力待ちのまま
    // 固まって見える。legacy自身のshell.c(do_shell()のK_dケース、
    // K_c(^C)/K_z(^Z)/バックスペース/K_m(Enter)と同じ「break」群)も、
    // ^Dを生のバイトとしてそのまま子のptyへfputc()しており、
    // ローカルな行編集としては一切扱っていない――^Cが既にsendControl
    // Byte()経由でこれと同じ扱いを受けているのと同じ理屈で、busy()の
    // 間はこちらも同じ経路へ回す。busy()でない間(普通にコマンドを
    // 組み立てている間)は、2026-08-17に追加された既存の前方削除の
    // 挙動をそのまま維持する。
    if (shell_.busy()) {
      shell_.sendControlByte(static_cast<unsigned char>(byte));
    } else {
      shell_.commandLine().deleteForward();
      shell_.commandLine().redraw();
    }
    return true;
  }
  // ^U/^K/^Y(2026-08-17修正): すぐ上の^A/^E/^B/^F/^Dとまさに同じ
  // 「promptEditor_について修正済みだが、shell_.commandLine()へは
  // 一度も反映されていない」隙間(その修正自身の2026-08-16のコメント
  // 参照)――この3つは当時単に見落とされていた。実際の報告:
  // "less CLAUDE.md"でカーソルが"U"の上にあり、^Kは行末まで削除
  // ("less CLA")、あるいは^Uは行頭まで削除("UDE.md")を期待していた、
  // どちらの方向でも^Yで復元可能――3つのどれも何もしなかった
  // (バイトは下のinsertByte()へ流れ落ち、行を編集する代わりに、
  // それ自身の反転表示された"@"オフセットのグリフとしてレンダリング
  // されていた)。
  if (byte == 0x15) {  // ^U: フィールドの先頭からカーソルまで削除
    shell_.commandLine().killToStart();
    shell_.commandLine().redraw();
    return true;
  }
  if (byte == 0x0b) {  // ^K: カーソルからフィールドの末尾まで削除
    shell_.commandLine().killToEnd();
    shell_.commandLine().redraw();
    return true;
  }
  if (byte == 0x19) {  // ^Y: ^U/^Kの削除バッファを再挿入
    shell_.commandLine().yank();
    shell_.commandLine().redraw();
    return true;
  }
  if (byte == kControlUnderscore) {  // ^_: 取り消し(LineEditor::undo()参照)
    shell_.commandLine().undo();
    return true;
  }
  if (byte == kControlC) {
    shell_.sendControlByte(static_cast<unsigned char>(byte));
    return true;
  }
  shell_.commandLine().insertByte(static_cast<unsigned char>(byte));
  shell_.commandLine().redraw();
  return true;
}

// scrollback_.setCursorHighlightEnabled(false)がオフにする行全体の
// ハイライトの代わりとなる、^P/^N自身のプレーンなテキストカーソル
// (なぜかについてはShell自身のコンストラクタのコメント参照: legacy
// のstand_str()は、SHELLモードについてはそのハイライトを一切描画
// しない)。生きたプロンプト行自体の上ではno-op(そこでの
// cursorDisplayRow()は、これが呼ばれる前に^P/^Nの両方の呼び出し箇所
// で既に実行済みの、redrawPromptRow()自身のcommandLine_.redraw()の
// カーソル位置と一致するので、上書きすべきものは何も無い)――実際に
// scrollback_の履歴を閲覧している時だけディスプレイの可視カーソルを
// 動かすので、生きたプロンプトのケースは通常通り編集を続けられる。
// -1(表示範囲外へスクロールされた、moveUp()/moveDown()が常に
// cursorIndex_をビューポート内に保つことを踏まえれば到達すべきで
// はないが、防御的にチェックしてある)は、カーソルを無意味などこか
// に表示するのではなく、単純にスキップされる。
//
// moveCursor()ではなくsetVisibleCursor()(2026-08-04の改訂――
// Display::setVisibleCursor()自身のコメント参照): 以前の版はプレーン
// なmoveCursor()を使っていた、これは-cについては機能した(この呼び
// 出しと端末が再びアイドルになる間には他の何も画面に触れない)が、
// -gについては機能しなかった、そこではrunGuiPhase()自身の一定の
// 約60Hzのポーリングが、これが実行されたのと「同じ」フレーム内で、
// 完全な、無関係なContentView::redraw()を再実行することがあり得て、
// その再描画「自身」の副次的なmoveCursor()呼び出しが最後に着地した
// 場所へ、可視カーソルを黙って引き戻してしまう(実際のユーザー報告:
// "-gのSHELLでは...一瞬出て消えるパターン")。
void Application::moveDisplayCursorToShellHistoryLine() {
  // 2026-08-25修正: Shell::historyBrowsing()自身の宣言コメント参照
  // ――ここでの「古い」計算によるチェック(cursorIndex() != 静止/最新
  // のインデックス)は、cursorIndex_を動かさずに正しく閲覧モードへ
  // 入るようになった「最初の」^P押下(Application自身の^Pハンドラ
  // 参照)が、それでもその最新の行の上に可視カーソルを全く表示でき
  // ないことを意味していた――状態の修正だけでは不十分だった、これが
  // 実際に、ユーザーが見る必要のある「ここを閲覧しています」という
  // フィードバックを描く「唯一の」場所であるため。
  if (!shell_.historyBrowsing()) return;
  int row = shell_.scrollback().cursorDisplayRow();
  if (row >= 0) display_.setVisibleCursor(row, 0);
}

ContentView &Application::activeView() {
  syncEpubHorizontalPresentation();
  return viewForMode(currentMode_);
}

// -gの縦書き用の描き方(小書きの仮名・半角の位置、「ー」の縦線、「：」の回転)を、今見せている
// 画面が縦書きの間だけ有効にする(GraphicalDisplay::setVerticalTextHint())。縦書きの本の横書きの
// 範囲(奥付等)では、fileView_に範囲の外の行を描かせない(ContentView::setShownIndexRange())。
// 2026-09-30、直接のユーザー報告("1画面に満たない奥付もなかなかのカオス"=ht): 以前は
// activeView()の中でだけ切り替えていたので、スクロールで奥付へ入って(activeView()を通らずに)
// fileView_を描き直すと縦書きの描き方のまま(区切り線が縦棒、数字が縦中横)で、1画面に満たない
// 奥付は画面を送れる上限で引き戻されて前の章の本文が上に見えていた。範囲に出入りする所
// (enterEpubHorizontal()等)からも呼ぶ。
void Application::syncEpubHorizontalPresentation() {
  // 描き方の切り替えは2026-09-30から各ビューが自分を描く時に行う(ContentView::setVerticalText())。
  // ContentViewを通らずに描く画面(SHELL等)のため、見せる画面が縦書きでなければここでも戻す。
  bool verticalShown = (currentMode_ == ApplicationMode::File && epubVerticalActive()) ||
                       (currentMode_ == ApplicationMode::Cut && epubTocVerticalActive());
  if (!verticalShown || promptActive()) display_.setVerticalTextHint(false);
  // 横書きの選んでいるリンク(selectAdjacentEpubHorizontalLink())は、画面に見えている間だけ反転する
  // 2026-09-30、直接のユーザー依頼("「あとがき」ではハイライト表示のリンクがスクロールに追随するので
  // 奥付でもよろしく"): 選んでいるリンクがスクロールで画面から外れたら、見えているリンクのうち
  // 近い方(先へ送ったなら一番上、戻したなら一番下)へ選び直す。見えるリンクが無ければ外す。
  // 同日の訂正(直接のユーザー指摘"スクロールというのはj/kではなくて慣性のほう。上向きだったら
  // 最上行で現在地更新だが、行内にアンダーラインがあればハイライト表示される"、"左右方向では
  // 同カーソルがちゃんとあって、行内のアンダーライン箇所を次々ハイライト表示しながらスクロールに
  // 追随する"): 縦書きの列カーソルと同じく、カーソルの行(慣性では送る向きの先頭の行)のリンクを
  // 選ぶ。選んでいるリンクがその行にあればそのまま、無ければその行の最初のリンク、行にリンクが
  // 無ければ選ばない。
  std::optional<ContentView::ByteSpan> standout;
  if (epubHorizontalLinksActive() && !fileMode_.epubLayoutHints().lineLinks.empty()) {
    int cursor = fileView_.cursorIndex();
    std::optional<EpubHorizontalLink> chosen;
    for (const auto &link : epubHorizontalVisibleLinks()) {
      if (link.index != cursor) continue;
      if (!chosen) chosen = link;
      if (epubHorizontalLink_ && link.index == epubHorizontalLink_->index && link.start == epubHorizontalLink_->start)
        chosen = link;
    }
    epubHorizontalLink_ = chosen;
    if (chosen) standout = ContentView::ByteSpan{chosen->index, chosen->start, chosen->end};
  } else {
    epubHorizontalLink_.reset();
  }
  fileView_.setExtraStandout(standout);
  if (!epubHorizontalSection_ || !epubBookVertical()) {
    fileView_.setShownIndexRange(std::nullopt);
    return;
  }
  const auto &lines = fileView_.lines();
  int from = static_cast<int>(lines.size()), until = static_cast<int>(lines.size());
  for (int i = 0; i < static_cast<int>(lines.size()); i++) {
    int raw = lines[i].rawLineNumber.value_or(0);
    if (from == static_cast<int>(lines.size()) && raw >= epubHorizontalSection_->first) from = i;
    if (raw > epubHorizontalSection_->second) {
      until = i;
      break;
    }
  }
  fileView_.setShownIndexRange(std::make_pair(from, until));
}

// activeView()から括り出した(2026-08-18)ので、currentMode_「以外」の
// モードについてビューを必要とするどんなコード――例えば
// grepReturnMode_のバッファを読むexecuteGrep()――も、より狭い、
// すぐに古くなるコピーを再導出するのではなく、まさに同じmode->view
// のマッピングを共有する(これが修正するバグについてはexecuteGrep()
// 自身のコメント参照: 以前はFile対「それ以外は全部Directory」と
// ハードコードしており、実際にgrepがそこから起動され得る他の全ての
// モード(Locateも含む、startGrepPrompt()はGrep自身からの再入場だけ
// をブロックし、それ以外はどのモードからも到達可能であるため)に
// ついて、黙って間違ったバッファを読み――失敗時には間違った
// ContentViewを再描画し――ていた)。
ContentView &Application::viewForMode(ApplicationMode mode) {
  switch (mode) {
    case ApplicationMode::File:
      return epubVerticalActive() ? epubVerticalView_ : fileView_;  // 縦書き、rebuildEpubVertical()参照
    case ApplicationMode::Grep:
      return grepView_;
    case ApplicationMode::Cut:
      return epubTocVerticalActive() ? epubTocVerticalView_ : cutView_;  // 縦書きの章の一覧
    case ApplicationMode::CutContent:
      return cutContentView_;
    case ApplicationMode::Man:
      return manView_;
    case ApplicationMode::Locate:
      return locateView_;
    case ApplicationMode::Archive:
      return archiveView_;
    case ApplicationMode::ArchiveContent:
      return archiveContentView_;
    case ApplicationMode::Shell:
      return shell_.scrollback();
    case ApplicationMode::Du:
      return duView_;
    case ApplicationMode::Underline:
      return underlineView_;
    case ApplicationMode::ListBrowser:
      return listBrowserView_;
    case ApplicationMode::Copy:
      return copyView_;
    case ApplicationMode::Directory:
    default:
      return directoryView_;
  }
}

bool Application::handleCommand(Command command) {
  // feedByte()自身の同じ判定参照(テスト等、handleCommand()を直接
  // 呼ぶ経路のため)。
  bool outermost = stdinPagerCheckDepth_ == 0;
  bool wasStdin = outermost && stdinPagerActive();
  bool wasReturnsToShell = wasStdin && fileMode_.pagerReturnsToShell();
  CutLeaveSnapshot cutBefore = cutLeaveSnapshot();
  ++stdinPagerCheckDepth_;
  bool result = handleCommandImpl(command);
  --stdinPagerCheckDepth_;
  if (outermost) saveCutPositionIfLeft(cutBefore);
  if (wasStdin && currentMode_ != ApplicationMode::File) discardStdinPager(wasReturnsToShell);
  if (outermost) showBufferLimitNoticeIfHit();
  // 横書きのEPUBの選んでいるリンク(syncEpubHorizontalPresentation())は、スクロールした後の画面で
  // 選び直す(activeView()はコマンドの前に呼ばれるので、j等の後では古いまま)。変わった時だけ描き直す。
  if (outermost && epubHorizontalLinksActive()) {
    auto before = epubHorizontalLink_;
    syncEpubHorizontalPresentation();
    bool changed = before.has_value() != epubHorizontalLink_.has_value() ||
                   (before && (before->index != epubHorizontalLink_->index || before->start != epubHorizontalLink_->start));
    if (changed && &activeView() == &fileView_) fileView_.redraw();
  }
  return result;
}

// usilicrcのBUFFER(2026-09-25): どこかの読み込みがBUFFERを超えていたら
// (Configuration::noteBufferLimitHit())、今の画面に"Not enough buffer"を
// 一時メッセージとして出す。入力/コマンドの処理の後、マウスのクリック/
// ドロップ、非同期の読み込み(DU/アーカイブ一覧)の完了時に呼ぶ。
void Application::showBufferLimitNoticeIfHit() {
  if (configuration_.consumeBufferLimitHit()) showTransientNotice(activeView(), "Not enough buffer");
}

void Application::showTransientNotice(ContentView &view, const std::string &text) {
  clearTransientNotice();
  view.setPositionTextOverride(text, /*plain=*/true);
  transientNoticeView_ = &view;
  view.redraw();
}

void Application::clearTransientNotice() {
  if (sortLabelNoticeShown_) {  // ^O^Lのソートキーのラベル(CycleLsSortKey参照)
    sortLabelNoticeShown_ = false;
    directoryMode_.setSortLabelShown(false);
    if (currentMode_ == ApplicationMode::Directory) directoryView_.redraw();
  }
  if (!transientNoticeView_) return;
  ContentView *view = transientNoticeView_;
  transientNoticeView_ = nullptr;
  view->setPositionTextOverride(std::nullopt);
  if (&activeView() == view) view->redraw();
}

bool Application::handleCommandImpl(Command command) {
  clearTransientNotice();  // showTransientNotice()自身の宣言コメント参照
  if ((command == Command::MoveDown || command == Command::MoveUp) &&
      selectAdjacentEpubHorizontalLink(command == Command::MoveDown ? 1 : -1))
    return true;
  // 横書きの本の^X^X: ContentViewのジャンプマークで戻った先がリンクの行なら、飛ぶ前の画面の位置と
  // 選んでいたリンクも戻す(restoreEpubJumpOrigin())。
  if (command == Command::ExchangeJumpMark && epubHorizontalLinksActive() && !epubBookVertical() &&
      epubJumpOrigin_ && fileView_.exchangeJumpMark()) {
    restoreEpubJumpOrigin();
    return true;
  }
  // 2026-09-30、直接のユーザー報告(同上、"「第20章」を選んで^X^Xしても目次に戻らない"): 縦書きの
  // リンクで横書きの範囲(奥付等)へ飛んだ後の^X^Xは、飛ぶ前の縦書きの列(epubVerticalMark_)へ戻る。
  if (command == Command::ExchangeJumpMark && currentMode_ == ApplicationMode::File && epubBookVertical() &&
      epubHorizontalSection_ && (epubVerticalMark_ || epubJumpRaw_)) {
    // 戻った先から^X^Xでまたここへ来られるよう、今の行を覚える(epubJumpRaw_)
    int here = fileRawLineAtCursor();
    if (!epubVerticalMark_) {
      int raw = *epubJumpRaw_;
      epubJumpRaw_ = here;
      showEpubVerticalRaw(raw);
      activeView().redraw();
      activeView().syncVisibleCursor();
      return true;
    }
    int target = *epubVerticalMark_;
    epubVerticalMark_.reset();
    epubJumpRaw_ = here;
    epubHorizontalSection_.reset();
    syncEpubHorizontalPresentation();
    setEpubVerticalView(target, target);
    activeView().redraw();
    activeView().syncVisibleCursor();
    return true;
  }
  // 2026-10-01(enterEpubHorizontalAtBookEnd()参照): 縦書きの本の横書きの範囲(奥付等)でも、
  // ^[</^[>は縦書きと同じ向き――^[<は本の末尾(奥付の最下行)、^[>は本の冒頭(縦書きの最初)。
  if (currentMode_ == ApplicationMode::File && epubBookVertical() && epubHorizontalSection_) {
    if (command == Command::GoToTop) {
      if (enterEpubHorizontalAtBookEnd()) return true;
    } else if (command == Command::GoToBottom) {
      epubJumpRaw_ = fileRawLineAtCursor();  // ^X^Xでここへ戻れるよう
      epubVerticalMark_.reset();
      epubHorizontalSection_.reset();
      syncEpubHorizontalPresentation();
      setEpubVerticalView(0, 0);
      activeView().redraw();
      activeView().syncVisibleCursor();
      return true;
    }
  }
  if (currentMode_ == ApplicationMode::File && epubVerticalActive())  // 縦書き(handleEpubVerticalCommand()参照)
    if (auto handled = handleEpubVerticalCommand(command)) return *handled;
  if (currentMode_ == ApplicationMode::Cut && epubTocVerticalActive())  // 縦書きの章の一覧
    if (auto handled = handleEpubTocVerticalCommand(command)) return *handled;
  // なぜこれがモードラインのプロバイダ自体の内部ではなく「ここ」
  // (ディスパッチされたコマンドごとに一度)に生きているのか、そして
  // なぜ(ほとんどの実際のキー入力はそこを通るが、副次的なホバー/
  // アイドルループの再描画は一切そこを呼ばず、一部の呼び出し元/
  // テストはhandleCommand()を直接呼ぶことでそこを迂回する
  // dispatchByte()ではなく)特にhandleCommand()なのかについては
  // DirectoryMode::clearEditError()自身のコメント参照――これが、
  // 実際のキーボードのディスパッチと、それら直接の呼び出し元の両方
  // が一致して通る唯一の場所である。Directoryモードの外や、現在何も
  // 表示されていない時は無害。下のswitchより「前に」実行されるので、
  // StartEdit自身のケース(executeEdit()/reportEditError()経由で、
  // これを再び設定することがある)は、影響を受けず、その後も依然
  // として「それ自身の」メッセージを表示する。
  directoryMode_.clearEditError();
  archiveMode_.clearEditError();  // ArchiveMode自身の同一の仕組み――それ自身の宣言コメント参照
  // ContentView::reportGrepNoMatchMessage()自身の宣言コメント参照、同じ
  // タイミング・同じ理由。2026-09-11、Directory/Fileの2回の明示呼び出し
  // から一般化: grepNoMatchActive_になり得るのは常にこの時点の
  // activeView()(=currentMode_のview)だけであり、この後の下のswitchが
  // currentMode_を変え得るより「前」にここで読むため。
  activeView().clearGrepNoMatchMessage();
  // in-place GREP(2026-09-09追加、2026-09-10改訂: 直接のユーザー指摘
  // "うっかりミスは回避したいので、明確なモード切り替えには当たらない
  // ...ページ送りキー（k/jその他）等はスルー"): 明確な「モード切り替え」
  // に当たらないコマンド(このジャンプ/マーク自身・素のカーソル移動/
  // ページ送り/画面再描画/無認識バイト)が来た間はセッションを継続し、
  // それ以外(Enter/StartCopy等、実際にcurrentMode_や画面を切り替え
  // 得るコマンド)が来た時点で、明示的な^G無しに自動的にセッションを
  // 終了する(requirements.mdのオープンクエスチョン3番からの改訂)。
  // この判定自体は「セッションを終了させないコマンド」を明示的に
  // 列挙する方式――逆(モード切り替えを起こすコマンドを全て列挙する)
  // だと、今後の新規コマンド追加のたびに見落としが起きやすい。
  // コマンド自体の実行は妨げない――下のswitchは普通にそのまま続く。
  //
  // 2026-09-19追加、直接のユーザー報告(PTYハーネス実験で再現確認:
  // "usilic -c ~/git/usilicでCLAUDE.mdを開き^S*→^M→^Gすると
  // CLAUDE.mdへ戻らず(ls)まで抜けてしまう"): Command::Backだけは
  // 上の「コマンド自体の実行は妨げない」の対象から除外する――^Gの
  // 役割はどのモードでも常に「一番内側の状態から順にキャンセルする」
  // ことであり(returnFromShell()自身のhistoryBrowsing()、CutMode::
  // canGoBack()と同じ「1回目は内側をキャンセル、2回目で実際に抜ける」
  // という既存の慣習)、in-place検索のハイライトはcurrentMode_を
  // 一度も離れていないという点でまさにその「一番内側の状態」に
  // 当たる。ここで吸収せずBackを素通りさせると、ハイライト解除と
  // 「元のモードから抜ける」が1回の^Gで同時に起きてしまい、
  // ユーザーが期待する「まずハイライトだけが消える」動作にならない。
  bool wasGrepInPlaceActive = grepInPlaceActive_;
  if (grepInPlaceActive_ && !isGrepInPlaceSafeCommand(command)) {
    deactivateGrepInPlace();
  }
  if (wasGrepInPlaceActive && command == Command::Back) return true;
  // 2026-08-25(FileMode::ensureLoadedThrough()自身のコメント参照):
  // CLIのページャで起動されたファイル/stdinのストリームは、これまで
  // のスクロールを満たすのに十分なコンテンツしかロードされていない
  // ――hasActiveStream()は他の「全ての」Fileモードの入り口(通常の
  // loadFile()で開かれたファイルは既に全てを持っている)については
  // falseなので、そこではこれは不活性である。
  // 2026-09-06再訂正(4回目)、直接のユーザー報告("80x24のターミナルで
  // 旧との違いを比較すると"②③、実機PTYでの再検証)を経て、GoToBottom
  // 以外の通常コマンド分の呼び出しをここから撤去した――以前は
  // 「あらゆるコマンドの前」に無条件でここでも先読みしていたが、この
  // 直後のswitch(command)自身のコマンド固有の移動が終わった後、
  // 同じ1回のメインループの反復内でApplication::pollPagerExitNotice()
  // (main.cpp自身のコメント参照、handleCommand()の直後に必ず呼ばれる)
  // が「topIndex_が動いたか」を見て同じ内容の先読みを既に行っている
  // ため、ここでも重ねて呼ぶと同じ1回のキー入力で先読みが二重に
  // 実行され(pagerReturnsToShell()セッションでは1回あたり「あと1行」
  // に絞られているため、二重実行の結果が「1行ずつ」ではなく「2行
  // ずつ」進むという、まさに直接のユーザー報告と一致する形の回帰と
  // なって現れた)、legacy自身の観測値(`j`1回につき先読みがちょうど
  // 1行しか伸びない)と食い違っていた。
  if (currentMode_ == ApplicationMode::File && fileMode_.hasActiveStream() && command == Command::GoToBottom) {
    // GoToBottomは、`less`自身のG コマンドがまだ成長中のファイル/
    // パイプに対してそうする(ソースが実際に終わるまでブロックする)
    // のと同様に、まず残りのストリーム「全体」を実際のEOFまで読み
    // 通す必要がある――そうしないと「末尾」は、ストリームの本当の
    // 終わりではなく、単に「ロードがたまたま止まった場所」を意味
    // することになってしまう。これはpollPagerExitNotice()自身の
    // 「あと1行」ペーシングでは決して代替できない、GoToBottom固有の
    // 「今すぐ全部」という意図そのものなので、ここに残す。
    ensurePagerStreamLoaded(std::numeric_limits<int>::max() - 1);
  }
  switch (command) {
    // setHoverIndex(nullopt)を各移動の「後」ではなく「前」に呼ぶ
    // (2026-08-05修正): 実際のユーザー報告――マウスをある行の上に
    // 静止させた(hoverIndex_を設定する、ContentView::setHoverIndex()
    // 自身のコメント参照)後、キーボードのナビゲートに切り替えると、
    // "モードライン行数は変わり内容も正しいがハイライトが追随しない"。
    // 下のmoveUp()/moveDown()等は全て内部でredraw()し、redraw()自身
    // のハイライトの選択は`hoverIndex_.has_value() ? *hoverIndex_ :
    // cursorIndex_`である――cursorIndex_はずっと正しく動いていた
    // (だからモードライン/コンテンツは正しかった)が、最後のマウス
    // ホバーから残った古いhoverIndex_が、その再描画でずっとそれより
    // 優先され続けており、そのためハイライトされた行は目に見えて
    // 追随することが決して無かった。移動自体がそれ自身の内部の
    // redraw()を実行する「前に」ここでそれをクリアすることは、その
    // redraw()が既にcursorIndex_を(正しい、最新の)ハイライトの元
    // として見ることを意味する――「キーボード入力は常に古いマウス
    // 駆動のUI状態に勝つ」という、ユーザー自身の診断が既に-gの
    // 「ドラッグ」バグへ適用していたのと同じ原則。
    case Command::MoveUp:
      activeView().setHoverIndex(std::nullopt);
      activeView().moveUp();
      activeView().syncVisibleCursor();
      refreshDirectEntryModeLine();  // RecentFile/RecentDirectory自身の読み取り専用の閲覧の外ではno-op
      return true;
    case Command::MoveDown:
      activeView().setHoverIndex(std::nullopt);
      activeView().moveDown();
      activeView().syncVisibleCursor();
      refreshDirectEntryModeLine();
      return true;
    case Command::ScrollLineUp:
      activeView().setHoverIndex(std::nullopt);
      activeView().scrollLineUp();
      activeView().syncVisibleCursor();
      // 2026-09-18追加、実機報告(⑤「^[vによるページ戻り（画面更新？）
      // で②が起きる」)を受けて追加: MoveUp/MoveDown(すぐ上)は既に
      // refreshDirectEntryModeLine()を呼んでいたが、この移植の
      // スクロール/ページ系コマンドは元から一度も呼んでいなかった
      // ――RecentFile/RecentDirectory自身の既存のdirect entry機能に
      // とっても本来同じ穴だったが、今回のLIST機能で初めて実際に
      // 踏まれて表面化した。
      refreshDirectEntryModeLine();
      return true;
    case Command::ScrollLineDown:
      activeView().setHoverIndex(std::nullopt);
      // 2026-09-06追加、直接のユーザー報告("％の行数が97と98を行き来
      // している...画面更新の際、行数を2回表示している箇所ってない？")
      // ――preloadPagerAhead()/suppressPagerRepoll()自身の宣言コメント
      // 参照。目標値はpollPagerExitNotice()自身の本来のtopIndex+rows*2
      // と同じ大きさにする――ensureLoadedThrough()自身のペーシングで
      // 内部的に「現在+1」へ切り詰められるが、目標値そのものを小さく
      // (例えば+1に)しすぎると、既に読み込み済みの行数をその小さな
      // 目標が下回ってしまい、早期リターンで実質no-opになる(直接の
      // ユーザー報告"spaceならラストページまで到達できるがjだと2回の
      // 行送りでENDになる"で発覚した実際の退行)。
      preloadPagerAhead(activeView().topIndex() + display_.rows() * 2);
      activeView().scrollLineDown();
      activeView().syncVisibleCursor();
      suppressPagerRepoll(activeView().topIndex());
      refreshDirectEntryModeLine();  // ScrollLineUp自身のコメント参照(⑤)
      return true;
    case Command::PageUp:
      activeView().setHoverIndex(std::nullopt);
      activeView().pageUp();
      activeView().syncVisibleCursor();
      refreshDirectEntryModeLine();  // ScrollLineUp自身のコメント参照(⑤)
      return true;
    case Command::PageDown:
      activeView().setHoverIndex(std::nullopt);
      // 2026-09-06追加、直接のユーザー報告("^V/^F/spaceで（どれも
      // ページ送りキーのはずなのに）行送りすると...摩訶不思議な症状"、
      // 修正後も再度「spaceの不可解な挙動が直ってない」との指摘)――
      // ContentView::pageDown()自身はheight_(画面の高さ)分だけtopIndex_を
      // 進めようとするが、maxTop = lines_.size() - height_で無条件に
      // クランプする。この移植の遅延読み込み型ページャ(FileMode::
      // hasActiveStream())は、ensureLoadedThrough()自身のペーシング
      // (pollPagerExitNotice()参照、通常のスクロールでは意図的に
      // 「現在+1」しか読み進めない)により、lines_.size()が現在の
      // ビューポートよりわずか数行しか多く育っていないことが多い――
      // 結果、まだ画面1枚分の余裕が無いままpageDown()が呼ばれると、
      // 本来の「1画面分」ではなく「たまたま読み込み済みだった数行」
      // しか進めず、legacyの本物のページ送りとは似ても似つかない
      // line-by-lineの見た目になっていた(jキーが意図的にcurrent+1
      // でしか伸びないのと違い、PageDown/^V/^F/spaceは「今すぐ1画面
      // 分」という利用者の明確な意図そのものなので、GoToBottomの
      // sentinel同様ここではペーシングを迂回してよい)。
      // 2026-09-06追記――preloadPagerAhead()/suppressPagerRepoll()自身の
      // 宣言コメント参照(Command::ScrollLineDownと共通の仕組み)。
      preloadPagerAhead(activeView().topIndex() + display_.rows() * 2, /*bypassPacing=*/true);
      activeView().pageDown();
      activeView().syncVisibleCursor();
      suppressPagerRepoll(activeView().topIndex());
      refreshDirectEntryModeLine();  // ScrollLineUp自身のコメント参照(⑤)
      return true;
    case Command::Enter:
      if (currentMode_ == ApplicationMode::Directory) return openSelectedEntry();
      if (currentMode_ == ApplicationMode::Cut) return openSelectedSegment();
      if (currentMode_ == ApplicationMode::Archive) return openSelectedArchiveEntry();
      if (currentMode_ == ApplicationMode::Du) return openSelectedDuEntry();
      if (currentMode_ == ApplicationMode::Underline) return openSelectedUnderlineEntry();
      if (currentMode_ == ApplicationMode::ListBrowser) return openSelectedListEntry();
      if (currentMode_ == ApplicationMode::Grep) return openSelectedGrepMatch();
      if (currentMode_ == ApplicationMode::Locate) return openSelectedLocateResult();
      // 2026-08-18: Fileモードには(プレーンなページャとして)他の
      // Enterの挙動は無い――openSelectedBannerEntry()自体は、実際に
      // 見られているファイルが^Tのバナーでない限りno-op(falseを
      // 返す)なので、これは通常のファイル閲覧についてはno-opである。
      if (currentMode_ == ApplicationMode::File) {
        if (epubVerticalActive()) return activateEpubVerticalColumn(epubVerticalCursor_);  // 列カーソル(setEpubVerticalView()参照)
        return openSelectedBannerEntry() || activateSelectedEpubHorizontalLink() || fileMode_.followEpubLink() ||
               openEpubWebLinkAtCursor() || openEpubLineImage(fileView_);
      }
      if (currentMode_ == ApplicationMode::CutContent) return openEpubLineImage(cutContentView_);
      return false;
    case Command::ReadFileNoWrap:
      if (currentMode_ != ApplicationMode::Directory) return openListEntrySpecial(/*raw=*/false);
      return openSelectedEntryNoWrap();
    case Command::ReadFileRaw:
      if (currentMode_ != ApplicationMode::Directory) return openListEntrySpecial(/*raw=*/true);
      return openSelectedEntryRaw();
    case Command::Back:
      if (currentMode_ == ApplicationMode::Copy) return returnFromCopy();
      if (currentMode_ == ApplicationMode::ListBrowser) return returnFromListBrowser();
      if (currentMode_ == ApplicationMode::Du) return returnFromDu();
      if (currentMode_ == ApplicationMode::Underline) return returnFromUnderline();
      if (currentMode_ == ApplicationMode::Shell) return returnFromShell();
      if (currentMode_ == ApplicationMode::ArchiveContent) return returnFromArchiveContent();
      if (currentMode_ == ApplicationMode::Archive) return returnFromArchive();
      if (currentMode_ == ApplicationMode::Locate) return returnFromLocate();
      if (currentMode_ == ApplicationMode::Man) return returnFromMan();
      if (currentMode_ == ApplicationMode::CutContent) return returnFromCutContent();
      if (currentMode_ == ApplicationMode::Cut) {
        if (cutMode_.canGoBack()) return cutMode_.goBack();
        return returnFromCut();
      }
      if (currentMode_ == ApplicationMode::Grep) return returnFromGrep();
      if (currentMode_ == ApplicationMode::File) {
        // 2026-08-25: CLIのページャで起動されたファイル/stdin
        // (startPagerFileList()/startPagerStdin()自身のコメント参照)
        // には、フォールバックすべきDirectory(や他のどんなモード)も
        // 全く無い――実際のlegacyバイナリに対して直接確認済み(PTY
        // キャプチャ): そこで^Gを押しても目に見える変化は一切生じず、
        // "この時はKEYで表示されるモードのみ使用できます"
        // (HELP.jp自身のIntroductionの章)に一致する。下の
        // returnToDirectory()へ流れ落ちて、本当は一度も「このファイル
        // がどこから来たか」ではない、他のどこかのディレクトリの一覧
        // を明らかにしてしまうのではなく、ここで最初にチェックして
        // 単なるno-opにする。ただし(2026-08-25、直接のユーザーの
        // 追跡報告: "戻り値が無く^Gも利かない...戻り値も「SHELL」で
        // あるべき")SHELLモードの再exec引き渡し
        // (Application::pollShellHandoff()自身のコメント参照)には
        // 「本当に」前のモードがある――それが打ち込まれた生きたSHELL
        // セッション――なのでfileMode_.pagerReturnsToShell()経由で
        // そこへ戻ることが、上の不活性な^Gのルールからの唯一の例外
        // である;ensureShellActive()自身のcurrentMode_/scrollback()/
        // redrawPromptRow()の末尾を正確に反映しているが、
        // shellReturnMode_/shellReturnName_の記帳は無い(Shellモードは
        // 実際には一度も離れていないので手を付けていない――
        // pollShellHandoff()はcurrentMode_をそこから入れ替えただけな
        // ので、それらは既に、引き渡しが起こる前に保持していたもの
        // をそのまま保持している)。
        // 2026-09-15追加、直接のユーザー指摘("^O^MはSTDINなページャー
        // 仕様へ完全移行していて^Gで（ls）へ戻れない"): FileMode::
        // setPagerReturnsToDirectory()自身のコメント参照。K-23
        // (loadFileAsPipe())はpagerReturnsToDirectory()を立てるので、
        // ここでは早期returnせず、下の(pagerでない普通のFileセッション
        // と全く同じ)fileReturnMode_ベースの分岐/returnToDirectory()
        // へそのまま素通りさせる――openSelectedEntryNoWrap()が
        // fileReturnMode_ = Directoryを設定済みなので、これで正しく
        // (ls)へ戻る。
        // 2026-09-17追加、Application::tryPollProgramHandoff()自身の
        // コメント参照: pagerReturnsToProgram_(^Jp経由のパイプされた
        // stdin引き渡し)も全く同じ理由でここに含める――こちらは
        // fileReturnMode_がcapturedMode(呼び出し時に実際にいたモード)
        // に設定済みなので、同じ素通しでそこへ正しく戻る。
        if (fileMode_.isPagerMode() && (fileMode_.pagerReturnsToDirectory() || fileMode_.pagerReturnsToProgram())) {
          // loadFileAsPipe()自身のコメント参照: returnPagerToShell()の
          // 3箇所と同じく、裏のfd(loadFileAsPipe()が開いた一時ファイル
          // ――既にunlink済みなのでディスクには残らないが、fd自体は
          // 明示的に閉じないと次のloadFile()/loadStream()まで開いた
          // ままになる)をここで閉じる。
          fileMode_.closeStream();
        } else if (fileMode_.isPagerMode()) {
          if (fileMode_.pagerReturnsToShell()) {
            return returnPagerToShell();
          }
          return false;
        }
        // 2026-08-14修正: fileReturnMode_自身の宣言コメント参照
        // ――LOCATE経由で到達したファイルは、まずLOCATE自身の結果
        // リストへ戻る(ユーザー自身の明示的な仕様に合わせている:
        // "^G連打で最終的にLOCATEを起動した(ls)へ戻るのはかまわない"
        // ――今度はLocateモードからの「2回目」の^Gが、
        // returnFromLocate()自身の既存の、手を付けられていないロジック
        // 経由で既に正しくlocateReturnStack_に到達する)。
        if (fileReturnMode_ == ApplicationMode::Locate) return returnFromFileToLocate();
        // 2026-09-09追加、openSelectedDuEntry()自身のコメント参照
        // ("DUでどれかを選ぶと戻り値がDUじゃない"): DU結果からファイル
        // を直接開いた場合、すぐ上のLocateの前例と同じく、まずDU自身の
        // 結果リストへ戻る。
        if (fileReturnMode_ == ApplicationMode::Du) return returnToDuResults();
        if (fileReturnMode_ == ApplicationMode::Cut && cutOrigin_ == CutOrigin::Epub) return returnToEpubToc();
        // FILE一覧(^X^F)から開いたファイル: 元のディレクトリへ戻した上で一覧を再表示する。
        // 注釈のゲート(Grepと同じ理屈): 別のファイルを開くと注釈が消え、この分岐は働かない。
        if (fileReturnsToFileList_ && !fileMode_.annotations().empty()) {
          fileReturnsToFileList_ = false;
          fileMode_.annotations().clear();
          currentMode_ = ApplicationMode::Directory;
          if (!fileListOriginDirectory_.empty() && directoryMode_.currentDirectory() != fileListOriginDirectory_)
            directoryMode_.enterDirectory(fileListOriginDirectory_);
          if (!startRegexPromptList(PromptKind::RecentFile)) return false;
          selectListBrowserItem(fileListSelectedItem_);
          return true;
        }
        // 2026-09-20追加、Command::StartUnderlineView自身の宣言コメント
        // 参照(`^Ou`): UNDER一覧から開いたファイルは、すぐ上のDu/Locate
        // と全く同じ理由でまず一覧自身へ戻る。
        if (fileReturnMode_ == ApplicationMode::Underline) return returnToUnderlineResults();
        // 2026-08-28追加、openSelectedGrepMatch()自身のFileの分岐の
        // コメント参照: GREPのマッチ経由で到達したファイル(直接開か
        // れたのではない)は、すぐ上のLocateの前例に合わせて、まず
        // 検索自身の結果リストへ戻る。
        // fileMode_.hasSearchAnnotation()のゲート(2026-08-28修正、
        // 実際の報告: "^Gの連打で無限ループ"): fileReturnMode_単独
        // では、一度設定されると永遠にGrepのままだったが、
        // returnFromGrep()自身の「何も選ばずに戻る」経路は既に注釈を
        // クリアしている(DirectoryModeの同一のhasSearchAnnotation()に
        // ゲートされたルーティングを反映しており、まさにこの理由で
        // このバグを一度も持たなかった)――ここで「も」その注釈を
        // チェックしないと、「2回目」の^G(今や単なるFile、注釈は
        // クリア済みだが、fileReturnMode_は依然としてGrepのまま)は、
        // 下のreturnToDirectory()へ流れ落ちる代わりに、永遠に
        // returnToGrepResults()へ跳ね返り続けていた。
        if (fileReturnMode_ == ApplicationMode::Grep && !fileMode_.annotations().empty()) return returnToGrepResults();
        // 2026-08-18: openSelectedBannerEntry()(K-0/^T自身の候補リスト
        // の選択)経由で開かれたw.Nのエントリ――returnToDirectory()の
        // 「ファイルの実際の親ディレクトリをたどる」ロジック(そう
        // しないとtempRootDirectory_に着地してしまう、なぜバナーファ
        // イル自体が既にそのロジックから除外されているのかについては
        // その関数自身の2026-08-18のコメント参照――新たに再ロードされ
        // たw.Nはバナーとは「異なる」ファイルであり、その除外ではカバー
        // されないので、代わりにここでこの別個の分岐が必要になる)では
        // なく、バナー自体へ直接戻る。
        if (fileReturnMode_ == ApplicationMode::File) return executeShowUsilicBanner();
        // 2026-08-28追加、infoPreviousFilePath_自身の宣言コメント参照:
        // (すぐ上のfileReturnMode_==Fileの分岐が既に処理する、それ
        // 自身のWORK/arc.Nの子ではなく)バナー「自体」からの^Gは、常に
        // returnToDirectory()の汎用の「ディレクトリへ戻る」処理へ流れ
        // 落ちるのではなく、^Iが押された時に実際に見られていた本物の
        // ファイルを何であれ再ロードする。
        if (fileMode_.currentFile() == bannerFilePath() && !infoPreviousFilePath_.empty()) {
          std::string target = infoPreviousFilePath_;
          infoPreviousFilePath_.clear();
          if (fileMode_.loadFile(target)) {
            currentMode_ = ApplicationMode::File;
            fileReturnMode_ = ApplicationMode::Directory;
            return true;
          }
        }
        return returnToDirectory();
      }
      // 2026-08-11修正: 実際の報告――openSelectedGrepMatch()が
      // "FGREP: pattern"というモードラインの注釈(DirectoryMode::
      // setSearchAnnotation()自身のコメント参照)付きでDirectoryモード
      // へ戻った後、modeStack_が空である(よくあるケース: 検索は
      // サブディレクトリへ降りたことのない最上位の一覧から直接実行
      // され、そのためポップすべきModeStackのフレームが無かった)
      // 限り、Back(^G)を押しても全く何も起きなかった。この注釈は
      // まさに「ここへ戻れる」ことを示すために存在するので、Backは
      // それを尊重すべきである――ModeStackベースのgoBack()の
      // フォールバックより「前に」チェックする、この既存の同じ順序の
      // 前例(File/Cut等は全て、汎用のdirectoryのケースより前に、
      // それら自身専用のチェックを得ている)に合わせている。(同日に)
      // Directoryモードから直接実行されたCUTもカバーするよう拡張
      // (returnFromCut()も「同じ」注釈を設定する、それ自身のコメント
      // 参照)――lastDirectorySearchKind_(この2つのどちらが注釈を
      // 設定したかと併せて設定される)が、注釈のテキストだけでは
      // 構造的にそれらを区別できないため、どちらの結果画面へ戻すかを
      // 選ぶ。
      if (currentMode_ == ApplicationMode::Directory && !directoryMode_.primaryAnnotations().empty()) {
        auto kind = *directoryMode_.primaryAnnotations().topKind();
        if (kind == DirectoryMode::DirAnnotationKind::Cut) return returnToCutResults();
        if (kind == DirectoryMode::DirAnnotationKind::Du) return returnToDuResults();
        // 2026-09-21、直接のユーザー判断: LOCATE一覧から選んだディレクトリの^Gも、DUと同様に
        // まずLOCATE一覧へ戻る(従来は一覧を飛ばして元のディレクトリへ戻っていた)。
        if (kind == DirectoryMode::DirAnnotationKind::Locate) {
          directoryMode_.primaryAnnotations().pop();
          // 元のディレクトリへ戻す(一覧のモードライン右端が実際の^Gの行き先を示すように)。
          if (!locateDirOriginDirectory_.empty() && directoryMode_.currentDirectory() != locateDirOriginDirectory_)
            directoryMode_.enterDirectory(locateDirOriginDirectory_);
          currentMode_ = ApplicationMode::Locate;
          display_.hideVisibleCursor();
          locateView_.redraw();
          return true;
        }
        if (kind == DirectoryMode::DirAnnotationKind::Usilic) return returnToUnderlineResults();
        if (kind == DirectoryMode::DirAnnotationKind::Dir) {
          // DIR一覧(^X^D)から選んだディレクトリ: 元の位置へ戻した上で一覧を再表示する
          // (その一覧の^Gは元のディレクトリのDirectoryへ戻る)。
          directoryMode_.primaryAnnotations().pop();
          if (!dirListOriginDirectory_.empty()) directoryMode_.enterDirectory(dirListOriginDirectory_);
          if (!startRegexPromptList(PromptKind::RecentDirectory)) return false;
          // 選んだ項目へカーソルを戻す(一覧はソート済み・重複排除なので同じ並びで再構築される)。
          selectListBrowserItem(dirListSelectedItem_);
          return true;
        }
        if (kind == DirectoryMode::DirAnnotationKind::Grep) return returnToGrepResults();
        // kind == Locate: 意図的に特別ルーティングしない(2026-09-20、
        // ユーザー承認による意図的な挙動修正――旧実装は、
        // lastDirectorySearchKind_がGrep/Cut/Duしか区別できず
        // LOCATEを一切設定していなかったことにより、LOCATEでディレクトリ
        // へ着地した直後の^Gが古い(デフォルトGrep)値に従って
        // returnToGrepResults()へ誤ってルーティングされる潜在バグが
        // あった(.steering/20260920-modeline-annotation-stack/
        // requirements.md参照)。ここでは何もせず、下の通常のgoBack()/
        // RecentDirectoryフォールバックへ進む――LOCATE由来の注釈自体は
        // 消費されず、次の`ls`まで表示され続ける。
      }
      // CP/MV/RM操作がアーム済みの間の^G――cancelPendingCpMvRm()自身の
      // コメント参照("CPでは^Gが利かない")。すぐ上のhasSearchAnnotation()
      // と同様、プレーンなgoBack()のフォールバックより前にチェックする。
      if (currentMode_ == ApplicationMode::Directory && pendingCpMvRm_) {
        return cancelPendingCpMvRm();
      }
      // arcExtractSourceAnnotation_("ARC: <path>"、2026-08-27追加、
      // 直接のユーザー提案): すぐ上のhasSearchAnnotation()と同じ形
      // ――汎用のModeStackベースのgoBack()より前にチェックする――
      // ただし専用のreturnTo*()関数ではなくgoToRecentFile()(^J^J)
      // へルーティングする、なぜならModeStackのフレームは「Archive
      // モードへ戻る」ことを全く表現できないため
      // (descendTo()がなぜArchiveモードがそこから入場したプレーンな
      // ディレクトリへ戻すフレームだけを積み、Archive自体へは決して
      // 積まないのかについてはpollPendingArchiveExtractShellRun()
      // 自身のコメント参照)。RecentFile自身の最も最近のエントリは、
      // まさにこの時点では依然としてそのアーカイブである(ここに着地
      // して以来、他の何もファイルを開いていない)ので、これは直接
      // それに到達する。
      if (currentMode_ == ApplicationMode::Directory && !directoryMode_.arcAnnotations().empty()) {
        return goToRecentFile();
      }
      if (directoryMode_.goBack()) return true;
      // 2026-08-27追加、直接のユーザー提案("「スタックが空」で^Gの
      // 時は、DIR直近履歴へ戻るルーチンへ移行ってのは？（以後は
      // ループ）"): ModeStackが尽きた(goBack()がたった今falseを
      // 返した)――行き止まりにするのではなく、RecentDirectory自身の
      // 最も最近訪れた「異なる」ディレクトリへフォールバックする
      // (なぜこれが追加の状態無しに自然にループ/トグルするのかに
      // ついてはgoToRecentDirectory()自身のコメント参照)。
      if (currentMode_ == ApplicationMode::Directory) return goToRecentDirectory();
      return false;
    // ':'/';' -- 文字通りBack+MoveDown+Enter / Back+MoveUp+Enter。
    // 2026-08-16: 実際のユーザー報告("「;」「:」の機能が逆")により、
    // 2026-08-03の「ユーザーによって直接確認済み」だったマッピングが、
    // kKeyTable自身のM-0/M-1エントリ(項目177、usilicrc自身のマクロ
    // 定義から逐語的に取得)に対してこの2つを逆にしていたことが判明
    // した;全体の説明についてはresolveCommand()自身のコメントと
    // Command::JumpNextの宣言コメント参照。各ステップは
    // handleCommand()へ再入場するので、前のステップの「後」に
    // 立っているcurrentMode_を見る(例えばBackはFile->Directoryを
    // 切り替えることがあり、その後MoveDown/Enterが今やcurrentな
    // Directoryビューに対して作用する)――3つの下層のキーが実際に
    // 1つずつタイプされたのとまさに同じである。中間ステップ自身の
    // 成功/失敗は意図的にチェックしない(実際の3キー入力の再生に
    // 合わせている、それは1つのキー入力がno-opだったからといって
    // 途中で止まったりしない);最後のステップの結果だけが返される。
    case Command::JumpNext:
      handleCommand(Command::Back);
      handleCommand(Command::MoveDown);
      return handleCommand(Command::Enter);
    case Command::JumpPrevious:
      handleCommand(Command::Back);
      handleCommand(Command::MoveUp);
      return handleCommand(Command::Enter);
    // 2026-09-18〜2026-09-19の整理(Command::StartFgrep/
    // Command::StartIncrementalGrep自身の宣言コメント参照): 2026-09-05に
    // 新設したGrepSyntax::Basic(プレーンな基本正規表現BRE)は、実際には
    // 拡張正規表現(EGREP)より習得が難しく("GREP"という素の名前から
    // 易しそうに見えるという誤解を、QUICK.txt自身の記述が助長していた)、
    // FGREP/EGREPと並ぶ3つ目の選択肢として残す価値が無いと判断され、
    // まず2026-09-18に素の^Sが「GREP(Basic)」から「リテラル(Fixed)」へ
    // 格下げされ、続く2026-09-19に素の^S自体がインクリメンタルサーチ
    // (Command::StartIncrementalGrep)へ一本化された。結果、Enterで確定
    // する一括検索としてキーから到達できるのは^J^S(StartEgrep)だけに
    // なり、そのリテラル版であるこのStartFgrepはキー割り当ての無い
    // 内部/テスト専用アーティファクトとして残っている(約40件の既存
    // ユニットテストが「grep系プロンプトを開く」汎用トリガーとして
    // 参照している)。
    case Command::StartFgrep:
      return startGrepPrompt(GrepSyntax::Fixed);
    case Command::StartEgrep:
      return startGrepPrompt(GrepSyntax::Regex);
    case Command::GrepInPlaceNext:
      return jumpToGrepInPlaceMatch(+1);
    case Command::GrepInPlacePrevious:
      return jumpToGrepInPlaceMatch(-1);
    case Command::StartIncrementalGrep:
      return startIncrementalGrepPrompt();
    case Command::StartCut:
      return startCutPrompt(GrepSyntax::Regex);  // 2026-09-27、BREからERE(grep -E)へ。下の同日のコメント参照
    case Command::StartCutFile:
      return startCutFilePrompt(GrepSyntax::Regex);
    case Command::ToggleTutorialLanguage:
      tutorialPath_ = (tutorialPath_ == tutorialBasePath_) ? tutorialBasePath_ + ".en" : tutorialBasePath_;
      return true;
    case Command::StartMan:
      return startManPrompt();
    case Command::StartManList:
      return startManList();
    case Command::StartProgram:
      return startProgramPrompt();
    case Command::StartProgramList:
      return startProgramList();
    case Command::StartLocate:
      return startLocatePrompt();
    case Command::LocateUpdateDb:
      return runLocateUpdateDb();
    case Command::StartShell:
      return startShell();
    case Command::ToggleMark:
      return toggleMark();
    case Command::MarkAndAdvance:
      // Fileモードでページャのファイルリストが有効な間はPagerNextFile
      // としても働く――Command::PagerNextFile自身のコメント参照。
      // MarkAndAdvance自身のtoggleMark()/MoveDownのペアはどのみち
      // Directory/Archive専用であり(toggleMark()自体が既にそれで
      // ゲートされている、それ自身のbody参照)、そこでは単にno-opに
      // なるだけなので、最初にチェックする。
      if (currentMode_ == ApplicationMode::File) return navigateToPagerFile(pagerFileIndex_ + 1);
      if (!toggleMark()) return false;
      return handleCommand(Command::MoveDown);
    case Command::MarkAndRetreat:
      if (currentMode_ == ApplicationMode::File) return navigateToPagerFile(pagerFileIndex_ - 1);
      if (!toggleMark()) return false;
      return handleCommand(Command::MoveUp);
    case Command::PagerNextFile:
      return navigateToPagerFile(pagerFileIndex_ + 1);
    case Command::PagerPreviousFile:
      return navigateToPagerFile(pagerFileIndex_ - 1);
    case Command::PagerFirstFile:
      return navigateToPagerFile(0);
    case Command::PagerLastFile:
      return navigateToPagerFile(static_cast<int>(pagerFileList_.size()) - 1);
    case Command::StartUnderlineView:
      return startUnderlineFileView();
    case Command::StartUsilicHistorySorted:
      return startUsilicHistory(UsilicStyle::Sorted);
    case Command::StartUsilicToc:
      return startUsilicHistory(UsilicStyle::Toc);
    case Command::StartUsilicHistory:
      return startUsilicHistory(UsilicStyle::Chronological);
    case Command::QuickLook:
      return executeQuickLook();
    case Command::StartCopy:
      return startCopyPrompt();
    case Command::StartMove:
      return startMovePrompt();
    case Command::StartRemove:
      return startRemovePrompt();
    case Command::SaveBufferToFile:
      return saveBufferToFile();
    case Command::ShellChangeDirectory:
      return shellChangeDirectory();
    case Command::ShellRunLsL:
      return shellRunLsL();
    case Command::ShellInsertName:
      return shellInsertName();
    case Command::ShellInsertBufferContent:
      return shellInsertBufferContent();
    case Command::GoToParentDirectory:
      clearListReturnAnnotations();
      if (currentMode_ == ApplicationMode::Directory) return directoryMode_.goToParent();
      return goToParentFromFileOrArchive();
    case Command::GoToFileSelector:
      return goToFileSelector();
    case Command::GoToRecentFile:
      return goToRecentFile();
    case Command::JumpToPathPrefix:
      return jumpToPathPrefix();
    case Command::RestartShell:
      return shellRestart();
    case Command::StartArchiveExtract:
      return startArchivePrompt();
    case Command::ShowErrorLog:
      return executeShowErrorLog();
    case Command::OpenDataDirectory:
      return executeOpenDataDirectory();
    case Command::OpenWorkList:
      return executeOpenWorkList();
    case Command::StartEpubPrompt:
      return startEpubPrompt();
    case Command::StartEpubList:
      return startEpubList();
    case Command::OpenEpubAsArchive:
      return openSelectedEpubAsArchive();
    case Command::ShowShellLog:
      return executeShowShellLog();
    case Command::StartLog:
      return executeStartLog();
    case Command::ShowUsilicBanner:
      return executeShowUsilicBanner();
    case Command::StartTutorial:
      return openTutorial();
    // 2026-08-17修正(=cn): K-46(^X^F、「Select file name」)――なぜ
    // これがもうそれを直接呼ばないのかについてはstartListBrowser()
    // 自身のコメント参照。startRegexPromptList()は、その名前にも
    // 関わらず既に十分汎用的である(純粋にPromptKindの値でディスパッチ
    // する)。
    case Command::StartFileHistory:
      return startRegexPromptList(PromptKind::RecentFile);
    case Command::EnterCopyMode:
      return startCopy();
    // ^O^Y: 実際に編集可能なプロンプトが立っている間だけ意味を持つ
    // (唯一の「貼り付け」先――clipboardText()が常にプレーンテキスト
    // なのはなぜかについてはcopy_mode.hpp自身のクラスコメント参照)。
    // プロンプトが有効でない場合はno-op(クラッシュではない)、現在の
    // 文脈で意味を持たないキーについてのこのポート自身の慣習に
    // 合わせている。
    case Command::PasteClipboard:
      if (!promptActive()) return false;
      promptEditor_.paste(copyMode_.clipboardText());
      return true;
    // ^[P(2026-09-08追加、Command::PasteTransientTargets自身の
    // コメント参照): PasteClipboardと同じpromptActive()ガードだが、
    // 貼り付ける内容はCopyModeのクリップボードではなく
    // DirectoryMode::transientMarkedNames()(CP/MV/ARC完了後の一時
    // マーキング)から都度組み立てる。requirements.md「設計判断A」
    // 決定通り、フルパス+シェルクォート済みで挿入する(宛先ディレク
    // トリを離れると相対名は無意味になるため)。
    case Command::PasteTransientTargets: {
      if (!promptActive()) return false;
      std::vector<std::string> names = directoryMode_.transientMarkedNames();
      if (names.empty()) return false;
      std::string dir = directoryMode_.currentDirectory();
      if (!dir.empty() && dir.back() != '/') dir += '/';
      std::vector<std::string> paths;
      paths.reserve(names.size());
      // stripLsFTypeMarker()自身のコメント参照――実在のパスとして
      // 使うので、`ls -F`の型マーカー('*'/'@'等)を剥がす(ディレクトリ
      // の'/'は実在のパスの表記としても問題ないが、剥がした上で改めて
      // 明示的に付け直すよりも、他の型マーカーと同じ扱いに揃える方が
      // 単純)。
      for (const auto &name : names) paths.push_back(dir + stripLsFTypeMarker(name));
      promptEditor_.paste(shellQuoteArgv(paths));
      return true;
    }
    case Command::StartExit:
      return startExitPrompt();
    case Command::StartDu:
      return runDuOnCurrentFolder();
    case Command::RedisplayDu:
      return redisplayDu();
    // 2026-08-17修正(=cn): K-48(^X^D、「Select directory name」)――
    // すぐ上のStartFileHistoryと同じ理屈。
    case Command::StartDirectoryHistory:
      return startRegexPromptList(PromptKind::RecentDirectory);
    // (Command::StartCommandHistory: 2026-09-20削除、旧startListBrowser()
    // 自身の削除コメント参照。)
    // 通常のDirectory/File閲覧からのプレーンな^R(2026-08-18追加、
    // 直接のユーザー依頼): legacy自身の素の^R(M_hist)は文字通りどの
    // モードからでもディスパッチされるが、プレーンな"(ls)"の閲覧から
    // 冷たく押されると、機能的に無用な"HISTORY 0/0 END No string"画面
    // を開くことが実証的に判明した(実際の新規ビルドしたlegacyバイナリ、
    // 項目353自身の調査参照)――ユーザー自身の理論: ^Rは「最初は」
    // COMMAND(SHELL実行履歴)専用として実装され、DIR/FILE/FGREP等が
    // 後から、それら自身専用の入り口(^X^D/^X^F/^J^S/...)として追加
    // された時点で、一度も配線し直されなかった。その行き止まりを
    // 忠実に再現するのではなく、これは正真正銘「新しい」ディレクトリ
    // 名/ファイル名の履歴のショートカットである: Directoryモードから
    // のプレーンな^Rは、^X^Dが行うのと「同じ」DIRピッカーを開き
    // (startRegexPromptList(RecentDirectory))、Fileモードから
    // (^Tのバナー画面を除く――bannerFilePath()自身のコメント参照;
    // 実際のユーザーファイルは普通の読み物であり、バナーは合成された
    // 索引画面であって、「ファイル名履歴」が意味を成すようなもので
    // はない)は、^X^Fが行うのと「同じ」FILEピッカーを開く。意図的に、
    // これら2つのモード「だけ」に限定してある――legacy自身の
    // (機能しない)ディスパッチテーブルのエントリが技術的にはそう
    // だった「どこからでも到達可能」ではない
    // ("機能的にはDIR（FILE）とダブるけれども、どこからでもOKでは
    // ない点が違う")。他の全てのモード(GREP/CUT/MAN/LOCATE/SHELL/...)
    // は、toggleListBrowserHistory()自身の既存の挙動へ変わらず流れ
    // 落ちる(既に開いているListBrowser画面の内部での本物のソート
    // 済み<->時系列のトグル、他の場所ではno-op)。
    case Command::ToggleHistoryView:
      if (currentMode_ == ApplicationMode::Directory) return startRegexPromptList(PromptKind::RecentDirectory);
      if (currentMode_ == ApplicationMode::File && fileMode_.currentFile() != bannerFilePath()) {
        return startRegexPromptList(PromptKind::RecentFile);
      }
      // 2026-08-20追加、直接のユーザー依頼: "ARC:^R新設。内容は、
      // FILE一覧のうちARC対応拡張子...付属ファイルのみの一覧" +
      // 補足説明"カレントディレクトリ如何に問わず今までアクセスした
      // アーカイブファイルが対象。ARCモードでの^Rで発動"――
      // Directory/File自身の^X^Fが既に使っているのとまさに「同じ」
      // RingCategory::RecentFileのringとPromptKind::RecentFileの
      // ピッカーを再利用する(アーカイブは、このポートが既にそのring
      // へ記録している「ファイル」である、
      // openSelectedEntry()/executePathInput()自身のpushRecent()呼び
      // 出し参照)、archiveHistoryFilterActive()(ここではなく
      // startPromptHistoryRecall()/toggleListBrowserHistory()自身の
      // 内部でチェックされる)によってアーカイブ互換のパスだけへ
      // フィルタリングされる――ユーザー自身の明示的な"カレント
      // ディレクトリ如何に問わず"に合わせ、ディレクトリのスキャンで
      // はなく履歴に基づく。
      if (currentMode_ == ApplicationMode::Archive || currentMode_ == ApplicationMode::ArchiveContent) {
        return startRegexPromptList(PromptKind::RecentFile);
      }
      return toggleListBrowserHistory();
    case Command::StartEdit:
      return executeEdit();
    case Command::StartHelp:
      return startHelp();
    case Command::StartMacroRecord:
      return startMacroRecording();
    case Command::StopMacroRecord:
      return stopMacroRecording();
    case Command::PlayMacro:
      return playRecordedMacro();
    case Command::ToggleGuiMode:
      guiModeToggleRequested_ = true;
      return true;
    case Command::Redraw:
      activeView().setHoverIndex(std::nullopt);  // Move*/Page*系のcase自身のコメント参照
      // 2026-09-08追加、直接のユーザー要望("^Lで消去"、DirectoryMode::
      // setTransientMarkedNames()自身のコメント参照)――CP/MV/ARC完了後
      // の一時マーキングを^Lで消す。CP/MV/RM着手前の複数選択
      // (markedIndices_)はclearTransientMarks()では一切触れないので
      // 影響しない。
      if (currentMode_ == ApplicationMode::Directory) directoryMode_.clearTransientMarks();
      // 2026-09-08追加、直接のユーザー要望("ページTOPとENDいがいでは、
      // ^L（描き直し）が投入された場合、ブロックカーソル位置のカレント
      // 行が画面中央に来るようにしてほしい。viの番号指定起動および
      // emacsの^Lが同じ仕様")――ContentView::centerOnCursor()自身の
      // コメント参照。バッファのTOP/END付近ではsetTopIndex()自身の
      // 既存のクランプにより自然に「可能な範囲で」寄るだけになる。
      activeView().centerOnCursor();
      activeView().redraw();
      activeView().syncVisibleCursor();
      return true;
    case Command::Suspend:
      return handleSuspend();
    case Command::GoToTop:
      activeView().setHoverIndex(std::nullopt);
      if (currentMode_ != ApplicationMode::Shell) activeView().markJumpOrigin();  // ^X^X用、それ自身のコメント参照
      activeView().goToTop();
      // 2026-08-26のバグ修正(すぐ上のESC-</ESC->のSHELL退出の報告と
      // 併せて発見): この汎用のコマンドはscrollback_自身のcursorIndex_
      // を直接動かし、Shell「自身の」^P/^N固有のhistoryBrowsing()/
      // atLivePromptLineの追跡を完全に迂回する(Shell::
      // historyBrowsing()自身の宣言コメント参照)――そのため、
      // cursorIndex_は本当に行0へ動いたにも関わらず、位置フィールド
      // は古い「静止」時のtotal/currentを(前からの値のまま)表示し
      // 続けていた。beginHistoryBrowsing()はcursorIndex_自体を動かさ
      // ない(上のgoToTop()が既にそれを行った)――それはフラグを同期
      // させて再描画するだけであり、それを「後に」呼ぶことがまさに
      // 欠けていた同期ステップになる、currentMode_がShellでない時は
      // (最初にチェックされているので)無害なno-opである。
      if (currentMode_ == ApplicationMode::Shell) shell_.beginHistoryBrowsing();
      activeView().syncVisibleCursor();
      return true;
    case Command::GoToBottom:
      activeView().setHoverIndex(std::nullopt);
      if (currentMode_ != ApplicationMode::Shell) activeView().markJumpOrigin();  // ^X^X用、それ自身のコメント参照
      activeView().goToBottom();
      // すぐ上のCommand::GoToTop自身の同一の2026-08-26のコメント参照:
      // scrollback_の「底」に到達することは、まさにShell自身の
      // exitHistoryBrowsing()が既に意味すること(「生きたプロンプトへ
      // 戻る」、それ自身の宣言コメント参照)である――それ自身の
      // goToBottom()呼び出しは、すぐ上の呼び出し(cursorIndex_は既に
      // そこにある)と重複する無害なno-opであり、実際に欠けていたのは
      // atLivePromptLine自身のフラグの同期である。
      if (currentMode_ == ApplicationMode::Shell) shell_.exitHistoryBrowsing();
      activeView().syncVisibleCursor();
      return true;
    case Command::ExchangeJumpMark:
      // SHELLのスクロールバックはESC </>自体が履歴閲覧の開始/終了を
      // 兼ねる別物(上のGoToTop/GoToBottom参照)なので対象外。
      if (currentMode_ == ApplicationMode::Shell) return false;
      activeView().setHoverIndex(std::nullopt);
      if (!activeView().exchangeJumpMark()) return false;
      activeView().syncVisibleCursor();
      return true;
    case Command::QuitPager:
      // usilicrcはこれをページャ専用として文書化している("Exit usilic
      // (pager)");Directoryモードにはそれ自身の確認済みの終了経路
      // (StartExit、^X^C)があり、素の'q'にそれを迂回されたくない。
      if (currentMode_ == ApplicationMode::Directory) return false;
      // 2026-08-25(上のCommand::Back自身のFileモードの分岐と同じ
      // ユーザーの追跡報告): SHELLの引き渡しで起動されたページャは、
      // 'q'でプロセス全体を殺しては「ならない」――それはユーザー自身
      // の生きたSHELLセッションを壊してしまい、まさにこの機能全体が
      // 存在する目的である「全く新しいusilicプロセスに見える」という
      // 症状そのものになる。そこでの^Gと同一に、代わりにShellへ戻る。
      if (currentMode_ == ApplicationMode::File && fileMode_.pagerReturnsToShell()) {
        return returnPagerToShell();
      }
      requestImmediateExit();
      return true;
    case Command::ToggleScrollMode: {
      bool enabled = !display_.smoothScrollEnabled();
      display_.setSmoothScrollEnabled(enabled);
      history_.setSmoothScrollEnabled(enabled);  // 実行をまたいで永続化される、History自身のフィールドのコメント参照
      return true;
    }
    // 2026-09-12追加、項目⑫(Command::ToggleModeLineVisibility自身の
    // 宣言コメント参照): ToggleLsMode等と異なり、この切り替えは
    // 予約行数そのものを変える。2026-09-13修正、直接のユーザー報告
    // ("スクロールバーが出るたびに折り返し位置の文字が消えてしまって
    // 文章が繋がらない"、項目462のフィードバック): 項目461でTerminalDisplay::
    // columns()自体をmodeLineVisible()に連動させて以来、この
    // layoutViews()+redraw()だけの旧い組み合わせでは幅が変わったこと
    // 自体をFileMode/ManModeが知らないまま(reflow()が一度も呼ばれない
    // ため)取り残されていた――表示だけが新しい(狭い)columns()で
    // 切り詰められる一方、保持している折り返し済みの行自体は古い幅の
    // ままだったため、行の末尾が黙って切り落とされ、次の行の本来の
    // 続きとかみ合わなくなっていた。relayoutForModeLineVisibilityChange()
    // (自身のコメント参照、今はhandleResize()そのものへ委譲している)
    // へ差し替えることで、本物の端末リサイズと全く同じ経路でFileMode/
    // ManModeの再折り返し・DirectoryModeのls列レイアウト再評価・Shell
    // のonResize()もまとめて行われるようにした。
    case Command::ToggleModeLineVisibility: {
      bool visible = !display_.modeLineVisible();
      display_.setModeLineVisible(visible);
      history_.setModeLineVisible(visible);  // 実行をまたいで永続化される、History自身のフィールドのコメント参照
      relayoutForModeLineVisibilityChange();
      return true;
    }
    case Command::StartFileInput:
      return startPathInputPrompt(/*isDirectory=*/false);
    case Command::StartDirectoryInput:
      return startPathInputPrompt(/*isDirectory=*/true);
    case Command::ToggleLsMode: {
      if (currentMode_ != ApplicationMode::Directory) return false;
      bool enabled = !directoryMode_.filenameOnlyMode();
      directoryMode_.setFilenameOnlyMode(enabled);
      history_.setLsMode(enabled ? 1 : 0);  // 実行をまたいで永続化される、History自身のフィールドのコメント参照
      return true;
    }
    // ToggleLsReverse/CycleLsSortKey自身の宣言コメント(application.hpp)
    // 参照。ToggleLsMode(^Oo)と同じくDirectoryモード限定。
    case Command::ToggleLsReverse:
      if (currentMode_ != ApplicationMode::Directory) return false;
      directoryMode_.toggleReverseSort();
      return true;
    case Command::CycleLsSortKey:
      if (currentMode_ != ApplicationMode::Directory) return false;
      directoryMode_.cycleSortKey();
      // DirectoryMode::setSortLabelShown()自身の宣言コメント参照(2026-09-25):
      // 新しいソートキーのラベルを"(ls)"の欄に出し、どのキーでも消す。
      directoryMode_.setSortLabelShown(true);
      sortLabelNoticeShown_ = true;
      directoryView_.redraw();
      return true;
    case Command::MarkAll:
      // LOCATE/DU/^X^F・^X^D/USILIC(2026-09-27、list_marks.hpp参照)。
      if (currentMode_ != ApplicationMode::Directory && currentMode_ != ApplicationMode::Archive) {
        int size = 0;
        ListMarks *marks = activeListMarks(&size);
        if (!marks) return false;
        marks->markAll(size);
        activeView().redraw();
        return true;
      }
      if (currentMode_ == ApplicationMode::Archive) {
        archiveMode_.markAll();
        archiveView_.redraw();  // ContentView::setMarkedPredicate()自身のコメント参照
        return true;
      }
      if (currentMode_ != ApplicationMode::Directory) return false;
      directoryMode_.markAll();
      directoryView_.redraw();
      return true;
    case Command::ClearAllMarks:
      if (currentMode_ != ApplicationMode::Directory && currentMode_ != ApplicationMode::Archive) {
        int size = 0;
        ListMarks *marks = activeListMarks(&size);
        if (!marks) return false;
        marks->clear();
        activeView().redraw();
        return true;
      }
      if (currentMode_ == ApplicationMode::Archive) {
        archiveMode_.clearMarks();
        archiveView_.redraw();
        return true;
      }
      if (currentMode_ != ApplicationMode::Directory) return false;
      directoryMode_.clearMarks();
      directoryView_.redraw();
      return true;
    case Command::None:
      return false;
  }
  return false;
}

// mode.cのT_dirケースが、T_tar/T_lha/T_unzip/T_rpm/T_debに達するか
// T_fileになるかから移植: legacyは、それをプレーンテキストとして
// 見ようとする前に、選択されたものの拡張子でディスパッチする。ここ
// ではそれは単に「まずArchiveModeを試し、FileModeにフォールバック
// する」というだけである――この単純化が導入する唯一の忠実性の隙間
// (一覧表示のコマンドが失敗する実際のアーカイブの拡張子は、特定の
// エラーではなくプレーンテキスト表示へ流れ落ちる)についてはクラス
// コメント参照。
bool Application::openSelectedEntry() {
  const DirectoryEntry *entry = directoryMode_.selectedEntry();
  if (!entry) return false;
  if (entry->isDirectory) {
    if (entry->name != "./") clearListReturnAnnotations();  // `./`は再読込のみで移動ではない
    return directoryMode_.openSelected();
  }
  // 2026-09-28追加、直接のユーザー報告("シムリンクが開かない。「pt3 ->
  // /var/www/html/epgrec/video/」の行をクリックしても空白"): `ls -l`の
  // 行頭が'l'のシンボリックリンクはisDirectoryにならないため、参照先が
  // ディレクトリでもファイルとして開こうとして空白になっていた。参照先を
  // 実際にstatし、ディレクトリならリンク自身のパスのまま(`cd`と同じく
  // 論理パスで、"../"はリンクの置かれたディレクトリへ戻る)降りる。
  if (!entry->lsLine.empty() && entry->lsLine[0] == 'l') {
    std::string target = directoryMode_.currentDirectory();
    if (!target.empty() && target.back() != '/') target += '/';
    target += entry->name;
    std::error_code ec;
    if (std::filesystem::is_directory(target, ec) && !ec) {
      clearListReturnAnnotations();
      return directoryMode_.descendTo(target + "/");
    }
  }

  // 2026-08-27追加、実際の報告("^J^K→カーソル位置が「usilicrc」
  // じゃない"): このディレクトリを離れる他の「全ての」方法
  // (descendTo()/goBack())は既にまずカーソル自身の行を保存している
  // ――プレーンな「ファイル」を開くことは、それがディレクトリから
  // ディレクトリへのナビゲートではないため、一度もそうしておらず、
  // 後で「このディレクトリへ」再入場する何か(goToFileSelector()自身
  // のenterDirectory(parent)呼び出し、あるいは他のどんなプレーンな
  // 再訪問でも)に、「ユーザーがちょうどいた場所」ではなく、古い/
  // 存在しない保存済みの位置を復元させることになってしまっていた。
  // DirectoryMode::saveCurrentCursorPosition()自身のコメント参照。
  directoryMode_.saveCurrentCursorPosition();
  // 2026-08-27修正、実際の報告+再現("~/.usilic/saved/（ls）で
  // 「ＢＳ世界の〜」を選択→^G連打で（ls）との行き来無限ループ"):
  // なぜファイルを開くことに特にこの明示的な呼び出しが必要なのか
  // (サブディレクトリへ降りることとは異なり、そちらは既に
  // enterDirectory()経由でそこに到達する)についてはDirectoryMode::
  // clearArcExtractSourceAnnotation()自身のコメント参照。これが無い
  // と、注釈はファイルを見た後も生きたままになり、そのため「次の」
  // ^G(このディレクトリに戻って)は依然としてgoToRecentFile()
  // (^J^J)へルーティングされていた――それはまさにこの同じファイル
  // (RecentFile自身の最も最近のエントリ)を再び開き、それが「その後」
  // ここへ再び^Gで戻る、永遠に。
  directoryMode_.arcAnnotations().pop();
  // 2026-09-15追加、直接のユーザー訂正("3.が惜しい。pre-commitを
  // 選択の時点でDUの履歴をクリアでよろしく（現状では引き続き^Gの
  // 戻り値がDU: /Users/...）。お手本なら`.`（カレントフォルダー）を
  // 選択、で確認可能"): すぐ上のclearArcExtractSourceAnnotation()と
  // 全く同じ理屈が、GREP/CUT/DU自身のsearchAnnotation_(directoryMode_.
  // setSearchAnnotation()、"GREP: ..."/"DU: ..."等)にも当てはまる。
  // ディレクトリのエントリを開く方(directoryMode_.openSelected()、
  // ".")はenterDirectory()を経由し、それ自身が内部で新しい`ls`ごとに
  // 古い検索を無効化する(directoryMode_.enterDirectory()自身の実装
  // 参照)ので「お手本」通り即座にクリアされる――プレーンな「ファイル」
  // を開く方はそのenterDirectory()呼び出しを一度も経由しないため、
  // 前回(returnFromDu()へのclear追加)はDu結果画面を経由して1往復
  // した「後」でようやくクリアされる程度にとどまっていた。この呼び
  // 出しで、ファイルを開いた「その場で」即座にクリアするようにした。
  directoryMode_.primaryAnnotations().pop();

  std::string path = directoryMode_.currentDirectory();
  if (!path.empty() && path.back() != '/') path += '/';
  path += entry->name;

  // EXEC(拡張子にマップされた外部ビューア)はアーカイブの検出より
  // 優先される――is_child()自身のf.execを先にチェックするという
  // 優先順位についてはクラスコメント参照(もっとも、Configurationの
  // 統一された拡張子テーブルは、実務上はチェックの順序をほぼ無意味
  // にしている)。完全に引き渡して、PROGRAM/EDITと同じ形で、
  // Directoryモードへ変わらず戻る。
  if (openExternallyIfApplicable(path)) return true;  // EXEC/画像のクイックルック(2026-09-27)
  if (isEpubFile(path)) return openEpub(path);  // openEpub()自身のコメント参照

  // 2026-09-20修正、直接のユーザー報告("debian-keyring...tar.xzを
  // 踏んだら反応が無くなった。LOCATE同様、^Gで読み込みを中断できる
  // ようにして"): 以前はここで同期的なarchiveMode_.loadArchive(path)
  // を直接呼んでおり、大きな(圧縮)アーカイブの展開+一覧構築が完了
  // するまで^G含め一切の入力に反応できなかった。startArchiveLoadAsync()
  // /awaitingArchiveLoad()/pollArchiveLoadCompletion()(runDuOnCurrentFolder()
  // と全く同じ非同期+^G中断の形)へ切り替えた――archiveParentStack_の
  // クリア/RecentFileへのpushRecent()という、以前ここにあった後始末は
  // finishArchiveLoadAsync()へ移した。ユーザーが直接選択した「最上位
  // の」アーカイブ(この呼び出し箇所)だけがこの非同期経路を通る――
  // openRecentFileEntry()(^X^F履歴再生)・materializeArchiveTempFile()
  // 経由のネストした降下・goToParentArchive()は、いずれも通常は
  // 小さい/既に手元にある内容なので、当面は従来通り同期のまま
  // (loadArchive()自身は無改造)。
  ArchiveKind archiveKind = ArchiveKind::None;
  if (configuration_.programForSuffix(path, &archiveKind) && archiveKind != ArchiveKind::None) {
    drawPromptLabel(PromptKind::ArchiveLoad, /*positionText=*/"", "Loading archive...");
    display_.flush();
    if (startArchiveLoadAsync(path)) return true;  // 今は待機中――awaitingArchiveLoad()参照
    activeView().redraw();
  }
  // BUFFERを超えるファイルは、FileMode::loadFile()自身が^O^M方式へ切り替える
  // (そちらのコメント参照)。
  if (!fileMode_.loadFile(path, [this]() { return keyInterruptRequested(); })) return false;
  currentMode_ = ApplicationMode::File;
  fileReturnMode_ = ApplicationMode::Directory;  // それ自身の宣言コメント参照
  return true;
}

// K-23(^O^M、M_fp、「行単位で読む」)/K-24(^[^M、M_code、「そのまま
// 読む」)、いずれも(ls)専用(QUICK.txt自身の"<(ls)>"の注記参照)――
// openSelectedEntry()自身のEXEC/アーカイブ判定の分岐までは複製しない
// (高度な/稀な機能であり、それらのファイル種別に対してこの2キーを
// 使うこと自体が想定外)。ディレクトリの選択時だけ明示的に拒否し、
// それ以外は(下のARCHIVE特例を除き)FileMode::loadFileAsPipe()へ
// 直接渡す――通常ファイルでなければそちら自身のstd::ifstreamチェック
// で自然にfalseになる。2026-09-15修正前はloadFileNoWrap()だった
// (loadFileAsPipe()自身のコメント参照、"(stdin)"ラベル付けが無かった)。
bool Application::openSelectedEntryNoWrap() {
  const DirectoryEntry *entry = directoryMode_.selectedEntry();
  if (!entry || entry->isDirectory) return false;
  std::string directory = directoryMode_.currentDirectory();
  if (!directory.empty() && directory.back() != '/') directory += '/';
  std::string path = directory + entry->name;

  // 2026-09-20追加、直接のユーザー指摘("^O^Mは.tarファイル等に適用
  // すると、ARC表示ではなく項目が選べないtvf表示画面になるだけだと
  // 思う"): old/src/menu.cを読み直すと、M_fp(^O^M)はfp->cr=2を経て
  // m_pipe()――つまり「一覧表示コマンド自体をパイプ越しに読ませる」
  // 経路に入る。通常のEnter/openSelectedEntry()がアーカイブに対して
  // 発動するArchiveMode(entries_へ構造化して対話的に選べる一覧)とは
  // 別物で、tar tvf/unzip -l等の生出力を、選択不可なプレーンな
  // ページャーテキストとしてそのまま見せるのが正しい解釈だった
  // (以前このケースを「高度な/稀な機能」として意図的に複製しない
  // 判断をしていたが誤りだった)。ArchiveMode::loadArchive()自身が
  // 一覧コマンドを組み立てる箇所と全く同じ構築(pipedFromArgvの
  // 2段パイプライン形式含む)を踏襲する――ArchiveEntryへの
  // parseListing()はここでは呼ばない(構造化しない)。
  // 2026-09-21修正、直接のユーザー指摘("~/.usilic/配下に実在の
  // <entry>.tvfファイルが作られ、^Gで戻ると元のディレクトリではなく
  // そちらへ移動してしまう。fork()で子プロセスの出力を直接取り込め
  // ば済むのでは？"): ChildProcess::run()/runPipeline()は既にfork()+
  // exec()+パイプ読み取りで出力をメモリ上(result.output)に捕捉し
  // 終えている――以前はそれをmaterializeArchiveTempFile()経由で
  // わざわざtempRootDirectory_直下の固定名ファイルへ書き出し直し、
  // loadFileNoWrap()という「実在パスを開く」経路へ渡していたが、
  // これがまさにユーザー指摘の2つの実害の原因だった: 残置ファイル
  // 自体、および(returnToDirectory()の「currentFile()の所属ディレク
  // トリを辿る」ロジックが誤発動する形で)^Gの戻り先の混乱。
  // FileMode::loadContentAsPipe()(loadFileAsPipe()自身のmkstemp+
  // unlinkの無名一時fd経路を公開したもの、file_mode.hpp自身の宣言
  // コメント参照)へメモリ上の内容を直接渡せば、実ファイルを一切
  // 経由せずに済む。
  // 2026-09-16修正、直接のユーザー依頼("debian-keyring...tar.xzを^M
  // した場合の^Gによる中断を^O^M/^[^Mへも適用してほしい"):
  // 以前はここでChildProcess::run()/runPipeline()を同期的に呼んで
  // おり、大きな(圧縮)アーカイブでは一覧構築が終わるまで^G含め一切の
  // 入力に反応できなかった。openSelectedEntry()自身のARCHIVE分岐
  // (^M、startArchiveLoadAsync()自身のコメント参照)と全く同じ非同期
  // +^G中断の形(startArchivePagerLoadAsync()/awaitingArchiveLoad()/
  // pollArchiveLoadCompletion()/finishArchiveLoadAsync()を共有)へ
  // 切り替えた――setSearchOrigin()/setPagerListingDirectory()呼び出し
  // 自体はfinishArchiveLoadAsync()自身のarchiveLoadForPager_分岐へ
  // 移した。
  ArchiveKind kind = ArchiveKind::None;
  if (configuration_.programForSuffix(path, &kind) && kind != ArchiveKind::None) {
    drawPromptLabel(PromptKind::ArchiveLoad, /*positionText=*/"", "Loading archive...");
    display_.flush();
    if (startArchivePagerLoadAsync(path, directory)) return true;  // 今は待機中――awaitingArchiveLoad()参照
    return false;
  }

  // 2026-09-15修正、直接のユーザー指摘("^O^Mで(stdin)形式になって
  // おらず"): ここ(アーカイブでない通常ファイル)は依然として
  // loadFileNoWrap()の素のまま――FileMode::loadFileAsPipe()自身の
  // コメント参照、legacy自身のM_fp(fp->cr=2)がm_pipe()経由のパイプ
  // 越しに読ませる分岐に対応する。
  if (!fileMode_.loadFileAsPipe(path)) return false;
  // 2026-09-16追加、直接のユーザー指摘("^O^Mの(stdin)モードラインも
  // アーカイブ同様に整備してほしい"): loadFileAsPipe()自身が内部で
  // 呼ぶsetSearchOrigin("", "(stdin)")(loadContentAsPipe()自身の
  // コメント参照)は、ARCHIVE分岐と同じ理由でファイル名/所属ディレ
  // クトリを一切示さない。"(stdin)"バッジ自体(このK-23固有のペース
  // 読み込み経路であることを示す技術的な事実)は引き続き正しいので
  // 残しつつ、name欄へ実際のファイルのフルパスを、trailing3へ所属
  // ディレクトリを追加で上書きする(ARCHIVE分岐のsetSearchOrigin(path,
  // badge)+setPagerListingDirectory(directory)呼び出しと全く同じ形)。
  // truncatedTrailingPath()(2026-09-16追加、archive_mode.cpp自身の
  // 同種の修正コメント参照): nameが今や短い固定文字列ではなくpath
  // (フルパス)になったことで、事前に切り詰めないとContentView自身の
  // withTruncatedTrailing3()の予算計算がしばしば4文字未満まで落ち込み
  // trailing3が丸ごと消え得る。
  fileMode_.annotations().pushWithNameOverride(FileMode::FileAnnotationKind::Search, path, "(stdin)");
  fileMode_.setPagerListingDirectory(truncatedTrailingPath(directory));
  currentMode_ = ApplicationMode::File;
  fileReturnMode_ = ApplicationMode::Directory;
  return true;
}

// 長い同期の読み込み(^[^Mの「そのまま読む」、^Mの通常の読み込み)の途中で、
// どれかのキーが押されたか。押されていればそのキーを捨ててtrue(2026-09-25、
// ^[^M自身のラムダから括り出し、^Mでも使うようにした――直接のユーザー
// 質問"^Mは中断できないの？")。
bool Application::keyInterruptRequested() {
  // 2026-09-25修正、直接のユーザー報告("GUI起動で.mdファイルを
  // `^[^M`すると...無反応"、-cは問題無し): 「実はすでにGUI」起動では
  // 起動元ターミナルが^Cのためにcooked mode(ICANON、main()の
  // guiOwnsWholeSession分岐のkeyInput.leaveRawMode()参照)へ戻されて
  // おり、そこでのread()はそのターミナルで改行が打たれるまで
  // ブロックする――raw mode(VMIN=0/VTIME=1、0.1秒で諦める)を前提
  // にしていたこの覗き見がそこで止まり、GUI全体が固まっていた。
  // 実際に何か届いている時だけ読む。
  //
  // 同日変更、直接のユーザー要望("`^G`で停止よりも形としては緊急性を
  // 伴うとも言えるので`^G`→任意のキーにしてほしい"): 中断のキーを
  // ^G限定から「任意のキー」へ広げた(押したキーは中断のためだけに
  // 使い、コマンドとしては実行しない)。GUIではキーがSDL経由で届き
  // 起動元ターミナルには来ないので、setInterruptPeeker()(main.cppの
  // runGuiPhase()が設定する)でGUIウィンドウ側のキーも見る。
  // usilicrcのマクロ再生中のバイトは「押されたキー」ではないので
  // 見ない(KeyInput::readByte()ではなく端末fdを直接read()する)。
  if (interruptPeeker_ && interruptPeeker_()) return true;
  if (keyInput_.fd() < 0) return false;
  struct pollfd pfd{keyInput_.fd(), POLLIN, 0};
  if (::poll(&pfd, 1, 0) <= 0 || !(pfd.revents & POLLIN)) return false;
  unsigned char discarded;
  return ::read(keyInput_.fd(), &discarded, 1) > 0;
}

// ^O^M/^[^Mの一覧版(2026-09-27追加、直接のユーザー依頼"^O^M、^[^MをLOCATE、
// DU、FILE、USILIC へもよろしく"): ハイライト行のファイルを、(ls)の
// openSelectedEntryNoWrap()/openSelectedEntryRaw()と同じ読み方で開く。^Gの
// 戻り先は^Mで開いた時と同じ(LOCATE/DU/USILICはその一覧、^X^F・^X^Dは(ls))。
// ディレクトリやアーカイブ、選択できない一覧では従来どおり^Mと同じ。
bool Application::openListEntrySpecial(bool raw) {
  int size = 0;
  if (!activeListMarks(&size)) return handleCommand(Command::Enter);
  std::string path;
  switch (currentMode_) {
    case ApplicationMode::Locate:
      if (const std::string *result = locateMode_.selectedResult()) path = *result;
      break;
    case ApplicationMode::Du:
      if (const DuEntry *entry = duMode_.selectedEntry()) path = entry->path;
      break;
    case ApplicationMode::ListBrowser:
      if (const std::string *item = listBrowserMode_.selectedItem()) {
        path = *item;
        if (auto parsed = parseCutFileHistoryEntry(path)) path = std::get<0>(*parsed);
      }
      break;
    case ApplicationMode::Underline:
      if (const std::string *entry = underlineMode_.selectedEntry()) path = *entry;
      break;
    default:
      break;
  }
  std::error_code ec;
  ArchiveKind kind = ArchiveKind::None;
  if (path.empty() || !std::filesystem::is_regular_file(path, ec) ||
      (configuration_.programForSuffix(path, &kind) && kind != ArchiveKind::None))
    return handleCommand(Command::Enter);

  ApplicationMode origin = currentMode_;
  std::string originName = origin == ApplicationMode::Locate ? "LOCATE"
                           : origin == ApplicationMode::Du     ? "DU"
                           : origin == ApplicationMode::Underline ? "USILIC"
                                                               : "FILE";
  if (raw) {
    auto shouldAbort = [this]() { return keyInterruptRequested(); };
    if (!fileMode_.loadFileRaw(path, shouldAbort)) return false;
    fileMode_.annotations().pushWithNameOverride(FileMode::FileAnnotationKind::Search, path, originName);
  } else {
    if (!fileMode_.loadFileAsPipe(path)) return false;
    fileMode_.annotations().pushWithNameOverride(FileMode::FileAnnotationKind::Search, path, "(stdin)");
    std::string directory = std::filesystem::path(path).parent_path().string() + "/";
    fileMode_.setPagerListingDirectory(truncatedTrailingPath(directory));
  }
  if (origin == ApplicationMode::ListBrowser) promptHistoryContext_ = std::nullopt;
  currentMode_ = ApplicationMode::File;
  fileReturnMode_ = origin == ApplicationMode::ListBrowser ? ApplicationMode::Directory : origin;
  fileView_.redraw();
  return true;
}

bool Application::openSelectedEntryRaw() {
  const DirectoryEntry *entry = directoryMode_.selectedEntry();
  if (!entry || entry->isDirectory) return false;
  std::string path = directoryMode_.currentDirectory();
  if (!path.empty() && path.back() != '/') path += '/';
  path += entry->name;
  // 2026-09-16追加、直接のユーザー報告("debian-keyring...tar.xz(75MB)
  // の^[^Mが^Gで中断不能")+提案("一回の読み込み分が終わったらそこを
  // 通り、^Gが投入されていたら中断"): FileMode::readAndWrapChunked()
  // (loadFileRaw()自身のコメント参照)が各チャンクの後に呼ぶ中断判定。
  // KeyInput::readByte()は元々raw mode設定(VMIN=0/VTIME=1)によりブロック
  // せず最大0.1秒で戻る、まさにご提案の「入力待ち無しのgetchっぽい
  // 関数」そのもの――新しい仕組みは不要だった。^G以外の何かが偶然
  // 届いていた場合(考えにくいが、例えば直前の操作の取りこぼし等)は
  // 捨てずにplayRawMacro()で送り返し、通常のディスパッチへ委ねる。
  auto shouldAbort = [this]() { return keyInterruptRequested(); };
  if (!fileMode_.loadFileRaw(path, shouldAbort)) return false;
  currentMode_ = ApplicationMode::File;
  fileReturnMode_ = ApplicationMode::Directory;
  return true;
}

// mode.cのT_tar等のEnter処理から移植、それに新しいネストしたアーカ
// イブのサポートを加えたもの(何かの移植ではない――legacyには一度も
// これが無かった、descendToNestedArchive()自身のコメント参照):
// 選択されたエントリがそれ自体、認識されたアーカイブの拡張子であれ
// ば、その生のバイトを見せる代わりに、openSelectedEntry()が既に
// 「最上位」で行っているのと同じ拡張子ディスパッチで、その中へ再帰
// する。そうでなければ: 選択されたエントリの生のバイトを展開し、
// プレーンなテキストの行として表示する(PipeBufferの文字コード/
// オーバーストライクの処理は無い――ArchiveModeは生のバイトだけを
// 返す、クラスコメント参照)。ディレクトリのエントリ、展開をサポート
// しない形式(RPM/.deb)、あるいは展開の失敗についてはfalse――
// ArchiveMode::extractSelected()はこれらのどれも他と区別しないので、
// これもそうする。
bool Application::openSelectedArchiveEntry() {
  // 2026-08-27修正、実際の報告+再現("^X^M[return]①→^G（=^J^J）→
  // カーソル位置が「utf8/ＢＳ世界の〜」じゃない"):
  // descendToNestedArchive()は離れる前に既にカーソル位置を保存して
  // いる(それ自身の呼び出し、すぐ下)が、プレーンな展開して見るだけ
  // の分岐は一度もそうしていなかった――ArchiveMode::loadArchive()
  // 自身のhistory_.positionFor()の問い合わせは、その時、このエントリ
  // が一度も選択される「前」から保存されていた何か(もしあれば)より
  // 新しいものを何も持たず、戻った時にカーソルをどこか古い場所に
  // 着地させていた。Application::openSelectedEntry()内の
  // DirectoryMode::saveCurrentCursorPosition()自身の呼び出しと同じ
  // 配置で、どちらの分岐よりも前に、ここで無条件に保存する。
  archiveMode_.saveCursorPosition();
  if (const ArchiveEntry *entry = archiveMode_.selectedEntry(); entry && !entry->isDirectory) {
    ArchiveKind nestedKind = ArchiveKind::None;
    if (configuration_.programForSuffix(entry->name, &nestedKind) && nestedKind != ArchiveKind::None)
      return descendToNestedArchive(*entry);
  }

  auto content = archiveMode_.extractSelected();
  if (!content) return false;
  capBufferContent(*content);  // usilicrcのBUFFER、それ自身のコメント参照

  // 2026-08-26修正、実際の報告("一覧から選んだファイル内容がハイ
  // ライト表示（←不要）かつ80桁分しか表示されない"):
  //   - setCursorHighlightEnabled(false)が単純にここで欠けていた
  //     ――他の全てのプレーンなコンテンツビュー(FileMode/ManMode/
  //     CutMode自身のコンテンツビュー)はこれをオフにする
  //     (file_mode.cpp自身のコンストラクタのコメント参照);これは
  //     依然としてContentViewのデフォルト(オン)のままだったので、
  //     読み取り専用の展開されたコンテンツのページャには「選択」
  //     すべきものが何も無いにも関わらず、現在の行がFileMode自身の
  //     反転表示のカーソルバーを示していた。
  //   - 下の生のstd::getline()による分割は、FileMode自身の
  //     readAndWrap()(PipeBuffer::feed()/flush())とは異なり、端末より
  //     幅の広い行を一切折り返さなかった――そのためrenderLine()自身の
  //     maxColによる切り詰めが、後続の表示行へ続ける代わりに、単に
  //     全ての長い行を画面の実際の幅で切り取ってしまい、「80桁分しか
  //     表示されない」と読める。FileModeが使っているのとまさに同じ
  //     PipeBufferを経由させるようにした(これはスタイルの無い生の
  //     アーカイブエントリのコンテンツなので、PipeBufferが他のどんな
  //     プレーンなファイルについても既に処理する以上の文字コード/
  //     オーバーストライクの懸念は無い)。
  std::string text(content->begin(), content->end());
  PipeBuffer pipe(encoding_, std::max(1, display_.columns()), 64L * 1024 * 1024);
  pipe.feed(text);
  pipe.flush();
  std::vector<ContentView::Line> lines;
  lines.reserve(pipe.lines().size());
  for (const auto &styled : pipe.lines()) lines.push_back({styled.text, std::nullopt, std::nullopt, styled.attributes});
  archiveContentView_.setCursorHighlightEnabled(false);
  archiveContentView_.setLines(std::move(lines));
  archiveContentView_.setCursorIndex(0);
  archiveContentView_.redraw();
  // 2026-08-30、直接のユーザー報告(GUI、"内容一覧でどれかを選ぶと
  // 選んだ時点でのブロックカーソルが画面に残留（^P/^Nで解消）"):
  // FileMode::loadFile()自身のsyncVisibleCursor()呼び出しと同じ理由
  // ――setCursorHighlightEnabled(false)で行全体のハイライトが無い
  // 以上、GUIの本物のブロックカーソル(Display::setVisibleCursor()
  // 経由、renderCursor()自身のコメント参照)だけが現在位置を示す
  // 唯一の手段である。これが無いと、直前のArchiveモード一覧で表示
  // されていた古いカーソル位置がGUI側に取り残されたまま(何であれ
  // 次にカーソルを動かす操作が改めてsyncVisibleCursor()を呼ぶまで、
  // 例えば^P/^N)、この新しい(たいてい短い)コンテンツの上に何も
  // 表示するものが無い場所へ浮いて見えてしまっていた。
  archiveContentView_.syncVisibleCursor();
  currentMode_ = ApplicationMode::ArchiveContent;
  return true;
}

// この機能についてはクラス自身の宣言コメント参照。
bool Application::descendToNestedArchive(const ArchiveEntry &entry) {
  auto content = archiveMode_.extractSelected();
  if (!content) return false;
  capBufferContent(*content);  // usilicrcのBUFFER、それ自身のコメント参照

  std::string tempPath = materializeArchiveTempFile(allocateArcDirectory(), entry.name, *content);
  if (tempPath.empty()) return false;

  std::string parentPath = archiveMode_.currentArchive();
  archiveMode_.saveCursorPosition();
  if (!archiveMode_.loadArchive(tempPath)) return false;

  archiveParentStack_.push_back(parentPath);
  currentMode_ = ApplicationMode::Archive;
  return true;
}

std::string Application::materializeArchiveTempFile(const std::string &dir, const std::string &entryName,
                                                      const std::vector<unsigned char> &bytes) {
  // 2026-08-18: 以前は素の`/tmp/usilic-arcXXXXXX`自体をmkdtemp()して
  // いた――`dir`は今や呼び出し元自身の既に確保済みのディレクトリ
  // (アーカイブの降下のためのarc.Nのスロット、あるいはバナーの単一の
  // 固定ファイルについては直接tempRootDirectory_――この関数自身の
  // 宣言コメント参照)なので、これは単にそれが存在することを保証する
  // だけでよい。
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  if (ec) return "";

  // entry.nameはそれ自体'/'を含み得る(アーカイブ内でのパス)――
  // programForSuffix()自身の拡張子ディスパッチにとってはベース名だけ
  // が重要であり、(アーカイブの内部のディレクトリ構造を再現しようと
  // するのではなく)一時ファイルを`dir`に直接置くことで、ここで他の
  // 何かをmkdir -pする必要を回避できる。
  std::string baseName = entryName;
  if (std::size_t slash = baseName.find_last_of('/'); slash != std::string::npos)
    baseName = baseName.substr(slash + 1);
  if (baseName.empty()) baseName = "entry";

  std::string path = dir + "/" + baseName;
  std::ofstream out(path, std::ios::binary);
  if (!out) return "";
  out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!out) return "";
  return path;
}

// mode.cのT_duのEnter処理から移植(なぜこれが、他の全てのプロンプト
// 駆動の結果画面とは異なり、GREP/LOCATEがそうするようなno-opで止まる
// のではなく、DirectoryMode/FileModeへまっすぐ突き進むのかについては
// クラスコメント参照): DuMode::loadUsage()は既に`du`を絶対パスで呼び
// 出しているので、`du`自身の出力エントリは既に絶対パスである――
// Cut/Archiveのセグメント/エントリとは異なり、ここではパスの結合は
// 不要である。
// 2026-09-09修正、実際のユーザー報告("DUでどれかを選ぶと戻り値が
// DUじゃないのでDU一覧へ戻らない"): ディレクトリ側は以前
// currentMode_をDirectoryへ切り替えるだけで、DUへの帰り道を一切
// 残していなかった――openSelectedGrepMatch()/openSelectedLocateResult()
// が既に確立している「注釈+lastDirectorySearchKind_」の慣習に
// 合わせ、setSearchAnnotation("DU: "+duMode_.currentPath())と
// DirectorySearchKind::Duを設定し、Command::Backのハンドラ自身の
// hasSearchAnnotation()分岐がreturnToDuResults()へルーティング
// できるようにした。ファイル側も同様に、無条件のfileReturnMode_ =
// Directoryが同じバグだった――DuをfileReturnMode_の正当な値として
// 追加した(Command::Back自身のFileの分岐参照)。
bool Application::openSelectedDuEntry() {
  const DuEntry *entry = duMode_.selectedEntry();
  if (!entry) return false;
  if (entry->isDirectory) {
    if (!directoryMode_.enterDirectory(entry->path)) return false;
    currentMode_ = ApplicationMode::Directory;
    // 2026-09-14修正、duEntryFolder()自身のコメント参照: 従来は
    // duMode_.currentPath()(集計対象のルート)固定だったが、実機での
    // 正解モードライン提示("pre-commit"を開いた場合はDU: .../.husky/
    // であって集計対象ルートではない)を踏まえ、ディレクトリ側も同じ
    // 「エントリ自身の居場所」(この分岐では、たった今enterDirectory()
    // した、そのディレクトリ自身)へ揃えた。
    // 2026-09-21修正、直接のユーザー指摘: 名前欄が既に到着先のディレクトリ
    // パスを表示しているので、"DU: パス"だと重複する。ファイルへ着地した
    // 場合(下のpushWithNameOverride(..., "DU"))と同様、"DU"だけにする。
    directoryMode_.primaryAnnotations().push(DirectoryMode::DirAnnotationKind::Du, "DU");
    // enterDirectory()は、上の注釈が設定される前に既に内部で一度
    // 再描画している――openSelectedLocateResult()自身の同一のコメント
    // 参照。
    directoryView_.redraw();
    return true;
  }
  if (openExternallyIfApplicable(entry->path)) return true;  // EXEC/画像のクイックルック(2026-09-27)
  if (isEpubFile(entry->path)) return openEpubReturningTo(entry->path, ApplicationMode::Du);
  if (!fileMode_.loadFile(entry->path)) return false;
  currentMode_ = ApplicationMode::File;
  fileReturnMode_ = ApplicationMode::Du;
  // 2026-09-10修正、実際のユーザー報告に基づく調査で発見(ファイル側は
  // fileReturnMode_だけ直しており、ディレクトリ側のsetSearchAnnotation()
  // に相当するものを一度も呼んでいなかった――loadFile()自身が内部で
  // searchAnnotation_をクリアするので、呼ぶなら「後」でなければ
  // ならない、openSelectedGrepMatch()自身のFileの分岐の同じ順序参照):
  // 注釈が無いままだと、Backでは正しくDUへ戻れていたにも関わらず、
  // モードライン自身には「DU: ...」が一切表示されていなかった。
  //
  // 2026-09-15修正、直接のユーザー訂正("HISTORY.md:13974は勘違い、
  // 正解は「DU」「1/2 TOP」「34」「.../.husky/pre-commit」――所在地が
  // DUであることの表明は戻り値ではなくモード名でなされる"): 前回まで
  // setSearchAnnotation("DU: "+folder)(name=ファイル名のまま、
  // trailing2にラベル付きの文字列を割り込ませる、GREP/CUT自身の
  // 慣習)を使っていたが、これはopenSelectedLocateResult()自身が既に
  // 確立していたsetSearchOrigin()の慣習(name「自体」をラベルへ
  // 差し替え、詳細は素のパスのみ――"LOCATE: "の接頭辞を付けない、
  // ラベル自体が既にLOCATEだと言っているから、を字句通りDUに置き
  // 換えた形)と食い違っていた。setSearchOrigin("DU", entry->path)へ
  // 切り替え、name="DU"+詳細=素の絶対パスという、LOCATEと全く同じ
  // 慣習に揃えた。
  //
  // 2026-09-20再訂正、直接のユーザー指摘(HISTORY.md:18022、LOCATE向け
  // の同種の指摘から一般化): 上の2026-09-15の判断("所在地がDUである
  // ことの表明は戻り値ではなくモード名でなされる")を撤回する。
  // モードライン最左のname欄は「今見ている中身そのもの」を表すべきで、
  // 「ここへどうやって来たか」ではない――^Gの行き先を示すのは
  // trailing2(この一覧に何らかのフレームがある間はPathMiddle切り詰め
  // が効く、file_mode.cpp自身のModeLineFieldsProvider参照)の役目。
  // openSelectedUnderlineEntry()(UNDER一覧、2026-09-20新設)を最初から
  // 正しい並びで書いたところ、DU/LOCATEだけが逆向きだったと判明した
  // ため、それら2箇所を揃える。name=entry->path(実際に見ているファイル)、
  // detail="DU"(戻り先ラベル)。
  fileMode_.annotations().pushWithNameOverride(FileMode::FileAnnotationKind::Search, entry->path, "DU");
  // loadFile()は、上の注釈が設定される「前」に既に内部で一度再描画
  // している(view_.redraw()、FileMode::loadFile()自身の実装参照)
  // ――openSelectedDuEntry()自身のディレクトリ側の分岐が
  // enterDirectory()に対して既に踏んでいるのと全く同じ罠。この
  // 明示的な再描画が無いと、次の無関係な再描画がたまたま起こるまで、
  // 画面は最初の、古い、注釈の無いモードラインを表示し続けてしまう
  // ――実際にこの罠にはまってバグとして残っていた(2026-09-10、
  // 実機での再現+ここでのfprintfデバッグで直接確認済み)。
  fileView_.redraw();
  return true;
}

// 2026-08-14: 直接のユーザー報告("ハイライト行で[return]が無反応")
// ――LocateMode自身のクラスコメントは、これを明示的に先送りしていた
// ("意図的に除外(今後の課題、このスライスではない): 選択された結果を
// どうするかを決める")、そしてその「今後の課題」の時が来た。
// すぐ上のopenSelectedDuEntry()を正確に反映している(DUの結果も同様に、
// どちらでもあり得る任意のファイルシステムパスである)――唯一の違い
// は、DuEntryが既にそれ自身のisDirectoryフラグ(`du`自身の出力から
// パース済み)を持っているのに対し、LocateMode::results()は`locate`
// からの生のパスに過ぎないので、代わりにこれはファイルシステムを
// 直接チェックする。ここでディレクトリナビゲートの床をリセット
// 「しない」というDU自身の前例に合わせている(DirectoryMode::
// resetNavigationToFloor()自身のコメント参照――それは特に^X^Dの履歴
// 「ピッカー」の「古い以前の連鎖を捨てる」ケースのためのものであり、
// ディレクトリに到達する非線形なあらゆる方法のためのものではない)。
bool Application::openSelectedLocateResult() {
  const std::string *path = locateMode_.selectedResult();
  if (!path) return false;
  std::error_code ec;
  if (std::filesystem::is_directory(*path, ec) && !ec) {
    locateDirOriginDirectory_ = directoryMode_.currentDirectory();
    if (!directoryMode_.enterDirectory(*path)) return false;
    currentMode_ = ApplicationMode::Directory;
    // 2026-08-14修正: 実際の報告("モードラインにバイト数...および
    // 戻り値「LOCATE: .../HKSCS.txt」が無い（旧版と違うので注意）")
    // ――LOCATEのマッチ経由でここに着地することは、以前は、GREP自身
    // の同一の慣習(この関数のすぐ上/下のsetSearchAnnotation()呼び
    // 出し)とは異なり、ここへ導いた検索の痕跡を一切残さなかった。
    // ここではEGREP/CUT自身の「MODE: pattern」という慣習にまさに
    // 一致させて、(マッチしたパスではなく)クエリを使う――宛先
    // ディレクトリ自体が既にDirectoryMode自身のnameフィールド全体
    // であるため、どの検索がそれを見つけたか以上に付け加えるものは
    // 何も無い。
    // 2026-09-21、直接のユーザー判断(戻り値の統一): DU/DIR/USILICと同様、戻り値は
    // "LOCATE"だけにする(検索語は付けない。ファイルへ着地した場合と同じ形)。
    directoryMode_.primaryAnnotations().push(DirectoryMode::DirAnnotationKind::Locate, "LOCATE");
    // enterDirectory()は、上の注釈が設定される前に、既に内部で一度
    // 再描画している――GREPのマッチは常に「既に」一覧表示された現在
    // のディレクトリの内側にあるため、enterDirectory()を全く呼ばない
    // openSelectedGrepMatch()とは異なり、LOCATEはまだ一覧表示されて
    // いない任意のディレクトリへ飛ぶことができ、それを呼ばなければ
    // ならない。この2回目の再描画が無いと、画面は次の無関係な再描画
    // がたまたま起こるまで、その最初の、古い、注釈の無いモードライン
    // を表示し続ける――上のユニットテスト(実際にRecordingDisplay自身
    // の画面バッファに届いたものをチェックするのではなく、
    // modeLineText()を直接呼ぶ(要求に応じて新たに再計算する))ではなく、
    // 実際のPTY実行(実際の`locate`検索+Enter)によって捕まえられた。
    directoryView_.redraw();
    return true;
  }
  // EXEC(2026-08-24追加、直接のユーザー依頼: "LOCATE、FGREP等
  // 「.mp4」マッチ一覧上でも可能で"): openSelectedEntry()自身の
  // 「EXECがプレーンな閲覧より優先される」という優先順位を反映して
  // いる――拡張子にマップされた結果(例えば`.mp4` -> `open -a IINA`、
  // 項目416参照)は、常に下のfileMode_.loadFile()へ流れ落ちるので
  // はなく、完全に引き渡して、Directoryモードから(ls)へ変わらず
  // 戻るEXECと同様に、LOCATEの結果リストへ変わらず戻る。
  if (openExternallyIfApplicable(*path)) return true;  // EXEC/画像のクイックルック(2026-09-27)
  if (isEpubFile(*path)) return openEpubReturningTo(*path, ApplicationMode::Locate);
  if (!fileMode_.loadFile(*path)) return false;
  currentMode_ = ApplicationMode::File;
  // 2026-08-14修正(ここでの以前の版に対する2つの直接のユーザーの
  // 訂正): (1) "^Gで(ls)へ戻るのはおかしい、LOCATEの検索結果一覧へ
  // 戻るわけだし"――LOCATE経由で到達したファイルはファイルシステム
  // のどこにでもあり得る、検索が行われる前にアクティブだったどんな
  // ディレクトリとも無関係なので、^Gは、どこか無関係なディレクトリ
  // へまっすぐ行くのではなく、まずLOCATE自身の結果リストへ戻るべき
  // である(fileReturnMode_自身の宣言コメント参照)。(2) "モード名は
  // LOCATE、戻り値も検索一覧同様「/Applications/Adobe Acrob...
  // source/.../HKSCS.txt」形式で"――setSearchOrigin()のラベル
  // ("LOCATE"であり、素のファイル名ではない)と詳細(完全なパス、
  // "LOCATE: "の接頭辞は付かない――ラベル自体が既にLOCATEだと言って
  // いる以上、なぜその重複がもう意味を成さないのかについてはそれ
  // 自身のコメント参照)。
  //
  // 2026-09-20再訂正、直接のユーザー指摘(HISTORY.md:18022、実機の
  // 具体例付き――「LOCATE」「00004/23297 0%」「367723」「/Applications/
  // Adobe.../HKSCS.txt」だったのを「/Applications/Adobe.../HKSCS.txt」
  // 「00004/23297 0%」「367723」「LOCATE」へ)。openSelectedDuEntry()
  // 自身の同日の同種の訂正コメント参照――name=今見ている中身
  // そのもの(パス)、detail=戻り先ラベルへ、DU/LOCATE両方揃えて逆転
  // させる。
  fileMode_.annotations().pushWithNameOverride(FileMode::FileAnnotationKind::Search, *path, "LOCATE");
  fileReturnMode_ = ApplicationMode::Locate;
  fileView_.redraw();  // すぐ上のDirectoryの分岐の同一のコメント参照
  return true;
}

// K-18(^J^K、「Go to file-selector」、usilicrc自身の行に従う――この
// ポートは今までこれを一度も実装していなかった)。LOCATE経由で到達
// したファイルという文脈で特になされた直接のユーザー依頼("検索
// ファイル所在地へ降りるためのキーアサインも実はあるので反映して
// ほしい（^J^K）")だが、そのキー割り当て自体はLOCATE固有ではなく
// 汎用的なFileモードの機能である――「現在の」ファイル自身の格納先
// ディレクトリの本物の`ls`一覧へまっすぐ飛ぶ。legacy自身の
// 「file-selector」という命名(disp.c)は、このポートが正確なカーソル
// 位置決めの挙動を検証するための手頃なlegacyバイナリを持たない、
// より深いK_zセンチネルの記帳(menu.c)に紐付いているので、意図的に
// ナビゲート自体だけに範囲を限定してある――ユーザー自身の明示的な
// 仕様に合わせている: "^J^Kなら「.../Adobe/」の(ls)になる
// （LOCATEへは戻らない）"、プレーンな、片道のジャンプ(LOCATE自身の
// 結果リストには一切触れないので、新しいディレクトリからの^Gはそこ
// へは戻らない)。openSelectedLocateResult()自身のDirectoryの分岐
// (resetNavigationToFloor()を呼ば「ない」ことについてはそれ自身の
// コメント参照)と同様、fileReturnMode_には手を付けない――ただし
// この関数はそれを呼「ぶ」(すぐ下のそれ自身の2026-08-15追加参照)、
// 別の理由で: ^X^D/^R自身のRecentDirectoryの分岐は、新しいディレク
// トリに着地する他の「全ての」方法について、この正確な「あなたが
// いた場所と行ったり来たりする」関係を既に確立しており、直接の
// ユーザー報告("(ls)での^Gは一つ前と行き来"――特にここで欠けている
// ことを確認済み、スクリーンショット=bd/=bf/=bg)により、これが
// 同じ扱いを与えられていなかった唯一のディレクトリジャンプの入り口
// であることが判明した。
void Application::clearListReturnAnnotations() {
  // 2026-09-21修正: 移動元がDu/Locate/Man一覧やFile等の場合も含め、
  // 現在地が変わる操作では常に戻り値を破棄する(モードのガードは不要)。
  manReturnsToCandidateList_ = false;
  manReturnsToUnderline_ = false;
  auto kind = directoryMode_.primaryAnnotations().topKind();
  if (!kind) return;
  if (*kind == DirectoryMode::DirAnnotationKind::Du || *kind == DirectoryMode::DirAnnotationKind::Locate ||
      *kind == DirectoryMode::DirAnnotationKind::Usilic || *kind == DirectoryMode::DirAnnotationKind::Dir) {
    directoryMode_.primaryAnnotations().clear();
  }
}

bool Application::goToFileSelector() {
  clearListReturnAnnotations();
  // 2026-08-16修正: usilicrc自身のK-?セクション(項目177/CLAUDE.md)
  // は、K-18を明らかに普遍的なキー割り当て(K-2「Return to previous
  // mode」、K-21「Go to previous regular file」、K-22「Read content
  // of file to buffer」)と一緒にグループ化している――実際のユーザー
  // 報告("CUTでは？と思い試そうとしたら^J^K不能だった...本来どこでも
  // 使えるキーアサインなので")により、これがFileだけでなくCUTからも
  // 動くことを意図されていたと確認された。CutContent/Cutはどちらも
  // File自体と同じようにファイルのコンテンツに対して作用するので、
  // ここでは同じ扱いを受ける。モードに関わらずfileMode_.currentFile()
  // を読む――バッファ亜種のCUT(Fileモードからの^Jc、これは
  // FileModeの自身の状態に一切触れないので、依然としてソースファイル
  // を保持している)については正しい――ディレクトリ亜種のCUT(K-35/36、
  // それ自身のcutFileSourcePath_は、executeCut()がそれを消費した瞬間
  // に意図的にクリアされるので、いったんCutモードの中に入ると何も
  // そのパスを保持しない;この修正の範囲外であり、LOCATE->File経由で
  // 到達したバッファ亜種だったユーザー自身のテスト済みシナリオに
  // 合わせている)へは拡張されていない。
  // 2026-08-16(続き): ハイライトされている候補を開くために最初に
  // Enterを押すことなく、^X^F自身のRecentFile候補リスト
  // (ApplicationMode::ListBrowser)からも直接使える――実際の報告
  // ("同画面で^J^Kが利かない=bo"、=boはまさにその候補リストの画面)。
  // この時点では何もまだ開かれていないので、fileMode_.currentFile()
  // ではなくハイライトされている候補文字列自体(items()[cursorIndex])
  // を読む。
  // 2026-08-16(続き): LOCATE自身の結果リスト(ApplicationMode::
  // Locate)からも直接使える――実際の報告("新^J^K: どこでも使えない。
  // 特にLOCATE一覧=buでの^J^Kを試したい")、すぐ上のListBrowser/
  // RecentFileのケースと同じ形: ハイライトされている結果自身のパス
  // を読む、何も先に開かれている必要は無い。
  // Directory(2026-08-23追加、直接のユーザー報告: このキーがそうし
  // なければあなたを着地させたであろう(ls)の一覧に既にいることは、
  // それが何もしない理由にはならない――legacy自身の相当物は、
  // 既にそこにいる時でも一覧の再実行を兼ねる("すでに着地済みとは
  // 言え何もしないわけではなく、lsの再実行も兼ねている")、例えば
  // ファイルをその場で上書き(既存の名前へのCP)した後、離れて戻る
  // ことなく一覧の古いサイズ/mtimeの列を更新したい場合)。
  // DirectoryMode::refresh()(2026-08-23、同日、直接の追跡訂正: この
  // 修正の最初の版はenterDirectory()を直接呼んでおり、それは「保存
  // された」履歴位置を参照して、ユーザーが現在いた場所に留めるので
  // はなく、以前の何らかの訪問からの古い行へカーソルを飛ばして
  // しまっていた――"現在行位置が更新されない...^P^P^P...^J^K
  // （マイナカード表.pngに戻る）")は、「同じ」パスに対して`ls`を
  // 再実行し、依然として同じエントリ名を持つ行へカーソルを復元する。
  if (currentMode_ == ApplicationMode::Directory) {
    if (!directoryMode_.refresh()) return false;
    display_.hideVisibleCursor();
    directoryView_.redraw();
    return true;
  }
  // Shell(2026-08-27追加、直接のユーザー報告――なぜこれにも
  // currentMode_==Shellがそもそもここへ到達できるようになる前の
  // 事前切り替えの除外が必要だったのかについてはfeedShellByte()自身
  // のisShellSafeGoToFileSelectorのコメント参照): SHELLからの「本物の
  // `ls`一覧へ行く」は、下の他の全ての分岐がそれを意味するような
  // 「ファイル」の格納先ディレクトリではなく、SHELL自身の実際の
  // 作業ディレクトリ(shell_.currentDirectory()、それ自身のプロンプト
  // から追跡される)を意味する――Shellには「現在のファイル」という
  // 概念がそもそも無い。
  if (currentMode_ == ApplicationMode::Shell) {
    std::string dir = shell_.currentDirectory();
    if (dir.empty()) return false;
    // なぜpreviousDirectoryがenterDirectory()がcurrentDirectory_を
    // 上書きする「前に」取得されるのかについては、下のFile/Cut/
    // CutContentの分岐についてのこの関数自身の同一のコメント参照。
    std::string previousDirectory = directoryMode_.currentDirectory();
    if (!directoryMode_.enterDirectory(dir)) return false;
    if (!previousDirectory.empty()) directoryMode_.resetNavigationToFloor(previousDirectory);
    currentMode_ = ApplicationMode::Directory;
    display_.hideVisibleCursor();
    directoryView_.redraw();
    return true;
  }
  // 2026-09-14追加、直接のユーザー報告("HELP画面で^J^J/^J^K/^O^K/
  // （どこでもキー）が利かない（一度でも（ls）へ行けばOK）"):
  // Cut/CutContentを「常にFileから来た」ものとして扱うすぐ下の
  // 分岐(sourceFile = fileMode_.currentFile())は、実際のユーザー
  // 起動のCUT(cutOrigin_==File)には正しいが、HELP/MAN/LOCATE/DU/
  // Directory起点のCUT(それぞれ自分自身のビューモデルから内容を
  // 得ており、fileMode_には一切触れない)には成り立たない前提
  // だった――一度も実ファイルをFileモードで開いていないセッション
  // (例えば起動直後に直接^Hを押した場合)では、fileMode_.currentFile()
  // が空のままなので、以下のparent.empty()チェックで常に失敗して
  // いた(「一度でも(ls)で何かを開けばOK」に見えたのは、それが
  // fileMode_.currentFile()を初めて非空にする、たまたま一番身近な
  // 操作だったため)。File起点以外のCut/CutContentは、そもそも
  // 「ファイル」という概念を持たない――その代わり、Cut/CutContentへ
  // 入る前のdirectoryMode_自身の状態はここでも無傷のまま残っている
  // ので、素のDirectoryモード自身の上の分岐と全く同じ「そこを
  // 再表示する」動作にフォールバックする。
  if ((currentMode_ == ApplicationMode::Cut || currentMode_ == ApplicationMode::CutContent) &&
      cutOrigin_ != CutOrigin::File) {
    if (!directoryMode_.refresh()) return false;
    currentMode_ = ApplicationMode::Directory;
    display_.hideVisibleCursor();
    directoryView_.redraw();
    return true;
  }
  std::string sourceFile;
  if (currentMode_ == ApplicationMode::ListBrowser) {
    if (listBrowserCategory_ != RingCategory::RecentFile) return false;
    const std::string *selected = listBrowserMode_.selectedItem();
    if (!selected) return false;
    sourceFile = *selected;
  } else if (currentMode_ == ApplicationMode::Locate) {
    const std::string *selected = locateMode_.selectedResult();
    if (!selected) return false;
    sourceFile = *selected;
  } else if (currentMode_ == ApplicationMode::File || currentMode_ == ApplicationMode::Cut ||
             currentMode_ == ApplicationMode::CutContent) {
    sourceFile = fileMode_.currentFile();
  } else if (currentMode_ == ApplicationMode::Archive || currentMode_ == ApplicationMode::ArchiveContent) {
    // 2026-08-27追加、直接のユーザー報告("^J^K: ARCで利かない"): この
    // else-ifの連鎖には、同じような「もう1段階先へ行く」キーについて
    // 既に両方を同じ方法でカバーしている、すぐ下のgoToParentFromFileOrArchive()
    // (^O^K)とは異なり、Archive/ArchiveContentの分岐がそもそも一度も
    // 無かった。currentArchive()はアーカイブの「ファイル」自体である
    // (下のparent_path()はそれを含むディレクトリである)、他の全ての
    // 分岐自身の「ソースはファイルパスである」という慣習に合わせて
    // いる。
    sourceFile = archiveMode_.currentArchive();
  } else {
    return false;
  }
  std::filesystem::path path(sourceFile);
  std::string parent = path.parent_path().string();
  if (parent.empty()) return false;
  // enterDirectory()がcurrentDirectory_を上書きする「前に」取得する
  // ――openSelectedListEntry()自身の同一のRecentDirectoryの分岐の
  // コメント参照。セッション全体がファイル上で直接始まった場合
  // (`usilic somefile.txt`、Directoryセッションが一度もアクティブに
  // なっていない)は、本当に空である(単に未設定というだけではない)
  // ――その場合resetNavigationToFloor()は呼ばれない、これは、戻る
  // べき本物の「前のディレクトリ」が「存在する」という、他の全ての
  // 床を確立する呼び出し箇所自身の暗黙の想定に合わせている。
  std::string previousDirectory = directoryMode_.currentDirectory();
  if (!directoryMode_.enterDirectory(parent)) return false;
  if (!previousDirectory.empty()) directoryMode_.resetNavigationToFloor(previousDirectory);
  currentMode_ = ApplicationMode::Directory;
  display_.hideVisibleCursor();
  directoryView_.redraw();
  return true;
}

// K-19(^O^K、GoToParentDirectory)をDirectoryモードの外へも拡張
// ――2026-08-20、直接のユーザー報告: "新：^O^KはFILE、ARC、等で
// 無反応"(このキーは、「どこでも」動くことが期待されているが、
// Directoryモードの外では何もしなかった)。legacy自身のm_dir()
// (項目101自身のコメント)は本当にDirectory専用なので、これはこの
// ポートについての意図的な「一般化」であり、legacyへの忠実性の修正
// ではない――goToFileSelector()自身の既に確立された「アクティブな
// どの外出先モードからパスを計算し、そこでenterDirectory()し、
// currentMode_を切り替える」という形(K-18/^J^K、2026-08-16の同じ
// 種類の報告について同じ方法で拡張された)に直接倣っている。^J^Kが
// ファイル/アーカイブ自身の格納先ディレクトリの「中に」あなたを
// 着地させるのに対し、これは「もう1段階先」――そのディレクトリの
// 「親」――へ行く、Directoryモード自身のgoToParent()の意味論
// (currentDirectory_ -> parent)を正確に反映しているが、既にアクティブ
// なDirectory一覧からではなく、「現在のファイル/アーカイブが現れる
// であろうディレクトリ」から計算されるだけである。
//
// 2026-09-20追加、直接のユーザー依頼("^O^KをHISTORY.md:9768と同じ
// 形で反映して"): 9768番の依頼はJumpToPathPrefix(^J+数字)を「単なる
// 文字列上も含めたまさにどこでも」(Locate/ListBrowser/Du含む、その
// jumpToPathPrefix()自身のコメント参照)へ一般化したものであり、今回
// ^O^Kにも同じ広さを反映する――具体例("^X^D^N^N^N^N^O^K"、ListBrowser
// のRecentDirectory候補からの発動)により、単に対応モードを追加する
// だけでは不十分だと判明した: File/Archiveは常に「ファイル」パスな
// ので`parent_path()`を2回(格納先ディレクトリ、さらにその親)適用して
// いたが、ListBrowserのRecentDirectory候補やDuのディレクトリエントリ
// は既にそれ自身が「ディレクトリ」であり、2回適用すると1段階余分に
// 遡ってしまう(具体例で言えば期待される"Resources/"ではなく
// "Contents/"に着地してしまう)。そこでduEntryFolder()と全く同じ考え方
// ――「格納先ディレクトリ(ファイルなら親、ディレクトリなら自分自身)」
// をまず求め、その「親」を取ることで、ファイル/ディレクトリどちらの
// ソースでも统一的に「もう1段階先」を計算できるようにした。ソースが
// ディレクトリかどうかの判定は、jumpToPathPrefix()と同様モードごとに
// 最も自然な方法を使う――ListBrowserはRingCategory(RecentDirectoryの
// 候補は末尾"/"付きの本物のディレクトリパスであることがhistory.cpp
// 自身のpushRecent()呼び出し箇所で保証されている)、Locateは
// runDuOnCurrentFolder()自身のLocate分岐と同じくstd::filesystem::
// is_directory()の実stat、DuはDuEntry::isDirectory(既にdu_mode.cpp
// 自身がstat済み)。
bool Application::goToParentFromFileOrArchive() {
  // 2026-09-14追加、goToFileSelector()自身の同一の2026-09-14修正の
  // コメント参照(直接のユーザー報告"HELP画面で^J^J/^J^K/^O^K/（どこ
  // でもキー）が利かない"): File起点以外のCut/CutContent(HELP/MAN/
  // LOCATE/DU/Directory起点)には、下のfileMode_.currentFile()という
  // 前提がそもそも成り立たない。Cut/CutContentへ入る前のdirectoryMode_
  // 自身の状態はここでも無傷のまま残っているので、素のDirectoryモード
  // 自身が^O^Kに使うdirectoryMode_.goToParent()(すぐ上のhandleCommand()
  // 自身のcase Command::GoToParentDirectory参照)へ直接フォールバック
  // する――こちらは「その場を再表示する」だけのgoToFileSelector()側の
  // フォールバックとは違い、"もう1段階先"というこの関数自身の意味論を
  // 正しく保つ。
  if ((currentMode_ == ApplicationMode::Cut || currentMode_ == ApplicationMode::CutContent) &&
      cutOrigin_ != CutOrigin::File) {
    if (!directoryMode_.goToParent()) return false;
    currentMode_ = ApplicationMode::Directory;
    display_.hideVisibleCursor();
    directoryView_.redraw();
    return true;
  }
  std::string sourcePath;
  bool sourceIsDirectory = false;
  if (currentMode_ == ApplicationMode::File || currentMode_ == ApplicationMode::Cut ||
      currentMode_ == ApplicationMode::CutContent) {
    // jumpToPathPrefix()自身のFile/Cut/CutContent分岐と同じ理屈
    // ――CUTはFileモードのバッファ亜種であり、fileMode_自身の状態には
    // 触れない。
    sourcePath = fileMode_.currentFile();
  } else if (currentMode_ == ApplicationMode::Archive || currentMode_ == ApplicationMode::ArchiveContent) {
    sourcePath = archiveMode_.currentArchive();
  } else if (currentMode_ == ApplicationMode::ListBrowser) {
    const std::string *selected = listBrowserMode_.selectedItem();
    if (!selected) return false;
    sourcePath = *selected;
    sourceIsDirectory = (listBrowserCategory_ == RingCategory::RecentDirectory);
  } else if (currentMode_ == ApplicationMode::Locate) {
    const std::string *selected = locateMode_.selectedResult();
    if (!selected) return false;
    sourcePath = *selected;
    std::error_code ec;
    sourceIsDirectory = std::filesystem::is_directory(sourcePath, ec) && !ec;
  } else if (currentMode_ == ApplicationMode::Du) {
    const DuEntry *selected = duMode_.selectedEntry();
    if (!selected) return false;
    sourcePath = selected->path;
    sourceIsDirectory = selected->isDirectory;
  } else {
    return false;
  }
  if (sourcePath.empty()) return false;
  // 2026-09-20修正、実際にテストを流して発覚: sourcePathが末尾"/"付きの
  // ディレクトリの場合、std::filesystem::pathはそれを「末尾に空の
  // コンポーネントが1つある」パスとして扱うため、parent_path()を
  // 1回呼んでも末尾のスラッシュが取れるだけで、最後の本物のディレクトリ
  // 名(例: "Resources")自体は消えない――"/a/b/c/"のparent_path()は
  // "/a/b"ではなく"/a/b/c"になる、というstd::filesystem::path自身の
  // 既知の挙動。そのため、ディレクトリの場合はまず末尾のスラッシュを
  // 文字列として素通しで取り除いてから(directory_mode.cpp自身の同種の
  // 正規化と同じ手法)parent_path()を1回だけ呼ぶ――ファイルの場合は
  // 元々末尾スラッシュを持たないのでこの問題自体が起こらない。
  std::string normalizedSource = sourcePath;
  if (sourceIsDirectory && normalizedSource.size() > 1 && normalizedSource.back() == '/') {
    normalizedSource.pop_back();
  }
  std::filesystem::path containing = sourceIsDirectory ? std::filesystem::path(normalizedSource)
                                                        : std::filesystem::path(sourcePath).parent_path();
  std::string parent = containing.parent_path().string();
  if (parent.empty()) return false;
  // goToFileSelector()自身の同一のコメント参照: enterDirectory()が
  // currentDirectory_を上書きする「前に」取得する。
  std::string previousDirectory = directoryMode_.currentDirectory();
  if (!directoryMode_.enterDirectory(parent)) return false;
  if (!previousDirectory.empty()) directoryMode_.resetNavigationToFloor(previousDirectory);
  currentMode_ = ApplicationMode::Directory;
  display_.hideVisibleCursor();
  directoryView_.redraw();
  return true;
}

// Command::JumpToPathPrefix(^J0..^J9): ユーザー依頼とlegacyでの検証
// の文脈についてはその列挙値自身のコメント参照。goToFileSelector()/
// goToParentFromFileOrArchive()自身の「アクティブなどの外出先モード
// からパスを計算し、それを切り詰め/導出し、そこでenterDirectory()
// し、currentMode_を切り替える」という形に直接倣っている――
// (K-18/K-19と)同じlegacyを超える一般化の前例、ただし
// parent_path()/parent_path()の代わりにtruncateToPathPrefix()を
// 使っているだけ。
//
// (ユーザー自身の"無条件に(戻り値も不要)"という枠組みに合わせて)
// 「プレビューして、^Gで復元する」というスタイルの機能ではなく、
// 意図的にハードで無条件のDirectoryモードへの切り替えである: ^J+
// 数字が押された時にアクティブだったプロンプト/リストブラウザ/
// モードの状態を検出したり保持したりしようとする試みは無い――例えば
// 無関係なGREPパターンの編集の途中でこれを押すと、プレーンな
// Directory一覧から押した時とまさに同じくらい唐突にその編集を放棄
// する。これは、feedPromptByte()からdispatchLeaderByte()経由で解決
// された後、他の「全ての」リーダーで開始されるコマンドが既にそう
// 振る舞うのを反映している(その関数自身の2026-08-12のコメント参照)
// ――^Qのクォート挿入は、本当にリテラルな"^J5"をパターンにタイプ
// したい人のための逃げ道として残っている。
//
// モードごとのパスの源、goToFileSelector()自身のcurrentMode_の分岐
// (ListBrowser/Locate/File/Cut/CutContent)に加え
// goToParentFromFileOrArchive()自身のArchive/ArchiveContentの分岐に
// 合わせつつ、Directory自体(legacy自身の、そしてこのポート唯一の
// 「元々の」m_dir()のケース)で拡張してある――ユーザー自身の報告で
// 直接名指しされた3つの文脈全て(DIR自身のnameフィールド、
// currentArchive()を示すARC自身のtrailingの「戻り値」フィールド、
// ^X^F/^X^D自身のハイライトされている候補文字列)に加え、K-18/K-19
// について既に同じ方法で結線されている他の全ての「外出先」画面を
// カバーし、"どこでも"に合わせている。
bool Application::jumpToPathPrefix() {
  clearListReturnAnnotations();
  std::string sourcePath;
  if (currentMode_ == ApplicationMode::Directory) {
    sourcePath = directoryMode_.currentDirectory();
  } else if (currentMode_ == ApplicationMode::Archive || currentMode_ == ApplicationMode::ArchiveContent) {
    sourcePath = archiveMode_.currentArchive();
  } else if (currentMode_ == ApplicationMode::File) {
    sourcePath = fileMode_.currentFile();
  } else if (currentMode_ == ApplicationMode::Cut || currentMode_ == ApplicationMode::CutContent) {
    // 2026-09-14追加、goToFileSelector()自身の同一の2026-09-14修正の
    // コメント参照(直接のユーザー報告"HELP画面で...（どこでもキー）
    // が利かない"): File起点のCUTだけがfileMode_.currentFile()を
    // 実際に持つ――それ以外の起点(HELP/MAN/LOCATE/DU/Directory)は、
    // すぐ上のDirectory自身の分岐と同じくdirectoryMode_.
    // currentDirectory()(Cut/CutContentへ入る前のまま無傷で残って
    // いる)にフォールバックする。
    sourcePath = (cutOrigin_ == CutOrigin::File) ? fileMode_.currentFile() : directoryMode_.currentDirectory();
  } else if (currentMode_ == ApplicationMode::Locate) {
    const std::string *selected = locateMode_.selectedResult();
    if (!selected) return false;
    sourcePath = *selected;
  } else if (currentMode_ == ApplicationMode::ListBrowser) {
    const std::string *selected = listBrowserMode_.selectedItem();
    if (!selected) return false;
    sourcePath = *selected;
  } else if (currentMode_ == ApplicationMode::Du) {
    // 2026-09-10追加、直接のユーザー依頼("^J6ではハイライト表示され
    // ているエントリのパスの(ls)となってほしい、DUの集計対象フォルダー
    // 自体は関係ない"): Locate/ListBrowser自身の分岐(すぐ上)と同じ
    // 「ハイライトされているエントリ自身のパス」の慣習――duMode_.
    // currentPath()(集計対象のフォルダー自体)ではなく、カーソルが
    // 今指している個々のエントリの実際のパスを使う。
    const DuEntry *selected = duMode_.selectedEntry();
    if (!selected) return false;
    sourcePath = selected->path;
  } else {
    return false;
  }
  if (sourcePath.empty()) return false;
  std::string truncated = truncateToPathPrefix(sourcePath, pendingPathPrefixDigit_);
  // goToFileSelector()自身の同一のコメント参照: enterDirectory()が
  // currentDirectory_を上書きする「前に」取得する。
  std::string previousDirectory = directoryMode_.currentDirectory();
  if (!directoryMode_.enterDirectory(truncated)) return false;
  if (!previousDirectory.empty()) directoryMode_.resetNavigationToFloor(previousDirectory);
  currentMode_ = ApplicationMode::Directory;
  display_.hideVisibleCursor();
  directoryView_.redraw();
  return true;
}

// m_list()のディスパッチから移植(ここで結線されていない他の5つの
// カテゴリについてはクラスコメント参照): RecentDirectory(K-48、
// ^X^D/^R)はDirectoryModeへ、RecentFile(K-46、^X^F)はFileModeへ
// 突き進む;他の全てのカテゴリ(このスライスではShellCommandのみ)は
// no-opである、なぜならシェルコマンドについての「開く」が何を意味
// するか(Shellのコマンドラインを事前に埋めるのか?Shellが既に開始
// されている必要があるのか?)を決めることは、当てずっぽうにする
// のではなく意図的に未着手のままにしてあるため。
bool Application::openSelectedListEntry() { return openSelectedListEntryImpl(); }

bool Application::openSelectedListEntryImpl() {
  const std::string *item = listBrowserMode_.selectedItem();
  if (!item) return false;
  // 2026-09-18追加、HELP.txt 2.13章「LIST」の実装――他の全ての分岐
  // (promptHistoryContext_等)より先にチェックする、returnFromTab
  // CompletionList()自身のコメント参照。
  if (tabCompletionContext_) return returnFromTabCompletionList(/*selected=*/true);
  if (promptHistoryContext_) {
    // まず「元の」下層のモードを復元し(プロンプトは、それらが始まった
    // 時にアクティブだったものの上に重なるものであり、対応する
    // execute*()関数は――例えばexecuteGrep()自身のgrepReturnMode_の
    // 読み取り参照――それがライブでタイプされたsubmitPrompt()呼び出し
    // についてまさにそうであるように、それが依然としてcurrentである
    // と想定する)、それから選ばれたテキストを、ライブなEnterがそう
    // するのとまさに同じ経路でディスパッチする。
    PromptKind kind = promptHistoryContext_->kind;
    std::string text = *item;
    // 2026-09-18追加(フォローアップ8、直接のユーザー報告「SHELLでの
    // ^R（COMMAND）はまだNG」): ShellCommand自身のここでの唯一の
    // 到達経路は、Tab補完編集中に押された本物の^R COMMAND履歴呼び戻し
    // (startPromptHistoryRecall()自身の新設ケース参照、Tab補完自体は
    // 別のtabCompletionContext_経路でこの関数の最初の分岐が既に処理
    // 済み)――既存のshellHistoryRecallActive_(プレーンなSHELLタイピング
    // 中の^R)自身の確立済みの設計判断("過去のコマンドは破壊的
    // (rm/mv等)であり得るため、選択しても自動実行しない、挿入のみ")
    // をそのまま踏襲する。executePromptSubmission()の共通経路
    // (GrepPattern/CutPattern/RecentFile/RecentDirectory等は選択即
    // 実行が正しい)には乗せない。
    if (kind == PromptKind::ShellCommand) {
      promptHistoryContext_ = std::nullopt;
      currentMode_ = ApplicationMode::Shell;
      shell_.commandLine().setText(text);
      shell_.scrollback().redraw();
      shell_.redrawPromptRow();
      return true;
    }
    // pendingSearchFromDirectEntryList_自身のコメント参照。
    pendingSearchFromDirectEntryList_ = promptHistoryContext_->isDirectEntry;
    bool fromSortedManList =
        kind == PromptKind::ManName && promptHistoryContext_->isDirectEntry && !listBrowserChronological_;
    currentMode_ = promptHistoryContext_->returnMode;
    promptHistoryContext_ = std::nullopt;
    bool fromDirectEntry = pendingSearchFromDirectEntryList_;
    std::string originDirectory = directoryMode_.currentDirectory();
    bool fromDirectoryMode = currentMode_ == ApplicationMode::Directory;
    bool ok = executePromptSubmission(kind, text);
    if (ok && kind == PromptKind::RecentFile && fromDirectEntry && fromDirectoryMode &&
        currentMode_ == ApplicationMode::File) {
      // 2026-09-21、直接のユーザー判断(選択肢B): FILE一覧(^X^F)から選んだファイルも、
      // DIR/MANと同じく戻り値を"FILE"にし、^Gで元のディレクトリと一覧(カーソルは選んだ
      // 項目)へ戻れるようにする。従来(legacy準拠)は^Gでファイルの親ディレクトリへ着地していた。
      fileListOriginDirectory_ = originDirectory;
      fileListSelectedItem_ = text;
      fileMode_.annotations().pushWithNameOverride(FileMode::FileAnnotationKind::Search, fileMode_.currentFile(),
                                                    "FILE");
      fileReturnsToFileList_ = true;
      fileView_.redraw();
    }
    if (ok && kind == PromptKind::RecentDirectory && fromDirectEntry && currentMode_ == ApplicationMode::Directory) {
      // 2026-09-21追加、直接のユーザー依頼(一覧ありモードの仕様統一): DIR一覧
      // (^X^D)から選んだディレクトリでは、^Gで一覧へ戻れるよう注釈を積む
      // (ディレクトリ移動でクリア)。
      dirListOriginDirectory_ = originDirectory;
      dirListSelectedItem_ = text;
      directoryMode_.primaryAnnotations().push(DirectoryMode::DirAnnotationKind::Dir, "DIR");
      // 2026-09-21修正: enterDirectory()は注釈を積む前に再描画済みなので、
      // ここで再描画しないと、モードラインに"DIR"が出ない(実機PTYで確認)。
      directoryView_.redraw();
    }
    if (kind == PromptKind::ManName) {
      pendingSearchFromDirectEntryList_ = false;  // Man自身はこのフラグを消費しない
      manReturnsToCandidateList_ = ok && fromSortedManList && manReturnStack_.size() == 1;
      if (manReturnsToCandidateList_) manListSelectedItem_ = text;
    }
    return ok;
  }
  if (shellHistoryRecallActive_) {
    // 挿入だけ――submitLine()はしない。なぜこれが意図的にMAN/LOCATE/
    // FGREPのEnterで自動実行するのと異なるのかについては
    // startShellHistoryRecall()自身のコメント参照。
    shellHistoryRecallActive_ = false;
    std::string text = *item;
    currentMode_ = ApplicationMode::Shell;
    shell_.commandLine().setText(text);
    shell_.scrollback().redraw();
    shell_.redrawPromptRow();
    return true;
  }
  if (listBrowserCategory_ == RingCategory::RecentDirectory) {
    // 2026-08-13: resetNavigationToFloor()――それ自身の宣言コメント
    // (directory_mode.hpp)参照――enterDirectory()がcurrentDirectory_
    // を上書きする「前に」取得するので、これは本当に「これを選ぶ前に
    // いた場所」であり、新しく入ったディレクトリ自体ではない。
    std::string previousDirectory = directoryMode_.currentDirectory();
    if (!directoryMode_.enterDirectory(*item)) return false;
    directoryMode_.resetNavigationToFloor(previousDirectory);
    currentMode_ = ApplicationMode::Directory;
    return true;
  }
  if (listBrowserCategory_ == RingCategory::RecentFile) return openRecentFileEntry(*item);
  return false;
}

// openSelectedListEntry()自身のRecentFileの分岐から抽出
// (2026-08-27、goToRecentFile()/Command::GoToRecentFileとも共有する
// ため――それ自身の宣言コメント参照)。2026-08-20修正、実際の報告+
// スクリーンショット(=dq): ^X^F自身の候補リストからアーカイブ
// (utf8.tar)を直接開くと、常にFileModeの生バイトビュー(「(tar)」
// バッジが欠け、tarファイル自身のバイナリヘッダバイトをプレーン
// テキストであるかのように表示する)へ流れ落ちていた
// ――openSelectedEntry()(Directoryモード自身のEnterキー)とは異なり、
// これは以前は無条件にfileMode_.loadFile()を呼び、先に
// archiveMode_.loadArchive()を試すことは一度も無かった。
// openSelectedEntry()自身の「アーカイブを先に、ファイルをフォール
// バックとして」という順序を正確に反映している。
bool Application::openRecentFileEntry(const std::string &path) {
  // 2026-09-24追加、直接のユーザー報告("^J^Jでカーソル位置が保存
  // されない。^Vでスクロールしたのち^J^Jしてもスクロール前の位置に
  // なる"): openSelectedArchiveEntry()自身の2026-08-27修正・
  // executeArchiveExtract()自身の2026-08-27修正(すぐ下)と全く同じ
  // 抜け――離れる前のFile/Archive自身のカーソル位置を保存しておか
  // ないと、loadFile()/loadArchive()側のhistory_.positionFor()問い
  // 合わせは「前回この関数を通って離れた時点」の古い位置しか知らず、
  // その間にPageDown等でcursorIndex_が進んでいても反映されない。
  // goToRecentFile()(^J^J)だけでなく、このopenRecentFileEntry()自体
  // を共有する全ての呼び出し元(^X^F自身のListBrowser選択・プロンプト
  // 直接入力・TUTORIAL画面等)に対して、ここ1箇所で一元的に直す
  // ――どの経路でも「今File/Archiveにいるなら、離れる前に保存する」
  // という条件は同じであるため。
  if (currentMode_ == ApplicationMode::File) {
    fileMode_.saveCursorPosition();
  } else if (currentMode_ == ApplicationMode::Archive || currentMode_ == ApplicationMode::ArchiveContent) {
    archiveMode_.saveCursorPosition();
  } else if (currentMode_ == ApplicationMode::Cut) {
    saveCutFilePosition();
  } else if (currentMode_ == ApplicationMode::CutContent) {
    saveCutFilePosition(cutContentView_.cursorIndex());
  }
  // 2026-09-20追加、直接のユーザー依頼(QUICK.txt自身のC-j bの説明
  // "後述[FILE]によるアクセス履歴一覧では「ファイル名 "文字列"」で
  // 表示され、選択すると文字列行の目次一覧画面となる"): executeCut()
  // 自身のfileSourcePath分岐が積む「パス "パターン"」という合成形の
  // 場合は、プレーンなファイル閲覧ではなくCUT-by-fileそのものを
  // 再実行する。2026-09-14拡張、parseCutFileHistoryEntry()自身の
  // コメント参照: 実行時の構文(Fixed/Regex)は区切り文字(引用符/
  // スラッシュ)から復元する。
  if (auto composite = parseCutFileHistoryEntry(path)) {
    const auto &[filePath, pattern, syntax] = *composite;
    // 2026-09-14追加、直接のユーザー依頼("usilic内での発動は^Tで
    // 行う"、^T画面からTUTORIALを選ぶ経路の追加で発覚): startCutFilePrompt()
    // 自身の同一のcutReturnMode_の代入(そちら自身のコメント参照)と
    // 同じ理屈――ここへ到達し得る全ての経路(FILE履歴のListBrowser、
    // ^X^F自身のライブコンボボックス、そしてこの新設の^T画面)のうち、
    // Directory以外から呼ばれた場合は、明示的にcutReturnMode_を発動元
    // へ揃えないと、^Gが(デフォルトのDirectoryへではなく)無関係な
    // 前回のCUTセッションが残した古いcutReturnMode_へ戻ってしまう。
    //
    // 2026-09-29修正、直接のユーザー報告("docs/HISTORY.mdを"^<"付きで
    // 開いた場合、戻り値がHISTORY.mdになっているところを見ると、^Cf
    // ではなく^Cbと見なされているんじゃない？"): 以前はDirectoryから
    // 呼ばれた場合にcutReturnMode_へ一切触れず、直前の^Cb(FILEから)が
    // 残したFileのままになり得た――^GがCUTしたファイル自身のFILE画面へ
    // 着地していた。cutFileReturnModeFor()で常に明示的に揃える。
    cutReturnMode_ = cutFileReturnModeFor(currentMode_);
    if (!fileMode_.loadFile(filePath)) return false;
    std::vector<StyledLine> sourceLines;
    sourceLines.reserve(fileView_.lines().size());
    for (const auto &line : fileView_.lines()) sourceLines.push_back({line.text, line.attributes});
    cutOrigin_ = CutOrigin::File;
    cutSyntax_ = syntax;
    if (!cutMode_.split(sourceLines, pattern, cutSyntax_)) return false;
    // 2026-09-29修正、直接のユーザー報告("参照後、(ls)へ戻った後^X^F^Rの直近に
    // "^<"が無い"): loadFile()がパス単体を積んで合成エントリより新しくして
    // しまうので、executeCut()のファイルの分岐と同じく、その後で積み直す。
    history_.pushRecent(RingCategory::RecentFile, path);
    restoreCutFilePosition(path);
    currentMode_ = ApplicationMode::Cut;
    // 2026-09-29追加、直接のユーザー依頼("^J^Jでも区間まで開くようにして"):
    // 区間の中身を見たまま離れていた(saveCutPositionIfLeft()/終了時の
    // saveResumableCutState()が行+1を入れた)なら、その区間を開いてその行へ。
    // 2026-10-01、直接のユーザー依頼("CUTモードの動作をEPUBに合わせてほしい。一覧ではなく
    // 以前見ていたページへ戻る。最初はリストの最上行をクリックした状態"): ^G^Gで一覧から
    // 離れていても、最後に見ていた区間を開く(cutLastContent_)。一度も中身を見ていなければ
    // 一覧(最初はカーソルが最上行)。
    reopenLastCutContent();
    return true;
  }
  if (path.size() > 1 && path.back() == '/' && isEpubFile(path.substr(0, path.size() - 1)))
    return openEpubAsArchive(path.substr(0, path.size() - 1));  // ARCで開いた.epub(openEpubAsArchive()参照)
  if (isEpubFile(path) && !configuration_.isExec(path)) return openEpub(path);  // openEpub()自身のコメント参照
  if (archiveMode_.loadArchive(path)) {
    archiveParentStack_.clear();
    currentMode_ = ApplicationMode::Archive;
    return true;
  }
  if (openExternallyIfApplicable(path)) return true;  // EXEC/画像のクイックルック(2026-09-27)
  if (!fileMode_.loadFile(path)) return false;
  currentMode_ = ApplicationMode::File;
  fileReturnMode_ = ApplicationMode::Directory;  // それ自身の宣言コメント参照
  return true;
}

// ^J^J: Command::GoToRecentFile自身の宣言コメント参照。
// goToFileSelector()(^J^K)とは異なり、これはモードごとの分岐を全く
// 必要としない――History::recentEntries(RingCategory::RecentFile)
// (^X^F自身の裏付けとなるストア)を直接読むので、goToFileSelector()
// が特別扱いする一握りのものだけでなく、文字通りどのモード/画面から
// でも意味を成す。ringが空(このセッションでEnter/^X^F経由で何も
// 開かれたことが無い)、あるいは最も最近のエントリがもう開かない
// (それ以来削除/移動された)場合はfalse。
bool Application::goToRecentFile() {
  const std::deque<std::string> &recent = history_.recentEntries(RingCategory::RecentFile);
  if (recent.empty()) return false;
  // 2026-08-27追加、直接のユーザー依頼("^G同様、今と直近を行き来
  // して欲しい"): goToRecentDirectory()(すぐ下の^G自身の空スタック
  // のフォールバック)は、既に「私が既にいる場所」と等しいエントリを
  // スキップしながらそのringを後方へたどり、本当に異なる最初のもの
  // に着地している――これは一度もそうしていなかった、そのため
  // 「既に」ring自身の最も最近のエントリを見ている間に^J^Jを押すと
  // (何かを開くたびにring自身の末尾にそれが再び積まれるので、^J^Jを
  // 一度使った直後の普通のケース)、^G自身の鏡像のような挙動とは
  // 異なり、それより前に開かれていたものへ戻る代わりに、単に「同じ」
  // ファイル/アーカイブを再び開くだけだった。File/Archive/
  // ArchiveContentの外(比較対象となる「現在開いている」ものが無い)
  // ではこれは変わらない――常にring自身の最後のエントリ。
  std::string current;
  if (currentMode_ == ApplicationMode::Archive || currentMode_ == ApplicationMode::ArchiveContent) {
    current = archiveMode_.currentArchive();
    if (isEpubFile(current)) current += "/";  // ARCで開いた.epubの履歴の形(openEpubAsArchive()参照)
  }
  else if (currentMode_ == ApplicationMode::File)
    current = fileMode_.currentFile();
  // 現在と同じものだけでなく、「失敗した」エントリも越えてたどり続け
  // る(goToRecentDirectory()自身の同一の2026-08-27の修正参照)。
  for (auto it = recent.rbegin(); it != recent.rend(); ++it) {
    if (!current.empty() && *it == current) continue;
    if (openRecentFileEntry(*it)) return true;
  }
  return false;
}

// Command::Back自身の空のModeStackのフォールバック(2026-08-27追加、
// 直接のユーザー提案)――それ自身の呼び出し箇所のコメント参照。
// RingCategory::RecentDirectory(^X^D自身の裏付けとなるストア)を
// 後方へたどり、「現在の」ディレクトリでは「ない」最も最近のエントリ
// を探す(ring自身の最後のエントリは常にenterDirectory()がたった今
// 着地した場所、つまりここであり――それを選ぶことは自己言及的な
// no-opになってしまう)。descendTo()ではなく意図的にenterDirectory()
// を使う: ModeStackのフレームは積まれないので、スタックは「空のまま」
// であり、この同じフォールバックが「次の」空スタックのBack押下でも
// 再び発火する――ディレクトリに入ることは常にそれをRecentDirectory
// へ再び積む(enterDirectory()自身のhistory_.pushRecent()呼び出し
// 参照)ので、この方法で訪れた2つのディレクトリは自然にring自身の
// 末尾で場所を交換し続ける、これがここでの「以後はループ」の意味
// である: ring自身の構造から自然に生まれるトグルであり、別個の
// たどり位置の状態は不要。
bool Application::goToRecentDirectory() {
  const std::deque<std::string> &recent = history_.recentEntries(RingCategory::RecentDirectory);
  // 2026-08-27修正、実際の報告("^Gだが無反応な時がある。状況は
  // 覚えてないがDIR履歴が複数あるのは確認"): これは以前は、
  // enterDirectory()がそれについて実際に成功したかどうかに関わらず、
  // currentDirectory()と異なる「まさに最初の」エントリでreturnして
  // いた――古いエントリ(それ以来削除されたディレクトリ、例えば
  // それ以来片付けられた古いARC展開の一時ディレクトリ/arc.Nの
  // スロット)は、たとえより古い、依然として有効な、本当に異なる
  // エントリがringの中でそのすぐ後ろに座っていても、これを行き止まり
  // ――「無反応」――にしていた。すぐ上のgoToRecentFile()自身の同一の
  // たどりに適用したのと同じ修正で、今は現在と同じものだけでなく、
  // 失敗した試みも越えてたどり続ける。
  for (auto it = recent.rbegin(); it != recent.rend(); ++it) {
    if (*it == directoryMode_.currentDirectory()) continue;
    if (directoryMode_.enterDirectory(*it)) return true;
  }
  return false;
}

// M_mark(^Space)から移植: カーソルの位置でマークをトグルする。
// このスライスに追加された他の全てのCP/MV/RM関連のアクションと同様
// Directory専用。
// K-57/K-58自身の「展開する特定のエントリをマークする」
// (HELP.jp 2.11)は、Archiveモード内でこの同じ^Spaceの割り当てを
// 再利用する――legacyの実際のモードごとのマーキングの仕組みを1つの
// 共有された^Space/ToggleMarkコマンドへ単純化するという、このポート
// の確立された選択(その選択についてはDirectoryMode自身のクラス
// コメント参照)に合わせている。
bool Application::toggleMark() {
  if (currentMode_ == ApplicationMode::Archive) {
    if (!archiveMode_.selectedEntry()) return false;
    archiveMode_.toggleMark(archiveView_.cursorIndex());
    archiveView_.redraw();  // 今、マーク自身のStandoutを塗る――ContentView::setMarkedPredicate()参照
    return true;
  }
  if (currentMode_ == ApplicationMode::Underline) {
    // UnderlineMode::toggleMark()自身の宣言コメント参照(2026-09-25)。
    // USILIC履歴の目次("MAN"/"DIR"/"FILE"の見出し)は選択の対象外。
    if (usilicLevel_ == UsilicLevel::Toc || !underlineMode_.selectedEntry()) return false;
    underlineMode_.toggleMark(underlineView_.cursorIndex());
    underlineView_.redraw();
    return true;
  }
  // LOCATE/DU/^X^F・^X^D(2026-09-27、list_marks.hpp参照)。
  {
    int size = 0;
    if (ListMarks *marks = activeListMarks(&size)) {
      ContentView &view = activeView();
      int cursor = view.cursorIndex();
      if (cursor < 0 || cursor >= size) return false;
      marks->toggle(cursor, size);
      view.redraw();
      return true;
    }
  }
  if (currentMode_ != ApplicationMode::Directory) return false;
  const DirectoryEntry *selected = directoryMode_.selectedEntry();
  if (!selected) return false;
  // 2026-09-25追加、直接のユーザー要望("(ls)で`.[]`による項目選択時、
  // ディレクトリを複数しようとするとビープ音とエラーメッセージ
  // (「複数は選択不可」の簡潔英文)を出力。表示場所はGREPの
  // 「No String」...と同じ位置にでも"): 既に別のディレクトリが選択済み
  // の状態で、未選択のディレクトリを新たに選択しようとした場合は拒否
  // する(選択の解除はいつでもできる)。メッセージはGREPの"No String"と
  // 同じ位置欄(setPositionTextOverride()、非ハイライト)に出し、次の
  // コマンドでhandleCommand()の冒頭が消す。
  int cursor = directoryView_.cursorIndex();
  if (selected->isDirectory && !directoryMode_.isMarked(cursor)) {
    const auto &entries = directoryMode_.entries();
    for (int i = 0; i < static_cast<int>(entries.size()); i++) {
      if (i == cursor || !entries[i].isDirectory || !directoryMode_.isMarked(i)) continue;
      showTransientNotice(directoryView_, "Cannot select multiple directories");
      display_.putChar(0x07);
      display_.flush();
      return false;
    }
  }
  directoryMode_.toggleMark(cursor);
  directoryView_.redraw();
  return true;
}

std::vector<std::string> Application::collectTargetPaths() const {
  if (cpMvRmOrigin_ != ApplicationMode::Directory) return cpMvRmListTargets_;  // 2026-09-27、それ自身の宣言コメント参照
  std::vector<std::string> paths;
  std::string dir = directoryMode_.currentDirectory();
  for (const auto &entry : directoryMode_.targetEntries()) {
    std::string path = dir;
    if (!path.empty() && path.back() != '/') path += '/';
    path += entry.name;
    // `ls -F`自身のディレクトリマーカーがentry.nameに残す末尾の
    // スラッシュを取り除く――2026-08-21修正、実際の報告+
    // スクリーンショット(=eh): cp/rsync系のツールは、再帰コピーの
    // 「ソース」として与えられた時、"dir/"を「dirそれ自体」ではなく
    // 「dirの中身」として扱うので、マークされたディレクトリのコンテ
    // ンツが、正しい"dest/dirname/"というコピーを作る代わりに、宛先
    // へ直接ダンプされてしまっていた。cp -vの自身の詳細出力は、
    // まさに同じ理由(それ自身の再帰が、与えられたトリムされていない
    // "dirname/"というソース引数に"/nested/path"を追加する)で、
    // リテラルな"dirname//nested/path"という二重スラッシュのアーチ
    // ファクトを示していた――これはこれの直接の症状であり、別の
    // バグではない。サイズ1でガードしてあるので、これは既に素の"/"
    // を空文字列に縮めることは決して無い(ここでは実際には起こり
    // 得ないケースだが――エントリが文字通り"/"であることは決して
    // 無い――安価で明らかに正しいガードである)。
    while (path.size() > 1 && path.back() == '/') path.pop_back();
    paths.push_back(std::move(path));
  }
  return paths;
}

std::vector<std::string> Application::collectTargetFilePaths() const {
  std::vector<std::string> paths;
  std::string dir = directoryMode_.currentDirectory();
  for (const auto &entry : directoryMode_.targetEntries()) {
    // 2026-09-21修正、直接のユーザー指摘(`^Ou`で.[]で選んだ
    // ディレクトリが一覧に出ない): DU/LOCATE等の他の一覧モードと
    // 同様、ディレクトリも一覧へ含める(選ぶと、DUと同じ形で
    // そのディレクトリへ移動し、^Gで一覧へ戻る)。
    std::string path = dir;
    if (!path.empty() && path.back() != '/') path += '/';
    path += entry.name;
    while (path.size() > 1 && path.back() == '/') path.pop_back();
    paths.push_back(std::move(path));
  }
  return paths;
}

// ^[uの目次の見出し(2026-09-27、PROGRAM/WORKを追加: 直接のユーザー依頼
// "^J^P一覧をUSILICのMAN/FILE/DIR一覧にPROGRAMで加えてほしい"、"WORKも
// そうしたい（^J^Ow）ので^[u（複数ファイル可）で対応")。usilicGroupKind_は
// この並びの添字。
static const std::vector<std::string> &usilicTocTitles() {
  static const std::vector<std::string> titles = {"MAN", "DIR", "FILE", "PROGRAM", "WORK"};
  return titles;
}
static constexpr int kUsilicGroupProgram = 3;
static constexpr int kUsilicGroupWork = 4;

// activeListMarks()/listTargetPaths()(2026-09-27): application.hpp自身の
// 宣言コメント参照。^X^F/^X^Dの一覧(ListBrowser)はファイル/ディレクトリの
// 履歴の時だけ(PROGRAM/MAN等の一覧はファイルを指さない)、USILICは目次と
// PROGRAMの項目以外。
ListMarks *Application::activeListMarks(int *size) {
  switch (currentMode_) {
    case ApplicationMode::Locate:
      *size = static_cast<int>(locateMode_.results().size());
      return &locateMode_.marks();
    case ApplicationMode::Du:
      *size = static_cast<int>(duMode_.entries().size());
      return &duMode_.marks();
    case ApplicationMode::ListBrowser:
      if (listBrowserCategory_ != RingCategory::RecentFile && listBrowserCategory_ != RingCategory::RecentDirectory)
        return nullptr;
      *size = static_cast<int>(listBrowserMode_.items().size());
      return &listBrowserMode_.marks();
    case ApplicationMode::Underline:
      if (usilicLevel_ == UsilicLevel::Toc ||
          (usilicLevel_ == UsilicLevel::Group && usilicGroupKind_ == kUsilicGroupProgram))
        return nullptr;
      *size = static_cast<int>(underlineMode_.entries().size());
      return &underlineMode_.marks();
    default:
      return nullptr;
  }
}

std::vector<std::string> Application::listTargetPaths() const {
  auto collect = [](const ListMarks &marks, int cursor, int size, const auto &pathAt) {
    std::vector<std::string> raw;
    if (marks.count() > 0) {
      for (int i : marks.indices())
        if (i < size) raw.push_back(pathAt(i));
    } else if (cursor >= 0 && cursor < size) {
      raw.push_back(pathAt(cursor));
    }
    return raw;
  };
  std::vector<std::string> raw;
  switch (currentMode_) {
    case ApplicationMode::Locate: {
      const auto &results = locateMode_.results();
      raw = collect(locateMode_.marks(), locateView_.cursorIndex(), static_cast<int>(results.size()),
                    [&](int i) { return results[i]; });
      break;
    }
    case ApplicationMode::Du: {
      const auto &entries = duMode_.entries();
      raw = collect(duMode_.marks(), duView_.cursorIndex(), static_cast<int>(entries.size()),
                    [&](int i) { return entries[i].path; });
      break;
    }
    case ApplicationMode::ListBrowser: {
      if (listBrowserCategory_ != RingCategory::RecentFile && listBrowserCategory_ != RingCategory::RecentDirectory)
        return {};
      const auto &items = listBrowserMode_.items();
      raw = collect(listBrowserMode_.marks(), listBrowserView_.cursorIndex(), static_cast<int>(items.size()),
                    [&](int i) {
                      // ^X^Fの「パス "パターン"」(CUTの履歴)はパスの部分。
                      if (auto parsed = parseCutFileHistoryEntry(items[i])) return std::get<0>(*parsed);
                      return items[i];
                    });
      break;
    }
    case ApplicationMode::Underline: {
      if (usilicLevel_ == UsilicLevel::Toc ||
          (usilicLevel_ == UsilicLevel::Group && usilicGroupKind_ == kUsilicGroupProgram))
        return {};
      raw = underlineMode_.targetEntries();
      break;
    }
    default:
      return {};
  }
  std::vector<std::string> paths;
  for (auto &path : raw) {
    while (path.size() > 1 && path.back() == '/') path.pop_back();
    std::error_code ec;
    if (!path.empty() && path.front() == '/' && std::filesystem::exists(path, ec)) paths.push_back(path);
  }
  return paths;
}

bool Application::startListCpMvRmPrompt(PromptKind kind) {
  int size = 0;
  ListMarks *marks = activeListMarks(&size);
  // (ls)と同じく、何も選択していなければ発動しない(startCopyPrompt()自身の
  // コメント参照)。
  if (!marks || marks->count() == 0) return false;
  std::vector<std::string> paths = listTargetPaths();
  if (paths.empty()) return false;
  if (switchToShellIfBusy()) return true;
  cpMvRmOrigin_ = currentMode_;
  cpMvRmListTargets_ = std::move(paths);
  ContentView &view = activeView();
  activatePrompt(kind, "", ModeLine::format(view.cursorIndex() + 1, size));
  promptCandidateListActive_ = false;  // startDestinationPrompt()自身のコメント参照
  return true;
}

bool Application::startUnderlineFileView() {
  if (currentMode_ != ApplicationMode::Directory) {
    // 一覧から(2026-09-27、underlineReturn_自身の宣言コメント参照): 選択
    // (無ければハイライト行)を閲覧の一覧にする。
    int size = 0;
    if (!activeListMarks(&size)) return false;
    std::vector<std::string> paths = listTargetPaths();
    if (paths.empty()) return false;
    UnderlineReturn back{currentMode_, {}, usilicLevel_, usilicGroupKind_, activeView().cursorIndex()};
    if (currentMode_ == ApplicationMode::Underline) back.entries = underlineMode_.entries();
    underlineReturn_ = std::move(back);
    usilicLevel_ = UsilicLevel::None;
    underlineMode_.show(std::move(paths));
    currentMode_ = ApplicationMode::Underline;
    return true;
  }
  underlineReturn_.reset();
  std::vector<std::string> paths = collectTargetFilePaths();
  if (paths.empty()) return false;
  underlineOriginDirectory_ = directoryMode_.currentDirectory();
  usilicLevel_ = UsilicLevel::None;
  underlineMode_.show(std::move(paths));
  currentMode_ = ApplicationMode::Underline;
  return true;
}

bool Application::startUsilicHistory(UsilicStyle style) {
  if (currentMode_ != ApplicationMode::Directory) return false;
  underlineReturn_.reset();
  underlineOriginDirectory_ = directoryMode_.currentDirectory();
  if (style == UsilicStyle::Toc) {
    // 2026-09-21、直接のユーザー仕様(`^[u`): MAN/DIR/FILEの3項目の
    // 目次(常時表示、履歴が空でも出る)。選ぶとその種別の一覧(項目名で
    // ソート・重複排除、行番号なし)へ進み、^Gは一覧→目次→Directoryの順に戻る。
    usilicLevel_ = UsilicLevel::Toc;
    usilicGroupKind_ = 0;
    underlineMode_.show(usilicTocTitles());
    currentMode_ = ApplicationMode::Underline;
    return true;
  }
  usilicLevel_ = UsilicLevel::None;
  const auto &ring = history_.recentEntries(RingCategory::Usilic);
  std::vector<std::string> entries(ring.begin(), ring.end());
  if (entries.empty()) return false;
  if (style == UsilicStyle::Sorted) {
    // `^J^U`: ベタなソート・重複排除一覧(MAN、DIR、FILEの区別なく項目名順)。
    std::sort(entries.begin(), entries.end());
    entries.erase(std::unique(entries.begin(), entries.end()), entries.end());
  }
  underlineMode_.show(std::move(entries), /*cursorAtEnd=*/style == UsilicStyle::Chronological);
  currentMode_ = ApplicationMode::Underline;
  return true;
}

// 履歴1行の種別: `/`で始まらなければMAN、`/`で始まり`/`で終わればDIR、それ以外はFILE。

static int usilicKindOf(const std::string &e) {
  if (e.empty() || e[0] != '/') return 0;
  return e.back() == '/' ? 1 : 2;
}

bool Application::openUsilicGroup(const std::string &title) {
  const auto &titles = usilicTocTitles();
  int kind = static_cast<int>(std::find(titles.begin(), titles.end(), title) - titles.begin());
  if (kind >= static_cast<int>(titles.size())) return false;
  std::vector<std::string> entries;
  if (kind == kUsilicGroupProgram) {
    entries = sortedUniqueRecentEntries(RingCategory::ShellCommand);  // ^J^Pの一覧と同じ
  } else if (kind == kUsilicGroupWork) {
    // 作成順(w.1、w.2...)のまま。まだ無ければ空の一覧。
    entries = allocatedWorkFiles_;
  } else {
    const auto &ring = history_.recentEntries(RingCategory::Usilic);
    for (const auto &e : ring)
      if (usilicKindOf(e) == kind) entries.push_back(e);
    std::sort(entries.begin(), entries.end());
    entries.erase(std::unique(entries.begin(), entries.end()), entries.end());
  }
  usilicLevel_ = UsilicLevel::Group;
  usilicGroupKind_ = kind;
  underlineMode_.show(std::move(entries));
  return true;
}

bool Application::openSelectedUnderlineEntry() {
  const std::string *path = underlineMode_.selectedEntry();
  if (!path) return false;
  if (usilicLevel_ == UsilicLevel::Toc) return openUsilicGroup(*path);
  // PROGRAMの項目: ^J^Pの一覧で選んだ時と同じく、PROGRAMプロンプトに
  // その文字列を入れる(実行は^Mで確定してから)。
  if (usilicLevel_ == UsilicLevel::Group && usilicGroupKind_ == kUsilicGroupProgram) {
    programPromptDirectory_.clear();
    activatePrompt(PromptKind::ProgramCommand, *path);
    return true;
  }
  // 2026-09-21追加: アクセス履歴の"名前 (man)"はmanページとして開き、^Gで
  // このUSILIC一覧へ戻る。
  if (path->size() > 6 && path->compare(path->size() - 6, 6, " (man)") == 0) {
    ApplicationMode anchor = nearestFileOrDirectoryAnchor();
    manReturnStack_ = {anchor};
    manAnchor_ = anchorSnapshotFor(anchor);
    if (!executeMan(path->substr(0, path->size() - 6))) return false;
    manReturnsToUnderline_ = true;
    return true;
  }
  // "パス \"文字列\""(CUT履歴)など、実在するパスでないものはFILE履歴と同じ経路で開く。
  {
    std::error_code existsEc;
    if (!std::filesystem::exists(*path, existsEc)) return openRecentFileEntry(*path);
  }
  // 2026-09-21追加: ディレクトリはopenSelectedDuEntry()自身の
  // ディレクトリ分岐と同型――移動先へ注釈(Usilic)を積み、^Gで
  // 一覧へ戻れるようにする。
  std::error_code ec;
  if (std::filesystem::is_directory(*path, ec)) {
    if (!directoryMode_.enterDirectory(*path)) return false;
    currentMode_ = ApplicationMode::Directory;
    directoryMode_.primaryAnnotations().push(DirectoryMode::DirAnnotationKind::Usilic, "USILIC");
    directoryView_.redraw();
    return true;
  }
  if (openExternallyIfApplicable(*path)) return true;  // EXEC/画像のクイックルック(2026-09-27)
  if (isEpubFile(*path)) return openEpubReturningTo(*path, ApplicationMode::Underline);
  if (!fileMode_.loadFile(*path)) return false;
  currentMode_ = ApplicationMode::File;
  fileReturnMode_ = ApplicationMode::Underline;
  // openSelectedDuEntry()/openSelectedLocateResult()自身のコメント
  // 参照――ただし2026-09-20のユーザー指摘により、name/detailの割り
  // 当ては両者の当時の慣習(name=モードラベル、detail=パス)とは逆に
  // する: nameには現在見ている中身そのもの(パス)を、detailには
  // ^Gの戻り先を示す短いラベル("UNDER")を割り当てる――stdin/ARC/
  // INFOの一部が既にこの並びを使っている(file_mode.cpp自身の
  // ModeLineFieldsProvider参照)。DU/LOCATE自身も同じ指摘を受けて
  // 同日中に修正済み(openSelectedDuEntry()/openSelectedLocateResult()
  // 自身のコメント参照)。
  fileMode_.annotations().pushWithNameOverride(FileMode::FileAnnotationKind::Search, *path, "USILIC");
  fileView_.redraw();
  return true;
}

// openSelectedUnderlineEntry()で開いたFile(fileReturnMode_ ==
// Underline)からのBack専用――returnToDuResults()と同型: 一覧自身の
// 内容は既にunderlineMode_へキャッシュ済みなので、再計算せず一覧へ
// 戻るだけでよい。一覧自身からのBack(Directoryへ戻る、すぐ下の
// returnFromUnderline())とは戻り先が異なるので、混同して1つの関数に
// まとめない。
bool Application::returnToUnderlineResults() {
  currentMode_ = ApplicationMode::Underline;
  underlineView_.redraw();
  return true;
}

// UNDER一覧「自体」からのBack専用――returnFromDu()と同型。
bool Application::returnFromUnderline() {
  if (usilicLevel_ == UsilicLevel::None && underlineReturn_) {  // 一覧からの^Ou(2026-09-27)
    UnderlineReturn back = std::move(*underlineReturn_);
    underlineReturn_.reset();
    currentMode_ = back.mode;
    if (back.mode == ApplicationMode::Underline) {
      usilicLevel_ = back.level;
      usilicGroupKind_ = back.groupKind;
      underlineMode_.show(std::move(back.entries), /*cursorAtEnd=*/false, back.cursor);
    } else {
      activeView().redraw();
    }
    return true;
  }
  if (usilicLevel_ == UsilicLevel::Group) {
    usilicLevel_ = UsilicLevel::Toc;
    underlineMode_.show(usilicTocTitles(), /*cursorAtEnd=*/false, usilicGroupKind_);  // 選んだ種別へカーソルを戻す
    return true;
  }
  if (directoryMode_.primaryAnnotations().topKind() == DirectoryMode::DirAnnotationKind::Usilic) {
    directoryMode_.primaryAnnotations().pop();  // ディレクトリ選択で積んだ注釈
  }
  if (directoryMode_.currentDirectory() != underlineOriginDirectory_ && !underlineOriginDirectory_.empty()) {
    directoryMode_.enterDirectory(underlineOriginDirectory_);  // 一覧から選んだディレクトリへ移動していた場合、元の位置へ戻す
  }
  // Du/Locateとは異なり、UNDERは常にDirectoryから直接発動し(検索
  // プロンプトを経由しない)、directoryMode_/directoryView_の状態
  // (エントリ一覧・カーソル位置)には一切触れていないので、戻り先は
  // 常にそのままのDirectory一覧であり、duReturnStack_/
  // locateReturnStack_のようなスタックは不要。
  currentMode_ = ApplicationMode::Directory;
  display_.hideVisibleCursor();  // returnToDirectory()自身の同一のコメント参照
  directoryView_.redraw();
  return true;
}

bool Application::openSelectedSegment() {
  const CutSegment *segment = cutMode_.selectedSegment();
  if (!segment) return false;
  if (cutOrigin_ == CutOrigin::Epub) return openEpubSegmentImage(*segment) || openEpubTocEntry(*segment);
  saveCutFilePosition();  // 中身を見たまま終了しても一覧の位置が残るように

  std::vector<StyledLine> content = cutMode_.segmentContent(*segment);
  std::vector<ContentView::Line> lines;
  lines.reserve(content.size());
  // l.attributes(2026-08-05): これはHELP.jp/CUTが表示する実際の画面
  // コンテンツである――なぜこれをここで落とすと、FileMode/CutModeが
  // それを持ち越すようになった後でも、まさにHELP.jp自身の太字の
  // セクション見出しが隠れていたのかについてはContentView::
  // Line::attributes自身のコメント参照。
  for (auto &l : content) lines.push_back({l.text, std::nullopt, std::nullopt, l.attributes});
  cutContentView_.setLines(std::move(lines));
  cutContentView_.setCursorIndex(0);
  cutContentView_.redraw();
  // 2026-08-30、直接のユーザー報告(GUI、"内容一覧でどれかを選ぶと
  // 選んだ時点でのブロックカーソルが画面に残留（^P/^Nで解消）")、
  // openSelectedArchiveEntry()自身の同一の修正参照(HELP.jpも
  // CutContent経由で表示される、このメソッド自身のコメント冒頭
  // 参照――同じ症状がHELPでも起き得る)。
  cutContentView_.syncVisibleCursor();
  currentMode_ = ApplicationMode::CutContent;
  return true;
}

// 2026-09-28追加、直接のユーザー提案("CUTで出る目次だが、「表紙」を「表紙
// [image: title]」にして returnでクイックルックというのは？"): EPUBの章の一覧
// (executeCut()自身の2026-09-28のコメント参照)で、画像だけの章(epubToHelpMarkup()
// が「表紙 [image: title.jpg]」の1行にまとめたもの)を選んだら、その章の中身
// ("[image: ...]"1行だけ)を見せる代わりに、画像をzipから一時ディレクトリへ
// 取り出してクイックルックで開く。クイックルックが無い環境(Linux)では従来どおり。
bool Application::openEpubSegmentImage(const CutSegment &segment) {
  if (cutOrigin_ != CutOrigin::Epub) return false;
  return openEpubImageIn(segment.label);
}

// 2026-09-28追加、直接のユーザー依頼(本文の`[image: ...]`の行で[Enter]したら
// クイックルック、"それでよろしく"): EPUBの本文(File)と章の中身(CutContent)で、
// カーソル行の画像を開く。File画面の[Enter]はusilicバナー画面の時しか使って
// いない(openSelectedBannerEntry())ので、そちらが失敗した時だけここへ来る。
bool Application::openEpubLineImage(const ContentView &view) {
  const auto &lines = view.lines();
  int idx = view.cursorIndex();
  if (idx < 0 || idx >= static_cast<int>(lines.size())) return false;
  return openEpubImageIn(lines[idx].text);
}

// `text`中の最初の"[image: <ファイル名>]"の画像を、表示中のEPUBのzipから
// 一時ディレクトリへ取り出してクイックルックで開く。クイックルックが無い
// 環境(Linux)や、EPUBでない・画像が見つからない場合はfalse。
// 2026-09-30追加、直接のユーザー依頼("webサイトへのリンク...踏んだらクイックルックへ移行して
// ほしい"): URLをWebの場所のファイル(.webloc)にしてクイックルック(runQuickLook())に渡す。
bool Application::openEpubWebLink(const std::string &url) {
  if (::access("/usr/bin/qlmanage", X_OK) != 0) return false;
  std::string host = url.substr(url.find("://") + 3);
  host = host.substr(0, host.find('/'));
  if (host.empty()) host = "link";
  std::string dir = allocateArcDirectory();
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  std::string path = dir + "/" + host + ".webloc";
  std::ofstream out(path);
  if (!out) return false;
  std::string escaped;
  for (char c : url) escaped += c == '&' ? std::string("&amp;") : c == '<' ? std::string("&lt;") : std::string(1, c);
  out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
         "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
         "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
         "<plist version=\"1.0\"><dict><key>URL</key><string>"
      << escaped << "</string></dict></plist>\n";
  out.close();
  return runQuickLook({path});
}

// 横書きの本文のカーソルの行のWebのリンク(openEpubWebLink())。
bool Application::openEpubWebLinkAtCursor() {
  if (!isEpubFile(fileMode_.currentFile())) return false;
  auto url = fileMode_.epubWebLinkForRawLine(fileRawLineAtCursor());
  return url && openEpubWebLink(*url);
}

bool Application::openEpubImageIn(const std::string &text) {
  if (!isEpubFile(fileMode_.currentFile())) return false;
  if (::access("/usr/bin/qlmanage", X_OK) != 0) return false;
  const std::string marker = "[image: ";
  std::size_t open = text.find(marker);
  if (open == std::string::npos) return false;
  std::size_t close = text.find(']', open);
  if (close == std::string::npos) return false;
  std::string name = text.substr(open + marker.size(), close - open - marker.size());
  EpubMemberReader reader = fileMode_.epubMemberReader(fileMode_.currentFile());
  auto member = findEpubImageMember(reader, name);
  if (!member) return false;
  auto data = reader(*member);
  if (!data) return false;
  std::string dir = allocateArcDirectory();
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  std::string imagePath = dir + "/" + name;
  std::ofstream out(imagePath, std::ios::binary);
  if (!out) return false;
  out.write(data->data(), static_cast<std::streamsize>(data->size()));
  out.close();
  return runQuickLook({imagePath});
}

// 2026-09-28追加、直接のユーザー提案("EPUBモード新設はどうかな。.epubファイルを
// 選ぶとCUT風一覧画面が立ち上がる。モード名がCUTからEPUBに変わり、タグジャンプ
// 機能を備えている点が違う。FILE履歴には"^I"も不要"、+"1. 本全体 2. ...最初に
// 開いた時以外は履歴に残ってる行で開けばいい"): EPUBモードは、章の一覧(CUTの
// 仕組みを流用、cutOrigin_ == CutOrigin::Epub、モード名"EPUB")と、本全体の
// 本文(FileMode、fileReturnMode_ == Cut)の2画面。章の一覧は本文の章の印
// (epubToHelpMarkup()が行末に付けるタブ、QUICK.txtと同じ)で分割したもの。
//   - 初めて開く本(FILEの位置の記録が無い)は章の一覧から、2回目以降は記録の行
//     から本文を開く。
//   - 章の一覧で[Enter]→本全体の本文のその章の頭へ(画像だけの章はクイック
//     ルック)。本文で^G→章の一覧(今読んでいる章にカーソル)。章の一覧で^G→
//     (ls)(returnFromCut())。
//   - 本文ではリンク(下線)で[Enter]→リンク先、^X^Xで戻る(followEpubLink())。
// 入口は(ls)の[Enter]、FILE履歴、^Xf、^Je/^J^E。EXEC節で.epubに外部
// プログラムを割り当てていればそちらが優先(openExternallyIfApplicable())。
// 2026-10-01、直接のユーザー依頼("epub画面が出る前に(ls)が出たり-gでは一旦横書き画面が出て
// から縦書きに切り替わったりするので...最終画面になるまでは...「Reading...」でよろしく"):
// 読み込みの間は画面への出力を止め(Display::setOutputEnabled())、止める前にモードラインへ
// "Reading..."(DUの"Disk usage..."と同じ形)を出す。開き終えてから一度だけ描く。既に止めて
// あれば(再開時のstartResumingSession()が"Reading Kat.epub..."を出している)そのまま。
bool Application::openEpub(const std::string &path) {
  bool wasEnabled = display_.outputEnabled();
  if (wasEnabled) {
    drawPromptLabel(PromptKind::EpubLoad, /*positionText=*/"", "Reading...");
    display_.flush();
  }
  display_.setOutputEnabled(false);
  bool opened = openEpubQuietly(path);
  display_.setOutputEnabled(wasEnabled);
  if (wasEnabled) {
    activeView().redraw();
    if (opened) activeView().syncVisibleCursor();
    display_.flush();
  }
  return opened;
}

bool Application::openEpubQuietly(const std::string &path) {
  // 2026-09-29、直接のユーザー依頼("キー操作での縦横切り替えは廃止。著者は、いずれかを
  // cssファイルに設定してあるはずなのでそれで判断する"): 縦書きかどうかは本が決める
  // (epubIsRightToLeft()、OPFのpage-progression-direction="rtl")。
  bool vertical = epubIsRightToLeft(fileMode_.epubMemberReader(path));
  epubJumpRaw_.reset();  // ^X^Xの戻り先は本ごと
  epubJumpOrigin_.reset();
  epubHorizontalSection_.reset();
  syncEpubHorizontalPresentation();
  bool firstTime = !history_.positionFor(PositionCategory::File, path);
  if (!fileMode_.loadFile(path, [this]() { return keyInterruptRequested(); })) return false;
  fileMode_.annotations().clear();
  fileMode_.annotations().pushWithNameOverride(FileMode::FileAnnotationKind::Search, path, "EPUB");
  std::vector<StyledLine> sourceLines;
  sourceLines.reserve(fileView_.lines().size());
  for (const auto &line : fileView_.lines()) sourceLines.push_back({line.text, line.attributes});
  cutOrigin_ = CutOrigin::Epub;
  cutPositionKey_.clear();
  cutSyntax_ = GrepSyntax::Fixed;
  cutReturnMode_ = ApplicationMode::Directory;
  epubVertical_ = vertical;
  epubVerticalPath_ = fileMode_.currentFile();
  if (vertical) rebuildEpubVertical(fileRawLineAtCursor());
  bool split = cutMode_.split(sourceLines, kHelpPattern, GrepSyntax::Fixed);
  if (split) applyEpubChapterLabels();
  if (!split) {
    // 章の印が1つも無い本: 本文だけ(^Gで(ls)へ)
    fileMode_.annotations().clear();
    currentMode_ = ApplicationMode::File;
    fileReturnMode_ = ApplicationMode::Directory;
    activeView().redraw();
    return true;
  }
  if (firstTime) {
    currentMode_ = ApplicationMode::Cut;
    if (epubTocVerticalActive()) refreshEpubTocVertical();
    activeView().redraw();
    activeView().syncVisibleCursor();
  } else {
    currentMode_ = ApplicationMode::File;
    fileReturnMode_ = ApplicationMode::Cut;
    activeView().redraw();
    activeView().syncVisibleCursor();
  }
  return true;
}

// 2026-09-30追加、直接のユーザー報告("LOCATEから.epubファイルを選ぶと変わるのはファイル名
// だけで内容は今と同じなんだが"): LOCATE/DU/USILIC(下線の一覧)の結果の.epubは、ふつうの
// ファイルとしてfileMode_.loadFile()で読むだけで、縦書きの列(rebuildEpubVertical())を組み
// 直していなかった――前の本の縦書きが残り、名前欄だけが変わっていた。openEpub()で開き、
// ^Gの戻り先(章の一覧/本文から)を発動元の一覧にする。
bool Application::openEpubReturningTo(const std::string &path, ApplicationMode returnMode) {
  if (!openEpub(path)) return false;
  cutReturnMode_ = returnMode;
  if (currentMode_ == ApplicationMode::File && fileReturnMode_ == ApplicationMode::Directory)
    fileReturnMode_ = returnMode;
  return true;
}

// 章の一覧(cutMode_)の名前の置き換え(openEpub()・returnToEpubToc()の分割の後): display:noneの
// 見出しの字(EpubLayoutHints::chapterLabel)と、中央寄せ・字下げの行の頭の空白。
void Application::applyEpubChapterLabels() {
  const auto &labels = fileMode_.epubLayoutHints().chapterLabel;
  const auto &segments = cutMode_.segments();
  for (std::size_t i = 0; i < segments.size(); i++) {
    int start = segments[i].startLine;
    if (start < 0 || start >= static_cast<int>(fileView_.lines().size())) continue;
    int raw = fileView_.lines()[start].rawLineNumber.value_or(-1);
    auto it = labels.find(raw);
    if (it != labels.end()) {
      cutMode_.setSegmentLabel(i, it->second);
      continue;
    }
    // 中央寄せ・字下げの行(FileModeが画面の行の頭に空白を足す)は、一覧ではその空白を除く
    const auto &hints = fileMode_.epubLayoutHints();
    const std::string &label = segments[i].label;
    bool padded = hints.indent.count(raw) ||
                  std::find(hints.center.begin(), hints.center.end(), raw) != hints.center.end();
    std::size_t pad = label.find_first_not_of(' ');
    if (padded && pad != std::string::npos && pad > 0) cutMode_.setSegmentLabel(i, label.substr(pad));
  }
}

// openEpub()自身のコメント参照: 章の一覧から本全体の本文のその章の頭へ。
bool Application::openEpubTocEntry(const CutSegment &segment) {
  const auto &lines = fileView_.lines();
  if (segment.startLine < 0 || segment.startLine >= static_cast<int>(lines.size())) return false;
  fileView_.setCursorIndex(segment.startLine);
  fileView_.setTopIndex(segment.startLine);
  currentMode_ = ApplicationMode::File;
  fileReturnMode_ = ApplicationMode::Cut;
  if (epubVerticalActive()) rebuildEpubVertical(fileRawLineAtCursor());  // 一覧にいる間のリサイズにも追従
  activeView().redraw();
  activeView().syncVisibleCursor();
  return true;
}

// openEpub()自身のコメント参照: 本文から章の一覧へ。本文の表示中にリサイズで
// 折り返しが変わっていても合うよう、今の本文から章の一覧を作り直し、今読んで
// いる行を含む章にカーソルを置く。
bool Application::returnToEpubToc() {
  if (epubVerticalActive()) syncEpubVerticalToFile();
  fileMode_.saveCursorPosition();
  int cursor = fileView_.cursorIndex();
  std::vector<StyledLine> sourceLines;
  sourceLines.reserve(fileView_.lines().size());
  for (const auto &line : fileView_.lines()) sourceLines.push_back({line.text, line.attributes});
  if (!cutMode_.split(sourceLines, kHelpPattern, GrepSyntax::Fixed)) return returnToDirectory();
  applyEpubChapterLabels();
  const auto &segments = cutMode_.segments();
  int selected = 0;
  for (std::size_t i = 0; i < segments.size(); i++)
    if (segments[i].startLine <= cursor) selected = static_cast<int>(i);
  cutView_.setCursorIndex(selected);
  currentMode_ = ApplicationMode::Cut;
  if (epubTocVerticalActive()) refreshEpubTocVertical();
  activeView().redraw();
  activeView().syncVisibleCursor();
  return true;
}

// 2026-09-28追加、直接のユーザー依頼("縦書きもよろしく"、"^Jeでファイルを選ぶと
// EPUBモードへ移行して縦書きになる"、"モード内での縦横切り替えは^Oeで"、同日の
// "Bibiのように横方向へもスクロールができればいい"→"まずは1列ずつから"): EPUBの
// 本文の縦書き。layoutEpubVerticalColumns()(epub_vertical.cpp)が本文を列の並びに
// 組み、epubVerticalFirst_(右端に置く列)から画面1枚分だけをepubVerticalView_へ
// 描く。縦書きの間はviewForMode(File)がfileView_の代わりにこれを返し、移動の
// キーはhandleEpubVerticalCommand()が列単位の横スクロールに読み替える。横書きの
// fileView_は裏で持ち続け、位置の記録(FILE履歴)・章の一覧との行き来・縦横の
// 切り替えは、元の行番号(rawLineNumber)で両者を対応させる(syncEpubVerticalToFile())。
bool Application::epubVerticalActive() const { return epubBookVertical() && !epubHorizontalSection_; }

// 縦書きの列を組んだ本(openEpub()のepubVerticalPath_)を見ている時だけ(2026-09-30、openEpubReturningTo()
// 参照――openEpub()を通らずに別の.epubを読むと、前の本の縦書きが残っていた)。
bool Application::epubBookVertical() const {
  return epubVertical_ && isEpubFile(fileMode_.currentFile()) && fileMode_.currentFile() == epubVerticalPath_;
}

std::optional<std::pair<int, int>> Application::epubHorizontalRangeAt(int rawLine) const {
  for (const auto &range : fileMode_.epubLayoutHints().horizontal)
    if (rawLine >= range.first && rawLine <= range.second) return range;
  return std::nullopt;
}

// 横書きの範囲へ入る: fileView_のカーソルをrawLineの行(範囲の外なら端)に置く。
// 描き直しは呼び出し元(activeView()はもうfileView_)。
void Application::enterEpubHorizontal(const std::pair<int, int> &range, int rawLine) {
  epubHorizontalSection_ = range;
  syncEpubHorizontalPresentation();
  rawLine = std::clamp(rawLine, range.first, range.second);
  const auto &lines = fileView_.lines();
  for (std::size_t i = 0; i < lines.size(); i++) {
    if (lines[i].rawLineNumber.value_or(0) < rawLine) continue;
    fileView_.setCursorIndex(static_cast<int>(i));
    // 2026-09-29(章の頭は画面の頭から、直接のユーザー報告"奥付が画面途中からのスタート"):
    // 範囲の最初の行を画面の一番上にする。範囲の途中/最後へ入る時はその行が見える所まで。
    int startIndex = static_cast<int>(i);
    while (startIndex > 0 && lines[startIndex - 1].rawLineNumber.value_or(0) >= range.first) startIndex--;
    fileView_.setTopIndex(std::max(startIndex, static_cast<int>(i) - std::max(1, fileView_.height()) + 1));
    return;
  }
}

// 縦書きで先へ(forward)/前へ動いた後: 動く前の列(fromRaw)と今の列の間に横書きの範囲が
// あれば(縦書きの列には横書きの範囲の行が無いので、越える時は間に挟まる)、そこへ入る。
// 本の端でそれ以上動けない時も、その先に横書きの範囲があれば入る(奥付は本の最後)。
bool Application::maybeEnterEpubHorizontal(int fromRaw, bool forward) {
  const auto &ranges = fileMode_.epubLayoutHints().horizontal;
  int now = epubVerticalCurrentRaw();
  // 本の端: カーソルの先(前)に本文の列(空の列以外)が無い
  auto noTextBeyond = [this](int dir) {
    for (int c = epubVerticalCursor_ + dir; c >= 0 && c < static_cast<int>(epubVerticalCols_.size()); c += dir)
      if (!epubVerticalCols_[c].padding) return false;
    return true;
  };
  if (forward) {
    for (const auto &range : ranges)
      if (range.first > fromRaw && (now >= range.first || noTextBeyond(1))) {
        enterEpubHorizontal(range, range.first);
        return true;
      }
  } else {
    for (auto it = ranges.rbegin(); it != ranges.rend(); ++it)
      if (it->second < fromRaw && (now <= it->second || noTextBeyond(-1))) {
        enterEpubHorizontal(*it, it->second);
        return true;
      }
  }
  return false;
}

// 横書きの範囲で、カーソルが範囲の外へ出ていたら縦書きへ戻る(前へ出たらその行の最後の
// 列、先へ出たらその行の最初の列)。EPUBの本文の外へ出ていたら(章の一覧等)範囲を忘れる。
bool Application::leaveEpubHorizontalIfOutside() {
  if (!epubHorizontalSection_) return false;
  if (currentMode_ != ApplicationMode::File || !epubBookVertical()) {
    epubHorizontalSection_.reset();
    syncEpubHorizontalPresentation();
    if (currentMode_ == ApplicationMode::Cut && epubTocVerticalActive()) {
      refreshEpubTocVertical();
      activeView().redraw();
    }
    return false;
  }
  // カーソルの行に加えて画面の先頭の行も見る(ホイールの慣性等はカーソルを動かさずに
  // 画面だけを送ることがある)。前へ出たら範囲の直前の行、先へ出たらカーソルの行へ。
  int raw = fileRawLineAtCursor();
  const auto &lines = fileView_.lines();
  int top = fileView_.topIndex();
  int topRaw = (top >= 0 && top < static_cast<int>(lines.size())) ? lines[top].rawLineNumber.value_or(raw) : raw;
  bool before = raw < epubHorizontalSection_->first || topRaw < epubHorizontalSection_->first;
  bool after = raw > epubHorizontalSection_->second;
  if (!before && !after) return false;
  if (before) raw = epubHorizontalSection_->first - 1;
  epubHorizontalSection_.reset();
  syncEpubHorizontalPresentation();
  rebuildEpubVertical(raw);  // リサイズで組み直しが要る場合もあるので、組み直してから置く
  if (before && !epubHorizontalSection_) {
    int col = 0;
    for (std::size_t c = 0; c < epubVerticalCols_.size(); c++)
      if (!epubVerticalCols_[c].padding && epubVerticalCols_[c].rawLineNumber <= raw) col = static_cast<int>(c);
    int perScreen = epubVerticalColumnsPerScreen(display_.columns());
    setEpubVerticalView((col / perScreen) * perScreen, col);  // 前の章の最後のページ
  }
  activeView().redraw();
  activeView().syncVisibleCursor();
  return true;
}

void Application::alignEpubVerticalCursorToLeftEdge() {
  if (!epubVerticalActive()) return;
  int perScreen = epubVerticalColumnsPerScreen(display_.columns());
  setEpubVerticalView(std::max(0, epubVerticalCursor_ - perScreen + 1), epubVerticalCursor_);
  epubVerticalView_.redraw();
  epubVerticalView_.syncVisibleCursor();
}

std::optional<Application::EpubScrollbar> Application::epubVerticalScrollbar() const {
  int total = static_cast<int>(epubVerticalCols_.size());
  int perScreen = epubVerticalColumnsPerScreen(display_.columns());
  // 2026-09-30、直接のユーザー報告("奥付ページになったとたんつまみの位置が変わる"): 縦書きの
  // 本の横書きの範囲(奥付等)では横書きの行で測っていたので、縦書きの列で測った位置から
  // 飛んでいた。範囲の直後の列(縦書きの列には範囲の行が無い)を右端に置いた位置とする。
  if (currentMode_ == ApplicationMode::File && epubBookVertical() && epubHorizontalSection_ && !promptActive()) {
    int after = total;
    for (int c = 0; c < total; c++)
      if (epubVerticalCols_[c].rawLineNumber > epubHorizontalSection_->second) {
        after = c;
        break;
      }
    return EpubScrollbar{std::clamp(after, 0, std::max(0, total - perScreen)), total, perScreen};
  }
  if (!epubVerticalDragActive()) return std::nullopt;
  if (currentMode_ == ApplicationMode::Cut)  // 縦書きの章の一覧
    return EpubScrollbar{epubTocFirst_, static_cast<int>(epubTocCols_.size()), perScreen};
  return EpubScrollbar{epubVerticalFirst_, total, perScreen};
}

void Application::jumpEpubVerticalToFraction(double fraction) {
  if (!epubVerticalDragActive()) return;
  int perScreen = epubVerticalColumnsPerScreen(display_.columns());
  if (currentMode_ == ApplicationMode::Cut) {  // 縦書きの章の一覧: 右端の列を動かし、右端の列の章を選ぶ
    int total = static_cast<int>(epubTocCols_.size());
    if (total == 0) return;
    epubTocFirst_ = static_cast<int>(std::clamp(fraction, 0.0, 1.0) * std::max(0, total - perScreen));
    int count = static_cast<int>(cutView_.lines().size());
    cutView_.setCursorIndex(std::clamp(epubTocCols_[epubTocFirst_].rawLineNumber - 1, 0, std::max(0, count - 1)));
    refreshEpubTocVertical(/*keepFirst=*/true);
    epubTocVerticalView_.redraw();
    epubTocVerticalView_.syncVisibleCursor();
    return;
  }
  int scrollable = std::max(0, static_cast<int>(epubVerticalCols_.size()) - perScreen);
  setEpubVerticalFirst(static_cast<int>(std::clamp(fraction, 0.0, 1.0) * scrollable));
  epubVerticalView_.redraw();
  epubVerticalView_.syncVisibleCursor();
}

// 2026-09-30: 縦書きの章の一覧(epubTocVerticalActive())も(scrollEpubVertical()参照)。
bool Application::epubVerticalDragActive() const {
  return ((currentMode_ == ApplicationMode::File && epubVerticalActive()) ||
          (currentMode_ == ApplicationMode::Cut && epubTocVerticalActive())) &&
         !promptActive();
}

bool Application::epubVerticalScrollActive() const {
  return ((currentMode_ == ApplicationMode::File && epubVerticalActive()) ||
          (currentMode_ == ApplicationMode::Cut && epubTocVerticalActive())) &&
         !promptActive();
}

int Application::fileRawLineAtCursor() const {
  const auto &lines = fileView_.lines();
  int idx = fileView_.cursorIndex();
  if (idx < 0 || idx >= static_cast<int>(lines.size())) return 1;
  return lines[idx].rawLineNumber.value_or(1);
}

int Application::epubVerticalRows() const {
  return display_.modeLineVisible() ? std::max(1, display_.rows() - 1) : std::max(1, display_.rows());
}

void Application::rebuildEpubVertical(int rawLine) {
  // 横書きの画面の行(折り返し済み)を、元の行番号ごとに論理行へつなぎ直す
  const EpubLayoutHints &hints = fileMode_.epubLayoutHints();
  std::set<int> alignEnd(hints.alignEnd.begin(), hints.alignEnd.end());
  std::set<int> center(hints.center.begin(), hints.center.end());
  std::set<int> chapterStarts;  // 章の印(行末のタブ、PipeBufferがU+2060にしたもの)のある行
  std::vector<EpubVerticalSourceLine> logical;
  for (const auto &original : fileView_.lines()) {
    int raw = original.rawLineNumber.value_or(-1);
    if (raw >= 0 && epubHorizontalRangeAt(raw)) continue;  // 横書きの範囲(epubHorizontalSection_参照)
    // 中央寄せの行(EpubLayoutHints::center)は、FileModeが頭に足した空白を取り除く。字下げの行
    // (EpubLayoutHints::indent、横書きの<indentN>が画面の行の頭に空白を入れる)も同じく取り除き、
    // 縦書きでは列の頭を空ける(EpubVerticalSourceLine::indentFirst/indentRest)。
    ContentView::Line line = original;
    if (center.count(raw) || hints.indent.count(raw)) {
      std::size_t pad = line.text.find_first_not_of(' ');
      if (pad == std::string::npos) pad = line.text.size();
      line.text.erase(0, pad);
      for (auto &run : line.attributes) {
        run.start = std::max(0, run.start - static_cast<int>(pad));
        run.end = std::max(0, run.end - static_cast<int>(pad));
      }
    }
    if (line.text.find("\xE2\x81\xA0") != std::string::npos) chapterStarts.insert(raw);
    if (logical.empty() || raw < 0 || logical.back().rawLineNumber != raw) {
      logical.push_back({line.text, line.attributes, raw, alignEnd.count(raw) > 0});
      if (auto indent = hints.indent.find(raw); indent != hints.indent.end()) {
        logical.back().indentFirst = indent->second.first;
        logical.back().indentRest = indent->second.second;
      }
      continue;
    }
    int offset = static_cast<int>(logical.back().text.size());
    logical.back().text += line.text;
    for (auto run : line.attributes) {
      run.start += offset;
      run.end += offset;
      logical.back().attributes.push_back(run);
    }
  }
  std::vector<EpubVerticalColumn> laid = layoutEpubVerticalColumns(logical, epubVerticalRows(), display_.isGraphical());
  // 2026-09-29、直接のユーザー依頼("Kindle、iBook、Kinoppy風の章管理...章の開始は必ず画面
  // 右端から始まるだけ"): 章の頭の列が画面1枚分(perScreen列)の区切りに来るよう、その前に
  // 空の列(padding)を足す――先頭から1画面ずつ送れば、章は必ず右端から始まる(章をまたぐ
  // 移動は区切りに揃える、handleEpubVerticalCommand()参照)。
  // 扉(EpubLayoutHints::centered): 横書きの中に置かれた縦書きの箱は1ページとし、両側に
  // 空の列を足して中央に置く(titlePage、^Nで次のページへ)。
  int perScreen = epubVerticalColumnsPerScreen(display_.columns());
  const auto &centered = hints.centered;
  epubVerticalCols_.clear();
  epubVerticalBoundaries_.clear();
  auto padToPage = [&]() {
    int raw = epubVerticalCols_.empty() ? 0 : epubVerticalCols_.back().rawLineNumber;
    while (epubVerticalCols_.size() % perScreen != 0) {
      epubVerticalCols_.push_back({{}, raw});
      epubVerticalCols_.back().padding = true;
    }
    int at = static_cast<int>(epubVerticalCols_.size());
    if (at > 0 && (epubVerticalBoundaries_.empty() || epubVerticalBoundaries_.back() != at))
      epubVerticalBoundaries_.push_back(at);
  };
  for (std::size_t c = 0; c < laid.size();) {
    int raw = laid[c].rawLineNumber;
    bool lineStart = c == 0 || laid[c - 1].rawLineNumber != raw;
    auto range = std::find_if(centered.begin(), centered.end(),
                              [raw](const auto &r) { return raw >= r.first && raw <= r.second; });
    if (range == centered.end()) {
      if (lineStart && chapterStarts.count(raw)) padToPage();
      epubVerticalCols_.push_back(std::move(laid[c++]));
      continue;
    }
    padToPage();
    std::size_t end = c;
    while (end < laid.size() && laid[end].rawLineNumber >= range->first && laid[end].rawLineNumber <= range->second)
      end++;
    int pad = std::max(0, perScreen - static_cast<int>(end - c));
    auto pushPadding = [&](int count, int padRaw) {
      for (int k = 0; k < count; k++) {
        epubVerticalCols_.push_back({{}, padRaw});
        epubVerticalCols_.back().padding = true;
        epubVerticalCols_.back().titlePage = true;
      }
    };
    pushPadding(pad / 2, range->first);
    for (; c < end; c++) {
      epubVerticalCols_.push_back(std::move(laid[c]));
      epubVerticalCols_.back().titlePage = true;
    }
    pushPadding(pad - pad / 2, range->second);
    padToPage();  // 扉のページの後(区切りを記録する)
  }
  padToPage();  // 最後の章も右端から始められるよう(右端に置ける列の上限は列の数-perScreen)
  epubVerticalMark_.reset();
  showEpubVerticalRaw(rawLine);
}

// rawLineの最初の列を右端に置く。
void Application::showEpubVerticalRaw(int rawLine) {
  if (auto range = epubHorizontalRangeAt(rawLine)) {  // 横書きの範囲の行(リンク・章の一覧・開いた位置)
    enterEpubHorizontal(*range, rawLine);
    return;
  }
  epubHorizontalSection_.reset();
  syncEpubHorizontalPresentation();
  int first = 0;
  for (std::size_t c = 0; c < epubVerticalCols_.size(); c++)
    if (epubVerticalCols_[c].rawLineNumber >= rawLine) {
      first = static_cast<int>(c);
      break;
    }
  int cursor = first;  // 扉のページなら書名の列へ(列の頭はページの頭のまま)
  while (cursor + 1 < static_cast<int>(epubVerticalCols_.size()) && epubVerticalCols_[cursor].titlePage &&
         epubVerticalCols_[cursor].padding)
    cursor++;
  setEpubVerticalView(first, cursor);
}

// 右端の列をfirstにして描き直す(範囲外は端で止める)。列カーソルは画面の外へ出る
// なら画面の端へ引き寄せる(ホイール等のスクロール用)。
void Application::setEpubVerticalFirst(int first) {
  int perScreen = epubVerticalColumnsPerScreen(display_.columns());
  int last = std::max(0, static_cast<int>(epubVerticalCols_.size()) - perScreen);
  first = std::clamp(first, 0, last);
  setEpubVerticalView(first, std::clamp(epubVerticalCursor_, first, first + perScreen - 1));
}

// 2026-09-29追加、直接のユーザー依頼("画面右端がタグジャンプエリア、というのは
// いかにも分かりにくい。...まずは手始めに右方向へのカーソル移動をサポートしてほしい"、
// 同日の補足"「列」ではなく「ブロック」カーソルを左右に動かせる。つまり横書き
// (選択行が無い画面では左端にブロックカーソル)仕様。傍線エリアに来たらハイライトに
// 変わる"): 横書きのカーソル行にあたる列カーソル。ふだんはその列の頭(一番上)に
// ブロックカーソル、リンク/画像の列ではその字を反転表示。[Enter]・クリックの2回目
// (activateEpubVerticalColumn())はこの列に対して働く。
// cursorが画面の外なら、見える所まで画面をずらす。モードラインの位置欄は
// 「列カーソルの列/列の数 %」。
void Application::setEpubVerticalView(int first, int cursor, std::optional<int> link) {
  int total = static_cast<int>(epubVerticalCols_.size());
  int perScreen = epubVerticalColumnsPerScreen(display_.columns());
  int last = std::max(0, total - perScreen);
  cursor = std::clamp(cursor, 0, std::max(0, total - 1));
  first = std::clamp(first, 0, last);
  if (cursor < first) first = cursor;
  if (cursor >= first + perScreen) first = cursor - perScreen + 1;
  epubVerticalFirst_ = std::clamp(first, 0, last);
  epubVerticalCursor_ = cursor;
  // 2026-09-30、直接のユーザー依頼("複数リンクを含む行では^N^Pで選べるようにしたら？ -gの時は
  // 左クリックで選べればいい"): 列カーソルの行の、選んでいるリンク(EpubVerticalCell::link)。
  // 指定が無ければ列カーソルの列にある最初のリンク(無ければ選ばない――リンクのある段落の、
  // リンクの無い列で段落全体を反転していた、直接のユーザー報告"行の中にリンクがあると行全体が
  // ハイライト表示になる")。
  epubVerticalLink_ = -1;
  epubVerticalLinkRaw_ = total > 0 ? epubVerticalCols_[cursor].rawLineNumber : -1;
  if (link) {
    epubVerticalLink_ = *link;
  } else if (total > 0) {
    for (const auto &cell : epubVerticalCols_[cursor].cells)
      if (cell.link >= 0) {
        epubVerticalLink_ = cell.link;
        break;
      }
  }
  bool actionable = total > 0 && epubVerticalColumnTarget(epubVerticalCursor_).has_value();
  int highlightLink = actionable && fileMode_.epubLayoutHints().lineLinks.count(epubVerticalLinkRaw_)
                          ? epubVerticalLink_
                          : -1;
  // 2026-09-30、直接のユーザー報告("左スクロール(jの方向)で同じページにタイトルが複数入るのは、
  // 余分なスペースが無意味になるのでバグ...それ以後は章のタイトルが右端に達するまで空行を
  // 吐き続ける。逆方向(kの場合)は...最後の行から始めてタイトルが右端に来るまで待つように
  // したら？"): 画面には右端の列の章(区切りepubVerticalBoundaries_の間)だけを描き、前後の章の
  // 列は空にする。章の間の空の列は飛ばす(stepEpubVerticalFirst()参照)。
  auto [shownFrom, shownUntil] = epubVerticalShownRange(epubVerticalFirst_);
  epubVerticalView_.setLines(renderEpubVerticalWindow(epubVerticalCols_, epubVerticalFirst_, epubVerticalRows(),
                                                      display_.columns(), !display_.isGraphical(),
                                                      actionable ? epubVerticalCursor_ : -1, shownFrom, shownUntil,
                                                      highlightLink));
  captureEpubVerticalEdges(epubVerticalView_, epubVerticalCols_, epubVerticalFirst_, actionable ? epubVerticalCursor_ : -1,
                           shownFrom, shownUntil, highlightLink);
  epubVerticalView_.setCursorIndex(0);
  epubVerticalView_.setTopIndex(0);
  epubVerticalView_.setContentCursorColumn(
      std::max(0, epubVerticalScreenColumn(epubVerticalCursor_ - epubVerticalFirst_, display_.columns())));
  char position[64];
  std::snprintf(position, sizeof(position), "%05d/%05d %3d%%", epubVerticalCursor_ + 1, std::max(total, 1),
                total > 0 ? static_cast<int>(100L * (epubVerticalCursor_ + 1) / total) : 100);
  epubVerticalView_.setPositionTextOverride(std::string(position));
}

// 左右のドラッグ/慣性で端から見えてくる列(Display::beginContentEdgeCapture()参照)を、
// kEpubVerticalEdgeColumns列だけ先/前から描いた画面として裏に描く。本文と同じビューで描くので、
// 描いた後に本文の行へ戻す。縦書きの本文(setEpubVerticalView())と章の一覧
// (refreshEpubTocVertical())で共用。
void Application::captureEpubVerticalEdges(ContentView &view, const std::vector<EpubVerticalColumn> &cols, int first,
                                           int highlight, int shownFrom, int shownUntil, int highlightLink) {
  if (!display_.isGraphical()) return;
  std::vector<ContentView::Line> window = view.lines();
  for (int side : {1, -1}) {
    view.setLines(renderEpubVerticalWindow(cols, first + side * kEpubVerticalEdgeColumns, epubVerticalRows(),
                                           display_.columns(), false, highlight, shownFrom, shownUntil,
                                           highlightLink));
    view.setCursorIndex(0);
    view.setTopIndex(0);
    display_.beginContentEdgeCapture();
    view.renderContentRows();
    display_.endContentEdgeCapture(side);
  }
  view.setLines(std::move(window));
  view.setCursorIndex(0);
  view.setTopIndex(0);
}

// 右端の列firstの章の列の範囲[from, until)(setEpubVerticalView()参照)。
std::pair<int, int> Application::epubVerticalShownRange(int first) const {
  int from = 0, until = static_cast<int>(epubVerticalCols_.size());
  for (int boundary : epubVerticalBoundaries_) {
    if (boundary <= first) from = boundary;
    else {
      until = boundary;
      break;
    }
  }
  return {from, until};
}

int Application::epubVerticalCurrentRaw() const {
  if (epubVerticalCursor_ < 0 || epubVerticalCursor_ >= static_cast<int>(epubVerticalCols_.size())) return 1;
  return epubVerticalCols_[epubVerticalCursor_].rawLineNumber;
}

// 縦書きの列カーソルの列の行へ、横書きのfileView_のカーソルを合わせる
// (位置の記録・章の一覧・横書きへの切り替えは、fileView_の側で行うため)。
void Application::syncEpubVerticalToFile() {
  int raw = epubVerticalCurrentRaw();
  const auto &lines = fileView_.lines();
  for (std::size_t i = 0; i < lines.size(); i++) {
    if (lines[i].rawLineNumber.value_or(0) < raw) continue;
    fileView_.setCursorIndex(static_cast<int>(i));
    fileView_.setTopIndex(static_cast<int>(i));
    return;
  }
}

// 1列の送り(forward)/戻り。空の列(章の頭を右端に揃えるためのもの)は飛ばし、扉の
// ページ(titlePage)は1列の送り/戻りでページごと越える(2026-09-29、直接のユーザー依頼
// "扉ページは画像ページ同様、一行送りがページ送りとなる")。空の列や扉を越えた時は、
// 着いた列のページ(perScreen列ごとの区切り)の頭を右端にする。
std::pair<int, int> Application::epubVerticalStep(int cursor, bool forward) const {
  int n = static_cast<int>(epubVerticalCols_.size());
  int perScreen = epubVerticalColumnsPerScreen(display_.columns());
  if (n == 0) return {-1, cursor};
  cursor = std::clamp(cursor, 0, n - 1);
  int dir = forward ? 1 : -1;
  int t = cursor + dir;
  bool jumped = false;
  if (epubVerticalCols_[cursor].titlePage) {
    while (t >= 0 && t < n && epubVerticalCols_[t].titlePage) t += dir;
    jumped = true;
  }
  while (t >= 0 && t < n && epubVerticalCols_[t].padding && !epubVerticalCols_[t].titlePage) {
    t += dir;
    jumped = true;
  }
  if (t < 0 || t >= n) return {-1, cursor};  // 本の端(横書きの範囲へはmaybeEnterEpubHorizontal())
  if (epubVerticalCols_[t].titlePage) {
    int pageStart = t;
    while (pageStart > 0 && epubVerticalCols_[pageStart - 1].titlePage) pageStart--;
    int title = pageStart;
    while (title < n && epubVerticalCols_[title].titlePage && epubVerticalCols_[title].padding) title++;
    if (title >= n || !epubVerticalCols_[title].titlePage) title = pageStart;
    return {pageStart, title};
  }
  return {jumped ? (t / perScreen) * perScreen : -1, t};
}

// 縦書きの本の末尾に横書きの範囲(奥付等)があれば、その最下行(画面の一番下)へ入る(^[<)。
// 縦書きの列の最後の本文の行より後ろの範囲だけ。
bool Application::enterEpubHorizontalAtBookEnd() {
  const auto &ranges = fileMode_.epubLayoutHints().horizontal;
  if (ranges.empty()) return false;
  int lastRaw = 0;
  for (const auto &col : epubVerticalCols_)
    if (!col.padding && !col.cells.empty()) lastRaw = std::max(lastRaw, col.rawLineNumber);
  const auto &range = ranges.back();
  if (range.first <= lastRaw) return false;
  enterEpubHorizontal(range, range.first);
  const auto &lines = fileView_.lines();
  int last = -1, startIndex = -1;
  for (int i = 0; i < static_cast<int>(lines.size()); i++) {
    int raw = lines[i].rawLineNumber.value_or(0);
    if (raw < range.first || raw > range.second) continue;
    if (startIndex < 0) startIndex = i;
    last = i;
  }
  if (last >= 0) {
    fileView_.setCursorIndex(last);
    fileView_.setTopIndex(std::max(startIndex, last - std::max(1, fileView_.height()) + 1));
  }
  activeView().redraw();
  activeView().syncVisibleCursor();
  return true;
}

bool Application::enterEpubHorizontalAtEdge(int delta) {
  // 2026-09-29(同じ報告"慣性スクロールは奥付の前の章で止まる"): 縦書きの列の端で、その先に
  // 横書きの範囲(奥付等)があればそこへ入る(k/jのmaybeEnterEpubHorizontal()と同じ)。
  // 2026-09-29訂正、直接のユーザー報告("jだと奥付の手前で止まる。スクロールも同じ"): 以前は
  // 縦書きの列全体の最後(最初)の列の行と比べていたが、本の最後の空行は奥付の範囲の外なので
  // 縦書きの列の最後に残り、「その先に範囲は無い」と判定されていた。今いる位置(画面の右端の
  // 列の行――スクロールで動くのは右端の列で、列カーソルは最後のページの空の列へ押し出されて
  // いることがある)と比べる。
  const auto &ranges = fileMode_.epubLayoutHints().horizontal;
  if (epubVerticalFirst_ < 0 || epubVerticalFirst_ >= static_cast<int>(epubVerticalCols_.size())) return false;
  int edgeRaw = epubVerticalCols_[epubVerticalFirst_].rawLineNumber;
  std::optional<std::pair<int, int>> target;
  if (delta > 0) {
    for (const auto &range : ranges)
      if (range.first > edgeRaw) {
        target = range;
        break;
      }
  } else {
    for (auto it = ranges.rbegin(); it != ranges.rend(); ++it)
      if (it->second < edgeRaw) {
        target = *it;
        break;
      }
  }
  if (!target) return false;
  enterEpubHorizontal(*target, delta > 0 ? target->first : target->second);
  activeView().redraw();
  activeView().syncVisibleCursor();
  return true;
}

// 2026-09-29追加、直接のユーザー依頼("k/jによる行送りでは章の最終行の次行は次の章になる
// (画面左端までの空行をスキップ)。同じ動きをスクロールにも実装して")。
// 2026-09-30訂正、直接のユーザー指摘("j/k同様、空の列を飛ばしてほしい。右方向では、せっかく
// 最後の行から始まって章のタイトルまで来ているのに、なぜそこで前の章の最終列へ飛ばないのかが
// わからん。そこから1列ずつ表示していく、の繰り返しでいいのでは？"): 空の列を飛ばした時も、
// 行き先のページの頭ではなく行き先の列そのものを右端に置く――先へは次の章の頭(ページの頭と
// 同じ)、前へは前の章の最後の列(画面には右端の列の章だけを描くので、その列から1列ずつ
// 現れる、setEpubVerticalView()参照)。扉のページはページの頭。
bool Application::stepEpubVerticalFirst(int delta) {
  bool moved = false;
  for (int k = 0; k < std::abs(delta); k++) {
    int before = epubVerticalFirst_;
    auto [first, target] = epubVerticalStep(epubVerticalFirst_, delta > 0);
    bool titlePage = target >= 0 && target < static_cast<int>(epubVerticalCols_.size()) &&
                     epubVerticalCols_[target].titlePage;
    setEpubVerticalFirst(titlePage && first >= 0 ? first : target);
    // 2026-09-30、直接のユーザー依頼("ブロックカーソルはスクロールの末尾ではなく先頭なので
    // 反映してほしい(縦スクロールの仕様)"): 横書きのj/k(ContentView::scrollLineDown()/
    // scrollLineUp())と同じく、列カーソルは送る向きの先頭――先へは画面の左端の、今の章の
    // 最後の本文の列(空の列は除く)、前へは右端の列。
    if (!titlePage && epubVerticalFirst_ != before) {
      int cursor = epubVerticalFirst_;
      if (delta > 0) {
        int perScreen = epubVerticalColumnsPerScreen(display_.columns());
        int until = epubVerticalShownRange(epubVerticalFirst_).second;
        cursor = std::min(epubVerticalFirst_ + perScreen, until) - 1;
        while (cursor > epubVerticalFirst_ && epubVerticalCols_[cursor].padding) cursor--;
      }
      setEpubVerticalView(epubVerticalFirst_, cursor);
    }
    if (epubVerticalFirst_ == before) break;
    moved = true;
  }
  return moved;
}

void Application::snapEpubVerticalToPage(bool forward) {
  int n = static_cast<int>(epubVerticalCols_.size());
  int c = epubVerticalCursor_;
  if (c < 0 || c >= n) return;
  if (epubVerticalCols_[c].titlePage) {  // 扉: ページの頭を右端に、カーソルは書名の列
    int pageStart = c;
    while (pageStart > 0 && epubVerticalCols_[pageStart - 1].titlePage) pageStart--;
    int title = pageStart;
    while (title + 1 < n && epubVerticalCols_[title].titlePage && epubVerticalCols_[title].padding) title++;
    setEpubVerticalView(pageStart, title);
    return;
  }
  if (!epubVerticalCols_[c].padding) return;
  // 空の列: その先(前)の本文の列へ、1列動いたのと同じ規則で(ページの区切りに揃う)
  auto [first, cursor] = epubVerticalStep(c, forward);
  setEpubVerticalView(first < 0 ? epubVerticalFirst_ : first, cursor);
}

// 縦書きの本文での移動: ←/^N/j/↓/ホイール下で1列先(左)へ、→/^P/k/↑で1列戻る、
// SPC/^V等(PageDown)で1画面先、ESC v等(PageUp)で1画面戻る、ESC >/ESC <で
// 先頭/末尾(横書きと逆)、^X^Xで大きく移動する前の列と行き来。それ以外はnullopt(通常の処理へ)。
std::optional<bool> Application::handleEpubVerticalCommand(Command command) {
  int perScreen = epubVerticalColumnsPerScreen(display_.columns());
  int first = epubVerticalFirst_, cursor = epubVerticalCursor_;
  int fromRaw = epubVerticalCurrentRaw();
  // 先へ/前へ進む移動は、間に横書きの範囲があればそこへ入る(maybeEnterEpubHorizontal())
  auto crossing = [&](bool forward) {
    if (!maybeEnterEpubHorizontal(fromRaw, forward)) return false;
    activeView().redraw();
    activeView().syncVisibleCursor();
    return true;
  };
  switch (command) {
    case Command::MoveDown: {  // 列カーソルを1列先(左)へ(setEpubVerticalView()、epubVerticalStep()参照)
      if (selectAdjacentEpubVerticalLink(+1)) return true;
      auto [nextFirst, next] = epubVerticalStep(cursor, true);
      setEpubVerticalView(nextFirst < 0 ? first : nextFirst, next);
      if (crossing(true)) return true;
      break;
    }
    case Command::MoveUp: {  // 1列戻る(右へ)
      if (selectAdjacentEpubVerticalLink(-1)) return true;
      auto [nextFirst, next] = epubVerticalStep(cursor, false);
      setEpubVerticalView(nextFirst < 0 ? first : nextFirst, next);
      if (crossing(false)) return true;
      break;
    }
    case Command::ScrollLineDown:  // j(2026-09-29、本の端では奥付等の横書きの範囲へ)
      if (!stepEpubVerticalFirst(1) && enterEpubHorizontalAtEdge(1)) return true;
      break;
    case Command::ScrollLineUp:  // k
      if (!stepEpubVerticalFirst(-1) && enterEpubHorizontalAtEdge(-1)) return true;
      break;
    case Command::PageDown:
      setEpubVerticalView(first + perScreen, cursor + perScreen);
      snapEpubVerticalToPage(true);
      if (crossing(true)) return true;
      break;
    case Command::PageUp:
      setEpubVerticalView(first - perScreen, cursor - perScreen);
      snapEpubVerticalToPage(false);
      if (crossing(false)) return true;
      break;
    // 2026-09-29、直接のユーザー指定("^[<`/`^[>`は、機能を逆にしたほうが明らかに
    // しっくりくる(末尾へは左で<、冒頭へは右方向で>)"): 縦書きでは向きが逆。
    case Command::GoToBottom:
      epubVerticalMark_ = cursor;
      setEpubVerticalView(0, 0);
      break;
    case Command::GoToTop:
      epubVerticalMark_ = cursor;
      // 2026-10-01、直接のユーザー報告("^[<で奥付の最下行にならない(横書きでは^[<は冒頭だが流れで
      // いくと末尾が自然)"): 本文の最後の列の先に横書きの範囲(奥付等)があれば、その最下行へ。
      if (enterEpubHorizontalAtBookEnd()) return true;
      setEpubVerticalView(static_cast<int>(epubVerticalCols_.size()), static_cast<int>(epubVerticalCols_.size()));
      break;
    case Command::ExchangeJumpMark: {
      // 横書きの範囲(奥付等)のリンクから飛んで来た時は、元の行(epubJumpRaw_)へ戻る
      if (!epubVerticalMark_ && epubJumpRaw_) {
        int raw = *epubJumpRaw_;
        epubJumpRaw_ = epubVerticalCurrentRaw();
        showEpubVerticalRaw(raw);
        if (epubHorizontalSection_) restoreEpubJumpOrigin();  // 横書きの範囲のリンクの所へ
        activeView().redraw();
        activeView().syncVisibleCursor();
        return true;
      }
      if (!epubVerticalMark_) return true;
      int target = *epubVerticalMark_;
      epubVerticalMark_ = cursor;
      setEpubVerticalView(target, target);
      break;
    }
    default:
      return std::nullopt;
  }
  epubVerticalView_.redraw();
  epubVerticalView_.syncVisibleCursor();  // -cの端末のカーソルを列カーソルの頭へ(setEpubVerticalView()参照)
  return true;
}

// -gのホイール/トラックパッド(app/main.cpp)から: delta列だけ横へ(正で先へ)。
bool Application::scrollEpubVertical(int delta) {
  if (!epubVerticalScrollActive()) return false;
  if (currentMode_ == ApplicationMode::Cut) {
    // 縦書きの章の一覧: 2026-09-30、直接のユーザー依頼("CUT風EPUB一覧にも慣性スクロールと
    // ルビを実装して"): 本文と同じく1列ずつ(ドラッグ・慣性・ホイール、app/main.cpp)。選ぶ章は
    // 本文の列カーソル(stepEpubVerticalFirst())と同じく送る向きの先頭――先へは画面の左端の
    // 列の章、前へは右端の列の章。キーの移動は従来どおり1章ずつ(handleEpubTocVerticalCommand())。
    int total = static_cast<int>(epubTocCols_.size());
    int perScreen = epubVerticalColumnsPerScreen(display_.columns());
    int first = std::clamp(epubTocFirst_ + delta, 0, std::max(0, total - perScreen));
    if (first == epubTocFirst_ || total == 0) return false;
    epubTocFirst_ = first;
    int col = delta > 0 ? std::min(first + perScreen, total) - 1 : first;
    int count = static_cast<int>(cutView_.lines().size());
    cutView_.setCursorIndex(std::clamp(epubTocCols_[col].rawLineNumber - 1, 0, std::max(0, count - 1)));
    refreshEpubTocVertical(/*keepFirst=*/true);
    epubTocVerticalView_.redraw();
    epubTocVerticalView_.syncVisibleCursor();
    return true;
  }
  if (!stepEpubVerticalFirst(delta)) return enterEpubHorizontalAtEdge(delta);
  epubVerticalView_.redraw();
  epubVerticalView_.syncVisibleCursor();  // -cの端末のカーソルを列カーソルの頭へ(setEpubVerticalView()参照)
  return true;
}

// application.hpp自身のepubTocCols_の宣言コメント参照。ネストした再分割の中は横書きのまま。
bool Application::epubTocVerticalActive() const {
  return cutOrigin_ == CutOrigin::Epub && epubBookVertical() && !cutMode_.parentPattern();
}

// cutView_の章の見出しを列に組み直し、選んでいる章が見える所に画面を置いて描く
// (描き直しは呼び出し元)。組版は安いので毎回組み直す(リサイズ・^Oeにも追従)。
// keepFirst(2026-09-30、scrollEpubVertical()の1列送り): 選んでいる章が画面の外へはみ出して
// いても、右端の列(epubTocFirst_)を動かさない。
void Application::refreshEpubTocVertical(bool keepFirst) {
  std::vector<EpubVerticalSourceLine> logical;
  const auto &labels = cutView_.lines();
  for (std::size_t i = 0; i < labels.size(); i++)
    logical.push_back({labels[i].text, labels[i].attributes, static_cast<int>(i) + 1});
  epubTocCols_ = layoutEpubVerticalColumns(logical, epubVerticalRows(), display_.isGraphical());
  int total = static_cast<int>(epubTocCols_.size());
  int segment = cutView_.cursorIndex();
  int cursorCol = 0;
  for (int c = 0; c < total; c++)
    if (epubTocCols_[c].rawLineNumber == segment + 1) {
      cursorCol = c;
      break;
    }
  int perScreen = epubVerticalColumnsPerScreen(display_.columns());
  int last = std::max(0, total - perScreen);
  int first = epubTocFirst_;
  if (!keepFirst && cursorCol < first) first = cursorCol;
  if (!keepFirst && cursorCol >= first + perScreen) first = cursorCol - perScreen + 1;
  epubTocFirst_ = std::clamp(first, 0, last);
  epubTocVerticalView_.setLines(renderEpubVerticalWindow(epubTocCols_, epubTocFirst_, epubVerticalRows(),
                                                         display_.columns(), !display_.isGraphical(), cursorCol));
  captureEpubVerticalEdges(epubTocVerticalView_, epubTocCols_, epubTocFirst_, cursorCol);
  epubTocVerticalView_.setCursorIndex(0);
  epubTocVerticalView_.setTopIndex(0);
  epubTocVerticalView_.setContentCursorColumn(
      std::max(0, epubVerticalScreenColumn(cursorCol - epubTocFirst_, display_.columns())));
  int count = static_cast<int>(labels.size());
  char position[64];
  std::snprintf(position, sizeof(position), "%02d/%02d", segment + 1, std::max(count, 1));
  epubTocVerticalView_.setPositionTextOverride(std::string(position));
}

void Application::selectEpubTocVertical(int segment) {
  int count = static_cast<int>(cutView_.lines().size());
  cutView_.setCursorIndex(std::clamp(segment, 0, std::max(0, count - 1)));
  refreshEpubTocVertical();
  epubTocVerticalView_.redraw();
  epubTocVerticalView_.syncVisibleCursor();
}

// 縦書きの章の一覧での移動: 本文の縦書き(handleEpubVerticalCommand())と同じ向き
// (←/^N/↓で次の章(左)、→/^P/↑で前の章、ESC >/ESC <で先頭/末尾)。PageDown/PageUpは
// 1画面分の列の先の章へ。それ以外はnullopt([Enter]・^G等は横書きの章の一覧と同じ)。
std::optional<bool> Application::handleEpubTocVerticalCommand(Command command) {
  int segment = cutView_.cursorIndex();
  int count = static_cast<int>(cutView_.lines().size());
  auto segmentAtColumn = [this](int c) {
    if (epubTocCols_.empty()) return 0;
    c = std::clamp(c, 0, static_cast<int>(epubTocCols_.size()) - 1);
    return epubTocCols_[c].rawLineNumber - 1;
  };
  int perScreen = epubVerticalColumnsPerScreen(display_.columns());
  int cursorCol = 0;
  for (int c = 0; c < static_cast<int>(epubTocCols_.size()); c++)
    if (epubTocCols_[c].rawLineNumber == segment + 1) {
      cursorCol = c;
      break;
    }
  switch (command) {
    case Command::MoveDown:
    case Command::ScrollLineDown:
      selectEpubTocVertical(segment + 1);
      return true;
    case Command::MoveUp:
    case Command::ScrollLineUp:
      selectEpubTocVertical(segment - 1);
      return true;
    case Command::PageDown:
      selectEpubTocVertical(std::max(segment + 1, segmentAtColumn(cursorCol + perScreen)));
      return true;
    case Command::PageUp:
      selectEpubTocVertical(std::min(segment - 1, segmentAtColumn(cursorCol - perScreen)));
      return true;
    case Command::GoToBottom:  // 本文の縦書きと同じく逆向き(handleEpubVerticalCommand()参照)
      selectEpubTocVertical(0);
      return true;
    case Command::GoToTop:
      selectEpubTocVertical(count - 1);
      return true;
    default:
      return std::nullopt;
  }
}

// 縦書きの章の一覧のクリック: 1回目はその章を選び、選んでいる章をもう一度クリック
// すると開く(横書きの一覧・本文の縦書きと同じ「1回目で選択、2回目で開く」)。
bool Application::clickEpubTocVertical(int row, int column) {
  (void)row;
  int k = epubVerticalColumnAt(column, display_.columns());
  long c = static_cast<long>(epubTocFirst_) + k;
  if (k < 0 || c >= static_cast<long>(epubTocCols_.size())) return false;
  int segment = epubTocCols_[c].rawLineNumber - 1;
  if (segment == cutView_.cursorIndex()) return openSelectedSegment();
  selectEpubTocVertical(segment);
  return true;
}

// K-100(^Oe、2026-09-29、直接のユーザー依頼"(ls)で^OeならARC、FILE履歴にも(unzip)付きで
// 残る。^Mで選んだらEPUB、FILE履歴には(epub)付きにすれば履歴一覧からモード種別を判断
// できる"): .epubをzipとしてARCで開く(.epubはConfigurationの組み込みの別名で.zip)。
// FILE履歴にはEPUBモードの「パス」と区別するため「パス/」で積む(fileHistoryBadges()が
// "(unzip)"を付け、openRecentFileEntry()/startResumingSession()がARCで開き直す)。
bool Application::openEpubAsArchive(const std::string &path) {
  if (currentMode_ == ApplicationMode::File) fileMode_.saveCursorPosition();
  if (!archiveMode_.loadArchive(path)) return false;
  archiveParentStack_.clear();
  currentMode_ = ApplicationMode::Archive;
  history_.pushRecent(RingCategory::RecentFile, path + "/");
  return true;
}

bool Application::openSelectedEpubAsArchive() {
  if (currentMode_ != ApplicationMode::Directory) return false;
  const DirectoryEntry *entry = directoryMode_.selectedEntry();
  if (!entry || entry->isDirectory || !isEpubFile(entry->name)) return false;
  std::string path = directoryMode_.currentDirectory();
  if (!path.empty() && path.back() != '/') path += '/';
  path += entry->name;
  directoryMode_.primaryAnnotations().pop();  // openSelectedEntry()と同じく、開いた時点で(ls)の注釈を消す
  return openEpubAsArchive(path);
}

// 縦書きの本文のクリック: 1回目はその列へ列カーソルを動かし、列カーソルの列を
// もう一度クリックするとactivateEpubVerticalColumn()(横書きのリンク行と同じ
// 「1回目で選択、2回目で開く」)。
bool Application::clickEpubVertical(int row, int column) {
  int k = epubVerticalColumnAt(column, display_.columns());
  long c = static_cast<long>(epubVerticalFirst_) + k;
  if (k < 0 || c >= static_cast<long>(epubVerticalCols_.size())) return false;
  // 2026-09-30、直接のユーザー依頼("目次一覧ではダブルクリックでないとタグジャンプしない。
  // シングルでは無理？"、"-gの時は左クリックで選べればいい"): リンク(下線)の字をクリックしたら、
  // そのリンクを選んで1回目で飛ぶ(画像は従来どおり2回目で開く)。
  const auto &cells = epubVerticalCols_[c].cells;
  if (row >= 0 && row < static_cast<int>(cells.size()) && cells[row].link >= 0) {
    setEpubVerticalView(epubVerticalFirst_, static_cast<int>(c), cells[row].link);
    return activateEpubVerticalColumn(c);
  }
  if (c == epubVerticalCursor_) return activateEpubVerticalColumn(c);
  setEpubVerticalView(epubVerticalFirst_, static_cast<int>(c));  // 1回目は列カーソルを動かすだけ
  epubVerticalView_.redraw();
  epubVerticalView_.syncVisibleCursor();  // -cの端末のカーソルを列カーソルの頭へ(setEpubVerticalView()参照)
  return true;
}

// 横書きのEPUBの本文を見せているか(縦書きの本の横書きの範囲を含む)。
bool Application::epubHorizontalLinksActive() const {
  return currentMode_ == ApplicationMode::File && isEpubFile(fileMode_.currentFile()) && !epubVerticalActive() &&
         !promptActive();
}

// 画面に見えている行のリンク(下線の並び)を上から順に(epubHorizontalLinks())。
std::vector<Application::EpubHorizontalLink> Application::epubHorizontalVisibleLinks() const {
  int top = fileView_.topIndex();
  return epubHorizontalLinks(top, top + std::max(1, fileView_.height()));
}

// fileView_の行[from, to)のリンク(下線の並び)を上から順に。番号(ordinal)は元の行の頭から数える
// (折り返した行をまたぐ下線は同じリンク)。見せない行(ContentView::setShownIndexRange())は除く。
std::vector<Application::EpubHorizontalLink> Application::epubHorizontalLinks(int from, int to) const {
  std::vector<EpubHorizontalLink> out;
  const auto &lines = fileView_.lines();
  int total = static_cast<int>(lines.size());
  int top = std::max(0, from);
  int end = std::min(total, to);
  if (top >= total || top >= end) return out;
  auto rawOf = [&](int i) { return lines[i].rawLineNumber.value_or(-1); };
  int start = top;
  while (start > 0 && rawOf(start - 1) == rawOf(top)) start--;
  int currentRaw = -2, ordinal = -1;
  bool previousUnderline = false;
  for (int i = start; i < end; i++) {
    int raw = rawOf(i);
    if (raw != currentRaw) {
      currentRaw = raw;
      ordinal = -1;
      previousUnderline = false;
    }
    std::vector<std::pair<int, int>> runs;
    for (const auto &run : lines[i].attributes)
      if (run.attribute == Display::Attribute::Underline && run.end > run.start) runs.push_back({run.start, run.end});
    std::sort(runs.begin(), runs.end());
    std::vector<std::pair<int, int>> merged;
    for (const auto &run : runs) {
      if (!merged.empty() && run.first <= merged.back().second) merged.back().second = std::max(merged.back().second, run.second);
      else merged.push_back(run);
    }
    const std::string &text = lines[i].text;
    std::size_t firstVisible = text.find_first_not_of(' ');
    int firstByte = firstVisible == std::string::npos ? 0 : static_cast<int>(firstVisible);
    for (std::size_t k = 0; k < merged.size(); k++) {
      bool continues = k == 0 && previousUnderline && merged[k].first <= firstByte;
      if (!continues) ordinal++;
      if (i >= top && fileView_.isIndexShown(i)) out.push_back({i, merged[k].first, merged[k].second, raw, ordinal});
    }
    std::size_t lastVisible = text.find_last_not_of(' ');
    int endByte = lastVisible == std::string::npos ? 0 : static_cast<int>(lastVisible) + 1;
    previousUnderline = !merged.empty() && merged.back().second >= endByte;
  }
  return out;
}

// 横書きのEPUBの本文で、次(delta>0)/前のリンクを選ぶ(↑↓・^N/^P)。本文全体で次/前のリンクを
// 探し、画面に見えていれば選び、見えていなければ1行スクロールする(繰り返せば、先のリンクが画面に
// 入った所で選ばれる)。2026-09-30訂正、直接のユーザー報告("↓↑でリンク位置は変わるけど画面が
// スクロールしない"): 以前は画面の中のリンクだけを見て、尽きたら従来の動き(見えないカーソルの
// 移動)に任せていた――選んだ時にカーソルをリンクの行へ動かしていたので、画面の端に届くまで
// スクロールしなかった。カーソルは動かさない。本にリンクが1つも無ければfalse(従来どおり)。
bool Application::selectAdjacentEpubHorizontalLink(int delta) {
  if (!epubHorizontalLinksActive() || fileMode_.epubLayoutHints().lineLinks.empty()) return false;
  int total = static_cast<int>(fileView_.lines().size());
  int top = fileView_.topIndex();
  int bottom = std::min(total, top + std::max(1, fileView_.height()));
  auto before = [](const EpubHorizontalLink &a, const EpubHorizontalLink &b) {
    return a.index < b.index || (a.index == b.index && a.start < b.start);
  };
  std::optional<EpubHorizontalLink> current;
  if (epubHorizontalLink_)
    for (const auto &link : epubHorizontalVisibleLinks())
      if (link.index == epubHorizontalLink_->index && link.start == epubHorizontalLink_->start) current = link;
  // カーソルの行(選んでいるリンクがあればそのリンク)から次/前を探す
  int cursor = fileView_.cursorIndex();
  EpubHorizontalLink here = current ? *current : EpubHorizontalLink{cursor, delta > 0 ? 1 << 30 : -1, 0, 0, 0};
  std::optional<EpubHorizontalLink> target;
  if (delta > 0) {
    for (const auto &link : epubHorizontalLinks(here.index, total))
      if (before(here, link)) {
        target = link;
        break;
      }
  } else {
    for (const auto &link : epubHorizontalLinks(0, here.index + 1))
      if (before(link, here)) target = link;
  }
  if (target && target->index >= top && target->index < bottom) {
    epubHorizontalLink_ = target;
    fileView_.setCursorIndex(target->index);  // 選ぶリンクはカーソルの行のもの(syncEpubHorizontalPresentation())
  } else if (delta > 0) {
    fileView_.scrollLineDown();
  } else {
    fileView_.scrollLineUp();
  }
  activeView().redraw();  // syncEpubHorizontalPresentation()が反転を付ける(画面から外れたら外す)
  return true;
}

// リンクの行き先(EpubLayoutHints::lineLinks)へ: 本の中なら飛び(^X^Xで戻れる)、Webならクイックルック。
bool Application::activateEpubHorizontalLink(const EpubHorizontalLink &link) {
  const auto &lineLinks = fileMode_.epubLayoutHints().lineLinks;
  auto it = lineLinks.find(link.raw);
  if (it == lineLinks.end() || link.ordinal < 0 || link.ordinal >= static_cast<int>(it->second.size())) return false;
  const std::string &target = it->second[link.ordinal];
  if (target.compare(0, 4, "url:") == 0) return openEpubWebLink(target.substr(4));
  if (target.compare(0, 5, "line:") != 0) return false;
  int raw = std::atoi(target.c_str() + 5);
  epubJumpOrigin_ = EpubJumpOrigin{link, fileView_.topIndex()};
  epubHorizontalLink_.reset();
  if (epubBookVertical()) {  // 縦書きの本の奥付等から: 縦書き(か横書きの範囲)へ
    // 2026-09-30、直接のユーザー報告("リンクへ飛んだ先から^X^Xで戻れないので飛ぶ前に位置を保存
    // して"): 飛ぶ前のリンクの行を覚え、^X^Xで戻る(epubJumpRaw_、縦書きの列のepubVerticalMark_は
    // 横書きの範囲の位置を表せない)。
    epubJumpRaw_ = link.raw;
    epubVerticalMark_.reset();
    showEpubVerticalRaw(raw);
    activeView().redraw();
    activeView().syncVisibleCursor();
    return true;
  }
  fileView_.setCursorIndex(link.index);  // ^X^X(ContentView::markJumpOrigin())で戻るのはリンクの行
  return fileMode_.jumpToEpubRawLine(raw);
}

// 2026-09-30、直接のユーザー報告("^X^XはOKじゃなかった。「あとがき」のようにちゃんとリンクへ
// 戻して"): ^X^Xで横書きのリンクの行へ戻ったら、飛ぶ前の画面の位置に戻し、そのリンクを選んだ
// 状態にする(戻った先がそのリンクの行の時だけ)。
bool Application::restoreEpubJumpOrigin() {
  if (!epubJumpOrigin_) return false;
  const auto &lines = fileView_.lines();
  const EpubHorizontalLink &link = epubJumpOrigin_->link;
  if (link.index < 0 || link.index >= static_cast<int>(lines.size()) ||
      lines[link.index].rawLineNumber.value_or(-1) != link.raw)
    return false;
  fileView_.setTopIndex(epubJumpOrigin_->top);
  fileView_.setCursorIndex(link.index);
  fileView_.setTopIndex(epubJumpOrigin_->top);
  epubHorizontalLink_ = link;
  activeView().redraw();
  return true;
}

bool Application::activateSelectedEpubHorizontalLink() {
  if (!epubHorizontalLink_ || !epubHorizontalLinksActive()) return false;
  EpubHorizontalLink link = *epubHorizontalLink_;
  return activateEpubHorizontalLink(link);
}

// 行indexの画面の桁columnが下線の字なら、そのリンクを選んで飛ぶ(-gのクリック)。
bool Application::clickEpubHorizontalLink(int index, int column) {
  if (!epubHorizontalLinksActive()) return false;
  const std::string &text = fileView_.lines()[index].text;
  int byte = -1;
  for (std::size_t pos = 0, col = 0; pos < text.size();) {
    int len = std::max(1, Utf8::sequence_length(static_cast<unsigned char>(text[pos])));
    int width = Utf8::display_width(reinterpret_cast<const unsigned char *>(text.data()) + pos, len);
    if (column >= static_cast<int>(col) && column < static_cast<int>(col) + std::max(width, 1)) {
      byte = static_cast<int>(pos);
      break;
    }
    col += static_cast<std::size_t>(std::max(width, 0));
    pos += static_cast<std::size_t>(len);
  }
  if (byte < 0) return false;
  for (const auto &link : epubHorizontalVisibleLinks())
    if (link.index == index && byte >= link.start && byte < link.end) {
      epubHorizontalLink_ = link;
      return activateEpubHorizontalLink(link);
    }
  return false;
}

// 同じ行の次(delta>0)/前のリンクを選び、そのリンクの最初の字の列へ列カーソルを動かす
// (setEpubVerticalView()のepubVerticalLink_参照、^N/^P等)。行にリンクが2つ以上あり、次/前が
// ある時だけtrue(無ければ呼び出し元が従来どおり列を動かす)。
bool Application::selectAdjacentEpubVerticalLink(int delta) {
  if (epubVerticalLink_ < 0 || epubVerticalCursor_ < 0 ||
      epubVerticalCursor_ >= static_cast<int>(epubVerticalCols_.size()))
    return false;
  int raw = epubVerticalCols_[epubVerticalCursor_].rawLineNumber;
  const auto &lineLinks = fileMode_.epubLayoutHints().lineLinks;
  auto it = lineLinks.find(raw);
  if (raw != epubVerticalLinkRaw_ || it == lineLinks.end() || it->second.size() < 2) return false;
  int next = epubVerticalLink_ + delta;
  if (next < 0 || next >= static_cast<int>(it->second.size())) return false;
  for (int c = 0; c < static_cast<int>(epubVerticalCols_.size()); c++) {
    if (epubVerticalCols_[c].rawLineNumber != raw) continue;
    for (const auto &cell : epubVerticalCols_[c].cells)
      if (cell.link == next) {
        setEpubVerticalView(epubVerticalFirst_, c, next);
        epubVerticalView_.redraw();
        epubVerticalView_.syncVisibleCursor();
        return true;
      }
  }
  return false;
}

// 列cのリンク先の行(EpubVerticalTarget::link)か画像の行のテキスト(image)。どちらも
// 無ければnullopt。
std::optional<Application::EpubVerticalTarget> Application::epubVerticalColumnTarget(long c) const {
  if (c < 0 || c >= static_cast<long>(epubVerticalCols_.size())) return std::nullopt;
  int raw = epubVerticalCols_[c].rawLineNumber;
  // リンクのある行は、選んでいるリンク(setEpubVerticalView()のepubVerticalLink_)の行き先
  const auto &lineLinks = fileMode_.epubLayoutHints().lineLinks;
  if (auto it = lineLinks.find(raw); it != lineLinks.end()) {
    int selected = raw == epubVerticalLinkRaw_ ? epubVerticalLink_ : -1;
    if (selected < 0 || selected >= static_cast<int>(it->second.size())) return std::nullopt;
    const std::string &target = it->second[selected];
    if (target.compare(0, 5, "line:") == 0) return EpubVerticalTarget{std::atoi(target.c_str() + 5), {}, {}};
    if (target.compare(0, 4, "url:") == 0) return EpubVerticalTarget{std::nullopt, {}, target.substr(4)};
    return std::nullopt;
  }
  for (const auto &line : fileView_.lines())
    if (line.rawLineNumber == raw && line.text.find("[image: ") != std::string::npos)
      return EpubVerticalTarget{std::nullopt, line.text, {}};
  return std::nullopt;
}

// 2026-09-29追加、直接のユーザー報告("-c起動では「まえがき」へ到達できず選べない"):
// -cはマウスを追わない(app/main.cppのsyncMouseTracking())ので、縦書きのリンク/画像は
// キーボードでも選べるようにする。列カーソル(setEpubVerticalView())の列にリンクが
// あればリンク先へ(^X^Xで戻れる)、画像("[image: ...]")があればクイックルック。
bool Application::activateEpubVerticalColumn(long c) {
  auto target = epubVerticalColumnTarget(c);
  if (!target) return false;
  if (!target->url.empty()) return openEpubWebLink(target->url);
  if (target->link) {
    epubVerticalMark_ = epubVerticalCursor_;
    epubJumpRaw_.reset();
    showEpubVerticalRaw(*target->link);
    // 2026-09-30、直接のユーザー報告("目次一覧の「第20章」の隣「奥付」を^Mしても何も起きない"):
    // リンク先が横書きの範囲(奥付等)なら見せる画面はfileView_になるので、epubVerticalView_では
    // なく今の画面を描く。
    activeView().redraw();
    activeView().syncVisibleCursor();  // -cの端末のカーソルを列カーソルの頭へ(setEpubVerticalView()参照)
    return true;
  }
  return openEpubImageIn(target->imageLine);
}

// K-98(^Je、2026-09-28): EPUBのパスを入力して開く(openEpub())。(ls)で.epubを
// 選んでいればその名前を入れておく。K-99(^J^E): FILE履歴のうちEPUBだけの一覧
// (選ぶとopenRecentFileEntry()→openEpub())。縦横は本が決める(openEpub()参照)。
bool Application::startEpubPrompt() {
  std::string seed;
  if (currentMode_ == ApplicationMode::Directory)
    if (const DirectoryEntry *entry = directoryMode_.selectedEntry(); entry && isEpubFile(entry->name))
      seed = entry->name;
  epubPathPending_ = true;
  activatePrompt(PromptKind::PathInput, seed);
  return true;
}

bool Application::startEpubList() {
  epubListPending_ = true;
  bool ok = startRegexPromptList(PromptKind::RecentFile);
  epubListPending_ = false;
  return ok;
}

// ringCategoryLabel(listBrowserCategory_)に、^J^E(EPUBだけのFILE履歴)の"EPUB"を足したもの。
std::string Application::listBrowserCategoryLabel() const {
  if (promptHistoryContext_ && promptHistoryContext_->epubOnly) return "EPUB";
  return ringCategoryLabel(listBrowserCategory_);
}

bool Application::returnToDirectory() {
  fileMode_.saveCursorPosition();
  // 2026-08-16修正: これは以前は、directoryMode_自身のcurrentDirectory_
  // が既にファイルの格納先ディレクトリだと信頼して、単にcurrentMode_
  // を切り替えdirectoryView_を「そのまま」再描画していた――通常の
  // Directoryへ降りてからEnterするというセッションについては真だが、
  // ^X^F/RecentFile経由で到達したファイルについては真では「ない」
  // (openSelectedListEntry()自身のRecentFileの分岐は
  // directoryMode_.enterDirectory()を一切呼ばないので、^X^Fより「前に」
  // たまたま表示されていたディレクトリが、^Gの後も表示され続ける)。
  // 実際のユーザー報告+自身の診断がこれを正確に突き止めた:
  // "=bo→[return]=bq→^G...結果は起動直後の（ls）なので「それ/
  // BDF_Driver...」を表示しようとしてんじゃないの？"――legacy自身の
  // 実際の挙動に対して確認済み(=br: ファイルの実際のLibraries/
  // ディレクトリに着地する)、そしてgoToFileSelector()自身の同一の
  // enterDirectory()+resetNavigationToFloor()のペアリングは、^J^Kの
  // ケースについてこの正確な修正を既に確立していた;単にここでは
  // プレーンな^Gについて欠けていただけである。通常の降下のケース
  // (親は既にcurrentDirectory_と一致する)についてはno-opなので、
  // これはそこでの既存のカーソル位置の挙動には触れない。
  // 2026-08-18修正: 現在ロードされているファイルが^Tバナー自身の
  // 合成されたファイル(bannerFilePath()、executeShowUsilicBanner()自身の
  // コメント参照)である時は、すぐ下の「ファイルの実際の親ディレク
  // トリをたどる」修正をスキップする――その修正は^X^F/RecentFile
  // (ユーザーが意図的に選んだ実際のファイル)については正しいが、
  // それ自身のディレクトリが実質的にユーザーが実際に行き着きたい
  // 場所であることが決して無い、使い捨てのステータス画面については
  // 間違っている(これがまさに、ここでdirectoryMode_に手を付けない
  // ままにすることが重要な理由である: これが、バナーからの^Gを、
  // バナーファイル自体がたまたま存在する場所ではなく、^Tが実際に
  // 「呼び出された」場所へ戻して着地させるものである――実際の報告
  // "戻り値も...^Tを起動したモードのはず"は、bannerFilePath()が
  // tempRootDirectory_を指すのをやめた時点で、これが既に意図された
  // 挙動だったことを確認した、その関数自身のコメント参照)。別個の
  // 状態を持つフラグではなく、直接のパス比較にしてある: これ1つ
  // だけが設定するフラグをクリアするのを覚えておく必要が、(約10個
  // ある呼び出し箇所の)それぞれに生じるのではなく、他のどんな
  // ファイルがロードされた瞬間にも自己修正される。
  if (fileMode_.currentFile() != bannerFilePath()) {
    std::filesystem::path filePath(fileMode_.currentFile());
    std::string parent = filePath.parent_path().string();
    if (!parent.empty() && parent.back() != '/') parent += '/';
    if (!parent.empty() && parent != directoryMode_.currentDirectory()) {
      std::string previousDirectory = directoryMode_.currentDirectory();
      if (directoryMode_.enterDirectory(parent) && !previousDirectory.empty())
        directoryMode_.resetNavigationToFloor(previousDirectory);
    }
  }
  currentMode_ = ApplicationMode::Directory;
  // Fileモード自身のビューは今や本物の可視カーソルを設定する
  // (ContentView::syncVisibleCursor()自身のコメント参照)――これが
  // 無いと、directoryView_.redraw()は意図的にDisplayの可視カーソル
  // の状態自体には決して触れないので(Application::returnFromShell()
  // 自身のhideVisibleCursor()呼び出しと同じ理屈)、それを置き換える
  // ディレクトリ一覧の上に浮き続けてしまう。
  display_.hideVisibleCursor();
  directoryView_.redraw();
  return true;
}

// 2026-08-11修正: Command::Enter自身のswitchには、そもそもGrepの
// ケースが一切無かった(実際の報告: FGREP/EGREPの結果でEnterを押して
// も何も起きない、"反応無し")。mode.cのT_grepケースから移植: 検索が
// 「実行された」場所(grepReturnMode_、returnFromGrep()自身の^G/Back
// の経路が既に使っているのと同じ対象)へ跳んで戻る、検索が始まる前
// にいた場所にそのままにしておくのではなく、選択されたマッチが
// 由来する「元の」行(GrepMatch::sourceLineIndex、GrepMode::search()
// 内のfgrep/egrep自身の`-n`行番号出力からパース済み)にカーソルを
// 着地させる――その後、一覧全体の中でマッチしたエントリがハイライト
// されていることを示す実際のCUIスクリーンショットに対して確認済み。
// Directoryのケースについて特に、legacy自身のlp->prevの挙動に合わせ、
// "FGREP: pattern"というモードラインの注釈(DirectoryMode::
// setSearchAnnotation()自身のコメント参照)も記録する。
bool Application::openSelectedGrepMatch() {
  const GrepMatch *match = grepMode_.selectedMatch();
  if (!match) return false;
  currentMode_ = grepReturnMode_;
  ContentView &target = activeView();
  target.setCursorIndex(match->sourceLineIndex);
  if (grepReturnMode_ == ApplicationMode::Directory) {
    directoryMode_.primaryAnnotations().push(DirectoryMode::DirAnnotationKind::Grep,
                                              std::string(grepSyntaxLabel(grepSyntax_)) + ": " + grepMode_.pattern());
    // EXEC(2026-08-24追加、openSelectedLocateResult()自身のコメント
    // と同じユーザー依頼): ここでのマッチは既に実際のディレクトリの
    // エントリに対応している(sourceLineIndexがdirectoryMode_.entries()
    // と1対1で並ぶことについてはexecuteQuickLook()自身のコメント参照)
    // ――もしそれが拡張子にマップされたEXECファイルなら、上の(ls)行
    // に着地して戻った後に2回目のEnterを要求するのではなく、即座に
    // それを起動する。
    const auto &entries = directoryMode_.entries();
    if (match->sourceLineIndex >= 0 && static_cast<std::size_t>(match->sourceLineIndex) < entries.size()) {
      const DirectoryEntry &entry = entries[match->sourceLineIndex];
      if (!entry.isDirectory) {
        std::string path = directoryMode_.currentDirectory();
        if (!path.empty() && path.back() != '/') path += '/';
        path += entry.name;
        if (configuration_.isExec(path)) {
          if (auto command = configuration_.programForSuffix(path)) execMode_.run(*command, path);
        }
      }
    }
  } else if (grepReturnMode_ == ApplicationMode::File) {
    // 2026-08-28追加、直接のユーザー報告+正確な再現("CLAUDE.mdを
    // 開いた状態で^J^S...[return]で最上行を選択→戻り値が
    // 「~/git/usilic/」のまま。検索結果一覧へ戻るのが筋"): 上の
    // Directoryの分岐を反映しているが、directoryView_ではなくfileView_
    // に着地するだけである――なぜこれがsetSearchOrigin()(ファイル
    // 自身の名前にはそのまま手を付けない)とは別個の仕組みなのかに
    // ついてはFileMode::setSearchAnnotation()自身のコメント参照。
    // fileReturnMode_ = Grep(新しい値、それ自身のCommand::Backの処理
    // 参照)なので、ここからの^GはreturnToDirectory()へ流れ落ちる
    // のではなく、結果リストへ再入場する。
    // 2026-08-28の訂正、正確な変更前/後のモードライン値を伴う直接
    // のユーザーの追跡("EGREP: ^-"(不良) vs "- レガシーC実装...
    // src/config.sh"(正解)): ここでのtrailing2の値は間違っていた
    // ――マッチ自身の「行のテキスト」こそが、実際にそこへ着地した後に
    // 有用で重複しない情報である(既に"^-"を検索したことは分かって
    // いる)、"EGREP: pattern"というラベルの繰り返しではない――「それ」
    // は代わりに結果リストの画面に属する(grepView_自身のプロバイダ、
    // それ自身の同一の訂正参照)。
    fileMode_.annotations().push(FileMode::FileAnnotationKind::Search, match->text);
    fileReturnMode_ = ApplicationMode::Grep;
  } else if (grepReturnMode_ == ApplicationMode::Man) {
    // 2026-09-19四訂正、直接のユーザー提案("フラグはなるべく使いたく
    // ない...スタックへの積み降ろしでは対応できない？"):
    // application.hpp自身のmanReturnStack_宣言コメント参照。
    // manReturnStack_自体(の先頭=本来の基点)には一切触れず、単に
    // Grepを1つpush_back()するだけ――複数回ネストしても(例えば
    // マッチへ着地→そこから「さらに」別のGREPをしてまた別のマッチへ
    // 着地)、returnFromMan()が1段ずつ正しくpop_back()できる。
    manReturnStack_.push_back(ApplicationMode::Grep);
  } else if (grepReturnMode_ == ApplicationMode::Locate) {
    // 上と同じ理屈、LOCATE版。
    locateReturnStack_.push_back(ApplicationMode::Grep);
  } else if (grepReturnMode_ == ApplicationMode::Du) {
    // 2026-09-19五訂正、直接のユーザー依頼("DUの修正と、戻り値の
    // 仕組みが無いようなのでMAN/LOCATEと同じ仕様にしてほしい")。
    // 上と同じ理屈、DU版。
    duReturnStack_.push_back(ApplicationMode::Grep);
  } else if (grepReturnMode_ == ApplicationMode::Shell) {
    // 2026-09-20追加、直接のユーザー報告("ps等実行後、^Sps^Mでせっかく
    // プロンプト位置へ移動するのに^Mで無反応は残念"): 上のtarget.
    // setCursorIndex()は既にカーソルをマッチ行(過去のプロンプト行)へ
    // 動かしていたが、Shell::historyBrowsing()自身のフラグには一切
    // 触れていなかった――^P/^Nによる通常の履歴閲覧はbeginHistoryBrowsing()
    // 経由でこれを立てる(Shell::beginHistoryBrowsing()自身の宣言
    // コメント参照)ことで、Enter(feedShellByte()自身のkCarriageReturn
    // ケース)が「ハイライトされた行のコマンドを再実行する」意味に
    // なる――しかしGREPのマッチ経由でここへ着地した場合はこのフラグが
    // 立たないままだったため、続けてEnterを押しても、まだ空の生きた
    // プロンプト行(commandLine_)をただ投入するだけの、それ以外の
    // 通常のEnterの意味にしかならず、見た目には何も起きなかった
    // (「無反応」)。^P/^Nと全く同じ「閲覧中」状態にすることで、この
    // 後のEnterも同じ意味を持つようにする。
    // 2026-09-11追記: Basic構文(^S)のShell起点は今やin-place方式に
    // なり、currentMode_がGrepへ一度も変わらないため、この関数自体が
    // 呼ばれなくなった――同じshell_.beginHistoryBrowsing()の呼び出しは
    // activateGrepInPlace()/jumpToGrepInPlaceMatch()自身に複製してある。
    // ここに残っているのは、Shell起点のFGREP(^O^S)/EGREP(^J^S)が今も
    // 従来通りの一覧モードのままで、この関数を経由し続けるため。
    shell_.beginHistoryBrowsing();
  }
  // 2026-08-28修正、直接のユーザー報告("検索前には画面にあった
  // ブロックカーソルが検索一覧のどれかを[return]後の画面では表示
  // されない...どれがマッチ行かわかりづらい"): hideVisibleCursor()
  // はここでは無条件だった、Directoryのケース(それ自身の行全体の
  // ハイライトが既に現在のエントリを示すので、端末のカレットは不要
  // ――returnToDirectory()の同一の非表示参照)には合っているが、
  // それ自身の他のハイライトを何も持たない、プレーンなコンテンツ
  // ビューであるFile/CutContentについては間違っている(それ以外の
  // 方法で示すための行全体のハイライトが無いことについてはFileMode::
  // loadFile()自身のコメント参照)――通常のコンテンツについては
  // cursorHighlightEnabled_はデフォルトでオフなので(loadFile()自身の
  // コメント)、redraw()だけではこれらについてもカレットを再びオンに
  // しない、そのためここでの明示的なsyncVisibleCursor()呼び出しが無い
  // と、マッチした行には目に見えるマーカーが全く残らなかった。
  if (grepReturnMode_ == ApplicationMode::Directory) {
    display_.hideVisibleCursor();
    target.redraw();
  } else {
    target.redraw();
    target.syncVisibleCursor();
  }
  return true;
}

// activateGrepInPlace()/deactivateGrepInPlace()/jumpToGrepInPlaceMatch()
// 自身の宣言コメント(application.hpp)参照。design.mdの経緯:
// .steering/20260909-grep-inline-highlight/。
namespace {
// activateGrepInPlace()自身のコメント参照(2026-09-11追加): GrepMatch
// (grep_mode.hpp)からContentView::GrepHighlightMatchへの変換――
// ContentViewはGrepModeより下位のレイヤーなので、GrepMatch自体を
// 型として持ち込まない(ContentView::GrepHighlightMatch自身のコメント
// 参照)。
std::vector<ContentView::GrepHighlightMatch> toHighlightMatches(const std::vector<GrepMatch> &matches) {
  std::vector<ContentView::GrepHighlightMatch> out;
  out.reserve(matches.size());
  for (const auto &m : matches) out.push_back({m.sourceLineIndex, m.matchSpans});
  return out;
}
}  // namespace

// buildGrepInPlaceOccurrences()自身の宣言コメント(application.hpp、
// Application::GrepOccurrence自身の宣言コメント)参照。GrepMatchは
// 「行」単位(1件=1行、その行内の複数一致は同じmatchSpansへまとめて
// 入る)なので、ここでmatchSpansの要素それぞれを1件のGrepOccurrence
// (出現)へ展開する――^N/^Pのナビゲーション単位を「行」から「一致した
// 個々の出現位置」へ細かくするための変換。
std::vector<Application::GrepOccurrence> Application::buildGrepInPlaceOccurrences(
    const std::vector<GrepMatch> &matches) const {
  std::vector<GrepOccurrence> out;
  for (const auto &m : matches) {
    if (m.matchSpans.empty()) {
      // ContentView::setGrepMatchHighlight()自身のフォールバックと
      // 同じ理屈(GrepMatch::matchSpans自身のコメント参照、取得に
      // 失敗した場合に行全体をハイライトする)――ナビゲーションでも
      // 行全体を1件の出現として扱う。
      out.push_back({m.sourceLineIndex, {0, static_cast<int>(m.text.size())}});
    } else {
      for (const auto &span : m.matchSpans) out.push_back({m.sourceLineIndex, span});
    }
  }
  return out;
}

bool Application::activateGrepInPlace() {
  // grepInPlaceShellWasBrowsing_自身の宣言コメント参照(application.hpp)
  // ――このセッションが「最初の」activationかどうかを、下でtargetの
  // カーソルが動かされる前に判定しておく必要がある。
  bool wasAlreadyActive = grepInPlaceActive_;
  const auto &matches = grepMode_.matches();
  if (matches.empty()) {
    // 2026-09-10修正、直接のユーザー報告(usilic -c git/usilicで再現:
    // "^[i.→さらに␣→.␣はマッチしないのにハイライトが消えない"):
    // ESC iはキー入力のたびにこの関数を呼び直す――前回のキー入力で
    // 一致があった(grepInPlaceActive_==trueになっていた)後、今回の
    // キー入力で0件になった場合、この早期returnがそれまでの
    // オーバーレイ/ハイライトを一切消さないまま素通りしていた
    // (grepInPlaceActive_もtrueのまま残っていた)。deactivateGrepInPlace()
    // 自身は非activeなら何もしないので、ここで無条件に呼んでおけば、
    // 「一度も一致したことが無い」通常の新規検索(このパスがそもそも
    // 意図していたケース)には影響せず、このインクリメンタルサーチ
    // 特有のケースだけを正しく後始末できる。
    deactivateGrepInPlace();
    std::string message = grepMode_.pattern() + ": No String";
    // 2026-09-11追加、直接のユーザー依頼のFGREP/EGREP統一(ContentView::
    // setGrepInPlaceLabel()自身のコメント参照): 各ModeLineFieldsProvider
    // がgrepNoMatchActive()の間に出すname欄も、grepSyntax_に応じた
    // 正しいラベル("FGREP"/"EGREP"/"GREP")にする。
    // 2026-09-12追加、同じ理由でtrailing2欄のバッジも(ContentView::
    // setGrepInPlaceBadge()自身のコメント参照)。
    for (ContentView *view : grepModeLineViews(grepInPlaceOriginMode_)) {
      view->setGrepInPlaceLabel(grepSyntaxLabel(grepSyntax_));
      view->setGrepInPlaceBadge(grepSyntaxBadge(grepSyntax_));
      view->reportGrepNoMatchMessage(message);
    }
    activeView().redraw();
    return false;
  }
  grepInPlaceMatches_ = matches;
  grepInPlaceOccurrences_ = buildGrepInPlaceOccurrences(matches);
  grepInPlaceIndex_ = 0;
  grepInPlaceActive_ = true;
  // 2026-09-10追加、直接のユーザー指摘("^N連打で最下行へ到達すると
  // 循環処理により最上行へと移動するのはいいとして、動きが今イチ
  // わかりにくい...EGREPと同じく行数情報を含ませて欲しい"): EGREP
  // (一覧モード)では、一覧自体の行がマッチそのものなので、通常の
  // position表示("現在行/総行数")がそのまま「マッチN/M」を意味する。
  // in-place方式ではDirectory/Fileの生のコンテンツがそのまま画面に
  // 残るため、通常のposition表示は(検索と無関係な)「カーソル行/
  // ファイル総行数」のままで、^N/^Pでマッチ間を移動しても何マッチ中
  // 何番目かが伝わらなかった。setPositionTextOverride()で明示的に
  // 「マッチ番号/総マッチ数」に差し替える――jumpToGrepInPlaceMatch()
  // 側でも同じ書式で更新し、deactivateGrepInPlace()で通常表示へ戻す。
  updateGrepInPlacePositionText();
  // 2026-09-10追加、直接のユーザー指摘("とにかくハイライト表示は
  // 出てくれないと...何にヒットしたかもわからない"): 全マッチを
  // 同時にハイライト表示する(ContentView::setGrepMatchHighlight()
  // 自身のコメント参照)――マッチ集合全体を常に見渡せるようにする。
  // matchSpans(2026-09-10再改訂、"『マッチした文字列』だけを")付きで
  // そのまま渡す。2026-09-19追記(GrepOccurrence自身のコメント参照):
  // このうち現在位置の1件(grepInPlaceOccurrences_[0])だけは
  // HighlightCurrentで区別する――以前はここが「カーソルが今どの
  // マッチにいるかとは独立」だったが、^N/^Pが出現単位になったことで
  // 「独立」ではいられなくなった。
  // 2026-09-11、直接のユーザー依頼("^Sと^[iをFILE/DIR以外へも反映して
  // ほしい")によりDirectory/Fileの二択からviewForMode()経由の汎用呼び
  // 出しへ一般化した(ContentView自身のGREP関連メソッド参照)。
  ContentView &target = grepViewForMode(grepInPlaceOriginMode_);
  // grepInPlaceShellWasBrowsing_自身の宣言コメント参照――最初の
  // activationでだけ、下のtarget.setCursorIndex()等で上書きされる
  // 「前」の、検索開始時点そのままのShellの状態を退避する。
  if (grepInPlaceOriginMode_ == ApplicationMode::Shell && !wasAlreadyActive) {
    grepInPlaceShellWasBrowsing_ = shell_.historyBrowsing();
    grepInPlaceShellPriorCursorIndex_ = target.cursorIndex();
  }
  // 2026-09-11追加、直接のユーザー依頼のFGREP/EGREP統一(ContentView::
  // setGrepInPlaceLabel()自身のコメント参照): 各ModeLineFieldsProvider
  // がgrepInPlaceActive()の間に出すname欄を、grepSyntax_に応じた正しい
  // ラベル("FGREP"/"EGREP"/"GREP")にする。
  // 2026-09-12追加、同じ理由でtrailing2欄のバッジも(ContentView::
  // setGrepInPlaceBadge()自身のコメント参照)。
  for (ContentView *view : grepModeLineViews(grepInPlaceOriginMode_)) {
    view->setGrepInPlaceLabel(grepSyntaxLabel(grepSyntax_));
    view->setGrepInPlaceBadge(grepSyntaxBadge(grepSyntax_));
    view->setGrepInPlaceOverlay(true);
  }
  target.setGrepMatchHighlight(toHighlightMatches(grepInPlaceMatches_), grepInPlaceOccurrences_[0].sourceLineIndex,
                                grepInPlaceOccurrences_[0].span);
  target.setCursorIndex(grepInPlaceOccurrences_[0].sourceLineIndex);
  target.centerOnCursor();
  if (!redrawEpubVerticalForGrep(target)) target.redraw();
  // syncVisibleCursor()自身の宣言コメント参照: cursorHighlightEnabled()
  // がfalseのモード(File/Man/Shell等、行全体の反転表示によるカーソル
  // 表現を持たない)だけで必要――2026-09-11、Directory/Fileの二択判定
  // から、この既存の汎用フラグを使う判定へ一般化した。
  if (!target.cursorHighlightEnabled()) target.syncVisibleCursor();
  // openSelectedGrepMatch()自身のShell分岐のコメント参照(2026-09-11
  // 追記): Basic構文のShell起点は今やin-place方式であり、あの関数自体
  // (currentMode_==Grepの時だけ呼ばれる)を経由しないため、同じ
  // shell_.beginHistoryBrowsing()呼び出しをここに複製する――さもないと
  // マッチへ着地してもEnterが「ハイライトされた行を再実行する」意味に
  // ならず、無反応になる。
  if (grepInPlaceOriginMode_ == ApplicationMode::Shell) shell_.beginHistoryBrowsing();
  return true;
}

void Application::deactivateGrepInPlace() {
  if (!grepInPlaceActive_) return;
  grepInPlaceActive_ = false;
  ContentView &target = grepViewForMode(grepInPlaceOriginMode_);
  for (ContentView *view : grepModeLineViews(grepInPlaceOriginMode_)) view->setGrepInPlaceOverlay(false);
  target.clearGrepMatchHighlight();
  if (&target == &fileView_ && currentMode_ == ApplicationMode::File && epubVerticalActive())
    rebuildEpubVertical(fileRawLineAtCursor());  // 縦書きの字の強調も消す(redrawEpubVerticalForGrep()参照)
  // grepInPlaceShellWasBrowsing_自身の宣言コメント参照: Shell起点の
  // 場合、検索開始前の状態へ正確に戻す――生きたプロンプトだったなら
  // ここでexitHistoryBrowsing()まで済ませてしまう(activateGrepInPlace()
  // 自身が呼んだbeginHistoryBrowsing()を、対になる1回で相殺する)ので、
  // 「1回のGREP終了=1回の状態遷移」になり、もう1回^Gを押して初めて
  // 生きたプロンプトへ戻るという余分な段が無くなる。既に手動で履歴
  // 閲覧中だった場合はhistoryBrowsing_自体は継続させ、退避しておいた
  // カーソル位置(検索でジャンプする前の位置)だけを戻す。
  if (grepInPlaceOriginMode_ == ApplicationMode::Shell) {
    if (grepInPlaceShellWasBrowsing_) {
      target.setCursorIndex(grepInPlaceShellPriorCursorIndex_);
    } else if (shell_.historyBrowsing()) {
      shell_.exitHistoryBrowsing();
    }
  }
  // updateGrepInPlacePositionText()自身のコメント参照――セッション
  // 終了時は通常のposition表示(カーソル行/総行数)へ戻す。
  activeView().setPositionTextOverride(std::nullopt);
  activeView().redraw();
}

// activateGrepInPlace()自身の2026-09-10追加コメント参照: 「マッチ番号/
// 総マッチ数」(1始まり、例: "3/12")の文字列そのもの。grepInPlaceActive_
// でなければ空文字列(呼び出し元はdrawPromptLabel()自身のpositionText
// 引数の慣習――空ならその分の隙間ごと省略される――にそのまま渡せる)。
// 2026-09-11追加: 当初はupdateGrepInPlacePositionText()内に直書き
// だったが、ライブ編集中(^[i、drawPromptLabel()自身の呼び出し箇所)にも
// 同じ文字列が必要になったため、文字列生成そのものをここへ括り出した
// ――直接のユーザー報告("^[iの文字列入力がGREPの隣から始まっている
// が、それだと^N^Pで循環が起きたかどうかを判別できない"): 以前は
// activeView()自身のposition表示(ContentView::renderModeLine()経由)
// だけを直していたが、ライブ編集中はdrawPromptLabel()+
// promptEditor_.redraw()が画面最終行を毎回上書きしてしまうため
// (liveIncrementalGrepSearch()自身のコメント参照)、そちらの呼び出し
// 箇所にも同じ文字列を渡す必要があった。
std::string Application::grepInPlaceMatchLabel() const {
  if (!grepInPlaceActive_) return "";
  // 2026-09-19、grepInPlaceMatches_.size()(行数)からgrepInPlaceOccurrences_
  // .size()(出現数)へ変更――GrepOccurrence自身の宣言コメント参照。
  // "**"のような1行に複数一致がある場合、以前は分母がその行を1件と
  // しか数えず、Emacs等と比べて総数がユーザーの直感より少なく出ていた。
  return std::to_string(grepInPlaceIndex_ + 1) + "/" + std::to_string(grepInPlaceOccurrences_.size());
}

// 2026-09-30、直接のユーザー質問("縦書きでは検索できない？"): FILEのin-place GREPは、見せている
// 画面(viewForMode())の行を検索して一致を強調していたので、EPUBの縦書き(epubVerticalView_、
// 画面1枚分の行)では本文が検索されなかった。GREPの行・強調・カーソルは常に本文の行(fileView_)
// で扱い、縦書きはredrawEpubVerticalForGrep()で組み直して見せる。
ContentView &Application::grepViewForMode(ApplicationMode mode) {
  return mode == ApplicationMode::File ? fileView_ : viewForMode(mode);
}

// モードラインのGREPの表示(ラベル・バッジ・一致なしの知らせ)を付けるビュー: GREPの対象と、
// 縦書きならその画面も。
std::vector<ContentView *> Application::grepModeLineViews(ApplicationMode mode) {
  std::vector<ContentView *> views{&grepViewForMode(mode)};
  if (&viewForMode(mode) != views.front()) views.push_back(&viewForMode(mode));
  return views;
}

// targetがfileView_で縦書きを見せている時は、本文のカーソルの行(一致の行)を右端に置いて縦書きを
// 組み直し(一致の強調、Display::Attribute::Highlight/HighlightCurrentは本文の行の装飾として
// 縦書きの字にも付く)、縦書きを描いてtrue。そうでなければ何もしないでfalse。
bool Application::redrawEpubVerticalForGrep(ContentView &target) {
  if (&target != &fileView_ || currentMode_ != ApplicationMode::File || !epubVerticalActive()) return false;
  rebuildEpubVertical(fileRawLineAtCursor());
  if (grepInPlaceActive_) updateGrepInPlacePositionText();
  activeView().redraw();
  activeView().syncVisibleCursor();
  return true;
}

void Application::updateGrepInPlacePositionText() {
  activeView().setPositionTextOverride(grepInPlaceMatchLabel());
}

bool Application::jumpToGrepInPlaceMatch(int delta) {
  if (!grepInPlaceActive_ || grepInPlaceOccurrences_.empty()) return false;
  int count = static_cast<int>(grepInPlaceOccurrences_.size());
  grepInPlaceIndex_ = ((grepInPlaceIndex_ + delta) % count + count) % count;  // 循環(wrap)
  const GrepOccurrence &occurrence = grepInPlaceOccurrences_[grepInPlaceIndex_];
  ContentView &target = grepViewForMode(grepInPlaceOriginMode_);
  target.setCursorIndex(occurrence.sourceLineIndex);
  // activateGrepInPlace()自身の2026-09-19追記コメント参照: 現在位置が
  // 出現単位で動くようになったので、^N/^Pのたびにハイライトも呼び
  // 直し、HighlightCurrentを新しい出現へ移す。
  target.setGrepMatchHighlight(toHighlightMatches(grepInPlaceMatches_), occurrence.sourceLineIndex, occurrence.span);
  updateGrepInPlacePositionText();
  target.centerOnCursor();
  if (!redrawEpubVerticalForGrep(target)) target.redraw();
  // activateGrepInPlace()自身のsyncVisibleCursor()呼び出しのコメント
  // 参照(2026-09-11、Directory/Fileの二択から汎用フラグ判定へ一般化)。
  if (!target.cursorHighlightEnabled()) target.syncVisibleCursor();
  // activateGrepInPlace()自身のshell_.beginHistoryBrowsing()呼び出しの
  // コメント参照――^N/^Pで別のマッチへ移るたびにも同じく必要。
  if (grepInPlaceOriginMode_ == ApplicationMode::Shell) shell_.beginHistoryBrowsing();
  return true;
}

bool Application::returnFromGrep() {
  // 2026-08-11修正: 直接のユーザーの説明――"検索への入口がCUTなら
  // 出るときもCUTから出るはず"(検索自身の入り口が候補リストだった
  // なら、Backもそこを通って出るべき)。grepReturnsToCandidateList_
  // 自身のコメント(pendingSearchFromDirectEntryList_)参照: 適切な
  // LIFOスタックに合わせ、grepReturnMode_へまっすぐ跳ぶのではなく、
  // 一度に1レベルずつ候補リストへ再入場する。currentMode_は「最初に」
  // grepReturnMode_へ設定される、これはstartRegexPromptList()自身の
  // startPromptHistoryRecall()が正しいpromptHistoryRecallReturnMode_
  // を取得するため(それは呼び出し時にcurrentMode_を読む)、この関数
  // がこの分岐で実際にはgrepReturnMode_自身のビューを一度も表示しな
  // いにも関わらず。
  if (grepReturnsToCandidateList_) {
    grepReturnsToCandidateList_ = false;
    currentMode_ = grepReturnMode_;
    // 2026-08-28修正、実際の報告+正確な再現("履歴Gが不良...②のはず
    // が上記①"): この分岐は、下の注釈をクリアするコードが一度も実行
    // される前に早期returnする――File自身のsearchAnnotation_
    // (マッチが選ばれた時にopenSelectedGrepMatch()によって設定される)
    // は、候補リストを通るこの迂回全体にわたって設定されたままだった
    // ので、「次に」Fileモード自身のビューが表示された時(再度開かれた
    // 候補リストからも戻った後)、それは依然として古いマッチの注釈を
    // 表示しており、grepからの新鮮なBackで抜けた際に到達すべき素の
    // ファイル("②")とは区別が付かず、再びマッチに新たに着地した
    // ("①")画面と見分けが付かなかった。Directoryにはここに相当する
    // 隙間は無い(DirectoryMode::enterDirectory()自体が、新しい`ls`
    // のたびにsearchAnnotation_をクリアする、それ自身のコメント参照)、
    // これがFileの分岐だけがこれを必要とした理由である。
    if (grepReturnMode_ == ApplicationMode::File) fileMode_.annotations().pop();
    return startRegexPromptList(PromptKind::GrepPattern);
  }
  currentMode_ = grepReturnMode_;
  // (以前のopenSelectedGrepMatch()から)"FGREP: pattern"の注釈が既に
  // 表示されているDirectory一覧からの^Gは、Grepモード(すぐ上の
  // returnToGrepResults())へ戻り、そこからの「2回目の」^Gが「ここ」
  // を通って戻ってくる――しかしモードラインは、以前の古い"FGREP:
  // .json"ではなく、grepからの新鮮なBackで抜けた際に表示される
  // プレーンな"(ls)"を読むべきである。openSelectedGrepMatch()は
  // 「私はこのマッチを選んだ」ことを意味する唯一の呼び出し元である
  // ――この関数は「キャンセル、何も(新しく)選ばずに戻っている」こと
  // を意味する、そのため以前の選択から残っていたどんな注釈も、もう
  // 今起きたことを説明しておらず、2回目に持ち越すのではなくクリア
  // される必要がある。
  if (grepReturnMode_ == ApplicationMode::Directory) directoryMode_.primaryAnnotations().pop();
  else if (grepReturnMode_ == ApplicationMode::File) fileMode_.annotations().pop();
  // 2026-09-19四訂正: Man/Locateについては、ここでの後始末はもう
  // 不要になった――openSelectedGrepMatch()自身のMan/Locate分岐は
  // manReturnStack_/locateReturnStack_へGrepをpush_back()するだけで
  // (application.hpp自身の宣言コメント参照)、この関数自体はそれらの
  // スタックに一切触れない。マッチを選ばずここへ直接来た経路では
  // そもそも何もpushされていないので、後始末そのものが要らない。
  activeView().redraw();
  return true;
}

// 2026-08-11修正: openSelectedGrepMatch()の対称的な相方――Directory
// モードからの"FGREP: pattern"注釈が表示されている間のBack(^G)は、
// 「同じ」依然として無傷な結果の上でGrepモードへ再入場する
// (grepView_のlines/matches_はopenSelectedGrepMatch()によって一切
// 触れられず、着地するDirectory/Fileのビューだけが触れられる)ので、
// これは検索を再実行する必要が全く無い。
bool Application::returnToGrepResults() {
  currentMode_ = ApplicationMode::Grep;
  grepView_.redraw();
  return true;
}

bool Application::returnFromCutContent() {
  if (cutOrigin_ == CutOrigin::File && !cutPositionKey_.empty())  // cutLastContent_参照
    cutLastContent_ = std::make_pair(cutView_.cursorIndex(), cutContentView_.cursorIndex());
  currentMode_ = ApplicationMode::Cut;
  cutView_.redraw();
  return true;
}

// 2026-08-11: GREP(マッチでのEnterが、注釈付きでDirectoryへ戻る唯一
// の経路であり、プレーンなBackはそれをクリアする別個の「キャンセル」
// 経路である――openSelectedGrepMatch()/returnFromGrep()自身のコメント
// 参照)とは異なり、Directoryモードから直接実行されたCUTにはそのよう
// な分裂が無い: BackがCutモードからDirectoryへ戻る唯一の方法である
// (GREPのEnterが機能するような「セグメントを選んでDirectoryへ跳ぶ」
// というアクションは無い)、そのためこの単一の関数が、Cutモードを
// 離れることと"CUT: pattern"という注釈を記録することの両方を行う。
// 呼び出しのたびに常に現在のパターンで新たに書かれるので、
// returnFromGrep()が防がなければならなかったような古い注釈の危険は
// 無い。
bool Application::returnFromCut() {
  saveCutFilePosition();
  if (cutOrigin_ == CutOrigin::Epub) {
    fileMode_.annotations().clear();  // openEpub()の"EPUB"注釈
    epubVertical_ = false;
  }
  // 2026-08-11修正: returnFromGrep()自身のgrepReturnsToCandidateList_
  // チェックと同じLIFOスタックの理屈――そのコメント参照。上の「候補
  // リストとCutモードの間には何も無い」というコメント自身の、常に
  // 新しい注釈の書き込みより前にチェックする、候補リストへ再入場する
  // ことは、まだ実際には検索を離れていないことを意味するため。
  if (cutReturnsToCandidateList_) {
    cutReturnsToCandidateList_ = false;
    currentMode_ = cutReturnMode_;
    return startRegexPromptList(PromptKind::CutPattern);
  }
  currentMode_ = cutReturnMode_;
  // cutResultsResumed_(2026-08-24修正、それ自身の宣言コメント参照):
  // 「再開された」結果画面(returnToCutResults()経由で到達、つまり
  // ユーザーが既に注釈付きのDirectory一覧の「中へ」クリックして戻った)
  // を離れることは、それを書き直すのではなくクリアする――そうしない
  // と、^Gは実際にはプレーンな(ls)へ決して脱出せず、2つの画面の間で
  // 永遠にループしてしまう。下のcutOrigin_==Directoryの書き込みより
  // 前にチェックされ、それに対して無条件である(再開されたセッション
  // は、構造上、そもそも常にDirectory/cutOrigin_==Directoryのケース
  // である)。
  if (cutResultsResumed_) {
    cutResultsResumed_ = false;
    if (cutReturnMode_ == ApplicationMode::Directory) directoryMode_.primaryAnnotations().pop();
    activeView().redraw();
    return true;
  }
  // cutOrigin_ == Directoryのみ(2026-08-23修正、実際の報告:
  // "^J^H→...→続いて^G^G→戻り値不良「CUT: ^  [0-9]+¥.」（「usilic
  // 1.5b」が正解）")――この関数は、実際のユーザーが開始した^J^C検索
  // だけでなく、Directoryへ戻ることができる、CutModeに裏打ちされた
  // 全ての画面で共有される。HELP(cutOrigin_==Help、kHelpPattern自身
  // の見出し行末タブ)とLOG(cutOrigin_==Log)は、どちらも
  // 内部的にまさに同じCutMode/split()の仕組みを再利用するが、どちら
  // から戻ることも「たった今CUT検索を実行した」ことにはならない
  // ――その後にDirectory一覧の検索注釈に"CUT: <内部パターン>"という
  // 痕跡を残すことは、表示の乱れではなく、まさにバグそのものである
  // (setSearchAnnotation()自身の値が実際にその後の再描画が描くもの
  // なので、これは古いフレームではなく間違ったデータだった)。本物の
  // Directory起点のCUTだけがそれを設定する。
  if (cutReturnMode_ == ApplicationMode::Directory && cutOrigin_ == CutOrigin::Directory) {
    directoryMode_.primaryAnnotations().push(DirectoryMode::DirAnnotationKind::Cut, "CUT: " + cutMode_.pattern());
  }
  activeView().redraw();
  return true;
}

// returnToGrepResults()に対応するCut/CUT-from-Directoryの相方――なぜ
// lastDirectorySearchKind_がこの2つを選ぶのかについてはCommand::Back
// 自身のコメント参照。cutView_自身のlines/segmentsはDirectoryへ戻る
// ことによって触れられないので、(Grepのケースと同様)これはsplitを
// 再実行する必要が無い。
void Application::saveCutFilePosition(int contentCursor) {
  if (cutPositionKey_.empty() || cutOrigin_ != CutOrigin::File || cutMode_.canGoBack()) return;
  History::CursorPosition pos;
  pos.row = cutView_.cursorIndex();
  pos.totalRows = static_cast<int>(cutMode_.segments().size());
  pos.lineNumber = contentCursor >= 0 ? contentCursor + 1 : -1;
  // 一覧のまま離れても、最後に見ていた区間とその行を残す(cutLastContent_参照)
  if (contentCursor < 0 && cutLastContent_) {
    pos.row = cutLastContent_->first;
    pos.lineNumber = cutLastContent_->second + 1;
  }
  history_.setPosition(PositionCategory::Screen, cutPositionKey_, pos);
}

Application::CutLeaveSnapshot Application::cutLeaveSnapshot() const {
  return {currentMode_, cutPositionKey_,
          currentMode_ == ApplicationMode::CutContent ? cutContentView_.cursorIndex() : -1};
}

void Application::saveCutPositionIfLeft(const CutLeaveSnapshot &before) {
  leaveEpubHorizontalIfOutside();  // epubHorizontalSection_参照(キー/コマンドの処理の後の見直しを相乗り)
  bool wasCut = before.mode == ApplicationMode::Cut || before.mode == ApplicationMode::CutContent;
  bool isCut = currentMode_ == ApplicationMode::Cut || currentMode_ == ApplicationMode::CutContent;
  if (!wasCut || isCut || before.key.empty() || before.key != cutPositionKey_) return;
  saveCutFilePosition(before.contentCursor);
}

void Application::saveResumableCutState() {
  // 2026-10-01、直接のユーザー報告("引数無しの-c/-g起動では、それぞれでも同じ章で立ち上がらない
  // ので閲覧位置を保存していない模様"): FILEの位置は^G等で離れる時にしか保存しておらず、見たまま
  // 終えると前に離れた時の位置で開き直していた。終了の直前にも保存する(縦書きなら列から本文の
  // 行へ合わせてから、returnToEpubToc()と同じ)。
  if (currentMode_ == ApplicationMode::File) {
    if (epubVerticalActive()) syncEpubVerticalToFile();
    fileMode_.saveCursorPosition();
  }
  if (currentMode_ == ApplicationMode::CutContent) saveCutFilePosition(cutContentView_.cursorIndex());
  else if (currentMode_ == ApplicationMode::Cut) saveCutFilePosition();
}

void Application::restoreCutFilePosition(const std::string &key) {
  cutPositionKey_ = key;
  cutLastContent_.reset();
  auto pos = history_.positionFor(PositionCategory::Screen, key);
  if (!pos) return;
  if (pos->lineNumber > 0) cutLastContent_ = std::make_pair(pos->row, pos->lineNumber - 1);
  int last = static_cast<int>(cutMode_.segments().size()) - 1;
  cutView_.setCursorIndex(std::max(0, std::min(pos->row, last)));
  cutView_.centerOnCursor();  // ^Lと同じく画面中央へ(2026-09-29、直接のユーザー依頼)
  cutView_.redraw();
}

ApplicationMode Application::cutFileReturnModeFor(ApplicationMode origin) {
  return origin == ApplicationMode::File ? ApplicationMode::Directory : origin;
}

bool Application::returnToCutResults() {
  currentMode_ = ApplicationMode::Cut;
  cutResultsResumed_ = true;  // それ自身の宣言コメント参照――次のreturnFromCut()で一度だけ消費される
  cutView_.redraw();
  return true;
}

bool Application::returnFromMan() {
  manMode_.saveCursorPosition();
  if (manReturnsToUnderline_) {
    manReturnsToUnderline_ = false;
    currentMode_ = ApplicationMode::Underline;
    underlineView_.redraw();
    return true;
  }
  if (manReturnsToCandidateList_) {
    // ソート済みMAN一覧から開いたページ: ^Gはその一覧へ戻る(2回目の^Gが
    // 通常の戻り先へ)。
    manReturnsToCandidateList_ = false;
    currentMode_ = manReturnStack_.back();
    if (!startRegexPromptList(PromptKind::ManName)) return false;
    selectListBrowserItem(manListSelectedItem_);  // 選んだページへカーソルを戻す
    return true;
  }
  // 2026-09-19四訂正、application.hpp自身のmanReturnStack_宣言コメント
  // 参照: 先頭(back())が常に「次にどこへ戻るか」――openSelected
  // GrepMatch()自身のMan分岐がマッチへ着地する度にGrepを1つpush_back()
  // しているので、ここではそれを読んで1段だけpop_back()する。
  // ただし残り1要素(前面=本来の基点)だけは決してpopしない――それは
  // 次にstartManPrompt()/startManList()が新規にリセットするまで
  // 生き続ける「フロア」であり、空にしてしまうと以降の.back()が
  // 未定義動作になる(ModeStack自身のfloorフレームと同じ発想)。
  currentMode_ = manReturnStack_.back();
  if (manReturnStack_.size() > 1) {
    manReturnStack_.pop_back();
  } else {
    // 2026-09-19七訂正、application.hpp自身のmanAnchor_宣言コメント
    // 参照: ここが本来の基点そのものへ戻る瞬間――途中でfileMode_/
    // directoryMode_が(例えばDU自身のopenSelectedDuEntry()経由で)
    // 無関係な内容へ書き換えられてしまっていた場合に備え、凍結して
    // おいた本来の内容を明示的に復元する。
    restoreAnchorContent(currentMode_, manAnchor_);
  }
  activeView().redraw();
  return true;
}

bool Application::returnFromLocate() {
  // 上と同じ理屈、LOCATE版。
  currentMode_ = locateReturnStack_.back();
  if (locateReturnStack_.size() > 1) {
    locateReturnStack_.pop_back();
  } else {
    restoreAnchorContent(currentMode_, locateAnchor_);
  }
  activeView().redraw();
  return true;
}

// fileReturnMode_自身の宣言コメント参照。returnToDirectory()を正確に
// 反映している(saveCursorPosition() + hideVisibleCursor() +
// 宛先のビューを再描画)、ただしdirectoryView_ではなくlocateView_に
// 着地するだけである。
bool Application::returnFromFileToLocate() {
  fileMode_.saveCursorPosition();
  currentMode_ = ApplicationMode::Locate;
  display_.hideVisibleCursor();
  locateView_.redraw();
  return true;
}

bool Application::returnFromArchiveContent() {
  currentMode_ = ApplicationMode::Archive;
  archiveView_.redraw();
  return true;
}

// archiveParentStack_が再び空になった時点で最終的にDirectoryへ戻る
// 前に、ネストしたアーカイブの降下を1段階ずつ登って戻る
// (descendToNestedArchive()自身のコメント参照)――ネストしていない
// ケースについては元々の、依然として変わっていない挙動。
bool Application::returnFromArchive() {
  archiveMode_.saveCursorPosition();
  if (!archiveParentStack_.empty()) {
    std::string parentPath = archiveParentStack_.back();
    archiveParentStack_.pop_back();
    if (archiveMode_.loadArchive(parentPath)) return true;
    // 記憶されているパスは常に依然として有効であるはずである(本物の
    // ファイル、あるいは私たち自身の依然としてディスク上にある一時
    // ファイル)――しかし何かがそれを足元から取り除いてしまった場合、
    // 画面に古いコンテンツを表示させ続けるのではなく、下のDirectory
    // へ流れ落ちることで安全に失敗する。
  }
  // 2026-08-27修正、実際の報告+再現("^G連打で行き来ループとなる...
  // ^J^Jは帰還後の^Gは在籍ディレクトリに設定できない？"): これは以前
  // は、directoryMode_自身のcurrentDirectory_が既にアーカイブの格納先
  // ディレクトリだと信頼して、単にcurrentMode_を切り替え
  // directoryView_を「そのまま」再描画していた――通常のDirectoryへ
  // 降りてからEnterするというセッションについては真だが、
  // goToRecentFile()(^J^J、あるいはCommand::Back自身の新しい
  // hasArcExtractSourceAnnotation()のルーティング――それ自身のコメント
  // 参照)経由で、完全に別のどこか(例えば~/.usilic/saved/のような
  // アーカイブ展開の宛先)から到達したアーカイブについては真では
  // ない: そのディレクトリ「自身」の"ARC: <archivePath>"という注釈が
  // 依然として表示されていたので、Archiveへ戻ってから再びBackを押す
  // と、単に同じ注釈がそこに依然としてあることを見つけ、再びArchive
  // へまっすぐ跳ね返っていた――2つの画面の間の無限のピンポンであり、
  // アーカイブの実際の格納先ディレクトリに一度も到達しない。
  // returnToDirectory()が同一のFileModeの種類のバグについて既に確立
  // した修正と同じもの(2026-08-16、それ自身のコメント参照)――
  // enterDirectory()経由でアーカイブの実際の親ディレクトリをたどる、
  // directoryMode_が既に正しくそれを表示している時は常にno-op
  // (通常のDirectoryからEnterするケース)。
  std::filesystem::path archivePath(archiveMode_.currentArchive());
  std::string parent = archivePath.parent_path().string();
  if (!parent.empty() && parent.back() != '/') parent += '/';
  if (!parent.empty() && parent != directoryMode_.currentDirectory()) {
    std::string previousDirectory = directoryMode_.currentDirectory();
    if (directoryMode_.enterDirectory(parent) && !previousDirectory.empty())
      directoryMode_.resetNavigationToFloor(previousDirectory);
  }
  currentMode_ = ApplicationMode::Directory;
  directoryView_.redraw();
  return true;
}

bool Application::returnFromDu() {
  duMode_.saveCursorPosition();
  // 2026-09-15追加、直接のユーザー報告("^Jd→.huskyを選択→pre-commit
  // を選択→^Gで行き来ループ、LOCATEでは起きない"): returnFromGrep()/
  // returnFromCut()自身の同一のsetSearchAnnotation("")呼び出し(すぐ
  // 上及びこのファイルの他の箇所参照、"そうしないと、^Gは実際には
  // プレーンな(ls)へ決して脱出せず、2つの画面の間で永遠にループして
  // しまう")がここには一度も無かった。再現の実際の経路: ^Jdで
  // ディレクトリエントリを開く(openSelectedDuEntry()、
  // directoryMode_.setSearchAnnotation("DU: ...")を設定)→その
  // ディレクトリ内で「別の」ファイルを普通にEnterで開く
  // (openSelectedEntry()、DU由来ではない通常の遷移なのでこの注釈には
  // 一切触れない)→^G(returnToDirectory()、開いたファイルの親が
  // 既にcurrentDirectory()と一致するため「戻る」の内部
  // enterDirectory()呼び出し自体がスキップされ、注釈はここでも
  // 生き残る)→directoryMode_.hasSearchAnnotation()がまだtrueなので
  // 次の^Gはここ(returnFromDu())へ戻ってくるが、以前はここで注釈を
  // 一切クリアしていなかったため、次のDirectory側の^Gが再びここへ
  // 跳ね返り続けていた。GREP/CUTと同じ形で、ここを離れる際に無条件で
  // クリアする(この注釈はopenSelectedDuEntry()自身のディレクトリ
  // エントリ分岐だけが設定するもので、duReturnStack_の戻り先がFileの
  // 場合でも無関係かつ無害――directoryMode_自身の状態なので、Fileへ
  // 戻る時はどのみち参照されない)。
  directoryMode_.primaryAnnotations().pop();
  // 2026-09-19再訂正、manReturnStack_/locateReturnStack_と全く同じ
  // 仕組みへ揃える(application.hpp自身のduReturnStack_宣言コメント
  // 参照): openSelectedGrepMatch()自身のDu分岐がマッチへ着地する度に
  // Grepを1つpush_back()するので、ここではそれを読んで1段だけ
  // pop_back()する。ただし残り1要素(本来の基点)だけは決してpopしない。
  currentMode_ = duReturnStack_.back();
  if (duReturnStack_.size() > 1) {
    duReturnStack_.pop_back();
  } else {
    // 2026-09-19七訂正、application.hpp自身のduAnchor_宣言コメント
    // 参照: ここが本来の基点そのものへ戻る瞬間――途中でfileMode_/
    // directoryMode_が(まさにこの関数のすぐ上のコメントが説明する
    // openSelectedDuEntry()経由で)無関係な内容へ書き換えられて
    // しまっていた場合に備え、凍結しておいた本来の内容を明示的に
    // 復元する。
    restoreAnchorContent(currentMode_, duAnchor_);
  }
  activeView().redraw();
  return true;
}

// 2026-09-09追加、openSelectedDuEntry()自身のコメント参照:
// returnToCutResults()/returnToGrepResults()と同じ形――duMode_自身の
// 内容は既にキャッシュ済みなので、再計算せずにモードを戻すだけでよい。
bool Application::returnToDuResults() {
  currentMode_ = ApplicationMode::Du;
  duView_.redraw();
  return true;
}

bool Application::returnFromShell() {
  currentMode_ = shellReturnMode_;
  // ^J^Wが押された「同じ」ListBrowser候補リストへ戻る時、一時停止
  // されていたpromptHistoryContext_セッション(それ自身の宣言コメント
  // 参照)を復元する――正確に一度だけ消費される。
  if (shellReturnMode_ == ApplicationMode::ListBrowser && suspendedPromptHistoryContext_) {
    promptHistoryContext_ = suspendedPromptHistoryContext_;
    suspendedPromptHistoryContext_ = std::nullopt;
  }
  // (通常はプレーンなディレクトリ一覧である)戻る先のモードは、
  // 可視カーソルではなくContentView自身の行のハイライトに依存して
  // おり、それ自体は一度もsetVisibleCursor()を呼ばない――これが無い
  // と、ContentView::redraw()は意図的にそれに一切触れないので
  // (そもそもなぜそれが副次的な再描画をまたいで持続しなければなら
  // ないのかについてはDisplay::setVisibleCursor()自身のコメント参照)、
  // -gはSHELLの最後のカーソル位置(履歴閲覧中の行、あるいは生きた
  // プロンプトのカレット)を、新しい画面のコンテンツの上に浮かせた
  // まま表示し続けてしまう。
  display_.hideVisibleCursor();
  activeView().redraw();
  // 一時停止されていた「直接アクティブな」プロンプト(promptKind_、
  // suspendedActivePromptKind_自身の宣言コメント参照)を復元する
  // ――すぐ上のactiveView().redraw()の「後」であり、前ではない:
  // activatePrompt()はプロンプト自身の24行目のオーバーレイを描き、
  // このポートの他の全てのプロンプトを表示する呼び出し箇所は、
  // それが下層のコンテンツの「上に」着地するよう、既にactivatePrompt()
  // が「最後に」描かれるものであることに依存している――これをより
  // 早く行うと、プレーンなコンテンツビュー自身のモードライン(同じ行)
  // がそれをすぐに塗り直してしまっていたはずである。上のListBrowser
  // 候補リストのケースとは異なり、これは特定のshellReturnMode_に
  // 限定されていない: activatePrompt()自体はcurrentMode_に触れない
  // (上で既にshellReturnMode_に設定済み)ので、直接アクティブな
  // プロンプト(素の^SからのGrepPattern等)は、それがどのモードの上に
  // 重ねられていたかに関わらず再開できる。
  if (suspendedActivePromptKind_) {
    activatePrompt(*suspendedActivePromptKind_, suspendedActivePromptText_);
    suspendedActivePromptKind_ = std::nullopt;
    suspendedActivePromptText_.clear();
  }
  return true;
}

// do_shell()自身の"if (kill(spid, 0)) { pv = NULL; m = -M_bl; break;
// }"から移植――legacyのinput_shell()は、リレー/シェルプロセスが
// 依然として生きているかを「あらゆる」ループのイテレーションで
// チェックし、生きていなくなった瞬間(典型的にはユーザーがちょうど
// `exit`をタイプしたため)、自動的に-M_bl(^G自身のコード)を返し、
// SHELLがどこから入場したかというモードへまっすぐ巻き戻す――別個の
// ^Gは不要。Shell::onPtyReadable()がEOFを報告するたびに呼ばれる
// (それ自身のコメント参照)。shellStarted_はfalseへリセットされる、
// これは「次の」Shellモードへの入場が、死んだものを再利用しようと
// するのではなく、ensureShellActive()自身のshellStarted_ガード経由
// で本当に新しい子プロセスを生むようにするため――その時点でShell::
// start()自身のresetSessionState()がscrollback_/pipeBuffer_等をクリア
// する、legacyの再起動時の新しいPP連鎖に合わせている。実際のユーザー
// 依頼(2026-08-02)がこれを確認した――全体のやり取りについては
// resetSessionState()自身のコメント参照。
void Application::handleShellChildExited() {
  shell_.terminate();
  shellStarted_ = false;
  if (currentMode_ == ApplicationMode::Shell) returnFromShell();
}

bool Application::returnFromListBrowser() {
  // 2026-09-18追加、openSelectedListEntry()自身の同じ理由のコメント
  // 参照。
  if (tabCompletionContext_) return returnFromTabCompletionList(/*selected=*/false);
  if (promptHistoryContext_) {
    PromptKind kind = promptHistoryContext_->kind;
    // EGREP自身の候補リスト(2026-08-12修正、直接のユーザーの指示)。
    // ここでの^Gは空の編集可能なプロンプトを再度開いては「ならない」。
    // docs/HISTORY.md:1538によれば――そもそも^J^Sが空のプロンプトの
    // 代わりに「直接」候補リストへ開く理由の全て(startRegexPromptList()
    // 自身の経緯)は、EGREPには、CUTの^Jc/^J^Cが1つの名前を共有し
    // それ以外の視覚的な区別が無いのと同じような、FGREP対EGREP方式の
    // 命名の手がかりが無いことである――そのためリスト自体が「これは
    // regexモードだ」という信号であり、誤用を防ぐために意図的に表示
    // される。^Gで黙って空のプロンプトへ流れ落ちる(この関数自身の
    // 既存の挙動、この関数が「も」処理する無関係な本物の^R呼び出しの
    // ケースからまるごと受け継いだもの、下の一般的な分岐参照)ことは、
    // ユーザーが二度とリストを見ることなくタイプすることへ滑り戻る
    // ことを許してしまい、まさにその信号を損なう。
    // promptHistoryRecallIsDirectEntry_の場合(つまり^J^S自体経由、
    // あるいは項目275自身の「候補リストへ戻る」LIFOの仕組み経由で
    // 到達した場合)にのみ適用される――EGREPについての「本物の」^R
    // 呼び出し(タイプ中、directEntry==false)は、それが完全に別の
    // シナリオ(編集中の呼び出しであり、「リストが入り口である」の
    // ではない)であるため、影響を受けず、既にタイプされていたものを
    // 再開し続ける。
    // CutPattern(2026-08-22修正、項目405――docs/HISTORY.md:10208、
    // 直接のユーザー報告: "（ls）のCLAUDE.md上で^J^B...戻り値の通り
    // （ls）へ戻らないと"): 元々は、CUT自身のregex亜種を先送りする
    // 2026-08-12の注記("まだCUTの試用まではしていないので不具合が
    // あれば対処すればいい"――CUTはまだ、この同じ隙間がそこでも問題
    // になるかどうかを知るのに十分試されていなかった)により、
    // GrepPatternだけに限定されていた。実際にはそうだった: ^J^B/
    // ^J^Cはこの同じ種類の候補リストへ直接開く
    // (startRegexPromptList()自身のコメント参照――CUT-regexはそれを
    // まるごと再利用する)ので、「^Gは空の編集可能なプロンプトを再度
    // 開くのではなく、これが呼び出された場所へまっすぐ戻らなければ
    // ならない」というまさに同一の理屈が変わらず当てはまる。実際の
    // 報告がその隙間を確認した今、これを組み込んだ。
    // RecentFile/RecentDirectory(2026-08-18修正、直接のユーザー報告+
    // スクリーンショット=cx/=cy): この2つは「常に」directEntryである
    // (startPromptHistoryRecall()自身のコメント参照――どちらにも
    // 別個の「タイプ中の本物の^R呼び出し」という入り口は無い)、
    // そしてGrepPattern/CutPatternとは異なり、それらは「編集を再開
    // すべきタイプ済みのパターン」という概念が全く無い純粋なナビ
    // ゲートなので、^Gで空のactivatePrompt()エディタを再度開くことは、
    // これがGrepPatternについてそうしていたよりもここではさらに意味
    // を成さない――それは、外出が実際に始まった、プレーンな"(ls)"を
    // 表示する画面("(ls)から^X^Dしてるんで(ls)へ戻らないと")の代わり
    // に、^X^D/^X^Fのプロンプト自身の"DIR"/"FILE"というモードライン
    // のラベルと空の編集可能なフィールドを、その下の本物の
    // DirectoryMode/FileModeのコンテンツの「上に」描かれたまま残して
    // いた(activeView().redraw()は既に正しい画面を塗っていたが、
    // その後activatePrompt()がそれ自身の24行目をまさにその上に描いて
    // いた)。GrepPattern自身の同一の修正と同じ早期returnの分岐に
    // 組み込まれている。
    if ((kind == PromptKind::GrepPattern || kind == PromptKind::CutPattern || kind == PromptKind::RecentFile ||
         kind == PromptKind::RecentDirectory) &&
        promptHistoryContext_->isDirectEntry) {
      // RecentFile/RecentDirectory + listBrowserChronological_
      // (2026-08-18、実際の新規ビルドしたlegacyバイナリのチェックに
      // 裏付けられた直接のユーザーの訂正――"^X^D→^R→^G(DIR一覧から
      // 来たんだからそこへ戻るのも当たり前)...現状そうなっていない
      // （^GでDIR一覧、じゃなくて(ls)へ戻る）"): ^Rの時系列の
      // 「HISTORY」トグルは、同じ画面上の余談ではなく、通常のソート
      // 済みの「DIR」/「FILE」リストの上に積み重なった、それ自身の
      // 「ネストした」画面である――old/src自身のlegacyバイナリ(まさに
      // この調査で新規にビルドしたもの)に対して`^X^D`→`^R`→`^G`を
      // 実行して確認済み: 最初の^Gは"DIR 1/1 END ..."の上に戻って
      // 着地し、「2回目の」^Gが実際に"(ls)"に到達するものである。
      // (無条件にpromptHistoryRecallReturnMode_へまっすぐ跳ぶ)下の
      // 早期returnの分岐は、その中間ステップを完全にスキップして
      // いた。まず(実際の^R押下が使うのとまさに同じ
      // toggleListBrowserHistory()経由で)トグルをソート済みへポップ
      // して戻し、早期にreturnする(ListBrowserに留まり、
      // promptHistoryRecallKind_/currentMode_には一切触れない)ことで、
      // legacyの2回^Gという形を再現する;2回目の^Gは、
      // listBrowserChronological_が既にfalseの状態で、この同じ分岐へ
      // 再び流れ落ち、下の通常の退出経路を取る。
      // GrepPattern/CutPattern(2026-08-29修正、正確な変更前/後の
      // フィールド値を伴う直接のユーザー報告――"^G:「CLUADE.md」
      // [EGREP]"――EGREP自身の時系列ビューからの^Gが、まず候補リスト
      // へポップして戻ることなく、起点のFile/Directoryへまっすぐ
      // スキップしていた): 2026-08-28自身の^R修正は、この2つの種類
      // についてトグル自体を機能させた(toggleListBrowserHistory()
      // 自身のコメント参照)が、それに合わせて「この」「まずトグルを
      // ポップする」チェックを拡張することは一度も無かった――同じ
      // 理由で、ここでも同じ2回^Gという形が当てはまる。
      if ((kind == PromptKind::RecentFile || kind == PromptKind::RecentDirectory || kind == PromptKind::GrepPattern ||
           kind == PromptKind::CutPattern) &&
          listBrowserChronological_) {
        return toggleListBrowserHistory();
      }
      ApplicationMode returnMode = promptHistoryContext_->returnMode;
      // 2026-09-11追加、直接のユーザー報告("usilic -c で起動→^X^F^J^S
      // ^Uusilic^M→...→^G→^G（なぜ2回？）"): 項目442/443により、
      // `^X^F`/`^X^D`自身の候補一覧を「閲覧中」に`^O^S`/`^J^S`
      // (FGREP/EGREP)を起動することが初めて可能になったが、これは
      // `startPromptHistoryRecall()`が`promptHistoryContext_`
      // (`RecentFile`/`RecentDirectory`のもの)を、EGREP自身の候補
      // 一覧向けの新しい`PromptHistoryContext`で無条件に上書きして
      // しまう(returnMode=currentMode_=ListBrowser、つまり「ただの
      // ListBrowserへ戻る」という具体性の無い値になる)ケースを新たに
      // 生んだ――ensureShellActive()/returnFromShell()自身の
      // `suspendedPromptHistoryContext_`と全く同じ理屈で、ここでも
      // returnMode自体がListBrowserを指す場合は、直前に上書きされて
      // 消えた「元々RecentFile/RecentDirectoryのどちらを閲覧していた
      // か」を`suspendedPromptHistoryContext_`から復元する(復元しない
      // と、`listBrowserMode_`自身の内容がEGREP自身の候補一覧のまま
      // 変わらず、その上で`currentMode_`だけがListBrowserへ戻る――
      // 画面が一切変わらないため見た目上「^Gが無反応」に見え、実際には
      // もう1回^Gを押して初めて、元のRecentFile/RecentDirectory一覧を
      // 一度も経由せず、その先の起点(Directory/File)へ直接ジャンプ
      // していた)。
      if (returnMode == ApplicationMode::ListBrowser && suspendedPromptHistoryContext_) {
        promptHistoryContext_ = suspendedPromptHistoryContext_;
        suspendedPromptHistoryContext_ = std::nullopt;
        RingCategory restoredCategory = promptHistoryContext_->kind == PromptKind::RecentFile
                                             ? RingCategory::RecentFile
                                             : RingCategory::RecentDirectory;
        listBrowserCategory_ = restoredCategory;
        listBrowserChronological_ = false;
        listBrowserView_.setCursorAtModeLineTrailingEnd(false);
        listBrowserMode_.show(sortedUniqueRecentEntries(restoredCategory));
        currentMode_ = ApplicationMode::ListBrowser;
        return true;
      }
      promptHistoryContext_ = std::nullopt;
      currentMode_ = returnMode;
      // 2026-08-12修正、直接のユーザーの訂正: "戻り値が(ls)ならちゃんと
      // そこ（(ls)でEGREPしたので当然(ls)のはず）へ戻ってほしい"――
      // この候補リストが項目275自身のGrep結果からのLIFOの戻りを経由
      // して到達した場合、returnModeだけに戻って着地するのでは十分
      // ではない: 「その」戻りの経路はcurrentMode_をDirectoryのまま
      // にするが、directoryMode_自身の古い"EGREP: pattern"という注釈
      // をクリアし「ない」(openSelectedGrepMatch()だけがそれを設定
      // し、その経路上の何もそれをクリアしない)、そのためここに着地
      // すると、外出全体が実際に始まった、本当のプレーンな"(ls)"では
      // なく、依然として注釈付きの画面(再びマッチを選んだばかりの
      // 状態と区別が付かない)を表示していた。まさに同じ理由による
      // returnFromGrep()自身の同一のクリア処理を反映している――それ
      // 自身のコメント参照。
      if (returnMode == ApplicationMode::Directory) directoryMode_.primaryAnnotations().pop();
      activeView().redraw();
      return true;
    }
    std::string text = promptHistoryContext_->textBeforeRecall;
    ApplicationMode returnMode = promptHistoryContext_->returnMode;
    promptHistoryContext_ = std::nullopt;
    currentMode_ = returnMode;
    activeView().redraw();
    activatePrompt(kind, text);  // ^Rの前に既にタイプされていたものから再開する
    // 2026-09-18追加(フォローアップ8、PromptKind::ShellCommand自身の
    // 宣言コメント参照): SHELLだけは生きたコマンドライン行
    // (commandLineRow())がモードライン行(activatePrompt()が今しがた
    // 書いた行)とは別の行にある――直前のactiveView().redraw()
    // (=shell_.scrollback().redraw())がcommandLineRow()を無条件に
    // 空欄化するため(Shell::redrawPromptRow()自身のコメント参照)、
    // 他の全てのkindには不要なこの明示的な描き直しがここだけ必要。
    if (kind == PromptKind::ShellCommand) shell_.redrawPromptRow();
    return true;
  }
  if (shellHistoryRecallActive_) {
    shellHistoryRecallActive_ = false;
    currentMode_ = ApplicationMode::Shell;
    shell_.commandLine().setText(shellHistoryRecallText_);  // ^Rの前に既にタイプされていたものを復元する
    shellHistoryRecallText_.clear();
    shell_.scrollback().redraw();
    shell_.redrawPromptRow();
    return true;
  }
  currentMode_ = listBrowserReturnMode_;
  activeView().redraw();
  return true;
}

// M_shell(^[s)の入り口から移植: ptyに裏打ちされた子プロセスを最初の
// 使用時にのみ開始し(shellStarted_)、その後のあらゆる入場ではアク
// ティブな画面を切り替えるだけである――子プロセスは、その間の
// Back/^Gをまたいでバックグラウンドで動き続ける(クラスコメント参照)。
// Shellモード自体の内部からの再入場は拒否する(することが無い)。
bool Application::startShell() {
  if (currentMode_ == ApplicationMode::Shell) return false;
  return ensureShellActive();
}

// returnPagerToShell()自身の宣言コメント(application.hpp)参照。
bool Application::returnPagerToShell() {
  fileMode_.closeStream();
  currentMode_ = ApplicationMode::Shell;
  shell_.scrollback().redraw();
  shell_.redrawPromptRow();
  return true;
}

bool Application::ensureShellActive() {
  if (currentMode_ == ApplicationMode::Shell) return true;
  // 2026-08-26修正(実際の、注意深く再現された報告): (Command::Back
  // 自身の専用の^G/'q'のページャ復帰処理とは対照的に)^[s/^J^W等の
  // 「この」汎用の経路経由でShellに到達する時、現在SHELLの引き渡し
  // 由来の(stdin)/ファイルページャのセッションを見ている間は、下の
  // shellReturnMode_ = currentMode_へ流れ落ちては「ならない」――
  // そのページャセッションは、既に注入済みのコンテンツの使い捨ての、
  // 一時的なビュー(FileMode::pagerReturnsToShell()自身のコメント
  // 参照)であり、戻り先として記録するのが安全な、コンテンツを持つ
  // 本物のナビゲート可能なモードではない。対処しないままだと、これは
  // 永遠にピンポンする: ここでの^[sはshellReturnMode_=Fileを設定して
  // しまい、その後の(ページャ固有の作業には一切触れられない、Shell
  // 自身の別個の無関係な「シェルを離れる」キー割り当てである)プレー
  // ンな^Gは、その「同じ」依然として設定されたままのページャへ戻り、
  // その自身のpagerReturnsToShell_が、次の^Gを再びまっすぐShellへ
  // 送り返してしまう――無限のSHELL<->(stdin)ループ(それに加え、
  // この経路で繰り返し再入場することが同じ古いランデブーデータに対
  // してpollShellHandoff()を再びトリガーするため、"SHELL"の
  // trailing3の代わりに"usilic v1.5b"のウォーターマークがそこに誤って
  // 表示される)。直接のユーザー提案("(stdin)の存在は画面を離れた
  // 時点で消失でいい"――そのセッションは、^Gだけでなく「どんな」
  // 手段によってであれ、その画面が離れられた瞬間に単純に存在しなく
  // なるべきである)に従い: 戻り経路を一切記録することなく、ここで
  // 直接Shellへ切り替える、まさにこの状態からCommand::Back自身の^G
  // が既に行っているのと同一である――戻ってくるべきものはもう何も
  // 無い。
  if (currentMode_ == ApplicationMode::File && fileMode_.pagerReturnsToShell()) {
    return returnPagerToShell();
  }
  // 2026-08-12修正: 実際の報告――usilicを起動、^J^S(EGREP候補リスト)、
  // ^J^W(今日の以前のfeedRegexListByte()/feedPromptByte()のリーダー
  // ディスパッチの修正のおかげで、今やそこから到達可能)、"|sort"を
  // タイプして実行し、それから生きたSHELLプロンプトで^R――ListBrowser
  // 自身のモードラインが"PROGRAM"ではなく"EGREP"を示し、エントリを
  // 選択すると、それをコマンドラインへ挿入する代わりに、シェルコマンド
  // のテキスト("cat .../w.usilic|sort")をパターンとして使うEGREP検索
  // を実行してしまうことが示された。根本原因: promptHistoryContext_
  // (元々の^J^SのstartPromptHistoryRecall()によって設定され、
  // feedRegexListByte()自身の「編集を開始する」遷移か
  // openSelectedListEntry()の投入かのどちらかによって、正確に一度
  // 消費されることを意図している)が、shellInsertBufferContent()が
  // それらどちらの通常の消費経路も通らずにShellモードへまっすぐ跳ぶ
  // ことによって、一度もクリアされていなかった。それはシェルコマンド
  // の実行をずっと通じて設定されたままだった(依然として
  // ==GrepPattern)ので、後でstartShellHistoryRecall()が(RingCategory::
  // ShellCommandのための)それ自身の無関係なListBrowserセッションを
  // 開いた時、dispatchByte()自身のトップレベルのチェック
  // (`currentMode_==ListBrowser && promptHistoryContext_ &&
  // (...==GrepPattern || ...==CutPattern)`)は、その古いフラグに依然
  // としてマッチし、通常のListBrowserのナビゲートではなく、全ての
  // バイトをfeedRegexListByte()(EGREP/CUT候補リストのコンボボックス
  // のロジック)経由でルーティングしてしまい、そのため両方の症状が
  // 同時に起きた(間違ったモードラインのラベル、そしてEnterが
  // feedRegexListByte()自身の編集して投入する経路経由で、選択を
  // grepパターンとして投入する)。
  //
  // (shellInsertBufferContent()の内部で特別にではなく)ここでクリア
  // されているのは、ensureShellActive()が、Shellへ入る「あらゆる」
  // コマンドが流れ込む唯一の共通の絞り込み点だからである
  // (shellInsertBufferContent()、shellChangeDirectory()、
  // shellRunLsL()、shellInsertName()、素のstartShell())――それらの
  // どれでも、プロンプト呼び出しのListBrowserセッションが依然として
  // 論理的に「進行中」(それ自身の^G/Enter/編集開始という消費経路
  // ではなく、リーダーコマンド経由で放棄された)である間にShellへ跳ぶ
  // ことは、^J^Wだけでなく同一の古さの危険を持つ。通常のケースでは
  // no-op(呼び出しが進行中でなければ、これは既にstd::nulloptである)
  // ――なので無条件に実行しても安全である。
  // 依然として本当に開いたままの呼び出しセッションを、単に破棄する
  // のではなく一時停止する――全体の経緯についてはsuspendedPromptHistoryContext_
  // 自身の宣言コメント参照。ここでのcurrentMode_ == ListBrowserは、
  // promptHistoryContext_(設定されていれば)が、以前の既に放棄された
  // ものからの古い残り物ではなく、「この」生きたセッションに属する
  // ことを意味する――returnFromShell()は、プレーンな^Gで戻る時にそれ
  // を復元する。
  if (currentMode_ == ApplicationMode::ListBrowser) {
    suspendedPromptHistoryContext_ = promptHistoryContext_;
  }
  promptHistoryContext_ = std::nullopt;
  // suspendedActivePromptKind_自身の宣言コメント参照: リーダーコマンド
  // (^J^W)が、直接アクティブな編集可能なプロンプト(promptKind_ !=
  // None、例えばFGREPパターンをタイプ中)からShellへまっすぐ跳ぶこと
  // は、そのプロンプト自身の通常の退出点を一度も通らず、promptKind_
  // を詰まったままにしてしまう――それが設定されたままである限り、
  // dispatchByte()自身のトップレベルのチェックは、以降の全てのバイト
  // をfeedShellByte()ではなくfeedPromptByte()経由でルーティングする。
  // 単にクリアするのではなく一時停止するので、プレーンな^GでShellから
  // 戻ると、ユーザーが離れたまさにその場所で編集が再開される。
  if (promptKind_ != PromptKind::None) {
    suspendedActivePromptKind_ = promptKind_;
    suspendedActivePromptText_ = promptEditor_.text();
    promptKind_ = PromptKind::None;
  }
  if (!shellStarted_) {
    // 2026-09-15追加、直接のユーザー報告("usilic -c ~/.usilic/saved
    // で起動→^[sでカレントディレクトリが~/.usilic/saved/じゃない")
    // ――Shell::start()自身のinitialDirectory宣言コメント参照。
    // directoryMode_.currentDirectory()は、現在どのモードがアクティブ
    // であってもDirectory一覧のナビゲート状態として永続する(File/
    // Grep等を経由していても最後に表示していたDirectory一覧を指す
    // ――このクラスの他の全ての場所と同じ前提)ので、ここでは呼び出し
    // 元のモードを問わず無条件にこれを使ってよい。
    if (!shell_.start(directoryMode_.currentDirectory())) return false;
    shellStarted_ = true;
  }
  // previousModeName()にはListBrowserのケースが無い(それは汎用的
  // すぎる――CUT/EGREP/FGREP/MAN/LOCATE/RecentFile/.../ShellCommand
  // の履歴が全てそれを共有する)ので、これは代わりに(実際に上で取得
  // されていれば)ちょうど一時停止されたばかりのPromptKind
  // (promptLabelText()経由、候補リスト自身のモードラインが既に使って
  // いるのと「同じ」"CUT"/"EGREP"等のマッピング)を使う――そうしない
  // と、Shell画面自身の「^Gが何へ戻るか」というバッジは、このケース
  // についても何も有用なものを表示しなかった(previousModeName()の
  // デフォルト""へ落ちる)。
  // 2026-08-28修正、実際の報告+再現("INFO画面で^J.→SHELLの戻り値が
  // 「~/.usilic/usilic」...→^G→INFO（←戻らなくていい）")+ユーザー
  // 自身の一般方針("基本的にINFOへは戻らなくていい。例外はusilicrc、
  // ERROR、SHELL、WORKの対象ファイルを選んだ場合またはINFOで
  // FGREP/EGREP/CUTを実行した場合のみ"): それら4つの例外はそもそも
  // ここに一切到達しない(それらはopenSelectedBannerEntry()経由の
  // FileモードのEnter駆動か、プロンプト駆動であり、どちらも
  // ensureShellActive()を通らない)――そのため、INFOから起動された
  // 「どんな」実際のSHELLの外出(^J.、^J^W、^[s、...)も、無条件に
  // その例外リストの外に落ちる。INFOを完全にスキップし、INFO自体が
  // そうしていたであろう場所(fileReturnMode_はそこでは無条件に
  // Directoryである、executeShowUsilicBanner()自身のコメント参照)へ戻る
  // ――上のpagerReturnsToShell()の特別なケースを精神的には反映して
  // いる(一時的な画面を戻り先として記録しない)が、それとは異なり、
  // ^Gは、戻る先が全く無いままShellへ再入場するのではなく、依然として
  // 実在するどこか(Directory)へ着地する。
  ApplicationMode effectiveReturnMode = currentMode_;
  if (currentMode_ == ApplicationMode::File && fileMode_.currentFile() == bannerFilePath())
    effectiveReturnMode = ApplicationMode::Directory;
  shellReturnName_ = suspendedPromptHistoryContext_ ? promptLabelText(suspendedPromptHistoryContext_->kind)
                                                     : previousModeName(effectiveReturnMode);
  shellReturnMode_ = effectiveReturnMode;
  currentMode_ = ApplicationMode::Shell;
  shell_.scrollback().redraw();
  shell_.redrawPromptRow();
  return true;
}

// K-60(^Xs、M_hup): クラス自体の宣言コメント参照。
// ensureShellActive()とは異なり、既にShellモードにいる時に短絡し
// 「ない」――それが「再起動」というものの全ての意味である――そのため
// ensureShellActive()へ委譲するのではなく、
// shellStarted_/currentMode_/shellReturnMode_を直接管理する。
bool Application::shellRestart() {
  if (refuseIfShellBusy()) return false;
  shell_.terminate();
  shellStarted_ = false;
  // ensureShellActive()自身の同一の2026-09-15のコメント参照――
  // 「再起動」でも新しいbash子プロセスに現在のDirectory一覧のcwdを
  // 引き継がせる。
  if (!shell_.start(directoryMode_.currentDirectory())) return false;
  shellStarted_ = true;
  // ensureShellActive()自身の同一の2026-08-26のコメント参照: SHELLの
  // 引き渡し由来のページャセッションも、同じ理由で記録された戻り先
  // には決してなってはならない。2026-08-28: INFO(bannerFilePath())
  // も同様――その修正についてはensureShellActive()自身の同一の
  // コメント参照。
  if (currentMode_ != ApplicationMode::Shell &&
      !(currentMode_ == ApplicationMode::File && fileMode_.pagerReturnsToShell())) {
    ApplicationMode effectiveReturnMode = currentMode_;
    if (currentMode_ == ApplicationMode::File && fileMode_.currentFile() == bannerFilePath())
      effectiveReturnMode = ApplicationMode::Directory;
    shellReturnName_ = previousModeName(effectiveReturnMode);
    shellReturnMode_ = effectiveReturnMode;
  }
  // 2026-09-06追加: Application::returnPagerToShell()自身のコメント
  // 参照――^Xs(このshellRestart()自身)経由でpagerReturnsToShell()の
  // ページャを離れる場合も同じOS側のfdの解放が必要。streamFd_が
  // 元々無い(通常のFileモード等)場合は無害なno-op。
  fileMode_.closeStream();
  currentMode_ = ApplicationMode::Shell;
  shell_.scrollback().redraw();
  shell_.redrawPromptRow();
  return true;
}

std::string Application::previousModeName(ApplicationMode mode) const {
  switch (mode) {
    case ApplicationMode::Directory:
      return directoryMode_.currentDirectory();
    case ApplicationMode::File:
      // 2026-08-28修正、実際の報告("SHELLの戻り値が「~/.usilic/
      // usilic」"): このFileのケースの他の「全ての」呼び出し元は本物
      // のファイルを見ており、それ自身のパスを表示することがまさに
      // 正しい――しかし特にINFOバナー(合成されたファイル、
      // bannerFilePath()自身のコメント参照)については、FileMode自身の
      // モードラインのプロバイダは既に代わりに"INFO"を表示することを
      // 知っている(searchOriginLabel_、executeShowUsilicBanner()自身の
      // setSearchOrigin()呼び出しによって設定される)――この「別の」
      // 表示経路(SHELL/KEY自身の「戻り値」trailingフィールド)は、
      // 一度も同じ認識を得ておらず、代わりに生の合成されたパスを表示
      // していた。
      return fileMode_.currentFile() == bannerFilePath() ? "INFO" : fileMode_.currentFile();
    // Man(2026-09-19修正、直接のユーザー報告"^Jmls^M^S→戻り値不良。
    // 正しくは「MAN: ls」"): すぐ下のLocateのケースと全く同じ穴――
    // ここは裸の"ls"を返しており、drawPromptLabel()自身のtrailing
    // フィールド計算(destinationMode==Manの場合、例えばMANの中から
    // FGREP/EGREP/GREPを起動しまだパターンを投入していない段階)で、
    // 名前欄が"GREP"等の別のリテラルである以上、素の"ls"だけでは
    // どのモードへ戻るのか紛らわしい――Locateの"LOCATE: <query>"と
    // 揃え、"MAN: "接頭辞を付ける。KEY/SHELL自身のtrailing(name欄が
    // それぞれ"KEY"/"SHELL"であり、こちらも重複の心配が無い)、
    // ExitConfirmの"prev"欄等、他の全ての呼び出し元にも同じ接頭辞が
    // 一貫して波及するが、いずれも名前欄が既に"MAN"を名乗っている
    // 場面ではない(manView_自身の画面はmanReturnStack_経由でここを
    // 呼ばない――manReturnStack_の要素が自分自身Manになることは無い)
    // ので重複の心配は無い。
    case ApplicationMode::Man:
      return "MAN: " + manMode_.currentPage();
    case ApplicationMode::Archive:
    case ApplicationMode::ArchiveContent:
      return archiveMode_.currentArchive();
    // Du(2026-09-19六訂正、直接のユーザー報告+実機の正解モードライン
    // "^S→戻り値不良。正しくはDU: /Users/haa150...ic/CLAUDE.md
    // （DUモードを明示）"): 以前は集計対象のルート(duMode_.currentPath())
    // を返していた(duView_自身のModeLineFieldsProviderの直前の
    // 2026-09-19六訂正まで踏襲していた同じ慣習)が、ここでの実際の
    // 用途(destinationModeがDuになる、drawPromptLabel()自身の
    // GrepPatternプロンプト等)は「戻り先」の表示であり、「何を集計
    // 中か」ではない。duReturnStack_自身のfront()(恒久的な基点、
    // 常にFile/Directoryへ解決済み)を再帰的にpreviousModeName()へ
    // 通す――Man/Locateの"MAN: "/"LOCATE: "接頭辞と同じ形。
    case ApplicationMode::Du:
      return "DU: " + previousModeName(duReturnStack_.front());
    // Locate(2026-09-19追加、直接のユーザー報告"^J^L.md^M^S→...戻り値
    // 空っぽは今も健在"): このswitchにLocateのケースが一度も無く、
    // defaultの""へ落ちていたため、LOCATEから起動したプロンプト
    // (GrepPattern/CutPattern等、destinationModeがlocateReturnStack_
    // 経由でLocateになる場合)は、まだパターンが未入力/未投入の間、
    // drawPromptLabel()自身のtrailingフィールドが丸ごと空欄になって
    // いた。ここだけ他のケース(Directory/File/Man/Du、いずれも接頭辞
    // 無しの生の値)と異なり"LOCATE: "接頭辞を付けて返す――locateView_
    // 自身のModeLineFieldsProvider(それ自身の2026-08-16の経緯コメント
    // 参照)が確立した、LOCATE自身の画面の「通常の」自己表示規約
    // ("LOCATE: <query>"、クエリ単体では何の値か紛らわしいための接頭辞)
    // をそのまま踏襲している――ここはLOCATE「自身」の恒久的な識別
    // 表示であり、grepView_自身のtrailing3が(発動元がGroup1モードの
    // 場合に)組み立てる「MODE: query」形式の一時的な戻り値ラベルとは
    // 別物(混同しないこと、直後のGREP: File分岐の訂正コメント参照)。
    case ApplicationMode::Locate:
      return "LOCATE: " + locateMode_.query();
    // Grep(2026-09-19追加、manReturnStack_/locateReturnStack_自身の
    // application.hpp宣言コメント参照): マッチへ着地した後の
    // manView_/locateView_自身のtrailing3が、以前は専用のフラグで
    // "<GREP種別>: <pattern>"を特別扱いしていたが、ここにこの
    // ケースを追加することで、その特別扱いを丸ごと不要にできる――
    // manReturnStack_.back()/locateReturnStack_.back()がGrepの時も、
    // 他のどの戻り先とも同じくpreviousModeName()を素通しするだけで
    // 済む。副産物として、KEY/SHELL自身のtrailing(keyReturnMode_/
    // shellReturnMode_がGrepの場合――Grep結果一覧を見ている最中に
    // KEY/SHELLを起動する、以前は空欄だった)も一般的に埋まる。
    case ApplicationMode::Grep:
      return std::string(grepSyntaxLabel(grepSyntax_)) + ": " + grepMode_.pattern();
    // Shell(2026-08-13): 直接のユーザーの訂正――drawPromptLabel()自身
    // の宛先trailingフィールドは、以前は`mode == Directory`について
    // のみ何かを表示していた、それが結線されているPromptKindの唯一の
    // 2つであるGrepPattern/CutPatternだけが反映すべき確立した
    // 「Directoryへ戻る」ケースを持っていたという理屈による。ユーザー
    // は、この理屈が成り立たないと指摘した: 「あらゆる」プロンプトは
    // 何らかのモードから起動され、そのモード自身の表示名が、プロンプト
    // 自体の起動がたまたまcurrentMode_を変えるかどうかとは関係なく、
    // 宛先そのものである("PROGRAMを起動したモードは必ず存在する...
    // SHELLならSHELL等")。このリテラルな"SHELL"は、
    // previousModeName()自体がまだカバーしていなかった唯一のケースを
    // 埋める(既存の呼び出し元――Shell自身のshellReturnName_、K-2自身
    // のKEYのtrailingフィールド――は一度もそれを必要としなかった:
    // shellReturnName_は「Shellの前の」モードを捉え、Shell自体は決して
    // 捉えず、また何もensureShellActive()を先に通ることなくプロンプト
    // から直接Shellへ戻ることは無い)。
    case ApplicationMode::Shell:
      return "SHELL";
    // Cut/CutContent(2026-08-24修正、実際の報告+参照:
    // "^J^W...→「CUT　　　　　　　　　　　　」"――CUT/HELP/LOGは全て
    // 同じCutModeに裏打ちされた画面を再利用しており、そのどれも、
    // Directory(パス)/File(ファイル名)/Man(ページ)が持つような、
    // 単一の意味のある「現在の項目」の名前を持たない――すぐ上の
    // Shell自身のリテラルな"SHELL"のケースに合わせたリテラルな"CUT"
    // は、この関数のswitchが最初に書かれた時、単純に一度も追加され
    // ていなかった。この修正の前は空白のまま残されており、代わりに
    // shellReturnName_自身の「前のモード名が無い」フォールバック
    // (それ自身のModeLineFieldsProviderのコメント参照)が発火し、
    // "CUT"が属すべき場所に"usilic v1.5b"を表示していた。
    case ApplicationMode::Cut:
    case ApplicationMode::CutContent:
      return "CUT";
    // 2026-09-20追加、Command::StartUnderlineView自身の宣言コメント
    // 参照(`^Ou`)――すぐ上のShell/Cut自身のリテラルな一語のケースと
    // 同じ理屈(UNDER一覧自身の画面には、クエリ/パターンのような
    // インスタンスごとの詳細が無い)。
    case ApplicationMode::Underline:
      return "USILIC";
    default:
      return "";
  }
}

// application.hpp自身の宣言コメント参照。currentMode_から出発し、
// File/Directoryに行き着くまで、各モード自身の*ReturnMode_を辿る。
// Archive/ArchiveContentはreturnFromArchive()自身の確立された終着点
// (常にDirectory)を直接返す――archiveParentStack_というそれ専用の
// 機構は単純な*ReturnMode_変数ではないため、ここでは辿らない。
ApplicationMode Application::nearestFileOrDirectoryAnchor() const {
  ApplicationMode mode = currentMode_;
  for (int guard = 0; guard < 16; guard++) {
    switch (mode) {
      case ApplicationMode::File:
      case ApplicationMode::Directory:
        return mode;
      case ApplicationMode::Archive:
      case ApplicationMode::ArchiveContent:
        return ApplicationMode::Directory;
      case ApplicationMode::Grep:
        mode = grepReturnMode_;
        break;
      case ApplicationMode::Cut:
      case ApplicationMode::CutContent:
        mode = cutReturnMode_;
        break;
      // Man/Locate/Du自身のスタック(manReturnStack_/locateReturnStack_/
      // duReturnStack_、application.hpp自身の宣言コメント参照)は、
      // front()(先頭=恒久的な基点)が常に既にFile/Directoryへ解決済み
      // の終端値――この関数自身が過去にそれを計算した結果そのもの
      // なので、さらに辿る必要が無く、ここで直接returnしてよい。
      case ApplicationMode::Man:
        return manReturnStack_.front();
      case ApplicationMode::Locate:
        return locateReturnStack_.front();
      case ApplicationMode::Du:
        return duReturnStack_.front();
      // 2026-09-20追加、Command::StartUnderlineView自身の宣言コメント
      // 参照(`^Ou`): UNDER一覧は常にDirectoryから直接発動する(スタック
      // を辿る必要が無い、returnFromUnderline()自身のコメント参照)
      // ので、すぐ上のArchive/ArchiveContentと同じ形で直接returnする。
      case ApplicationMode::Underline:
        return ApplicationMode::Directory;
      case ApplicationMode::Copy:
        mode = copyReturnMode_;
        break;
      case ApplicationMode::ListBrowser:
        mode = listBrowserReturnMode_;
        break;
      case ApplicationMode::Shell:
        mode = shellReturnMode_;
        break;
      default:
        return ApplicationMode::Directory;
    }
  }
  return ApplicationMode::Directory;  // 保険――上の各*ReturnMode_は常に「より外側」を指すはずで、実際にはここへ到達しない
}

// 2026-09-19追加、manAnchor_/locateAnchor_/duAnchor_自身のapplication.hpp
// 宣言コメント参照(AnchorSnapshot、この関数の戻り値の型自体はさらに
// application.hpp冒頭の名前空間スコープの宣言参照)。`anchor`
// (nearestFileOrDirectoryAnchor()の戻り値、常にFile/Directoryの
// どちらか)が解決された「その瞬間」のfileMode_/directoryMode_の
// 実際の中身(基点がFileの場合はfileReturnMode_も併せて)を、凍結して
// 覚えておくために呼ぶ――後で(例えばDU自身のopenSelectedDuEntry()が
// 同じ共有されたfileMode_/directoryMode_/fileReturnMode_を全く無関係
// な内容へ上書きしてしまった後でも)restoreAnchorContent()で確実に
// 復元できるようにする。
AnchorSnapshot Application::anchorSnapshotFor(ApplicationMode anchor) const {
  AnchorSnapshot snapshot;
  if (anchor == ApplicationMode::File) {
    snapshot.path = fileMode_.currentFile();
    snapshot.fileReturnMode = fileReturnMode_;
  } else {
    snapshot.path = directoryMode_.currentDirectory();
  }
  return snapshot;
}

// anchorSnapshotFor()の対称的な相方: returnFromMan()/returnFromLocate()/
// returnFromDu()自身が、スタックの最後の1要素(=恒久的な基点)まで
// 戻ってきた「その瞬間」にだけ呼ぶ――途中の一時的なGrepの寄り道
// (スタックのそれ以外の要素)を通過するだけの時は呼ばない。pathが
// 空なら何もしない(anchorSnapshotFor()が呼ばれなかった/失敗した
// 場合の保険――空文字列でfileMode_.loadFile("")/
// directoryMode_.enterDirectory("")を呼んで却って壊すよりは、何も
// しない方が安全)。
void Application::restoreAnchorContent(ApplicationMode anchor, const AnchorSnapshot &snapshot) {
  if (snapshot.path.empty()) return;
  if (anchor == ApplicationMode::File) {
    fileMode_.loadFile(snapshot.path);
    fileReturnMode_ = snapshot.fileReturnMode;
  } else if (anchor == ApplicationMode::Directory) {
    directoryMode_.enterDirectory(snapshot.path);
  }
}

std::string Application::cutContentReturnLabel() const {
  std::string label;
  if (const CutSegment *segment = cutMode_.selectedSegment()) {
    std::size_t start = segment->label.find_first_not_of(' ');
    if (start != std::string::npos) label = segment->label.substr(start);
  }
  return "CUT:" + label;
}

// K-17(^X^W、M_w4): menu.c自身の`send_pp(hp->bp->pp, "a")`(追記
// モード)から移植――activeView()の現在の行をcurrentWorkFilePath_へ
// 書く、このセッションでこれ(あるいはK-64)が最初に使われた時に
// 新しいw.Nを確保し、それ以降の^X^Wの押下ごとにそれを再利用する
// (2026-08-18: なぜ代わりにK-64は常に新しい番号へ進むのかについては
// allocateWorkFilePath()自身のコメント参照)。どのモードからでも利用
// 可能(legacy自身のM_w4のケースにもモードのガードは無い)。ファイル
// を書き込みのために開けなかった場合はfalse(例えばApplicationが
// 空のhomeDirectoryで構築された場合、コンストラクタ自身のコメント
// 参照)。
bool Application::saveBufferToFile() {
  if (currentWorkFilePath_.empty()) currentWorkFilePath_ = allocateWorkFilePath();
  std::ofstream out(currentWorkFilePath_, std::ios::app);
  if (!out) return false;
  for (const auto &line : activeView().lines()) out << line.text << "\n";
  return true;
}

// K-61(^J.、M_chdir2): menu.c自身のm_w()のM_chdir2ケース("cd
// '<dir>'\r"、自動投入)から移植。legacyのlp->dir(このポートには
// DirectoryModeの外にそれに相当するものが無い、汎用の「現在の
// ディレクトリ」フィールド)に最も近いものとして、DirectoryMode自身
// の現在のディレクトリを使う。
bool Application::shellChangeDirectory() {
  if (refuseIfShellBusy()) return false;
  std::string dir = directoryMode_.currentDirectory();
  if (dir.empty()) return false;
  if (!ensureShellActive()) return false;
  shell_.commandLine().setText("cd '" + dir + "'");
  shell_.submitLine();
  return true;
}

// K-62(^J,、M_chdir): menu.c自身のm_w()のM_chdirケース(呼び出し元が
// 与えたコマンド文字列を転送する)から移植――usilicrc自身のK-62の
// 説明("Execute `ls -l \cM`")はリテラルなコマンドを直接名指しして
// いるので、legacy自身のap->commがどこから埋められたかを再導出する
// のではなく、それがここで送られるものである。
bool Application::shellRunLsL() {
  if (refuseIfShellBusy()) return false;
  if (!ensureShellActive()) return false;
  shell_.commandLine().setText("ls -l");
  shell_.submitLine();
  return true;
}

// K-63(^Jw、M_w): menu.c自身のm_w()のM_wケースから、逐語的にでは
// なく「精神的に」移植――legacyは`name`を一時ファイルへ書いてから
// "echo `cat <file>`"を組み立てる、これはひとえに、生成されたコマンド
// 文字列に名前を直接埋め込むことに関する1990年代のシェルのクォート
// の懸念を回避するためのものである;このポートのLineEditor::
// setText()は単に文字列を直接受け取る(ユーザーが実際にそれを投入
// するまでシェルのパースは一切起こらない)ので、コマンドラインに
// "echo <name>"を直接挿入することは、一時ファイルの迂回無しで同じ
// 目に見える効果を持つ。選択されているDirectoryModeのエントリの名前、
// あるいは既に何かを見ている場合はFileMode自身の現在のファイルの
// ベース名を使う――他のモードはまだカバーされていない(legacy自身の
// ap->nameに合わせた、あらゆるモードにわたって汎用的に追跡される
// 実際の「現在のアクセス名」は、このポートには存在しない)。
//
// 素のエントリ/ファイル名ではなく「完全な」パス(2026-08-18修正、
// 直接のユーザー報告: "新では「echo ./」とか「echo CLAUDE.md」とかに
// なる...SHELLのカレントディレクトリが(ls)のそれとは違う場合もある
// ので「./」「CLAUDE.md」のフルパスへの変更は必要"): SHELL自身の
// 現在のディレクトリは、DirectoryMode/FileMode自身のそれと一致する
// とは保証されていない(例えばSHELLへ直接タイプされた`cd`の後)ので、
// そこに挿入された素の相対名は、間違ったディレクトリに対して解決
// されるか、完全に失敗し得る。Directoryモード自身の自己エントリ
// ("./")は、ディレクトリパス自体だけになるよう特別扱いされている
// (冗長な"./"を落とす――そのまま連結しても依然として動く、
// `cat`/`echo`は末尾の"/./"を問題無く扱うが、奇妙に読める)。
bool Application::shellInsertName() {
  if (refuseIfShellBusy()) return false;
  std::string name;
  if (currentMode_ == ApplicationMode::Directory) {
    const DirectoryEntry *entry = directoryMode_.selectedEntry();
    if (entry) name = (entry->name == "./") ? directoryMode_.currentDirectory()
                                             : directoryMode_.currentDirectory() + entry->name;
  } else if (currentMode_ == ApplicationMode::File || shellReturnMode_ == ApplicationMode::File) {
    name = fileMode_.currentFile();  // 既にフルパス
  }
  if (name.empty()) return false;
  if (!ensureShellActive()) return false;
  shell_.commandLine().setText("echo " + name);
  shell_.commandLine().redraw();
  return true;
}

// K-64(^J^W、M_w2): 2026-08-18の「再設計」、直接のユーザー依頼――
// 特にDirectory/Fileについて、もはやworkRootDirectory_の下に番号付き
// のw.Nを一切確保しない。代わりに`<homeDirectory>/.usilic/<name>`の
// 下に保存する、ここで`<name>`は「現在の」モード自身のベース名
// ――DirectoryMode自身の現在のディレクトリのベース名(例えば
// ".../git/usilic/" -> "usilic")、あるいはFileMode自身の現在の
// ファイルのベース名(例えば".../CLAUDE.md" -> "CLAUDE.md")――で
// あり、番号付きのスロットを蓄積するのではなく押下のたびに上書きする、
// なぜなら今やその目的は使い捨てのスクラッチバッファではなく
// 「この」モードの永続的な、人間が読めるスナップショットだから
// である。2026-08-19追加: ArchiveContentもこのグループに加わった
// (下のそれ自身の分岐参照)、アーカイブの中から開かれたファイルに
// ついて、まさにこの命名/場所を期待していた直接のユーザー報告/
// スクリーンショット(=di)による。
//
// 他の「全ての」モードについてのw.Nのフォールバック(2026-08-18、
// 意図的に残してある): Grep/Cut/CutContent/Man/Locate/Archive/
// Shell/Du/ListBrowser/Keyには、Directory/File/ArchiveContentが今や
// 持っているような確立した「モード名」の概念が無い
// (すぐ上のshellInsertName()――K-63――自身の同一の、既に受け入れ
// られた「他のモードはまだカバーされていない」という制約に合わせ
// ている)――しかしK-63(他に同じ効果に到達する方法の無い便利な挿入)
// とは異なり、このキーは、既存の、既に修正済みのいくつかの回帰が
// そもそもShellモードに到達する唯一の方法でも「ある」(例えば、まさに
// この下のensureShellActive()のテスト: ^J^S/^J^C候補リストからまっ
// すぐ到達する"start, ^J^S, ^J^W, run a shell command...")。そこで
// これを黙ってno-opにすることは、単に「近代化しない」というだけで
// なく、その再現経路を静かに壊してしまっていたはずなので、
// Directory/File/ArchiveContent以外の全てのモードについては、以前と
// 全く同じに動作し続ける。
//
// activeView()の「現在の」行(Shellモードへ切り替える「前に」アク
// ティブだったビュー、そのためこれはensureShellActive()がcurrentMode_
// を変える前に実行される必要がある)を書く、以前のバージョン自身の
// 同一の順序に合わせている。
bool Application::shellInsertBufferContent() {
  if (refuseIfShellBusy()) return false;
  // Grep(2026-08-24修正、直接のユーザー報告+彼ら自身の診断:
  // "^J^Wは現在表示データのcatだが、FGREPの守備範囲はモードラインだけ
  // なので同モードの^J^Wは本来意味が無い...見た目で言えば背景の
  // Introductionページがcatされるのが自然"): FGREP/EGREPの結果自体は、
  // 検索が実際に「実行された」画面(grepReturnMode_)に対する、単に
  // フィルタリングされた「ビュー」にすぎない――(この関数が既に本物
  // の独立したソースとして扱っている他の全てのモードとは異なり)
  // 書き出す価値のある独立した「Grepモードのコンテンツ」というもの
  // は存在しない。ここでgrepReturnMode_を代入することは、それを
  // 命名とコンテンツの両方について完全に透過的なものとして扱う、
  // そのためGrepの結果画面からの^J^Wは、まさにその裏に実際に見えて
  // いるもの(マッチしたスニペット行の一握りだけでなく、例えば「元の」
  // HELPの章のページ)から押されたかのように振る舞う。
  ApplicationMode effectiveMode = (currentMode_ == ApplicationMode::Grep) ? grepReturnMode_ : currentMode_;
  std::string modeName;
  if (effectiveMode == ApplicationMode::Directory) {
    std::string dir = directoryMode_.currentDirectory();
    if (!dir.empty() && dir.back() == '/') dir.pop_back();  // そうしないと"/"終端のパスのfilename()は空になる
    modeName = std::filesystem::path(dir).filename().string();
  } else if (effectiveMode == ApplicationMode::File) {
    modeName = std::filesystem::path(fileMode_.currentFile()).filename().string();
  } else if (effectiveMode == ApplicationMode::ArchiveContent) {
    // 2026-08-19追加、直接のユーザー報告/スクリーンショット(=di):
    // "=di→^J^W→/Users/haa15030/.usilic/saved/国際報道２０２６...ts"
    // ――アーカイブコンテンツビューが、すぐ上のFileMode自身の実際の
    // ファイルシステムのケースと同じくらい「ファイル」である
    // (openSelectedArchiveEntry()自身のコメント参照)にも関わらず、
    // ArchiveContentは依然として上のこの関数自身のクラスコメントで
    // 説明された(「確立したモード名の概念が無い...Archive/
    // ArchiveContent...」)w.Nのフォールバックへ流れ落ちていた。選択
    // されているエントリ自身のベース名を使う、archiveContentView_
    // 自身のモードラインのプロバイダ(このビュー向けの
    // application.cppのsetModeLineFieldsProvider())と同じ。
    if (const ArchiveEntry *entry = archiveMode_.selectedEntry()) {
      modeName = std::filesystem::path(entry->name).filename().string();
    }
  }
  ContentView &source = viewForMode(effectiveMode);
  std::string savePath;
  if (!modeName.empty()) {
    // <homeDirectory>/.usilic/saved/(2026-08-19修正、直接のユーザー
    // 依頼: "^J^Wの保存先は「~/.usilic/saved/」（無ければ自動作成）
    // でよろしく")――usilicrc/history/error/shellと一緒に名前付きの
    // スナップショットを.usilic/へ直接落とすのではなく、専用のサブ
    // ディレクトリにする、最初の使用時に自動作成される、(起動時に
    // 無条件に作成される.usilic/自体とは異なり――main()自身の
    // usilicDirのmkdir参照)それが既に存在することを他の何も保証しない
    // ため。
    // 2026-09-27、saved/からdata/へ改名(dataDirectory()自身の宣言コメント参照)。
    std::string savedDir = dataDirectory();
    savePath = savedDir + "/" + modeName;
    // 2026-09-09追加、直接のユーザー報告("^X^D→履歴リストに
    // /Users/haa15030/.usilic/saved/が無い。SHELLでcatする際、同
    // ディレクトリにアクセスしたのは事実なので、それもまた履歴への
    // 登録条件に加えてほしい"): DirectoryMode::enterDirectory()自身の
    // pushRecent(RingCategory::RecentDirectory, ...)呼び出しと同じ
    // 理屈――ここも実際に(catされるファイルの置き場所として)この
    // ディレクトリへアクセスしているので、^X^D(K-48)の履歴リストに
    // 登録する。enterDirectory()自身の慣習(末尾"/"付き)に合わせる。
    history_.pushRecent(RingCategory::RecentDirectory, savedDir + "/");
  } else {
    currentWorkFilePath_ = allocateWorkFilePath();
    savePath = currentWorkFilePath_;
  }
  std::ofstream out(savePath, std::ios::trunc);
  if (!out) return false;
  for (const auto &line : source.lines()) out << line.text << "\n";
  out.close();
  if (!ensureShellActive()) return false;
  // 2026-08-27修正、実際の報告+再現("↑「[」等がエスケープされて
  // ない"): savePath(実際のアーカイブエントリ自身のベース名から構築
  // される、例えばこのフィクスチャ自身の"…[二][字]…")が、クォート
  // 無しでそのままコマンドラインへ入っていた――escapeArchiveMemberPattern()
  // がtar/unzip自身のメンバー引数についてまさに修正したのとまさに
  // 同じ種類のバグである(それ自身のコメント参照)、ただしここでは
  // argvリストではなく生のシェルコマンド文字列なので、個々のglob
  // 文字をバックスラッシュエスケープするのではなく、シングルクォート
  // (shellQuoteArg()、このファイルが1つを組み立てる他の全ての場所で
  // 既に使われている――それ自身のコメント参照)が正しい道具である。
  shell_.commandLine().setText("cat " + shellQuoteArg(savePath));
  shell_.commandLine().redraw();
  return true;
}

std::string Application::dataDirectory() const {
  std::string dir = std::filesystem::path(usilicrcPath_).parent_path().string() + "/data";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  return dir;
}

// allocateWorkFilePath()/allocateArcDirectory()自身の宣言コメント
// (application.hpp)参照。
std::string Application::allocateWorkFilePath() {
  std::string path = workRootDirectory_ + "/w." + std::to_string(++nextWorkFileNumber_);
  allocatedWorkFiles_.push_back(path);
  return path;
}

std::string Application::allocateArcDirectory() {
  std::string path = workRootDirectory_ + "/arc." + std::to_string(++nextArchiveNumber_);
  allocatedArcDirs_.push_back(path);
  return path;
}

// K-67: クラス自身の宣言コメント参照。
bool Application::executeShowErrorLog() {
  if (!std::filesystem::exists(errorLogPath_)) return false;
  if (!fileMode_.loadFile(errorLogPath_)) return false;
  currentMode_ = ApplicationMode::File;
  fileReturnMode_ = ApplicationMode::Directory;  // それ自身の宣言コメント参照
  return true;
}

// ^M等でファイルを開く前に、外部のプログラムへ渡すべきものならそうして
// trueを返す(2026-09-27に括り出し、DU/USILIC/^X^Fにも適用):
//   - usilicrcのEXEC(従来どおり)。
//   - 2026-09-27追加、直接のユーザー提案("pdf、jpg、bmp、tiff等はバイナリだと
//     判定されるファイルでもあるので...file(1)で画像ファイルだと判定された
//     ファイルはusilic画面にその旨表示する代わりにクイックルック行き"):
//     usilicrcのCODEでバイナリと判定され、かつfile(1)のMIMEタイプが画像
//     (image/*)かPDFなら、「(^X) 種類」の1行を表示する代わりにクイックルック
//     (qlmanage -p)で開く。qlmanageがある環境(macOS)だけ。
bool Application::openExternallyIfApplicable(const std::string &path) {
  if (configuration_.isExec(path)) {
    if (auto command = configuration_.programForSuffix(path)) execMode_.run(*command, path);
    activeView().redraw();
    return true;
  }
  if (::access("/usr/bin/qlmanage", X_OK) != 0 || !fileMode_.isBinaryFile(path)) return false;
  ChildProcess::Result result = ChildProcess::run({"file", "-b", "--mime-type", path});
  std::string mime = result.ok ? result.output.substr(0, result.output.find('\n')) : "";
  if (mime.compare(0, 6, "image/") == 0 || mime == "application/pdf") return runQuickLook({path});
  // 2026-09-27拡張、直接のユーザー質問("画像・pdf以外で見れそうなのは.numbers
  // とか。可能なヤツ一覧とかは無い？"): クイックルックが扱える種類の一覧は
  // `qlmanage -m plugins`(UTIと担当するジェネレーターの対応表)で取れる。
  // ファイル自身のUTI(と、その上位の種類)を`mdls -name kMDItemContentTypeTree`
  // で取り、どれかが一覧にあればクイックルックで開く(Numbers/Keynote/Pages、
  // Office、音声、動画、フォント等)。Spotlightが種類を返さない場所のファイル
  // は上の画像/PDFの判定だけになる。
  const std::set<std::string> &supported = quickLookSupportedTypes();
  if (supported.empty()) return false;
  ChildProcess::Result tree = ChildProcess::run({"mdls", "-raw", "-name", "kMDItemContentTypeTree", path});
  if (!tree.ok) return false;
  for (std::size_t open = tree.output.find('"'); open != std::string::npos;
       open = tree.output.find('"', tree.output.find('"', open + 1) + 1)) {
    std::size_t close = tree.output.find('"', open + 1);
    if (close == std::string::npos) break;
    if (supported.count(tree.output.substr(open + 1, close - open - 1))) return runQuickLook({path});
  }
  return false;
}

// `qlmanage -m plugins`の各行("  <UTI> -> <ジェネレーター> (...)")のUTIの集合。
// 起動後最初の1回だけ実行する(openExternallyIfApplicable()参照)。
const std::set<std::string> &Application::quickLookSupportedTypes() {
  static const std::set<std::string> types = [] {
    std::set<std::string> result;
    ChildProcess::Result plugins = ChildProcess::run({"qlmanage", "-m", "plugins"});
    if (!plugins.ok) return result;
    std::istringstream lines(plugins.output);
    std::string line;
    while (std::getline(lines, line)) {
      std::size_t arrow = line.find(" -> ");
      if (arrow == std::string::npos) continue;
      std::size_t start = line.find_first_not_of(' ');
      if (start < arrow) result.insert(line.substr(start, arrow - start));
    }
    return result;
  }();
  return types;
}

// K-96(^J^Od、2026-09-27): dataDirectory()自身の宣言コメント参照。
bool Application::executeOpenDataDirectory() {
  if (!directoryMode_.enterDirectory(dataDirectory() + "/")) return false;
  currentMode_ = ApplicationMode::Directory;
  display_.hideVisibleCursor();
  directoryView_.redraw();
  return true;
}

// K-97(^J^Ow、2026-09-27): ^[uの目次の「WORK」を直接開く。^Gで目次へ、
// もう一度^Gで(ls)へ戻る(目次から入った時と同じ)。
bool Application::executeOpenWorkList() {
  underlineReturn_.reset();
  underlineOriginDirectory_ = directoryMode_.currentDirectory();
  usilicLevel_ = UsilicLevel::Toc;
  if (!openUsilicGroup("WORK")) return false;
  currentMode_ = ApplicationMode::Underline;
  return true;
}

// K-68: クラス自身の宣言コメント参照。
// executeShowShellLog()(K-68、^X^Kl)とexecuteStartLog()(ESC l、
// LOG――2026-08-18追加、それ自身のコメント参照)で共有される: Shell::
// scrollback()の現在の行をlogFilePath_(既に引数の有無を意識済み、
// tempRootDirectory_自身のコメント参照)へ書く、そのため両方の機能
// は常に「同じ」最新のスナップショットを見る。Shellが一度も開始
// されていない(スナップショットするものが無い)か、ファイルが書き込
// みのために開けなかった場合はfalse。
bool Application::refreshShellLogFile() {
  if (!shellStarted_) return false;
  std::ofstream out(logFilePath_, std::ios::trunc);
  if (!out) return false;
  for (const auto &line : shell_.scrollback().lines()) out << line.text << "\n";
  return static_cast<bool>(out);
}

bool Application::executeShowShellLog() {
  if (!refreshShellLogFile()) return false;
  if (!fileMode_.loadFile(logFilePath_)) return false;
  currentMode_ = ApplicationMode::File;
  fileReturnMode_ = ApplicationMode::Directory;  // それ自身の宣言コメント参照
  return true;
}

// ESC l(LOG): (ShellモードのInside中に生のESCを横取りする)最初の
// 試み(Command::StartShellLog)が構造的に不可能だと判明した後、
// CLAUDE.mdの経緯の項目176が検証した「実際に機能する手動の手順」の、
// 本物の、直接の入り口となる版――feedShellByte()は、意図的に、
// 単独のESCがdispatchLeaderByte()に一切到達しないようにしている
// (vi-mode/Metaキーとの互換性、本物のシェルはそれを必要とする)、
// そのため、その試みは実際には決して発火し得ず、取り消された。
// これは重要な点で異なる: それは、他の全てのESCリーダーコマンドと
// まさに同じ方法(このswitch、dispatchLeaderByte()のescPending_の
// ケース)で到達される、そしてそれは――その同じ調査によれば――
// (検証済みの手動の手順自体がそれ自身の^Gの後に始まる、まさに)
// Shellモードの「外」でのみ到達可能である(Directory/File等)。
// かつては6つの手動ステップだった(^X^Klでスナップショット -> ^Gで
// Directoryへ戻る -> ログファイルを選ぶ -> ^Jbで開かずに分割 -> パター
// ンをタイプ -> Enter)ものが、この単一の自動のものへ折り畳まれる、
// その古い試みのリテラルなデモ文字列"usilic v1.5b"の代わりに、固定の
// 分割パターンとして"usilic-1.5b:"(項目335自身の新しい太字のSHELL
// プロンプトのマーカー)を使う。
bool Application::executeStartLog() {
  if (!refreshShellLogFile()) return false;
  if (!fileMode_.loadFile(logFilePath_)) return false;
  std::vector<StyledLine> sourceLines;
  sourceLines.reserve(fileView_.lines().size());
  for (const auto &line : fileView_.lines()) sourceLines.push_back({line.text, line.attributes});
  cutSyntax_ = GrepSyntax::Fixed;
  // split()の返り値をチェックした「後」ではなく、それを呼ぶ「前に」
  // 設定する――実際のPTYテストで発見: CutMode::split()は、この関数が
  // 実行される機会を一度も得る前に、内部でcutView_を(自身の最後の行で)
  // 再描画するので、モードラインのまさに最初のフレームは"LOG"では
  // なく"CUT"(cutOrigin_がたまたま既に何であれ)を示し、後の、無関係
  // な再描画で初めて自己修正していた。このファイル内の他の全ての
  // cutOrigin_の代入(executeCut()のFile/Directoryの分岐、
  // startHelp())にも影響する、既存の順序のバグだったと判明した
  // ――このバグと併せて修正した。最終的な失敗時に設定しても無害:
  // split()内部の全ての失敗経路は、それ自身のredraw()より前にreturn
  // するので、ここでの「無駄になった」代入を使って何かがレンダリング
  // されることは決して無い。
  cutOrigin_ = CutOrigin::Log;
  if (!cutMode_.split(sourceLines, "usilic-1.5b:", cutSyntax_)) return false;
  currentMode_ = ApplicationMode::Cut;
  return true;
}

// K-0(^T): 同時に実行中の、引数付きで起動された2つのインスタンスが、
// (実行中のインスタンス数に関わらず、表示される全てのパスが以前は
// `<homeDirectory>/.usilic`という1つの固定の場所だったため)ここで
// 互いの古いERROR/SHELLのコンテンツを表示していたという実際の報告
// (スクリーンショット=cm)への対応として、2026-08-18に再設計された。
// フィールドのレイアウトは(そもそもインスタンスごとの概念を持たない
// legacyの何かから移植したものではなく)ユーザー自身の明示的な設計
// である: 「Boot options」/「Startup file」は共通のラベル幅に右寄せ
// された単一行のフィールド;「ERROR」/「SHELL」/「WORK」はその
// 「同じ」幅に右寄せされた短いラベル(そのためどのコロンも揃う);
// WORKは、このセッションでこれまでに確保された全てのw.N
// (allocatedWorkFiles_、古い順――K-17/K-64がまだ使われていなければ
// 空)を列挙し、継続行はラベルを繰り返さずパスの列にインデントされる;
// arc.N(ネストしたアーカイブの降下、allocatedArcDirs_)も同じ方法で
// 続くが、(WORKとは異なり、常に、それ自身のラベルを、後に何も無く
// ても表示する)WORKとは異なり、空のラベルを結びつける先となる、
// 常に関連する単一の「唯一の」ARCという概念が無いので、空の場合は
// 完全に省かれる。「History file」/「User name」(どちらも「旧」
// レイアウトに存在した)は完全に無くなった: historyFilePath_は本当に
// セッションをまたいで蓄積するringであり、この再設計の「このインス
// タンスが今使っているものはこれ」という枠組みが対象とするインス
// タンスごとの成果物ではなく、User nameはこの回のどんな報告にも
// 一度も言及されなかった。
std::string Application::startupFilePath() const {
  std::string home = std::filesystem::path(usilicrcPath_).parent_path().parent_path().string();
  for (const std::string &candidate :
       {usilicrcPath_, home + "/.local/lib/usilic/usilicrc", std::string("/usr/local/lib/usilic/usilicrc")}) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(candidate, ec)) return candidate;
  }
  return usilicrcPath_;
}

std::string Application::bannerFilePath() const {
  return std::filesystem::path(usilicrcPath_).parent_path().string() + "/usilic";
}

// Command::Suspend(^Z)自身の宣言コメント参照。legacy自身のkey.c
// (M_susp、`f_M != M_shell`の分岐)からの移植: raw modeではISIGを
// 切っているのでカーネルの通常のジョブコントロールは発火しない
// ――leaveRawMode()で本物のtermio設定へ戻してから自分自身に
// SIGTSTPを送って初めて、シェルから見て「本当に」止まる。
//
// 2026-09-20追加、直接のユーザー報告("-Zは-g起動で丸レインボーが
// 回転"): -g(GraphicalDisplay)自体は制御端末のraw termioに一切
// 依存していない(SDL2のウィンドウ+イベントループ)ため、そもそも
// legacyのM_susp("シェルへ戻す")という概念自体が存在しない――
// それでもSIGTSTPを自分自身へ送ると、SDL2のイベントループ/描画が
// 応答しなくなり、macOS(ウィンドウサーバー)からは「応答無し」の
// スピナー(丸レインボー)としてしか見えない、本物のジョブコント
// ロールには繋がらないハングになる。display_.isGraphical()
// (Display::isGraphical()自身のコメント参照、DelegatingDisplayが
// 実際のバックエンドへ転送する)でGUIかどうかを判定し、GUIの場合は
// 何もせずfalseを返す――legacy自身の`f_M != M_shell`という除外を、
// legacyには存在しなかったGUIモードにも拡張したもの。
bool Application::handleSuspend() {
  if (display_.isGraphical()) return false;
  // 2026-09-28修正(Display::leaveForSuspend()自身のコメント参照): 以前は
  // カーソルのあった位置(画面の途中)で改行を出すだけだったため、
  // シェルの「Stopped」やプロンプトが一覧の途中に割り込んでいた。
  display_.leaveForSuspend();
  keyInput_.leaveRawMode();
  ::kill(::getpid(), SIGTSTP);
  // ここへ戻ってくるのはSIGCONTで再開された後――key_input.cpp自身の
  // handleCont()が(非同期のシグナルハンドラとして)既にreenterRawMode()
  // を呼び、g_resizePendingも立てている。consumeResizeSignal()で
  // それを消費し、リサイズの有無に関わらず無条件に完全な再描画で
  // 復帰する(legacy自身のvt_winsize()+条件付きkey_winch(0)を、この
  // 移植では単純化した形)。
  keyInput_.consumeResizeSignal();
  display_.reenterFromExternalProgram();
  handleResize();
  return true;
}

bool Application::executeShowUsilicBanner() {
  // 2026-08-28修正、実際の報告+再現("「~/.usilic/（ls）」でusilicrc
  // を選択→^I→^G→usilicrcへ戻らない"): 他の「全ての」Directoryから
  // Fileへの遷移は、まずカーソル自身の行を保存する(openSelectedEntry()
  // 自身の2026-08-27の修正が同一の形/コメントを持つ);この入り口は
  // 一度もそうしていなかったので、戻る途中のHistory::positionFor(
  // Directory, ...)は、復元すべき、以前の無関係な訪問より新しい
  // ものを何も持たなかった。無条件(currentMode_==Directoryにゲート
  // されていない): DirectoryMode自身のcurrentDirectory_は、単に
  // 「異なる」モードがたまたまアクティブだからといって変わらないし、
  // 下のfileReturnMode_はどのみち無条件にDirectoryなので、^Iが実際に
  // どのモードから押されたかに関わらず安全である。
  directoryMode_.saveCurrentCursorPosition();
  // 2026-08-28追加、infoPreviousFilePath_自身の宣言コメント参照:
  // ^Iが押された時に(ディレクトリ一覧でカーソルがハイライトされて
  // いるだけでなく)実際に「見られていた」本物のファイルがあれば、
  // それを覚えておく、そうすればバナーからの^Gは、常にディレクトリ
  // へフォールバックする代わりに、それを再ロードして戻ることができ
  // る。この呼び出し自体が「それ自身のWORK/arc.Nの子の1つからバナー
  // へ戻る」ホップ(入場時にfileReturnMode_ == File、
  // openSelectedBannerEntry()自身のfileReturnMode_ = Fileのコメント
  // 参照)である場合はスキップされる――そのホップ自身のcurrentFile()
  // は使い捨てのバナーの子であり、覚えておく価値のある本物のファイル
  // ではなく、infoPreviousFilePath_(このネストが始まる前の「元々の」
  // ^I押下によって設定される)はその往復を通じて変わらず生き残らな
  // ければならない。
  if (fileReturnMode_ != ApplicationMode::File) {
    if (currentMode_ == ApplicationMode::File && fileMode_.currentFile() != bannerFilePath()) {
      fileMode_.saveCursorPosition();
      infoPreviousFilePath_ = fileMode_.currentFile();
    } else {
      infoPreviousFilePath_.clear();
    }
  }
  constexpr int kLabelWidth = 12;  // "Boot options"/"Startup file"自身の幅――他の全てのラベルはこれに右寄せされる
  auto field = [](const std::string &label) {
    std::string padded = label;
    if (static_cast<int>(padded.size()) < kLabelWidth) padded = std::string(kLabelWidth - padded.size(), ' ') + padded;
    return padded + " : ";
  };
  std::string continuationIndent(kLabelWidth + 3, ' ');  // field()自身の" : "に続く列に揃える

  // 2026-08-28追加、直接のユーザー報告("選べない項目は...装飾無しで
  // よろしく"): 「Boot options」(決して開けない、リテラルなargv
  // テキスト、openSelectedBannerEntry()自身のコメント参照)と空の
  // 「WORK :」(まだパスが全く無い、このセッションでK-17/K-64が一度
  // も使われていない)は、以前は本当に開けるどの行とも同じハイライト
  // を得ていた。ERROR/SHELLは、それら自身のファイルがまだ存在しない/
  // 実際のコンテンツを持たないかもしれないにも関わらず、意図的に
  // ここには含まれていない――すぐ下のtouch()呼び出しと
  // openSelectedBannerEntry()自身の「No String」の処理参照: 同日の
  // 追跡報告が、それらのハイライトも抑制すると「まだここには何も
  // 無い」ではなく「このパスが最終的な、正典の値『である』」と読める
  // 危険があると指摘した、そのためその2つは一様に開けそうな見た目
  // のままにし、代わりに要求に応じて実際の(空であれ)コンテンツを
  // 表示する。1行につき1つのbool、下のバナーのテキストと「同じ」
  // 順序で組み立てる。
  std::vector<bool> openable;
  openable.push_back(false);  // 「Boot options」――リテラルなargvテキスト、決してパスではない
  openable.push_back(true);   // 「Startup file」――usilicrc、常に実在する
  openable.push_back(true);   // 「ERROR」――下でtouchされる、それ自身のコメント参照
  openable.push_back(true);   // 「SHELL」――下でtouchされる、それ自身のコメント参照
  // 2026-09-14追加、直接のユーザー依頼("usilic内での発動は^Tで行う"):
  // 「TUTORIAL」――tutorialPath_自身のコメント参照(インストール先
  // $PREFIX/lib/usilic/TUTORIAL、~/.usilic/ではない)。Startup file/
  // ERROR/SHELLとは異なり実体はTUTORIAL自身が固定で持つ(このセッション
  // 中に何かが起きて初めて存在する類のファイルではない)ので、touch()は
  // 不要――未インストール環境(TUTORIAL自体が無い)ではopenSelectedBannerEntry()
  // 自身のCUT再生がfileMode_.loadFile()の失敗経由で黙ってno-opになる、
  // ERROR/SHELLがまだ空の時と同じ扱い。
  openable.push_back(true);
  for (std::size_t i = 0; i < (allocatedWorkFiles_.empty() ? std::size_t{1} : allocatedWorkFiles_.size()); i++)
    openable.push_back(!allocatedWorkFiles_.empty());
  for (std::size_t i = 0; i < allocatedArcDirs_.size(); i++) openable.push_back(true);

  // 2026-08-28追加、直接のユーザー提案("最初にそれら（/tmp/
  // .usilic.NNNNN/shell等）をtouchしておき、実際にアクセスしてアン
  // ダーライン「No String」を表示する仕様に変更してほしい"): ERROR/
  // SHELLはまだ実際のコンテンツを持たないかもしれない(このインスタ
  // ンスでは何もログされていない/シェルセッションが開始されていない)
  // ――ここでtouchする(無ければ空で作成し、既に実在すればそのまま
  // にする)ことで、openSelectedBannerEntry()自身のloadFile()は、
  // 黙って失敗するのではなく、この2つについて常に成功する;それ自身
  // の「ロードされたコンテンツが空 -> No Stringを表示する」処理
  // (それ自身のコメント参照)が残りをカバーする。
  {
    std::ofstream errorTouch(errorLogPath_, std::ios::app);
    std::ofstream shellTouch(logFilePath_, std::ios::app);
  }

  std::ostringstream banner;
  banner << field("Boot options") << bootOptions_ << "\n";
  banner << field("Startup file") << startupFilePath() << "\n";
  banner << field("ERROR") << errorLogPath_ << "\n";
  banner << field("SHELL") << logFilePath_ << "\n";
  banner << field("TUTORIAL") << tutorialPath_ << "\n";
  banner << field("WORK");
  for (std::size_t i = 0; i < allocatedWorkFiles_.size(); i++) {
    if (i) banner << continuationIndent;
    banner << allocatedWorkFiles_[i] << "\n";
  }
  if (allocatedWorkFiles_.empty()) banner << "\n";
  for (std::size_t i = 0; i < allocatedArcDirs_.size(); i++) banner << continuationIndent << allocatedArcDirs_[i] << "\n";

  std::string text = banner.str();
  std::vector<unsigned char> bytes(text.begin(), text.end());
  // "usilic"ではなく"usilic"(2026-08-18): FileMode自身のモードラインの
  // プロバイダは、その`name`フィールドをロードされたファイルの素の
  // ベース名から導出する(それ自身のコメント参照)――これが、専用の
  // ContentView/モードを必要とせずに、依頼された「ここで表示される
  // モード名はusilic」の背後にある仕組みの全てである。
  // tempRootDirectory_/allocateArcDirectory()ではなくbannerFilePath()
  // 自身のディレクトリ――それ自身の宣言コメント参照。
  std::filesystem::path bannerPath(bannerFilePath());
  std::string tempPath =
      materializeArchiveTempFile(bannerPath.parent_path().string(), bannerPath.filename().string(), bytes);
  if (tempPath.empty() || !fileMode_.loadFile(tempPath)) return false;
  // WORK/arc.Nの候補リストの選択(2026-08-18修正、=cq): 実際の報告に
  // より、これらの行が全く選び出せないことが判明した――FileMode自体
  // がデフォルトでcursorHighlightEnabled_を無効にしている(プレーンな
  // ページャの読み取りには正しい、それ自身のコンストラクタのコメント
  // 参照)、この画面は特にそれを再びオンにする必要がある。loadFile()
  // 自体は、呼び出しのたびにそれをその同じ「オフ」のデフォルトへ
  // リセットする(それ自身のコメント参照)ので、これは次にロードされる
  // ものへ漏れ出さない。
  fileView_.setCursorHighlightEnabled(true);
  // 「<label> : 」という区切りの後からだけハイライトする(2026-08-18
  // 修正、直接のユーザー報告: "一覧表示のハイライトを「: 」の後から
  // にしてほしい"): この合成されたバナー向けにloadFile()/
  // readAndWrap()がちょうど生成した全ての行は、まさに同じ形をして
  // いる――`field(label) + value`か`continuationIndent + value`の
  // どちらか――どちらも「値」をこの同じ固定の列(continuationIndent
  // 自身の幅)に置くので、単一の一様なhighlightStartColumnが、それが
  // どのフィールドに属するかを区別する必要無く、全ての行について
  // 機能する(`ls -l`の行についてのDirectoryMode自身のnameStartColumn
  // の前例を反映している)。注: 2つ目の物理行へ「折り返される」ほど
  // 長い値(readAndWrap()自身の仕業、このコードがその結果を見る前に
  // 起こる)は、その継続行にもこの同じ列を適用してしまい、そこでの
  // ハイライトをわずかに誤配置する――稀なエッジケースとして受け
  // 入れる(このポート自身の$HOME/usilicrcのパスは、通常、折り返し
  // 前に利用可能な約65列分の予算よりはるかに短い)、ここでは解決
  // していない。
  {
    std::vector<ContentView::Line> lines = fileView_.lines();
    for (std::size_t i = 0; i < lines.size(); i++) {
      // highlightStartColumnは、開けるかどうかに関わらず、常にその行
      // 自身の「値の先頭」の列(continuationIndent.size())である――
      // 2026-08-28修正、実際の報告("Boot optionsは...ブロックカーソル
      // 位置は「WORK : 」と同じでよろしく"): これへの「以前の」試み
      // は、代わりに開けない行のhighlightStartColumnをそれ自身のテキ
      // ストの末尾より先へ向けており、それはたまたま本物の端末の
      // 「カレット」もそこへ動かしてしまっていた(syncVisibleCursor()
      // はこの同じフィールドを使う)――空の場合の「WORK :」について
      // は問題無い(ラベルの後に何も続かないので両方の列が一致する)
      // が、「Boot options」については間違っている(bootOptions_は
      // 本物の長い文字列になり得る)、WORK自身の慣習が置く場所より
      // はるか先にカレットを着地させてしまう。実際に今SGRのハイライト
      // を抑制するのはLine::neverHighlighted(この列ではない)なので、
      // 「カレットがどこに座るか」と「この行が目に見えてハイライト
      // されることがあるか」という2つの懸念は独立している。
      lines[i].highlightStartColumn = static_cast<int>(continuationIndent.size());
      lines[i].neverHighlighted = !(i < openable.size() && openable[i]);
    }
    fileView_.setLines(std::move(lines));
  }
  currentMode_ = ApplicationMode::File;
  // いつも通りfileReturnMode_ = Directoryだが、returnToDirectory()
  // 自身の2026-08-18のコメント参照: 他の全てのFileモードの入り口とは
  // 異なり、これは戻る途中で意図的に「ファイルの実際の親ディレクトリ
  // をたどる」という扱いを受けない(このファイル自身のディレクトリ、
  // tempRootDirectory_は、実質的に、ユーザーが実際に行き着きたい場所
  // では決して無い)。
  fileReturnMode_ = ApplicationMode::Directory;  // それ自身の宣言コメント参照
  // 戻り先の表示(2026-08-18修正、直接のユーザー報告: "-c
  // ~/git/usilic"経由で起動してから^Tすると、trailingの「戻り値」
  // フィールドに"~/git/usilic"ではなく"/Users/haa15030/.usilic"が
  // 表示された)。FileMode自身のデフォルトのtrailing2(そのコンスト
  // ラクタ自身のコメント参照)は、ロードされたファイル「自身」の
  // 格納先ディレクトリである――本物のファイルについては正しいが、
  // このファイルの実際の親(bannerFilePath()自身のディレクトリ)は、
  // 上の^Gが実際に戻る先では決してない(すぐ上のコメント参照);
  // setSearchOrigin()は、まさに「ここに表示される宛先を上書きする」
  // ためにもう存在している(LOCATE自身のFile結果も同一にそれを使う)。
  // directoryMode_.currentDirectory()は、上のfileReturnMode_が無条件
  // にDirectoryであり、その経路上の何もDirectoryMode自身の状態に
  // 触れないので、^Iが実際にどのモードから押されたかに関わらず正しい。
  // 2026-08-27のリネーム、直接のユーザー報告: そのラベル
  // (bannerFilePath()自身のベース名に合わせた"usilic")は、この画面
  // のモードラインの`name`フィールドとしても重複していた(FileMode
  // 自身のModeLineFieldsProviderのコメント参照: 空でないsearchOriginLabel_
  // は、trailing2だけでなく`name`も完全に上書きする)――あたかも
  // 実在の普通のファイルが文字通り"~/.usilic/usilic"という名前で
  // 見られているかのように読めた。今は、割り当て直されたキー自身の
  // kKeyTableのラベルに合わせて"INFO"。
  // 2026-08-28修正、直接のユーザー報告("CLAUDE.mdから来たのに戻り値
  // が「~/git/usilic/」のまま。^GでCLAUDE.mdには戻るので戻り値の修正
  // だけよろしく"): ^Gの実際の「宛先」は、設定されていれば既に
  // infoPreviousFilePath_をたどっている(それ自身の宣言コメント参照)
  // が、この「表示される」trailingフィールドは、それに関わらず依然
  // として無条件にdirectoryMode_.currentDirectory()を表示していた
  // ――この2つはずれてしまっていた。今は^Gが実際に使う方をそのまま
  // 反映する。
  fileMode_.annotations().pushWithNameOverride(
      FileMode::FileAnnotationKind::Search, "INFO",
      infoPreviousFilePath_.empty() ? directoryMode_.currentDirectory() : infoPreviousFilePath_);
  // 2026-08-28修正、実際の報告+再現("usilic -c ~/git/usilic→^I→
  // 名前欄が「INFO」ではなく「usilic」、戻り値が「~/.usilic/」の
  // まま"): 上のloadFile()は既に内部で一度、FileMode自身の「デフォル
  // ト」のモードラインで再描画していた――その時点ではsearchOriginLabel_/
  // searchOriginDetail_はまだ空だった(loadFile()自体がそれらをクリア
  // する、それ自身のコメント参照)ので、その最初のフレームは
  // `name = path.filename().string()`(bannerFilePath()自身のベース
  // 名、"usilic")と`trailing2 = directory`(bannerFilePath()自身の
  // 格納先ディレクトリ、"~/.usilic/")にフォールバックしていた
  // ――まさに報告された2つの間違った値。setSearchOrigin()が実際に
  // 実行された今、その古いフレームを訂正済みのものに置き換えるため
  // に、その後何も再び再描画していなかった。これは、
  // openSelectedBannerEntry()が2026-08-18に既に修正した(それ自身の
  // コメント参照)、まさに同じ隙間であり、単にここにも適用されて
  // いなかっただけである。実際の対話的な実行はこれを即座に示す
  // (後のキー入力に駆動される再描画がたまたまそれを覆い隠すことは
  // 無い);このポート自身のテストがこれを一度も捕まえなかったのは、
  // そこではfeedByte()の後にどのみち無条件の再描画が続くためである。
  fileView_.redraw();
  return true;
}

// K-0/^T自身の候補リストの選択(2026-08-18): 元のモックアップは、
// WORK/arc.Nの行に"[↑ ハイライト文字列選択後は][戻り値「usilic」
// 画面へ]"と注釈を付けていた――それらの行のどれかを選択してEnterで
// それを開き、^Gがまさにこのバナーへ戻ってくる。これを専用の
// ListBrowserModeスタイルの画面として作り直すのではなく、他の全ての
// ContentViewが既に持っているのと「同じ」汎用のcursorIndex_/
// moveUp()/moveDown()を再利用する(ContentView::redraw()自身の、構造
// 的にモードに依存しないハイライト行のレンダリング参照)――
// fileView_は、cutContentView_/scrollback_がそうするようにはカーソル
// のハイライトから一度も「外れて」いなかったので、上下+ハイライト
// された行はここで既に無料で動作していた;Enterだけが新しい配管を
// 必要とした。行全体の一致ではなく「末尾」で一致させる: 「WORK」行
// 自体は継続/arc.Nの行には無い"WORK : "というラベルの接頭辞
// (field()自身の書式)を持っているので、末尾でのマッチングは、
// 最初の行を特別扱いする必要無く、両方の形を一様に処理する。w.N
// のエントリは「ファイル」として開く(fileReturnMode_ = File、
// Command::Back自身のFileの分岐に追加された新しいケース、
// returnToDirectory()ではなくexecuteShowUsilicBanner()へまっすぐ
// ルーティングする――そのswitch自身のコメント参照);arc.Nのエント
// リは、スタックのフレームを積まない、プレーンなenterDirectory()
// 経由で「ディレクトリ」として開く(通常の降下の外から到達した時、
// ^Xd経由で任意のパスへジャンプすることが既に振る舞うのを反映して
// いる: スタックが空になった時点で、ユーザーがここからさらにいくつ
// のディレクトリへ降りることになろうと、それをまたいで状態を持つ
// 「バナーへ戻る」フラグが古くなる危険を冒すよりも、^Gは無害な
// no-opになる)。
// 2026-09-20追加、直接のユーザー依頼("INFOは^I、TUTORIALは^T"):
// 以前は^Tが^Iへ丸ごと付け替えられ(2026-08-27)、TUTORIALは^I画面
// (INFO banner)の中の1行としてしか到達できなくなっていた――このport
// 自身のここ数日の変更で新設された機能なのでlegacyには先例が無いが、
// 実際にはTUTORIAL専用の直接キーとして^T自体を(空いていたので)復活
// させたいという要望。openSelectedBannerEntry()自身の「TUTORIAL」行
// 分岐と全く同じCUT-by-file再生ロジックをここへ括り出し、両方の入口
// (^I画面での選択、^T直接)から共有する。
bool Application::openTutorial() {
  return openRecentFileEntry(buildCutFileHistoryEntry(tutorialPath_, tutorialCutPattern(), GrepSyntax::Basic));
}

bool Application::openSelectedBannerEntry() {
  if (fileMode_.currentFile() != bannerFilePath()) return false;
  const auto &lines = fileView_.lines();
  int idx = fileView_.cursorIndex();
  if (idx < 0 || idx >= static_cast<int>(lines.size())) return false;
  const std::string &text = lines[idx].text;
  auto endsWith = [&text](const std::string &suffix) {
    return !suffix.empty() && text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
  };
  // Startup file/ERROR/SHELL(2026-08-18修正、直接のユーザー報告:
  // "ハイライト上の[return]が反応しない"――ここでの全ての行は今や
  // 同じ方法でハイライトされている(executeShowUsilicBanner()自身の
  // highlightStartColumnのコメント参照)ので、これら3つのどれかに
  // カーソルを着地させたユーザーは、黙って何もしないのではなく、
  // Enterがそれが名指しするファイルを開くことを合理的に期待する
  // (以前はWORK/arc.Nだけが結線されていた)。「Boot options」は意図
  // 的に除外されている――それはリテラルなargvテキストであり、どんな
  // 本物のファイルへのパスでもない。下のWORKと同じfileReturnMode_
  // = Fileの慣習で「ファイル」として開くので、^Gはこのバナーへ
  // まっすぐ戻ってくる。loadFile()自体は、まだ存在しないパス(そこに
  // 何かがログされる前のERROR/SHELL)については既に優雅に(false)
  // 失敗する――その場合、Enterは、空のWORKスロットについて既にそう
  // だったのと同様、単にno-opのままである。
  // loadFile()がcurrentFile_を上書きする/ビューをリセットする「前に」
  // saveCursorPosition()(2026-08-18修正、直接のユーザー報告: "^Gで
  // 同じ行に戻らない"――このクラス内の他の「全ての」Fileモードから
  // Fileモードのような遷移は、まず「離れる」位置を保存する、
  // returnFromFileToLocate()自身の同一の2行の形に合わせているが、
  // これは一度もそうしていなかった――loadFile()自身のHistory::
  // positionFor()の問い合わせ(bannerFilePath()、固定のパスをキーに
  // している)には復元すべきものが何も無かったので、バナーへの^Gは、
  // 実際にどの行が選択されていたかに関わらず、常に行0に着地して
  // いた)。
  //
  // loadFile()の「後の」setSearchOrigin("usilic")(同じ報告: "戻り値
  // が不良"): 本物のファイルについてのFileMode自身のデフォルトの
  // trailing2は、その格納先ディレクトリである(コンストラクタ自身の
  // コメント参照)――真だがここでは誤解を招く、なぜならこれらのどれ
  // からの^Gも、`/tmp/.usilic.<pid>/`のディレクトリ一覧へは全く導か
  // ず、まっすぐ^Tバナー画面へ戻る、上のバナー自身のtrailing2に既に
  // 適用された「ここに表示される宛先は^Gが実際に行く先であるべき」
  // という修正とまさに同じ。`label`はファイル自身の素のベース名
  // (FileModeが既に表示していたであろうものから変わっていない)と
  // して渡されるので、モードラインの宛先の半分だけが実際に変わる。
  // 2026-09-14追加、直接のユーザー依頼("usilic内での発動は^Tで行う"):
  // 「TUTORIAL」行は他の固定パス行(Startup file/ERROR/SHELL、プレーン
  // なloadFile())とは異なり、CUT-by-fileそのものを再生する必要がある
  // ――openTutorial()自身のコメント参照(2026-09-20、^T単独キーとの
  // 共有のため括り出した)。
  if (endsWith(tutorialPath_)) return openTutorial();
  for (const std::string &fixedPath : {startupFilePath(), errorLogPath_, logFilePath_}) {
    if (!endsWith(fixedPath)) continue;
    // 2026-08-27修正、実際の報告("^T→「/tmp/.usilic.NNNNN/shell」
    // [return]→反応なし（ファイルコピーがSHELLで行われたのに）"):
    // usilicrcPath_/errorLogPath_(usilic自身が進行につれて書く本物
    // のファイル)とは異なり、logFilePath_はあくまで「スナップ
    // ショット」にすぎない――実際にShell::scrollback()をそれへダンプ
    // するのはrefreshShellLogFile()(それ自身のコメント参照)であり、
    // そうでなければ、シェルセッション自身のメモリ上のscrollback
    // (例えばまさにこのCP自身の出力)が実際のコンテンツを持っている
    // にも関わらず、それはまだディスク上に全く存在しない。
    // executeShowShellLog()/executeStartLog()は、まさにこの理由で
    // 既に最初にそれを更新している;この入り口は一度もそうしていな
    // かった。
    if (fixedPath == logFilePath_) refreshShellLogFile();
    fileMode_.saveCursorPosition();
    if (!fileMode_.loadFile(fixedPath)) return false;
    // 2026-08-28追加、直接のユーザー提案("最初にそれら（/tmp/
    // .usilic.NNNNN/shell等）をtouchしておき、実際にアクセスしてアン
    // ダーライン「No String」を表示する仕様に変更してほしい"):
    // executeShowUsilicBanner()は今やerrorLogPath_/logFilePath_を前もって
    // touchするので、それらは常に存在し、一様にハイライトされた/
    // 開けそうな見た目のままである(それ自身のコメント参照)――しかし
    // 何かがログされる前のERROR、あるいは本物の出力がある前のSHELL
    // は、本当に「空」のファイルである。空白の画面(本物の(退屈だが)
    // コンテンツと間違われ得る)ではなく、「No String」(下線付き)
    // として表示する、GrepMode::search()/ContentView::setEmptyPlaceholder()
    // が「ここには何も無い」について既に確立したのと「同じ」慣習。
    if (fileView_.lines().empty()) {
      static const std::string kNoString = "No String";
      fileView_.setLines({ContentView::Line{
          kNoString, std::nullopt, std::nullopt, {AttributeRun{0, static_cast<int>(kNoString.size()), Display::Attribute::Underline}}}});
    }
    fileMode_.annotations().pushWithNameOverride(FileMode::FileAnnotationKind::Search,
                                                  std::filesystem::path(fixedPath).filename().string(), "INFO");  // 2026-08-27リネーム
    fileReturnMode_ = ApplicationMode::File;
    // loadFile()は既に内部で一度、デフォルトのモードラインで再描画
    // していた(その時点ではsearchOriginはまだ空だった――loadFile()
    // がそれをクリアした「後」、すぐ上でだけ設定される)――明示的な
    // 2回目のredraw()が、実際に訂正済みのtrailing2を実際の画面へ
    // 反映させるものである、まさに同じ理由によるopenSelectedLocateResult()
    // 自身の同一の順序/コメントに合わせている。
    fileView_.redraw();
    return true;
  }
  for (const auto &workPath : allocatedWorkFiles_) {
    if (!endsWith(workPath)) continue;
    fileMode_.saveCursorPosition();
    if (!fileMode_.loadFile(workPath)) return false;
    fileMode_.annotations().pushWithNameOverride(FileMode::FileAnnotationKind::Search,
                                                  std::filesystem::path(workPath).filename().string(), "INFO");  // 2026-08-27リネーム
    fileReturnMode_ = ApplicationMode::File;
    fileView_.redraw();  // 上のfixedPathループ自身の同一のコメント参照
    return true;
  }
  for (const auto &arcPath : allocatedArcDirs_) {
    if (!endsWith(arcPath)) continue;
    if (!directoryMode_.enterDirectory(arcPath)) return false;
    currentMode_ = ApplicationMode::Directory;
    return true;
  }
  return false;
}

// K-27: クラス自身の宣言コメント(application.hpp)参照。
// M_cp/M_mv(^Xc/^Xm)がマーキング/閲覧のサブモードに入るところから
// 移植――2026-08-21の変更(実際のユーザー報告、=ecスクリーンショット):
// これは以前は即座に宛先プロンプトを有効化しており、ユーザーがまだ
// 何もマークしていない段階で空のテキスト入力ボックスを表示していた。
// それはマーキングが終わった時点で、別個の^X^M押下をアーム済みで
// 待っていた(PendingCpMvRmOperation自身のコメント参照)。2026-08-27
// の再設計、直接のユーザー報告("ファイル選択後の冗長性...さらに
// ^X^Mとやるのがめんどくさい")+彼ら自身が提案した修正("真っ新
// （ls）時は発動しない"/"項目を一つでも選択すれば発動→そのまま文字
// 列入力へ"): マーキングは、後で^X^Mの時点でチェックされる前提条件
// というだけでなく、今や有効化のゲート自体を兼ねる――何もマーク
// されていない(「真っ新」な一覧)は下では単なるno-opであり、まるで
// そのキーが一度も押されなかったかのようであり、一方「どんな」
// マークされたエントリでも、アームすると同時に、同じキー入力で
// 即座にstartDestinationPrompt()を呼び、今や冗長になった別個の^X^M
// を完全にスキップする。ここでは意図的にtargetEntries()ではなく
// markedCount()をチェックしている(そのカーソルへのフォールバック
// ――クラスコメント参照――は、そうしないと「何もマークされていない」
// を「1項目マークされている」と区別が付かなくしてしまい、このゲート
// の目的全体を台無しにするため)。
bool Application::startCopyPrompt() {
  cpMvRmOrigin_ = ApplicationMode::Directory;
  cpMvRmListTargets_.clear();
  if (currentMode_ != ApplicationMode::Directory) return startListCpMvRmPrompt(PromptKind::CopyDestination);  // 2026-09-27
  if (directoryMode_.markedCount() == 0) return false;
  if (switchToShellIfBusy()) return true;  // それ自身のコメント参照
  pendingCpMvRm_ = PendingCpMvRmOperation{PromptKind::CopyDestination, directoryMode_.currentDirectory()};
  directoryMode_.setPendingOperationLabel("CP");
  // 2026-08-27修正、実際の報告("残留ハイライトを消去かつ全アンダー
  // ラインを全ハイライトにしてみて"): この宛先プロンプトが有効な
  // フェーズ全体が、カーソル行のハイライトを一切示さず、本当にマーク
  // されている行をUnderlineではなくStandoutでレンダリングするという、
  // 再設計以前のlegacyの慣習(=ed参照スクリーンショット)を復元する
  // ――2026-08-27の1ステップの再設計がアーム+プロンプトを1つの呼び
  // 出しへ折り畳んだ時に失われた、startDestinationPrompt()自体が、
  // それが実行された瞬間に無条件で両方のフラグをtrueへ戻していた
  // ため(古い2ステップの流れについては正しい、そこではそれが常に
  // どちらへ進むにせよ「アーム/抑制された状態を後にする」ことを
  // 意味したが、startCopyPrompt()等がまさに同じキー入力でこれへ
  // 直接呼び込むようになった今では間違っている――ここで無条件に
  // 復元することは、1フレームがそれを表示する機会を得る前に、まさに
  // この抑制を元に戻してしまっていた)。startDestinationPrompt()は
  // もう無条件にはその復元を行わない――それ自身のコメント参照。
  // (下のstartDestinationPrompt()の内部から呼ばれる)activatePrompt()
  // は、プロンプト自身の行(drawPromptLabel()/promptEditor_.redraw())
  // だけを描き――一覧自身のコンテンツ行を再描画することは決して無い、
  // そのためここでの明示的なredraw()が、実際にこの2つのフラグの新しい
  // 効果を塗るものである;これが無いと、コンテンツ行は単に(マーキング
  // による)「最後の」本物の再描画が既に塗ったものを表示し続け、この
  // 修正全体を黙って見えなくしてしまう。
  directoryView_.setCursorRowAlsoHighlighted(false);
  directoryView_.setMarkedRowUsesUnderline(false);
  directoryView_.redraw();
  return startDestinationPrompt();
}

bool Application::startMovePrompt() {
  cpMvRmOrigin_ = ApplicationMode::Directory;
  cpMvRmListTargets_.clear();
  if (currentMode_ != ApplicationMode::Directory) return startListCpMvRmPrompt(PromptKind::MoveDestination);  // 2026-09-27
  if (directoryMode_.markedCount() == 0) return false;
  if (switchToShellIfBusy()) return true;  // それ自身のコメント参照
  pendingCpMvRm_ = PendingCpMvRmOperation{PromptKind::MoveDestination, directoryMode_.currentDirectory()};
  directoryMode_.setPendingOperationLabel("MV");
  directoryView_.setCursorRowAlsoHighlighted(false);  // startCopyPrompt()自身のコメント参照
  directoryView_.setMarkedRowUsesUnderline(false);
  directoryView_.redraw();
  return startDestinationPrompt();
}

// M_rm(^Xr)がマーキング/閲覧のサブモードに入るところから移植――
// 2026-08-21/2026-08-27の変更についてはstartCopyPrompt()自身の
// コメント参照。確認プロンプト自体(m_dest()のT_rmの分岐、同じよう
// にタイプされた単一の'y'をチェックする、の意図的に単純化された
// 代役、クラスコメント参照)はstartDestinationPrompt()によって表示
// される。
bool Application::startRemovePrompt() {
  cpMvRmOrigin_ = ApplicationMode::Directory;
  cpMvRmListTargets_.clear();
  if (currentMode_ != ApplicationMode::Directory) return startListCpMvRmPrompt(PromptKind::RemoveConfirm);  // 2026-09-27
  if (directoryMode_.markedCount() == 0) return false;
  if (switchToShellIfBusy()) return true;  // それ自身のコメント参照
  pendingCpMvRm_ = PendingCpMvRmOperation{PromptKind::RemoveConfirm, directoryMode_.currentDirectory()};
  directoryMode_.setPendingOperationLabel("RM");
  directoryView_.setCursorRowAlsoHighlighted(false);  // startCopyPrompt()自身のコメント参照
  directoryView_.setMarkedRowUsesUnderline(false);
  directoryView_.redraw();
  return startDestinationPrompt();
}

// K-71隣接(CP/MV/RM操作がアーム済みの間の^G): 実際のユーザー報告
// ("CPでは^Gが利かない")――^Gの通常のDirectoryモードの処理
// (directoryMode_.goBack()、上/戻るへナビゲート)は、そもそも
// pendingCpMvRm_を一切見ていなかったので、アーム済みの間にそれを
// 押すと、何も有用なことをしないか、アーム(とそれ自身の"CP"ラベル/
// 抑制されたカーソルハイライト)を有効なままにしつつ黙ってナビ
// ゲートしてしまうかのどちらかだった。handleCommand()自身の
// Command::Backのケースで、通常のBack処理より前にチェックされる
// (そのswitchの既存の「汎用のdirectoryMode_.goBack()フォールバック
// より前の特別なケース」という前例、例えばそのすぐ上の
// hasSearchAnnotation()に合わせている)――どこへもナビゲートせずに、
// アームをキャンセルして通常の閲覧を復元する(legacy自身のM_cp/
// M_mv/M_rmからの^Gも、同様にディレクトリを変えない)。
// 2026-08-24修正、直接のユーザー報告+彼ら自身の提案した修正: ファイル
// をマークし、CP(^Xc)をアームし、'.'経由で別のファイルをマークして
// から^Gでキャンセルすると、カーソルが数行離れて動いた後でも、
// 「最初の」ファイルのマークが目に見えて(反転表示のまま)固着して
// いた――"^Gのあとls再実行でいいんじゃない？"(^Gの後に単に`ls`を
// 再実行してはどうか?)。まさに正しい: 成功したCP/MV/RMは、それ
// 自身の操作後の再ロード(DirectoryMode::enterDirectory()自身の
// 文書化された挙動、「マークはそれが作られた一覧に限定される」)経由
// で既にマークをクリアするので、「キャンセルされた」ものがそれらを
// 残していたことこそが不整合なケースであり、再ロードがマークを
// クリアするという挙動自体ではなかった。refresh()(項目408/409で
// ^J^K/"./"のために追加された)は、(エントリ名で)カーソル自身の位置
// を保ったまま既に`ls`を再実行する――ここでそれを再利用することで、
// 古いマークをクリアすることと、別個の、今や冗長になった
// clearPendingOperationLabel()/redraw()呼び出しを不要にすることの
// 両方が達成される(enterDirectory()自体が既に前者を呼ぶ;refresh()
// 自体が既に再描画する)。
bool Application::cancelPendingCpMvRm() {
  pendingCpMvRm_.reset();
  directoryView_.setCursorRowAlsoHighlighted(true);
  directoryView_.setMarkedRowUsesUnderline(true);
  directoryMode_.refresh();
  return true;
}

// startCopyPrompt()/startMovePrompt()/startRemovePrompt()が始める
// M_cp/M_mv/M_rm -> M_destの流れの後半――PendingCpMvRmOperation自身の
// コメント参照。マークされた(あるいはカーソルで選択された)対象が
// 「今」存在すること(アーム時からここへ先送りされた、
// startCopyPrompt()自身のコメント参照)、そして現在のディレクトリが
// 操作がアームされたものと依然として一致することを要求する――無関係
// なディレクトリへのナビゲートを生き延びた古いアームは、代わりに
// そこでたまたまカーソルの下にあるものに対して黙って作用しては
// ならない。
//
// 2026-09-08、直接のユーザー依頼("冗長分は全て削除で")によりK-71
// (旧^X^M)自体の独立したキーバインドを削除: この関数はCP/MV/RM自身
// (すぐ上のstartCopyPrompt()等)がアーム直後に即座に自分で呼ぶので、
// 独立したキーからここへ到達する経路自体がそもそも冗長だった。
// Archiveモードの分岐(以前ここにあった、^X^Mが押された時に
// startArchivePrompt()へリダイレクトするだけの処理)も同時に削除
// した――^Oa(旧^Xa)自体が既に単独でstartArchivePrompt()を直接呼ぶ
// ため、Archiveモードでこの関数が呼ばれる経路はもう存在しない。
bool Application::startDestinationPrompt() {
  if (currentMode_ != ApplicationMode::Directory) return false;
  if (!pendingCpMvRm_) return false;
  PendingCpMvRmOperation pending = *pendingCpMvRm_;
  pendingCpMvRm_.reset();
  directoryMode_.clearPendingOperationLabel();
  // 2026-08-27修正、startCopyPrompt()自身のコメント参照: これは以前は
  // まさにここで無条件に両方のフラグをtrueへ復元していた、古い2
  // ステップの流れ(この関数は別個の^X^Mのキー入力経由でのみ到達
  // され、どちらへ進むにせよ常に「アーム/抑制された状態を後にする」)
  // については正しいが、startCopyPrompt()等が今や同じキー入力で
  // これへ直接呼び込むようになった今では間違っている――ここで無条件
  // に復元することは、1フレームがそれを表示する機会を得る前に抑制
  // を元に戻してしまっていた。今は下の「中断」の経路でだけ復元
  // される;成功の経路は、プロンプトが有効な期間全体にわたって両方
  // を抑制したままにし、プロンプトが後で解除された時に復元される
  // (cancelPrompt()自身のCP/MV/RMの分岐、あるいは操作後の再ロードの
  // どちらでも到達する)。
  if (pending.directory != directoryMode_.currentDirectory() || directoryMode_.targetEntries().empty()) {
    directoryView_.setCursorRowAlsoHighlighted(true);
    directoryView_.setMarkedRowUsesUnderline(true);
    directoryView_.redraw();  // 今ラベルの除去を塗る、startCopyPrompt()自身のredraw()に合わせている
    return false;
  }
  // 位置テキスト("06/22 END"、下層の一覧自身の現在のカーソル位置に
  // 一致させている)――2026-08-21修正、同じ「「Destination ? 」という
  // テキストが全く無い」という報告の一部(drawPromptLabel()自身の
  // コメント参照): これを表示する他の全てのPromptKindは、明示的に
  // それを渡す(ModeLine::format(cursorIndex+1, total))が、この呼び
  // 出し箇所はそれをactivatePrompt()自身の""デフォルトのままにして
  // おり、それはまた、drawPromptLabel()の「Destination ? 」メッセージ
  // の前にlabelWidthを何も進めないことも意味していた、それ自体は
  // 正しいが、参照スクリーンショット(=ef)が示すその隣接する位置
  // フィールドを黙って欠いていた。
  std::string positionText =
      ModeLine::format(directoryView_.cursorIndex() + 1, static_cast<int>(directoryMode_.entries().size()));
  activatePrompt(pending.kind, "", positionText);
  // activatePrompt()は、positionTextだけからpromptCandidateListActive_
  // を推測する(それ自身のコメント参照: 空でないpositionTextを渡す
  // 他の全ての呼び出し元は、本当に依然として見えている^J^S/^J^Cの
  // リストからの候補を編集している)――ここには候補リストは無く、
  // 単なる位置表示なので、あらゆる場所で空でないpositionTextが意味
  // することを許される範囲を広げるのではなく、明示的にその推測を
  // 取り消す。
  promptCandidateListActive_ = false;
  return true;
}

// M_exit(^X^C)が確認プロンプトへ入るところから移植――なぜこれが
// どのモードからでも到達可能で、CP/MV/RMのようなtargetEntries()
// スタイルのガードを必要としないのかについてはクラスコメント参照。
bool Application::startExitPrompt() {
  // "y"が事前入力されている(2026-08-05修正、実際のCUIスクリーン
  // ショットの"y"は、"(y/n) ? "の隣でほとんど見えなかった、なぜなら
  // プロンプトが最初に現れた時点で「既に」そこにあり、後でユーザーが
  // タイプしたものではないため)――デフォルトの答えは「はい、終了
  // する」なので、Enterだけを押せば確認になる;それをバックスペース
  // で消して"n"をタイプする(それからEnter)ことは、executeExit()自身
  // の`confirmation != "y"`チェック経由で、変わらずキャンセルになる。
  activatePrompt(PromptKind::ExitConfirm, "y");
  return true;
}

// 2026-09-09、直接のユーザー依頼による全面的な再設計("DUの仕様として
// は常時カレントで、ファイル閲覧中なら在籍フォルダーでduを発動し、
// ^J^D同様文字列入力エリア無しでよろしく")によりプロンプト自体を
// 廃止した(旧PromptKind::DuPath、旧m_du()/m_cr()のT_duケースから
// 移植した「事前に埋められたフィールドを^Mで確定する」流れ)。
// ユーザー自身の理屈: LOCATEは検索文字列を入力しない限り一覧が
// 出ないので入力欄に意味があるが、DUは集計が終わり次第自動的に
// 一覧が出るので、そもそも「何を入力するか」が無い――対象フォルダー
// は常に「今どこにいるか」(Directoryモードなら閲覧中のディレクトリ、
// Fileモードなら閲覧中のファイルの在籍フォルダー)から自動的に決まる。
// 別のフォルダーを集計したければ、通常のナビゲートでそこへ移動して
// から改めて^Jdを押せばよい(プロンプトでパスを打ち直す代わりに)。
bool Application::runDuOnCurrentFolder() {
  std::string folder;
  if (currentMode_ == ApplicationMode::Directory) {
    folder = directoryMode_.currentDirectory();
  } else if (currentMode_ == ApplicationMode::File) {
    std::filesystem::path filePath(fileMode_.currentFile());
    folder = filePath.parent_path().string();
    if (!folder.empty() && folder.back() != '/') folder += '/';
  } else if (currentMode_ == ApplicationMode::Locate) {
    // 2026-09-15追加、直接のユーザー方針("基本的に、SHELLも含めどの
    // モードからでも他のモードへ移行可能。ただし移行先では移行元に
    // 関する履歴を失う"、具体例: "LOCATEおよびMAN等からDUへ移行でき
    // ず...方針にそぐわない")。LOCATEの結果はファイルシステム上の
    // 任意のパス(ディレクトリの場合もある)なので、DuEntry自身の
    // duEntryFolder()と同じ考え方――ディレクトリならそれ自身、
    // ファイルならその親フォルダー。
    const std::string *selected = locateMode_.selectedResult();
    if (!selected) return false;
    std::error_code ec;
    bool isDirectory = std::filesystem::is_directory(*selected, ec) && !ec;
    folder = isDirectory ? *selected : std::filesystem::path(*selected).parent_path().string();
    if (!folder.empty() && folder.back() != '/') folder += '/';
  } else if (currentMode_ == ApplicationMode::Du) {
    // 2026-09-27追加、startLocatePrompt()自身の同日のコメント参照(DU中の
    // ^Jd): ハイライト行がディレクトリならそこを、ファイルならその親を
    // 集計し直す。戻り先は最初にDUを発動した場所のまま。
    const DuEntry *entry = duMode_.selectedEntry();
    if (!entry) return false;
    folder = duEntryFolder(*entry);
  } else {
    return false;
  }
  // 2026-09-19追加、startManPrompt()/startLocatePrompt()自身のコメント
  // と同じ方針転換: 以前はduReturnStack_という仕組み自体が無く、
  // returnFromDu()が常にDirectoryへハードコートされていたが、MAN/
  // LOCATEと同じ「通常ファイルかディレクトリ」棲み分けに揃える。
  //
  // 2026-09-20追加、startLocatePrompt()自身のstillInLocateRealm分岐と
  // 同じ理由での予防的な修正(直接のユーザー報告はLOCATE限定だったが、
  // openSelectedDuEntry()もfileReturnMode_ = ApplicationMode::Duを同じ
  // 形で設定するため、全く同じ再現手順――DU結果を直接Fileとして開き、
  // そのFileから「もう一度」^Jd/^J^Dを発動――で全く同じ①③ループが
  // 起こり得る)。currentMode_==Fileの時点でnearestFileOrDirectoryAnchor()
  // はそれ以上遡らずFile自身を即座に返すので、ここで無条件に再計算
  // すると、まさに今置き換えようとしている「古い」DUセッションへの
  // 参照(fileReturnMode_==Du)をうっかり新しい基点のスナップショットへ
  // 凍結してしまう。
  bool stillInDuRealm = currentMode_ == ApplicationMode::Du ||
                        (currentMode_ == ApplicationMode::File && fileReturnMode_ == ApplicationMode::Du);
  if (!stillInDuRealm) {
    ApplicationMode anchor = nearestFileOrDirectoryAnchor();
    duReturnStack_ = {anchor};
    duAnchor_ = anchorSnapshotFor(anchor);
  }
  // 2026-09-19追加、直接のユーザー依頼("`/`で発動すると待たされるので
  // `^Jl`と同じく集計中な意味のメッセージを出して欲しい"、メッセージ
  // 文言は"Disk usage..."と直接指定): runLocateUpdateDb()自身の
  // "Updating database..."と全く同じ仕組み。
  drawPromptLabel(PromptKind::Du, /*positionText=*/"", "Disk usage...");
  display_.flush();
  // 2026-09-19再訂正、直接のユーザー依頼("LOCATEと同じく^Gでduを中断し、
  // 構築中断時点のDUリストへ移動"): 以前はここでexecuteDu()を直接
  // ブロッキングで呼んでいた(du自体はChildProcess::run()で完了を待つ)
  // ――^Gがこの呼び出しの「内側」に割り込む余地は無かった。
  // ChildProcess::startCapturing()経由で非同期に起動し直す
  // (awaitingDu()/pollDuCompletion()自身の宣言コメント参照)。
  std::vector<std::string> argv = duMode_.commandArgvFor(folder);
  if (argv.empty()) {
    activeView().redraw();
    return false;
  }
  ChildProcess::AsyncCapture capture = ChildProcess::startCapturing(argv);
  if (capture.pid <= 0) {
    activeView().redraw();
    return false;
  }
  duPid_ = capture.pid;
  duOutputFd_ = capture.fd;
  duPendingOutput_.clear();
  duPendingPath_ = folder;
  return true;  // 今は待機中――awaitingDu()/pollDuCompletion()参照
}

// awaitingDu()自身の宣言コメント参照: 完了(pollDuCompletion())で
// あれ^Gによる中断(dispatchByte()自身のawaitingDu()ゲート)であれ、
// その時点までにduPendingOutput_へ蓄積された分をDuMode自身の一覧へ
// 反映して着地する、共有の仕上げ処理。
void Application::finishDuAsync() {
  duMode_.loadUsageFromOutput(duPendingPath_, duPendingOutput_);
  currentMode_ = ApplicationMode::Du;
  duView_.redraw();
}

bool Application::awaitingDu() const { return duPid_ > 0; }

// usilicrcのBUFFER(2026-09-25、"バッファへ読み込むモードはすべて対象"):
// 非同期に溜めている出力(DU/アーカイブ一覧)がBUFFERを超えたら、子プロセスを
// 止め、最後の改行で切り、Configuration::noteBufferLimitHit()で"Not enough
// buffer"を出させる――legacyのalloc_pipe()の中断と同じく、そこまでの一覧を
// 表示する(legacyでは8MBでもDUの一覧でよくこれが出ていた)。超えたらtrue。
// usilicrcのBUFFER(2026-09-25): アーカイブから取り出した中身がBUFFERを超えて
// いたら、最後の改行までに切り、"Not enough buffer"を出させる。取り出しの実行
// 自体には上限を付けない(アーカイブ全体を展開した出力から目的のファイルを探す
// ので、途中で打ち切ると後ろにあるファイルが見つからなくなる)。
void Application::capBufferContent(std::vector<unsigned char> &content) {
  auto limit = static_cast<std::size_t>(configuration_.bufferSizeLimitBytes());
  if (content.size() <= limit) return;
  std::size_t cut = limit;
  for (std::size_t i = limit; i > 0; i--) {
    if (content[i - 1] == '\n') {
      cut = i;
      break;
    }
  }
  content.resize(cut);
  configuration_.noteBufferLimitHit();
}

bool Application::capAsyncOutput(std::string &output, pid_t pid) {
  if (static_cast<long long>(output.size()) <= configuration_.bufferSizeLimitBytes()) return false;
  ChildProcess::terminate(pid);
  std::size_t newline = output.rfind('\n', static_cast<std::size_t>(configuration_.bufferSizeLimitBytes()));
  output.resize(newline == std::string::npos ? static_cast<std::size_t>(configuration_.bufferSizeLimitBytes()) : newline + 1);
  configuration_.noteBufferLimitHit();
  return true;
}

void Application::pollDuCompletion() {
  if (duPid_ <= 0) return;
  duPendingOutput_ += ChildProcess::readAvailable(duOutputFd_);
  if (capAsyncOutput(duPendingOutput_, duPid_)) {
    close(duOutputFd_);
    duOutputFd_ = -1;
    duPid_ = -1;
    finishDuAsync();
    showBufferLimitNoticeIfHit();
    return;
  }
  if (!ChildProcess::hasExited(duPid_)) return;
  // 2026-09-19追加: hasExited()がまさに今waitpid()で回収した直後
  // ――子プロセス自身は既に標準出力をクローズ済み(execvp()した
  // `du`が正常終了/異常終了のどちらであれ)なので、ここでもう一度
  // readAvailable()を呼び、終値の直前に書き込まれ、直前の1回の
  // 呼び出しではまだ読み切れていなかったかもしれない最後の分を
  // 拾ってから閉じる。
  duPendingOutput_ += ChildProcess::readAvailable(duOutputFd_);
  close(duOutputFd_);
  duOutputFd_ = -1;
  duPid_ = -1;
  finishDuAsync();
  showBufferLimitNoticeIfHit();
}

// awaitingArchiveLoad()自身の宣言コメント参照。openSelectedEntry()
// 自身のアーカイブ分岐から呼ばれる――ArchiveMode::loadArchive()自身の
// 「コマンドを解決する」部分だけをここで複製し(プロセスは起動しない
// 安価な文字列一致)、実際の起動はChildProcess::startCapturing()で
// 非同期に行う。.tgz等のパイプ形式(command->pipedFromArgv)は、
// startCapturing()が単一のargvしか受け付けないため、shellQuoteArgv()
// で安全にクォートした上で`/bin/sh -c "段1 | 段2"`として1プロセス
// にまとめる(forceUtf8CTypeForListingは、この/bin/sh自身に対して
// 設定すれば、fork/exec経由でその子である段1/段2にもそのまま
// 継承される)。
bool Application::startArchiveLoadAsync(const std::string &path) {
  ArchiveKind kind = ArchiveKind::None;
  auto command = configuration_.programForSuffix(path, &kind);
  if (!command || kind == ArchiveKind::None) return false;

  ChildProcess::Options options;
  options.forceUtf8CTypeForListing = true;
  std::vector<std::string> argv;
  if (command->pipedFromArgv) {
    std::vector<std::string> firstStage = *command->pipedFromArgv;
    firstStage.push_back(path);
    std::string shellLine = shellQuoteArgv(firstStage) + " | " + shellQuoteArgv(command->argv);
    argv = {"/bin/sh", "-c", shellLine};
  } else {
    argv = command->argv;
    argv.push_back(path);
  }
  ChildProcess::AsyncCapture capture = ChildProcess::startCapturing(argv, options);
  if (capture.pid <= 0) return false;
  archiveLoadPid_ = capture.pid;
  archiveLoadOutputFd_ = capture.fd;
  archiveLoadPendingOutput_.clear();
  archiveLoadPendingPath_ = path;
  archiveLoadForPager_ = false;
  return true;  // 今は待機中――awaitingArchiveLoad()/pollArchiveLoadCompletion()参照
}

// startArchiveLoadAsync()自身の宣言コメント参照。openSelectedEntryNoWrap()
// (^O^M)向けの同一の非同期起動――archiveLoadForPager_/
// archiveLoadPendingDirectory_/archiveLoadPendingBadge_を追加で立てる
// 点だけが違う(finishArchiveLoadAsync()自身の分岐参照)。
bool Application::startArchivePagerLoadAsync(const std::string &path, const std::string &directory) {
  ArchiveKind kind = ArchiveKind::None;
  auto command = configuration_.programForSuffix(path, &kind);
  if (!command || kind == ArchiveKind::None) return false;

  ChildProcess::Options options;
  options.forceUtf8CTypeForListing = true;
  std::vector<std::string> argv;
  if (command->pipedFromArgv) {
    std::vector<std::string> firstStage = *command->pipedFromArgv;
    firstStage.push_back(path);
    std::string shellLine = shellQuoteArgv(firstStage) + " | " + shellQuoteArgv(command->argv);
    argv = {"/bin/sh", "-c", shellLine};
  } else {
    argv = command->argv;
    argv.push_back(path);
  }
  ChildProcess::AsyncCapture capture = ChildProcess::startCapturing(argv, options);
  if (capture.pid <= 0) return false;
  archiveLoadPid_ = capture.pid;
  archiveLoadOutputFd_ = capture.fd;
  archiveLoadPendingOutput_.clear();
  archiveLoadPendingPath_ = path;
  archiveLoadPendingDirectory_ = directory;
  archiveLoadPendingBadge_ = command->argv.empty() ? "" : "(" + command->argv.front() + ")";
  archiveLoadForPager_ = true;
  return true;  // 今は待機中――awaitingArchiveLoad()/pollArchiveLoadCompletion()参照
}

// awaitingArchiveLoad()自身の宣言コメント参照: 完了(pollArchiveLoadCompletion())
// であれ^Gによる中断(dispatchByte()自身のawaitingArchiveLoad()ゲート)
// であれ、その時点までにarchiveLoadPendingOutput_へ蓄積された分を
// ArchiveMode::finishLoadFromOutput()へ渡す、共有の仕上げ処理――
// openSelectedEntry()自身の(以前は同期的なloadArchive()呼び出しの
// 直後にあった)後始末(archiveParentStack_のクリア+RecentFileへの
// pushRecent())もここへ移した。
//
// 2026-09-16追加: archiveLoadForPager_(startArchivePagerLoadAsync()自身
// のコメント参照)が立っている場合は、openSelectedEntryNoWrap()自身の
// ARCHIVE分岐が同期的に行っていたのと同じ着地(FileMode::
// loadContentAsPipe()+setSearchOrigin()+setPagerListingDirectory())を
// 代わりに行う――ArchiveModeへは一切入らない(対話的な選択不可の
// プレーンなtvfページャー表示のまま)。
// 2026-10-01、直接のユーザー依頼("^Oeまたは^O^Mで選んだ時のsort順を逆にして"): ^O^Mで見る
// .epubの`unzip -l`の一覧(ArchiveMode::finishLoadFromOutput()の名前順と同じく)は、前後の
// "----"の罫線の間のファイルの行を名前(4つ目の欄から後)の順に並べる。
std::string sortZipListingByName(const std::string &listing) {
  std::vector<std::string> lines;
  for (std::size_t pos = 0; pos < listing.size();) {
    std::size_t nl = listing.find('\n', pos);
    lines.push_back(listing.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
    pos = nl == std::string::npos ? listing.size() : nl + 1;
  }
  auto isRule = [](const std::string &line) { return line.find("---------") != std::string::npos; };
  auto nameOf = [](const std::string &line) {
    std::size_t p = 0;
    for (int field = 0; field < 3; field++) {
      p = line.find_first_not_of(' ', p);
      if (p == std::string::npos) return std::string();
      p = line.find(' ', p);
      if (p == std::string::npos) return std::string();
    }
    p = line.find_first_not_of(' ', p);
    return p == std::string::npos ? std::string() : line.substr(p);
  };
  auto first = std::find_if(lines.begin(), lines.end(), isRule);
  if (first == lines.end()) return listing;
  auto second = std::find_if(first + 1, lines.end(), isRule);
  if (second == lines.end()) return listing;
  std::stable_sort(first + 1, second,
                   [&](const std::string &a, const std::string &b) { return nameOf(a) < nameOf(b); });
  std::string out;
  for (std::size_t i = 0; i < lines.size(); i++) {
    out += lines[i];
    if (i + 1 < lines.size() || (!listing.empty() && listing.back() == '\n')) out += '\n';
  }
  return out;
}

void Application::finishArchiveLoadAsync() {
  if (archiveLoadForPager_) {
    archiveLoadForPager_ = false;
    if (isEpubFile(archiveLoadPendingPath_)) archiveLoadPendingOutput_ = sortZipListingByName(archiveLoadPendingOutput_);
    if (fileMode_.loadContentAsPipe(archiveLoadPendingOutput_)) {
      fileMode_.annotations().pushWithNameOverride(FileMode::FileAnnotationKind::Search, archiveLoadPendingPath_,
                                                    archiveLoadPendingBadge_);
      fileMode_.setPagerListingDirectory(truncatedTrailingPath(archiveLoadPendingDirectory_));
      currentMode_ = ApplicationMode::File;
      fileReturnMode_ = ApplicationMode::Directory;
    }
    activeView().redraw();
    return;
  }
  if (archiveMode_.finishLoadFromOutput(archiveLoadPendingPath_, archiveLoadPendingOutput_)) {
    archiveParentStack_.clear();  // 全く新しい最上位のアーカイブ――以前のネストした降下の連鎖はもう当てはまらない
    currentMode_ = ApplicationMode::Archive;
    history_.pushRecent(RingCategory::RecentFile, archiveLoadPendingPath_);
  } else if (!archiveLoadAborted_ && fileMode_.loadFile(archiveLoadPendingPath_)) {
    // 2026-09-25追加: アーカイブとして読めなかった(一覧が1件も取れない――
    // 中身がその形式でないファイルに別名を付けた等、ArchiveMode::
    // finishLoadFromOutput()自身のコメント参照)なら、普通のファイルとして
    // 開く。executePathInput()等の同期の経路が既にしている(loadArchive()が
    // 失敗したらloadFile())のと同じ。^Gで中断した場合は開かない。
    currentMode_ = ApplicationMode::File;
    fileReturnMode_ = ApplicationMode::Directory;
    fileView_.redraw();
  } else {
    // ^Gによる中断(出力が全く読めていなかった場合を含む)、あるいは普通の
    // ファイルとしても開けなかった――Directoryのまま、単に読み込み中
    // メッセージを消して再描画する。
    activeView().redraw();
  }
}

bool Application::awaitingArchiveLoad() const { return archiveLoadPid_ > 0; }

void Application::pollArchiveLoadCompletion() {
  if (archiveLoadPid_ <= 0) return;
  archiveLoadPendingOutput_ += ChildProcess::readAvailable(archiveLoadOutputFd_);
  if (capAsyncOutput(archiveLoadPendingOutput_, archiveLoadPid_)) {  // それ自身のコメント参照
    close(archiveLoadOutputFd_);
    archiveLoadOutputFd_ = -1;
    archiveLoadPid_ = -1;
    finishArchiveLoadAsync();
    showBufferLimitNoticeIfHit();
    return;
  }
  if (!ChildProcess::hasExited(archiveLoadPid_)) return;
  // pollDuCompletion()自身の同じ理由のコメント参照。
  archiveLoadPendingOutput_ += ChildProcess::readAvailable(archiveLoadOutputFd_);
  close(archiveLoadOutputFd_);
  archiveLoadOutputFd_ = -1;
  archiveLoadPid_ = -1;
  finishArchiveLoadAsync();
  showBufferLimitNoticeIfHit();
}

// ^J^D(K-38): Command::RedisplayDu自身のコメント参照。
bool Application::redisplayDu() {
  // 2026-09-19修正、直接のユーザー依頼("最初のDUを^J^Dで発動した
  // 場合、集計結果がまだ無いので、そのときに限り^Jdと同じくduを
  // 起動してほしい"): 以前はここが「まだ一度もDUを実行していない」
  // 場合に無言のno-opだった(K-38自身のusilicrcの文書「Redisplay
  // window」通り――まだ何も無ければ何も再表示するものが無いという
  // 理屈)。ユーザーの意図は「^J^Dは^Jdの安全な同義語であってよい」
  // ということなので、初回に限りrunDuOnCurrentFolder()(^Jd自体)へ
  // フォールバックする。
  if (duMode_.currentPath().empty()) return runDuOnCurrentFolder();
  // 2026-09-20追加、直接のユーザー報告("^Jdは直ったが^J^Dが手つかず"):
  // startManPrompt()/startManList()自身の直上のコメント・
  // 「まだそのモードにいない時だけ」というルールと全く同じものを
  // ここにも適用する。以前はここのゲートが「duMode_.currentPath()が
  // 空か」(セッションが一度でも存在したか)だけで、既存セッションが
  // あればduReturnStack_/duAnchor_を一切再計算せずreturnToDuResults()
  // へ直行していた――しかし別のファイル/ディレクトリへ移動した「後」
  // に^J^Dで古いDU結果を再表示すると、基点が動く前の場所(あるいは
  // 前回本当にDUを開始した時の場所)のまま固定されてしまい、MAN/
  // LOCATEの「Redisplay」が常にその場から再アンカーするのと食い違う。
  // 2026-09-20追加、runDuOnCurrentFolder()自身のstillInDuRealm分岐と
  // 同じ理由: currentMode_==Fileであっても、そのFile自体がまだDUの
  // realmの中にいる間(fileReturnMode_==Du)は、下の再計算をスキップ
  // する必要がある――さもないと、まさに今置き換えようとしている
  // 「古い」DUセッションへの参照を新しい基点のスナップショットへ
  // 凍結してしまう。
  bool stillInDuRealm = currentMode_ == ApplicationMode::File && fileReturnMode_ == ApplicationMode::Du;
  if (currentMode_ != ApplicationMode::Du && !stillInDuRealm) {
    ApplicationMode anchor = nearestFileOrDirectoryAnchor();
    duReturnStack_ = {anchor};
    duAnchor_ = anchorSnapshotFor(anchor);
  }
  return returnToDuResults();
}

// K-58: クラス自身の宣言コメント参照。
bool Application::startArchivePrompt() {
  if (currentMode_ == ApplicationMode::Archive) {
    if (archiveMode_.targetEntries().empty()) return false;
  } else if (currentMode_ == ApplicationMode::Directory) {
    const DirectoryEntry *entry = directoryMode_.selectedEntry();
    if (!entry || entry->isDirectory) return false;
    std::string path = directoryMode_.currentDirectory();
    if (!path.empty() && path.back() != '/') path += '/';
    path += entry->name;
    ArchiveKind kind = ArchiveKind::None;
    configuration_.programForSuffix(path, &kind);
    // 2026-09-28追加、直接のユーザー指定("(ls)上の.epubファイルに対しては、
    // ARCHIVEへの設定如何に関わらず^Oaと同じ扱い"): .epubは[Enter]では本文として
    // 開くが、^Oaでは(ARCHIVE節に別名が無くても)zipとして展開する。
    if (kind == ArchiveKind::None && isEpubFile(path)) kind = ArchiveKind::Zip;
    if (kind == ArchiveKind::None) return false;
  } else {
    return false;
  }
  activatePrompt(PromptKind::ArchiveDestination, "");
  return true;
}

// 2026-08-27追加、実際の報告+PTYでの再現("^X^Mの③が変化無し
// （何も起きない）"――プロンプト/宛先のロジックとは完全に独立に、
// 実際のutf8.tarフィクスチャに対して`tar xf`/`unzipを`usilicの外で
// 直接実行することで突き止めた): `tar`/`unzip`のどちらも、それら
// 自身のメンバー名の引数は、リテラルな文字列のマッチではなく、
// シェルのglob「パターン」である(bsdtar自身のman page:
// "Patterns are shell-style globbing patterns as documented in
// tcsh(1)";unzipも同一に振る舞う――直接確認済み)。それ自身の名前が
// 本当にglobのメタ文字を「含む」実際のアーカイブエントリ(このフィク
// スチャ自身の"utf8/...[二][字]...ts"――実際のテレビ番組表スタイルの
// ファイル名にある、ごく普通の角括弧の注釈であり、奇異なものは何も
// 無い)は、その`[...]`がリテラルな角括弧ではなくglobの文字クラス
// として誤ってパースされてしまい、どちらのツールも、一覧からその
// 名前がどれだけ忠実に往復しても、「まさにこの名前のエントリ」を
// 決して見つけられない――どんな文字コードの懸念からも独立に確認済み
// (生の一覧出力のバイトと、同じ名前を手動でNFC正規化したコピーの
// どちらも同様に失敗した;実際のglobのメタ文字をバックスラッシュ
// エスケープすることだけがそれを直した)。tcsh自身のglob構文が認識
// するあらゆるメタ文字(`\*?[]{}`――tcsh(1)参照)をバックスラッシュ
// エスケープすることで、パターンは正確なリテラルマッチに戻る、
// 同じ方法で両方のツールに対して直接確認済み。
std::string Application::escapeArchiveMemberPattern(const std::string &name) {
  std::string escaped;
  escaped.reserve(name.size());
  for (char c : name) {
    if (c == '\\' || c == '*' || c == '?' || c == '[' || c == ']' || c == '{' || c == '}') escaped += '\\';
    escaped += c;
  }
  return escaped;
}

std::vector<std::string> Application::buildArchiveExtractArgv(const std::string &archivePath, ArchiveKind kind,
                                                                const std::vector<std::string> &members,
                                                                const std::string &destination) const {
  if (kind == ArchiveKind::Tar) {
    std::vector<std::string> argv = {"tar", "xf", archivePath, "-C", destination};
    for (const auto &m : members) argv.push_back(escapeArchiveMemberPattern(m));
    return argv;
  }
  if (kind == ArchiveKind::Zip) {
    std::vector<std::string> argv = {"unzip", "-o", archivePath};
    for (const auto &m : members) argv.push_back(escapeArchiveMemberPattern(m));
    argv.push_back("-d");
    argv.push_back(destination);
    return argv;
  }
  if (kind == ArchiveKind::SevenZip) {
    // 2026-09-04追加、上のクラス宣言コメント参照。escapeArchiveMemberPattern()
    // をここでは使わない――それはtar/unzip自身のシェルスタイルglob構文
    // (`\*?[]{}`)向けであり、実際の`7z`バイナリで直接確認した通り、7z
    // 自身の`[...]`はそもそもglobの文字クラスとして解釈されない(常に
    // リテラル一致)。それにも関わらずバックスラッシュを前置すると、7z
    // はそれをファイル名の一部として字面通りに探しにいってしまい、
    // 実在するはずのエントリが「マッチ無し」で静かに展開されなくなる
    // (`abc[1].txt`で実機確認済み: エスケープ無しは成功、
    // `abc\[1\].txt`は"No files to process")。
    std::vector<std::string> argv = {sevenZipCommand(), "x", archivePath, "-o" + destination, "-y"};  // 7z/7zz(2026-09-27)
    for (const auto &m : members) argv.push_back(m);
    return argv;
  }
  if (kind == ArchiveKind::Lha) {
    // 2026-09-16追加、直接のユーザー依頼("LZH展開実装よろしく")。
    // 実機のLhasa(Homebrew、v0.6.0、addBuiltinArchive(".lzh", ...)
    // 自身のコメント参照)を`lha`単体(引数無し)で起動すると使い方が
    // 表示され、そこに構文が明記されている: `lha [-]{lvtxep[q{num}]
    // [finv]}[w=<dir>] archive_file [file...]`。`x`(パス情報を保持
    // したまま展開、tar/unzipのデフォルトと同じ考え方)+`f`(確認無し
    // で上書き、zip/7zの-o/-yと同じ考え方)+`w=<dir>`(展開先)を1つの
    // トークンにまとめる(Lhasa自身のオプション構文がそれを要求する
    // ――ハイフン無しで`x`/`f`/`w=...`を連結)。実機の`src/skrw021.lzh`
    // に対して`lha xfw=<dest> <archive> [members...]`(メンバー指定
    // 無しならアーカイブ全体)の両方を直接実行し、実際にファイルが
    // 展開先へ現れることを確認済み。メンバー名がシェルスタイルの
    // glob文字(`\*?[]{}`)を含む場合の扱いも実機で確認済み――`"*.txt"`
    // を実際に渡すと(エスケープ無しでは)複数エントリにマッチした
    // (tar/unzipと同じfnmatch系のglobエンジンと判断)ため、
    // escapeArchiveMemberPattern()をそのまま適用する(7z自身の
    // 「`[...]`は常にリテラル」という例外は踏まない前提――`[...]`
    // 自体の実機確認はしていないが、`*`の挙動から同じglobエンジン
    // 系列と推定)。
    std::vector<std::string> argv = {"lha", "xfw=" + destination, archivePath};
    for (const auto &m : members) argv.push_back(escapeArchiveMemberPattern(m));
    return argv;
  }
  return {};  // Rpm/Deb/Bzip2――この最初のスライスではサポートされない、クラス自身の宣言コメント参照
}

// K-45/K-47(^Xf/^Xd、「Input file/directory name」)――
// runDuOnCurrentFolder()等とは異なり、意図的にDirectoryモードにはゲート
// されていない: legacy自身のM_lfile2/M_ldir2は、汎用の「任意のパス
// をタイプして、それが行き着く先へ着地する」ナビゲートであり、
// ディレクトリ一覧からと同じくらいFile/Man/Grep等からも利用可能
// である。PathResolverは、どのキーが押されたかではなく、パス自体
// からファイルかディレクトリかを既に決めるので、両方のキーはこの
// 単一のプロンプト/実行のペアを共有する――`isDirectory`は、純粋に
// promptLabelText()の利益のためだけにpathInputIsDirectory_へ保管
// される(legacyは、2つがこの単一の実行経路を共有するにも関わらず、
// キーごとに別々の"FILE"/"DIR"のモードラインのラベルを表示する;
// 実際の並列スクリーンショット比較、2026-08-03により、このポートが
// どちらのキーが押されたかに関わらず常に"FILE/DIR"を表示していた
// ことが判明した)。
bool Application::startPathInputPrompt(bool isDirectory) {
  pathInputIsDirectory_ = isDirectory;
  activatePrompt(PromptKind::PathInput, "");
  return true;
}

// startListBrowser()とtoggleListBrowserHistory()自身の非時系列の
// 分岐で共有される: History::recentEntries()は今や生のアクセスログ
// である(History::pushRecent()自身のコメント、2026-08-04の訂正
// 参照)、そして連続していない重複を含み「得る」――例えばAに、次に
// Bに、それから再びAにアクセスすると、ringには[A, B, A]が残る。
// ソートされた「一意な最近のアイテム」ビューは、ringが決して繰り
// 返さないことに頼るのではなく、その生のログに対してそれ自身の
// 重複排除を必要とする、これをstd::setは1ステップで(ソート済みかつ
// 一意に)行う。
std::vector<std::string> Application::sortedUniqueRecentEntries(RingCategory category) const {
  const std::deque<std::string> &recent = history_.recentEntries(category);
  std::set<std::string> unique(recent.begin(), recent.end());
  return std::vector<std::string>(unique.begin(), unique.end());
}

// (旧startListBrowser()、2026-09-20削除、直接のユーザー依頼(^Rの
// 再設計、"^J^Rを^J^Pに置き換える(^J^Rは廃棄)"): m_list()のディス
// パッチから移植した、プレーンな(直接入力の候補リストではない)
// FILE/DIR/COMMANDピッカー専用の共有エントリポイントだったが、
// RecentFile/RecentDirectoryは2026-08-17に既にstartRegexPromptList()
// 経由へ移行済みで、この時点でCommand::StartCommandHistory
// (^J^R、旧"COMMAND")だけが唯一の呼び出し元として残っていた。
// ^J^R自体を廃止した(dispatchLeaderByte()自身のcase 0x12削除コメント
// 参照)ことで完全に到達不能になったため、Command::StartCommandHistory
// 自身とともに削除した――^J^Pが持つ、RingCategory::ShellCommandへの
// 唯一の入口としての役割はstartPromptHistoryRecall()
// (sortDirectEntryList自身の同日の拡張コメント参照)がそのまま
// 引き継いでいる。)

// K-49(素の^R、M_hist): 全体の経緯についてはToggleHistoryView自身の
// コメント参照。listBrowserCategory_自身のringを再表示するが、
// startListBrowser()自身のソート済み/重複排除された順序と生の時系列
// 順の間でトグルする(History::recentEntries()は既に最も古いものから
// 返すので、つまり反転無しでちょうど「最新のものが最下行」になる)
// ――時系列順に入ると、加えてカーソルを「最後の」エントリへ動かす
// ので、実際に最初に見える/選択されるのは最新の項目になる、ユーザー
// 自身の説明("最下行が最新"、直接確認済み、2026-08-04)とlegacy自身
// のmode.c(M_hist/T_histのグループ: `pnp->row = hp->bp->nrow - 1;`)
// に合わせている。ListBrowserモードの外ではno-op、^Rが当てはまらない
// 文脈についてのm_list()自身の`echo_mess(S_errf)`に合わせている。
// EGREPの一覧(2026-09-27、startPromptHistoryRecall()自身のコメント参照):
// usilicrcのEGREP節の実例をその並びのまま先頭に置き、続けて、それ以外の
// 過去に検索した正規表現をソート+重複排除して並べる。
// EGREPの一覧を開いた時のカーソル位置: 最後に使った正規表現の行(無ければ先頭)。
int Application::egrepListCursorIndex(const std::vector<std::string> &items) const {
  const auto &recent = history_.recentEntries(RingCategory::RecentEgrepPattern);
  if (recent.empty()) return 0;
  auto it = std::find(items.begin(), items.end(), recent.back());
  return it == items.end() ? 0 : static_cast<int>(it - items.begin());
}

std::vector<std::string> Application::egrepListItems() const {
  std::vector<std::string> items;
  for (const auto &preset : configuration_.grepPresets())
    if (std::find(items.begin(), items.end(), preset) == items.end()) items.push_back(preset);
  std::vector<std::string> presets = items;
  for (const auto &entry : sortedUniqueRecentEntries(RingCategory::RecentEgrepPattern))
    if (std::find(presets.begin(), presets.end(), entry) == presets.end()) items.push_back(entry);
  return items;
}

bool Application::toggleListBrowserHistory() {
  if (currentMode_ != ApplicationMode::ListBrowser) return false;
  // プロンプトから起動される呼び出し(startPromptHistoryRecall())には、
  // legacyでは、そもそもトグルして「入る」べき別個の「ソート済み」
  // ビューが無い――なぜかについてはそれ自身のコメント参照(legacyには
  // これらのカテゴリのどれについても、hでない別のringは存在しない)
  // ――そのためRecentFile/RecentDirectory(2026-08-17追加)以外の全て
  // について、以前は2回目の^Rはここではno-opだった: GREP/CUT/MAN/
  // LOCATE/PROGRAMとは異なり、legacy自身のT_hfile/T_hdirには本当に
  // 別個のソート済みringがある。
  // GrepPattern/CutPatternは(2026-08-28)拡張された、直接のユーザー
  // 依頼("前回使用した正規表現は何だっけ？...EGREPの検索文字列一覧
  // からも発動してくれないと困る"): 上の「legacyには別個のソート済み
  // ringが無い」という理屈は「legacy」については真だが、実際には
  // 「このポート」がGREP/CUT自身の直接入力リストについて表示する
  // ものには当てはまらない――その「ソート済み、重複排除された」
  // シード+最近のビュー自体が、既にlegacyのプレーンな時系列ring
  // (2026-08-05、"同ファイルを読み込んだ後もソートしてほしい")の
  // 上に乗った、ポート固有の追加である、そのためRecentFile/
  // RecentDirectoryがトグルする、まさに同じ2つの本当に異なるビュー
  // (ソート済みで一意対、生の重複ありの時系列)がGREP/CUTについても
  // 存在し、FILE/DIRについてと同じくらい正当に、「最後に何を検索
  // したか」(時系列順、最新が最後)に答えるためにトグルする価値が
  // ある。ManName/LocateQueryは拡張されていない――それらについては
  // まだ同等の報告が無く、GREP/CUTとは異なり、それらはそもそも一度も
  // このポート自身のソート済み重複排除された直接入力の亜種を与えられた
  // ことが無い(sortDirectEntryList自身の範囲についてのコメント参照)、
  // そのためトグルして入る2つ目のビューが無い。
  // 2026-09-20追加、直接のユーザー依頼(^Rの再設計、"「まだ同等の報告が
  // 無い」の意味が今イチ不明だが、対象範囲へ入れて欲しい、との要望を
  // 指しているのなら、今をもって報告としたい"): ProgramCommandを
  // ここへ追加した――上のコメントが挙げていた「拡張しない理由」自体は
  // 「まだ報告が無かった」というだけで、GREP/CUT同様ソート済み+
  // 重複排除の直接入力ビューを持てない技術的な制約は無い
  // (sortDirectEntryList自身の同日の拡張参照)。^J^R(旧COMMAND、
  // Command::StartCommandHistory)を廃止して^J^Pへ統合したことで、
  // ^J^Pの候補一覧(PromptKind::ProgramCommand)がこのソート済みビュー
  // とその後の^Rでの生の時系列ビューへのトグルの唯一の入口になった。
  if (promptHistoryContext_ && promptHistoryContext_->kind != PromptKind::RecentFile &&
      promptHistoryContext_->kind != PromptKind::RecentDirectory &&
      promptHistoryContext_->kind != PromptKind::GrepPattern &&
      promptHistoryContext_->kind != PromptKind::CutPattern &&
      promptHistoryContext_->kind != PromptKind::ProgramCommand) {
    return false;
  }
  listBrowserChronological_ = !listBrowserChronological_;
  std::vector<std::string> items;
  if (listBrowserChronological_) {
    // 生のログ、重複を含む――なぜこのringが正当にエントリを繰り返す
    // ことがあるのかについてはHistory::pushRecent()自身のコメント参照。
    const std::deque<std::string> &recent = history_.recentEntries(listBrowserCategory_);
    items.assign(recent.begin(), recent.end());
  } else {
    // 2026-09-11削除: 以前はここでlistBrowserCategory_==RecentEgrepPattern
    // の場合だけconfiguration_.grepSeedPatterns()をマージしていたが、
    // その候補一覧の種自体が(startGrepPrompt()自身のコメント参照の
    // 統一により)廃止されたため、単純にsortedUniqueRecentEntries()の
    // 結果だけを使う。2026-09-27、EGREPは実例を先頭にした一覧へ
    // (startPromptHistoryRecall()自身の同日のコメント参照)。
    items = listBrowserCategory_ == RingCategory::RecentEgrepPattern ? egrepListItems()
                                                                      : sortedUniqueRecentEntries(listBrowserCategory_);
  }
  bool egrepList = !listBrowserChronological_ && listBrowserCategory_ == RingCategory::RecentEgrepPattern;
  // archiveHistoryFilterActive()自身のコメント参照――startPromptHistoryRecall()
  // が組み立てた最初のリストだけでなく、^Rの時系列トグルをまたいでも
  // このフィルタを適用したままにする。
  if (archiveHistoryFilterActive()) {
    items.erase(std::remove_if(items.begin(), items.end(),
                                [](const std::string &item) { return !hasArchiveCompatibleSuffix(item); }),
                items.end());
  }
  if (promptHistoryContext_ && promptHistoryContext_->epubOnly)  // ^J^E: EPUBだけ(startEpubList()参照)
    items.erase(std::remove_if(items.begin(), items.end(), [](const std::string &item) { return !isEpubFile(item); }),
                items.end());
  listBrowserMode_.show(items, fileHistoryBadges(configuration_, listBrowserCategory_, items));
  if (listBrowserChronological_ && !items.empty()) {
    // show()自体は既に、cursorIndex_を新たに0へリセットした状態で
    // 一度再描画済みである――下の「末尾へ動かされた」カーソルを
    // (内部状態だけでなく)画面が実際に反映するよう、再びredraw()する。
    listBrowserView_.setCursorIndex(static_cast<int>(items.size()) - 1);
    listBrowserView_.redraw();
  } else if (egrepList && !items.empty()) {
    listBrowserView_.setCursorIndex(egrepListCursorIndex(items));  // startPromptHistoryRecall()自身の同日のコメント参照
    listBrowserView_.redraw();
  }
  // RecentFile/RecentDirectory(2026-08-18修正、=czの報告――"文字列
  // 入力エリアが無い"): この関数は、これら2つのPromptKindについて
  // 読み取り専用の候補画面を再描画する、startPromptHistoryRecall()/
  // Command::MoveUp/MoveDown以外の「もう1つの」場所であり、
  // (一つ前の回に追加された)refreshDirectEntryModeLine()には一度も
  // 結線されていなかった――そのためここでの^Rトグルは、この同じ
  // 画面の他の全ての再描画は既に正しく見えているにも関わらず、
  // 24行目を「古い」、項目351以前のレンダリング(listBrowserView_
  // 自身のプレーンなModeLineFieldsProvider、隙間無く完全に反転され、
  // ハイライトされていない候補も無い)のままにしていた。他の全ての
  // PromptKindについてはno-op(上の早期returnのガード)、他の全ての
  // 呼び出し箇所自身のガードに合わせている。
  refreshDirectEntryModeLine();
  return true;
}

// ^O^C(M_copy、「COPY」、usilicrc K-29): code.cのmake_map()から移植
// ――その状態(カーソル/選択/クリップボード)が、ここではなく完全に
// そのクラスの中に生きている理由についてはCopyMode自身のクラス
// コメント参照。activeContentView().lines()を凍結する(カーソルは
// 起点自身の現在ハイライトされている行から始める――スナップショット
// を取った直後のcode.c自身の`row = rvp->row`――これの最初の版が間違え
// たと実際のユーザー報告が確認済み: "^Oc→ブロックカーソル位置が...
// →正しくは直前のハイライト行位置")。
//
// Copyの間のモードラインは、起点自身のModeLineFieldsがたまたま何
// だったかの凍結されたコピーではなく、固定の形である(これの以前の
// 版は後者を試みたが、DirectoryモードからCopyに入る際に目に見える
// 変化を全く生まなかった――実際のユーザー報告: "^Oc→モードライン
// 不良（変化無し）。正しくは「COPY」「---」「/path/」"): nameは常に
// リテラルな"COPY"(disp.c自身のmp[M_copy]のラベル、str.c:
// `{" COPY", M_copy}`)、positionは常にlegacy自身の固定の"---"という
// プレースホルダー(disp.c: `case M_copy: strcpy(line, "---")`)、そして
// trailingは起点自身の識別する値(その`name`フィールド――Directory/
// Fileモードについてはディレクトリ/ファイルパス、ListBrowserについ
// てはカテゴリラベル、等)を運ぶので、このスナップショットが「どこ
// から」来たのかが依然として見える、他の単一値のモードラインと同様
// に画面自身の右端まで埋められる(パスの後の末尾の空白についての
// ユーザー自身の期待に一致させている)。
bool Application::startCopy() {
  if (currentMode_ == ApplicationMode::Copy) return false;
  ContentView &origin = activeContentView();
  ContentView::ModeLineFields frozen = origin.currentModeLineFields();
  copyReturnMode_ = currentMode_;
  currentMode_ = ApplicationMode::Copy;
  // 直接アクティブなプロンプトを破棄するのではなく一時停止する
  // ――^O^Cは、FGREP/EGREP/CUT等のパターンをタイプ中でも到達可能
  // (feedPromptByte()は全ての^Oリーダーのシーケンスを
  // dispatchLeaderByte()へ転送する、それ自身のコメント参照)であり、
  // dispatchByte()自身のトップレベルのチェックは、promptKind_が
  // 設定されたままである限り全てのバイトをfeedPromptByte()へ
  // ルーティングする――ここでそれを設定したままにすると、下の
  // feedCopyByte()が到達不能になってしまう。まさに同じ理由で
  // ensureShellActive()/returnFromShell()が既に使っているのと同じ
  // 一時停止/復元のペア(suspendedActivePromptKind_自身の宣言コメント
  // 参照);Shell固有のものではなく本当に同じ汎用の仕組み(「モーダル
  // な外出をまたいで生きたプロンプトを一時停止する」)であるため、
  // 複製するのではなく再利用してある。
  if (promptKind_ != PromptKind::None) {
    suspendedActivePromptKind_ = promptKind_;
    suspendedActivePromptText_ = promptEditor_.text();
    promptKind_ = PromptKind::None;
  }
  // loadSnapshot()の「後」ではなく「前」に設定する:
  // loadSnapshot()自身のrender()は、その最後のステップとして実際に
  // setContentCursorColumn()(本物の端末のカレットを配置する)を呼ぶ
  // ――実際のユーザー報告("^Oc→ブロックカーソル位置がモードラインの
  // 透かし文字...")を、loadSnapshot()が既に正しくカレットを配置した
  // 「後」に、これの以前の版がここでcopyView_.redraw()を「再び」呼ん
  // でいたことまで突き止めた、それはモードラインを再描画し(テキスト
  // を描くことは、他の全てのContentViewの描画と同様、副作用として
  // 本物のカーソルを動かす)、代わりにカレットを「その」描画が終わった
  // 場所に座らせたままにする、なぜならcursorHighlightEnabled()が
  // falseの間、redraw()自体は決してsyncVisibleCursor()を呼ばないから
  // である(それ自身のコメント参照)。先にプロバイダを設定することは、
  // loadSnapshot()自身の単一のredraw()が既に正しいモードラインを
  // 表示することを意味するので、2回目の再描画は全く必要無い。
  copyView_.setPositionTextOverride("---");
  copyView_.setModeLineFieldsProvider([name = frozen.name](int) -> ContentView::ModeLineFields {
    return {"COPY", name, /*fillTrailingToEdge=*/true, /*rightWatermark=*/"", /*trailing2=*/""};
  });
  copyEscapePending_ = false;
  // 2026-09-07、直接のユーザー報告⑤("CLAUDE.md→^V（ページ送り）→
  // ^O^C→コピーモード発動前と後とでブロックカーソル位置が違うため
  // 画面が変化してしまいコピー開始位置を見失う"): 起点のtopIndex_を
  // loadSnapshot()自身の`initialTopIndex`引数(その宣言コメント参照)
  // として渡す――起点のtopIndex_とcursorIndex_の関係がたまたまズレて
  // いた場合(例: PageDown直後は、cursorIndex_がtopIndex_からheight_
  // だけ進むのに対しtopIndex_自体はmaxTopで頭打ちになり得るため、
  // 両者の差がheight_ちょうどではなくなることがある)、以前は
  // copyView_自身のclampViewport()が起点とは全く別のtopIndex_を独自に
  // 導出してしまい(cursorIndex_-height_+1、境界に強制的に寄せる式)、
  // COPY突入直後に画面が目に見えて動いてしまっていた。
  copyMode_.loadSnapshot(origin.lines(), origin.cursorIndex(), origin.topIndex());
  return true;
}

// Copyモードからのどんな退出キーでも――なぜこのポートが、legacy
// 自身の「ホワイトリストに無いどんなキーでも、それから再生する」
// という仕組みではなく、プレーンな^Gを使うのかについては
// feedCopyByte()自身のコメント参照。起点のContentViewは、Copyモード
// にいる間一切触れられないので、currentMode_を復元してそれを再描画
// することは、^O^Cより前に表示されていたものをまさにそのまま取り
// 戻すのに十分である(returnFromKey()自身の理屈を反映している)――
// それに加え、上のstartCopy()によって一時停止されたプロンプトを
// 復元する、同じ順序(まずコンテンツビューを再描画し、それからその
// 上にactivatePrompt())で、returnFromShell()自身の同一の復元ステップ
// と同じ理屈。
bool Application::returnFromCopy() {
  currentMode_ = copyReturnMode_;
  activeView().redraw();
  if (suspendedActivePromptKind_) {
    activatePrompt(*suspendedActivePromptKind_, suspendedActivePromptText_);
    suspendedActivePromptKind_ = std::nullopt;
    suspendedActivePromptText_.clear();
  }
  return true;
}

// copyMouseDown()/copyMouseDrag()/copyMouseUp()自身の宣言コメント
// (application.hpp)参照。
// clampRowToCopySnapshot()自身の宣言コメント参照(2026-09-07追加、
// 直接のユーザー報告⑧: "^O^C後はマウスカーソル移動範囲をモード
// ラインよりも上かつ指定開始位置が自在[現状: ドラッグ後が自在]")。
namespace {
int clampRowToCopySnapshot(const ContentView &copyView, int row) {
  int lineIndex = copyView.lineIndexAtRow(row);
  if (lineIndex >= 0) return lineIndex;
  // lineIndexAtRow()は、クリック位置がスナップショットの最後の行より
  // 下の余白(短い内容の場合)に落ちると-1を返す――Directory等の
  // 「実在するエントリを指す」クリックにとっては正しいno-opだが、
  // COPYの自由な範囲選択にとっては「モードラインより上ならどこでも
  // 開始位置になれる」べきで、最後の行にクランプするのが自然。
  int count = static_cast<int>(copyView.lines().size());
  return count > 0 ? count - 1 : -1;
}
}  // namespace

void Application::copyMouseDown(int row, int column) {
  if (currentMode_ != ApplicationMode::Copy) return;
  int lineIndex = clampRowToCopySnapshot(copyView_, row);
  if (lineIndex < 0) return;  // 空のスナップショット――他に手の打ちようが無い
  // 2026-09-07再訂正、直接のユーザー報告⑭("マウスドラッグで範囲指定
  // はできるけど、あらたな範囲指定開始点を定められない...別の場所の
  // ハイライトが繋がってしまい全てが指定範囲となる"): 前回(98730d5)の
  // 訂正(⑨⑪、マーク中は常にキャレットの再配置だけ)は、^X SPC直後の
  // マウス操作(⑪)を正しくしたが、副作用として「一度マークが実質的な
  // 範囲を持ってしまうと、以後のクリックは常にそれを拡張するだけで、
  // 全く別の場所に新しい選択を始める手段が無くなる」という別の問題
  // (⑭)を生んだ。CopyMode::beginOrExtendFromMouse()自身のコメント
  // 参照――実質的な範囲を持つマークの最中の押下は、それを1つの島として
  // 確定・画面に残したまま新しいアンカーをここから始め(⑭)、まだ実質的
  // な範囲を持たない場合(⑪の^X SPC直後等)は既存のマークの自由端を
  // 定義するだけにする、という形で両立させる。
  copyMode_.beginOrExtendFromMouse(lineIndex, column);
}

void Application::copyMouseDrag(int row, int column) {
  if (currentMode_ != ApplicationMode::Copy) return;
  int lineIndex = clampRowToCopySnapshot(copyView_, row);
  if (lineIndex < 0) return;
  copyMode_.placeCursorAt(lineIndex, column);
}

void Application::copyMouseUp() {
  // 2026-09-07訂正、直接のユーザー報告⑩("ドラッグ後、クリックを
  // 解放してもハイライトは温存[現状: 消滅]"): 以前はここでconfirmMark()
  // を呼び、マウスの解放自体を「選択の確定」と同一視していた――
  // その結果、解放と同時にハイライトが消え、^[w/^[pで実際に確定する
  // 前に選択内容を目で確認する機会が失われていた。マウスの解放は
  // 「ドラッグというジェスチャの終わり」を意味するだけで、選択そのもの
  // の確定を意味しない――マーク状態(と、そのライブハイライト)は
  // そのまま保持し、キーボードの確定ショートカット(^[w/^[p)や、
  // 続けてのドラッグ/キーボード移動による調整に委ねる。このメソッド
  // 自体は今のところ何もしないが、将来ボタン解放固有の処理が必要に
  // なった時のために残しておく。
  if (currentMode_ != ApplicationMode::Copy) return;
}

// ApplicationMode::Copyが有効な間の、COPY自身の小さなキーの語彙。
// code.c自身のkey_copy[]のホワイトリスト(M_up/M_dn/M_mark/M_head/
// M_tail/M_forw/M_back/M_kforw/M_kback/M_prev/M_next/M_clear)から
// 逐語的にではなく「精神的に」移植: このポートは、既に意味を持って
// いる自身の最小限のバイトの集合を、CopyMode自身のmove*()/
// startMark()/confirmMark()メソッドへマッピングし(なぜこれが
// resolveCommand()のグローバルなテーブルを再利用しないのかについては
// application.hppのfeedCopyByte()自身の宣言コメント参照)、任意の
// 終了キーを起点のモードへ再生するのではなく、プレーンな^Gで退出する
// ――returnFromCopy()自身のコメント参照。
//
// 2026-08-31、直接のユーザー依頼("本体で変更が必要なのは^Space...
// emacsに合わせたい"): マーク開始(旧`^Space`)/確定(旧: `^Space`の
// 2回目の押下)を、emacsのset-mark-command(^@)/kill-ring-save(^[w)に
// 合わせて別々のキーへ分離した。^Spaceは日本語変換エンジン(ATOK等)の
// 入力モード切り替えキーと衝突することがあるための変更。CopyMode
// 自身のコメント参照。
bool Application::feedCopyByte(int byte, std::optional<KeyInput::SpecialKey> special) {
  // "^[N"/"^[gg"(goto-line)自身の桁蓄積――application.hpp自身の
  // copyGotoLineDigitsPending_のコメント参照。
  //
  // 2026-09-08修正、直接のユーザー報告("何も起きないんだが。旧では
  // モードライン「現在行/総行 ??%」が数値入力エリアとなって数字入力を
  // 受け付ける仕様なのでそのようにして欲しい"): legacy自身のprint_line()
  // (old/src/disp.c、linep==F2の分岐)を読んだところ、数字以外の
  // キーで即キャンセルするのではなく、認識される4種類(数字/Enter/
  // Backspace/^G)以外は単に無視して入力を待ち続ける、という仕様
  // だったと判明した(`if ((c = vt_getch()) != K_m && c != K_g &&
  // c != K_dl && c != K_h && (c < '0' || c > '9')) continue;`)。
  // Backspaceは1桁削除(蓄積が空の状態でのBackspaceはキャンセルに
  // 相当)、^Gは常に明示的なキャンセル。この仕様に合わせて全面的に
  // 書き直した。桁を1つ追加/削除するたびに、legacy自身のモードライン
  // 上の直接エコーと同じ見た目になるようcopyMode_.setGotoLinePreview()
  // を呼ぶ。
  if (copyGotoLineDigitsPending_) {
    if (!special && byte >= '0' && byte <= '9') {
      copyGotoLineDigits_.push_back(static_cast<char>(byte));
      copyMode_.setGotoLinePreview(copyGotoLineDigits_);
      return true;
    }
    if (!special && byte == kCarriageReturn) {
      copyGotoLineDigitsPending_ = false;
      copyMode_.setGotoLinePreview(std::nullopt);
      if (!copyGotoLineDigits_.empty()) copyMode_.gotoLine(std::stoi(copyGotoLineDigits_));
      copyGotoLineDigits_.clear();
      return true;
    }
    if (!special && byte == 0x7f) {  // Backspace
      if (!copyGotoLineDigits_.empty()) {
        copyGotoLineDigits_.pop_back();
        copyMode_.setGotoLinePreview(copyGotoLineDigits_);
      } else {
        copyGotoLineDigitsPending_ = false;
        copyMode_.setGotoLinePreview(std::nullopt);
      }
      return true;
    }
    if (!special && byte == kControlG) {
      copyGotoLineDigitsPending_ = false;
      copyGotoLineDigits_.clear();
      copyMode_.setGotoLinePreview(std::nullopt);
      return true;
    }
    return true;  // 未認識のキーは無視して入力を待ち続ける(legacy自身の`continue`と同じ)
  }
  // "^[gg"の2バイト目――application.hpp自身のcopyGotoLineGPending_の
  // コメント参照。2つ目が'g'以外ならこのシーケンス全体を単に吸収する。
  if (copyGotoLineGPending_) {
    copyGotoLineGPending_ = false;
    if (!special && byte == 'g') {
      copyGotoLineDigitsPending_ = true;
      copyGotoLineDigits_.clear();
      copyMode_.setGotoLinePreview(copyGotoLineDigits_);
    }
    return true;
  }
  // "^[w"(confirmMark())自身の1バイト先読み――application.hpp自身の
  // copyEscapePending_のコメント参照。'w'以外が続いた場合(または
  // specialキーが割り込んだ場合)は、そのESCシーケンス全体を単に
  // 吸収する(このモードでは元々どのみち意味を持たないため、実害は無い)。
  // "^[w"/"^[p"自身の1バイト先読み――application.hpp自身の
  // copyEscapePending_のコメント参照。
  //
  // 2026-09-07追加、直接のユーザー要望("`^[w`でクリップボードへ
  // コピーしたのちCOPYを終了、または`^[p`で他のモードへも渡せる"):
  // 'w'(従来通りconfirmMark()のみ)に加えて'p'を追加した。'p'は
  // confirmMark()に続けてreturnFromCopy()も呼ぶ(起点が生きたプロン
  // プトを一時停止していた場合、returnFromCopy()自身がそれを再開
  // する)――promptActive()になっていれば、Command::PasteClipboard
  // 自身と全く同じpromptEditor_.paste()(カーソル位置に挿入)で
  // 確定済みのCOPYの内容を流し込む。'w'自身も、矩形マーク
  // (copyMode_.rectangleMarkActive())を確定した場合に限りCOPYを
  // 終了する――直接のユーザー要望の文面通り、この自動終了は矩形
  // マーク専用で、通常マークの'w'は(既存の挙動・テストの通り)COPY
  // に留まる。'w'/'p'以外が続いた場合(またはspecialキーが割り込んだ
  // 場合)は、そのESCシーケンス全体を単に吸収する(このモードでは
  // 元々どのみち意味を持たないため、実害は無い)。
  if (copyEscapePending_) {
    copyEscapePending_ = false;
    // 2026-09-07追加、直接のユーザー要望⑬("^X^Cおよび^[^[のみ追加で
    // よろしく"): copyLeaderXPending_のkControlCケース(このファイル
    // 内、少し下)と対になるもう一方の例外。通常モード側でも
    // dispatchLeaderByte()のescPending_ブロックが全く同じ"^[^["を
    // Command::StartExitへ結線している(同ブロック自身のコメント
    // 参照、emacsのC-x C-c相当をESC ESCでも呼べるようにする趣旨)ので、
    // COPYモードの内側だけこの組み合わせが無反応というのは一貫性を
    // 欠く。マーク中の内容は確定・保存せず、'w'/'p'いずれでもない
    // ため下のsetKillBuffer()/copyTextToSystemClipboard()へは進ませず
    // ここで直接returnする("終了"はコピーを確定する操作ではないため)。
    if (!special && byte == kEscape) {
      returnFromCopy();
      handleCommand(Command::StartExit);
      return true;
    }
    // "^[N"/"^[gg"(goto-line)自身の入口――application.hpp自身の
    // copyGotoLineGPending_/copyGotoLineDigitsPending_のコメント参照
    // (2026-09-08追加、直接のユーザー要望"未サポートでもある^[N
    // （Nは行番号）で当該行へジャンプする機能をサポートして欲しい。
    // emacsではalt＋ggNなのでそちらもよろしく"): 数字が直接続けば
    // (^[N)そのままcopyGotoLineDigitsPending_へ、'g'が続けば
    // (^[gg、Emacs自身のM-g g)まず2バイト目の'g'を待つ状態へ。
    if (!special && byte >= '0' && byte <= '9') {
      copyGotoLineDigitsPending_ = true;
      copyGotoLineDigits_.assign(1, static_cast<char>(byte));
      copyMode_.setGotoLinePreview(copyGotoLineDigits_);
      return true;
    }
    if (!special && byte == 'g') {
      copyGotoLineGPending_ = true;
      return true;
    }
    bool wasRectangle = copyMode_.rectangleMarkActive();
    if (!special && byte == 'w') {
      copyMode_.confirmMark();
      if (wasRectangle) returnFromCopy();
    } else if (!special && byte == 'p') {
      // 2026-09-07再訂正、直接のユーザー要望⑦("^@で範囲指定を開始した
      // のち、停止位置で^[pが投入されたらその場で範囲指定終了
      // （^[w相当）とみなしてPROGRAMへ移行し、プロンプトにペーストして
      // 欲しい"): 以前は「起点が生きたプロンプトを一時停止していれば
      // それを再開してペーストする」という汎用的な挙動だったが、
      // ユーザーはPROGRAM specificallyへの移行を求めている――
      // startProgramPrompt()を明示的に呼ぶ(起点にプロンプトが無かった
      // 場合でも常にPROGRAMへ入る)。setText()(paste()ではなく)を使う
      // 理由はstartProgramPrompt()自身の事前入力との混在を避けるため
      // ――前回の訂正(84067f1→4793907)で既に確認済みの理由と同じ。
      copyMode_.confirmMark();
      returnFromCopy();
      startProgramPrompt();
      if (promptActive()) promptEditor_.setText(copyMode_.clipboardText());
    }
    promptEditor_.setKillBuffer(copyMode_.clipboardText());
    // copyTextToSystemClipboard()自身のコメント参照(2026-09-07追加、
    // 直接のユーザー報告⑫)――上のsetKillBuffer()と同じく、'w'/'p'
    // いずれが押されたか(あるいは何も変わらなかったか)に関わらず
    // 無条件に同期する。
    copyTextToSystemClipboard(copyMode_.clipboardText());
    return true;
  }
  // "^X SPC"(startOrUpgradeRectangleMark())自身の1バイト先読み――
  // application.hpp自身のcopyLeaderXPending_のコメント参照(2026-09-07
  // 追加、直接のユーザー要望"①emacsには矩形範囲指定の機能がある
  // のであれを移植してほしい...emacs同様^X␣で発動")。
  //
  // 2026-09-07追加、直接のユーザー要望⑬("^X^Cおよび^[^[のみ追加で
  // よろしく"): feedCopyByte()自身の宣言コメントが言う「^G以外は
  // ほぼ全て吸収する設計」に対する、ユーザー自身が選んだ最小限の
  // 例外2つのうち1つ。通常モード側の^X^C(このファイル内、
  // leaderXPending_ディスパッチのkControlCケース、"終了"参照)と
  // 全く同じ意味を、COPYモードの内側からも直接発動できるようにする
  // ――emacsのC-x C-cが常時どのバッファからでも効くのと同じ理屈。
  // returnFromCopy()で起点のモードへ戻してから、通常のExit確認
  // プロンプトを開始する(マーク中の内容は保存せず捨てる――
  // "終了"はコピーを確定する操作ではないため)。SPCでもkControlCでも
  // ない場合は、従来通り単に吸収する(copyEscapePending_と同じ理屈)。
  if (copyLeaderXPending_) {
    copyLeaderXPending_ = false;
    if (!special && byte == ' ') {
      copyMode_.startOrUpgradeRectangleMark();
    } else if (!special && byte == kControlC) {
      returnFromCopy();
      handleCommand(Command::StartExit);
    }
    return true;
  }
  // ^X(2026-09-07追加、同上): 通常のCOPYモードの語彙には元々含まれて
  // いない(この関数自身の宣言コメントが挙げるlegacy自身のkey_copy[]
  // ホワイトリストにも無い)キーだが、emacsのC-x SPCに倣い、マーク中
  // か否かに関わらず常に「矩形マーク発動の2バイトの先頭」として先読み
  // する――emacsのrectangle-mark-modeは、マークが未設定の状態からも
  // C-x SPCだけで新規に開始できるため。
  if (!special && byte == 0x18) {
    copyLeaderXPending_ = true;
    return true;
  }
  if (special) {
    switch (*special) {
      case KeyInput::SpecialKey::Up:
        copyMode_.moveUp();
        return true;
      case KeyInput::SpecialKey::Down:
        copyMode_.moveDown();
        return true;
      case KeyInput::SpecialKey::Left:
        copyMode_.moveLeft();
        return true;
      case KeyInput::SpecialKey::Right:
        copyMode_.moveRight();
        return true;
      default:
        return true;  // 吸収される――PageUp/PageDown等はここでは意味を持たない
    }
  }
  switch (byte) {
    case kControlG:
      return returnFromCopy();
    case 0x10:  // ^P
      copyMode_.moveUp();
      return true;
    case 0x0e:  // ^N
      copyMode_.moveDown();
      return true;
    case 0x02:  // ^B
      copyMode_.moveLeft();
      return true;
    case 0x06:  // ^F
      copyMode_.moveRight();
      return true;
    case 0x01:  // ^A: 行頭(このポートの他の場所と同じ意味)
      copyMode_.moveToLineStart();
      return true;
    case 0x05:  // ^E: 行末
      copyMode_.moveToLineEnd();
      return true;
    case kEscape:  // "^[w"のリーダー――上のcopyEscapePending_参照
      copyEscapePending_ = true;
      return true;
    case 0x00:  // ^@(M_mark開始) -- 実端末では0を読めないのでKeyInputが0x80として報告する
    case 0x80:
      // 2026-09-08、直接のユーザー報告により撤回: 一時^-(Ctrl+マイナス、
      // 0x1F)をATOK非衝突な別名として追加していたが(コミット2acca7d/
      // 90ce7c1)、Ubuntu実機で確認したところ、今度はGNOME Terminal側の
      // フォントサイズ縮小ショートカット(Ctrl+マイナス)と衝突すると
      // 判明した。ユーザー自身の判断: "OSで場合分けするよりも[ATOKとの
      // 衝突は]-cでしか起きない症状なので、[ATOKの]普及度から言っても
      // ^@に戻すのが最善"――ターミナルエミュレータ/デスクトップ環境
      // ごとに何が衝突するかは千差万別で、キーを変えるだけでは根本
      // 解決にならないと判断し、この別名は撤去する。^@自身の物理キー
      // (Ctrl+Space)がATOKと衝突するのは-c(実端末)の中でIMEが有効な
      // 場合限定であり、-g自身は(graphical_display.cpp自身の同日の
      // 修正コメント参照)IME関連の横取りを一切経由しないため無関係
      // ――-gで万一OS/デスクトップ環境側の別のショートカットが
      // Ctrl+Spaceを奪っている場合は、下のbeginOrExtendFromMouse()
      // (マウスドラッグ)が代替手段になる(HELP.jp側での言及は別途
      // 検討)。
      //
      // 2026-09-08追加、直接のユーザー報告("^@がトグル（ON/OFF）に
      // なってない"): startMark()ではなくtoggleMark()を呼ぶよう変更
      // ――copy_mode.hpp/cpp自身のtoggleMark()宣言・実装コメント参照。
      copyMode_.toggleMark();
      return true;
    case '.':  // 後方互換のトグル別名(ATOKと衝突しないため'.'は変更していない)。
               // このポート自身の他の場所にあるToggleMarkの別名(Command::ToggleMark
               // 自身のコメント参照)に一致しているが、無関係な機能(CP/MV/RMのエントリの
               // マーキング)であり、このディスパッチはどのみち一度もそのグローバルな
               // Commandへ流れ落ちないので、実際の衝突は無い。
      if (copyMode_.markActive()) {
        copyMode_.confirmMark();
        promptEditor_.setKillBuffer(copyMode_.clipboardText());
      } else {
        copyMode_.startMark();
      }
      return true;
    default:
      return true;  // 吸収される――Copyモードにいる間、他の全てのキーはno-opである
  }
}

// m_grep()から移植: FGREP/EGREPに入ることは、単にどこへ戻るかを覚え
// (legacy: save_stack())、次のバイトがコマンドではなく検索パターン
// になるようプロンプト状態へ切り替える。grepの結果の内部からの
// ネストした検索の開始は拒否する、m_grep()自身のis_name(ap->name,
// M_fgrep/M_egrep)のガードに合わせている。
//
// (2026-08-05〜2026-09-11)以前はEGREP(Regex)だけ空のプロンプトを
// スキップし、代わりに直接候補リストへ開く意図的な例外だったが、
// 2026-09-11、直接のユーザー依頼によりこの例外は撤廃した――詳細は
// startGrepPrompt()自身のコメント参照。FGREP/EGREP/GREPのいずれも
// 今は同じ空のプロンプトから始まる。
// 2026-09-11追加、直接のユーザー依頼("^Sと^[iをFILE/DIR以外(現状GREP
// が稼働している全てのモード)へも反映してほしい"): in-place方式
// (専用の一覧画面Grepへは切り替えず、元の画面にマッチをハイライト
// 表示したまま留まる)の対象モード一覧――executeGrep()/
// startIncrementalGrepPrompt()の両方から参照される単一の判定基準。
// Grep自身(再入場防止)、Cut(セグメント一覧自体、CutContentとは別物)、
// Copy(凍結テキスト選択、性質が違う)は対象外。同日、FGREP/EGREPを
// GREPと同じ仕様に統一する依頼(startGrepPrompt()自身のコメント参照)
// により、executeGrep()側のgrepSyntax_==Basic限定も撤廃したので、
// ^J^S(EGREP)/^O^S(FGREP)も今はここに含まれるモードならin-place化
// される。
bool Application::isGrepInPlaceEligibleMode(ApplicationMode mode) const {
  switch (mode) {
    case ApplicationMode::Directory:
    case ApplicationMode::File:
    case ApplicationMode::Man:
    case ApplicationMode::Locate:
    case ApplicationMode::Du:
    case ApplicationMode::Archive:
    case ApplicationMode::ArchiveContent:
    case ApplicationMode::Shell:
    case ApplicationMode::CutContent:
    case ApplicationMode::ListBrowser:
    // 2026-09-26追加、直接のユーザー依頼("HISTORY.mdを`^<`でCUTして調べ事
    // しようとするのに検索は必要なのでCUT一覧上のF/EGREPを実装して"): CUTの
    // 一覧(区切った各セグメントの見出し)でも^S/^J^Sで検索し、マッチした
    // 見出しへ移動して^Mでそのセグメントを開けるようにする。
    case ApplicationMode::Cut:
      return true;
    default:
      return false;
  }
}

bool Application::startGrepPrompt(GrepSyntax syntax) {
  if (currentMode_ == ApplicationMode::Grep) return false;
  grepSyntax_ = syntax;
  grepReturnMode_ = currentMode_;
  // grepReturnListCategory_自身の宣言コメント参照: grepReturnMode_が
  // ListBrowserになる場合だけ、上書きされる前のlistBrowserCategory_
  // (RecentFile/RecentDirectoryのどちらを閲覧中か)をここで退避する。
  if (grepReturnMode_ == ApplicationMode::ListBrowser) grepReturnListCategory_ = listBrowserCategory_;
  // 2026-09-11削除、直接のユーザー依頼("FGREPおよびEGREPを同じ仕様に
  // 統一して欲しい。違いは直値および拡張正規表現で検索が行われる点
  // ...候補一覧は廃棄"): 以前はGrepSyntax::Regex(EGREP)だけ
  // startRegexPromptList()経由で専用の候補一覧(usilicrcのGREPシード
  // +過去に検索した正規表現の履歴)へ直接開いていたが、この場当たり的
  // な候補一覧自体がユーザー体験を損なっていた(以下の直接のユーザー
  // 指摘参照: (1)実機で`^X^F`から`^J^S`を起動→検索確定→結果一覧で
  // Enterしても、FILE上のマッチ箇所へ行かずEGREP自身の候補一覧画面に
  // なってしまうバグに遭遇――`openSelectedGrepMatch()`が
  // `grepReturnMode_==ListBrowser`起点を一度も想定していなかったため。
  // (2)GREP(Basic)の洗練されたin-place UI(マッチ行センタリング等)を
  // 見た後では、EGREP結果一覧のマゼンタハイライトだけ同じ動きをしない
  // ことに強い違和感がある――新規ユーザーも同様に感じるはず。
  // (3)候補一覧自体、旧ソース時代の場当たり的収集物でしかなく正規表現
  // の代表例でもない、マニュアル解説の予定も無い――中途半端なデフォル
  // トを見せるくらいなら、正規表現に疎いユーザーにはLLM等で調べて
  // もらう方がよほど有用。以後はFGREP/EGREP/GREPのいずれも同じ空の
  // プロンプトから始まり(下のactivatePrompt()、Basicと全く同じ経路)、
  // 違いは検索アルゴリズム(固定文字列/拡張正規表現/基本正規表現)
  // だけになる――isGrepInPlaceEligibleMode()自身のコメント参照
  // (grepSyntax_==Basic限定を撤廃)、これによりFGREP/EGREPも同じ
  // in-place方式(専用の一覧画面Grepへは切り替えない)になり、問題(1)の
  // バグ自体もこの経路が到達不能になることで解消する。
  activatePrompt(PromptKind::GrepPattern, "");
  return true;
}

// Command::StartIncrementalGrep自身の宣言コメント参照(2026-09-10追加、
// 2026-09-19に発動キーがESC iからプレーンな^Sへ一本化)。
// 2026-09-18修正、直接のユーザー依頼(Command::StartIncrementalGrep自身の
// コメント参照): searchInProcess()自体は元々grepSyntax_の値に関わらず
// 常にリテラル一致なので、GrepSyntax::Basicのままではラベル("GREP")と
// 実際の挙動(リテラル)が食い違っていた――GrepSyntax::Fixed(FGREP)に
// 揃えて表示と挙動を一致させる(モードラインのラベルが今も"FGREP"で
// あるのはこのため――^Sの検索が「リテラル」であることを、^J^Sの
// "EGREP"と対にして示し続ける)。
bool Application::startIncrementalGrepPrompt() {
  if (!isGrepInPlaceEligibleMode(currentMode_)) return false;
  grepSyntax_ = GrepSyntax::Fixed;
  grepReturnMode_ = currentMode_;
  incrementalGrepActive_ = true;
  activatePrompt(PromptKind::GrepPattern, "");
  return true;
}

// liveIncrementalGrepSearch()自身の宣言コメント参照(application.hpp)。
void Application::liveIncrementalGrepSearch() {
  std::string pattern = promptEditor_.text();
  if (pattern.empty()) {
    if (grepInPlaceActive_) deactivateGrepInPlace();
  } else {
    ContentView &source = grepViewForMode(grepReturnMode_);
    std::vector<std::string> sourceLines;
    sourceLines.reserve(source.lines().size());
    for (const auto &line : source.lines()) sourceLines.push_back(line.text);
    if (grepMode_.searchInProcess(sourceLines, pattern)) {
      grepInPlaceOriginMode_ = grepReturnMode_;
      activateGrepInPlace();
    }
  }
  // activateGrepInPlace()/deactivateGrepInPlace()はどちらも最後に
  // target.redraw()を呼び、通常のモードラインを画面最終行(まさに
  // 生きたプロンプトの行)へ描き直してしまう――直後にプロンプト自身の
  // 表示をもう一度その上へ描き直す(activatePrompt()自身が最初に
  // プロンプトを開く時に使うのと同じ組)。
  // 2026-09-11修正、grepInPlaceMatchLabel()自身のコメント参照:
  // 空文字列ではなく「マッチ番号/総マッチ数」を渡す(マッチが無い/
  // まだ検索していない間はgrepInPlaceActive_がfalseなので自動的に
  // 空文字列のまま――呼び出し元を選ぶ条件分岐は不要)。
  drawPromptLabel(promptKind_, grepInPlaceMatchLabel());
  promptEditor_.redraw();
}

// (2026-09-11追記): 以下はK-32/K-34(^J^S EGREP、^J^C CUTのregex亜種)が
// この関数を使っていた頃の経緯だが、startGrepPrompt()自身のコメント
// 参照の統一により、EGREP/CUT-Regexはもうこの関数を呼ばない(FGREPと
// 同じ空のプロンプトから始まる)――以下の説明はRecentFile/
// RecentDirectory/ManName/ProgramCommand(K-44/K-42、下の方の追記参照)
// が引き続きこの関数に依存している理由の歴史的記録として残す。
//
// K-32/K-34(^J^S EGREP、^J^C CUTのregex亜種、usilicrc自身のK-31..K-34
// の文書化による「Select string」――K-31/K-33の「Input string」
// FGREP/CUT-fixedとは異なる): 実際のlegacy-usilicへの直接のPTYテスト
// (新規の$HOME、~/.usilic/historyが全く無い――項目170自身の⑫の調査参照)
// により、これが単なる文書上のニュアンスではないことが確認された:
// ^J^S/^J^Cは、空のプロンプトではなく、候補パターンリスト
// (usilicrc自身のGREPセクション、Configuration::grepSeedPatterns()の
// コメントと、このポートが今や構築時に行うシードの投入参照)へ
// 「直接」開く――そこへ行くのに^Rが押されることさえ一度も無い。
// 2026-08-05のユーザー報告は、特にCUTについてこれを求めた: (それら
// 自身のモードラインの名前で視覚的に区別できる)FGREP/EGREPとは
// 異なり、CUTの2つの亜種はどちらも同一に"CUT"として表示される
// ――リストを前もって表示することだけが、regex/「Select」の意味論
// が「この」呼び出しについて有効であることを示す唯一の目に見える
// 信号であり、ユーザーが、CUTの固定文字列亜種だと思い込んでいる
// ものにリテラルな文字列をタイプしてしまうのを防ぐ。
//
// そのRingCategory選択のswitchを複製するのではなく、
// startPromptHistoryRecall()をまるごと再利用する: それは既に、(a)
// promptKind_/grepSyntax_/cutSyntax_(どちらもこれが実行される前に
// 呼び出し元によって既に設定されている)から正しいringを選ぶ方法、
// (b)^Gで編集可能な「空の」プロンプトを復元する方法
// (returnFromListBrowser()自身のpromptHistoryRecallKind_の分岐)
// ――これは「1つを選ぶ代わりに新しいパターンをタイプする」という
// 機能を兼ねるので、そのために新しいUIを作る必要は無かった――そして
// (c)選択された項目でのEnterを、ライブなEnterがそうするのとまさに
// 同じexecuteGrep()/executeCut()経路経由でディスパッチする方法を
// 知っている。まずpromptEditor_.setText(""): これが無いと、
// startPromptHistoryRecall()は、最後にどのプロンプトが使われた時の
// ものであれ、たまたま残っていた古いテキストを取得してしまい、
// ユーザーが^G経由で戻る際にそれ(新鮮な空のプロンプトではなく)を
// 復元してしまう。
//
// 2026-08-13(CLAUDE.mdの経緯の項目289の追跡): その名前にも関わらず、
// これは実際には全くregex固有ではない――これは単に「プロンプトの
// 履歴ringを、空のエディタではなく候補リストとして開く」だけであり、
// これはまさにMAN/PROGRAM自身の「Select」キー(K-44/K-42)も必要と
// している(startManList()/startProgramList())、それら2つについての
// usilicrcの「Select」という表現に、ここで扱われているEGREP/
// CUT-regexの「Select string」という表現と同じように合わせている。
// 既存の全てのEGREP/CUT呼び出し箇所にわたる無関係な差分を避ける
// ため、リネームはされていない。
// 一覧(ListBrowser)のカーソルを、指定した項目の行へ移す(2026-09-21追加、DIR/FILE/MAN一覧から
// 開いた先で^Gして一覧へ戻る時、選んだ項目へカーソルを復元するため)。無ければ何もしない。
void Application::selectListBrowserItem(const std::string &item) {
  const auto &items = listBrowserMode_.items();
  auto it = std::find(items.begin(), items.end(), item);
  if (it == items.end()) return;
  listBrowserView_.setCursorIndex(static_cast<int>(it - items.begin()));
  listBrowserView_.redraw();
}

bool Application::startRegexPromptList(PromptKind kind) {
  promptKind_ = kind;
  promptEditor_.setText("");
  return startPromptHistoryRecall(/*directEntry=*/true);
}

// m_cut()から移植: Fileモード(ファイル全体の新規の分割)、
// CutContent/Cut(再分割、すぐ下の2026-08-05のコメント参照)、あるいは
// ――2026-08-11以降――Directoryモード(現在の`ls`一覧自体の新規の分割、
// startGrepPrompt()自身の制限の無いDirectoryのサポートを反映して
// いる;一覧の行が実際にどこで読まれるかについてはexecuteCut()自身
// のDirectoryの分岐参照)から到達可能。Grepからの開始はこのスライス
// には含まれない。
bool Application::startCutPrompt(GrepSyntax syntax) {
  // ApplicationMode::Cut(2026-08-05修正): 再分割は既にCutContent
  // (先に特定のセグメント自身のコンテンツに「入って」いる)について
  // 正しく結線されていたが、直接のユーザーの再現(HELP.jpを選択 ->
  // ^Jc "man" -> 今やラベルの「リスト」自体を見ている、
  // ApplicationMode::Cut、カーソルはあるエントリの上 -> ^Jc "usilic"を
  // 「再び」)により、このガードが2回目の^Jcを完全に拒否し、"usilic"の
  // キー入力シーケンス全体を黙って通常のリストのナビゲートへ落とし
  // てしまうことが示された。CutMode::selectedSegment()/
  // splitSelected()は、cutView_/cutContentView_のどちらがたまたま
  // 現在「表示されている」ビューであるかに関わらず、既にview_
  // (== cutView_)「自身の」cursorIndex_を読んでいる――それらが実際に
  // 既にCutContentの内部にいることを必要とするものは何も無く、この
  // ガードは単に必要以上に厳しかっただけである。
  // 2026-08-11: ApplicationMode::Directoryが追加された――実際の報告
  // により、プレーンなディレクトリ一覧からの^J^Cが、^J^S(EGREP、
  // startGrepPrompt()にはそのような制限が全く無い)とは異なり、黙って
  // no-opになる("反応無し")ことが判明した。CUTは、GrepMode::search()
  // が既にgrepReturnMode_で分岐してそうしているのと同じように、
  // directoryView_「自身の」行を読むようには一度も結線されていなかった
  // ――これと併せて追加された、executeCut()自身の新しいDirectoryの
  // 分岐参照。
  // 2026-09-15修正、直接のユーザー方針("CUT、F/EGREPは[MAN、LOCATE、
  // DU等に]関与（検索）できる"): Man/Locate/Duを許可対象へ追加した。
  // これらは(直前のMAN/LOCATE/DUの修正で確立した通り)Directory固定の
  // 戻り先を持つ「自己完結した」目的地だが、CUT/GREP自身は逆にそれら
  // へ「関与」できる側であり、実際に発動した場所(Man/Locate/Du自身)
  // へ正しく戻れる必要がある――すぐ下のcutReturnMode_/cutOrigin_の
  // 新設分岐参照。
  if (currentMode_ != ApplicationMode::File && currentMode_ != ApplicationMode::CutContent &&
      currentMode_ != ApplicationMode::Cut && currentMode_ != ApplicationMode::Directory &&
      currentMode_ != ApplicationMode::Man && currentMode_ != ApplicationMode::Locate &&
      currentMode_ != ApplicationMode::Du)
    return false;
  cutSyntax_ = syntax;
  if (currentMode_ == ApplicationMode::File) cutReturnMode_ = ApplicationMode::File;
  // 2026-09-15追加: Man/Locate/Du自身も、Fileと同じ理屈で明示的に
  // cutReturnMode_を発動元へ揃える(デフォルトのDirectoryのままでは、
  // ^GがMAN/LOCATE/DUへではなくDirectoryへ戻ってしまう)。
  else if (currentMode_ == ApplicationMode::Man) cutReturnMode_ = ApplicationMode::Man;
  else if (currentMode_ == ApplicationMode::Locate) cutReturnMode_ = ApplicationMode::Locate;
  else if (currentMode_ == ApplicationMode::Du) cutReturnMode_ = ApplicationMode::Du;
  // cutReturnMode_はDirectoryについては手を付けないままにしてある
  // (そのクラスのデフォルトは既にApplicationMode::Directoryである、
  // startCutFilePrompt()自身の同一の理屈に合わせている、そのコメント
  // 参照)。
  // cutOrigin_(2026-08-24修正、executeCut()自身の「Fileを再利用する」
  // 分岐のコメント参照): 本物の、新しくアームされたDirectory起点の
  // 要求は、アーム時に「ここで」明示的にそれを言う必要がある――投入
  // 時にexecuteCut()がcurrentMode_だけから推測するのに任せるのでは
  // ない、これがまさに、^Gがその間に既にcurrentMode_をDirectoryへ
  // 戻していた時点で、^Rで呼び戻されたFile起点のパターンが黙って
  // Directory起点として再解釈されてしまうことを許していたものである。
  if (currentMode_ == ApplicationMode::Directory) cutOrigin_ = CutOrigin::Directory;
  // 2026-09-15追加: 上と同じ理屈でMan/Locate/Duも事前にアームする。
  else if (currentMode_ == ApplicationMode::Man) cutOrigin_ = CutOrigin::Man;
  else if (currentMode_ == ApplicationMode::Locate) cutOrigin_ = CutOrigin::Locate;
  else if (currentMode_ == ApplicationMode::Du) cutOrigin_ = CutOrigin::Du;
  // 2026-09-11削除、直接のユーザー依頼(startGrepPrompt()自身のコメント
  // 参照、"CUTも同様。基本拡張表現（GREP仕様）に統一してほしい"、
  // AskUserQuestionで(a)候補一覧だけ廃止を選択): 以前はRegex("^J^C"、
  // 「Select string」)だけEGREPと同じ理屈でstartRegexPromptList()
  // 経由の直接候補リストへ開いていたが、この統一によりCUT-Fixed/
  // CUT-Regexのどちらも同じ空のプロンプトから始まるようにした――CUTの
  // セグメント分割・ラベル付き一覧というCUT自身の仕組み(この関数の
  // 他の全て)は一切変更していない、変わるのは検索パターン入力の
  // 起動方法だけである。
  activatePrompt(PromptKind::CutPattern, "");
  return true;
}

// K-35/K-36の「通常ファイル」CUT亜種――CutMode自身のクラスコメント
// が、まさにこのショートカット(別個の「まず開く」ステップ無しで、
// ディレクトリ一覧からファイルのコンテンツを直接分割する)を、
// 結線されていない今後の課題として既に挙げていた;ここでそれを配線
// することは単に: 選択されたエントリのパスを解決し、それを保管し、
// バッファ亜種が既に持っているのと「同じ」CutPatternプロンプト/
// executeCut()を再利用するだけである――executeCut()は、
// cutFileSourcePath_が空でない時、既に表示されているバッファを読む
// のではなく、分割する前にFileMode経由でそれをロードする。
// cutReturnMode_はここでは意図的に手を付けないままにしてある: その
// クラスのデフォルトは既にApplicationMode::Directoryであり、まさに
// この亜種がそこから入場し、そこへ戻るべき場所である。
bool Application::startCutFilePrompt(GrepSyntax syntax) {
  if (currentMode_ == ApplicationMode::Directory) {
    const DirectoryEntry *entry = directoryMode_.selectedEntry();
    if (entry && !entry->isDirectory) {
      std::string path = directoryMode_.currentDirectory();
      if (!path.empty() && path.back() != '/') path += '/';
      path += entry->name;
      cutFileSourcePath_ = path;
      cutSyntax_ = syntax;
      // 2026-09-11削除: バッファ亜種のstartCutPrompt()と同じ統一により、
      // Regex("^J^B")も直接候補リストへは開かず、Fixedと同じ空の
      // プロンプトから始まる――詳細はstartCutPrompt()自身のコメント
      // 参照。
      activatePrompt(PromptKind::CutPattern, "");
      return true;
    }
  }
  // 2026-09-20追加、直接のユーザー依頼("C-j bだが、QUICK.txt記載の
  // ようなFILE履歴にしてほしい"): Directory一覧での選択が無い(または
  // Directoryモード自体にいない)場合、cutFilePendingSyntax_自身の
  // コメント参照の通り、K-45/K-47と全く同じPathInputプロンプトを
  // 起動してファイル名を入力/履歴から選ばせる――その投入は
  // executePromptSubmission()自身のPathInput分岐がこのフローへ
  // 引き継ぐ。
  cutFilePendingSyntax_ = syntax;
  activatePrompt(PromptKind::PathInput, "");
  return true;
}

// m_man()から移植: GREP/CUTとは異なり、m_man()にはis_name()による
// ネストしたエントリのガードが無く、現在アクティブなビューから何も
// 読まない(クラスコメント参照)――そのためこれは、Man自体(先にMan
// モードを離れることなく別のページを見る)を含む、どのモードからも
// 到達可能である。manReturnStack_は「最初の」1回だけ(つまりまだMan
// モードにいない時だけ)リセットされるので、何ページも連続で閲覧した
// 後の^Gは、その直前に見ていたページへではなく、その外出が元々
// 始まった場所へ依然として戻る。
bool Application::startManPrompt() {
  // 2026-09-19再訂正、直接のユーザー方針の転換("MANを使ってみて思う
  // のは、通常ファイルを閲覧中にMANを発動した場合、そこへ戻る方が
  // 自然な気がする。読書でも注釈参照後は元の場所へ戻るように"):
  // 2026-09-15の「MAN/LOCATE/DUは自己完結した目的地であり、発動元が
  // どこであれ常に基点のDirectoryへ戻るべき」という判断を撤回する。
  // nearestFileOrDirectoryAnchor()自身のコメント参照――「通常ファイル
  // かディレクトリ」を発動元自体からたどって遡る。
  if (currentMode_ != ApplicationMode::Man) {
    ApplicationMode anchor = nearestFileOrDirectoryAnchor();
    manReturnStack_ = {anchor};
    manAnchor_ = anchorSnapshotFor(anchor);
  }
  activatePrompt(PromptKind::ManName, "");
  return true;
}

// K-44(^J^M、「MAN: Select manual page」): LOCATE/DUの「Execute」/
// 「Redisplay window」のペア(CLAUDE.mdの経緯の項目288の調査と項目
// 289の追跡参照)とは異なり、usilicrc自身のここでの表現は本当に
// 「Input」/「Select」――EGREPの^J^Sと同じ形――なので、これは
// startRegexPromptList()の共有の仕組み経由で、最近のMANページの
// 候補リストへ直接開く(その仕組みについてはそれ自身のdocコメント
// が扱っている;名前にも関わらず実際にはregex固有ではない)。
// manReturnStack_は、すぐ上のstartManPrompt()が使うのとまさに同じ
// 「まだManにいない時だけ」というルールでリセットされる――
// executeMan()(候補が選ばれた時点で、openSelectedListEntry()の
// promptHistoryRecallKind_の分岐経由で到達する)は、
// manReturnStack_自体には触れず、currentMode_だけに触れるので必要
// である。
bool Application::startManList() {
  // 2026-09-19再訂正、startManPrompt()自身の直上のコメント参照――
  // 2026-09-15の「常に基点のDirectory」という判断を撤回する。
  if (currentMode_ != ApplicationMode::Man) {
    ApplicationMode anchor = nearestFileOrDirectoryAnchor();
    manReturnStack_ = {anchor};
    manAnchor_ = anchorSnapshotFor(anchor);
  }
  return startRegexPromptList(PromptKind::ManName);
}

// m_prog()のM_prog/M_prog2の入り口から移植(なぜGREP/CUT/MANとは
// 異なり、これがApplicationModeを一切切り替えないのかについては
// クラスコメント参照――フォアグラウンドのコマンドを実行し、既に
// 表示されていたものへ戻る)。
//
// 2026-08-17修正(docs/ideas/IDEAS.md:218、「新しい」機能――legacyは
// これを一度も持っていなかった、ユーザーが直接確認済み:
// "IDEAS.mdだから旧では動かない"): 実際の報告+手描きのモックアップ
// (=cb、"お手本は手入力なので念のため"――この参照は、機能している
// legacyやC++の挙動の実際のスクリーンショットではなく、手でタイプ
// されたもの)が、^Jpはプロンプトを、ハイライトされている/開いている
// ファイル自身の「素の」ファイル名で事前に埋め、カーソルは「最初の」
// 文字に置くべきだと指定した、そうすればプログラム名
// ("less ")をタイプすると、ファイルの完全なパスを手でタイプする必要
// 無く、自然に"less CLAUDE.md"へ前置される。以前は文脈に関わらず
// 常に空のプロンプトから始まっていた、これは今や、ファイルの文脈が
// 存在しない時(例えば現在のファイルの無い画面から呼び出された時)
// 常にフォールバックする先である。sourceFileは、
// goToFileSelector()自身のcurrentMode_の分岐(DirectoryのselectedEntry、
// File/Cut/CutContentのfileMode_.currentFile())を反映しているが、
// ここでテスト/依頼された内容の一部ではなかったので、意図的に
// その関数の完全な集合(ListBrowser/Locate)より狭くしてある。
bool Application::startProgramPrompt() {
  std::string seed;
  bool cursorAtStart = false;
  programPromptDirectory_.clear();

  if (currentMode_ == ApplicationMode::Directory) {
    // マークされたエントリ(targetEntries()、CP/MV/RM自身の
    // collectTargetPaths()が読むのと同じソース)は、単一のカーソル
    // エントリより優先される――2026-08-25、この事前入力を、CP/MV/RM
    // に既に配線されているのと同じdired方式のマーキングの流れ
    // ('.'/'['/']')に結びつける直接のユーザー依頼。全てのマークは
    // 同じ一覧の中に生きている(markedIndices_はenterDirectory()で
    // クリアされる)ので、それら全てに1つの共有されたディレクトリを
    // 使うことは常に正しい。ディレクトリはフィルタリングして除外
    // される、下の既存の単一エントリの挙動に合わせている(ディレク
    // トリに対する任意のPROGRAMコマンドは意味のある事前入力ではない)。
    // 単一名のケースは、下の複数名の結合(スペースを含むマークされた
    // 名前が隣と融合しないようshellQuoteArgv()を必要とする)とは別に
    // 特別扱いすることで、2026-08-25以前の挙動(素の、クォートされて
    // いないファイル名――「素の名前、完全なパスではない」のテスト
    // 参照)とバイト単位で同一に保たれている。
    std::vector<std::string> names;
    for (const auto &entry : directoryMode_.targetEntries())
      if (!entry.isDirectory) names.push_back(entry.name);
    if (names.size() == 1) {
      programPromptDirectory_ = directoryMode_.currentDirectory();
      seed = names.front();
      cursorAtStart = true;
    } else if (names.size() > 1) {
      programPromptDirectory_ = directoryMode_.currentDirectory();
      seed = shellQuoteArgv(names);
      cursorAtStart = true;
    }
  } else if (currentMode_ == ApplicationMode::File || currentMode_ == ApplicationMode::Cut ||
             currentMode_ == ApplicationMode::CutContent) {
    std::string sourceFile = fileMode_.currentFile();
    if (!sourceFile.empty()) {
      std::filesystem::path path(sourceFile);
      programPromptDirectory_ = path.parent_path().string();
      seed = path.filename().string();
      cursorAtStart = true;
    }
  } else {
    // 2026-09-27追加、直接のユーザー依頼("（ls）で可能な...^Jpプロンプトへの
    // ハイライト行挿入を、LOCATE、DU、FILE、USILIC でも可能にして"): 選択
    // (無ければハイライト行)のファイル。(ls)と同じくディレクトリは除く。
    // 全部が同じディレクトリにあればそこへcdして名前だけ、そうでなければ
    // 絶対パスで入れる。
    int size = 0;
    if (activeListMarks(&size)) {
      std::vector<std::string> files;
      for (const auto &path : listTargetPaths()) {
        std::error_code ec;
        if (!std::filesystem::is_directory(path, ec)) files.push_back(path);
      }
      if (!files.empty()) {
        std::string dir = std::filesystem::path(files.front()).parent_path().string();
        bool sameDir = true;
        std::vector<std::string> names;
        for (const auto &file : files) {
          std::filesystem::path fp(file);
          if (fp.parent_path().string() != dir) sameDir = false;
          names.push_back(fp.filename().string());
        }
        if (sameDir) {
          programPromptDirectory_ = dir;
          seed = names.size() == 1 ? names.front() : shellQuoteArgv(names);
        } else {
          seed = shellQuoteArgv(files);
        }
        cursorAtStart = true;
      }
    }
  }

  activatePrompt(PromptKind::ProgramCommand, seed, "", cursorAtStart);
  return true;
}

// K-42(^J^P、「PROGRAM: Select program name」): すぐ上のMANの
// K-43/K-44と同じ「Input」/「Select」の形――startManList()自身の
// コメント参照。MANとは異なり、ここでは戻りモードの記帳は不要:
// executeProgram()は決してApplicationModeを一切変えない(この関数
// 自身のクラスコメント)ので、startPromptHistoryRecall()が既に
// promptHistoryRecallReturnMode_に取得しているもの以上に、後の^Gが
// 巻き戻す必要のあるものは何も無い。
bool Application::startProgramList() { return startRegexPromptList(PromptKind::ProgramCommand); }

// m_locate()のM_locate(^J^L)の経路から移植: MAN/PROGRAMとは異なり、
// LOCATE自身の結果画面の内部からLOCATEに入ることは拒否される
// (m_locate()自身のis_name(ap->name, M_locate)のガード)――LOCATEが
// MAN/PROGRAMではなくGREPと共有する唯一の性質。
//
// 2026-08-13の更新(docs/ideas/IDEAS.mdのLOCATEセクション、
// CLAUDE.mdの経緯の項目288): legacy自身の「再びプロンプトを出さず
// に前回の結果セットを再利用する」というショートカット(^Jl、
// M_locate2)は複製されていない――^Jlは今や代わりにupdatedbを実行
// する(LocateUpdateDb/runLocateUpdateDb())、legacyには一度も無かっ
// た機能。精神的に複製されているのは、依頼されたUXである: ここで
// 開かれるプロンプトは(空ではなく)「最新の」locateクエリで種を
// 蒔かれるので、変更せずにEnterを押すと、`locate`を再実行すること
// なく前回の結果を再表示する(これのもう半分についてはexecuteLocate()
// 自身のコメント参照)。
bool Application::startLocatePrompt() {
  // 2026-09-27、LOCATE中の^J^Lのガード(m_locate()自身のis_name()から移植)を
  // 外した――直接のユーザー依頼("モード中のモード発動キーを有効にして
  // 欲しい（LOCATE中の^J^L等。モードスタッククリアのうえ再発動）"): 下の
  // stillInLocateRealmにLOCATE自身も含め、戻り先は最初にLOCATEを発動した
  // 場所のまま(中間の履歴は残さない)で、新しいクエリを入力し直す。
  // 2026-09-19再訂正、startManPrompt()自身のコメント参照――2026-09-15
  // の「常に基点のDirectory」という判断を撤回する。nearestFileOrDirectoryAnchor()
  // で「通常ファイルかディレクトリ」を発動元から遡る。
  //
  // 2026-09-20追加、直接のユーザー報告("LOCATE中のLOCATE発動も最初の
  // LOCATEまで履歴抹消でよろしく"、"^G連打で行き来ループ"再現手順:
  // ^J^L .txt ^M ^M(結果を直接Fileとして開く)→ そのFileから「もう一度」
  // ^J^L .md ^M ^M): currentMode_がFileであっても、そのFile自体が(直前の
  // LOCATE結果を直接開いたことで)まだLOCATEのrealmの中にいる間は
  // (fileReturnMode_自身の宣言コメント参照)、下の再計算をスキップする
  // 必要がある。nearestFileOrDirectoryAnchor()は、currentMode_==Fileの
  // 時点でそれ以上遡らずFile自身を即座に返す(MANを通常ファイル閲覧中
  // に発動した場合はFile自身が正当な基点になる、という別件の直接の
  // ユーザー依頼による意図的な設計、すぐ上のコメント参照)ため、ここで
  // 再計算してしまうと、まさに今置き換えようとしている「古い」LOCATE
  // セッションへの参照(fileReturnMode_==Locate)をうっかり新しい基点の
  // スナップショットへ凍結してしまう――結果、後でこのFileへ戻って
  // きた時にfileReturnMode_がLocateへ復元され続け、^Gを連打しても
  // File/LOCATEの間を永遠に往復するだけでDirectoryへ辿り着けなくなる
  // (openSelectedDuEntry()と同じ形のバグ、AnchorSnapshot自身のコメント
  // 参照)。スキップして「本来の」(最初のLOCATEが記録した)
  // locateReturnStack_/locateAnchor_をそのまま使い続けることで、この
  // 中間のFile自体はスタックに一切載らず、^Gの連打は最初のLOCATEを
  // 呼び出した場所まで一直線に戻る――ユーザーの明示的な依頼通り、
  // 中間の履歴は消える。
  bool stillInLocateRealm = currentMode_ == ApplicationMode::Locate ||
                            (currentMode_ == ApplicationMode::File && fileReturnMode_ == ApplicationMode::Locate);
  if (!stillInLocateRealm) {
    ApplicationMode anchor = nearestFileOrDirectoryAnchor();
    locateReturnStack_ = {anchor};
    locateAnchor_ = anchorSnapshotFor(anchor);
  }
  // 2026-09-11追加、直接のユーザー報告(docs/CHANGES.md項目439参照:
  // 「久々にusilic -c（引数無し）で起動」→事前入力された前回クエリ
  // ".txt"の末尾へ気付かず新しいクエリ".txt"を追記してしまい、
  // ".txt.txt"という誰も意図しない連結クエリになり0件になっていた)
  // への対応。selectAllOnSeed=trueにより、事前入力された前回クエリを
  // 「全選択」状態にする――そのまま何か入力すれば前回クエリごと
  // 消えて新しいクエリに置き換わり(ブラウザのアドレスバーと同じ
  // 慣習)、単にEnterだけ押せば前回クエリをそのまま再実行できる。
  const std::deque<std::string> &recent = history_.recentEntries(RingCategory::RecentLocateQuery);
  activatePrompt(PromptKind::LocateQuery, recent.empty() ? std::string() : recent.back(), "", false, true);
  return true;
}

// K-39(^Jl、「LOCATE: Execute `locate'」)、docs/ideas/IDEAS.mdの
// LOCATEセクションに従う: locateのデータベースを更新する。
// currentMode_/どのビューにも触れない――ユーザーがいた場所に留まる、
// これがナビゲートのアクションではなくメンテナンスのアクションで
// あることに合わせている。
//
// 2026-08-13の更新、直接のユーザー依頼: (1) updatedb()が完了した
// 時点で、単に黙ってユーザーがいた場所へ戻るのではなく、K-40自身の
// シード済みプロンプト(startLocatePrompt())へまっすぐ連鎖させる
// ――IDEAS.md自身の仕様("^Jl -> updatedbを実行")はコマンドの実行だけ
// をカバーしていたが、これは実際の次のアクションへループを閉じる;
// (2) updatedb()が実行されている「間」、LOCATEプロンプト自身の
// フィールドの位置に下線付きの「Updating database...」メッセージを
// 表示する(このポートは終始シングルスレッド/同期的なままである
// ――"機能自体は完了でいい"――のでこれには本物の非同期の配管は
// 必要無く、この1回の呼び出しの中で描いてからブロックし、それから
// 再び描くだけでよい)。
//
// 2026-08-13の追跡: これの最初の版は、drawPromptLabel()自身のラベル
// 描画のロジックの独自のコピーを、直接ここで手作りしていた――実際の
// 追跡報告("モードライン共有ルーチンはできない？")は、それがまさに、
// 項目291自身のモードラインの監査が他の場所でちょうど排除したばかり
// の、その場限りの複製の一種だと指摘した。今はラベル/列の計算を
// 独立に再導出するのではなく、drawPromptLabel()自体(この場合の
// ために特別に追加された、それ自身のstaticMessageパラメータ)経由で
// 行う。
// 2026-08-14の更新: "^Jl直後に「Updating database...」がモードライン
// 左端に出てくれないと意味がない。フリーズ中ではないメッセージなので"
// ――今はメッセージを即座に描く(下記参照)、どんな待機の裏にも決して
// 隠れない。
//
// 2026-08-14の追跡(実際の報告: "④の^[不具合は-gで起きる模様。
// ^Jl後の^Gも受け付けない。-cでは問題なし"): 同日の以前の2回の
// "常時^Gで...移行"という試みには、それぞれ実際の問題があった。
// 最初のものは、単に項目294の元々の「ブロックする」
// locateMode_.updateDb()呼び出しに戻し、startLocatePrompt()に到達
// すれば^Gは既に機能するはずだという理屈だった――しかし実際の
// updatedbの実行は「数分」かかることが判明し、その間そのブロック
// する呼び出しは決して戻らないので、^Gは一切読まれない。2回目の
// ものは、この同じ呼び出しの内部で、キーボード入力と
// ChildProcess::hasExited()の両方を無制限のループでポーリングする
// ことでそれを修正した――これは-cでは機能した(直接のPTYテストで
// 確認済み)が、-gでは機能しなかった: そのループはこの1つの
// Application呼び出しの「内部で」ブロックしていたので、
// app/main.cpp自身のGUIイベントポンプ(runGuiPhase()の
// gui.pumpEvents())は、そのループが戻るまで二度と実行されず、
// SDLウィンドウでの実際の^Gのクリック/キー入力はSDLによってキュー
// に積まれるが、実際には一度も処理されないことを意味していた。
// -c自身の実行ループがそれでも動いていたのは、たまたま
// STDIN_FILENOがそこでは本物のキーボードだったからにすぎない
// ――偶然であり設計によるものではなく、2つ目のバックエンドがそれに
// 対して試された瞬間に、このクラス自身の「バックエンドに依存しない」
// という性質(クラスコメント参照)を壊していた。
//
// 3回目の再設計で、全く「ブロックしない」ようにした: 切り離された
// updatedb(LocateMode::startUpdateDb())を起動し、即座に戻り、
// locateUpdateDbPid_を設定したままにする。
// awaitingLocateUpdateDb()/pollLocateUpdateDbCompletion()(それら
// 自身のコメント参照)は、代わりに「どちらの」実行ループも既に持って
// いる、繰り返しごとのティックに待機を駆動させる――-cの
// poll(STDIN_FILENO,...,100)ループと-gのwhile(!wantsExit())の
// フレームループは、どちらもバックエンドに関わらず既に自分自身で
// 繰り返すので、どちらも新しいバックエンド固有の配管を必要としない。
// ^G自体は特別なポーリングを全く必要としない: それは
// feedByte()/dispatchByte()自身の新しいトップレベルの
// awaitingLocateUpdateDb()ゲートを通じて普通のバイトとして届く、
// どちらの実行ループも既にfeedByte()を駆動するのに使っている、まさに
// 同じ呼び出しである。
bool Application::runLocateUpdateDb() {
  static const std::string kUpdatingMessage = "Updating database...";
  drawPromptLabel(PromptKind::LocateQuery, /*positionText=*/"", kUpdatingMessage);
  display_.flush();

  locateUpdateDbPid_ = locateMode_.startUpdateDb();
  if (locateUpdateDbPid_ <= 0) {
    locateUpdateDbPid_ = -1;
    startLocatePrompt();
    return false;
  }
  return true;  // 今は待機中――awaitingLocateUpdateDb()/pollLocateUpdateDbCompletion()参照
}

bool Application::awaitingLocateUpdateDb() const { return locateUpdateDbPid_ > 0; }

void Application::pollLocateUpdateDbCompletion() {
  if (locateUpdateDbPid_ <= 0) return;
  if (!ChildProcess::hasExited(locateUpdateDbPid_)) return;
  locateUpdateDbPid_ = -1;
  startLocatePrompt();
}

// 2026-09-10追加(liveIncrementalGrepSearch()自身のコメント、
// application.hpp自身のfeedPromptByteInner()宣言コメント参照): 本体
// (feedPromptByteInner())の前後でpromptEditor_.text()を比較するだけの
// 薄いラッパー。incrementalGrepActive_中に実際にテキストが変化した
// 時(通常の文字挿入だけでなく、Backspace/^U/^K/^Y等あらゆる編集操作を
// 等しく捕捉する――個々の編集操作それぞれに手を入れる必要が無い)
// だけliveIncrementalGrepSearch()を呼ぶ。
bool Application::feedPromptByte(int byte) {
  // 2026-09-18追加、HELP.txt 2.13章「LIST」の実装、promptTabArmed_
  // 自身の宣言コメント参照(Application::feedShellByte()自身の同一の
  // 修正と同じ理由・同じ配置)。
  if (byte != 0x09) promptTabArmed_ = false;
  bool trackIncremental = incrementalGrepActive_ && promptKind_ == PromptKind::GrepPattern;
  std::string before = trackIncremental ? promptEditor_.text() : std::string();
  bool result = feedPromptByteInner(byte);
  if (trackIncremental && incrementalGrepActive_ && promptKind_ == PromptKind::GrepPattern &&
      promptEditor_.text() != before) {
    liveIncrementalGrepSearch();
  }
  return result;
}

bool Application::applyWordEditEscape(LineEditor &editor, int byte) {
  if (!escPending_) return false;
  switch (byte) {
    case 'f':
      editor.moveWordRight();
      break;
    case 'b':
      editor.moveWordLeft();
      break;
    case 'd':
      editor.killWordForward();
      break;
    case 0x7f:
    case 0x08:
      editor.killWordBackward();
      break;
    default:
      return false;
  }
  escPending_ = false;
  return true;
}

bool Application::feedPromptByteInner(int byte) {
  // ^Q(2026-08-11修正): 実際のユーザーの記憶――"文字列入力の際の
  // コントロールコードは^Qの前置で入力できたような"。LineEditorは
  // 既にtoggleQuotedInsert()/quotedInsertPending()/clearQuotedInsert()
  // を実装している(key.c自身のK_qのケース、`Key.f.ctrl = 1;`から
  // 移植)が、どこにも一度もそれらを呼ぶものが無かった――項目273の
  // 修正の前のkillToStart()/killToEnd()と同じ「存在するが結線されて
  // いない」パターン。下の他の全ての特殊バイトより「先に」チェック
  // される: 一度アームされると、まさに「次の」バイトは、それが他に
  // 何を意味するかに関わらず(kCarriageReturn/kControlG/^R/^U/^K/DEL
  // でさえも)リテラルに挿入される、パターンの途中にリテラルな制御
  // バイトを埋め込んでいるusilicrc自身のGREPのシードパターンに合わ
  // せている(生のファイルを読んで確認済み: 64行目の"^  [0-9]\x08
  // [0-9]+\."にはリテラルな^Hがあり、71行目の"^\x0c"にはリテラルな
  // ^Lがある――^Q^H/^Q^Lが、おそらくそれらが元々タイプされた方法で
  // ある)。
  if (promptEditor_.quotedInsertPending()) {
    promptEditor_.clearQuotedInsert();
    promptEditor_.insertByte(static_cast<unsigned char>(byte));
    promptEditor_.redraw();
    return true;
  }
  if (byte == kControlQ) {
    promptEditor_.toggleQuotedInsert();
    return true;
  }
  // リーダーのシーケンス(^J/^X/^O/ESC、2026-08-12修正): 文字列入力の
  // ルーチンが「場当たり的」に見えるという直接のユーザーの不満、なぜ
  // なら^J^W(「SHELL: insert buffer content」/K-64)は^J^S候補リスト
  // (すぐ上のfeedRegexListByte()自身の2026-08-11修正)からは動作した
  // が、FGREP/CUT自身のプレーンなプロンプト入力からは「動作せず」
  // ――そこでは依然としてリテラルな反転表示の"JW"テキストを表示して
  // いた。根本原因: 「この」関数――全てのPromptKindが既に使っている
  // 唯一の共有されたプロンプト編集ルーチン(GrepPatternはFGREPの
  // ^Jcと、EGREPの直接入力の経路の両方をカバーする、CutPattern、
  // ManName、ProgramCommand、LocateQuery、
  // Copy/Move/RemoveDestination、ExitConfirm、PathInput、
  // ArchiveDestination――列挙型自身のコメント参照。DuPathは2026-09-09
  // にプロンプト自体が廃止された)――
  // feedRegexListByte()とは異なり、リーダーバイトを一切チェックして
  // いなかった。モードごとにではなく「ここで」それを修正することが
  // まさに求められていたことである: "全モードが同ルーチンを共有化
  // する形に作り直して欲しい"――そしてそれらのプロンプトの種類の
  // どれもが既にこの単一の関数へ流れ込むので、ここでの1つのチェック
  // がそれら全てを一度にカバーする、legacyのkey.c自身の
  // `case K_w: if (f_M != T_input) { c = T_w; break; }`という設計
  // (リーダーバイトはT_input以外の全ての文脈でそのコマンドの意味へ
  // 解決される;上の^Qのクォート挿入は、パターン自体にリテラルな
  // リーダーバイトをタイプするための唯一の逃げ道として残っている、
  // 例えばusilicrc自身の^Q^Hを埋め込んだGREPパターン)に合わせている。
  // shellInsertBufferContent()自体は既に汎用的にactiveView().lines()
  // を読む(それ以上currentMode_にゲートされていない)ので、それは
  // アクティブなプロンプトの実際の裏にある画面を自然に捉える
  // ――FGREP/CUTのディレクトリ一覧、再検索の途中のGrep/Cutの結果
  // 画面、等――"画面表示のある場面ならどこでも有効"に合わせている。
  // SHELL自体は自動的に除外される: feedShellByte()は完全に別個の
  // 自分自身の入力経路を持っている(dispatchByte()は、SHELL由来の
  // プロンプトについてpromptKind_が一度も参照される前にそちらへ
  // ルーティングする)ので、この変更はそこへ手を伸ばしてSHELL自身の
  // 生きたコマンドラインを再解釈することはできない。
  // feedRegexListByte()と同じ理屈で、直接dispatchLeaderByte()へ
  // ルーティングされる: ここでdispatchByte()を呼ぶと、それ自体の
  // promptKind_チェック(promptKind_は変わっていない)を通じて単に
  // feedPromptByte()へ再入場するだけになるので、直接リーダーの
  // リゾルバへ行く。
  // ESC f/b/d/DEL(M-f/M-b/M-d/M-DEL、2026-09-26追加): グローバルな
  // ESCシーケンスには割り当てが無いので、リーダーへ回す前に横取りする。
  if (applyWordEditEscape(promptEditor_, byte)) return true;
  if (escPending_ || leaderJPending_ || leaderJOPending_ || leaderXPending_ || leaderOPending_ ||
      byte == kLeaderJ || byte == kLeaderX || byte == kLeaderO || byte == kEscape) {
    return dispatchLeaderByte(byte, std::nullopt);
  }
  if (byte == kCarriageReturn) return submitPrompt();
  if (byte == kControlG) {
    cancelPrompt();
    return true;
  }
  // K-49(^R): startPromptHistoryRecall()自身のコメント参照。どちらに
  // せよ飲み込まれる(リテラルなテキストとしては挿入されない)――0x12
  // は表示可能ではなく、legacy自身のm_list()のディスパッチも、^Rが
  // 当てはまらない文脈についてそれをリテラルにタイプすることへは
  // 一度も流れ落ちない。
  if (byte == 0x12) {
    startPromptHistoryRecall();
    return true;
  }
  // K-51/52/57/58(^W、「前のディレクトリを挿入する」): 2026-08-23
  // 追加、直接のユーザー依頼――"旧版で多用していたキーアサイン"。
  // key.c自身のread_m()から移植: `case K_w: if
  // (f_M==T_cp||f_M==T_mv||f_M==T_arc||f_M==T_arc3) { ...insert
  // `old`... }`――legacyの`lp->old`は「まさにこのディレクトリの直前に
  // 現在だったディレクトリ」である(menu.cは、あらゆるディレクトリの
  // 変更の直前に`lp->old = lp->dir`を設定する)、これはこのポートの
  // RecentDirectoryのringが既に無料で追跡しているものである: 成功
  // した全てのDirectoryMode::enterDirectory()は新しいディレクトリを
  // 追加する(それ自身のコメント参照)ので、このCP/MV/ARCの宛先プロン
  // プトが開いている時点で、ringの「最後の」エントリが「現在の」
  // ディレクトリ(操作がアームされた場所)であり、「最後から2番目」
  // がまさにlegacyの`lp->old`である。legacyは"^U^K<old>"を再生する
  // (フィールドをクリアしてから、それをタイプする)――setText()は
  // 両方のステップを1回の呼び出しで行い、カーソルを末尾に着地させる
  // ので、挿入されたパスは依然として編集/拡張できる(ユーザー自身が
  // 説明した使い方に合わせている: 挿入してから、サブディレクトリ/
  // ファイル名を末尾にタイプし続ける)。
  if (byte == kControlW && (promptKind_ == PromptKind::CopyDestination ||
                             promptKind_ == PromptKind::MoveDestination ||
                             promptKind_ == PromptKind::ArchiveDestination)) {
    const auto &recentDirs = history_.recentEntries(RingCategory::RecentDirectory);
    if (recentDirs.size() >= 2) promptEditor_.setText(recentDirs[recentDirs.size() - 2]);
    return true;
  }
  // K-65(^I): PathInput自身のバッファは完全にパスそのものなので、
  // テキスト全体が補完される。PROGRAM(2026-08-17追加、スクリーン
  // ショット=ce: "'less '^Iでは反応無し")は異なる――そのバッファは
  // 完全なシェルコマンド行(例えば"less CLAUDE.md")であり、そのうち
  // 典型的には「最後の単語」だけが補完する価値のあるファイル名の
  // 引数である、これはまさにShell::onTab()が既にSHELLコマンドライン
  // 自身の最後の単語を補完するのと一致する(カーソル前のスペースで
  // 分割する同じロジック、promptEditor_とshell_.commandLine()は共有
  // ヘルパーを吊るす共通の所有者を持たない別個のLineEditorのインス
  // タンスなので、共有するのではなくここにコピーしてある)。
  // 2026-09-18追加、HELP.txt 2.13章「LIST」の実装: ManName(旧コメント
  // は「MAN/LOCATE/GREP等には補完する意味のあるものが無い」としていた
  // が、legacyのmake_list()(old/src/key.c)はM_man/M_man2をM_prog/
  // M_shellと全く同じ$PATH検索対象に含めている――ページ名がそもそも
  // ファイルシステムパスではない、という判断自体は正しいが、legacy
  // 仕様の見落としだった)を、ProgramCommandと全く同じ「最後の単語」
  // 分割+$PATH検索の対象へ追加する。LOCATE/GREP等(パターン/クエリを
  // 入力する場所で、"最後の単語"という概念自体が意味を成さない)は
  // 引き続き除外。CopyDestination/MoveDestination/ArchiveDestination
  // (2026-08-23追加)は、PathInputと全く同じ「完全にパスそのもの」。
  // DuPathは2026-09-09にプロンプト自体が廃止されたため、このリスト
  // から削除済み――runDuOnCurrentFolder()自身のコメント参照。
  // 2026-09-18追加(フォローアップ5、直接のユーザー依頼:「^Xf/^Xdへの
  // 流用も可能なら進めて」): RecentFile/RecentDirectory(^X^F/^X^D)は
  // feedRegexListByte()自身の「編集を開始する」遷移が、これと全く同じ
  // activatePrompt()+feedPromptByte()の経路を既に使っているので
  // (promptHistoryContext_自身のコメント参照)、この条件へ2つ加える
  // だけで、直接入力中のTabによるパス補完+曖昧時の一覧表示(この機能
  // 全体)がそのままそちらにも乗る――新しい配線は一切不要。
  if (byte == 0x09 &&
      (promptKind_ == PromptKind::PathInput || promptKind_ == PromptKind::ProgramCommand ||
       promptKind_ == PromptKind::ManName || promptKind_ == PromptKind::CopyDestination ||
       promptKind_ == PromptKind::MoveDestination || promptKind_ == PromptKind::ArchiveDestination ||
       promptKind_ == PromptKind::ShellCommand || promptKind_ == PromptKind::RecentFile ||
       promptKind_ == PromptKind::RecentDirectory)) {
    const std::string &text = promptEditor_.text();
    int cursor = promptEditor_.cursorBytePos();
    std::string partial = text;
    std::size_t wordStart = 0;
    // 2026-09-18追加(フォローアップ5): ShellCommandはSHELLの生きた
    // コマンドラインそのものを引き継いだプロンプトなので、
    // ProgramCommand/ManNameと同じく「最後の単語」だけを補完対象と
    // する(Shell::onTab()自身の同一のロジックのコピー)。
    bool wordSplitting =
        promptKind_ == PromptKind::ProgramCommand || promptKind_ == PromptKind::ManName || promptKind_ == PromptKind::ShellCommand;
    if (wordSplitting) {
      std::size_t searchFrom = (cursor > 0) ? static_cast<std::size_t>(cursor - 1) : 0;
      wordStart = text.find_last_of(" \t", searchFrom);
      wordStart = (wordStart == std::string::npos) ? 0 : wordStart + 1;
      if (static_cast<int>(wordStart) > cursor) wordStart = static_cast<std::size_t>(cursor);
      partial = text.substr(wordStart, static_cast<std::size_t>(cursor) - wordStart);
    }
    // 2026-09-18追加、shouldSearchPath()自身のコメント参照: 入力
    // バッファ「全体」に区切り文字が無ければ(=まだコマンド名/ページ
    // 名自体を入力中)$PATH検索、そうでなければ従来通りカレント
    // ディレクトリのファイル名補完。PathInput(pathInputIsDirectory_
    // ==true、K-47/^X^d)/ArchiveDestination(展開先は常にディレクトリ)
    // はディレクトリのみに絞る。
    bool searchPath = wordSplitting && shouldSearchPath(text);
    bool directoriesOnly = (promptKind_ == PromptKind::PathInput && pathInputIsDirectory_) ||
                           promptKind_ == PromptKind::ArchiveDestination ||
                           promptKind_ == PromptKind::RecentDirectory;
    // 2026-09-18追加(フォローアップ5): ShellCommandはdirectoryMode_の
    // カレントディレクトリではなく、shell_自身が最後に自己申告した
    // ディレクトリ(Shell::currentDirectory()、cd等で乖離し得る)を基準
    // にする――Application構築時のshell_.setCompletionProvider()自身の
    // コメント参照、Tabの1回目(promptKind_がまだNoneのままShell::onTab()
    // 経由で処理される間)と全く同じ基準に揃えるため。
    const std::string &baseDirectory =
        promptKind_ == PromptKind::ShellCommand ? shell_.currentDirectory() : directoryMode_.currentDirectory();
    auto candidates =
        searchPath ? pathProgramCandidates(partial) : completeFilesystemPath(partial, baseDirectory, directoriesOnly);
    // 2026-09-19追加(フォローアップ11、直接のユーザー要望: "^Xfで出る
    // リストはレギュラーファイルだけにして欲しい。「/」で終わる項目を
    // 排除するだけで済むのでは？"): RecentFile(^X^F)は「最近開いた
    // ファイル」の一覧であり、ディレクトリを候補に含める意味が無い
    // ――RecentDirectory(^X^D)は既にdirectoriesOnly=trueで対称的に
    // 絞られている。completeFilesystemPath()自身はディレクトリの候補に
    // 常に末尾"/"を付ける(このファイル冒頭の一覧表示との既存の慣習)
    // ので、ご提案通りそれだけを目印に除外すれば足りる。
    // 2026-09-24修正、直接のユーザー報告("^Xf^I（履歴一覧無しFILEで
    // 補完）で通常ファイルのみ、にならない"): フォローアップ11時点では
    // "^Xf"をRecentFile(^X^F)の書き間違いと誤読しており、この除外を
    // RecentFileだけに適用していた。実際には履歴一覧の無いFILE入力
    // (PathInput、pathInputIsDirectory_==false、K-45)自体が要望の対象
    // だった――wholeFieldKind側(Tab確定時の即時submit)には後日別途
    // PathInputが追加されていたが、この「ディレクトリ除外」フィルタ側
    // への追加が抜けていたため、症状(ディレクトリ候補が混ざる)が
    // 残っていた。RecentDirectory/ArchiveDestination(directoriesOnly側)
    // と対称的に、PathInputがFILE用途の時もここに含める。
    if (promptKind_ == PromptKind::RecentFile ||
        (promptKind_ == PromptKind::PathInput && !pathInputIsDirectory_)) {
      candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                       [](const std::string &c) { return !c.empty() && c.back() == '/'; }),
                        candidates.end());
    }
    if (candidates.empty()) {
      promptTabArmed_ = false;
      return true;
    }
    // 2026-09-15修正、直接のユーザー報告("PROGRAMのプロンプトで
    // 'less 'と打ったところで^Iとやっても候補が出ない")+依頼("最長
    // 共通接頭辞まで補完")――Shell::onTab()自身の同一の修正参照
    // (このcompleteFilesystemPath()自体をそちらと共有している)。以前は
    // 候補がちょうど1件の時だけ補完し、複数件(曖昧、例えば'less 'の
    // 後の空の単語はカレントディレクトリの全ファイルが候補になる)
    // なら何もしなかった。bashの素のTabと同じ、候補全体の最長共通
    // 接頭辞まで補完する形にした。
    std::string lcp = candidates[0];
    for (std::size_t i = 1; i < candidates.size(); i++) {
      std::size_t j = 0;
      while (j < lcp.size() && j < candidates[i].size() && lcp[j] == candidates[i][j]) j++;
      lcp.resize(j);
    }
    bool ambiguous = candidates.size() > 1;
    // 2026-09-18追加、HELP.txt 2.13章「LIST」の実装(Shell::onTab()
    // 自身の同一の修正参照、legacyのf_listフラグと同じ役割)。
    if (ambiguous && promptTabArmed_) {
      promptTabArmed_ = false;
      showTabCompletionList(candidates, partial, text.substr(0, wordStart));
      return true;
    }
    if (lcp.size() > partial.size()) promptEditor_.paste(lcp.substr(partial.size()));
    if (ambiguous) {
      display_.putChar(7);  // BEL、Shell::onTab()自身の同一の修正参照
      promptTabArmed_ = true;
    } else {
      promptTabArmed_ = false;
      // 2026-09-19再訂正(フォローアップ13、直接のユーザー報告+正確な
      // 再現手順: "usilic ~/git/usilic -c^M→(ls)→^Xf^I^I^M（.DS_Storeを
      // 選択）→(ls)のまま"): フォローアップ12では「^Xf」を"^X^F"
      // (RecentFile)の書き間違いと誤読し、対象からPathInput(^X f、
      // 小文字、K-65、Command::StartFileInput)を意図的に除外していた
      // ――しかし正確な再現手順は紛れも無く小文字の「^X f」+「^I」
      // (leaderXPending_下のf、その後のTab)であり、PathInputこそが
      // 今回の一連の実機報告全体の対象だったと判明した。"^X^F^R"
      // (お手本)自体はRecentFileのままで正しい参照だったが、ユーザー
      // 自身が実際に操作・報告していたのは終始PathInputだった。
      // ArchiveDestinationもフィールド全体を1つの完結した値(常に
      // ディレクトリ)で置き換える全く同じ形のkindなので、対称的に
      // 含める――こちらには阻害する既存のdoctestが無いことを確認済み。
      if (promptKind_ == PromptKind::RecentFile || promptKind_ == PromptKind::RecentDirectory ||
          promptKind_ == PromptKind::PathInput || promptKind_ == PromptKind::ArchiveDestination) {
        return submitPrompt();
      }
    }
    return true;
  }
  // DEL/^H(2026-08-05): 全てのプロンプトの種類(MAN/LOCATE/FGREP/
  // EGREP/CUT/PROGRAM/PathInput/Copy/Move/Remove/.../
  // ArchiveDestination――この共有のfeedPromptByte()経由でルーティン
  // グされるもの全て)は、下のinsertByte()へ流れ落ちていた、それには
  // 「消去」という概念が無く、単に生の0x7f/0x08バイトを表示不可能な
  // リテラルとして挿入するだけである(print_buff()相当のレンダリング
  // によって反転表示の"?"としてレンダリングされる)。feedShellByte()
  // は既に、SHELLコマンドライン自身のLineEditorについて、この同じ
  // backspace()/redraw()のペアを持っている(すぐ上のそれ自身のコメント
  // 参照、約907行目)――promptEditor_は同じLineEditorクラスであり
  // 既にbackspace()を公開している、この汎用のプロンプトの経路は
  // 単にそれを呼ぶよう一度も結線されていなかっただけである。
  if (byte == 0x7f || byte == 0x08) {
    promptEditor_.backspace();
    promptEditor_.redraw();
    return true;
  }
  // ^A/^E/^B/^F(2026-08-12修正): LineEditorは既に
  // moveHome()/moveEnd()/moveLeft()/moveRight()を実装している
  // (feedShellByte()がSHELLコマンドライン自身の矢印キーの処理に使う)
  // が、readline方式の制御キーの相当物(^A/^E/^B/^F)をこの共有の
  // プロンプトの経路へ結線したものは一度も無かった――実際の報告:
  // "^A^E^B^F等文字列編集のためのキーが利かずハイライトAEBFとなる"、
  // つまりこれらは下のリテラルなinsertByte()のフォールバックまで
  // 完全に流れ落ち、各制御バイトはそれ自身の反転表示された"@"
  // オフセットのグリフとしてレンダリングされていた
  // (LineEditor::renderVisible()自身の慣習: バイトb < ' 'は反転表示
  // されたchr(b+'@')としてレンダリングされる、そのため0x01+0x40='A'、
  // 0x05+0x40='E'、0x02+0x40='B'、0x06+0x40='F'――まさに"AEBF")。
  // このポート自身のプレーンな^B/^Fコマンドの割り当て
  // (resolveCommand()のPageUp/PageDown、usilicrcのK-8/K-9)は、どの
  // みちここでは決して適用されないことに注意: dispatchByte()は、
  // プロンプトがアクティブな限り、resolveCommand()が一度も参照される
  // 前に、既に全てのバイトを「まず」feedPromptByte()へルーティング
  // する。
  if (byte == 0x01) {  // ^A: 行頭
    promptEditor_.moveHome();
    return true;
  }
  if (byte == 0x05) {  // ^E: 行末
    promptEditor_.moveEnd();
    return true;
  }
  if (byte == 0x02) {  // ^B: 1文字戻る
    promptEditor_.moveLeft();
    return true;
  }
  if (byte == 0x06) {  // ^F: 1文字進む
    promptEditor_.moveRight();
    return true;
  }
  if (byte == 0x04) {  // ^D: 前方削除(上の^A/^E/^B/^Fに対応するreadline系のもの)
    promptEditor_.deleteForward();
    return true;
  }
  // ^U/^K/^Y(2026-08-11修正): LineEditorは既にkillToStart()/
  // killToEnd()/yank()を実装している(key.cのK_u/K_k/K_yのケースから
  // 移植――それら自身のdocコメント参照)が、どこにも一度もそれらを
  // 呼ぶものが無かったので、これらのバイトは下のinsertByte()へ流れ
  // 落ち、行を編集する代わりにリテラルな制御文字として挿入されて
  // いた。実際の報告: startRegexPromptList()の候補リスト(^J^C)が、
  // ハイライトされている候補のテキストで事前に埋められた編集可能な
  // プロンプトモードへ遷移した(feedRegexListByte()自身の「編集を
  // 開始する」フォールバック)後、新しいパターンをタイプする前にその
  // 事前入力されたテキストをクリアするはずだった^Uが、代わりに黙って
  // プロンプトのテキストを壊してしまい(リテラルな0x15バイトが前置
  // される)、その後タイプされたパターンは何にもマッチせず、検索が
  // 黙って失敗した("何も起きない")。
  if (byte == 0x15) {  // ^U: フィールドの先頭からカーソルまで削除
    promptEditor_.killToStart();
    promptEditor_.redraw();
    return true;
  }
  if (byte == 0x0b) {  // ^K: カーソルからフィールドの末尾まで削除
    promptEditor_.killToEnd();
    promptEditor_.redraw();
    return true;
  }
  if (byte == 0x19) {  // ^Y: ^U/^Kの削除バッファを再挿入
    promptEditor_.yank();
    promptEditor_.redraw();
    return true;
  }
  if (byte == kControlUnderscore) {  // ^_: 取り消し(LineEditor::undo()参照)
    promptEditor_.undo();
    return true;
  }
  // ^N/^P(2026-08-12修正): 直接のユーザー報告――^J^S/^J^Cの候補リスト
  // を閲覧中に1文字でもタイプすると、正しく編集へ遷移する
  // (feedRegexListByte()自身の「編集を開始する」フォールバック)が、
  // それは以前は永久にそこでの^N/^P自身の意味を諦めてしまっていた
  // ("さらに^N^Pが利かなくなる（他のハイライト文字列へ移れない）")
  // ――それらはすぐ下の汎用の制御バイト拒否のcatch-allへ流れ落ち、
  // 巡回する代わりに黙ってビープを鳴らしていた。候補「リスト」自体は
  // 編集がアクティブな間ずっと画面上に完全に見えたままである
  // (activatePrompt()自身の「編集を開始する」コメント)ので、編集の
  // 途中でも^N/^Pがそれを動き続けない理由は無い――これは、^N/^Pが
  // 次に着地したどの候補ででもプロンプトを再び種付けし、それまでに
  // タイプされていたものを何であれ破棄する(コンボボックス自身の
  // 「元の候補を巡回する」という意味論に合わせている、異なるエントリ
  // へのジャンプをまたいで編集途中の内容を保とうとするものではない)。
  // promptCandidateListActive_の場合にのみ有効(それ自身のコメント
  // 参照)――プレーンなFGREP/MAN/LOCATE等のプロンプトは、そもそも
  // 巡回すべきリストから来たものではないので、そこでの^N/^Pは依然
  // として下の通常の拒否へ流れ落ちる。
  if ((byte == 0x0e || byte == 0x10) && promptCandidateListActive_) {
    if (byte == 0x0e) listBrowserView_.moveDown();
    else listBrowserView_.moveUp();
    listBrowserView_.redraw();
    int count = static_cast<int>(listBrowserMode_.items().size());
    int idx = listBrowserView_.cursorIndex();
    const std::string *selected = listBrowserMode_.selectedItem();
    drawPromptLabel(promptKind_, count > 0 ? ModeLine::format(idx + 1, count) : "");
    promptEditor_.setText(selected ? *selected : "");
    return true;
  }
  // 他の認識されない制御バイトは、リテラルに挿入するのではなく、
  // 完全に拒否する(2026-08-12修正、直接のユーザーの指示:
  // "コントロールコード入力は^Qの前置が必須、を反映して"――制御
  // コードの入力は^Qの前置を必要とすべきである)。これより前は、
  // 例えば(上のPathInput自身の補完についてのみ意味を持つ)迷子の
  // Tab(0x09)がGrepPattern/CutPattern等のプロンプトにタイプされる
  // と、下のプレーンなinsertByte()のフォールバックまで完全に流れ
  // 落ち、実際には何であるかというよりは表示の乱れのように読める、
  // 反転表示の制御文字のグリフ(「byte+'@'」の慣習については上の
  // ^A/^E/^B/^Fのコメント参照)としてレンダリングされたリテラルな
  // 制御バイトで、黙ってパターンを壊していた。この関数のまさに冒頭
  // にある^Q自身のクォート挿入のチェックが、意図的にリテラルな制御
  // バイトを挿入する唯一の方法として残っている(この変更より前から
  // 既にそうだった――これは、以前すり抜けていたバイトについての
  // クォート無しの経路を塞ぐだけである)。
  if (byte < 0x20) {
    display_.putChar(7);  // BEL、LineEditor自身の境界での拒否の慣習に合わせている
    return true;
  }
  promptEditor_.insertByte(static_cast<unsigned char>(byte));
  promptEditor_.redraw();
  return true;
}

bool Application::submitPrompt() {
  PromptKind kind = promptKind_;
  std::string text = promptEditor_.text();
  // 2026-09-10追加、直接のユーザー提案(".git ␣^M"での再現: "それでも
  // プロンプトは生きている、ってのはどうだろう？.git ␣はマッチしない
  // けど^Hで.gitに戻せばマッチするので、No Stringが消えると同時に
  // 検索も継続可能"): インクリメンタルサーチ中に0件のままEnterされた
  // 場合、下でpromptKind_/incrementalGrepActive_を書き換える「前」に
  // ここで検出し、それらは変更せずに即returnする――プロンプトが開いた
  // まま、パターン文字列もそのまま残るので、続けて^Hすれば通常の
  // liveIncrementalGrepSearch()経路でそのまま検索を再開できる。
  // "No String"自体は既にgrepNoMatchActive_として設定済み(直前の
  // キー入力時のactivateGrepInPlace()の空マッチ分岐)だが、
  // liveIncrementalGrepSearch()自身の末尾がライブ編集中は常に
  // drawPromptLabel()+promptEditor_.redraw()でそれを覆い隠して
  // しまう(ライブ中は見せない設計)ので、ユーザーの「.gitで確定したら
  // No Stringを見たい」という要望通り、ここでは`activeView().redraw()`
  // だけを呼び、その上書きをしないことで初めてメッセージを画面に
  // 出す(次の^H等のキー入力で通常通りliveIncrementalGrepSearch()の
  // 末尾がプロンプト表示を復元する)。falseを返すのは、この関数の他の
  // 失敗経路(executeGrep()がマッチ0件でfalseを返す、既存の
  // "zzz: No String"テスト参照)と同じ「この操作は何も確定しなかった」
  // という規約に揃えるため。
  if (incrementalGrepActive_ && kind == PromptKind::GrepPattern && grepMode_.matches().empty()) {
    activeView().redraw();
    return false;
  }
  promptKind_ = PromptKind::None;
  // incrementalGrepActive_自身の宣言コメント参照(2026-09-10追加)。
  // 2026-09-10同日さらに修正、直接のユーザー報告(usilic -c
  // git/usilicで再現: "^[i.→全マッチ文字.がハイライト表示→さらに
  // ␣→.␣はマッチしないのにハイライトが消えない→さらに^M→意味不明"):
  // 当初この後は無条件にexecutePromptSubmission()→executeGrep()
  // (実物のgrep、GrepSyntax::Basic=正規表現)を1回だけ実行していた
  // ――「確定した結果を、実際にusilicrcで設定された検索コマンドと
  // 完全に同じ意味論で揃え直すため」という設計だったが、これは
  // ライブ中のGrepMode::searchInProcess()が「正規表現ではなくfgrep
  // 相当のリテラル検索」であるべきという、その後の別の直接の
  // ユーザー依頼("^[iでは正規表現検索ではなくfgrepタイプにして
  // ほしい")によって前提が崩れていた: ライブ中は"."をリテラルな点
  // として扱うのに、Enterで確定した瞬間だけ実物のgrepが正規表現として
  // 再解釈してしまい、ライブで0件だったパターンがEnter後に突然
  // 大量ヒットする、という食い違いが起きていた。searchInProcess()は
  // 既にmatches_/matchSpansを直接計算済みで、実物のgrepへ問い合わせ
  // 直す技術的な必要性はそもそも無い(fgrep相当のリテラル検索に、
  // 正規表現エンジンとの間の食い違いという概念自体が存在しない)ので、
  // インクリメンタルサーチだったセッションはfinalizeIncrementalGrep()
  // (それ自身のコメント参照)という別経路へ分岐させ、ライブで既に
  // 確立済みの状態をそのまま維持するだけにした。
  bool wasIncrementalGrep = incrementalGrepActive_ && kind == PromptKind::GrepPattern;
  incrementalGrepActive_ = false;
  if (wasIncrementalGrep) return finalizeIncrementalGrep(text);
  return executePromptSubmission(kind, text);
}

// submitPrompt()自身のコメント参照。ライブ中のGrepMode::
// searchInProcess()(fgrep相当のリテラル検索)が既にmatches_/
// matchSpansを直接計算済みであり、かつactivateGrepInPlace()/
// deactivateGrepInPlace()の両方がキー入力のたびに正しく呼ばれ続けて
// きた(liveIncrementalGrepSearch()自身のコメント参照)以上、Enterの
// 時点で表示されている状態こそが既に「確定した」状態そのものである
// ――実物のgrepへ改めて問い合わせ直す必要は無い(意味論が違う以上、
// 問い合わせ直すこと自体がむしろ食い違いの原因になる、submitPrompt()
// 自身のコメント参照)。履歴への記録だけは通常の^Sと同じ慣習に揃える。
bool Application::finalizeIncrementalGrep(const std::string &pattern) {
  history_.pushRecent(grepSyntaxRingCategory(grepSyntax_), pattern);
  // 通常のexecuteGrep()経路は、その先のactivateGrepInPlace()自身の
  // redraw()によって、画面最終行(それまで生きたプロンプトが描画
  // されていた行)が自然に通常のモードラインで上書きされていた――
  // その呼び出し自体を丸ごとスキップするこの経路では、明示的に
  // redraw()し直さないと、プロンプトの最後の見た目(パターン文字列+
  // カーソル)がEnter後もそのまま画面に居座ってしまう。
  activeView().redraw();
  return true;
}

bool Application::executePromptSubmission(PromptKind kind, const std::string &text) {
  switch (kind) {
    case PromptKind::GrepPattern:
      return executeGrep(text);
    case PromptKind::CutPattern:
      return executeCut(text);
    case PromptKind::ManName:
      return executeMan(text);
    case PromptKind::ProgramCommand:
      return executeProgram(text);
    case PromptKind::LocateQuery:
      return executeLocate(text);
    case PromptKind::CopyDestination:
      return executeCopy(text);
    case PromptKind::MoveDestination:
      return executeMove(text);
    case PromptKind::RemoveConfirm:
      return executeRemove(text);
    case PromptKind::ExitConfirm:
      return executeExit(text);
    case PromptKind::ArchiveDestination:
      return executeArchiveExtract(text);
    case PromptKind::PathInput:
      // cutFilePendingSyntax_自身のコメント参照(startCutFilePrompt()
      // 経由でここへ来た場合、通常のexecutePathInput()――ファイルを
      // そのまま開く――ではなく、解決したパスをCUTの対象として
      // CutPatternプロンプトへ引き継ぐ)。
      if (cutFilePendingSyntax_) {
        GrepSyntax syntax = *cutFilePendingSyntax_;
        cutFilePendingSyntax_.reset();
        ResolvedPath resolved = pathResolver_.resolve(text, directoryMode_.currentDirectory());
        if (!resolved.exists || resolved.isDirectory) {
          activeView().redraw();
          return false;
        }
        cutFileSourcePath_ = resolved.path;
        cutSyntax_ = syntax;
        activatePrompt(PromptKind::CutPattern, "");
        return true;
      }
      if (epubPathPending_) {  // ^Je(startEpubPrompt()参照)
        epubPathPending_ = false;
        ResolvedPath resolved = pathResolver_.resolve(text, directoryMode_.currentDirectory());
        if (!resolved.exists || resolved.isDirectory || !isEpubFile(resolved.path)) {
          activeView().redraw();
          return false;
        }
        return openEpub(resolved.path);
      }
      return executePathInput(text);
    // K-46/K-48自身の投入: PathInput(K-45/K-47)と同一のパスの検証
    // ――legacy自身のkey.cは、T_hfile/T_hdirを、M_lfile/M_ldir
    // (Input亜種)と「同じ」buff[]のチルダ展開+access()チェックの
    // ブロックにグループ化している、単に空のプロンプトではなく履歴
    // リスト経由で到達するだけである。この機能の残りについては
    // startListBrowser()自身のコメント参照。
    case PromptKind::RecentFile:
      return executePathInput(text);
    case PromptKind::RecentDirectory: {
      // PathInputとは異なり、K-48はopenSelectedListEntry()の「旧」
      // RecentDirectoryの分岐のresetNavigationToFloor()呼び出しも
      // 必要とする(それ自身の宣言コメント参照: ^X^Dは明示的に通常の
      // Enterでサブディレクトリへ降りることでは「ない」ので、その
      // 後の^Gは、ピッカーが開く前からの複数レベルの降下の連鎖を
      // 巻き戻すのではなく、ここと前のディレクトリの間をトグルする
      // べきである)――その分岐は、今やRecentDirectoryについては特に
      // 到達不能である(promptHistoryRecallKind_が優先される、
      // openSelectedListEntry()自身のコメント参照)が、その挙動は、
      // ハイライトされているエントリを選ぶことで到達しようと、新しい
      // パスをタイプすることで到達しようと、この共有された投入経路
      // への移行を依然として生き延びる必要がある。
      std::string previousDirectory = directoryMode_.currentDirectory();
      if (!executePathInput(text)) return false;
      if (currentMode_ == ApplicationMode::Directory) directoryMode_.resetNavigationToFloor(previousDirectory);
      return true;
    }
    // 2026-09-18追加(フォローアップ5、PromptKind::ShellCommand自身の
    // 宣言コメント参照): Enterはこのプロンプト自身にとっては「確定」
    // だが、SHELLにとっては「このコマンドラインを実際に実行する」と
    // いう意味であり、GrepPattern等の「その場で1回だけ何かを実行して
    // 終わる」プロンプトとは違って、SHELL自体はこの後も生き続ける
    // セッションである。編集中は一切触れていなかったshell_.commandLine()
    // (Tabが最初に押される「前」の状態のまま凍結されていた)へ確定した
    // 文字列を書き戻し、Shell::submitLine()(browsingHistory自身のEnter
    // 分岐がまさに同じ「setText()してsubmitLine()」の形を使っている、
    // すぐ上のコメント参照)へ普通に投入する――これがfreezePromptLine()
    // 経由でscrollback_自身のlines_を書き換える(が、それ自体は
    // ContentView::setLines()同様、画面の再描画は一切行わない)。
    // 一覧の内容行がまだ画面に残ったままなので、submitLine()自身の
    // 内部のredrawPromptRow()呼び出し「だけ」では不十分――
    // returnFromTabCompletionList()自身の同一の理由のコメント参照、
    // scrollback_の変更の後はredraw()+redrawPromptRow()の組が必要
    // (redraw()自体がcommandLineRow()も含めて無条件にクリアするため、
    // redrawPromptRow()での描き直しが必須)。
    // 2026-09-18再訂正(フォローアップ7): currentMode_はここまで
    // (フォローアップ5以来ずっと)意図的にListBrowserのまま――
    // currentMode_を本当にShellへ戻すのはまさにこの瞬間、実際に
    // 投入が確定した「後」である。
    case PromptKind::ShellCommand:
      currentMode_ = ApplicationMode::Shell;
      shell_.commandLine().setText(text);
      shell_.submitLine();
      shell_.scrollback().redraw();
      shell_.redrawPromptRow();
      return true;
    case PromptKind::None:
    case PromptKind::Du:  // promptKind_には決して代入されない、drawPromptLabel()専用のラベルマーカー――application.hpp自身の宣言コメント参照
    case PromptKind::ArchiveLoad:  // 同上――PromptKind::ArchiveLoad自身の宣言コメント参照
    case PromptKind::EpubLoad:     // 同上(openEpub()の"Reading...")
    case PromptKind::TabCompletionList:  // 同上――PromptKind::TabCompletionList自身の宣言コメント参照
      return false;
  }
  return false;
}

void Application::cancelPrompt() {
  // cutFilePendingSyntax_自身のコメント参照: このフロー中(PathInput
  // プロンプト表示中)に^Gされた場合、次に何の関係も無いPathInput/
  // ^Xf等が起動された時に誤って引き継がれないよう無条件にクリアする
  // (このフローの外では常にnulloptなので、これは常に安全なno-op)。
  cutFilePendingSyntax_.reset();
  epubPathPending_ = false;  // ^Je(startEpubPrompt())も同じ理由
  // 2026-09-10追加、incrementalGrepActive_自身の宣言コメント参照:
  // ^Gはemacs自身のisearchの慣習(C-gで検索開始前の位置へ戻す)に
  // 合わせ、ライブ中に既に適用されていたハイライト/ジャンプ/
  // オーバーレイを完全に取り消す――Enterでの確定(submitPrompt())とは
  // 意図的に非対称: こちらは「何も無かったことにする」、あちらは
  // 「今見えている状態を確定する」。
  if (incrementalGrepActive_) {
    incrementalGrepActive_ = false;
    if (grepInPlaceActive_) deactivateGrepInPlace();
    // 2026-09-26追加、直接のユーザー報告("「^<」のファイル分割一覧上で
    // 「色」を検索...^N/^Pでちゃんと次/前のマッチ文字列へ移動する...
    // 一旦戻って再度「色」を検索しようと^S^Rとやっても「色」はやはり
    // 無い"): ^Sは入力行を開いたまま^N/^Pでマッチ間を移動できる
    // (feedByte()自身の0x0e/0x10の横取り参照)ので、「見たいところを
    // 見終えたら^Gで抜ける」のが自然な使い方になる。ところが履歴への
    // 記録はEnterでの確定(finalizeIncrementalGrep())にしか無く、^Gで
    // 抜けると何度検索しても^Rの一覧に一切残らなかった。表示の取り消し
    // (上のdeactivateGrepInPlace())はemacsのisearch同様そのままにし、
    // 入力した文字列だけは確定時と同じringへ記録する。
    std::string pattern = promptEditor_.text();
    if (promptKind_ == PromptKind::GrepPattern && !pattern.empty()) {
      history_.pushRecent(grepSyntaxRingCategory(grepSyntax_), pattern);
    }
  }
  // 2026-08-27: CP/MV/RMの宛先プロンプトのキャンセルは、以前は
  // cancelPendingCpMvRm()(directoryMode_.refresh()を呼ぶ、マークも
  // 「一掃する」実際の`ls`再実行――2026-08-24の修正、"^Gのあとls
  // 再実行でいいんじゃない？")に到達していたが、今やpendingCpMvRm_
  // はプロンプトが表示される前に既に不可分に消費されている
  // (startCopyPrompt()自身のコメント参照)ので、その経路は到達不能に
  // なり、代わりにこの汎用のキャンセルが引き継いだ。ここで「旧」の
  // 挙動を復元しようとした最初の試み(refresh()を直接呼ぶ)自体が、
  // ユーザーが即座に捕まえた実際の後退だった: "注意深く選んだマーク
  // がうっかり^Gでパーみたいな切ない事態を避けるため。意図的には
  // ^X^Nで全クリア可能なのでマークは残して欲しい"――つまり、^Gが
  // もはや別個の「アーム済み」サブモードを後に残さなくなった今、
  // 2026-08-24の理屈は実際にはもう当てはまらない(その修正は、そこ
  // から離れてナビゲートした後に古いアームのマークが居残ることに
  // ついてのものだった;ここには古くなるべき中間状態が無い)。そのため:
  // 2つの表示フラグ「だけ」を復元する(refresh()自身の同じ2つの
  // 復元と同一で、目に見える違いは無い)、markedIndices_を一掃せずに
  // ――今やマークはキャンセルされたCP/MV/RMを生き延びる、^X^N
  // (ClearAllMarks)が実際にそれらをクリアする唯一の意図的な方法で
  // あり続けるのとまさに同様に。
  if (promptKind_ == PromptKind::CopyDestination || promptKind_ == PromptKind::MoveDestination ||
      promptKind_ == PromptKind::RemoveConfirm) {
    promptKind_ = PromptKind::None;
    if (cpMvRmOrigin_ != ApplicationMode::Directory) {  // 一覧から発動(2026-09-27)、選択は残す
      cpMvRmOrigin_ = ApplicationMode::Directory;
      cpMvRmListTargets_.clear();
      activeView().redraw();
      return;
    }
    directoryView_.setCursorRowAlsoHighlighted(true);
    directoryView_.setMarkedRowUsesUnderline(true);
    directoryView_.redraw();
    return;
  }
  // 2026-09-18追加(フォローアップ5、PromptKind::ShellCommand自身の
  // 宣言コメント参照): 下の汎用の末尾(`activeView().redraw()`)は
  // currentMode_==Shellの間shell_.scrollback()を返すactiveView()経由で
  // 内容行自体は正しく再描画するが、shell_.redrawPromptRow()を呼ばない
  // ――redraw()自体がcommandLineRow()を無条件にクリアするため
  // (Shell::redrawPromptRow()自身のコメント参照)、これが無いと生きた
  // プロンプト行が空欄のまま残る。編集中一切触れていなかった
  // shell_.commandLine()は、Tabが最初に押される「前」の文字列のまま
  // なので、ここではsetText()し直す必要は無い――このまま復元表示
  // すれば良い。
  if (promptKind_ == PromptKind::ShellCommand) {
    promptKind_ = PromptKind::None;
    // 2026-09-18再訂正(フォローアップ7): currentMode_を本当にShellへ
    // 戻すのはここ(キャンセルが確定した瞬間)――executePromptSubmission()
    // 自身の同種の修正参照。
    currentMode_ = ApplicationMode::Shell;
    shell_.scrollback().redraw();
    shell_.redrawPromptRow();
    return;
  }
  // 2026-09-09、DU用の分岐がここに一時期あった(^Jdのプロンプトを
  // ^Gでキャンセルした時に既存結果へ戻す処理)が、同日中にDUの
  // プロンプト自体が完全に廃止された(runDuOnCurrentFolder()自身の
  // コメント参照)ため、この分岐ごと不要になり削除した――^Jdはもはや
  // プロンプトを一切開かないので、キャンセルされ得るDU用プロンプト
  // 自体が存在しない。
  promptKind_ = PromptKind::None;
  activeView().redraw();
}

// それ自身の宣言コメント(application.hpp)参照。
// startPromptHistoryRecall()とtoggleListBrowserHistory()の両方が、
// `items`を表示するまさに直前にこれをチェックするので、現在表示
// されているRecentFileのリストがどう(再)構築されたかに関わらず、
// アーカイブのフィルタ(hasArchiveCompatibleSuffix()自身のコメント
// 参照)は適用されたままである。
bool Application::archiveHistoryFilterActive() const {
  return promptHistoryContext_ && promptHistoryContext_->kind == PromptKind::RecentFile &&
         (promptHistoryContext_->returnMode == ApplicationMode::Archive ||
          promptHistoryContext_->returnMode == ApplicationMode::ArchiveContent);
}

// プロンプトがアクティブな間に押されたK-49(^R): menu.cのm_list()
// から移植、それは純粋に「現在の」プロンプト自身の文脈から
// RingCategoryを選ぶ(`is_name(ap->name, M_man) -> T_hman`、等)――
// これが基づいている全体のソースの読み取りについては、なぜFILE/DIR
// (ToggleHistoryView、既に開いているリストの「トグル」)と、これ
// (初めてリストを開く、プロンプト時の「呼び出し」)が、下層で同じ
// ListBrowserのレンダリング/RingCategoryの仕組みを最終的に共有する
// にも関わらず異なる統合ポイントなのかを含め、クラス自身の調査
// メモ(CLAUDE.md項目161)参照。
// MAN/LOCATE/FGREP/EGREP(2026-08-04)にPROGRAM(2026-08-05、統合
// ポイント4――CLAUDE.md項目162自身の4ポイント計画参照): ProgramMode::
// run()は既に、実行された全ての呼び出しの「完全な」コマンド文字列
// (例えば"less /path/to/HELP")をRingCategory::ShellCommandへ積む
// ――do_prog()自身の`*histp = save_history(*histp, str, 1);`から
// 移植、これは、do_shell()がSHELLコマンドに使うのと「同じ」共有
// されたss.p.histのringへ記録する(mode.cの
// `do_prog(rvp->str, &hp->ss.p.cmd, &hp->ss.p.hist, 1);`の呼び出し
// 箇所参照)。ここで呼び戻されたエントリを選択すると自動的にそれを
// 「実行する」(SHELL自身の^Rとは異なりMAN/LOCATE/FGREPに合わせて
// いる――ユーザーによって直接確認済み、2026-08-05: "あくまで旧版に
// したがうと(B)"――なぜならPROGRAMの呼び出しは、SHELLの制限の無い
// コマンドラインとは異なり、既にConfiguration::programSeedCommands()
// の許可リストに制限されているため)。
// CutPattern(2026-08-05、上のバッチに残されていた唯一の意図的な
// 隙間を埋める): legacyはss.p.hegrepをCUTパターンに再利用する
// (menu.cの`case M_cut: case M_cut2: hp->ss.p.hegrep =
// save_history(...)`、上で引用したのと同じソースの読み取り)――
// 新しいringを発明するのではなく、ここではRingCategory::
// RecentEgrepPatternとして再利用され、よりきれいな設計を推測する
// のではなく、まさにその癖に合わせている。実際のユーザーの再現
// (2026-08-05、⑪付きのスクリーンショット)が、1回のCUT起動の呼び
// 出しで"man"/"manager"/"usilic"が混ざった11個のエントリを示していた
// ことは、これと一致する: "manager"は、ユーザーが以前実行した実際の
// EGREP検索であり、彼らのCUTパターン"man"/"usilic"とringを共有して
// いる可能性が非常に高い――これはこの関数自身の作業上の想定として
// ユーザーへ伝えてあり、ソースの読み取りとこの1枚のスクリーン
// ショットの状況証拠的な一致を超えて独立に確認されたものではない。
//
// その同じスクリーンショットは、この関数がもともと間違えていた
// 「2つ」のことも訂正した(同日の以前の改訂は、ここでソート済みで
// 重複排除されたリスト、name=カテゴリ、例えば"FGREP"を示していた):
//   - リスト自体は、(History::pushRecent()自身のring――項目161の
//     訂正――に合わせた)重複を含む生の時系列のログであり、
//     sortedUniqueRecentEntries()ではない。(通常表示する別個の
//     ソート済みringを本当に持ち、^Rでのみ時系列にトグルされる
//     ――ToggleHistoryView)FILE/DIR自身の^X^F/^X^Dとは異なり、これら
//     のプロンプトから起動されるカテゴリには、legacyにはそのような
//     別個のソート済みringがそもそも無い(このソースの読み取りが
//     元々この関数を生み出した際に確認済み: ss.p.hman/hlocate/
//     hfgrep/hegrepが、その4つについて存在する「全て」であり、hで
//     ない対応物は無い)――そのためここで「ソートして入る」べきもの
//     は何も無い;プロンプトで^Rを押すことは常に「生の履歴を見せて」
//     を意味する、それだけである。
//   - モードラインのnameフィールドは(カテゴリではなく)固定の
//     リテラル"HISTORY"である、mp[M_hist]==" HISTORY"が、現在の
//     画面をどのringが裏付けているかに関わらず、T_hfile/T_hdir/
//     T_hman/T_hlocate/T_hfgrep/T_hegrepにわたって一様に適用される
//     のに合わせている(この調査全体の早い段階で読んだmenu.cの
//     m_list()だが、今まで「この」関数自身のレンダリングの選択には
//     完全には結び付けられていなかった)――代わりにどのカテゴリで
//     あるかは、listBrowserView_自身のModeLineFieldsProviderが
//     promptHistoryRecallKind_を読むことで、別個の右寄せのバッジ
//     として表示される(それ自身のコメント参照)。
bool Application::startPromptHistoryRecall(bool directEntry) {
  RingCategory category;
  switch (promptKind_) {
    case PromptKind::ManName:
      category = RingCategory::RecentManPage;
      break;
    case PromptKind::LocateQuery:
      category = RingCategory::RecentLocateQuery;
      break;
    case PromptKind::GrepPattern:
      category = grepSyntaxRingCategory(grepSyntax_);
      break;
    case PromptKind::ProgramCommand:
      category = RingCategory::ShellCommand;
      break;
    case PromptKind::CutPattern:
      // executeCut()自身の2026-08-05の訂正コメント参照: FIXED(^Jc)
      // はFGREP自身のringを共有し、常にEGREPのものではない。
      category = (cutSyntax_ == GrepSyntax::Fixed) ? RingCategory::RecentFgrepPattern : RingCategory::RecentEgrepPattern;
      break;
    case PromptKind::RecentFile:
      category = RingCategory::RecentFile;
      break;
    case PromptKind::RecentDirectory:
      category = RingCategory::RecentDirectory;
      break;
    // 2026-09-18追加(フォローアップ8、直接のユーザー報告「SHELLでの
    // ^R（COMMAND）はまだNG」): ShellCommandプロンプト編集中(Tab補完で
    // 一度でも一覧を開いた後)の^Rがここに来るようになった――ProgramCommand
    // と全く同じRingCategory::ShellCommand(usilicrc自身の"COMMAND"、
    // ProgramCommand自身のコメント参照)を共有する。プレーンなSHELL
    // タイピング中(promptKind_がまだNoneのまま)の^Rは、この関数とは
    // 別の既存のstartShellHistoryRecall()が引き続き処理する(そちらは
    // 無変更、この修正の対象外)。
    case PromptKind::ShellCommand:
      category = RingCategory::ShellCommand;
      break;
    default:
      return false;
  }
  // 2026-09-18追加(フォローアップ8、PromptKind::ShellCommand自身の
  // 宣言コメント参照): SHELL宛先のShellCommandプロンプト編集中は
  // currentMode_が意図的にListBrowserのまま(フォローアップ7)なので、
  // それをそのままreturnModeとして捕まえると、この^R呼び戻し自体の
  // 戻り先が「ただのListBrowser」という具体性の無い値になってしまう
  // ――showTabCompletionList()自身の同種の修正と同じ理屈。
  ApplicationMode historyReturnMode =
      promptKind_ == PromptKind::ShellCommand ? ApplicationMode::Shell : currentMode_;
  // 2026-09-11追加、returnFromListBrowser()自身の
  // suspendedPromptHistoryContext_を使った復元ロジックのコメント参照:
  // ここに到達する時点でcurrentMode_が既にListBrowser(つまり`^X^F`/
  // `^X^D`自身の候補一覧を「閲覧中」に、この呼び出し――例えば
  // EGREPの直接入力の候補一覧――が起きた)なら、これから直後の行で
  // 上書きしてしまう前の`promptHistoryContext_`を退避しておく
  // ――ensureShellActive()が全く同じ状況(ListBrowserの最中に別の
  // セッションへ跳ぶ)に対して既に使っている、同じ
  // suspendedPromptHistoryContext_を再利用する。
  // 2026-09-11再訂正: `returnFromGrep()`自身の`grepReturnsToCandidateList_`
  // LIFO再入場(1階層ずつ候補一覧へ戻る既存の仕組み、そのコメント参照)
  // が、EGREP検索の確定後に`startRegexPromptList()`経由でこの関数を
  // 「再び」呼ぶ――その時点では(検索実行前に既にactivatePrompt()/
  // submitPrompt()を経てクリアされているため)`promptHistoryContext_`
  // は既に空になっている。無条件に退避すると、この2回目の呼び出しが
  // 「空」を退避して`suspendedPromptHistoryContext_`を上書きしてしまい、
  // 1回目の呼び出し(実際に退避すべきだったRecentFile/RecentDirectory
  // のコンテキスト)を破壊してしまっていた。`suspendedPromptHistoryContext_`
  // が「まだ何も保持していない」時だけ退避することで、この二重呼び出し
  // 越しでも最初の退避を保ち続ける。
  if (currentMode_ == ApplicationMode::ListBrowser && !suspendedPromptHistoryContext_ && promptHistoryContext_) {
    suspendedPromptHistoryContext_ = promptHistoryContext_;
  }
  promptHistoryContext_ = PromptHistoryContext{promptKind_, historyReturnMode, promptEditor_.text(), directEntry};
  promptHistoryContext_->epubOnly = epubListPending_;  // ^J^E(startEpubList()参照)
  epubListPending_ = false;
  promptKind_ = PromptKind::None;
  listBrowserCategory_ = category;
  // 2026-08-12修正: ContentView::setCursorAtModeLineTrailingEnd()
  // 自身のコメント参照。GrepPatternとCutPatternのringはどちらもこれ
  // (RecentFgrepPattern/RecentEgrepPattern)を共有する――直接入力
  // (^J^S/^J^C)経由で到達しようと、タイプ中の本物の^R呼び出し経由で
  // あろうと真である、なぜならどちらも同じ生きた編集可能なコンボ
  // ボックスだからである(feedRegexListByte()のディスパッチのチェック
  // は、directEntryではなくpromptHistoryRecallKind_の「値」だけに
  // 基づいている)。他の全てのカテゴリ(MAN/LOCATE/PROGRAM)はfalseの
  // ままである――そこではEnterは単に選択するだけであり、生きた編集
  // というパラダイムは無い。
  listBrowserView_.setCursorAtModeLineTrailingEnd(category == RingCategory::RecentFgrepPattern ||
                                                   category == RingCategory::RecentEgrepPattern);
  // ^J^S/^J^C自身の「直接入力」の候補リスト(2026-08-12修正、
  // directEntryに狭く限定してある――なぜかについては下記参照): 他の
  // 全てのプロンプトから起動される呼び出しが使う生の時系列のログ
  // ではなく、ソート済み+重複排除されている(まさに、この関数の上の
  // 大きなコメントが「これへ」訂正されたと文書化している方針、逆方向
  // に間違えていた同日の以前の改訂から)。実際のユーザー依頼:
  // "GREPの文字列一覧だが、同ファイルを読み込んだ後もソートして
  // ほしい...のちのち文字列数が多くなってから起きる重複等回避する
  // ためにもやはりソート後重複削除は必要"。本物の^R呼び出し
  // (directEntry==false、例えばパターンをタイプ中に^Rを押す)には
  // 意図的に適用されない: そのケースは、legacy自身の生の、重複を
  // 含む呼び出しリストの実際のスクリーンショット(⑪)に裏付けられ、
  // まさに上のコメントが既に決着させたものである――この修正は、
  // タイプが始まる「前」に開く別個の直接入力の候補リストだけに関係
  // する(legacy自身の参照されたスクリーンショット㊳/㊴が「その」
  // リストがソートされていることを示すのに合わせている)。
  // RecentFile/RecentDirectory(2026-08-17追加、K-46/K-48)は「常に」
  // directEntry経由で到達する(この2つには別個の「タイプ中の本物の
  // ^R呼び出し」という入り口は無い――startListBrowser()が唯一の呼び
  // 出し元である)、そしてこのPromptKindベースの書き直し全体の前
  // でさえ既にソート済み+重複排除されて表示されていた(これが置き
  // 換える、プレーンなListBrowserModeの経路は、
  // sortedUniqueRecentEntries()を直接呼んでいた)ので、ここでその
  // 同じ順序を保つことは、新しい挙動の変更ではなく、そのまま持ち
  // 越すだけである。
  // 2026-09-11修正、直接のユーザー依頼(startGrepPrompt()自身の
  // コメント参照、"FGREPおよびEGREPを同じ仕様に統一...候補一覧は
  // 廃棄"): 以前はGrepPattern/CutPatternについてもRegex構文の場合
  // だけこのdirectEntryの直接入力候補リストへ開いていたが
  // (startRegexPromptList()自身の、EGREP/CUT-Regexを直接候補リストへ
  // 開いていた古い呼び出し元参照)、その候補一覧自体が廃止された今、
  // GrepPattern/CutPatternがdirectEntry==trueでここに来ることは無く
  // なった――RecentFgrepPattern/RecentEgrepPatternをこの判定に含める
  // 意味も無くなったので外した(この2つのringが指す実際の履歴表示は、
  // 引き続き下のchronological分岐(タイプ中の本物の^R呼び出し)経由で
  // 提供される)。
  // 2026-09-20追加、直接のユーザー依頼(^Rの再設計、"^J^Rを^J^Pに置き
  // 換える(^J^Rは廃棄)"): RingCategory::ShellCommand(PromptKind::
  // ProgramCommand経由でしかここへ来ない――このswitch自身の対応する
  // caseコメント参照)を追加した。従来^J^Rが単独で担っていた「ソート
  // 済み+重複排除された一覧」(旧ラベル"COMMAND"、ringCategoryLabel()
  // 自身の改名コメント参照)は、以後^J^P(このカテゴリの唯一の
  // directEntry入口)の初期表示として統合される。
  bool sortDirectEntryList = directEntry && (category == RingCategory::RecentFile ||
                                              category == RingCategory::RecentDirectory ||
                                              category == RingCategory::ShellCommand ||
                                              category == RingCategory::RecentManPage);
  // 2026-09-27、直接のユーザー依頼("EGREPの一覧を復活して欲しい。
  // usilicrcのGREP（←「EGREP」に変更よろしく）に載せてくれた実例が履歴に
  // 埋もれるのは惜しい"): 以前(2026-09-25)はusilicrcのEGREP節の実例を
  // 時系列の履歴の古い側(先頭)へ混ぜていたため、検索するほど実例が
  // 履歴の奥へ埋もれていった。EGREP/CUT(正規表現)の^Rは、実例がある
  // 限り、まず「EGREPの一覧」(egrepListItems()、実例をusilicrcの並び
  // どおり先頭に)を開く。もう一度^Rで純粋な時系列の履歴(HISTORY)。
  bool egrepList = category == RingCategory::RecentEgrepPattern && !configuration_.grepPresets().empty();
  listBrowserChronological_ = !sortDirectEntryList && !egrepList;
  std::vector<std::string> items;
  if (egrepList) {
    items = egrepListItems();
  } else if (sortDirectEntryList) {
    items = sortedUniqueRecentEntries(category);
  } else {
    const std::deque<std::string> &recent = history_.recentEntries(category);
    items.assign(recent.begin(), recent.end());
  }
  // archiveHistoryFilterActive()自身のコメント参照。
  // promptHistoryRecallKind_/promptHistoryRecallReturnMode_はどちらも
  // 上で既に設定されているので、`category`だけでは通常の^X^Fから
  // 「Archiveからの^R」を区別できない(どちらもRingCategory::
  // RecentFileを再利用する)にも関わらず、これは正しく読める。
  if (archiveHistoryFilterActive()) {
    items.erase(std::remove_if(items.begin(), items.end(),
                                [](const std::string &item) { return !hasArchiveCompatibleSuffix(item); }),
                items.end());
  }
  if (promptHistoryContext_ && promptHistoryContext_->epubOnly)  // ^J^E: EPUBだけ(startEpubList()参照)
    items.erase(std::remove_if(items.begin(), items.end(), [](const std::string &item) { return !isEpubFile(item); }),
                items.end());
  listBrowserMode_.show(items, fileHistoryBadges(configuration_, category, items));
  currentMode_ = ApplicationMode::ListBrowser;
  if (!items.empty()) {
    if (egrepList) {
      // 2026-09-27、直接のユーザー報告("EGREP一覧のカーソル位置が保存され
      // ない"): 先頭ではなく、最後に使った正規表現(履歴の最新、~/.usilic/
      // historyに残るので次回の起動後も同じ)の行に置く。
      listBrowserView_.setCursorIndex(egrepListCursorIndex(items));
    } else if (directEntry) {
      // 2026-08-11: legacyのmode.cは、本物の^R呼び出し(T_hfgrep/
      // T_hegrep、`row = nrow - 1`)と直接入力(M_fgrep/M_locate/
      // M_cut2、`i = -2`――明示的な行は無く、先頭にフォールバックする)
      // を区別している――promptHistoryRecallIsDirectEntry_自身のコメント
      // 参照。実際の報告により、これが最初のシードパターンではなく
      // 「最後の」(11個中11番目の)ものに着地することが判明した。
      listBrowserView_.setCursorIndex(0);
    } else {
      // 最新(最も最近積まれたエントリ、末尾にある)が、実際に最初に
      // 見える/選択されるものである――ToggleHistoryView自身の時系列
      // モードが使うのと同じ「最下行が最新」という慣習、そしてlegacy
      // 自身のmode.c(M_hist/T_histのグループ:
      // `pnp->row = hp->bp->nrow - 1;`)に合わせている。
      listBrowserView_.setCursorIndex(static_cast<int>(items.size()) - 1);
    }
    listBrowserView_.redraw();
  }
  // 意図的に`!items.empty()`のブロックの外にある: RecentFile/
  // RecentDirectoryは、フィールドに種を蒔く候補がある時だけでなく、
  // 本当に空のring(以前に一度もファイル/ディレクトリが訪れられて
  // いない)であっても、それら自身のdrawPromptLabel()スタイルの
  // モードライン(refreshDirectEntryModeLine()自身のコメント参照)を
  // 必要とする。
  refreshDirectEntryModeLine();
  return true;
}

bool Application::startShellHistoryRecall() {
  if (currentMode_ != ApplicationMode::Shell) return false;
  shellHistoryRecallActive_ = true;
  shellHistoryRecallText_ = shell_.commandLine().text();
  listBrowserCategory_ = RingCategory::ShellCommand;
  // 2026-09-20修正、直接のユーザー報告(^Rの再設計、"SHELLプロンプトの
  // 最初の^Rでは履歴一覧が自然ではないかと"): 以前はここでソート済み+
  // 重複排除された一覧(旧ラベル"COMMAND"、今は^J^P/PromptKind::
  // ProgramCommand経由でのみ到達するビュー、ringCategoryLabel()自身の
  // 改名コメント参照)を最初に見せ、2回目の^R(toggleListBrowserHistory()
  // 経由)で初めて生の時系列(HISTORY)へ切り替えていたが、SHELLの
  // プロンプトで打った^Rが最初から欲しいのは「直近何を打ったか」の
  // 生の履歴であり、ソート済み一意リストではないというのが直接の
  // ユーザー判断――^J^Pの方はソート済みビューの入口として引き続き
  // 機能する(sortDirectEntryList自身の同日の拡張コメント参照)ので、
  // ここを変えても「ソート済み一覧を見る手段が無くなる」ことはない。
  listBrowserChronological_ = true;
  // ContentView::setCursorAtModeLineTrailingEnd()自身のコメント参照
  // ――ここではEnterは単に選択をシェルのコマンドラインへ「挿入する」
  // だけであり(openSelectedListEntry()自身のshellHistoryRecallActive_
  // の分岐参照)、ハイライトされた行を生きたまま編集するというパラダ
  // イムではないので、カレットは他のどんなプレーンなピッカーとも
  // 同様、ハイライトされている行に属する。
  listBrowserView_.setCursorAtModeLineTrailingEnd(false);
  const std::deque<std::string> &recent = history_.recentEntries(RingCategory::ShellCommand);
  std::vector<std::string> items(recent.begin(), recent.end());
  listBrowserMode_.show(items);
  currentMode_ = ApplicationMode::ListBrowser;
  // toggleListBrowserHistory()自身の同種の処理(History::recentEntries()
  // は最も古いものから返すので、最後の要素が最新――時系列モードでは
  // それを最初に見せる、"最下行が最新"というユーザー自身の説明済みの
  // 慣習に合わせる)。
  if (!items.empty()) {
    listBrowserView_.setCursorIndex(static_cast<int>(items.size()) - 1);
    listBrowserView_.redraw();
  }
  return true;
}

// showTabCompletionList()自身のapplication.hpp宣言コメント参照。
bool Application::showTabCompletionList(const std::vector<std::string> &candidates, const std::string &partial,
                                          const std::string &prefixBeforeWord) {
  // 2026-09-18追加(フォローアップ7、実機報告「直ってない」を受けての
  // さらなる修正): PromptKind::ShellCommand自身の宣言コメント参照
  // ――このコミット以降、SHELLへ戻る編集中はcurrentMode_をShellへ
  // 戻さず(意図的にListBrowserのまま)保つため、編集中にもう一度Tabを
  // 押して2回目の一覧を開く(この関数のこの呼び出し自体)時点では
  // currentMode_はShellではなくListBrowserになっている。ここで単純に
  // currentMode_を戻り先として採用すると、2回目以降の一覧が
  // returnMode=ListBrowserという誤った値を記憶してしまう――
  // promptKind_==ShellCommandである間は、戻り先は常にShellである
  // (この関係はPromptKind::ShellCommand自身の設計上、他の値を取り
  // 得ない)ので、そちらを優先する。
  ApplicationMode returnMode = promptKind_ == PromptKind::ShellCommand ? ApplicationMode::Shell : currentMode_;
  // 2026-09-18追加(フォローアップ8): kindは「一覧を確定/編集した後、
  // どのPromptKindへ戻るか」――SHELL宛先ならShellCommand(promptKind_が
  // 既にShellCommandならそのまま、まだNoneのままの初回ならこの時点で
  // 確定する)、それ以外は既存のpromptKind_をそのまま引き継ぐ(元々
  // ProgramCommand/ManName/PathInput/ArchiveDestination/RecentFile/
  // RecentDirectoryのどれであっても、その値は編集中ずっと変わらない)。
  PromptKind kind = returnMode == ApplicationMode::Shell ? PromptKind::ShellCommand : promptKind_;
  tabCompletionContext_ = TabCompletionContext{returnMode, kind, partial, prefixBeforeWord};
  listBrowserView_.setCursorAtModeLineTrailingEnd(false);
  listBrowserMode_.show(candidates);
  currentMode_ = ApplicationMode::ListBrowser;
  activeView().redraw();
  // 2026-09-18追加、実機報告(②「^I直後のプロンプトはハイライトの
  // まま」): RecentFile/RecentDirectory自身の前例は、最初の1枚目は
  // (listBrowserView_自身のModeLineFieldsProviderが描く)ハイライト
  // されたtrailingのままで、実際にMoveUp/MoveDownした後にだけ
  // refreshDirectEntryModeLine()が非ハイライトのプレビューへ切り替える
  // ――しかしこの機能については、その最初の1枚目からして「編集可能な
  // フィールド」に見えるべきだとユーザーから直接指摘された。ここで
  // 即座にrefreshDirectEntryModeLine()自身と同じ内容を呼ぶことで、
  // 一度も動かしていない状態でも最初から非ハイライトのプレビューに
  // なるようにする。
  refreshDirectEntryModeLine();
  return true;
}

// returnFromTabCompletionList()自身のapplication.hpp宣言コメント参照。
bool Application::returnFromTabCompletionList(bool selected) {
  if (!tabCompletionContext_) return false;
  ApplicationMode returnMode = tabCompletionContext_->returnMode;
  PromptKind kind = tabCompletionContext_->kind;
  std::string prefixBeforeWord = tabCompletionContext_->prefixBeforeWord;
  std::string partial = tabCompletionContext_->partial;
  tabCompletionContext_ = std::nullopt;
  currentMode_ = returnMode;
  // 2026-09-18追加(フォローアップ7): returnMode==Shellの場合、
  // promptKind_はShellCommand(編集を一度でも開始していれば)かNone
  // (一度も編集を開始せずここへ来た、通常の初回の一覧確定)のどちらか
  // ――どちらであれ、Enter/^Gで一覧そのものを確定/キャンセルする時点で
  // は「もうShellCommandプロンプトの中にいる」状態ではなく「生きた
  // SHELLコマンドラインへ戻る」状態なので、promptKind_を明示的にNone
  // へ戻す。これが無いと、ShellCommandプロンプトの中でもう一度Tabを
  // 押して開いた「入れ子の」一覧をEnterで確定した際、promptKind_が
  // ShellCommandのまま残ってしまい、この後のバイトがdispatchByte()の
  // `promptKind_ != None`チェックに捕まって(本来はfeedShellByte()へ
  // 届くべきところ)誤ってfeedPromptByte()へ流れ続けてしまう。
  if (returnMode == ApplicationMode::Shell) promptKind_ = PromptKind::None;
  // 2026-09-18修正、実機報告の追跡調査で判明した回帰(選択時に
  // "fake-onetwo"のような二重連結になる): 以前は「残りの接尾辞だけ
  // 追記する」流儀(既存のLCP補完と同じ)だったが、これは
  // showTabCompletionList()自身の②修正(一覧を開いた直後から
  // refreshDirectEntryModeLine()相当のプレビューを描く)と両立
  // しない――カーソル移動のたびに、下敷きのエディタ自身の文字列が
  // 「現在ハイライト中の候補の全文」で上書きされてしまうため、選択
  // 確定時点でもう「partialのままの元のテキスト」ではなくなっている。
  // prefixBeforeWord+選択項目(または非選択時はpartialへ戻す=一覧を
  // 開く前の状態への復元)で毎回「完全な行」を再構成し、置換
  // (setText())する形に改めた――feedTabCompletionListByte()自身の
  // 「編集を開始する」遷移と同じ考え方に揃えている。
  const std::string *item = selected ? listBrowserMode_.selectedItem() : nullptr;
  std::string full = prefixBeforeWord + (item ? *item : partial);
  // 2026-09-19追加(フォローアップ12、直接のユーザー要望:「せっかく
  // 完全な形で補完できているのだから、あとはそれを先方に渡さないと。
  // 今はそれだけで終わってる」): RecentFile/RecentDirectoryは単語の
  // 途中を補完するSHELL/PROGRAM/MANとは違い、フィールド「全体」を
  // 1つの完結した値で置き換えるkindである――一覧から候補を選んだ
  // 時点でその値は既に確定済みであり、続けて別の引数をタイプする
  // 余地が無い。選ばれた(`selected`)場合はfeedRegexListByte()自身の
  // 候補選択と同じ意味(「戻ってさらに編集」ではなく「この値で確定・
  // 投入」)として扱い、executePromptSubmission()(→executePathInput()
  // でファイルを開く/ディレクトリへ移動する)まで直接進める――
  // submitPrompt()自身のpromptKind_クリアと同じ作法に揃える。^Gキャン
  // セル(selected==false)はこれまで通り編集可能な状態へ戻すだけ。
  // 2026-09-19再訂正(フォローアップ13、直接のユーザー報告+正確な
  // 再現手順: "usilic ~/git/usilic -c^M→(ls)→^Xf^I^I^M（.DS_Storeを
  // 選択）→(ls)のまま"): feedPromptByteInner()自身の同種の修正コメント
  // 参照――「^Xf」は小文字であり"^X f"(PathInput、K-65、
  // Command::StartFileInput)のことだった。フォローアップ12は
  // "^X^F"(RecentFile)との書き間違いと誤読し、PathInput/ArchiveDestination
  // を意図的に除外していたが、実際の一連の報告全体がPathInputを対象に
  // していたと判明したため、この2つも含める。
  bool wholeFieldKind = kind == PromptKind::RecentFile || kind == PromptKind::RecentDirectory ||
                        kind == PromptKind::PathInput || kind == PromptKind::ArchiveDestination;
  if (selected && item && wholeFieldKind) {
    promptKind_ = PromptKind::None;
    return executePromptSubmission(kind, full);
  }
  if (returnMode == ApplicationMode::Shell) shell_.commandLine().setText(full);
  else promptEditor_.setText(full);
  if (returnMode == ApplicationMode::Shell) {
    shell_.scrollback().redraw();
    shell_.redrawPromptRow();
  } else {
    activeView().redraw();
  }
  return true;
}

// feedTabCompletionListByte()自身のapplication.hpp宣言コメント参照。
// feedRegexListByte()と全く同じ「ナビゲートのみの小さな語彙、それ
// 以外の全バイトは編集開始」という構造の複製――違いは戻り先が
// promptHistoryContext_(常にPromptKindを持つ)ではなく
// tabCompletionContext_(SHELLの生きたコマンドラインという
// PromptKindを持たない戻り先もあり得る)である点。
bool Application::feedTabCompletionListByte(int byte, std::optional<KeyInput::SpecialKey> special) {
  if (!tabCompletionContext_) return false;
  // 2026-09-18追加、実機報告(④「^[vはOKだが^Vが利かないのでページ
  // 送りできない」): ^[v(ESC v)は既にescPending_経由でdispatchLeaderByte()
  // へ普通に流れる(下のリーダー分岐)ので最初から動いていたが、素の
  // 制御バイト^V(0x16、resolveCommand()自身のPageDownへのマッピング
  // 参照)はnav語彙に含まれておらず、代わりに「編集を開始する」遷移へ
  // 誤って落ちていた。
  // (2026-09-18訂正、直接のユーザー指摘: 「^B/^Fは文字列編集用だから
  // ダメだって。^Vだけでよろしく」)――^B(0x02)/^F(0x06)は
  // feedPromptByte()/feedShellByte()自身の2026-08-16の修正(それぞれの
  // 呼び出し箇所参照)で既に「編集中の行内でのカーソル左右移動」に
  // 割り当て済みの制御バイトであり、このLIST画面でだけnavの意味に
  // 割り込ませると、後述の「編集を開始する」遷移後にまさにその
  // カーソル移動が使えなくなる(遷移前のこの分岐に先に捕まってしまう
  // ため)。^N/^P/^Gと違い、^B/^Fは「編集中の文字列そのものを操作する」
  // 目的で押される現実的な可能性があるので、nav語彙には加えない――
  // ^V(PageDown)だけを追加する。
  bool isNav = special == KeyInput::SpecialKey::Up || special == KeyInput::SpecialKey::Down ||
               special == KeyInput::SpecialKey::PageUp || special == KeyInput::SpecialKey::PageDown;
  if (!special) {
    switch (byte) {
      case 0x0e:  // ^N
      case 0x10:  // ^P
      case kCarriageReturn:
      case kControlG:
      case 0x0c:  // ^L
      case 0x16:  // ^V(PageDown)
        isNav = true;
        break;
    }
  }
  if (isNav) return handleCommand(resolveCommand(byte, special));
  // feedRegexListByte()自身の同じ理由のコメント参照: リーダーの
  // シーケンス(^J/^X/^O/ESC)は「編集を開始する」ではなく、通常通り
  // コマンドとしてディスパッチする。
  if (escPending_ || leaderJPending_ || leaderJOPending_ || leaderXPending_ || leaderOPending_ ||
      gotoLineGPending_ || gotoLineDigitsPending_ ||
      (!special && (byte == kLeaderJ || byte == kLeaderX || byte == kLeaderO || byte == kEscape))) {
    return dispatchLeaderByte(byte, special);
  }
  if (special) return false;  // 処理されない特殊キー――テキストとして送り込む意味のあるものが無い

  // ここから先は「編集を開始する」――実機報告(③「^Nで非ハイライトに
  // はなるものの編集不能。'#q.txt#'で^Hとやると'#q.txt'とはならずに
  // HELPになる」)への対応。prefixBeforeWord(補完対象語の前にあった
  // テキスト、例えば"cat ")+現在ハイライト中の候補で完全な行を
  // 再構成し、通常の生きた編集可能なプロンプト(activatePrompt())へ
  // 遷移してから、このバイト自身を通常の経路(feedPromptByte())へ
  // 再送りする――feedRegexListByte()と同じ「遷移が連続しているように
  // 感じられる」ための再送り。
  //
  // 2026-09-18再設計(フォローアップ5、直接のユーザー提案:「SHELL自体
  // をスタックへ積んだ上でLISTルーチンへ移行し、文字列編集はLIST内で
  // 行った上で(当然一覧も無傷)編集が完了した時点でスタックからSHELLを
  // 戻して文字列を渡す」): 以前はSHELL宛先だけshell_.commandLine()/
  // shell_.redrawPromptRow()を直接操作し、バイトをfeedShellByte()へ
  // 再送りしていた――これはSHELLだけが他のPromptKind(GrepPattern/
  // CutPattern/ManName/RecentFile/RecentDirectory等)と違って専用の
  // PromptKindを持たず、編集対象が別個のウィジェット(shell_.commandLine()
  // 、独自のcommandLineRow())だったための特別扱いであり、行の所有権が
  // 一覧の内容行と2系統に分かれ、実機報告のたびに(パス補完/ソート/
  // 生きた編集/ページ送り/戻り値バッジ/一覧の消失、と5ラウンド連続で)
  // 取りこぼしが起きていた。PromptKind::ShellCommand(自身の宣言コメント
  // 参照)を新設し、SHELL宛先の場合もPROGRAM/MAN等と全く同じ
  // activatePrompt()+promptEditor_の経路に統合した――これにより
  // shell_.commandLine()自体は、確定(Enter、executePromptSubmission()
  // 自身のShellCommand分岐)またはキャンセル(^G、cancelPrompt()自身の
  // ShellCommand分岐)まで一切触れられなくなり、編集中は他の全ての
  // PromptKindと全く同じく「activatePrompt()はモードライン1行だけを
  // 書く」という既存の保証だけで一覧が無傷のまま保たれる。
  ApplicationMode returnMode = tabCompletionContext_->returnMode;
  std::string prefixBeforeWord = tabCompletionContext_->prefixBeforeWord;
  const std::string *selected = listBrowserMode_.selectedItem();
  std::string seed = prefixBeforeWord + (selected ? *selected : tabCompletionContext_->partial);
  listBrowserView_.redraw();
  // 2026-09-18再訂正(フォローアップ7、実機報告「直ってない」): 前回
  // (コミット0628934)はcurrentMode_をここで無条件にreturnModeへ戻して
  // いたため、SHELL宛先の場合はcurrentMode_が(実際にはまだ何も送信
  // されていない、単なる編集中の下書きに過ぎないのに)Shellへ戻って
  // しまい、main.cpp自身のwatchShellPty以外にも「currentMode()==Shell
  // なら本物のShellセッションとして扱ってよい」と仮定している箇所が
  // 他にも存在する可能性を捨て切れなかった(実際に直っていなかった)。
  // 直接のユーザー提案(「SHELL自体をスタックへ積んだ上でLISTルーチンへ
  // 移行し...編集が完了した時点でスタックからSHELLを戻す」)により忠実
  // に倣い、SHELL宛先の間はcurrentMode_を意図的にListBrowserのまま
  // 保つ(=画面に実際に映っているものと矛盾しない状態を保つ)ことにした
  // ――本当にcurrentMode_をShellへ戻すのは、編集が確定/中断して初めて
  // (executePromptSubmission()/cancelPrompt()自身のShellCommand分岐)。
  // ProgramCommand等(SHELL以外の宛先)は元々何の問題も無かった(実機
  // 報告は全てSHELL起点)ので、退行を避けるため従来通りcurrentMode_を
  // 即座にreturnModeへ切り替える。
  //
  // feedRegexListByte()自身の2026-09-11修正と同じ理由でtabCompletionContext_の
  // クリアをactivatePrompt()の「後」まで遅らせる(直前のlistBrowserView_.
  // redraw()自身のModeLineFieldsProviderが読む、閲覧中と同じラベルとの
  // 整合のため――フォローアップ8、refreshDirectEntryModeLine()自身の
  // 同時の修正参照)。kindはtabCompletionContext_->kind自身をそのまま
  // 使う(showTabCompletionList()が既に同じロジックで確定済み――二重に
  // 計算し直さない)。
  PromptKind kind = tabCompletionContext_->kind;
  // 2026-09-19追加(フォローアップ9、直接のユーザー報告「^Xf→^I→FILEの
  // まま行数現る→^B→行数消えてプロンプトがまんま移動」+お手本の提示
  // 「^X^F^R→FILE→HISTORY→^B→HISTORYのまま」): feedRegexListByte()
  // 自身の「編集を開始する」遷移は、閲覧中ずっと見えていた位置表示
  // ("01/10 TOP")を、編集開始後もactivatePrompt()の第3引数として
  // そのまま引き継ぐ(feedRegexListByte()自身の2026-08-xxの確立済みの
  // 修正――「[bs]→「1/10 TOP」が消滅」の反省)――この関数はそれを
  // コピーし忘れていたため、編集開始の瞬間に位置表示が消え、
  // それに伴いdrawPromptLabel()自身のlabelWidth計算(position表示の
  // 有無で編集可能フィールドの開始列が変わる)が変わってしまい、
  // プロンプト自体が横にずれて見えていた(「プロンプトがまんま移動」)。
  std::string positionText;
  int candidateCount = static_cast<int>(listBrowserMode_.items().size());
  if (candidateCount > 0) positionText = ModeLine::format(listBrowserView_.cursorIndex() + 1, candidateCount);
  if (returnMode != ApplicationMode::Shell) currentMode_ = returnMode;
  activatePrompt(kind, seed, positionText);
  tabCompletionContext_ = std::nullopt;
  return feedPromptByte(byte);
}

std::string Application::promptLabelText(PromptKind kind) const {
  switch (kind) {
    case PromptKind::GrepPattern:
      return grepSyntaxLabel(grepSyntax_);
    case PromptKind::CutPattern:
      return "CUT";
    case PromptKind::ManName:
      return "MAN";
    case PromptKind::ProgramCommand:
      return "PROGRAM";
    case PromptKind::LocateQuery:
      return "LOCATE";
    case PromptKind::CopyDestination:
      return "CP";
    case PromptKind::MoveDestination:
      return "MV";
    case PromptKind::RemoveConfirm:
      return "RM";
    case PromptKind::ExitConfirm:
      return "EXIT (y/n)?";
    case PromptKind::PathInput:
      return pathInputIsDirectory_ ? "DIR" : "FILE";
    case PromptKind::ArchiveDestination:
      return "ARC";
    case PromptKind::RecentFile:
      return "FILE";
    case PromptKind::RecentDirectory:
      return "DIR";
    case PromptKind::Du:
      return "DU";
    case PromptKind::ArchiveLoad:
      return "ARC";
    case PromptKind::EpubLoad:
      return "EPUB";
    case PromptKind::TabCompletionList:
      return "LIST";
    case PromptKind::ShellCommand:
      return "SHELL";
    case PromptKind::None:
      return "";
  }
  return "";
}

// showShellBusyNotice()/refuseIfShellBusy()自身の宣言コメント
// (application.hpp)参照。legacy自身の実機再現("SHELL busy"、
// ^G/^J.のPTYキャプチャ、太字プレーンテキスト+bell、反転表示の
// モードライン自体は起こさない)。
// 2026-09-06追記: かつてはこれと対をなす、"(stdin)"パイプが末尾に
// 追いつけなかった時に同じ描画パターンで出すshowPagerExitNotice()
// もあったが、直接のユーザー報告("バイト単位読み込み仕様となった
// STDINではEOFと（Exit）がほぼ同時となり無意味なので(stdin)では
// 通知を無くしてほしい")を受けて削除した――ensureLoadedThrough()の
// ペーシングが「あと1行」単位まで絞られた結果、legacy自身のような
// 「まだ本当のEOFではないのに先読みマージンだけ枯渇している」区間が
// 事実上消え、この通知が意味を持つ場面自体がほぼ無くなっていた
// (pollPagerExitNotice()自身のコメント参照)。
void Application::showShellBusyNotice() {
  int row = std::max(0, display_.rows() - 1);
  display_.moveCursor(row, 0);
  display_.putChar(0x07);
  display_.write(" (");
  display_.setAttribute(Display::Attribute::Bold);
  display_.write("SHELL busy");
  display_.setAttribute(Display::Attribute::BoldOff);
  display_.write(") ");
  display_.flush();
}

// 2026-09-25追加、直接のユーザー要望("SHELLプロンプトが戻っていない
// 状態でCP/MV/RMを実行しようとすると自動的にSHELL画面へ切り替わる
// (戻っていない理由確認のため)"): CP/MV/RMは実際にはSHELLへコマンドを
// 送り込んで実行する(startCpMvRmShellRun())ので、前のコマンドがまだ
// 終わっていないSHELLへは送れない。宛先の入力/確認へ進む前にSHELL画面
// へ切り替えて、何が終わっていないのかを見せる。選択(マーク)はその
// まま残るので、^Gで戻ればやり直せる。
bool Application::switchToShellIfBusy() {
  if (!shellStarted_ || !shell_.busy()) return false;
  return ensureShellActive();
}

bool Application::refuseIfShellBusy() {
  if (!shell_.busy()) return false;
  showShellBusyNotice();
  return true;
}

void Application::ensurePagerStreamLoaded(int targetLineIndex, bool bypassPacing) {
  // 2026-09-20追加、直接のユーザー報告("man ls直後の画面が出ない
  // （jで表示）"): FileMode::ensureLoadedThrough()自身は、新しく
  // 読み込んだ内容をview_へ反映する(setLines())だけで、実際の画面の
  // 再描画は一切行わない――これは元々、この関数がhandleCommand()の
  // switch(command)自身の直前でだけ呼ばれ、その直後に各コマンドの
  // ケース自身(moveDown()/pageDown()等)が必ず自前でredraw()する、
  // という前提に立っていたため、それ自体は問題にならなかった。しかし
  // このメソッド自体は今やpollPagerExitNotice()経由でメインループの
  // 毎回の反復からも(コマンドの発行と無関係に)呼ばれるので、その
  // 経路には「この直後に誰かがredraw()する」という前提がそもそも
  // 成り立たない――結果、SHELLからのPAGERハンドオフ直後、たまたま
  // 子プロセスがまだ何も書き込んでいない一瞬(Application::pollShellHandoff()
  // 自身のloadStream()呼び出し時点)に当たると、内容は(このポーリング
  // 経由で)裏側では正しく読み込まれ続けるのに、画面には一切反映され
  // ないまま――ユーザーが偶然何かキー(例えば'j')を押して「その」
  // コマンド自身のredraw()が初めて拾うまで、ずっと空白の"00000/00000
  // END"のまま固まって見えていた。ensureLoadedThrough()が実際に
  // 新しい内容を読み込めた(true)場合はここで明示的にredraw()+flush()
  // する――handleCommand()自身の呼び出し元から見れば、直後の
  // switch(command)自身のredraw()と二重になるが、無害である。
  if (fileMode_.ensureLoadedThrough(targetLineIndex, bypassPacing)) {
    activeView().redraw();
    display_.flush();
  }
}

// メインループ(main.cpp)から、他の全てのpoll*()系メソッド
// (pollShellHandoff()等)と並んで、CUI/GUIの両方の反復ごとに無条件で
// 呼ばれる。
//
// 2026-09-20再訂正、直接のユーザー報告2件("（Exit）は出るようだが
// すぐ消える（上書き？）"、"man ls直後の画面が出ない（jで表示）")。
// 前者の原因: このメソッドが「あらゆる」反復で無条件に
// ensurePagerStreamLoaded()を呼び続けていたため、legacy自身の実機
// (ユーザーが自分で次にspaceを押すまでは何もしない、待ち合わせは
// 「ユーザー自身の次の操作」を単位にしている)とは異なり、この
// 移植は裏側で継続的に読み直しを試み続けていた――man/groffのような
// 実際に「まだ書きかけ」のパイプでは、ユーザーが(Exit)通知に気付く
// より前に、その裏側の再試行がたまたま新しい内容を見つけてしまい、
// ensurePagerStreamLoaded()自身の新しいredraw()(すぐ下のコメント
// 参照、これ自体は別件の直接のユーザー報告への対応)がそれを即座に
// 描き直し、出したばかりの(Exit)通知を一瞬で上書きしていた。
//
// 2026-09-05再々訂正、直接のユーザー報告(スクリーンショット"=fp"、
// "-g起動ではここで止まった"): 上の再訂正時点では「まだ画面に何も
// 表示されていない(1画面分未満)」場合だけ自動再読み込みし、それ
// 以降は「実際のユーザーの操作(handleCommand()自身の
// ensurePagerStreamLoaded()呼び出し)だけが続きを読み込むトリガー
// になる」としていたが、これはCUIのキー操作が必ずhandleCommand()を
// 経由することが前提――GUIのマウスホイール/慣性スクロール
// (main.cpp、ContentView::setTopIndex()/stepTopIndex()を直接操作、
// handleCommand()を一切経由しない)にはこの前提が成り立たず、結果
// 最初の1画面分を読み終えた後は、ユーザーがどれだけホイールで
// スクロールし続けても続きが一切読み込まれない――ちょうど
// スクリーンショットの通り、ある行で完全に止まったまま動かなく
// なる(通知も出ない、ensureLoadedThrough()自体が二度と呼ばれない
// ので当然)。
// 「ユーザー自身の次の操作を待つ」という元の設計意図を保ちつつ
// GUIもCUIも同じ経路でカバーするため、判定を「コマンド経由か」
// ではなく「ビューアの表示位置(topIndex)が前回この場所を試みた
// 時から実際に動いたか」というエッジトリガーに一般化する――
// キー操作でもホイールでもスクロールバードラッグでも、topIndex_が
// 動けば(=ユーザーが何かした証拠があれば)それだけを合図に1回だけ
// 試みる。ユーザーが(Exit)通知を読むためにスクロールの手を止めて
// じっとしている間はtopIndex_も動かないので再試行は起きず、通知は
// 消えない(元の再訂正が防ごうとした「すぐ消える」問題はそのまま
// 保たれる)。
// 「最初の1画面分にまだ満たない」(lines().size() < rows())の
// 判定は引き続き別枠で残す――起動直後はtopIndex_が0のまま動かない
// (エッジトリガーの対象外)にも関わらず、子プロセスからの最初の
// 読み込みがまだ書きかけの場合があるため、legacy自身の実機が最初
// から画面いっぱいの初期ページを示すのに合わせて、少なくとも最初の
// 1画面分は無条件に埋めきる。
//
// 2026-09-06さらに訂正(6回目、直接のユーザー報告
// "`man ls|usilic -c^M`→「00001/00050」"「`PAGER='usilic -c' man ls^M`
// →「00001/00024」で`j`→「00024/00050」」「SHELLプロンプトで
// `man ls^M`後の最初の画面が瞬時に出ない」を受け、legacy自身の実機
// `U`で`man ls`を改めて逐一比較): 「常にあと1行」という前回までの
// 一般化は誤りだった――legacy実機は、まだ画面が一度も埋まっていない
// 「最初の1画面」は0.3秒足らずでほぼ瞬時に埋め切り(`man ls\r`直後を
// 0.1秒刻みでサンプリングして確認)、その後の通常のスクロール(`j`
// 連打)でだけ先読み分がちょうど1行ずつしか伸びない。stillCatchingUp
// FirstScreenの間はensureLoadedThrough()自身のペーシングをbypassPacing
// 経由で迂回し、要求もrows*2ではなくrows(画面ぴったり1枚)にした
// ――FileMode::ensureLoadedThrough()自身の2026-09-06さらに訂正分の
// コメント参照。このペーシングはpagerReturnsToShell_に絞らず全ての
// ストリームへ一般化済みなので、`man ls|usilic -c`のような直接パイプ/
// `$PAGER`経由の起動でも、最初の1画面は瞬時に、以後のスクロールは
// current+1で育つ。
void Application::pollPagerExitNotice(bool suppressDraw) {
  // 2026-09-17追加、pendingProgramChildPid_自身の宣言コメント参照
  // (runNestedUsilicProgram()自身のコメントも参照): ハンドオフ後に
  // 待たずに手放した子(sh経由の入れ子usilic、ソケットの送信バッファ
  // が満杯になりwrite()でブロックされたままかもしれない)を、この
  // メインループ毎回の反復のついでに(ゾンビ化を防ぐため)機会がある
  // たびにopportunisticに回収する――hasExited()自身のコメント通り
  // WNOHANGなので、まだ生きていれば単に0を返すだけで何も変えない。
  if (pendingProgramChildPid_ > 0 && ChildProcess::hasExited(pendingProgramChildPid_)) {
    pendingProgramChildPid_ = -1;
  }
  if (currentMode_ == ApplicationMode::File && fileMode_.hasActiveStream()) {
    int topIndex = activeView().topIndex();
    bool stillCatchingUpFirstScreen = static_cast<int>(activeView().lines().size()) < display_.rows();
    // 2026-09-06追加、直接のユーザー報告("80x24ターミナルだと開いた
    // 直後「00001/00024」のはずだが「00001/00050」になっている"):
    // lastPagerPollTopIndex_自身の宣言コメントの「初期値-1はどの
    // topIndexとも一致しないので、最初の呼び出しは必ず『動いた』
    // 扱いになる」という設計そのものが、この不具合の直接の原因
    // だった――新しいストリームを開いた直後、たとえloadStream()自身の
    // 初回同期読み込みで既にちょうど1画面分ぴったり埋まっていても、
    // メインループの最初の1回の反復がこの「-1との不一致」に基づいて
    // 無条件にtopIndex+rows*2(2画面分)の先読みを追加でトリガーして
    // しまい、"00001/00024"が一瞬で"00001/00049"前後まで一気に育って
    // しまっていた(実機PTYで確認: man bash | usilic -c)。「最初の
    // 呼び出しでは必ず動いた扱いにする」こと自体はstillCatchingUpFirstScreen
    // (画面がまだ埋まっていなければ、こちらが独立に先読みを続ける)
    // で既に不要になっていたため、lastPagerPollTopIndex_を-1のまま
    // (「まだ一度も記録していない」)である間は「動いた」と判定しない
    // よう改め、代わりに「動いたかどうかに関わらず、このtickの
    // topIndex自体は毎回必ず記録する」よう変更した――そうしないと、
    // 次にユーザーが本当に動かした時の比較対象(基準点)自体が
    // 永遠に-1のまま更新されず、GUIのホイール操作の検出が効かなく
    // なってしまう。
    bool viewerMovedSinceLastAttempt = lastPagerPollTopIndex_ != -1 && topIndex != lastPagerPollTopIndex_;
    lastPagerPollTopIndex_ = topIndex;
    if (stillCatchingUpFirstScreen) {
      if (suppressDraw) fileMode_.ensureLoadedThrough(topIndex + display_.rows(), /*bypassPacing=*/true);
      else ensurePagerStreamLoaded(topIndex + display_.rows(), /*bypassPacing=*/true);
    } else if (viewerMovedSinceLastAttempt) {
      if (suppressDraw) fileMode_.ensureLoadedThrough(topIndex + display_.rows() * 2);
      else ensurePagerStreamLoaded(topIndex + display_.rows() * 2);
    }
  }
}

// Command::ScrollLineDown/PageDown自身の宣言コメント参照。この関数
// 自身は「移動する前」に呼ぶ――FileMode::ensureLoadedThrough()を
// 直接呼び、ensurePagerStreamLoaded()(Applicationのラッパー)が
// 新しい内容を読み込めるとその場でredraw()してしまうのを避ける
// (実機PTYで確認: そのままだと、まだtopIndex_が「古い」位置のまま
// 一度描き直され、総行数だけ新しい値・カーソル位置は古いまま、という
// 中途半端な1回が挟まっていた)――可視の再描画は、呼び出し元が
// この直後に行う実際の移動(scrollLineDown()/pageDown()等)自身の
// 1回「だけ」に委ねる。
void Application::preloadPagerAhead(int prospectiveTopIndex, bool bypassPacing) {
  if (currentMode_ != ApplicationMode::File || !fileMode_.hasActiveStream()) return;
  fileMode_.ensureLoadedThrough(prospectiveTopIndex, bypassPacing);
}

// preloadPagerAhead()自身のコメント参照。この関数は「移動した後」に、
// 実際に着地した新しいtopIndex_で呼ぶ――pollPagerExitNotice()自身の
// lastPagerPollTopIndex_(「前回どこを試したか」の記憶)をここで
// 先回りして合わせておくことで、次のメインループの反復でそれが
// 「まだ見ていない動き」だと誤認し、同じ内容をもう一度(ペーシング
// された「現在+1」で)読み込んで3回目の再描画を引き起こすことを防ぐ。
void Application::suppressPagerRepoll(int actualNewTopIndex) {
  if (currentMode_ != ApplicationMode::File || !fileMode_.hasActiveStream()) return;
  lastPagerPollTopIndex_ = actualNewTopIndex;
}

void Application::drawPromptLabel(PromptKind kind, const std::string &positionText, const std::string &staticMessage) {
  int row = std::max(0, display_.rows() - 1);
  display_.moveCursor(row, 0);
  // まず行「全体」を反転表示で埋める(2026-08-18修正、実際の報告+
  // スクリーンショット=bn/=cq: "基本的に右端まで埋まっていない
  // モードラインは全部不良")。このポートの他の全ての既に正しい
  // モードラインは、それ自身の行を端から端まで埋める
  // (ContentView::renderModeLine()自身のfillTrailingToEdge参照)が、
  // この関数は、古いclearToEndOfLine()由来のプレーンな、埋められて
  // いない隙間で区切られた、個別の反転表示の走り(label/position/
  // trailing)だけを描いていた――実際の端末では色付きの「穴」として
  // 見え、壊れた/欠けたコンテンツと誤読しやすい。ここでベースの層を
  // 埋めてから、その上にlabel/position/trailingを描く(既に反転表示
  // されているので、それら自体には見た目の変化は無い)ことで、個々
  // の隙間を全て手作業で追跡する必要無く、構造的に隙間が埋められない
  // ままになることが決して無いことを保証する。promptEditor_自身の
  // redraw()(別途、この関数が戻った直後に呼ばれる)は、それ自身の
  // フィールドを描く前に明示的にstandoutをオフにする(LineEditor::
  // redraw()、key_eeol()のvt_endstand()から移植)――これは、編集可能な
  // フィールド自身の領域を、この塗りつぶしの上でプレーンへ「打ち
  // 抜く」、これがまさに望まれる見た目である: 生きた入力領域自体は
  // ハイライトされないままであり、行の他の全ては埋められたままである。
  // columns()ではなくcolumns()-1(2026-08-18の追跡修正、実際の報告:
  // "-c起動なのに何が違うんだろう？直ってないので"、`script`で
  // キャプチャした生のセッションが、無関係なキー入力があるまで
  // Terminal.appが再描画しなかった、バイト単位で正しい出力を示して
  // いたことから根本原因を突き止めた)。renderLine()自身の2026-08-13
  // 修正とContentView::renderModeLine()自身の対応する2026-08-18修正
  // (それ自身の長いコメント参照)と同じ根本原因: standoutでマーク
  // された文字を端末の本当の「最後の」列へ書くと自動折り返しが
  // トリガーされ、DECSTBMのスクロール領域(1-23行目)の「外側」でも
  // ある行からカーソルを黙って動かしてしまう――実際の端末はその
  // 組み合わせを一貫性無く扱い、明らかに、後の無関係な書き込みまで
  // 再描画を遅らせる。本当の最後の列を埋めないままにしておくことは、
  // renderLine()とrenderModeLine()が既に行ったのと同じ実用的な
  // トレードオフである。
  display_.setAttribute(Display::Attribute::Standout);
  display_.write(std::string(std::max(0, display_.columns() - 1), ' '));
  display_.setAttribute(Display::Attribute::StandoutOff);
  // 本当の最後の列自体: 印字される文字ではなくclearToEndOfLine()
  // (termcapの`ce`)経由でそれを消去する――消去操作は、最後の列に
  // 「文字を書く」ことがそうするようには自動折り返しをトリガーしない
  // (それがまさに、この上限全体が存在して避けようとしている仕組み
  // である)、そのためこれは、バグを再導入することなく、以前の画面が
  // そこに残した古いコンテンツを安全にクリアする。
  if (display_.columns() > 0) {
    display_.moveCursor(row, display_.columns() - 1);
    display_.clearToEndOfLine();
  }
  display_.moveCursor(row, 0);

  if (kind == PromptKind::ExitConfirm) {
    // disp.c自身のM_exitケースから移植(実際のCUIスクリーンショット、
    // 2026-08-05のユーザー報告に対して検証済み): モードラインの
    // NAMEフィールドは単に(反転表示された)"EXIT"(str.cのP_modeテー
    // ブル: " EXIT")である――しかしその後の"Exit (y/n) ? "という
    // メッセージのテキストは反転表示され「ない」(print_line()は、
    // メッセージがS_errg("Exit")で始まるため、特にvt_endstand()を
    // 呼ぶ)。このポートは以前、両方を単一の"EXIT (y/n)?"という文字列
    // へまとめ、完全に反転表示で描いていた――表現もスタイルも、
    // どちらもCUIのオリジナルに一致していなかった。「prev」フィールド
    // (モードライン[6]、戻る先のモード)もここに移植されている:
    // 反転表示、画面の右端まで埋められる、タイプされたy/nの答えの
    // 余地を残すためメッセージの2、3列後から始まる。
    static const std::string kName = "EXIT";
    static const std::string kMessage = "Exit (y/n) ? ";
    display_.setAttribute(Display::Attribute::Standout);
    for (unsigned char c : kName) display_.putChar(c);
    display_.setAttribute(Display::Attribute::StandoutOff);
    display_.putChar(' ');
    for (unsigned char c : kMessage) display_.putChar(c);
    int labelWidth = static_cast<int>(kName.size()) + 1 + static_cast<int>(kMessage.size());

    std::string prev = previousModeName(currentMode_);
    int editableWidth = std::max(1, display_.columns() - labelWidth);
    constexpr int kAnswerGap = 2;  // trailingフィールドが始まる前の、タイプされたy/nのための余地
    int trailingCol = labelWidth + kAnswerGap;
    if (!prev.empty() && trailingCol < display_.columns()) {
      int available = display_.columns() - trailingCol;
      std::string padded = prev;
      if (static_cast<int>(padded.size()) > available) padded = padded.substr(0, available);
      else padded += std::string(available - padded.size(), ' ');
      display_.moveCursor(row, trailingCol);
      display_.setAttribute(Display::Attribute::Standout);
      for (unsigned char c : padded) display_.putChar(c);
      display_.setAttribute(Display::Attribute::StandoutOff);
      // promptEditor_自身のredraw()は、キー入力のたびにそのフィールド
      // の全幅をスペースでクリアする(LineEditor::redraw()自身の
      // コメント)――これをkAnswerGapに制限することで、すぐ上で描いた
      // ばかりのtrailingフィールドを消してしまうことを防ぐ。
      editableWidth = kAnswerGap;
    }
    promptEditor_.setField(row, labelWidth, editableWidth);
    return;
  }

  // RecentFile/RecentDirectory + listBrowserChronological_
  // (2026-08-18修正、同じ=cz/=daの回の一部): refreshDirectEntryModeLine()
  // が存在する前にこの画面が使っていた「古い」レンダラー
  // (listBrowserView_自身のModeLineFieldsProvider、それ自身のコメント
  // 参照)は、時系列順にトグルされている間、既に"DIR"/"FILE"の代わり
  // に"HISTORY"を表示していた(項目160自身の既に確立した慣習、
  // `^R` = ToggleHistoryView)。drawPromptLabel()は全てのPromptKindに
  // わたって汎用的であり、listBrowserChronological_という概念を
  // 全く持たないので、これが無いと、refreshDirectEntryModeLine()が
  // この画面のレンダリングを引き継いだ瞬間に、そのヒントを黙って
  // 落としてしまう――実際にどちらの順序が表示されているかに関わらず、
  // ラベルは"DIR"/"FILE"のままだった。
  std::string label = ((kind == PromptKind::RecentFile || kind == PromptKind::RecentDirectory) &&
                        listBrowserChronological_)
                           ? "HISTORY"
                           : promptLabelText(kind);
  display_.setAttribute(Display::Attribute::Standout);
  for (unsigned char c : label) display_.putChar(c);
  display_.setAttribute(Display::Attribute::StandoutOff);
  int labelWidth = static_cast<int>(label.size());
  // 2026-08-18修正: この隙間(そしてこの関数内の他の全ての1列分の
  // 隙間)は、以前は純粋に`labelWidth`の算術上の増分であり、実際には
  // 一度も「書かれて」いなかった――上の2026-08-18のfill-to-edgeの
  // 修正より前は無害だった(clearToEndOfLine()が既に行全体をプレーン
  // のままにしていたので、触れられていない列はデフォルトで単にプレー
  // ンのままだった)が、その塗りつぶしは今や、触れられていない全ての
  // 列を代わりに「反転表示」のままにする(実際の報告がこれを捕まえた:
  // "DIR"と"1/1"の間の隙間自体が反転表示されており、1つの連続した
  // ブロックのように読めた)。明示的にプレーンなスペースを書く
  // (ここでは既にStandoutはオフ)ことが、今やその塗りつぶしを通して
  // 実際に本物の隙間を打ち抜くものである。
  if (!label.empty()) {
    display_.putChar(' ');
    labelWidth += 1;
  }
  // positionText(2026-08-12修正): この関数自身の宣言コメント参照。
  // 反転表示、候補リスト自身のModeLineFieldsProviderが1フレーム前に
  // それについて既に使っていたのと「同じ」"01/10 TOP"というスタイル
  // に合わせている(feedRegexListByte()の編集への遷移が、これを
  // 空でない値で渡す唯一の呼び出し元である)。(`label`を描いた後に
  // カーソルがたまたま着地した場所に頼るのではなく)明示的に
  // labelWidthへmoveCursor()するので、下のこのフィールドの前の隙間
  // の計算が、この修正の以前の版がしていたように黙って列を二重に
  // 数えることができない――実際の報告+参照スクリーンショットにより、
  // このリスト自身の候補テキスト(例えば"^Question")が、プレーンな
  // リスト閲覧中に座っていた場所(name+" "+position+" "+trailingを、
  // 2列ではなく各フィールドの後にちょうど1列の隙間で描く、
  // listBrowserView_自身のModeLineFieldsProvider)より、編集が始まる
  // と1列分さらに右に着地することが判明した。
  if (!positionText.empty()) {
    display_.moveCursor(row, labelWidth);
    // 2026-08-28の反転、直接のユーザーの訂正(HISTORY.md:9539/9545は
    // 誤読だった――"ENDが％でなかったことによる思い込み"): これが
    // 置き換える2026-08-18の修正は、"「DIR」「1/1」「END」の間にも
    // 隙間を入れて欲しい"が、ラベル("DIR")とpositionText全体の間の
    // 1つだけでなく、「その」内部の隙間も求めていたという思い込みで、
    // positionText("1/1 END"/"01/10 TOP")をその内部のスペースで、
    // 別々に隙間を空けた2つのStandoutの走りへ分割していた。後の報告
    // ("「01/10」「TOP」...区切りは不要")とこの訂正により、「元々の」
    // 依頼は、ラベルのすぐ後の1つの隙間(このブロックが実行される前に
    // 既に上で無条件に処理されている)だけを意味していたに過ぎない
    // ことが明らかになった――positionText自体は常に1つの連続した
    // Standoutの走り、"一律「1/1 END」"であるべきで、他の全ての
    // モードラインのrenderModeLine()自身の慣習(それも一度もそれを
    // 分割していなかった――この分割は常にdrawPromptLabel()だけに
    // 限定されていた、削除されたコメント自身の注記についてはgit
    // の履歴参照)に合わせている。
    display_.setAttribute(Display::Attribute::Standout);
    for (unsigned char c : positionText) display_.putChar(c);
    display_.setAttribute(Display::Attribute::StandoutOff);
    labelWidth += static_cast<int>(positionText.size());
    display_.putChar(' ');  // 次のフィールドの前の明示的なプレーンな隙間
    labelWidth += 1;
  }
  // CopyDestination/MoveDestination/RemoveConfirm自身の
  // "Destination ? "/"Delete ? "メッセージ――2026-08-21修正、実際の
  // 報告+参照スクリーンショット(=ef、実際のlegacy/M_destリファクタ
  // 前のバイナリを並べて実行): このポートは実際にはこのテキストを
  // 一度も描いておらず、そのため^X^Mの宛先/確認プロンプトは、反転
  // 表示された"CP"/"MV"/"RM"のラベルと位置フィールドだけを示し、
  // その後、間に何も判読できるものが無いまま、右寄せのソースパス
  // フィールドへ直接跳んでいた――「プロンプトが全く表示されていない」
  // と誤読しやすい、これはまさに、このテキストが見えるようになった
  // 時点でここに正しく着地する流れの修正について、報告が最初にそう
  // 読んだ通りである("なんも変わってないんだが")。legacyのkey.c自身
  // のT_rm("Delete ? ")対T_cp/T_mv(デフォルト、"Destination ? ")の
  // メッセージテキストから移植、すぐ上のExitConfirmの分岐自身の
  // "Exit (y/n) ? "の前例(disp.c自身のvt_endstand()の慣習: モード名
  // は反転表示され、その後のメッセージは反転表示されない)に合わせて
  // プレーンに(Standoutではなく)描かれる――参照スクリーンショットで
  // は、行自身の反転表示の塗りつぶしを通して打ち抜かれた、通常の
  // (反転表示でない)テキストの「穴」として見える。
  if (kind == PromptKind::CopyDestination || kind == PromptKind::MoveDestination ||
      kind == PromptKind::RemoveConfirm) {
    std::string message = kind == PromptKind::RemoveConfirm ? "Delete? (y/n) " : "Destination? ";  // 2026-09-27、RMは"Delete? (y/n) "、CP/MVは"Destination? "(直接のユーザー依頼)
    display_.moveCursor(row, labelWidth);
    for (unsigned char c : message) display_.putChar(c);
    labelWidth += static_cast<int>(message.size());
  }
  // staticMessage(2026-08-13): この関数自身の宣言コメント参照
  // ――編集可能なフィールドがそうでなければ始まるであろうまさにその
  // 列に、プレーンな下線付きのステータスメッセージを描き、それから
  // promptEditor_には一切触れずに戻る(予約/設定すべきフィールドが
  // 無い――これは本物の編集可能なプロンプトではない)。
  // (2026-09-10、一度この直後のreturn;を取り除く修正を試みたが、
  // 直接のユーザー報告(スクリーンショット=fw)により、staticMessage
  // と下のtrailingフィールドの間に無地のハイライトの帯が残留する
  // 見た目の後退を引き起こしたため撤回――ユーザーの本来の意図は
  // そもそも戻り値の復元ではなかった(GREP自身のマッチ無し表示を
  // このLOCATE/DUの体裁に「合わせてほしい」という意味であり、この
  // 関数自体を変更してほしいという意味ではなかった)。この関数は
  // 元の挙動のまま。)
  if (!staticMessage.empty()) {
    display_.moveCursor(row, labelWidth);
    display_.setAttribute(Display::Attribute::Underline);
    for (unsigned char c : staticMessage) display_.putChar(c);
    display_.setAttribute(Display::Attribute::UnderlineOff);
    return;
  }
  // Trailingの予約(2026-08-12修正): 直接のユーザーの公式――
  // "文字入力エリアの幅は、「モード名（EGREPなら5＋1）」＋「...右詰
  // 16文字（/Users/haa15030/git/usilicなら「...5030/git/usilic/」＋
  // 1）」をCOLUMNSから引いた値でいい"――grepView_自身の
  // ModeLineFieldsProviderのtrailing2フィールド(GREP/CUTの「結果」
  // 画面に表示されるCWD、それ自身のコメント参照)を反映しているが、
  // GREP/CUTだけでなく、全てのプロンプトの種類自身の「編集」画面
  // について一様にここで予約される。今は意図的に「幅」だけを予約する
  // ――この空間には実際にはまだ何も描かれない、ユーザー自身の明示的な
  // "それを超える文字幅入力への対処はのちほど"に合わせている: この
  // 予約が無いと、プロンプトは現在、legacy自身の相当するフィールドが
  // 一度もそうだったことがないほど、画面の実際の右端までタイプされ
  // 得る(legacy自身のフィールドは、ディレクトリパスのフィールドの
  // ためにこのtrailing領域を常にそのままにしていた)、そのため今
  // それを狭めることで、そのtrailingフィールド自身の「コンテンツ」
  // が後の回で描かれるようになる前でも、このポート自身の行の折り
  // 返し/`need_scroll()`相当のロジックを、legacyの実際の利用可能な
  // 幅と一致させたままにする。ExitConfirmは除外されている(上の
  // それ自身の分岐が、既にその実際の「前のモード」のtrailingフィー
  // ルドを正しく予約している)。
  constexpr int kTrailingPathEllipsis = 3;  // "..."の分
  constexpr int kTrailingPathGap = 1;
  int trailingReserve = kTrailingPathTailChars + kTrailingPathEllipsis + kTrailingPathGap;
  int editableWidth = std::max(1, display_.columns() - labelWidth - trailingReserve);

  // 戻り先のコンテンツ(2026-08-12修正;同日に「2回」2026-08-13に
  // 一般化された――下記参照): 上の予約は以前は幅だけであり、実際に
  // そこに何が描かれるかを意図的に先送りしていた――実際の報告
  // ("FGREPで戻り先が無い"、そしてEGREPについて、編集が始まった瞬間
  // "モードラインでは戻り先の消える")により、その空間が空白のまま
  // で、「予約済みだがまだ実装されていない」ではなく「戻り先が全く
  // 無い」と読めることが判明した。
  //
  // 最初の一般化(それでも間違っていた、下で訂正): 「それ自身専用の
  // *ReturnMode_変数を持つ全てのPromptKind、そのモード== Directoryの
  // 時だけ表示される」に限定されていた――これはexecuteProgram()が
  // 決してApplicationModeを変えないので「戻りモードという概念が無い」
  // という理屈で、ProgramCommandを除外していた。直接のユーザーの訂正:
  // その理屈は全く成り立たない――"PROGRAMを起動したモードは必ず存在
  // するわけだから...それが戻り値になるのであってモード自身を変更
  // するかどうかなんて何の関係もない"。「宛先がDirectoryの場合のみ」
  // に制限するのも同じくらい恣意的だった――legacy自身のlp->prev
  // (disp_window()のモードライン[6])は、`strcpy(lp->prev,
  // &ap->name[...]);`経由で、あらゆるモード遷移関数によって同一に
  // 埋められる(m_prog()/m_grep()/m_cut()/m_locate()/m_man()は全て
  // この1行を共有する)、つまり「ap->name(『前の』画面自身の表示名)
  // が何であれ」――Directory専用の特別なケースではない。
  //
  // previousModeName(ApplicationMode)は既に存在し、まさにこのポート
  // 自身の、その汎用の「モードXの表示名」を解決するもの(既にShell
  // 自身のshellReturnName_とKEY自身のtrailingフィールドが使っている)
  // に相当する――このプロンプトが実際に戻る先のモードについて(ある
  // いは、専用の*ReturnMode_変数を持たないPromptKind――
  // ProgramCommandも含む――については、何もそれを変えないので単に
  // currentMode_自体)、2つ目の、より狭いDirectory専用の版を独自に
  // 手作りするのではなく、ここでそれを再利用する。
  ApplicationMode destinationMode = currentMode_;
  // 2026-09-18追加(フォローアップ8): tabCompletionContext_が有効な間
  // (=一覧をまだ閲覧中、編集はまだ始まっていない)は、下のkindごとの
  // switchより「先に」これを優先する――このタイミングでは
  // drawPromptLabel()自身にtabCompletionContext_->kind(SHELL/PROGRAM/
  // MAN/FILE/DIR等、実際のラベル)がそのまま渡ってくるようになった
  // ため(フォローアップ8自身のrefreshDirectEntryModeLine()修正参照)、
  // 下のswitchがそのkind専用のケース(例えばRecentFile/RecentDirectory
  // 自身のpromptHistoryContext_依存の分岐)に誤って乗ってしまう――
  // promptHistoryContext_はこの閲覧中は常にnulloptなので、そちらの
  // ケースはDirectoryへ誤ってフォールバックしてしまう。
  // tabCompletionContext_->returnModeこそが、この閲覧中における唯一
  // 正しい戻り先である。
  if (tabCompletionContext_) {
    destinationMode = tabCompletionContext_->returnMode;
  } else
  switch (kind) {
    case PromptKind::GrepPattern: destinationMode = grepReturnMode_; break;
    case PromptKind::CutPattern: destinationMode = cutReturnMode_; break;
    case PromptKind::ManName: destinationMode = manReturnStack_.back(); break;
    case PromptKind::LocateQuery: destinationMode = locateReturnStack_.back(); break;
    // RecentFile/RecentDirectory(2026-08-18修正、=cxの報告――"戻り値
    // が惜しい"): GrepPattern/CutPattern/ManName/LocateQueryとは異なり、
    // この2つはそれ自身専用の*ReturnMode_を持たない――そのため
    // destinationModeは上の`= currentMode_`のデフォルトへ流れ落ちて
    // いた。それは実際の編集が始まった後は正しい(その時currentMode_
    // は本当に、意図的にListBrowserである――この関数自身の呼び出し
    // 元参照)が、refreshDirectEntryModeLine()は、currentMode_が
    // (startPromptHistoryRecall()自身のrefreshDirectEntryModeLine()
    // 呼び出しより前に)「既に」ListBrowserである、読み取り専用の
    // タイプ前の候補リストを「依然として」表示している間にこの関数を
    // 呼ぶ。previousModeName()にはListBrowserのケースが無い(それ
    // 自身の`default: return ""`へ落ちる)ので、destinationDirectory
    // は空のまま戻り、trailingフィールド全体のfill-to-edgeの箱が
    // 中にテキストを全く持たないまま描かれていた。
    // promptHistoryRecallReturnMode_(ListBrowserへの切り替えより
    // 「前に」currentMode_に設定される、startPromptHistoryRecall()
    // 参照)は、まさにこのプロンプトが実際に戻る先のモードであり、
    // returnFromListBrowser()自体が既に頼っているのと同じ値である
    // ――別個の変数を独自に手作りするのではなく、ここでそれを再利用
    // する。
    // 2026-09-18修正(フォローアップ8): 非nulloptのpromptHistoryContext_
    // が無い場合のフォールバックを、ハードコードされたDirectoryから
    // currentMode_へ変更した――tabCompletionContext_駆動の(RecentFile/
    // RecentDirectory自身のTab補完、フォローアップ5参照)「編集を開始
    // する」遷移後は、promptHistoryContext_は常にnulloptのままだが、
    // currentMode_は既に(feedTabCompletionListByte()自身の遷移が)本当の
    // 戻り先(File/Directory)へ切り替え済みなので、こちらの方が正しい。
    case PromptKind::RecentFile:
    case PromptKind::RecentDirectory:
      destinationMode = promptHistoryContext_ ? promptHistoryContext_->returnMode : currentMode_;
      break;
    // PromptKind::TabCompletionList自身の宣言コメント参照――もう
    // drawPromptLabel()へ渡されることが無い(フォローアップ8、
    // refreshDirectEntryModeLine()自身の同時の修正参照)ため、この
    // caseは到達不能だが、他のDu/ArchiveLoad同様のマーカー専用値と
    // して残す。
    case PromptKind::TabCompletionList:
      destinationMode = tabCompletionContext_ ? tabCompletionContext_->returnMode : ApplicationMode::Directory;
      break;
    // 2026-09-18追加(フォローアップ7、PromptKind::ShellCommand自身の
    // 宣言コメント参照): このPromptKindは常にSHELLへ戻ることを意味する
    // 定義そのもの(他のどんな戻り先も取り得ない)なので、状態を一切
    // 読まずハードコードできる――かつ、この回のコミットからは
    // currentMode_自体がShellCommand編集中は意図的にListBrowserのまま
    // なので、default(=currentMode_)には二重の意味で乗れない。
    case PromptKind::ShellCommand:
      destinationMode = ApplicationMode::Shell;
      break;
    default: break;
  }
  // 2026-08-18: 同日の試みは、ここでRecentFile/RecentDirectoryを完全
  // に除外していた(理屈: ここで現在のディレクトリを表示すると、
  // 数列左で編集可能なフィールドにタイプされているのと同じパスの
  // ほぼ重複のように読める)。直接のユーザーの訂正: 実際の問題は
  // このフィールドの「コンテンツ」では一度も無く、その周りの「行が
  // 端まで埋められていなかった」ことだった("=bn参照。基本的に右端
  // まで埋まっていないモードラインは全部不良"――下のfill-to-edgeの
  // ブロックが実際の修正である)。他の全てのPromptKindが既に使って
  // いる無条件のpreviousModeName(destinationMode)へ戻した。
  // RecentFile/RecentDirectory + listBrowserChronological_
  // (2026-08-18修正、直接のユーザーの訂正: "^X^D→^Rは戻り値DIRで
  // ないと。旧版はそうなってるんで"): このtrailingフィールド自身の
  // 「宛先パス」のコンテンツ(すぐ上の修正)は、通常のソート済みの
  // "DIR"/"FILE"画面については正しいが、legacy自身のT_hfile/T_hdir
  // ――そしてrefreshDirectEntryModeLine()が引き継ぐ前の、まさにこの
  // 画面についてのこのポート自身の「古い」レンダラー
  // (listBrowserView_自身のModeLineFieldsProvider、それ自身の
  // コメント参照: `rightWatermark = listBrowserChronological_ ?
  // promptLabelText(...) : ""`)――は、いったん時系列の「HISTORY」
  // ビューへトグルされると、この同じスロットに代わりに「元の」
  // カテゴリのバッジ("DIR"/"FILE")を表示し、「どちらの」ringの履歴
  // なのかを明確にする(左のラベル自体はその時点で単に固定のリテラル
  // "HISTORY"を言うだけである、他の全てのpromptHistoryRecallKind_
  // を保持するもの自身の時系列ビューと同様)。宛先パスではなく、この
  // 関数が実際に持つ唯一のスロットに、その同じバッジをここで復元
  // する。
  bool showsBadge =
      (kind == PromptKind::RecentFile || kind == PromptKind::RecentDirectory) && listBrowserChronological_;
  // CutContent(2026-08-24修正、直接のユーザー報告: HELPのある章自身
  // のコンテンツの内部から直接タイプされたFGREPは、プロンプトを
  // 投入/離れる「前は」、参照のような"CUT:1.  Introduction"ではなく
  // 素の"CUT"をここに表示していた;previousModeName()自体は、そこ
  // からセグメントのラベルを描くためのインスタンスごとの状態を持た
  // ない(それ自身のコメント参照)ので、代わりにこれはcutContentReturnLabel()
  // ――grepView_自身の投入後のモードラインが同一のシナリオについて
  // 既に使っているのと「同じ」ヘルパー――を再利用する。
  // 2026-09-11追加、直接のユーザー報告("usilic -c（引数無し）で起動→
  // ^X^D→^O^S：モード名FGREPのはずなのにGREPかつハイライトの戻り値
  // 空白"、続けて"あとは戻り値だけ。現状空白なのでFILE/DIRでは
  // それぞれFILE/DIRとしてくれればいい"): ^O^S(FGREP)/^J^S(EGREP)を
  // ^X^F/^X^D自身の候補一覧を「まだ閲覧中」(何もタイプする前)に
  // 起動すると、grepReturnMode_(destinationModeの元)は
  // ApplicationMode::ListBrowserになる――previousModeName()には
  // ListBrowserのケースが無い(すぐ上のRecentFile/RecentDirectory
  // 自身のコメント参照、同じ理由で以前blank化していた)ため、
  // destinationDirectoryが空のままになっていた。grepReturnListCategory_
  // (startGrepPrompt()自身のコメント参照――promptHistoryContext_を
  // 使わないのは、EGREPが`startRegexPromptList()`経由でそれを自分の
  // 候補一覧向けに上書きしてしまい、FGREPとEGREPとで挙動が非対称に
  // なっていたため)を見てユーザー依頼通り単に"FILE"/"DIR"を表示する。
  std::string destinationDirectory;
  if (destinationMode == ApplicationMode::ListBrowser && kind == PromptKind::GrepPattern) {
    destinationDirectory = grepReturnListCategory_ == RingCategory::RecentFile ? "FILE" : "DIR";
  } else if (showsBadge) {
    destinationDirectory = promptLabelText(kind);
  } else if (destinationMode == ApplicationMode::CutContent) {
    destinationDirectory = cutContentReturnLabel();
  } else if (kind == PromptKind::ShellCommand) {
    // 2026-09-19再訂正(フォローアップ12、直接のユーザー指摘: "SHELLは
    // OKだが、なぜか消え去ったはずの「LIST」が戻り値に復活している。
    // ^Iを押す直前の戻り値でいいんでよろしく"): フォローアップ11で
    // ここに"LIST"をハードコードしたが、これはフォローアップ8自身の
    // 「汎用の機能名では無く実際の宛先を見せる」という方針に自ら反して
    // いた。SHELL自身のネイティブなモードライン(shell_.scrollback()の
    // ModeLineFieldsProvider、このファイル冒頭)は、Tabが押される
    // 「前」から既に`fields.trailing3 = shellReturnName_`という、まさに
    // これと同じ枠に「本物の」戻り値(SHELL自体が^Gで戻る先、例えば
    // "DIR"/"FILE")を表示している――ここでもそれをそのまま使うことで、
    // Tab補完の前後で戻り値が変わらないようにする。
    destinationDirectory = shellReturnName_;
  } else {
    destinationDirectory = previousModeName(destinationMode);
  }
  if (!destinationDirectory.empty()) {
    // CutContent(2026-08-24、上と同じ修正)は元々、truncatedTrailingPath()
    // を通さず"CUT:<label>"を丸ごと生かしていた――その16文字の「末尾」
    // キャップは末尾部分が意味のある部分である長い「パス」のための
    // もので、丸ごと適用すると"CUT:"という接頭辞自体を食べてしまう
    // (例えば"CUT:1.  Introduction" -> "...1.  Introduction")ためで、
    // 当時は「(HELPの番号付き見出しのように)ラベルは常に短いので
    // 問題が表面化しない」という前提でこれを許容していた。
    //
    // 2026-09-20修正、直接のユーザー依頼("今のKEYをHELPに置き換え...
    // 正味の：カットによるHELPとしてほしい")によりHELPの本物のセグメント
    // ラベルはQUICK.txt/usilic.man由来の「見出し：本文」形になり、
    // 上の前提が崩れた――ラベルがeditableWidth(下のtrailingReserve
    // 分だけ切り詰められた入力欄の幅)へ食い込むほど長いと、まだ入力中
    // のプロンプト自身の再描画がtailの左側を上書きしてしまう
    // (実例: "CUT:Section One：first entry"の先頭"CUT:Sectio"が消え、
    // "n One：first entry"だけが残る)。今はMAN/LOCATE/DUと全く同じ
    // 「接頭辞は必ず生かし、値だけ切り詰める」方式に統一する――ただし
    // 値側は末尾ではなく先頭を残す(パスと違い、ラベルは見出しの部分
    // が先頭にあり、そちらの方が有用なため)。
    std::string tail;
    constexpr int kPromptTrailingTailChars = kTrailingPathTailChars - 1;  // 15、このブロック自身のコメント参照
    if (destinationMode == ApplicationMode::CutContent) {
      constexpr int kCutPrefixLen = 4;  // "CUT:"、cutContentReturnLabel()自身の実装参照
      std::string prefix = destinationDirectory.substr(0, kCutPrefixLen);
      std::string value = destinationDirectory.substr(kCutPrefixLen);
      int valueBudget = kPromptTrailingTailChars + 3 - static_cast<int>(prefix.size());
      if (valueBudget < 1) valueBudget = 1;
      if (static_cast<int>(value.size()) <= valueBudget) {
        tail = prefix + value;
      } else {
        int valueHeadChars = std::max(1, valueBudget - 3);
        tail = prefix + value.substr(0, valueHeadChars) + "...";
      }
    } else if (destinationMode == ApplicationMode::Man || destinationMode == ApplicationMode::Locate ||
               destinationMode == ApplicationMode::Du) {
      // 2026-09-19追加、直接のユーザー報告("^S→②戻り値不良。正しくは
      // DU: /Users/haa150...ic/CLAUDE.md（DUモードを明示）"): これら
      // 3つはprevious ModeName()自身が既に"<MODE>: <値>"という接頭辞
      // 付き文字列を返す(それぞれの宣言コメント参照)――しかし、すぐ
      // 上のCutContent分岐が回避しているのとまさに同じバグを、ここでは
      // 依然として踏んでいた: 下のelse節の素朴な「末尾を残す」切り
      // 詰めを"DU: /very/long/path"のような文字列全体へ適用すると、
      // パス側は十分短ければ生き残る一方、接頭辞自体("DU: ")が真っ先に
      // 食べられてしまう(CutContentの"CUT:<label>"と全く同じ理屈だが、
      // あちらはラベルが常に短いので問題が表面化しなかった――MAN/
      // LOCATEの値も短いことが多く同様に見過ごされていたが、DUの値は
      // ファイルパスなので長くなりがちで、まさにこの報告で発覚した)。
      // CutContentのように丸ごと切り詰めを諦めるのではなく、コロン以降
      // の「値」部分だけを切り詰め、接頭辞は必ず生き残す――値側の予算
      // は、この関数の他の全てのプレーンなパスが使う予算(kPromptTrailingTailChars
      // + "..."の3文字)から接頭辞自身の長さを差し引いたもの。
      std::size_t colon = destinationDirectory.find(": ");
      if (colon == std::string::npos) {
        tail = destinationDirectory;  // 保険――実際には常に見つかるはず
      } else {
        std::string prefix = destinationDirectory.substr(0, colon + 2);
        std::string value = destinationDirectory.substr(colon + 2);
        int valueBudget = kPromptTrailingTailChars + 3 - static_cast<int>(prefix.size());
        if (valueBudget < 1) valueBudget = 1;
        if (static_cast<int>(value.size()) <= valueBudget) {
          tail = prefix + value;
        } else {
          int valueTailChars = std::max(1, valueBudget - 3);
          tail = prefix + "..." + value.substr(value.size() - valueTailChars);
        }
      }
    } else if (static_cast<int>(destinationDirectory.size()) <= kTrailingPathTailChars) {
      tail = destinationDirectory;
    } else {
      tail = "..." + destinationDirectory.substr(destinationDirectory.size() - kPromptTrailingTailChars);
    }
    // バッジ(2026-08-18修正、直接のユーザーの訂正+スクリーンショット
    // =db: "戻り値のDIRは左詰めでよろしく"): 上のDIR/FILEのバッジは、
    // 短い固定幅のカテゴリのマーカーであり、パスではない――それを
    // 右寄せする(ここの他の全てのPromptKindが使う、長いパスの末尾
    // 部分が意味のある部分である、宛先パスの慣習)と、単にそれを、
    // 前に広い隙間を伴って画面の右端に孤立させてしまう。代わりに
    // 左寄せする、promptEditor_自身のtrailingの予約が始まるのと同じ
    // 列(labelWidth + editableWidth、下で計算)から始める――つまり、
    // パス末尾の慣習の右端ではなく、予約された箱の左端。
    int col = showsBadge ? labelWidth + editableWidth
                          // columns()ではなくcolumns()-1(2026-08-18
                          // 修正): すぐ上のベースの塗りつぶしと同じ
                          // 自動折り返し回避――このStandoutの走りを
                          // 本当の最後の列でちょうど終えることには、
                          // 同一の実際の端末の再描画の問題がある。
                          // 本当の最後の列自体は、(上記参照)ベースの
                          // 塗りつぶし自身の末尾のプレーンなスペース
                          // の書き込みに任せる、renderLine()/
                          // renderModeLine()自身の確立された「まさに
                          // 最後の列にはマークしない」というトレード
                          // オフに合わせている。
                          : display_.columns() - 1 - static_cast<int>(tail.size());
    if (col > labelWidth) {
      // 2026-08-28修正、直接のユーザー報告(すぐ上の`tail`自身のコメント
      // と同じ再現): `tail`を縮めるだけで本物の隙間の列は解放された
      // が、そこに明示的にプレーンなスペースを「書く」ものが一度も
      // 無かった――この関数の他の「全ての」隙間は、明示的なプレーンな
      // putChar(' ')でベースのStandoutの塗りつぶしを通して打ち抜かれ
      // るが、これは、`col`がlabelWidth+editableWidthを超えて着地
      // する(上の縮小が今や長い/切り詰められたtailについてそれを保証
      // する)たびに、単に一度も訪れられておらず、そのため、この関数
      // のまさに冒頭のベースの塗りつぶしが既にそこに書いたもの
      // (Standout ON)を黙って保持していた――本物の隙間ではなく、
      // 1つの連続した反転表示のブロックの一部として読める。
      if (!showsBadge && col > labelWidth + editableWidth) {
        display_.moveCursor(row, labelWidth + editableWidth);
        display_.setAttribute(Display::Attribute::StandoutOff);
        for (int c = labelWidth + editableWidth; c < col; c++) display_.putChar(' ');
      }
      display_.moveCursor(row, col);
      display_.setAttribute(Display::Attribute::Standout);
      for (unsigned char c : tail) display_.putChar(c);
      display_.setAttribute(Display::Attribute::StandoutOff);
    }
  }

  promptEditor_.setField(row, labelWidth, editableWidth);
}

void Application::refreshDirectEntryModeLine() {
  if (currentMode_ != ApplicationMode::ListBrowser) return;
  // 2026-09-18追加、HELP.txt 2.13章「LIST」の実装、実機報告
  // (スクリーンショット=gv、legacyの参照画面: "プロンプトエリアは
  // 編集可能な非ハイライト表示が正解")を受けての追加。RecentFile/
  // RecentDirectoryの直下の分岐と全く同じ形――promptHistoryContext_
  // ではなくtabCompletionContext_を見る点だけが違う(design.md参照、
  // SHELLの生きたコマンドラインにはPromptKindという概念自体が無い
  // ため、この機能専用の別のoptionalを使っている)。
  // 2026-09-18再訂正(フォローアップ8、直接のユーザー報告「SHELLなら
  // SHELL、FILEならFILEから一歩も出ずに補完しているように見える」):
  // 以前はここで汎用のPromptKind::TabCompletionList("LIST")を無条件に
  // 渡していたため、編集開始の瞬間にtabCompletionContext_->kind
  // (SHELL/PROGRAM/MAN等)へラベルが切り替わって見えていた――
  // ^X^F^Rの「HISTORY」が閲覧中も編集中も同じラベルを保つのに倣い、
  // 閲覧中からtabCompletionContext_->kind自身をそのまま渡す。
  if (tabCompletionContext_) {
    int candidateCount = static_cast<int>(listBrowserMode_.items().size());
    std::string positionText =
        candidateCount > 0 ? ModeLine::format(listBrowserView_.cursorIndex() + 1, candidateCount) : "";
    drawPromptLabel(tabCompletionContext_->kind, positionText);
    // 2026-09-19追加(フォローアップ10、直接のユーザー指摘「SHELLの
    // 場合、^Iを押した時も"usilic "（←SHELLプロンプトで入力済み文字列）
    // を入れておけば^Bで突然それが現れて位置がズレることもないので
    // は？」): prefixBeforeWord(補完対象語の「前」にあったテキスト、
    // 例えば"usilic "――単語の途中を補完している時に非空になる)を
    // 付け忘れていたため、閲覧中のプレビューは候補自身("fake-one")
    // だけを見せ、「編集を開始する」遷移(feedTabCompletionListByte()
    // 自身の同じ式のseed計算参照)で初めてprefixBeforeWordが前に
    // 付いた完全な行("usilic fake-one")が現れていた――まさにこの
    // 「突然現れる」ことそのものがフォローアップ9の位置ズレとは別の、
    // もう1つの見た目の跳躍だった。閲覧中から同じ完全な行を見せる
    // ことで、編集開始時に内容が変わらなくなる。
    const std::string *selected = listBrowserMode_.selectedItem();
    promptEditor_.setText(tabCompletionContext_->prefixBeforeWord +
                           (selected ? *selected : tabCompletionContext_->partial));
    promptEditor_.redraw();
    return;
  }
  if (!promptHistoryContext_ || !promptHistoryContext_->isDirectEntry) return;
  PromptKind kind = promptHistoryContext_->kind;
  if (kind != PromptKind::RecentFile && kind != PromptKind::RecentDirectory) return;
  int candidateCount = static_cast<int>(listBrowserMode_.items().size());
  std::string positionText = candidateCount > 0 ? ModeLine::format(listBrowserView_.cursorIndex() + 1, candidateCount) : "";
  drawPromptLabel(kind, positionText);
  const std::string *selected = listBrowserMode_.selectedItem();
  promptEditor_.setText(selected ? *selected : "");
  promptEditor_.redraw();
}

void Application::activatePrompt(PromptKind kind, const std::string &initialText, const std::string &positionText,
                                  bool cursorAtStart, bool selectAllOnSeed) {
  promptKind_ = kind;
  // feedPromptByte()自身の^N/^P処理参照――空でないpositionTextは
  // feedRegexListByte()の編集への遷移によってのみ渡される(他の全ての
  // activatePrompt()の呼び出し箇所はそれを""のデフォルトのままにする)
  // ので、それは別個のパラメータ無しに「このプロンプトは、依然として
  // 見えている^J^S/^J^Cのリスト『から』候補を編集しているので、^N/^P
  // はそれを巡回し続けるべきである」を兼ねる。
  promptCandidateListActive_ = !positionText.empty();
  drawPromptLabel(kind, positionText);
  promptEditor_.setText(initialText);
  promptEditor_.clearUndo();  // 前のプロンプトの内容へは戻さない
  if (cursorAtStart) promptEditor_.moveHome();
  if (selectAllOnSeed) promptEditor_.selectAll();
  promptEditor_.redraw();
}

bool Application::executeGrep(const std::string &pattern) {
  // 検索自体が下で何かを見つけるかどうかに関わらず記録される――
  // なぜFGREPとEGREPが別々のringを得るのかについては
  // startPromptHistoryRecall()自身のコメント参照(legacy自身の
  // p.hfgrep/p.hegrepの分割に合わせている、promptLabelText()が
  // 既にFGREP/EGREPのモードラインのラベルを選ぶのと同じ方法で
  // grepSyntax_をキーにしている)。
  history_.pushRecent(grepSyntaxRingCategory(grepSyntax_), pattern);
  // 2026-08-18修正: 以前はハードコードされたFile対Directoryの三項
  // 演算子であり、grepがそこから起動され得る他の全てのモードについて
  // 黙って間違ったContentViewを読み(そして失敗時には再描画し)て
  // いた――実際の報告("^J^L'.txt'→=co→^S'.txt'→モードライン不良か
  // つ検索してない")により、LOCATEの結果から開始されたFGREPが、
  // locateView_が画面に残っている間に実際にはdirectoryView_の古い
  // コンテンツを検索し、それから失敗時にその上にdirectoryView_を
  // 再描画してモードラインを文字化けさせていたことが判明した。
  // viewForMode()(まさにこのために括り出された)は、File/Directory
  // だけでなく、どんなgrepReturnMode_についても正しいビューを選ぶ。
  ContentView &source = grepViewForMode(grepReturnMode_);
  std::vector<std::string> sourceLines;
  // 2026-09-11追加、直接のユーザー報告("^X^F^J^S^Uusilic^M→検索結果は
  // usilicが一件だけ"――62件あるはずのRecentFileの中に"usilic"を含む
  // パスは何件もあるのに、なぜか1件しか見つからない): EGREP
  // (GrepSyntax::Regex)は`startRegexPromptList()`が、grepReturnMode_
  // (=ListBrowser)を設定した「直後」に、それ自身の候補パターン一覧
  // (`RecentFgrepPattern`/`RecentEgrepPattern`)で`listBrowserView_`
  // (=`viewForMode(ListBrowser)`)の中身を上書きしてしまう――そのため
  // 実際にパターンを打ち込んでEnterする時点では、`source.lines()`は
  // もう`RecentFile`/`RecentDirectory`の62件では「なく」、EGREP自身の
  // 候補パターン一覧("Contents"/"Res"/.../"usilic"等)に置き換わって
  // いた。たまたま検索パターン"usilic"自身がその候補パターン一覧の
  // 中に文字通り1件存在していた(過去に検索したパターンとして記録
  // 済み)ため、「1件だけヒット」という、いかにも本物の検索結果らしい
  // 見た目の誤った結果を返していた。grepReturnListCategory_
  // (項目443)は`startGrepPrompt()`が上書きされる「前」に退避済みなので、
  // ListBrowserの場合はそれを使って`RecentFile`/`RecentDirectory`の
  // 一覧を都度再構築する(`listBrowserView_`の「現在の」表示内容を
  // 一切信用しない)ことで、この上書きの影響を受けない。
  if (grepReturnMode_ == ApplicationMode::ListBrowser) {
    sourceLines = sortedUniqueRecentEntries(grepReturnListCategory_);
  } else {
    sourceLines.reserve(source.lines().size());
    for (const auto &line : source.lines()) sourceLines.push_back(line.text);
  }

  if (!grepMode_.search(sourceLines, pattern, grepSyntax_)) {
    if (!redrawEpubVerticalForGrep(source)) source.redraw();
    return false;
  }
  // 2026-09-09追加、直接のユーザー提案(.steering/20260909-grep-inline-
  // highlight/requirements.md参照): isGrepInPlaceEligibleMode()(自身の
  // コメント参照)対象のモード起点の場合だけ、専用の一覧画面(下の
  // ApplicationMode::Grepへの遷移)へは切り替えず、in-place方式
  // (activateGrepInPlace()自身のコメント参照)へ分岐する。当初
  // (2026-09-09)はGrepSyntax::Basic(^S/^[i)限定、Directory/Fileの2
  // モードだけに限定していたが、2026-09-11、直接のユーザー依頼("^Sと
  // ^[iをFILE/DIR以外(現状GREPが稼働している全てのモード)へも反映して
  // ほしい")によりMan/Locate/Du/Archive/ArchiveContent/Shell/
  // CutContent/ListBrowser/Keyへ拡張した。さらに同日、EGREP検索結果
  // 一覧で選択してもFILE上の該当箇所へ飛ばずEGREP自身の候補一覧に
  // 戻ってしまうバグの報告をきっかけに、ユーザーから
  // "FGREPおよびEGREPを同じ仕様に統一して欲しい。違いは直値および
  // 拡張正規表現で検索が行われる点...候補一覧は廃棄"という統一依頼が
  // あり、GrepSyntax::Basicへの限定も撤廃した――FGREP(^O^S)/EGREP
  // (^J^S)も含め、grepSyntax_の値に関わらずisGrepInPlaceEligibleMode()
  // だけで判定する(startGrepPrompt()自身のコメントも参照、EGREP用の
  // 候補一覧起動(startRegexPromptList())自体をこの統一で廃止した)。
  if (isGrepInPlaceEligibleMode(grepReturnMode_)) {
    grepInPlaceOriginMode_ = grepReturnMode_;
    return activateGrepInPlace();
  }
  currentMode_ = ApplicationMode::Grep;
  // pendingSearchFromDirectEntryList_自身のコメント参照――値に関わら
  // ずここで(一回限り)消費されるので、後の再分割/空のプロンプトから
  // タイプされた検索が、古いtrueを引き継ぐことはない。
  grepReturnsToCandidateList_ = pendingSearchFromDirectEntryList_;
  pendingSearchFromDirectEntryList_ = false;
  return true;
}

bool Application::executeCut(const std::string &pattern) {
  // 下の結果/ネストの深さに関わらず記録される――なぜこれが専用の
  // ringではなくRecentFgrepPattern/RecentEgrepPatternを再利用する
  // のかについてはstartPromptHistoryRecall()自身のコメント参照
  // (legacy自身のCUTについてのss.p.hfgrep/hegrepの再利用に合わせて
  // いる)。2026-08-05の訂正: 同日の以前の改訂は、cutSyntax_に関わら
  // ず無条件にRecentEgrepPatternを使っていた――ユーザーが直接指摘
  // した実際のlegacyのバグ(menu.c自身の`case M_cut: case M_cut2:
  // hp->ss.p.hegrep = save_history(...)`は、同じswitchのすぐ上に
  // ある正しいパターンを示すFGREP/EGREPのブロックにも関わらず、
  // ここでM_cut2(^Jc、FIXED)対M_cut(^J^C、REGEX)で一度も分岐しない、
  // まさに同じことをしている): ^Jc自身のCUTはFGREPのように振る舞う
  // べきである(そして「その」履歴を共有する)、^J^Cのものは
  // EGREPのように、そうしないと、既にFGREP/EGREPを知っているユーザー
  // は、CUTが同じ固定対regexの分割に従うことを合理的に期待し、
  // そうならないと混乱する。ここでC++側へ移植された、そして
  // legacyのCソース自体(menu.c)へも「戻って」移植された――その修正
  // 自身のコメント参照。
  history_.pushRecent(cutSyntax_ == GrepSyntax::Fixed ? RingCategory::RecentFgrepPattern
                                                       : RingCategory::RecentEgrepPattern,
                       pattern);
  bool ok;
  std::string fileSourcePath = std::move(cutFileSourcePath_);
  cutFileSourcePath_.clear();  // 下のどちらの分岐が実行されるか、その結果に関わらず正確に一度消費される
  // saveCutFilePosition()自身のキー: ネストした再分割以外は新しい一覧
  // なので、下のファイルの分岐が設定し直すまで空にする。
  if (currentMode_ != ApplicationMode::CutContent && currentMode_ != ApplicationMode::Cut) {
    cutPositionKey_.clear();
    cutLastContent_.reset();
  }

  if (currentMode_ == ApplicationMode::CutContent || currentMode_ == ApplicationMode::Cut) {
    // ネストした再分割――cutOrigin_には手を付けない、クラスコメント
    // 参照。「既に選択されたセグメント自身のコンテンツを見ている」
    // (CutContent)と「依然としてラベルの『リスト』自体にいて、
    // カーソルはあるエントリの上」(Cut)の両方が、同一にここに着地
    // する、後者がなぜ欠けていたのかについてはstartCutPrompt()自身
    // の2026-08-05のコメント参照。
    ok = cutMode_.splitSelected(pattern, cutSyntax_);
  } else if (!fileSourcePath.empty()) {
    // K-35/K-36の「通常ファイル」亜種: まずファイルをロードし
    // (通常それを開くのと同様に)、それから今や現在のバッファに
    // なったものを「分割する」――startCutFilePrompt()自身のコメント
    // 参照。すぐ下のプレーンなDirectoryの分岐より「前に」チェック
    // される: startCutFilePrompt()も、currentMode_が依然として
    // Directoryのまま呼び出される(それ自身のプロンプトもcurrentMode_
    // を一切変えない)ので、fileSourcePathが空でないことが、実際に
    // 「一覧自体を分割する」ことから「この選択されたファイルを分割
    // する」ことを区別するものである――どちらもまさに同じ
    // currentMode_から到達可能である。
    // 2026-09-29修正、openRecentFileEntry()自身の同日のコメント参照:
    // 以前はcutReturnMode_に手を付けず、FILEから^Cfした場合や直前の
    // ^Cbの後では、^GがCUTしたファイル自身のFILE画面へ着地していた
    // (^Cb扱い)。離れるFILE自身の位置もここで保存する。
    if (currentMode_ == ApplicationMode::File) fileMode_.saveCursorPosition();
    cutReturnMode_ = cutFileReturnModeFor(currentMode_);
    if (!fileMode_.loadFile(fileSourcePath)) {
      activeView().redraw();
      return false;
    }
    std::vector<StyledLine> sourceLines;
    sourceLines.reserve(fileView_.lines().size());
    for (const auto &line : fileView_.lines()) sourceLines.push_back({line.text, line.attributes});
    // 2026-08-18修正: split()の返り値をチェックした後ではなく、それを
    // 呼ぶ前に設定する――なぜかについてはexecuteStartLog()自身の発見
    // のコメント参照(CutMode::split()は、この関数が実行される機会を
    // 一度も得る前に、内部でcutView_を再描画する;その後でcutOrigin_
    // を設定すると、この再描画全体について、モードラインは「以前の」
    // cutOrigin_がたまたま何であれそれを表示したままになり、後の、
    // 無関係な再描画で初めて自己修正していた)。無害
    // 最終的な失敗時にでも設定しても無害――split()内部の全ての失敗
    // 経路は、それ自身のredraw()より前にreturnするので、ここでの
    // 「無駄になった」代入を使って何かがレンダリングされることは
    // 決して無い。
    cutOrigin_ = CutOrigin::File;
    ok = cutMode_.split(sourceLines, pattern, cutSyntax_);
    // 2026-09-20追加、直接のユーザー依頼(QUICK.txt自身のC-j bの
    // 説明"後述[FILE]によるアクセス履歴一覧では「ファイル名 "文字列"」
    // で表示され、選択すると文字列行の目次一覧画面となる"): 通常の
    // ファイルパス単体のRecentFileエントリ(fileMode_.loadFile()自身が
    // 既に無条件に積んでいる)とは別に、「パス "パターン"」という
    // 合成形のエントリも積む――openRecentFileEntry()自身のコメント
    // 参照、この形をFILE履歴から選び直すとCUT自体を再実行する。
    if (ok) {
      std::string entry = buildCutFileHistoryEntry(fileSourcePath, pattern, cutSyntax_);
      history_.pushRecent(RingCategory::RecentFile, entry);
      restoreCutFilePosition(entry);
    }
  } else if (cutOrigin_ == CutOrigin::File && !fileMode_.currentFile().empty()) {
    // 2026-08-24修正、実際の報告+直接の追跡質問("戻り値が正常化
    // さえ実現すれば直る問題じゃないの？"――^Gが正しく再びDirectory
    // に着地すれば、「これ」もそれで直るのでは?): 新しいファイル
    // セレクタでの選択(fileSourcePathは空)ではなく、元々File起点の
    // 検索(^Jb/^J^B)に使われたパターンを^Rで呼び戻すことで到達する
    // ――例えばユーザーが(cutReturnMode_に従って正しく)Directoryへ
    // 完全に戻ってから、そこから^Rを押した。まさにこの瞬間の
    // currentMode_==Directoryは、以前は下の分岐を発火させ、呼び戻された
    // パターンを黙って「代わりにディレクトリの『一覧』を検索する」
    // と再解釈していた――技術的にはcurrentMode_と整合しているが、
    // 「File検索を再開/再適用する」ことの意味ではない、そしてユーザー
    // が正しく指摘したのは、これが実際には履歴ringに何か「新しい」
    // パターンごとの起点の記帳を必要としないことである: cutOrigin_
    // とfileMode_.currentFile()は、元の検索とこの^R呼び出しの間ずっと、
    // 完全に変わらず「両方とも」そこに座ったままである――それ以来
    // どちらにも何も触れていない(本当に新しいDirectory起点のアーム、
    // DirectoryからのstartCutPrompt()が、今や事前に明示的にcutOrigin_
    // をDirectoryへ切り替えるものである、それ自身のコメント参照――
    // そのため、cutOrigin_が依然としてFileのままここに到達すること
    // は、「それ以来一度も新しいDirectory検索がアームされていない」
    // ことを曖昧さ無く意味する、「今たまたまDirectoryである」ことで
    // はない)。currentMode_から再導出するのではなく、それらを直接
    // 再利用することが実際の修正である。
    std::vector<StyledLine> sourceLines;
    sourceLines.reserve(fileView_.lines().size());
    for (const auto &line : fileView_.lines()) sourceLines.push_back({line.text, line.attributes});
    ok = cutMode_.split(sourceLines, pattern, cutSyntax_);
  } else if (currentMode_ == ApplicationMode::Directory) {
    // 2026-08-11: ディレクトリ一覧に対して直接実行されたCUT
    // (Directoryモードからの^J^C、startCutPrompt()自身のコメント
    // 参照)――grepReturnMode_ == Directoryの時にGrepMode::search()が
    // directoryView_.lines()を読むのを反映している、「一覧自体が
    // バッファである」という同じ発想。ディレクトリ一覧の行はオーバー
    // ストライクの属性を決して持たない(ContentView::Line::attributes
    // 自身のコメント参照)ので、ここでの`.attributes`は常に空である
    // ――それでも他の分岐との構造的な対称性のためだけに素通しされる。
    std::vector<StyledLine> sourceLines;
    sourceLines.reserve(directoryView_.lines().size());
    for (const auto &line : directoryView_.lines()) sourceLines.push_back({line.text, line.attributes});
    cutOrigin_ = CutOrigin::Directory;  // 上のFileの分岐自身の2026-08-18のコメント参照
    ok = cutMode_.split(sourceLines, pattern, cutSyntax_);
  } else if (currentMode_ == ApplicationMode::Man) {
    // 2026-09-15追加、直接のユーザー方針("CUT、F/EGREPは[MAN、
    // LOCATE、DU等に]関与（検索）できる"): すぐ上のDirectory分岐と
    // 全く同じ発想――「一覧/画面自体がバッファである」――で
    // manView_.lines()を分割する。
    std::vector<StyledLine> sourceLines;
    sourceLines.reserve(manView_.lines().size());
    for (const auto &line : manView_.lines()) sourceLines.push_back({line.text, line.attributes});
    cutOrigin_ = CutOrigin::Man;
    ok = cutMode_.split(sourceLines, pattern, cutSyntax_);
  } else if (currentMode_ == ApplicationMode::Locate) {
    std::vector<StyledLine> sourceLines;
    sourceLines.reserve(locateView_.lines().size());
    for (const auto &line : locateView_.lines()) sourceLines.push_back({line.text, line.attributes});
    cutOrigin_ = CutOrigin::Locate;
    ok = cutMode_.split(sourceLines, pattern, cutSyntax_);
  } else if (currentMode_ == ApplicationMode::Du) {
    std::vector<StyledLine> sourceLines;
    sourceLines.reserve(duView_.lines().size());
    for (const auto &line : duView_.lines()) sourceLines.push_back({line.text, line.attributes});
    cutOrigin_ = CutOrigin::Du;
    ok = cutMode_.split(sourceLines, pattern, cutSyntax_);
  } else {
    std::vector<StyledLine> sourceLines;
    sourceLines.reserve(fileView_.lines().size());
    for (const auto &line : fileView_.lines()) sourceLines.push_back({line.text, line.attributes});
    // 新規の最上位の分割――リサイズ時のリフローの対象になる。順序に
    // ついては上のFileの分岐自身の2026-08-18のコメント参照。
    cutOrigin_ = CutOrigin::File;
    ok = cutMode_.split(sourceLines, pattern, cutSyntax_);
  }

  if (!ok) {
    activeView().redraw();
    return false;
  }
  currentMode_ = ApplicationMode::Cut;
  // pendingSearchFromDirectEntryList_自身のコメント参照――値に関わら
  // ずここで(一回限り)消費されるので、後の再分割/空のプロンプトから
  // タイプされた検索が、古いtrueを引き継ぐことはない。
  cutReturnsToCandidateList_ = pendingSearchFromDirectEntryList_;
  pendingSearchFromDirectEntryList_ = false;
  // ^Cf(ファイルを選んでCUT)でも、前に同じファイル・パターンで見ていた区間を開く
  // (openRecentFileEntry()の同日のコメント参照)。
  if (!fileSourcePath.empty() && cutOrigin_ == CutOrigin::File) reopenLastCutContent();
  return true;
}

void Application::reopenLastCutContent() {
  if (!cutLastContent_ || cutOrigin_ != CutOrigin::File || currentMode_ != ApplicationMode::Cut) return;
  auto [row, line] = *cutLastContent_;
  int lastSegment = static_cast<int>(cutMode_.segments().size()) - 1;
  if (lastSegment < 0) return;
  cutView_.setCursorIndex(std::clamp(row, 0, lastSegment));
  cutView_.centerOnCursor();
  if (!openSelectedSegment()) return;
  int last = static_cast<int>(cutContentView_.lines().size()) - 1;
  cutContentView_.setCursorIndex(std::max(0, std::min(line, last)));
  cutContentView_.centerOnCursor();
  cutContentView_.redraw();
  cutContentView_.syncVisibleCursor();
}

bool Application::executeMan(const std::string &name) {
  manReturnsToUnderline_ = false;  // USILIC一覧から開いた場合だけ、呼び出し元が立て直す
  manReturnsToCandidateList_ = false;  // 一覧から開いた場合だけ、呼び出し元(openSelectedListEntry())が立て直す
  history_.pushRecent(RingCategory::RecentManPage, name);  // 下のloadPage()自身の結果に関わらず記録される
  if (!manMode_.loadPage(name)) {
    activeView().redraw();
    return false;
  }
  currentMode_ = ApplicationMode::Man;
  return true;
}

// executeProgram()とsetProgramLauncher()自身のラムダで共有される
// (2026-08-17追加、後者が追加された時に括り出された): 何らかの
// PROGRAM指示のエントリ自身の最初の単語とのstrncmp接頭辞マッチ
// (find_prog()、sub.c自身の意味論)。
//
// "usilic"自身は常に無条件で認識済み扱いにする(2026-09-10、直接の
// ユーザー指摘): SHELLモード(Shell::start()自身のsetenv("USILIC",
// "1", ...)のコメント参照)は、自分自身の再帰起動を、usilicrc経由の
// ユーザー設定に一切頼らず、バイナリ自身に組み込まれた特別扱いとして
// 認識している――ユーザーが打ち込む先が「任意の外部ツール」ではなく
// この実行ファイル自身だと分かっている以上、usilicrcのPROGRAM一覧へ
// 事前登録することを要求する理由が無い。PROGRAM(^Jp)だけこの慣習から
// 外れ、他の外部プログラムと同列にusilicrcへの登録を要求していたのは
// 単なる見落としだった(実際の報告: `^Jpusilic ...`が無反応で単に
// 元の画面に戻るだけに見えた――usilicrcのPROGRAM一覧に"usilic"が
// 元々含まれていなかったため)。
bool Application::isRecognizedProgram(const std::string &firstWord) const {
  if (firstWord == "usilic") return true;
  for (const auto &seed : configuration_.programSeedCommands()) {
    std::size_t seedEnd = seed.find_first_of(" \t");
    std::string seedFirstWord = seed.substr(0, seedEnd);
    if (!seedFirstWord.empty() && firstWord.compare(0, seedFirstWord.size(), seedFirstWord) == 0) return true;
  }
  return false;
}

// executeProgram()とsetProgramLauncher()自身のラムダで共有される:
// usilic自体は決してchdir()しない(executePathInput()自身のコメント
// 参照)ので、素の/相対のファイル名を参照するコマンドは、実際の
// 子プロセスが先に正しいディレクトリへ`cd`されている場合にのみ正しく
// 解決される――ChildProcess::runInteractive()(新規のfork/exec)も
// sh -cも、directoryMode_やShell自身の純粋に論理的な「現在のディレク
// トリ」の記帳から何も継承しない。POSIXのシングルクォートのエスケープ
// (クォートを閉じ、エスケープされたリテラルなクォートを出し、再度
// 開く)は、埋め込まれた「'」を含むディレクトリパスを正しく扱う;
// 単純な'<dir>'で包むだけでも、はるかによくあるケース(スペース)は
// 既に扱われる。`dir`が空はno-op(コマンドは変わらない)である――
// 解決すべきディレクトリの文脈が無い呼び出し元(例えば
// startProgramPrompt()がファイルを見つけられなかった場合)は、
// プロセスのcwdをそのままにする、既存の挙動である。
std::string Application::cdWrap(const std::string &dir, const std::string &command) {
  if (dir.empty()) return command;
  std::string quoted = "'";
  for (char c : dir) {
    if (c == '\'')
      quoted += "'\\''";
    else
      quoted += c;
  }
  quoted += "'";
  return "cd " + quoted + " && " + command;
}

// mode.cのM_progケースから移植(isRecognizedProgram()自身のコメント
// 参照)。認識された場合はProgramMode::run()経由でタイプされた行
// 全体を実行する;どちらにせよApplicationModeは決して変わらない
// (クラスコメント参照)。
bool Application::executeProgram(const std::string &command) {
  std::size_t start = command.find_first_not_of(" \t");
  if (start == std::string::npos) {
    activeView().redraw();
    return false;
  }
  std::size_t end = command.find_first_of(" \t", start);
  std::string firstWord = command.substr(start, end == std::string::npos ? std::string::npos : end - start);

  if (!isRecognizedProgram(firstWord)) {
    activeView().redraw();
    return false;
  }

  // 2026-09-17追加、実機報告("D. が直ってない")+提案(runNestedUsilicProgram()
  // 自身のapplication.hpp宣言コメント参照): "usilic"だけは通常の
  // programMode_.run()を経由せず、ランデブーのポーリングに参加できる
  // 専用の起動経路を使う。
  if (firstWord == "usilic") return runNestedUsilicProgram(command);

  // programPromptDirectory_: それ自身の宣言コメント参照――
  // startProgramPrompt()の素のファイル名の事前入力(例えば
  // "CLAUDE.md")には、このcdの包みが必要である;ファイルの文脈が
  // 見つからなかった時は常に空(no-op)、手でタイプされたコマンド
  // についての既存の挙動に合わせている。
  std::string toRun = cdWrap(programPromptDirectory_, command);
  programPromptDirectory_.clear();  // 結果に関わらず正確に一度消費される

  display_.flush();
  programMode_.run(toRun, command);
  activeView().redraw();
  return true;
}

// Command::QuickLook(実験的、その列挙子自身のコメント参照)。
// executeProgram()のisRecognizedProgram()/programPromptDirectory_/
// プロンプトの仕組み全体を意図的に迂回する――ここにはユーザーが
// タイプしたコマンドラインは無く、カーソルの下にたまたまある実際の
// ファイルシステムパスから直接組み立てられる、ハードコードされた
// 1つの`qlmanage -p <path>`だけがある、そのためそれらは何も当ては
// まらない。単一の既に分かっているパスについてシェルのcwdを経由
// する理由が無いので、(cdWrap()の「先にディレクトリへcdする」という
// 仕掛けではなく)完全な絶対パス自体を組み立てる。
//
// そのパスをモードごとに解決する(2026-08-23の拡張、これが答える
// 正確な依頼についてはその列挙子自身のコメント参照):
//   - Directory: カーソルの下のエントリ。
//   - Locate: LocateMode自身の選択された結果――既に素の絶対パス、
//     `locate`(1)自身の出力形式。
//   - Grep、ただしgrepReturnMode_ == Directoryの時「だけ」: 「ファイル」
//     自身のコンテンツに対するGrepの実行は「行」にマッチしたのであり、
//     別々のファイルではないので、そこには解決すべきマッチごとの
//     パスが無い――しかしDirectoryの「一覧」に対するGrepの実行
//     (`ls`自体に対するFGREP/EGREP)は、各マッチのsourceLineIndexが
//     directoryMode_.entries()と1対1で並ぶ(executeGrep()自身の
//     コメント参照: Directory起点についてGrepMode::search()へ与え
//     られる行は、entries_自体とまさに同じループで構築される
//     directoryView_から直接読まれる、DirectoryMode::enterDirectory()
//     参照)。
// 意図的にカバーされていないもの: Archive/ArchiveContent(メンバー
// 自身の「パス」は仮想的である――実際に展開されるまでqlmanageに
// 渡すべきディスク上の何も無い);Du(各行はディレクトリの集計
// 「サイズ」であり、ユーザー自身の依頼が表現していたような「通常
// ファイル」とは実質的に言えない);そしてListBrowserのRecentFile/
// RecentDirectoryの候補は、そこでのselectedItem()が既に素の実際の
// パスで「ある」にも関わらず――その2つのカテゴリは「生きたコンボ
// ボックス」のもの(feedRegexListByte()自身のコメント参照)であり、
// プレーンなSpaceは既に「リテラルなスペース文字から始めて候補の
// テキストの編集を開始する」ことに意味を持って結び付けられている
// (実際のパスは1つ含み得るので有用である);Quick Lookはそれを黙って
// 奪ってしまうことになる。このディスパッチの地点(dispatchByte()の
// 末尾)は、そもそもその2つのカテゴリについては一切到達されない
// ――feedRegexListByte()が、dispatchByte()のより上流で最初に横取り
// する――そのためここで解決すべき本物の衝突は一度も無く、単にこの
// ポートが踏み込まないことを選んだ範囲というだけである。
bool Application::executeQuickLook() {
  std::vector<std::string> paths;
  if (currentMode_ == ApplicationMode::Directory) {
    // マークされたエントリ(targetEntries()、CP/MV/RM/PROGRAM自身の
    // ターゲット収集が読むのと同じソース――collectTargetPaths()/
    // startProgramPrompt()参照)は、単一のカーソルエントリより優先
    // される。2026-08-25、Quick Lookを同じdired方式のマーキングの
    // 流れ('.'/'['/']')に結びつける。targetEntries()自体は、何も
    // マークされていない時は単なる孤立したカーソルエントリへフォール
    // バックするので、マークが無い時は既存の単一ファイルの挙動
    // (と下のテスト)は変わらない。`qlmanage -p`は複数のパスをネイ
    // ティブに受け付ける(それらを閲覧可能なギャラリーとして表示
    // する)ので、`paths`が組み立てられた後、それ以上の分岐は不要
    // である。
    std::string dir = directoryMode_.currentDirectory();
    for (const auto &entry : directoryMode_.targetEntries()) {
      std::string p = dir;
      if (!p.empty() && p.back() != '/') p += '/';
      p += entry.name;
      paths.push_back(p);
    }
    if (paths.empty()) return false;
  } else if (currentMode_ == ApplicationMode::Locate) {
    // 2026-09-27、選択があればその全部(listTargetPaths())。
    paths = listTargetPaths();
    if (paths.empty()) return false;
  } else if (currentMode_ == ApplicationMode::Grep && grepReturnMode_ == ApplicationMode::Directory) {
    const GrepMatch *match = grepMode_.selectedMatch();
    const auto &entries = directoryMode_.entries();
    if (!match || match->sourceLineIndex < 0 ||
        static_cast<std::size_t>(match->sourceLineIndex) >= entries.size())
      return false;
    std::string p = directoryMode_.currentDirectory();
    if (!p.empty() && p.back() != '/') p += '/';
    p += entries[match->sourceLineIndex].name;
    paths.push_back(p);
  } else if (currentMode_ == ApplicationMode::Underline) {
    // 2026-09-25追加、直接のユーザー要望("^Ouの一覧でクイックルックは
    // できる？"): ハイライト行のパス。MANの項目(" (man)")やUSILIC
    // 目次の見出し("MAN"/"DIR"/"FILE")等、実在しないものは対象外。
    // 同日追記("（ls）同様、複数指定のクイックルック"): '.'/'['/']'で
    // 選択があればその全部(UnderlineMode::targetEntries())。
    if (usilicLevel_ == UsilicLevel::Toc) return false;
    for (const auto &entry : underlineMode_.targetEntries()) {
      std::error_code ec;
      if (std::filesystem::exists(entry, ec)) paths.push_back(entry);
    }
    if (paths.empty()) return false;
  } else if (currentMode_ == ApplicationMode::Du) {
    // 2026-09-25追加、直接のユーザー要望("実行ファイル一覧以外のリスト
    // 上でも可能なら"): DUの結果のハイライト行(DuEntry::pathは
    // openSelectedDuEntry()がそのまま開いている絶対パス)。^X^F/^X^Dの
    // 履歴一覧は、打った文字がそのままパスの直接編集になる
    // (feedRegexListByte())ため、スペースを含むパスを打てなくなる
    // Spaceの横取りはしない。PROGRAM/CUT/MAN等、ファイルを指さない
    // 一覧も対象外。
    // 2026-09-27、選択があればその全部(listTargetPaths())。
    paths = listTargetPaths();
    if (paths.empty()) return false;
  } else if (currentMode_ == ApplicationMode::Archive) {
    // 2026-09-27追加、直接のユーザー依頼("ARCは？ファイル操作は不要だが
    // クイックルックはどうかな"): ハイライト行のメンバーを一時ディレクトリ
    // (<workRoot>/ql)へ取り出してから渡す(^Mで開く時と同じextractSelected())。
    // ディレクトリや、中のアーカイブ(^Mで潜る対象)は対象外。
    const ArchiveEntry *entry = archiveMode_.selectedEntry();
    if (!entry || entry->isDirectory) return false;
    ArchiveKind nestedKind = ArchiveKind::None;
    if (configuration_.programForSuffix(entry->name, &nestedKind) && nestedKind != ArchiveKind::None) return false;
    auto content = archiveMode_.extractSelected();
    if (!content) return false;
    capBufferContent(*content);
    std::string tempPath = materializeArchiveTempFile(workRootDirectory_ + "/ql", entry->name, *content);
    if (tempPath.empty()) return false;
    paths.push_back(tempPath);
  } else {
    return false;
  }
  return runQuickLook(paths);
}

// executeQuickLook()の後半(2026-09-27に括り出した): `paths`を`qlmanage -p`へ
// 渡し、PROGRAMの履歴へ「qlmanage -p ベース名...」で記録する。
bool Application::runQuickLook(const std::vector<std::string> &paths) {
  // 分割する前にディレクトリ自身の末尾の"/"(DirectoryEntry::name
  // 自身の慣習)を取り除く、そのため記録されるラベルは"cpmv_a"
  // スタイルのベース名であり、空(末尾がスラッシュのパスに対する
  // std::filesystem::path::filename()は""を返す)にはならない。
  // 単一パスのラベルの形式("qlmanage -p <basename>")は以前から変
  // わっていない;複数パスの場合は、単にさらに各ベース名をスペース
  // 区切りで追加するだけである。
  std::string label = "qlmanage -p";
  for (const auto &path : paths) {
    std::string trimmed = path;
    if (!trimmed.empty() && trimmed.back() == '/') trimmed.pop_back();
    std::size_t slash = trimmed.find_last_of('/');
    label += " " + (slash == std::string::npos ? trimmed : trimmed.substr(slash + 1));
  }

  display_.flush();
  programMode_.run("qlmanage -p " + shellQuoteArgv(paths), label);
  activeView().redraw();
  return true;
}

// 2026-08-13(docs/ideas/IDEAS.mdのLOCATEセクション、CLAUDE.mdの経緯
// の項目288): `query`がringの中の直前のエントリとまさに同じ場合
// (つまりユーザーがstartLocatePrompt()のシードを変更せずに投入
// した)、`locate`は「再実行されない」――LocateMode自身の以前の
// 検索からのresults_/view_は、単に依然としてそこにあるので、
// currentMode_をLocateへ切り替えることで、それらをそのまま再表示
// する。pushRecent()(そうしないとringのback()を先に`query`へ動か
// し、これを常に「変わっていない」ように見せてしまう)より「前に」
// チェックされる。クエリが変わった場合は、この変更より前と同様、
// 通常の新規の検索を実行する。
bool Application::executeLocate(const std::string &query) {
  // 2026-09-11修正、直接のユーザー報告(docs/CHANGES.md項目439参照:
  // 「usilic -c（引数無し）で起動→^J^Lで--が出る」がselectAllOnSeed
  // 修正後もなお再現): 以前はring(RecentLocateQuery、`~/.usilic/
  // history`から前回セッションをまたいで復元される)のback()とだけ
  // 比較していたが、これは「このプロセスでLOCATEを一度も実行して
  // いない」場合(locateMode_.query()が空、locateView_も未着手の
  // ままemptyPlaceholderLine_の既定値"--"を表示するだけ)にも
  // 「変更無し」と誤判定してしまう――前回セッションが残した履歴上の
  // クエリと、事前入力されたシードがたまたま同じ文字列である限り、
  // ユーザーが実際にはEnterで初めてそのクエリを投入したにも
  // 関わらず、search()自体を一切実行せずlocateMode_の(空のままの)
  // results_/locateView_を再表示するだけになっていた。
  // locateMode_自身がまさに直前に検索したクエリ(query_、search()の
  // 中でのみ設定される)と比較することで、「このプロセスで実際に
  // このクエリを検索済みで、その結果が今も有効」という本来の意図
  // した不変条件に一致させる。
  bool unchanged = query == locateMode_.query() && !locateMode_.query().empty();
  history_.pushRecent(RingCategory::RecentLocateQuery, query);  // 下のsearch()自身の結果に関わらず記録される
  if (unchanged) {
    currentMode_ = ApplicationMode::Locate;
    // search()は通常、実際の実行の最後にlocateView_自体を再描画する
    // (locate_mode.cpp);ここでsearch()をスキップすることは、これが
    // 新たにアクティブになったビューが得る唯一の再描画であることを
    // 意味する。
    locateView_.redraw();
    return true;
  }
  if (!locateMode_.search(query)) {
    activeView().redraw();
    return false;
  }
  currentMode_ = ApplicationMode::Locate;
  return true;
}

// m_dest()のT_cp/T_mvの分岐から移植(宛先の検証は単純化されて省かれ
// ている――実際のcp/mvの呼び出しは、今はshell_経由で実行され
// (下記参照)、代わりに失敗は通常のシェルの出力として表面化する)。
// 2026-08-22修正(最初の版、下の2回目に置き換えられる): 実際の報告
// ――"処理過程はSHELLに残してもらわないと困る。あとで参照もできない
// し、もしも-iオプション付きだったら（デフォルトはそう）その場で
// フリーズするんじゃない？"――により、K-52("SHELLウィンドウ無し")
// が、CpMvRmMode::copyTo()、キャプチャされた/非対話的な
// ChildProcess::run()経由で実行され、stdinがプロンプトに答えられる
// 何にも接続されていないことが判明した;Configuration自身のデフォ
// ルトの`cp`ロールのコマンド(src/config.sh)は`-i`を含むので、既存
// の宛先ファイルは黙って永遠にブロックしてしまう。最初の修正: K-52
// もcopyToInteractive()(K-51自身のrunInteractive()、usilic自身の
// 端末から直接継承された本物のpty)経由でルーティングする――これは
// 確かに`-i`のプロンプトに答えられるようにしたが、追跡報告
// ("CP/MV/RM:SHELLでの実行が反映されていない（LOGが空白）")により、
// その実行自身の出力が依然として一度もshell_のscrollback(^X^Kl/LOG
// 自身のデータソース、refreshShellLogFile()参照)に届いていないこと
// が判明した――runInteractive()は、usilicの生の端末のfdを直接継承
// する素のfork()+execvp()であり、(^J.が所有する実際の持続的な
// bashセッションである)shell_を完全に迂回する――そのためLOGが表示
// すべきものがそもそも一度も無かった。2回目の、現在の修正: 本物の
// cp/mv/rmのコマンドラインをshell_自体へ注入し
// (shell_.commandLine().setText()+submitLine()、
// shellChangeDirectory()/shellRunLsL()が既に使っているのとまさに
// 同じ仕組み)、Shellモードへ切り替える――コマンド自身の-vの出力
// (と、他のどんなSHELLの入力とも全く同様に、プレーンな"y"/"n"の
// 行をタイプして答える、あらゆる-iのプロンプト)は、今や本当に
// shell_のscrollbackに生きているので、LOG/後のSHELLへの訪問がそれを
// 本当に表示する。完了は非同期に検出される
// (pollPendingCpMvRmShellRun()、実行ループの繰り返しのたびに呼ばれる
// ――なぜここでブロックする待機では「ない」のかについては、それ
// 自身のコメント参照: -gのGUIループについて
// pollLocateUpdateDbCompletion()が既に学んだのとまさに同じ教訓)、
// これはまた「宛先ディレクトリに着地する」ステップ(実際の報告+
// スクリーンショット: legacy自身のcommand()/M_shellの再起動の仕組み
// が結局そこに至る、"最後には/tmpの(ls)になる"に合わせている)が
// 最終的に実行される時でもある。K-51("SHELLウィンドウ付き")と
// K-52は、こうして今や1つの仕組みに完全に統一された――legacy自身の
// 唯一の区別(無条件にセッションをフォアグラウンドに持ってくるか、
// 必要な時だけかどうか)は「常にそれを表示する」に潰れる、その1つの
// 見た目の違いを再現するより、LOG/フリーズの安全性の保証を優先する
// という公表された単純化である。K-51自身のキー割り当て(^X^Kc、
// 「SHELLウィンドウ付き」)は完全に削除された(2026-08-23、この統一
// によってそれが^Xcと機能的に同一の重複になった時点での直接のユー
// ザー依頼)――^Xcが今や唯一の入り口である。
// 2026-08-27修正、実際の報告+再現("'saved'[return]→「saved/
// （ls）」にならない（~/usilic/（ls）のままかつカーソル位置が
// 「usilic-h.h」でなく最上行）"): 「相対の」宛先(例えば"/tmp"ではなく
// "saved")は、以前はそのまま注入されるシェルのコマンドラインへ直接
// 渡されていた――Shell::start()は、directoryMode_自身の
// currentDirectory_に合わせて決してchdir()しない(それは単に
// fork()経由でusilic自身のプロセスのcwdを継承するだけである)ので、
// 素の相対の宛先は、明らかに意図されていた「このディレクトリ」では
// なく、シェルセッションの「実際の」cwdがたまたまどこであれ、そこ
// に対して解決され、実際のcp/mvは黙ってソースの近くではないどこかに
// 着地する(あるいは完全に失敗する)。argvを構築する「前に」ここで
// currentDirectory()に対して解決するので、本物のシェルコマンドと
// pollPendingCpMvRmShellRun()自身の後のUIの着地ステップ(こちらも
// 絶対パスを必要とする、それ自身のコメント参照)の両方が、シェル
// 自身のcwdに関わらず、常に同じ曖昧さの無い絶対パスを見る。
std::string Application::resolveCpMvDestination(const std::string &destination) const {
  if (destination.empty() || destination.front() == '/') return destination;
  std::string base = directoryMode_.currentDirectory();
  // 一覧から発動した場合(2026-09-27)の相対の宛先は、先頭の対象の親ディレクトリ基準。
  if (cpMvRmOrigin_ != ApplicationMode::Directory && !cpMvRmListTargets_.empty())
    base = std::filesystem::path(cpMvRmListTargets_.front()).parent_path().string();
  if (!base.empty() && base.back() != '/') base += '/';
  return base + destination;
}

// CP/MV/RMの失敗時等の戻り先ディレクトリ。一覧から発動した場合(2026-09-27)は
// 先頭の対象の親ディレクトリ。
std::string Application::cpMvRmSourceDirectory() const {
  if (cpMvRmOrigin_ != ApplicationMode::Directory && !cpMvRmListTargets_.empty())
    return std::filesystem::path(cpMvRmListTargets_.front()).parent_path().string() + "/";
  return directoryMode_.currentDirectory();
}

bool Application::executeCopy(const std::string &destination) {
  std::vector<std::string> targets = collectTargetPaths();
  if (targets.empty() || destination.empty()) {
    activeView().redraw();
    return false;
  }
  std::string resolvedDestination = resolveCpMvDestination(destination);
  std::vector<std::string> argv = configuration_.commandFor(ConfiguredRole::Cp).argv;
  if (argv.empty()) {
    activeView().redraw();
    return false;
  }
  for (const auto &target : targets) argv.push_back(target);
  argv.push_back(resolvedDestination);
  // 2026-09-08追加、DirectoryMode::setTransientMarkedNames()自身の
  // コメント参照("CP/MV/ARCモードで...対象ファイルおよびディレク
  // トリを^[Pへも渡せるようアンダーライン表示して欲しい"): 着地先
  // ディレクトリの中では、コピー元のベースネームのままのはず(宛先が
  // 実在のディレクトリであることは、この後pollPendingCpMvRmShellRun()
  // 自身のdescendTo()成功チェックで保証される――単一ファイルの
  // リネームコピーのようにdescendTo()が失敗するケースでは、そもそも
  // 一時マークは設定されない)。
  std::vector<std::string> targetBasenames;
  targetBasenames.reserve(targets.size());
  for (const auto &target : targets)
    targetBasenames.push_back(stripLsFTypeMarker(std::filesystem::path(target).filename().string()));
  return startCpMvRmShellRun(argv, resolvedDestination, cpMvRmSourceDirectory(), targetBasenames);
}

// executeCopy()と同じ2026-08-22の再設計――それ自身のコメント参照。
bool Application::executeMove(const std::string &destination) {
  std::vector<std::string> targets = collectTargetPaths();
  if (targets.empty() || destination.empty()) {
    activeView().redraw();
    return false;
  }
  if (!checkTargetsWritable(targets)) return false;
  std::string resolvedDestination = resolveCpMvDestination(destination);
  std::vector<std::string> argv = configuration_.commandFor(ConfiguredRole::Mv).argv;
  if (argv.empty()) {
    activeView().redraw();
    return false;
  }
  for (const auto &target : targets) argv.push_back(target);
  argv.push_back(resolvedDestination);
  // executeCopy()自身の直上のコメント参照(2026-09-08追加)。
  std::vector<std::string> targetBasenames;
  targetBasenames.reserve(targets.size());
  for (const auto &target : targets)
    targetBasenames.push_back(stripLsFTypeMarker(std::filesystem::path(target).filename().string()));
  return startCpMvRmShellRun(argv, resolvedDestination, cpMvRmSourceDirectory(), targetBasenames);
}

// m_dest()のT_rmの分岐から移植: `confirmation`はまさに"y"でなければ
// ならず(legacyの`*ap->mess != 'y'`チェックに合わせている)、そうで
// なければ何も削除されない。executeCopy()と同じ2026-08-22の再設計
// ――それ自身のコメント参照(src/config.sh自身のデフォルトのRM
// ロールコマンド、"rm -vri"も`-i`であり、CPとまさに同じフリーズ/
// LOG可視性の隙間を持っていた)。RMには宛先が無い――完了すると常に
// ソースディレクトリへ戻って着地する(startCpMvRmShellRun()自身の
// 空の宛先の分岐)。
bool Application::executeRemove(const std::string &confirmation) {
  std::vector<std::string> targets = collectTargetPaths();
  if (targets.empty() || confirmation != "y") {
    activeView().redraw();
    return false;
  }
  if (!checkTargetsWritable(targets)) return false;
  std::vector<std::string> argv = configuration_.commandFor(ConfiguredRole::Rm).argv;
  if (argv.empty()) {
    activeView().redraw();
    return false;
  }
  for (const auto &target : targets) argv.push_back(target);
  return startCpMvRmShellRun(argv, /*destination=*/"", cpMvRmSourceDirectory());
}

// executeCopy()/executeMove()/executeRemove()の共有される末尾
// (2026-08-22の再設計、executeCopy()自身のコメント参照): `argv`
// (既にソースを含み、CP/MVについては宛先も含む)をshell_の実際の、
// 持続的なSHELLセッションへ、1つのシェルでクォートされたコマンド
// ラインとして注入し、ユーザーが他のどんなSHELLコマンドともまさに
// 同様にそれを見る/答えられるようShellモードへ切り替え、
// pendingCpMvRmShellRun_経由で後でどこに着地すべきかを覚えておく
// ――pollPendingCpMvRmShellRun()参照。
bool Application::startCpMvRmShellRun(const std::vector<std::string> &argv, const std::string &destination,
                                       const std::string &sourceDirectory,
                                       std::vector<std::string> targetBasenames) {
  if (!ensureShellActive()) {
    activeView().redraw();
    return false;
  }
  pendingCpMvRmShellRun_ = PendingCpMvRmShellRun{shell_.promptGeneration(), destination, sourceDirectory,
                                                 std::move(targetBasenames), cpMvRmOrigin_};
  cpMvRmOrigin_ = ApplicationMode::Directory;  // 対象は上のargvに確定済み
  cpMvRmListTargets_.clear();
  shell_.commandLine().setText(shellQuoteArgv(argv) + "; echo " + kCpMvRmExitMarker + "$?");
  shell_.submitLine();
  return true;
}

void Application::pollPendingCpMvRmShellRun() {
  if (!pendingCpMvRmShellRun_) return;
  if (!shell_.alive()) {
    pendingCpMvRmShellRun_.reset();
    return;
  }
  if (shell_.promptGeneration() == pendingCpMvRmShellRun_->promptGenerationBefore) return;  // まだ実行中
  PendingCpMvRmShellRun run = *pendingCpMvRmShellRun_;
  pendingCpMvRmShellRun_.reset();
  currentMode_ = ApplicationMode::Directory;
  // scrollback(新しい順)を、startCpMvRmShellRun()が注入した行に
  // 付け加えた終了コードのマーカーについて走査する――kCpMvRmExitMarker
  // 自身のコメント参照。見つからない(プロンプトが本当に再び現れた
  // 後は起こらないはずだが、パイプラインの途中の素の"return"や同様の
  // シェルの奇癖は考えられる)場合は、保守的なデフォルトとして失敗
  // として扱う。
  bool ok = false;
  const auto &lines = shell_.scrollback().lines();
  static const std::size_t kMarkerLen = std::char_traits<char>::length(kCpMvRmExitMarker);
  for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
    if (it->text.compare(0, kMarkerLen, kCpMvRmExitMarker) == 0) {
      ok = (it->text.substr(kMarkerLen) == "0");
      break;
    }
  }
  // 成功した時「だけ」、そしてそれが何か実在するものへ解決される
  // 時「だけ」宛先ディレクトリに着地する(そうでなければdescendTo()
  // は安全に失敗時no-opになる――例えば`destination`がディレクトリ
  // ではなく新しい「ファイル名」だった単一ファイルのコピー);RM
  // (空の宛先)、失敗した実行、そして宛先が実を結ばなかったどんな
  // CP/MVも、全てsourceDirectoryを再ロードすることへフォールバック
  // する。素のenterDirectory()呼び出しではなくdescendTo()(こちらは
  // 「同時に」ModeStackのフレームも積む)を使う――実際の報告:
  // "CPで/tmpへのコピーを終えた後の（ls）では^Gで/tmpから変わらない"、
  // これは、enterDirectory()単独では(設計上)modeStack_に一切触れず
  // (それ自身のコメント参照)、そのため^GにはsourceDirectoryへポップ
  // して戻すフレームが無かったことまで突き止められた。descendTo()は、
  // 通常のサブディレクトリへのEnter/goToParent()のナビゲートが行う
  // のとまさに同様に、まずそのフレームを積むので、その後の^Gは他の
  // どんな降下とも同じように振る舞う。run.destinationは常に空(RM)
  // か、既に「絶対」パスである――executeCopy()/executeMove()が、
  // それがそもそもここへ到達する前にそれを解決する(それら自身の
  // 2026-08-27のコメント参照)。
  // 一覧から発動した場合(2026-09-27): CP/MVが成功して宛先へ入れたら(ls)の
  // 宛先へ(従来と同じ)。それ以外(RM、失敗)は発動元の一覧へ戻り、選択を
  // 解く。LOCATEは消えた/動いたファイルが一覧に残らないよう検索し直す。
  if (run.origin != ApplicationMode::Directory) {
    if (ok && !run.destination.empty() && directoryMode_.descendTo(run.destination)) {
      directoryMode_.setTransientMarkedNames(run.targetBasenames);
      if (run.origin == ApplicationMode::ListBrowser) promptHistoryContext_ = std::nullopt;
      directoryView_.redraw();
      return;
    }
    currentMode_ = run.origin;
    if (run.origin == ApplicationMode::Locate && ok && !locateMode_.query().empty()) {
      locateMode_.search(locateMode_.query());
    }
    int size = 0;
    if (ListMarks *marks = activeListMarks(&size)) marks->clear();
    activeView().redraw();
    return;
  }
  if (!ok || run.destination.empty() || !directoryMode_.descendTo(run.destination)) {
    directoryMode_.enterDirectory(run.sourceDirectory);
  } else {
    // 2026-09-08追加、DirectoryMode::setTransientMarkedNames()自身の
    // コメント参照――descendTo()が成功した(宛先が本物の、閲覧可能な
    // ディレクトリへ解決した)場合に限り、コピー/移動した対象を一時
    // マークする。descendTo()自身がenterDirectory()を呼ぶので、その
    // 内部でtransientMarkedNames_は既にクリアされている――この直後に
    // 設定するので問題ない。
    directoryMode_.setTransientMarkedNames(run.targetBasenames);
  }
  directoryView_.redraw();
}

// pollPendingCpMvRmShellRun()と同じ、ノンブロッキングの「実行ループ
// 自身の既存のティックに待機を駆動させる」という形――それ自身の
// コメント参照。2026-08-26追加(直接のユーザーの仕様): 今や成功時
// には宛先ディレクトリにも着地する、pollPendingCpMvRmShellRun()が
// CP/MVについて使うのとまさに同じdescendTo()してから注釈を付ける
// という形(なぜ素のenterDirectory()ではなくdescendTo()なのかに
// ついてはそれ自身のコメント参照)――以前はこれは成功/失敗を一切
// チェックせず、単に展開がそこから起動されたモードを復元するだけ
// だった。executeArchiveExtract()は、ここへ到達する前に既に宛先が
// 実際のディレクトリとして存在することを検証しているので、ここで
// の失敗は稀であるはずである(本当に破損した/読めないアーカイブが
// 実際のtar/unzipの呼び出し自体を失敗させる)――そのケースは、この
// 変更より前と同様、依然として`returnMode`にフォールバックする。
void Application::pollPendingArchiveExtractShellRun() {
  if (!pendingArchiveExtractShellRun_) return;
  if (!shell_.alive()) {
    pendingArchiveExtractShellRun_.reset();
    return;
  }
  if (shell_.promptGeneration() == pendingArchiveExtractShellRun_->promptGenerationBefore) return;
  PendingArchiveExtractShellRun run = *pendingArchiveExtractShellRun_;
  pendingArchiveExtractShellRun_.reset();

  bool ok = false;
  const auto &lines = shell_.scrollback().lines();
  static const std::size_t kMarkerLen = std::char_traits<char>::length(kCpMvRmExitMarker);
  for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
    if (it->text.compare(0, kMarkerLen, kCpMvRmExitMarker) == 0) {
      ok = (it->text.substr(kMarkerLen) == "0");
      break;
    }
  }

  if (ok && directoryMode_.descendTo(run.destination)) {
    currentMode_ = ApplicationMode::Directory;
    directoryMode_.arcAnnotations().push(DirectoryMode::ArcAnnotationKind::Arc, run.archivePath);
    // 2026-09-09修正、PendingArchiveExtractShellRun::topLevelNames自身の
    // コメント参照――アーカイブ自身のメンバー一覧から求めた名前を
    // そのまま一時マークする(ディレクトリの実際の前後比較には一切
    // 頼らない、既存/新規を問わず常に正しい)。DirectoryMode::
    // isMarked()自身がentries_[index].nameの型マーカー('/'/'*'/'@'等)
    // を比較時に剥がすので、ここで渡す名前は型マーカー無しのままでよい。
    directoryMode_.setTransientMarkedNames(run.topLevelNames);
    directoryView_.redraw();
    return;
  }
  currentMode_ = run.returnMode;
  activeView().redraw();
}

bool Application::pollShellHandoff() {
  if (currentMode_ != ApplicationMode::Shell || !shell_.alive()) return false;
  std::string path = "/tmp/.usilic-shell/s." + std::to_string(::getuid()) + "." + std::to_string(shell_.childPid());
  struct stat st;
  if (::stat(path.c_str(), &st) != 0) return false;

  if (S_ISSOCK(st.st_mode)) {
    // パイプされたstdin(SHELLプロンプトでタイプされた
    // `ls -l | usilic`): 「子」がソケットサーバーである
    // (main.cpp自身のtryHandoffToParentShell()のコメント参照)――
    // この側がクライアントとして接続し、パイプされたバイトを、本物の
    // fd 0についてstartPagerStdin()が既に使っているのとまさに同じ
    // 遅延ロードの経路であるfileMode_.loadStream()を通じてストリー
    // ミングする、ただし代わりにこの接続されたソケットfdから供給
    // される(どちらに対する::read()も同一に振る舞う)。loadStream()
    // (loadFile()がそうするのと同様、これがそれをクリアする)の
    // 「後の」setSearchOrigin()――""はnameフィールドを空白のままに
    // し(直接のユーザー依頼: "モード名は無し")、"(stdin)"がtrailing2
    // のバッジになり、Directoryの"(ls)"/Archiveの"(tar)"が占めるのと
    // 「同じ」スロットに描かれる(直接のユーザー依頼: "（ls）(tar)等
    // と同じ位置に")――このtrailing2が実際にどこで読み戻されるかに
    // ついてはFileMode自身のModeLineFieldsProvider(pagerMode_の分岐)
    // 参照。
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
      ::close(fd);
      return false;
    }
    currentMode_ = ApplicationMode::File;
    fileReturnMode_ = ApplicationMode::Shell;
    // 2026-09-06修正、直接のユーザー報告("総行数が１行単位で進まない
    // ...旧同様１バイトずつ読み込んで改行が来たら１増やす")――
    // FileMode::ensureLoadedThrough()自身の2026-09-06追加分のコメント
    // 参照: legacy自身のfrom_shell()と同じ「見えている範囲は1行ずつ」
    // というペーシングは、その後のensureLoadedThrough()呼び出し側
    // だけを直しても、この最初のloadStream()自身が(以前通り)
    // rows*2行分もまとめて先に食い切ってしまっては効果が無い――
    // 初回はここでもわざと1行だけに絞り、以後の各メインループの
    // 反復(pollPagerExitNotice()自身のコメント参照)が1行ずつ育てる。
    fileMode_.loadStream(fd, /*ownsFd=*/true, "", 1);
    fileMode_.annotations().pushWithNameOverride(FileMode::FileAnnotationKind::Search, "", "(stdin)");
    fileMode_.setPagerReturnsToShell(true);  // それ自身の宣言コメント参照: ^G/'q'はShellへ戻る、不活性/終了ではない
    activeView().redraw();
    return true;
  }

  // プレーンなパス(ディレクトリかファイル)、あるいは引数が全く無い
  // (「シェル自身のcwd」を意味する――tryHandoffToParentShell()自身の
  // コメント参照): 単一の行、子プロセスによって一時ファイルへ書か
  // れ、不可分にrename()でその場所へ配置されるので、この読み取りは
  // 完全に書かれたファイルだけを見ることになり、部分的なものを見る
  // ことは決して無い。
  std::ifstream in(path);
  std::string resolved;
  std::getline(in, resolved);
  in.close();
  ::unlink(path.c_str());
  if (resolved.empty()) return false;

  if (std::filesystem::is_directory(resolved)) {
    currentMode_ = ApplicationMode::Directory;
    return directoryMode_.enterDirectory(resolved, /*consultHistory=*/false);
  }
  // 2026-08-25の訂正(実際のlegacyバイナリを再確認した直接のユーザー
  // の追跡): プレーンな「ファイル」の引数は、ページャの仕組みも、
  // SHELLへ戻るための配管も、5桁/「SHELL」のモードラインの扱いも
  // 一切必要としない――"SHELLプロンプトでusilic起動したときの引数
  // の画面になればいいだけ...つまりはls画面で「.bashrc」を選択した
  // 場合と同じ。モードをそこへ移せばいいだけ"。そのためこれは
  // fileMode_.loadFile()(通常の「積極的」ロード、openSelectedEntry()
  // がプレーンなディレクトリ一覧のEnterについて行うのと同じ呼び出し)
  // とfileReturnMode_ = Directoryをまさに確立された慣習通りに使う
  // ――startPagerFileList()の遅延/ストリーミングのloadStream()の
  // 経路(あちらは、本当に異なるスタンドアロンのCLI起動とパイプされ
  // たstdinのケース専用のまま残る、loadStream()自身のコメント参照)
  // ではない。その後の^GはreturnToDirectory()に到達し、それ自身の
  // 既存の「ファイルの実際の親ディレクトリをたどる」修正(それ自身の
  // 2026-08-16のコメント参照)が、それ自身で既に正しい格納先ディレク
  // トリへ再入場する――ここでは追加のdirectoryMode_のセットアップは
  // 一切必要無い。
  if (!fileMode_.loadFile(resolved)) return false;
  currentMode_ = ApplicationMode::File;
  fileReturnMode_ = ApplicationMode::Directory;
  activeView().redraw();  // 通常のキー入力ごとの再描画サイクルの外での一度限りの呼び出し、startPagerFileList()自身のコメント参照
  return true;
}

// runNestedUsilicProgram()自身の宣言コメント参照。pollShellHandoff()
// の核心部分の複製(パイプされたstdin/プレーンなパス/ファイルの
// 3分岐とも同一のロジック)――違いはshell_.childPid()の代わりに引数の
// pidを使うこと、currentMode_ == Shellのゲートが無いこと。
// 2026-09-17追加、直接のユーザー指摘("戻り値がPROGRAMならPROGRAMへ
// 戻らないと。なんだけど、PROGRAMは^J^P(program一覧)でも発動する
// ので話がややこしくなる。そこで戻り値は~/git/usilic/でいいかなと"):
// 以前はパイプされたstdinのケースだけ戻り先ラベルを固定文字列
// "PROGRAM"にしていたが、"PROGRAM"という語自体がusilicrc設定一覧
// (^J^P、startProgramList())と紛らわしく、しかも実際の^Gの戻り先は
// 常にcapturedModeが指す「実際の場所」であって「PROGRAM画面」という
// 独立した何かではない――ARCHIVE再設計(①)/K-23(⑥)と同じ理屈で、
// ラベルではなく実際の戻り先パスをtrailing3に出す。capturedModeが
// Directoryの場合(最も一般的、PROGRAMがディレクトリ一覧から発動
// された場合)はdirectoryMode_.currentDirectory()をそのまま使う――
// programPromptDirectory_(マークされたファイル/カーソル位置の
// エントリが無いと空のまま)には依存しない、こちらは常に有効。
// それ以外のcapturedMode(File/Grep/Locate/Du等からのPROGRAM発動)は
// 今のところ対象外(pagerListingDirectory_が空のままなのでtrailing3
// 自体が単に表示されない、独立起動と同じ既定の見た目に留まる)――
// フェーズ2の他モード監査で改めて扱う。
bool Application::tryPollProgramHandoff(pid_t pid, ApplicationMode capturedMode) {
  std::string path = "/tmp/.usilic-shell/s." + std::to_string(::getuid()) + "." + std::to_string(pid);
  struct stat st;
  if (::stat(path.c_str(), &st) != 0) return false;

  if (S_ISSOCK(st.st_mode)) {
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
      ::close(fd);
      return false;
    }
    currentMode_ = ApplicationMode::File;
    fileReturnMode_ = capturedMode;
    fileMode_.loadStream(fd, /*ownsFd=*/true, "", 1);
    fileMode_.annotations().pushWithNameOverride(FileMode::FileAnnotationKind::Search, "", "(stdin)");
    fileMode_.setPagerReturnsToProgram(true);
    if (capturedMode == ApplicationMode::Directory) {
      fileMode_.setPagerListingDirectory(truncatedTrailingPath(directoryMode_.currentDirectory()));
    }
    activeView().redraw();
    return true;
  }

  std::ifstream in(path);
  std::string resolved;
  std::getline(in, resolved);
  in.close();
  ::unlink(path.c_str());
  if (resolved.empty()) return false;

  if (std::filesystem::is_directory(resolved)) {
    currentMode_ = ApplicationMode::Directory;
    return directoryMode_.enterDirectory(resolved, /*consultHistory=*/false);
  }
  if (!fileMode_.loadFile(resolved)) return false;
  currentMode_ = ApplicationMode::File;
  fileReturnMode_ = ApplicationMode::Directory;
  activeView().redraw();
  return true;
}

// 2026-09-17追加、直接のユーザー報告+提案参照(このメソッド自身の
// application.hpp宣言コメント)。executeProgram()自身の"usilic"分岐
// から呼ばれる――ProgramMode::run()(完全同期のChildProcess::
// runInteractive())を経由せず、ここで直接ChildProcess::
// spawnInteractive()+ポーリングループを組み立てる。
bool Application::runNestedUsilicProgram(const std::string &command) {
  ApplicationMode capturedMode = currentMode_;
  // USILIC=1(Shell::start()自身のsetenv()と同じ意味だが、こちらは
  // Shellの管理下のptyではなくusilic自身がここで直接fork/execする
  // ので、コマンドライン自体に付与する)――cdWrap()が生成する
  // "cd '<dir>' && <command>"全体の先頭にではなく、<command>自体の
  // 先頭に付ける(cdだけに変数代入が掛かってしまわないよう、cdWrap()
  // へ渡す前に付与する)。
  std::string toRun = cdWrap(programPromptDirectory_, "USILIC=1 " + command);
  programPromptDirectory_.clear();  // 結果に関わらず正確に一度消費される、executeProgram()自身の慣習通り

  display_.leaveForExternalProgram();
  keyInput_.leaveRawMode();
  pid_t pid = ChildProcess::spawnInteractive({"sh", "-c", toRun});
  if (pid > 0) {
    bool handedOff = false;
    while (!handedOff && !ChildProcess::hasExited(pid)) {
      if (tryPollProgramHandoff(pid, capturedMode)) handedOff = true;
      ::usleep(20000);
    }
    // 子が既に終了済みでもランデブーファイルの受け取りが間に合って
    // いない可能性への最後の一押し(ポーリング間隔20msに対し、子自身の
    // 待ち合わせは最大2秒あるので通常はここに来る前にhandedOff済みの
    // はずだが、念のため)。
    if (!handedOff) tryPollProgramHandoff(pid, capturedMode);
    // 2026-09-17追加、実機報告("00000/00000 END...キー入力すると
    // 生データがターミナルに出てくる"、修正後さらに"表示はOKだが
    // キーが一切効かなくなった/以前効いた^Cも効かなくなった")――
    // 調査の結果、真因はこの直前まで存在していた「handedOff後も
    // hasExited(pid)になるまで待ち続ける」というループ設計そのもの
    // だったと判明した。main.cpp自身のtryHandoffToParentShell()の
    // パイプされたstdin分岐(子側)は、読めたバイトをブロッキングの
    // write()でソケットへそのまま流し込むだけで、こちら(親)側の
    // 意図的なペーシング(loadStream()自身のコメント参照: 生きた
    // パイプ/ライブなSHELLのpagerに配慮し、最初は1行、以後は
    // ensureLoadedThrough()が「画面1枚分育ったら以後はユーザーが
    // 実際にスクロールするまで追加要求しない」という設計)を一切
    // 考慮しない――ある程度の大きさのファイル(このCLAUDE.md自身が
    // 実例)では、画面1枚分を読み終えた時点で親側の要求が止まり、
    // ソケットの送信バッファが満杯になった子側のwrite()がブロック
    // されたまま二度と進まなくなる。そこへ「子が終了するまで待つ」
    // という設計を重ねると、親自身がその子の完了を無期限に待ち続け、
    // 結果としてkeyInput_.reenterRawMode()に一度も到達できないまま
    // 固まる(cooked+echoのまま、SHELLのpollShellHandoff()相当の
    // 「ハンドオフしたら即座に通常の対話ループへ戻る」という前提を、
    // このメソッド自身が独自の待ち合わせループで踏みにじっていた)。
    // 修正: ハンドオフが成立した時点(SHELLのpollShellHandoff()が
    // メインループの1反復内で完結するのと同じ意味)で即座にループを
    // 抜ける――子(sh、その子である入れ子usilic)がまだソケットへ
    // 書き込み中/ブロック中でも構わない、以後はSHELLの生きたpager
    // 同様、通常のメインループ自身のpollPagerExitNotice()呼び出し
    // (この関数の外、main.cpp側)がユーザーの実際のスクロールに応じて
    // 少しずつ読み進め、その都度ブロックされていた子のwrite()を
    // 必要なだけ再開させる。子プロセス自体はここでは待たず、
    // pendingProgramChildPid_自身の宣言コメント通り、以後
    // pollPagerExitNotice()自身の冒頭のopportunisticな回収チェック
    // 経由で機会があるたびに(ゾンビ化を防ぐため)回収する。
    if (handedOff && !ChildProcess::hasExited(pid)) pendingProgramChildPid_ = pid;
    // ここで一度だけ即座に呼ぶことで、画面へ戻る前に(次のメイン
    // ループの反復を待たず)可能な範囲の続きを先読みしておく――
    // 既にstillCatchingUpFirstScreen自身の条件で自然に止まるので、
    // 上のブロッキングのdead lockには戻らない。
    if (handedOff) pollPagerExitNotice(/*suppressDraw=*/false);
  }
  // 2026-09-17追加、同じ実機報告の後半("キー入力すると生データが
  // ターミナルに出てくる")向けの修正: 上のループの間、子(sh、その
  // 子である入れ子usilic)のstdinはリダイレクトされたファイルであり
  // 本物の端末を一切読まない――less/vi(ProgramMode::run()の既存の
  // 経路、子が本物の端末を直接読む)と違い、この端末はleaveRawMode()
  // でcooked+echoに戻ったまま、この間ずっと「誰も読んでいない」。
  // その間にユーザーがキーを押すと、cookedモードのカーネル自身に
  // よってそのバイトがそのまま画面へエコーされ(かつ入力キューに
  // 溜まり)、これがまさに報告された「生データ」の正体――tcflush()
  // で未読の入力キューを捨ててからraw modeへ戻すことで、その残骸が
  // 以後のreadByte()に紛れ込むことも防ぐ。
  if (keyInput_.fd() >= 0) ::tcflush(keyInput_.fd(), TCIFLUSH);
  keyInput_.reenterRawMode();
  display_.reenterFromExternalProgram();

  history_.pushRecent(RingCategory::ShellCommand, command);
  activeView().redraw();
  return true;
}

// docs/HISTORY.md:1840("chown実行後のRM、MVではファイルが選べない...
// その旨何らかのメッセージ表示があれば親切"): legacy自身のis_write()
// チェック(menu.cのm_cr())は、単にビープを鳴らして、権限が拒否
// されたRM/MVの対象を「マークすること」さえも黙って拒否する。この
// ポートのマーキング(^Space)は無条件で操作に依存しない
// (DirectoryMode::toggleMark()自身のクラスコメント参照)ので、代わり
// に、実際のコマンドが実行される直前にここでチェックが行われる
// ――CPは意図的に除外されている(読み取り専用のソース「から」コピー
// することは読み取り権限だけを必要とし、CpMvRmMode::isWritable()は
// それをチェックしないので、正当なコピーを誤って拒否してしまう)。
// 別の、異なる挙動をする仕組みではなく、^Jeの「Invalid mode」につい
// てdocs/HISTORY.md:8912が既に確立した、DirectoryMode::reportError()
// のまさにその下線付きモードラインメッセージの表示を再利用する。
bool Application::checkTargetsWritable(const std::vector<std::string> &targets) {
  for (const auto &target : targets) {
    if (!CpMvRmMode::isWritable(target)) {
      if (cpMvRmOrigin_ != ApplicationMode::Directory) {
        // 一覧から発動した場合(2026-09-27)は、その一覧の位置欄に出す。
        cpMvRmOrigin_ = ApplicationMode::Directory;
        cpMvRmListTargets_.clear();
        showTransientNotice(activeView(), "Permission denied");
        display_.putChar(0x07);
        display_.flush();
        return false;
      }
      directoryMode_.reportError("Permission denied");
      directoryView_.redraw();
      return false;
    }
  }
  return true;
}

// m_cr()のM_exitケースから移植: `if (*rvp->str == 'y') return
// -T_exit;`――確認はまさに"y"でなければならない(executeRemove()の
// 最初の1文字だけのlegacyチェックと同じ単純化)。それ自体は何もせず
// wantsExit_を設定するだけ;どちらにせよcurrentMode_/画面について
// 何も変わらない。
bool Application::executeExit(const std::string &confirmation) {
  if (confirmation != "y") {
    activeView().redraw();
    return false;
  }
  saveResumableCutState();
  wantsExit_ = true;
  return true;
}


// K-58: startArchivePrompt()自身のコメント参照。2026-08-23の再設計、
// executeCopy()自身のもの(そのコメント参照)と同じ理屈、そして
// それをARCへ拡張する同じ直接のユーザー依頼: K-57(^X^Ka、「SHELL
// ウィンドウ付き」)は無くなったので、これが今や唯一の入り口であり、
// 常にChildProcess::run()/runInteractive()の代わりにshell_自身の
// 実際の、持続的なSHELLセッション経由で実行される
// (startArchiveExtractShellRun()、startCpMvRmShellRun()を反映して
// いる)――この方法で、展開自身のtar/unzipの出力は本当にshell_の
// scrollback(LOGで見える)に生きる、CP/MV/RMに合わせている。CP/MV/RM
// とは異なり、宛先ディレクトリへの着地は無い(ARCについては一度も
// 依頼されていない)――シェルのプロンプトが再び現れると、
// pollPendingArchiveExtractShellRun()は、単にこれがそこから起動
// された、どちらのモード(DirectoryかArchive)でも復元し、再描画
// するだけである。
bool Application::executeArchiveExtract(const std::string &destination) {
  std::string archivePath;
  ArchiveKind kind = ArchiveKind::None;
  std::vector<std::string> members;

  if (currentMode_ == ApplicationMode::Archive) {
    archivePath = archiveMode_.currentArchive();
    kind = archiveMode_.archiveKind();
    // 2026-09-08修正、実際のユーザー報告("選択マーク皆無のARC画面での
    // ^Oa^Mではsaved/への全展開が起きない"): archiveMode_.targetEntries()
    // は(CP/MV同様の確立された慣習として)何もマークされていなければ
    // カーソル下の1エントリへフォールバックする――そのため以前は、
    // マークが無い時でも常にちょうど1件のmembersが積まれてしまい、
    // 下のDirectoryモードの分岐が意図的にmembersを空のままにして
    // 表現している「アーカイブ全体」に、Archiveモードの側からは
    // 決して到達できなかった。ここではmarkedCount()で明示的なマーク
    // の有無だけを見て、マークが1件も無ければmembersを空のままにし、
    // マークがあればそれらだけをmembersへ積む――「マーク無し=全体、
    // マークあり=マーク分」という、Directoryモードの分岐と同じ意味論
    // にArchiveモードの側も合わせている。
    if (archiveMode_.markedCount() > 0) {
      for (const auto &entry : archiveMode_.targetEntries()) members.push_back(entry.name);
    }
  } else if (currentMode_ == ApplicationMode::Directory) {
    const DirectoryEntry *entry = directoryMode_.selectedEntry();
    if (!entry) {
      activeView().redraw();
      return false;
    }
    archivePath = directoryMode_.currentDirectory();
    if (!archivePath.empty() && archivePath.back() != '/') archivePath += '/';
    archivePath += entry->name;
    configuration_.programForSuffix(archivePath, &kind);
    if (kind == ArchiveKind::None && isEpubFile(archivePath)) kind = ArchiveKind::Zip;  // startArchivePrompt()参照
    // membersは空のままにしてある――アーカイブ全体、HELP.jp 2.11
    // 自身のFile-selectorの例に従う。
  }

  // 2026-08-26追加、^X^M/^X aが実際に受け取り得る3つの宛先の形全てを
  // カバーする、直接の(暫定的な)ユーザーの仕様:
  //   - 空: 以前は黙ってno-op("何も起きない")だった――今は
  //     <homeDirectory>/.usilic/saved/にデフォルトする(無ければ
  //     自動作成される、shellInsertBufferContent()自身の^J^Wの
  //     ファイルへ保存する機能が既に確立したのと「同じ」ディレクト
  //     リ、その前例についてはそれ自身のコメント参照)、そのため
  //     タイプされた宛先無しで展開すると、エントリは
  //     .usilic/saved/<アーカイブ相対のパス>に着地する(tar/unzipは、
  //     このディレクトリを指定されるとそれ自体でそのパスを保つ、
  //     ここではエントリごとの追加のロジックは不要)。
  //   - 実際に存在するディレクトリ: タイプされた通りに使う、以前
  //     から変わっていない。
  //   - それ以外の何か(タイプミス、一度も作られたことのないパス):
  //     こちらも以前は黙ってno-opだった――今は、シェルコマンドを
  //     起動する前に、「<path>: No such directory」というモードライン
  //     メッセージ(Archive/Directory自身のreportError()、
  //     checkTargetsWritable()自身のコメント参照、CP/MV/RM自身の
  //     「Permission denied」の慣習に合わせている)で完全に拒否
  //     される。
  std::string resolvedDestination = destination;
  if (resolvedDestination.empty()) {
    resolvedDestination = dataDirectory();  // 2026-09-27、saved/から改名
  }
  if (!std::filesystem::is_directory(resolvedDestination)) {
    std::string message = resolvedDestination + ": No such directory";
    if (currentMode_ == ApplicationMode::Archive) archiveMode_.reportError(message);
    else directoryMode_.reportError(message);
    activeView().redraw();
    return false;
  }

  std::vector<std::string> argv = buildArchiveExtractArgv(archivePath, kind, members, resolvedDestination);
  if (argv.empty()) {
    // 2026-09-16追加、実機報告("src/skrw021.lzhを選択→...^Oa^M→
    // ~/.usilic/saved/へ移動せずserver.txtも保存されていない"):
    // buildArchiveExtractArgv()自身の宣言コメントの通りLha/Rpm/Deb/
    // Bzip2はこの最初のスライスでは未サポートで空を返すが、以前は
    // ここで何の説明も無く単にno-opしていた――「何も起きなかった」
    // ように見え、宛先が間違っているのか操作自体が無効なのか区別が
    // 付かなかった。"<dest>: No such directory"と同じ考え方
    // (reportError()経由の下線付きモードラインメッセージ)で、対応
    // していないアーカイブ種別であることを明示する。
    std::string message = "Extract not supported for this archive type";
    if (currentMode_ == ApplicationMode::Archive) archiveMode_.reportError(message);
    else directoryMode_.reportError(message);
    activeView().redraw();
    return false;
  }

  // 2026-08-27修正、openSelectedArchiveEntry()自身のコメントと同じ
  // 報告("カーソル位置が「utf8/ＢＳ世界の〜」じゃない"): Archive
  // モードから直接展開する(^X^M/^X a)ことは、openSelectedArchiveEntry()
  // ではなくシェルの実行経由で離れるので、それ自身でこの同じ保存を
  // 必要とする――そうしないと、後でこのアーカイブへ戻る
  // goToRecentFile()(^J^J)は、実際に展開が呼び出された場所ではなく、
  // 以前の無関係な訪問から保存されていた位置を何であれ復元してしまう。
  if (currentMode_ == ApplicationMode::Archive) archiveMode_.saveCursorPosition();

  ApplicationMode returnMode = currentMode_;
  if (!ensureShellActive()) {
    activeView().redraw();
    return false;
  }
  // 2026-09-09修正、PendingArchiveExtractShellRun::topLevelNames自身の
  // コメント参照(直接のユーザー報告、展開前後のディレクトリ一覧の
  // 差分方式では「以前にも同じ場所へ展開したことがある」場合に機能
  // しないと判明): `members`(部分展開なら実際に展開されるメンバー、
  // 空なら「アーカイブ全体」)から、実際に宛先の直下に現れるはずの
  // 名前(パスの最初の"/"より前の部分)を求める――ディレクトリの実際
  // の中身には一切頼らない。
  std::vector<std::string> topLevelNames;
  {
    std::vector<std::string> namesToScan;
    if (!members.empty()) {
      namesToScan = members;
    } else {
      // archiveMode_がまだこのアーカイブを読み込んでいなければ読み込む
      // (Directoryモードからの直接展開はarchiveMode_に一切触れていない
      // ――上のcurrentMode_==Directoryの分岐自身のコメント参照)。
      // 既に同じアーカイブを読み込み済み(Archiveモードから展開した
      // 場合等)なら再読み込みしない。
      if (archiveMode_.currentArchive() != archivePath) archiveMode_.loadArchive(archivePath);
      for (const auto &entry : archiveMode_.entries()) namesToScan.push_back(entry.name);
    }
    std::set<std::string> seen;
    for (std::string name : namesToScan) {
      if (name.rfind("./", 0) == 0) name = name.substr(2);
      std::size_t slash = name.find('/');
      std::string top = (slash == std::string::npos) ? name : name.substr(0, slash);
      if (!top.empty() && seen.insert(top).second) topLevelNames.push_back(top);
    }
  }
  // 2026-09-16追加、直接のユーザー指摘("MS-DOSの.lzhの構成員って
  // 展開するとパーミッションは強制一律↑なんだろうか？"→"パーミッ
  // ションは0644へ変更よろしく"): 実機のLhasa(`lha xfw=`)は展開した
  // ファイルを`-rw-------`(0600、所有者のみ)にする――MS-DOS側に
  // unixパーミッションの概念が無いための安全側フォールバックと見られ、
  // tar/unzipが実機で示す典型的な0644より制限的だった。展開コマンド
  // 自体の直後に`chmod`を続け、topLevelNames(このコマンド自身が
  // 宛先直下に実際に作るはずの名前、すぐ上のコメント参照)だけを対象に
  // する――宛先ディレクトリに元々あった無関係なファイルには触れない。
  // go+r(0600→0644相当)+u+rw(既にrwなので無害)+go+X(ディレクトリ
  // だけ実行ビットも付け、辿れるようにする――大文字Xは「ディレクトリ
  // か、既に誰かに実行権が立っている場合だけ」実行ビットを追加する
  // chmodのシンボリックモード上の特殊扱い、この移植では平ファイルに
  // 誤って実行権を付けてしまう心配が無い)。ArchiveKind::Lhaだけに
  // 限定する(tar/unzip/7zは既に妥当な既定パーミッションのため無変更)。
  std::string chmodSuffix;
  if (kind == ArchiveKind::Lha && !topLevelNames.empty()) {
    std::vector<std::string> chmodTargets;
    for (const auto &name : topLevelNames) chmodTargets.push_back(resolvedDestination + "/" + name);
    chmodSuffix = " && chmod -R u+rw,go+rX " + shellQuoteArgv(chmodTargets);
  }
  pendingArchiveExtractShellRun_ = PendingArchiveExtractShellRun{
      shell_.promptGeneration(), returnMode, resolvedDestination, archivePath, std::move(topLevelNames)};
  shell_.commandLine().setText(shellQuoteArgv(argv) + chmodSuffix + "; echo " + kCpMvRmExitMarker + "$?");
  shell_.submitLine();
  return true;
}

// mode.cのM_lfile/M_ldirの処理から移植: タイプされたパスを解決し
// (~の展開、./と../の整理、PathResolver自身のクラスコメント参照)、
// それが実際に何であるかに応じてDirectoryかFileモードに着地する。
// openSelectedEntry()より意図的に単純: ここにはEXECの拡張子やアー
// カイブ形式の自動検出は無い(legacy自身のM_lfile2もそれをしない
// ――パスをタイプすることは「これをプレーンテキスト/一覧として
// ロードする」ことであり、既存の一覧の「エントリ」がそうされる
// であろう賢い方法でそれを開くことではない、これはEnter自身の別個
// の仕事である)。空、存在しない、あるいは読めないパスについては
// false。
bool Application::executePathInput(const std::string &path) {
  if (path.empty()) {
    activeView().redraw();
    return false;
  }
  // openRecentFileEntry()自身のコメント参照: PromptKind::RecentFile
  // (^X^F自身のFILE履歴一覧)の投入はexecutePathInput()を直接呼ぶ
  // (openSelectedListEntry()自身のpromptHistoryContext_分岐経由の
  // ため、下のRecentFile専用のopenRecentFileEntry()分岐を経由し
  // ない)ので、ここでも同じ合成形の検出をしておく必要がある。
  if (parseCutFileHistoryEntry(path)) return openRecentFileEntry(path);
  // 2026-10-01、直接のユーザー報告("^X^F一覧中、^Oeで開いた.epubには(unzip)が付いているのに
  // (epub)時と同じ表示になる"): ARCで開いた.epubの履歴の形「パス/」(openEpubAsArchive())は、
  // パスとして解決すると末尾の/が落ちてEPUBモードで開いていた。FILE履歴と同じくARCで開く。
  if (!pathInputIsDirectory_ && path.size() > 1 && path.back() == '/' &&
      isEpubFile(path.substr(0, path.size() - 1))) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(path.substr(0, path.size() - 1), ec) && !ec)
      return openRecentFileEntry(path);
  }
  // directoryMode_自身のcurrentDirectory()(最後に「閲覧された」
  // ディレクトリ、そこから開かれたFile/Man等の画面を見ている間も
  // そのまま留まる――なぜこれがプロセス自身のcwdではあり得ないのか
  // についてはPathResolver::resolve()自身のbaseDirectoryのコメント
  // 参照: usilicは決してchdir()しない)に対して相対的に解決される。
  // 同じ理由で既にshell_.currentDirectory()に対して解決している
  // Shell自身のTab補完に合わせている。
  ResolvedPath resolved = pathResolver_.resolve(path, directoryMode_.currentDirectory());
  if (!resolved.exists) {
    activeView().redraw();
    return false;
  }
  if (resolved.isDirectory) {
    if (!directoryMode_.enterDirectory(resolved.path)) {
      activeView().redraw();
      return false;
    }
    currentMode_ = ApplicationMode::Directory;
    return true;
  }
  // 2026-08-20修正、実際の報告+スクリーンショット(=dq): これが、
  // タイプされたパスのプロンプト(K-45、M_lfile)と^X^F自身のRecentFile
  // ピッカー(K-46、PromptKind::RecentFileはまっすぐここへルーティング
  // される――なぜopenSelectedListEntry()自身のRecentFileの分岐が
  // それについては実際には一度も実行されないのかについては
  // executePromptSubmission()自身のコメント参照)の両方が到達する
  // 「実際の」共有された経路である――openSelectedEntry()(Directory
  // モード自身のEnterキー)とは異なり、これは以前は無条件に
  // fileMode_.loadFile()を呼んでいたので、どちらの経路経由でアーカ
  // イブ(utf8.tar)を開いても、常にArchiveモードではなくFileModeの
  // 生バイトビュー(「(tar)」バッジが無く、tarファイル自身のバイナリ
  // ヘッダバイトがプレーンテキストであるかのように表示される)へ
  // 流れ落ちていた。openSelectedEntry()自身の「アーカイブを先に、
  // ファイルをフォールバックとして」という順序に合わせ、まず
  // archiveMode_.loadArchive()を試す。
  if (isEpubFile(resolved.path) && !configuration_.isExec(resolved.path)) return openEpub(resolved.path);  // openEpub()参照
  if (archiveMode_.loadArchive(resolved.path)) {
    archiveParentStack_.clear();
    currentMode_ = ApplicationMode::Archive;
    return true;
  }
  if (openExternallyIfApplicable(resolved.path)) return true;  // EXEC/画像のクイックルック(2026-09-27)
  if (!fileMode_.loadFile(resolved.path)) {
    activeView().redraw();
    return false;
  }
  currentMode_ = ApplicationMode::File;
  fileReturnMode_ = ApplicationMode::Directory;  // それ自身の宣言コメント参照
  return true;
}

// K-65(^I、M_list): クラス自身の宣言コメント参照。
std::vector<std::string> Application::completeFilesystemPath(const std::string &partial,
                                                               const std::string &baseDirectory,
                                                               bool directoriesOnly) const {
  std::string dirPart, prefix;
  std::size_t slash = partial.find_last_of('/');
  if (slash == std::string::npos) {
    prefix = partial;
  } else {
    dirPart = partial.substr(0, slash + 1);
    prefix = partial.substr(slash + 1);
  }

  std::string searchDir;
  if (!dirPart.empty() && (dirPart.front() == '/' || dirPart.front() == '~')) {
    searchDir = pathResolver_.resolve(dirPart).path;
  } else {
    searchDir = baseDirectory;
    if (!searchDir.empty() && searchDir.back() != '/') searchDir += '/';
    searchDir += dirPart;
  }

  std::vector<std::string> results;
  std::error_code ec;
  std::filesystem::directory_iterator it(searchDir, ec);
  if (ec) return results;
  for (const auto &entry : it) {
    std::string name = entry.path().filename().string();
    if (name.compare(0, prefix.size(), prefix) != 0) continue;
    bool isDir = entry.is_directory(ec);
    if (directoriesOnly && !isDir) continue;
    results.push_back(dirPart + name + (isDir ? "/" : ""));
  }
  // 2026-09-18追加、実機報告(①「sortされていない」): std::filesystem::
  // directory_iterator自身の列挙順序はOS/ファイルシステム依存で
  // アルファベット順とは限らない――LCP補完1件だけを取り出す従来の
  // 用途では順序自体は無関係だったが、HELP.txt 2.13章「LIST」の
  // 一覧表示機能がこの並びをそのままユーザーへ見せるようになった今、
  // 順序も意味を持つ。pathProgramCandidates()は元から
  // ソート済み・重複排除済みだったので、こちらも揃える。
  std::sort(results.begin(), results.end());
  return results;
}

// pathProgramCandidates()自身の宣言コメント参照(application.hpp)。
std::vector<std::string> Application::pathProgramCandidates(const std::string &partial) const {
  std::vector<std::string> results;
  const char *pathEnv = std::getenv("PATH");
  if (!pathEnv) return results;
  std::istringstream stream(pathEnv);
  std::string dir;
  while (std::getline(stream, dir, ':')) {
    if (dir.empty()) continue;
    std::error_code ec;
    std::filesystem::directory_iterator it(dir, ec);
    if (ec) continue;
    for (const auto &entry : it) {
      std::string name = entry.path().filename().string();
      if (name.compare(0, partial.size(), partial) != 0) continue;
      if (entry.is_directory(ec)) continue;
      std::error_code permEc;
      auto status = entry.status(permEc);
      if (permEc) continue;
      if ((status.permissions() & std::filesystem::perms::owner_exec) == std::filesystem::perms::none) continue;
      results.push_back(name);
    }
  }
  std::sort(results.begin(), results.end());
  results.erase(std::unique(results.begin(), results.end()), results.end());
  return results;
}

// menu.cのM_editケース(^Je)から移植――なぜこれがプロンプト駆動でも
// モード切り替えでもないのか、そして行番号についての意図的な単純化
// (Directoryモードでの編集は常に1行目で開く――その文脈でのdo_edit()
// 自身の`pnp->line`引数は、これから開かれるファイル内の意味のある
// 行番号ではなく、「ディレクトリ一覧」内でのカーソルの行である――
// ここには持ち越されない、Fileモードでの編集は実際のカーソル行を
// 使う)についてはクラスコメント参照。
bool Application::executeEdit() {
  std::string path;
  int lineNumber = 1;

  if (currentMode_ == ApplicationMode::Directory) {
    const DirectoryEntry *entry = directoryMode_.selectedEntry();
    if (!entry) return false;
    if (entry->isDirectory) {
      // legacyのdo_edit()のS_errv("Invalid mode")のケースから移植
      // ――ディレクトリのエントリに対して^Jeが押された時、以前の
      // 黙ったno-opではなく、実際のユーザー依頼が特にこれを求めた。
      // DirectoryMode::reportEditError()自身のコメント参照。
      directoryMode_.reportEditError();
      directoryView_.redraw();
      return true;
    }
    path = directoryMode_.currentDirectory();
    if (!path.empty() && path.back() != '/') path += '/';
    path += entry->name;
  } else if (currentMode_ == ApplicationMode::File) {
    path = fileMode_.currentFile();
    // 2026-09-08修正、直接のユーザー報告("usilic画面では98行だった
    // 行が97として表示されている"――CLAUDE.mdの長い1行が幅超過で
    // 2つの表示行に折り返され、cursorIndex()がその分だけ実際の
    // 生の行番号よりも多く数えてしまっていた): fileView_.cursorIndex()
    // +1は「折り返し後の表示行インデックス」であり、viのような外部
    // エディタが実際に理解する「生の(改行区切りの)行番号」とは別物
    // ――ContentView::Line::rawLineNumber(PipeBuffer::wrapLine()が
    // スタンプする、file_mode.cpp自身のconvertPipeLines()/
    // convertStreamPipeLines()参照)があればそちらを使う。無ければ
    // (rawLineNumberは折り返しが起こり得るFileModeでは常に設定される
    // はずだが、念のため)従来通りのフォールバック。
    int idx = fileView_.cursorIndex();
    const std::vector<ContentView::Line> &fileLines = fileView_.lines();
    lineNumber = (idx >= 0 && idx < static_cast<int>(fileLines.size()) && fileLines[idx].rawLineNumber)
                     ? *fileLines[idx].rawLineNumber
                     : idx + 1;
    // 2026-09-20追加、直接のユーザー要望("`e`でviを呼ぶ前に^L(画面への
    // 表示無しでもいい)の処理だけ実行して欲しい...`^[97^M^N^N^Ne`(100
    // 行目でviを呼ぶ)とやるとvi画面では100行がセンターなので戻った時も
    // そうなって欲しい"): vi自身が`+N`起動時に対象行を画面中央付近に
    // 置くので、usilic側もエディタを呼ぶ「前」に同じセンタリング
    // (ContentView::centerOnCursor()、Command::Redraw(^L)が使うのと
    // 同じもの)を先に適用しておけば、エディタから戻った後の再描画が
    // vi側の見た目と自然に揃う。実際に今この場で画面へ出す必要は無い
    // (どうせ次の行でエディタへ制御を渡す)ので、redraw()もsyncVisible
    // Cursor()も呼ばない――centerOnCursor()自身はtopIndex_を動かす
    // だけの軽い操作。
    fileView_.centerOnCursor();
  } else {
    return false;  // このスライスの範囲外、クラスコメント参照
  }

  int cursorBefore = activeView().cursorIndex();
  bool changed = editMode_.edit(path, lineNumber);
  if (const char *dbg = std::getenv("USILICGUIDEBUG")) {
    FILE *f = std::fopen(dbg, "a");
    if (f) {
      std::fprintf(f, "executeEdit: path=%s lineNumber=%d cursorBefore=%d changed=%d cursorAfterEditCall=%d\n",
                   path.c_str(), lineNumber, cursorBefore, changed ? 1 : 0, activeView().cursorIndex());
      std::fclose(f);
    }
  }
  if (changed && currentMode_ == ApplicationMode::File) fileMode_.loadFile(path);
  activeView().redraw();
  // 2026-09-20追加、直接のユーザー報告("-cではviから復帰後のブロック
  // カーソルが無い"): redraw()自体は(意図的に)決してsyncVisibleCursor()
  // を呼ばない――Command::Redrawや goto-line の各出口で既に同じ理由で
  // 呼んでいるのと同じ穴が、ここ(EDIT復帰)にも残っていた。
  activeView().syncVisibleCursor();
  if (const char *dbg = std::getenv("USILICGUIDEBUG")) {
    FILE *f = std::fopen(dbg, "a");
    if (f) {
      std::fprintf(f, "executeEdit: after redraw+sync, cursorIndex=%d\n", activeView().cursorIndex());
      std::fclose(f);
    }
  }
  return true;
}

std::vector<StyledLine> Application::wrapHelpText(int width) const {
  PipeBuffer pipe(encoding_, width, 16L * 1024 * 1024);
  pipe.feed(helpText_);
  pipe.flush();
  return pipe.lines();  // .attributesはそのまま素通しされる――cutMode_.split()自身のコメント参照
}

// 2026-09-20更新、直接のユーザー依頼("今のKEYをHELPに置き換え...
// 正味の：カットによるHELPとしてほしい"): main.cppからの通常の
// 起動時呼び出しでは、~/.local/lib/usilic/usilic.man(usilic -mで
// QUICK.txtから生成、legacy自身のHELP.jpと同じnroff重ね打ちバイト
// 列形式)の生の内容がそのまま渡される。テストからは引き続き任意の
// 合成テキストを直接注入できる(このシグネチャ自体は変更していない
// ――KEYのloadKeyText()のような「引数を取らない専用ローダー」とは
// 異なり、こちらは元々main.cpp自身のUSILICHELP環境変数の経路とも
// 共有されていた汎用の注入口)。
bool Application::loadHelpText(const std::string &text) {
  helpText_ = text;
  return !helpText_.empty();
}

// menu.cのM_help/T_helpケースから移植――なぜこれがそれ自身の表示の
// 仕組みではなくCutModeの分割(本物のCUTとHELPは1つのインスタンス
// を共有する)なのかについてはクラスコメント参照。どのモードから
// でも到達可能(legacyのinit_bp()のデフォルトケースにも、M_help/
// T_helpについてのモードの文脈のガードは無い)。
// 2026-09-20更新: CUTパターンをkHelpPattern(旧: 番号付き見出しの
// 正規表現)/GrepSyntax::Regexから、全角「：」の固定文字列検索
// (GrepSyntax::Fixed)へ切り替えた――kHelpPattern自身の定義コメント
// 参照。
bool Application::startHelp() {
  if (helpText_.empty()) return false;
  std::vector<StyledLine> helpLines = wrapHelpText(display_.columns());

  bool preserveExistingCut = (currentMode_ == ApplicationMode::Cut || currentMode_ == ApplicationMode::CutContent);
  bool ok;
  if (preserveExistingCut) {
    // 本物のCUTセッションが既に表示されている――(ネストした
    // splitSelected()の再分割が使うのと「同じ」仕組みで)それをプッシュ
    // するので、HELPから抜ける^Gは、Back既存のcanGoBack()/goBack()
    // チェック経由でそれを再開する。ここでは意図的にcutSyntax_/
    // cutOrigin_に触れない: どちらも単一の共有されたフィールドである
    // (ModeStackのレベルごとに保存/復元されない)ので、それらをその
    // ままにしておくことで、保存されている本物のcutが既に持っていた
    // ものが保たれる――HELP自身の都合でそれらを設定すると、^Gがそれ
    // を復元した時点で、その本物のcut自身の状態を壊してしまう。
    ok = cutMode_.pushSplit(helpLines, kHelpPattern, GrepSyntax::Fixed);
  } else {
    // 2026-09-14追加、直接のユーザー報告("-g: HELPのカーソル行が
    // 保存されていない（^Hで常に最上行）"): cutOrigin_はCUT系セッション
    // を抜けた後もそのまま残る(このファイル内、新規の最上位セッションを
    // 作るまさにこの手の代入以外では一切書き換えられない――他の全ての
    // 代入箇所を確認済み)ので、「今まさにcutOrigin_がHelpである」こと
    // 自体が「前回もHELPだった(=このcutView_のcursorIndex()は前回の
    // HELP閲覧位置をまだ保持している)」ことの十分な証拠になる。前回の
    // カーソル位置を退避してから、下のsplit()(常にインデックス0へ
    // リセットする、CutMode::split()自身のコメント参照)を呼び、直後に
    // 復元する。
    bool resumingHelpSession = (cutOrigin_ == CutOrigin::Help);
    int previousCursorIndex = resumingHelpSession ? cutView_.cursorIndex() : 0;
    cutReturnMode_ = currentMode_;  // 保存すべきものが無い――本物のCUTが使うのと同じ「Cutを...へ戻す」という錨
    // 新規の最上位の分割――リサイズ時のリフローの対象になる。split()
    // の返り値をチェックした後ではなく、それを呼ぶ前に設定する
    // ――なぜかについてはexecuteCut()自身のFileの分岐の2026-08-18の
    // コメント参照(CutMode::split()は、この関数が実行される機会を
    // 一度も得る前に内部でcutView_を再描画する;実際の報告――LOG
    // 自身のモードラインが一瞬"LOG"ではなく"CUT"を表示していた――
    // が、まさにこの順序のバグまで突き止められた、これはこのファイル
    // 内の他の全てのcutOrigin_の代入にも影響していたと判明した、
    // HELP自身のものも含めて)。
    cutOrigin_ = CutOrigin::Help;
    cutSyntax_ = GrepSyntax::Fixed;  // ここで設定しても安全: この分岐では古い値に依存する本物のものは何も無い
    ok = cutMode_.split(helpLines, kHelpPattern, GrepSyntax::Fixed);
    if (ok && resumingHelpSession && previousCursorIndex > 0) {
      int last = static_cast<int>(cutMode_.segments().size()) - 1;
      cutView_.setCursorIndex(std::clamp(previousCursorIndex, 0, std::max(0, last)));
      cutView_.redraw();
    }
  }
  if (!ok) return false;

  currentMode_ = ApplicationMode::Cut;
  return true;
}

// menu.cのT_winch処理から移植――これが保っている3方向の分割
// (Directory/Grep/Locate/Archive/Du/ListBrowserについてはプレーンな
// ビューポートのリサイズ、File/Manと「最上位の」Cut/CutContentの
// 分割についてはreflow()、Shellのptyは無条件にリサイズされる)、
// そしてなぜFile/Man自身のreflow()が文字通りのalloc_winch()の移植で
// はないのかについてはクラスコメント参照。
void Application::handleResize() {
  layoutViews();
  // 2026-08-30、実際のユーザー報告("フォントはOKだが文字数がおかしい
  // 、HELPはOKだけどね"): 以前はFile/ManモードがそれぞれcurrentMode_
  // として「今」アクティブな時にだけreflow()(実際の再描画込み)を
  // 呼んでいたため、GUIのフォント/ウィンドウサイズがDirectoryモード
  // を見ている間に変わると、それぞれのwidth_はApplication構築時の
  // 古い値のまま取り残されていた――後でDirectoryからファイルを開いた
  // loadFile()は、その古いwidth_で折り返してしまう(HELPが無関係
  // なのは、Application::wrapHelpText()が毎回display_.columns()を
  // その場で読み直す、全く別の使い捨てPipeBuffer経由だからで見つかった
  // ――FileMode/ManModeのようなキャッシュされたwidth_を一切持たない)。
  // setWidth()(redrawを伴わない軽量な更新)は非アクティブな方にも
  // 無条件に適用し、アクティブな方だけ追加でreflow()自身を呼んで
  // 実際に再描画する。
  fileMode_.setWidth(display_.columns());
  manMode_.setWidth(display_.columns());
  std::optional<int> epubRaw;  // 縦書きは、今のページの最初の行を保って組み直す
  if (currentMode_ == ApplicationMode::File && epubVerticalActive()) epubRaw = epubVerticalCurrentRaw();
  if (currentMode_ == ApplicationMode::File) fileMode_.reflow(display_.columns());
  if (epubRaw) rebuildEpubVertical(*epubRaw);
  if (currentMode_ == ApplicationMode::Cut && epubTocVerticalActive()) refreshEpubTocVertical();
  if (currentMode_ == ApplicationMode::Man) manMode_.reflow(display_.columns());
  // DirectoryMode::applyColumnAutoDisplay()自身のコメント参照
  // (2026-09-12追加、直接のユーザー依頼): 端末幅が変わり得た、まさに
  // このタイミングで、usilicrcのCOLUMNディレクティブに基づく自動判定
  // を再評価する。DirectoryModeにはFile/Man同様の「アクティブな時
  // だけreflow」制約は無い(entries_/lsLine自体は変わらず、単に表示
  // 層のfilenameOnlyMode_を切り替えるだけなので、Directory非アクティブ
  // 中でも安全に呼べる)。
  directoryMode_.applyColumnAutoDisplay();
  if ((currentMode_ == ApplicationMode::Cut || currentMode_ == ApplicationMode::CutContent) &&
      !cutMode_.canGoBack()) {
    std::vector<StyledLine> freshSource;
    if (cutOrigin_ == CutOrigin::Help) {
      freshSource = wrapHelpText(display_.columns());
    } else if (cutOrigin_ == CutOrigin::Directory) {
      // 2026-08-11: ディレクトリ一覧の行は、File/Manのテキストがそう
      // するようには幅に合わせてリフロー/折り返しされない
      // (DirectoryModeにはそれ自身のreflow()が無い――`ls -alF`の
      // 出力はレンダリング時に切り詰められるのであって、再取得され
      // るのではない)、そのため下のfileMode_.reflow()に相当する
      // ものをここで呼ぶものは何も無い;directoryView_の既に現在の
      // 行が、そのまま単に再分割されるだけである。
      freshSource.reserve(directoryView_.lines().size());
      for (const auto &line : directoryView_.lines()) freshSource.push_back({line.text, line.attributes});
    } else {
      fileMode_.reflow(display_.columns());
      freshSource.reserve(fileView_.lines().size());
      for (const auto &line : fileView_.lines()) freshSource.push_back({line.text, line.attributes});
    }
    int savedIndex = cutView_.cursorIndex();
    bool wasContent = (currentMode_ == ApplicationMode::CutContent);
    if (cutMode_.split(freshSource, cutMode_.pattern(), cutSyntax_)) {
      int last = static_cast<int>(cutMode_.segments().size()) - 1;
      if (savedIndex < 0) savedIndex = 0;
      if (savedIndex > last) savedIndex = (last < 0) ? 0 : last;
      cutView_.setCursorIndex(savedIndex);
      if (wasContent) openSelectedSegment();  // (願わくば同じ)セグメントでcutContentView_を再び満たす
    }
  }
  shell_.onResize(display_.rows(), display_.columns());
  activeView().redraw();
  if (promptActive()) promptEditor_.redraw();
}

void Application::relayoutForModeLineVisibilityChange() {
  // application.hpp自身の宣言コメント参照――項目461でTerminalDisplay::
  // columns()自体がmodeLineVisible()に連動するようになったため、
  // 単なるlayoutViews()+redraw()では幅依存のリフローが古いまま
  // 取り残される。handleResize()は本物の端末リサイズと全く同じ理由で
  // display_の現在のrows()/columns()を読み直すので、そのままここでも
  // 再利用できる。
  handleResize();
}

bool Application::wantsExit() const { return wantsExit_; }
void Application::requestImmediateExit() {
  saveResumableCutState();
  wantsExit_ = true;
}

bool Application::consumeGuiModeToggleRequest() {
  bool requested = guiModeToggleRequested_;
  guiModeToggleRequested_ = false;
  return requested;
}

// IDEAS.md自身のGUIモードのクリックの説明(legacyではない)から移植
// ――クリックで選択/再度クリックで開くという慣習についてはクラス
// コメント参照。長い間Directory専用だった(クラスコメント参照)が、
// 2026-08-14: 同じ方法でLOCATEの結果もカバーする直接のユーザー依頼
// ("マウスの左クリックでも選択可（←(ls)のように）を導入すれば
// 解消")、これは特に、別個の報告(ホバーされているがクリックされて
// いない行の、中間の省略記号の切り詰め幅のちらつき、
// ContentView::renderLine()自身のellipsisBudgetのコメント参照)への
// 修正として提供された――ポインタが通過する際に単に行をホバーする
// (そしてホバーを外す)のではなく、Directory自身の一覧が既にそう
// しているのと同じ方法で、クリックが即座に実際の選択を確定させる
// ようにする。
bool Application::handleMouseClick(int row, int column) {
  bool result = handleMouseClickImpl(row, column);
  showBufferLimitNoticeIfHit();  // usilicrcのBUFFER、それ自身のコメント参照
  return result;
}

bool Application::handleMouseClickImpl(int row, int column) {
  if (currentMode_ == ApplicationMode::Directory) {
    int idx = directoryView_.lineIndexAtRow(row);
    if (idx < 0) return false;
    // 2026-09-15修正、直接のユーザー依頼("（ls）、LOCATE等で導入して
    // いた「ドラッグエリア」も不要でいいので反映してみて"): かつては
    // ここでisColumnWithinLineScope()(パーミッション/サイズ/日付の
    // 左マージンをクリックスコープ外にする)を使っていたが、それは
    // 押下即座にクリックが実行されていた頃、猫の手ドラッグの開始点を
    // 確保するための2026-08-05のユーザー提案だった。左クリックの
    // 「実行」自体がボタン解放まで遅延するようになった(2026-09-15、
    // GraphicalDisplay::Impl::pendingLeftClick自身のコメント参照)
    // ことで、押下位置がクリックのスコープに入っていても、そのまま
    // ドラッグの開始点として自由に使えるようになったため、この前提
    // 自体が不要になった――isColumnWithinRowText()(上限のみ、左
    // マージンの除外なし)へ緩めた。
    if (!directoryView_.isColumnWithinRowText(idx, column)) return false;
    if (idx == directoryView_.cursorIndex()) return openSelectedEntry();
    directoryView_.setCursorIndex(idx);
    directoryView_.redraw();
    return true;
  }
  if (currentMode_ == ApplicationMode::Du) {
    // 2026-09-15追加、直接のユーザー報告("一覧上で画面ドラッグ
    // できるのはDU画面だけ...本来ならLOCATEのように左クリックした
    // 時点で内容表示された画面がスクロールするはずなのに、表示され
    // ないから一覧画面のままスクロールしている")――原因は
    // handleMouseClick()自体にDu分岐が丸ごと存在しなかったことで、
    // Directory/Archive/Locateがそれぞれ既に持っているクリック→
    // 選択/オープンの仕組みがDUにだけ配線されていなかった(結果、
    // クリックは常にfalseで無視され、代わりにドラッグだけが常に
    // 効いていた)。すぐ上のDirectoryMode自身の分岐と同じ理由で
    // isColumnWithinRowText()(上限のみ)を使う――マージン除外は
    // 同日のうちに不要と判明した(すぐ上のDirectoryMode自身の分岐の
    // コメント参照)。
    int idx = duView_.lineIndexAtRow(row);
    if (idx < 0) return false;
    if (!duView_.isColumnWithinRowText(idx, column)) return false;
    if (idx == duView_.cursorIndex()) return openSelectedDuEntry();
    duView_.setCursorIndex(idx);
    duView_.redraw();
    return true;
  }
  // 2026-09-20追加、Command::StartUnderlineView自身の宣言コメント
  // 参照(`^Ou`)――すぐ上のDu自身の分岐と全く同じ理由・同じ形。
  if (currentMode_ == ApplicationMode::Underline) {
    int idx = underlineView_.lineIndexAtRow(row);
    if (idx < 0) return false;
    if (!underlineView_.isColumnWithinRowText(idx, column)) return false;
    if (idx == underlineView_.cursorIndex()) return openSelectedUnderlineEntry();
    underlineView_.setCursorIndex(idx);
    underlineView_.redraw();
    return true;
  }
  if (currentMode_ == ApplicationMode::Archive) {
    // すぐ上のDirectoryMode自身の分岐と同じ理由でisColumnWithinRowText()
    // (上限のみ、左マージンの除外なし)を使う――2026-08-26に導入した
    // マージン除外(formatEntryLine()自身の右寄せサイズ欄+2スペースの
    // 隙間、loadArchive()自身のコメント参照)は、2026-09-15の直接の
    // ユーザー依頼により、DirectoryMode自身の分岐のコメントに書いた
    // 理由と同じく不要になった。
    int idx = archiveView_.lineIndexAtRow(row);
    if (idx < 0) return false;
    if (!archiveView_.isColumnWithinRowText(idx, column)) return false;
    if (idx == archiveView_.cursorIndex()) return openSelectedArchiveEntry();
    archiveView_.setCursorIndex(idx);
    archiveView_.redraw();
    return true;
  }
  if (currentMode_ == ApplicationMode::Locate) {
    int idx = locateView_.lineIndexAtRow(row);
    if (idx < 0) return false;
    // LOCATE自身の結果はプレーンな、属性の無い行である
    // (highlightStartColumnが無い――LocateMode::search()自身のLine
    // の構築参照)、そのためisColumnWithinLineScope()の左マージンの
    // スキップはここではno-opである(startのデフォルトは0)――固定
    // 形式の`ls -l`の行の中のファイル名の部分文字列だけではなく、
    // パス全体が「エントリ」であることに合わせて、レンダリングされた
    // 行全体がクリック可能である。
    if (!locateView_.isColumnWithinLineScope(idx, column)) return false;
    if (idx == locateView_.cursorIndex()) return openSelectedLocateResult();
    locateView_.setCursorIndex(idx);
    locateView_.redraw();
    return true;
  }
  // 2026-09-14追加、直接のユーザー報告("-g: HELPのハイライト行が
  // マウスのクリックで選べない"): 上のDu分岐自身のコメントと全く同じ
  // 原因――handleMouseClick()にCut分岐が丸ごと存在しなかった。HELPは
  // Cut/CutContentに乗る通常のCUTセッションの1つに過ぎない(クラス
  // コメント参照)ので、これは実はHELP固有のバグではなく、CUT自体
  // (実際のユーザー起動のCUTはもちろん、MAN/LOCATE/DUのCUT等も含む)
  // がGUIモードで一度もクリック選択に対応していなかった、という
  // より根深い既存の穴だった。Directory/Du/Archive自身の分岐と同じ
  // 理由でisColumnWithinRowText()(上限のみ)を使う――セグメントの
  // ラベルはls形式のパーミッション/サイズ/日付列を持たない、単純な
  // 1列のテキストなので。
  // 2026-09-25追加、直接のユーザー報告("^I（INFO）ではハイライトを
  // クリックしても開かない"): INFOはFILEモード上のバナー(bannerFilePath()
  // を読み込んだfileView_)として実装されているが、ここにFile分岐が
  // 無かった――上のDu/Cut分岐自身のコメントと同じ種類の欠落。バナー
  // 表示中に限り、開けない行(neverHighlighted、Boot options等)を除いて
  // Directory等と同じ「1回目で選択、2回目で開く」にする。
  if (currentMode_ == ApplicationMode::File && fileMode_.currentFile() == bannerFilePath()) {
    int idx = fileView_.lineIndexAtRow(row);
    if (idx < 0) return false;
    if (fileView_.lines()[idx].neverHighlighted) return false;
    if (!fileView_.isColumnWithinRowText(idx, column)) return false;
    if (idx == fileView_.cursorIndex()) return openSelectedBannerEntry();
    fileView_.setCursorIndex(idx);
    fileView_.redraw();
    return true;
  }
  if (currentMode_ == ApplicationMode::File && epubVerticalActive()) return clickEpubVertical(row, column);
  if (currentMode_ == ApplicationMode::Cut && epubTocVerticalActive()) return clickEpubTocVertical(row, column);
  // 2026-09-28追加(FileMode::followEpubLink()自身の宣言コメント参照): EPUBの本文
  // でも、リンク(下線)か画像("[image: ...]")のある行だけは、バナーと同じ
  // 「1回目で選択、2回目で開く」。他の行のクリックは従来どおり。
  if (currentMode_ == ApplicationMode::File && isEpubFile(fileMode_.currentFile())) {
    int idx = fileView_.lineIndexAtRow(row);
    if (idx >= 0 && clickEpubHorizontalLink(idx, column)) return true;  // 下線の字のクリック
    if (idx >= 0 && fileView_.isColumnWithinRowText(idx, column) &&
        (fileMode_.hasEpubLinkAt(idx) || fileView_.lines()[idx].text.find("[image: ") != std::string::npos)) {
      if (idx == fileView_.cursorIndex())
        return fileMode_.followEpubLink() || openEpubWebLinkAtCursor() || openEpubLineImage(fileView_);
      if (fileMode_.hasEpubLinkAt(idx)) {  // リンクは1回目で飛ぶ(clickEpubVertical()の2026-09-30の注記参照)
        fileView_.setCursorIndex(idx);
        return fileMode_.followEpubLink() || openEpubWebLinkAtCursor();
      }
      fileView_.setCursorIndex(idx);
      fileView_.redraw();
      return true;
    }
  }
  if (currentMode_ == ApplicationMode::Cut) {
    int idx = cutView_.lineIndexAtRow(row);
    if (idx < 0) return false;
    if (!cutView_.isColumnWithinRowText(idx, column)) return false;
    if (idx == cutView_.cursorIndex()) return openSelectedSegment();
    cutView_.setCursorIndex(idx);
    cutView_.redraw();
    return true;
  }
  return false;
}

bool Application::promptActive() const { return promptKind_ != PromptKind::None; }

// 2026-08-20追加、実際のUSILICGUIDEBUGグリッドダンプのキャプチャ(-gでの
// ^X^D)から原因を特定した: promptActive()単独(promptKind_ != None)は、
// RecentFile/RecentDirectoryの候補再呼び出し状態を意図的にカバーしない
// ――startPromptHistoryRecall()は、promptHistoryRecallKind_へ捕捉した
// 瞬間に、明示的にpromptKind_をPromptKind::Noneへ戻す(その関数自身の
// コメント参照)。そして既存のテスト群("opens a MAN/PROGRAM candidate
// list, NOT a prompt")は、そこでpromptActive()==falseであることを、
// 「これはピッカーであって、生きたテキスト編集ではない」という実際に
// 意味のある区別として意図的に固定している――promptActive()自体を
// promptHistoryRecallKind_もカバーするように広げる(この修正の最初の
// 試み)と、まさにそれらのテストを壊してしまった。そのためこれは別の、
// より狭い述語であり、呼び出し元が気にする別の問いのために存在する:
// 「今画面に出ているContentViewをたまたま再描画することが、キー入力
// 駆動のリフレッシュが既に丁寧に描いたものを台無しにする危険がある
// か」――生きたプロンプトと、このdirect-entryの再呼び出し画面の
// どちらでもtrue、通常のDirectory/Locate等の一覧ではfalse。
//
// キャプチャしたログはまさにこのギャップが引き起こす症状を示していた:
// runGuiPhase()自身のhover再描画ゲート(`!app.promptActive()`)が、
// ^X^D中の紛れ込んだSDL_MOUSEMOTIONを素通りさせ、素のlistBrowserView_
// .redraw()に落ちてしまう――これは、refreshDirectEntryModeLine()自身の
// drawPromptLabel()とは異なり、listBrowserView_の「旧」
// ModeLineFieldsProvider(右寄せの「戻り先」末尾フィールドを持つ方)経由
// で描画してしまう――そのため、直近の再描画がキー入力由来
// (refreshDirectEntryModeLine()、枠無し)だったか、たまたまのマウス
// 移動由来(listBrowserView_.redraw()、枠が再出現)だったかによって、
// そのフィールドがフレーム間でちらついて見えたり消えたりする。これは、
// ^Xf/PathInput自身の生きたフィールドについて一度既に修正したのと
// 全く同じバグの「クラス」である(2026-08-03、「^Xfの入力欄がマウスを
// 少し動かしただけで消える」)――その修正がうまくいったのは、PathInput
// 自身のactivatePrompt()がpromptKind_を正しく設定するため、
// promptActive()が既にそれをカバーしていたからだった。RecentFile/
// RecentDirectory自身の、意図的に異なるpromptKind_のクリア設計のため、
// この同じゲートの隙間に落ちてしまっていた。他のあらゆる
// promptActive()でゲートされたhover/再描画抑制の呼び出し箇所(main.cpp
// のマウス移動のみのhoverプレビュー、ドラッグ/慣性停止時の再描画)でも、
// runGuiPhase()自身のhover消費ゲートだけでなく、この状態について
// 全く同じクラスのギャップを持つため使われている――^Xfの前例となった
// 修正と同じ形である。
bool Application::redrawSensitivePromptOrRecallActive() const {
  return promptKind_ != PromptKind::None || promptHistoryContext_.has_value();
}
const std::string &Application::promptText() const { return promptEditor_.text(); }

ApplicationMode Application::currentMode() const { return currentMode_; }
Charset::Encoding Application::encoding() const { return encoding_; }

bool Application::awaitingSequenceOrPromptByte() const {
  return escPending_ || leaderJPending_ || leaderCPending_ || gotoLineGPending_ || gotoLineDigitsPending_ ||
         promptKind_ != PromptKind::None;
}
DirectoryMode &Application::directoryMode() { return directoryMode_; }
FileMode &Application::fileMode() { return fileMode_; }
GrepMode &Application::grepMode() { return grepMode_; }
CutMode &Application::cutMode() { return cutMode_; }
ManMode &Application::manMode() { return manMode_; }
ProgramMode &Application::programMode() { return programMode_; }
LocateMode &Application::locateMode() { return locateMode_; }
ArchiveMode &Application::archiveMode() { return archiveMode_; }
DuMode &Application::duMode() { return duMode_; }
UnderlineMode &Application::underlineMode() { return underlineMode_; }
ListBrowserMode &Application::listBrowserMode() { return listBrowserMode_; }
Shell &Application::shell() { return shell_; }
CpMvRmMode &Application::cpMvRmMode() { return cpMvRmMode_; }
EditMode &Application::editMode() { return editMode_; }
ExecMode &Application::execMode() { return execMode_; }
ContentView &Application::activeContentView() { return activeView(); }

void Application::setInterruptPeeker(std::function<bool()> peeker) { interruptPeeker_ = std::move(peeker); }

bool Application::openDroppedPaths(const std::vector<std::string> &paths) {
  if (paths.empty() || promptActive()) return false;
  bool opened;
  if (paths.size() == 1) {
    opened = executePathInput(paths.front());
  } else {
    // 2026-09-25変更、直接のユーザー要望("複数ファイルをusilic画面へ
    // ドラッグでは、pagerのusilicに渡しているが、^Ouへは渡せない？
    // 一覧の最上行^Mで表示"): `usilic a b c`起動と同じページャ
    // (startPagerFileList()、^Gで戻れない・'q'でusilic自体が終わる)
    // ではなく、(ls)で選んだファイルを^Ouした時と同じUnderline一覧へ
    // 渡し、その最上行を^Mしたのと同じ状態で開く――^Gで一覧へ戻り、
    // ';'/':'で巡回できる。一覧からの^Gの戻り先は、ドロップ時点の
    // (ls)のディレクトリ(startUnderlineFileView()と同じ)。
    std::vector<std::string> existing;
    for (const auto &path : paths) {
      std::error_code ec;
      if (std::filesystem::exists(path, ec)) existing.push_back(path);
    }
    if (existing.empty()) {
      opened = false;
    } else {
      underlineOriginDirectory_ = directoryMode_.currentDirectory();
      usilicLevel_ = UsilicLevel::None;
      underlineMode_.show(std::move(existing));
      currentMode_ = ApplicationMode::Underline;
      opened = openSelectedUnderlineEntry();
    }
  }
  activeView().redraw();
  showBufferLimitNoticeIfHit();
  return opened;
}

bool Application::offerShellCommand(const std::string &commandLine, std::function<std::string()> followUp,
                                    int retries) {
  if (!ensureShellActive()) return false;
  shell_.commandLine().setText(commandLine);
  shell_.redrawPromptRow();
  if (followUp) shellOffer_ = ShellOffer{std::move(followUp), commandLine, shell_.promptGeneration(), retries};
  else shellOffer_.reset();
  return true;
}

// offerShellCommand()自身の宣言コメント参照。実行ループから毎回呼ばれる。
// プロンプトが新しく出ても、入力欄がまだ置いた行のまま(=ユーザーがまだ
// 実行していない。SHELL起動直後の最初のプロンプト等)なら何もしない。
void Application::pollShellOfferFollowUp() {
  if (!shellOffer_) return;
  if (!shell_.alive()) {
    shellOffer_.reset();
    return;
  }
  int generation = shell_.promptGeneration();
  if (generation == shellOffer_->generation) return;
  shellOffer_->generation = generation;
  if (shell_.commandLine().text() == shellOffer_->offered) return;
  std::string next = shellOffer_->followUp();
  if (next.empty() || shellOffer_->retries <= 0) {
    shellOffer_.reset();
    return;
  }
  shellOffer_->retries--;
  shellOffer_->offered = next;
  shell_.commandLine().setText(next);
  if (currentMode_ == ApplicationMode::Shell) shell_.redrawPromptRow();
}

}  // namespace fm
