#pragma once

// 2026-08-30、直接のユーザー依頼: HELP/HELP.jpのオンラインヘルプ本文を
// C++移植版の現状に合わせて全面的に書き直した上で、ソースへ直接埋め込む
// (「使用前に読まれて『実はすでにGUI』ネタバレを回避すべく」)。
// レガシー版自身のsrc/doc/HELP・HELP.jp(old/自身が今もmake installで
// 使う、historyバイト単位の重ね打ち形式)は意図的に一切変更していない
// ――このヘッダはC++移植版専用の、全く別の新しいヘルプ本文である。
//
// 太字/下線は<b>...</b>/<u>...</u>という軽量な独自マークアップで表現し、
// expandHelpMarkup()(help_text.cpp)が、PipeBuffer::wrapLine()が既に
// 理解する昔ながらの重ね打ちバイト列(X\bX=太字、_\bX=下線、1文字ごと)
// へ実行時に一度だけ変換する――ファイル自体には制御文字が一切無いので
// 素のテキストエディタで安全に編集できる。
//
// 各章見出しは行頭から半角スペース2つ+数字+ピリオドで始まる
// ("  N.  Title"/"  N.M.  Title")――Application::kHelpPattern
// ("^  [0-9]+\\."、application.cpp)がこの形をそのまま前提にして
// CutMode::split()経由で章ごとに分割するため、新しい見出しを足す時も
// この形を厳密に守ること。

#include <string>

namespace fm {

// PipeBuffer(Charset::Encoding)がAsciiでない場合に使う日本語版基本章、
// および常時ASCII/その他の場合に使う英語版基本章。
constexpr const char *kHelpTextJa =
R"HELPTXT(
  1.  はじめに

  <u>control+g</u> でこの目次画面に戻ります。<u>control+p</u> / <u>control+n</u> でカーソルを上下に移動し、<u>return</u> で選択した章を開きます。<u>;</u> / <u>:</u> で前後の章へ直接移動できます。全キー一覧は <u>control+h</u> (KEY) で確認できます。

  本文中、^H は control+h、^J^H は ^J の後に ^H、return は ^M、esc は ^[ を表します。

  2.  モード一覧

  画面最下行の mode-line は [ファイル名] [現在行/総行] [ページ%] [サイズ] [プログラム名] [前のモード] の順で情報を表示します。^G で [前のモード] へ戻ります。

  2.1.  ファイル選択 (^J^K)

  カレントディレクトリの一覧を表示します。<u>return</u> で通常ファイルを開く、ディレクトリならそこへ移動します。<u>^O^M</u> は1行ずつ読み込む簡易ページャ、<u>^[^M</u> は制御コードもそのまま可視化して表示します。<u>^X^W</u> でバッファ内容を作業ファイルへ保存します。

  .tar / .tgz / .zip / .lzh / .rpm / .deb 等のアーカイブは自動的に内容一覧表示になります(ARC参照)。

  2.2.  KEY (^H)

  usilicrc で定義したキー割り当てと対応モードの一覧です。行を選んで <u>return</u> を押すと、その定義を呼び出したモードへ戻ってから実行します。

  2.3.  FGREP / EGREP (^S / ^J^S)

  文字列検索です。fgrep は入力文字列をそのまま検索、egrep は正規表現として検索します。結果一覧から <u>return</u> でバッファ内の該当箇所へ移動します。

  2.4.  CUT (^Jc / ^J^C / ^Jb / ^J^B)

  正規表現(または固定文字列)にマッチする行でバッファを分割し、目次のように一覧表示します。HELPのこの目次画面も内部的にはCUTの一種です。<u>;</u> / <u>:</u> で章の前後移動、目次一覧上でさらに検索した場合は <u>+</u> / <u>*</u> で該当行へ移動できます。

  2.5.  DU (^Jd / ^J^D)

  カレントディレクトリ以下の使用量を du(1) で集計表示します。^J^D で再実行せずDU画面へ戻ります。

  2.6.  LOCATE (^Jl / ^J^L)

  locate(1) によるファイル名検索です。事前に updatedb(1) が実行されている必要があります。

  2.7.  MAN (^Jm / ^J^M)

  man(1) を起動してマニュアルページを表示します。例: `^Jm 8 cron return`。

  2.8.  PROGRAM (^Jp / ^J^P)

  usilicrc の PROGRAM リストから選んだプログラムを実行します。リストに無い名前を入力した場合はSHELLへ切り替わります。

  2.9.  FILE / DIR (^Xf・^X^F / ^Xd・^X^D)

  アクセス履歴からファイル・ディレクトリを選んで移動します。<u>^R</u> でHISTORY一覧に切り替えられます。

  2.10.  HISTORY / COMMAND (^R / ^J^R)

  各モードで入力・選択した文字列の履歴です。COMMANDはSHELL/PROGRAMで実行したコマンドの重複無しリストで、どのモードからでも ^J^R で参照できます。

  2.11.  CP / MV / RM / ARC (^Xc・^Xm・^Xr・^Xa)

  ファイルの複写・移動・削除・アーカイブ展開です。<u>return</u> でマーク、<u>^X^M</u> で処理先(RMは`y`)を入力すると実行されます。<u>^X^Y</u> / <u>^X^N</u> で全マーク・全解除。ARCは展開先ディレクトリを指定するとアーカイブ全体を、個別ファイルを選べばそれだけを取り出せます。

  2.12.  SHELL (^[s / ^Xs / ^J. / ^J, / ^Jw / ^J^W)

  ログイン(または usilicrc 指定)シェルの仮想端末です。^Xs で再起動。^J. でSHELLのカレントディレクトリをFile-selectorに合わせ、^Jw / ^J^W でファイル名・バッファ内容をプロンプトへ挿入します。エディタ・ページャ等、端末制御を必要とするプログラムはSHELL内では正しく動作しません(EDIT/PROGRAM/MANモードをお使いください)。

  2.13.  ファイル名補完 (^I / tab)

  入力中の文字列を含むファイル名を補完します。候補が複数ある場合はビープ後、もう一度押すと一覧表示します。

  2.14.  COPY (^O^C)

  画面内の任意の文字列をコピーバッファへ保存します。^space で範囲の開始・終了、Command-lineの移動キーでカーソルを動かし、^G で確定。^O^Y で挿入します。

  2.15.  EDIT (^Je)

  usilicrc 指定のエディタを、現在行を+N オプションで指定して起動します。

  2.16.  ERROR (^X^Ke)

  子プロセスの異常終了時のエラー出力です。

  2.17.  LOG (^X^Kl)

  SHELLの操作ログです。

  2.18.  EXIT (^X^C / ^[^[)

  終了時、引数無し起動であればアクセス履歴を ~/.usilic/history に保存し、次回起動時に復元します。ERROR/LOGの内容も ~/.usilic/error・~/.usilic/shell に保存されます。

  2.19.  INFO (^I)

  起動オプション・usilicrcのパス・ERROR/SHELLログ・作業ファイルの一覧を表示します。旧バージョンの ^T に相当します。

  3.  設定ファイル (usilicrc)

  起動時に ~/.usilic/usilicrc (無ければ共有ライブラリの usilicrc) を読み込みます。主な設定項目:

     ARCHIVE / FILE / EXEC   拡張子ごとに起動するプログラムの対応表
     GREP / PROGRAM          EGREP・CUT・PROGRAMで使う候補リスト
     EDITOR                  EDITで起動するエディタ
     CP / MV / RM            各モードで実行するコマンドのオプション
     HISTORY / BUFFER        履歴の保存件数・通常ファイル用バッファ容量
     KEY                     KEYモードで表示するキー割り当てのカスタマイズ

  4.  補足

  usilic(1) はモードを実行するたびにモードスタックへ積み、^Gで一段戻ります。ディレクトリを直接指定して移動するとスタックはクリアされます。

  文字コードは起動時に自動判別し、^Oa で ASCII / EUC / UTF-8 を切り替えられます。環境変数 USILICCHARSET でも指定できます。

  5.  キー一覧

  5.1.  Window

     ^H            KEY
     PageDown/^V   画面を1ページ下へ
     PageUp/^[v    画面を1ページ上へ
     ↓/↑          カーソルを1行上下へ
     ^Z            サスペンド
     ^[^[          終了

  5.2.  Command-line (mode-line への文字列入力)

     ^F/^B         カーソルを右/左へ
     ^A/^E         行頭/行末へ
     ^K/^U         カーソルから行末/行頭まで削除
     ^Y            削除内容を挿入
     ^D            カーソル位置の文字を削除
     ^L            画面を再描画
     ^S            FGREP
     ^R            HISTORY
     ^I/tab        ファイル名補完
     ^Q            制御文字を直接入力

  5.3.  Interaction (SHELLでの対話的プログラム実行中)

     ^C            入力を中断
     ^D            入力を終了
     ^G            SHELLを抜ける
     ^H            back space
)HELPTXT";

constexpr const char *kHelpTextEn =
R"HELPTXT(
  1.  Introduction

  Press <u>control+g</u> to return to this table of contents. Move the cursor with <u>control+p</u> / <u>control+n</u> and press <u>return</u> to open a section. <u>;</u> / <u>:</u> jump directly to the previous/next section. The full key list is under <u>control+h</u> (KEY).

  In this text, ^H means control+h, ^J^H means ^J followed by ^H, return means ^M, and esc means ^[.

  2.  Modes

  The mode-line at the bottom of the screen shows [name] [current/total lines] [page %] [size] [program] [previous mode], in that order. ^G returns to [previous mode].

  2.1.  File-selector (^J^K)

  Lists the current directory. <u>return</u> opens a regular file or enters a directory. <u>^O^M</u> loads a file a line at a time (a simple pager); <u>^[^M</u> shows control codes as visible glyphs instead of interpreting them. <u>^X^W</u> saves the current buffer to a work file.

  Archives (.tar, .tgz, .zip, .lzh, .rpm, .deb, ...) automatically open as a listing -- see ARC below.

  2.2.  KEY (^H)

  Lists every key binding defined in usilicrc together with the mode it triggers. Selecting a row and pressing <u>return</u> returns to the mode that invoked KEY and runs that binding.

  2.3.  FGREP / EGREP (^S / ^J^S)

  Searches the buffer. fgrep matches the input literally; egrep treats it as a regular expression. <u>return</u> on a result jumps to that line.

  2.4.  CUT (^Jc / ^J^C / ^Jb / ^J^B)

  Splits the buffer wherever a pattern (fixed string or regex) matches, and shows the matches as a table of contents -- this HELP screen is itself one CUT session. <u>;</u> / <u>:</u> move between sections; after searching within the table of contents, <u>+</u> / <u>*</u> jump to the matching row.

  2.5.  DU (^Jd / ^J^D)

  Runs du(1) on the current directory. ^J^D returns to the last result without re-running it.

  2.6.  LOCATE (^Jl / ^J^L)

  Searches file names via locate(1) (updatedb(1) must have run beforehand).

  2.7.  MAN (^Jm / ^J^M)

  Runs man(1). Example: `^Jm 8 cron return`.

  2.8.  PROGRAM (^Jp / ^J^P)

  Runs a program from usilicrc's PROGRAM list. A name not in the list falls through to SHELL instead.

  2.9.  FILE / DIR (^Xf/^X^F, ^Xd/^X^D)

  Jumps to a file or directory from your access history. <u>^R</u> switches to the HISTORY list.

  2.10.  HISTORY / COMMAND (^R / ^J^R)

  Per-mode input history. COMMAND is a deduplicated list of commands run from SHELL/PROGRAM, reachable from any mode via ^J^R.

  2.11.  CP / MV / RM / ARC (^Xc, ^Xm, ^Xr, ^Xa)

  Copy, move, delete, or extract. <u>return</u> marks an entry; <u>^X^M</u> asks for a destination (RM asks for `y`) and runs the command. <u>^X^Y</u> / <u>^X^N</u> mark/unmark everything. In ARC, giving a destination directory extracts the whole archive; marking individual entries first extracts only those.

  2.12.  SHELL (^[s, ^Xs, ^J., ^J,, ^Jw, ^J^W)

  A virtual terminal running your login (or usilicrc-configured) shell. ^Xs restarts it. ^J. sets the shell's current directory to match File-selector; ^Jw / ^J^W insert the current file name / buffer contents at the prompt. Editors, pagers, and other programs that need real terminal control do not work correctly inside SHELL -- use EDIT/PROGRAM/MAN instead.

  2.13.  Filename completion (^I / tab)

  Completes a filename containing what you've typed so far. If more than one file matches, it beeps; pressing it again lists the candidates.

  2.14.  COPY (^O^C)

  Saves any on-screen text into the copy buffer. ^space marks the start/end of the range, the usual Command-line motion keys move the cursor, ^G confirms. ^O^Y pastes it back.

  2.15.  EDIT (^Je)

  Launches the editor configured in usilicrc, passing the current line as a `+N` option.

  2.16.  ERROR (^X^Ke)

  Standard error captured from a failed child process.

  2.17.  LOG (^X^Kl)

  The SHELL session log.

  2.18.  EXIT (^X^C / ^[^[)

  On exit from a no-argument launch, your access history is saved to ~/.usilic/history (restored on the next launch), along with ERROR/LOG contents under ~/.usilic/error and ~/.usilic/shell.

  2.19.  INFO (^I)

  Shows boot options, the usilicrc path, ERROR/SHELL log paths, and any allocated work files. This replaces the older ^T screen.

  3.  Configuration (usilicrc)

  Read from ~/.usilic/usilicrc at startup (falling back to the shared usilicrc if that doesn't exist). Notable entries:

     ARCHIVE / FILE / EXEC   which program to run for which file suffix
     GREP / PROGRAM          candidate lists used by EGREP/CUT/PROGRAM
     EDITOR                  the editor EDIT launches
     CP / MV / RM            command-line options for each mode
     HISTORY / BUFFER        history size / buffer size for regular files
     KEY                     customizes the bindings shown in KEY

  4.  Notes

  Each mode you enter is pushed onto a mode stack; ^G pops one level. Jumping straight to a directory clears the stack.

  Character encoding is auto-detected at load time; ^Oa cycles ASCII / EUC / UTF-8. USILICCHARSET can set it explicitly.

  5.  Key Reference

  5.1.  Window

     ^H            KEY
     PageDown/^V   page down
     PageUp/^[v    page up
     down/up       cursor down/up
     ^Z            suspend
     ^[^[          quit

  5.2.  Command-line (typing into the mode-line)

     ^F/^B         cursor right/left
     ^A/^E         start/end of line
     ^K/^U         delete to end/start of line
     ^Y            paste deleted text
     ^D            delete char under cursor
     ^L            redraw screen
     ^S            FGREP
     ^R            HISTORY
     ^I/tab        filename completion
     ^Q            insert a literal control character

  5.3.  Interaction (an interactive program is running inside SHELL)

     ^C            interrupt
     ^D            end input
     ^G            leave SHELL
     ^H            backspace
)HELPTXT";


// Display::isGraphical()が真の間だけApplication::wrapHelpText()が
// 末尾に追加する、GUI専用の章。
constexpr const char *kHelpTextGuiJa =
R"HELPTXT(

  6.  GUI (-g / ダブルクリック起動)

  ここは GUI ウィンドウ表示中だけに現れる章です。

  6.1.  フォントサイズ・配色

  <u>Cmd +</u> / <u>Cmd -</u> でフォントサイズを変更、<u>Cmd 0</u> で起動時のサイズへ戻します。<u>Cmd K</u> で組み込みの配色(標準・緑地に黒・黒地にアンバー・白地に黒)を順に切り替えます。変更内容は次回起動時にも記憶されます。

  6.2.  フォントの指定

  起動時に <u>-f <フォントファイルのパス></u> を指定すると、そのフォントを使用します。一度指定すれば以後は記憶され、アイコンからのダブルクリック起動でも同じフォントで開きます。

  6.3.  マウス操作

  一覧・内容表示エリアをクリックしたままドラッグすると画面をスクロールします。右端のスクロールバーを直接ドラッグすると、その位置へ一気に移動します。一覧上の行は、クリックでカーソル移動、すでにカーソルがある行を再度クリックすると <u>return</u> と同じ選択動作になります。

  6.4.  前回の続きから再開

  引数無しで起動すると、前回終了時に通常ファイルまたはアーカイブの内容一覧を表示していた場合は同じ画面から再開します。該当のファイルが移動・削除されていた場合は、その在籍先ディレクトリ(無ければ実在する祖先ディレクトリ)で起動します。
)HELPTXT";

constexpr const char *kHelpTextGuiEn =
R"HELPTXT(

  6.  GUI (-g / launched from an icon)

  This section only appears while running in a GUI window.

  6.1.  Font size and colors

  <u>Cmd +</u> / <u>Cmd -</u> change the font size, <u>Cmd 0</u> resets it to the size the window opened with. <u>Cmd K</u> cycles the built-in color presets (default, green-on-black, amber-on-black, black-on-white). Changes are remembered for the next launch.

  6.2.  Choosing a font

  Pass <u>-f <path-to-a-font-file></u> at startup to use that font. Once given, it is remembered from then on -- including when the app is opened later by double-clicking its icon.

  6.3.  Mouse

  Click and drag inside a listing or content view to scroll it. Dragging the scrollbar on the right jumps straight to that position. Clicking a row moves the cursor there; clicking the already-selected row again acts like pressing <u>return</u>.

  6.4.  Resuming where you left off

  A no-argument launch resumes exactly where the previous session ended, if that was a regular file or a top-level archive listing. If that path no longer exists, it starts in whichever directory it used to live in (or the nearest surviving ancestor).
)HELPTXT";


// <b>X</b>/<u>X</u>を、PipeBuffer::wrapLine()が理解する昔ながらの
// 重ね打ちバイト列(1文字ごとのX\bX/_\bX)へ変換する。定義はhelp_text.cpp。
std::string expandHelpMarkup(const std::string &text);

}  // namespace fm
