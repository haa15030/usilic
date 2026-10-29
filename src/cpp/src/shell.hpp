#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "charset.hpp"
#include "content_view.hpp"
#include "display.hpp"
#include "line_editor.hpp"
#include "pipe_buffer.hpp"

namespace fm {

// Shellセッションのオプション。(Shell内部にネストされるのではなく)
// 名前空間スコープなのは、ChildProcessOptionsと同じ理由による:
// ネストされた構造体のデフォルトメンバ初期化子は、それを参照する
// メンバ関数のデフォルト引数を囲むクラスがパースする時点でまだ
// 見えていない――完全な経緯についてはchild_process.hppの
// ChildProcessOptionsに関するコメント参照(理論上の話ではなく、
// このプロジェクトで一度これを誤ったことによる実際のバグが
// 起きた)。
struct ShellOptions {
  // ptyへexecされるプログラム + argv(例えば{"bash"}や
  // {"/bin/tcsh"})。ここで$SHELLやusilicrcのCHILD/SHELLディレクティブ
  // から解決するのは意図的にしていない――どのプログラムを実行するか
  // はConfigurationの仕事である(child.cのsave_child()/get_child()
  // 参照)。ChildProcessが自身のargvを決して自分で決めないのと
  // 同じに合わせている。
  std::vector<std::string> argv = {"/bin/sh"};
  // Shellがバイトストリーム内で自身のプロンプト行を認識できるように
  // PS1に織り込まれる、十分にユニークな番兵(legacy: s_S(S_mark))。
  // PS1自体がそれを2回埋め込むので、2つのShellインスタンス(または
  // Shellと同じptyへ何か他のものが出力するもの)は1つのプロセスの
  // マークを共有してはならない――buildSetupCommand()のコメント参照。
  std::string promptMark = "<usilic>";
  // 2つのpromptMarkの出現の間にPS1へ織り込まれる(legacy:
  // getenv(ENV_PROG)、つまりこのプログラム自身の名前)――プロンプト
  // 内の純粋に装飾的/診断的なテキストで、マッチングには使われない。
  std::string programName = "usilic";
  Charset::Encoding encoding = Charset::Encoding::Ascii;
  int rows = 24;
  int columns = 80;
  // 内部のPipeBufferへ転送される、バイト単位でのスクロールバック
  // 内容の合計予算(legacy: 対応物無し――連結リストのスクロール
  // バックは単に無制限に成長した)。PipeBuffer::overBudget()により
  // 呼び出し元は気付いて例えば警告/削減でき、PipeBufferがファイル/
  // パイプの内容に対して既に公開しているのと同じつまみに合わせて
  // いる。
  long scrollbackBudget = 1 << 20;
  // buildSetupCommand()自身のコメント参照(2026-08-11、"移植されて
  // いない: PAGERのexport"): 空(デフォルト)は、legacy由来のこの
  // PAGER/MANPAGERのexportを丸ごと省略する(実際のバイナリパスを
  // 知らないテスト用のShellOptions{}で、無関係な子プロセスがうっかり
  // $PAGERとしてusilic自身を起動してしまうことを避ける)。main.cpp
  // が自身の実行ファイルの絶対パスをここへ設定した時だけ、実際に
  // export/setenvされる。
  std::string pagerPath;
};

// SHELLモードのセッション: pty駆動の対話的な子シェル(bash/
// csh/...)、完了したプロンプト+出力行のスクロールバック、そして
// 編集中の入力途中のコマンド行。shell.cのmake_shell()/
// start_shell()/output_shell()/input_shell()/read_line()/
// write_shell()、そしてsub.cのopen_pty()(ほぼ逐語のまま――
// shell.cppのopenPty()参照)からの移植。
//
// これは逐語的な移植ではない。legacyのSHELLモードは3つの協調する
// プロセス――メインのusilicプロセス、メインプロセスがキー入力を待って
// ブロックしている間ptyの排出を続けるためだけにforkされた2つ目の
// 「表示」プロセス、そしてシェル自身――が、SIGALRMをメッセージ
// バスの呼び鈴として乱用した8個の手作りパイプ(同期リクエストの
// ためのwrite_comm()/read_comm()/wait_comm()、「新しいプロンプト
// が現れた」ための2つ目の非同期プッシュチャネル)でやり取りする
// ものである。ここにはそれは全く存在しない: poll()/select()が
// 利用可能なので、1つのプロセスがptyのfdとキーボードのfdを直接
// 多重化でき、そのため、それがプロセス間をフィールドごとに運んで
// いた重複したPP/RVスクロールバック構造体を含む、IPCプロトコル
// 全体が単純に不要になる。それが解消された後に残るものは比較的
// 小さい: ptyプロセスのライフサイクル、その出力中のシェル自身の
// プロンプトの認識、報告されたcwdの追跡、そして入ってくる各バイト
// をスクロールバックか生きたコマンド行のどちらかへルーティング
// すること。
//
// 意図的に除外(Shellではなく他の既に移植済みまたは将来のクラスに
// 属する):
//   - ptyの出力の汎用的な折り返し/タブ/オーバーストライク太字/
//     下線のパース: PipeBufferの仕事。Shellは内部に1つ所有し、
//     read_line()のff.bs/ff.md/ff.usステートマシンを再実装する
//     のではなく、プロンプトでないバイトをそれへ供給する。
//   - スクロールバックの保存とスクロール機構(1行/ページ単位の
//     上下、ハードウェアスクロール対再描画): ContentViewの仕事。
//     Shellは、shell.c自身のPP/RV連結リストとnext_line()/
//     shell_row()/shell_page()の代わりに、内部に1つ所有する。
//   - 1行編集の機構(挿入/削除/カーソル移動/kill-yank/UTF-8列
//     追跡): LineEditorの仕事。Shellは、入力途中のコマンド行の
//     ために内部に1つ所有する。呼び出し元は、Shellが自身で
//     insert/backspace等を再度公開するのではなく、通常のキーを
//     commandLine()へ直接転送する。
//   - 生のキーボードバイトの読み取り、エスケープシーケンスの
//     デコード、SIGWINCHの検出: 既にKeyInputの仕事。Shellは、
//     呼び出し元がリサイズが起きたと判断した後にだけそれに反応
//     し(onResize())、呼び出し元が生のバイトが他の特別なキー
//     ではないと判断した後にだけ通常のコマンドライン用のバイト/
//     Enter/Tabを受け取る。
//   - TAB補完の候補生成(make_list()): LineEditor自身の除外と
//     同様、将来のPathCompleterクラスへ明示的に委ねられる。
//     Shellは、呼び出し元が提供するプロバイダに問い合わせて
//     答えを繋ぎ戻す方法だけを知っている。
//   - コマンド/ディレクトリ履歴の保存(save_history()/
//     save_str()): 将来のHistoryクラスの仕事。Shellは、保存を
//     自分で所有するのではなく、呼び出し元が提供する記録用の
//     コールバックを呼ぶ。
//   - 外部の対話的PROGRAMを実際に実行すること(do_prog()の
//     raw-mode一時中断/フォアグラウンドで実行/「Hit any key」
//     という一連の動作): Mode/Applicationの仕事、ChildProcess
//     自身のヘッダーが非対話的なケースに対して既に描いているのと
//     同じ分割。Shellは、入力されたコマンドの末尾のパイプライン
//     段が(呼び出し元が提供する述語経由で)設定済みのPROGRAMに
//     一致することだけを「決め」、引き渡す――submitLine()参照。
//     呼び出し元は、その引き渡しを実装する際、f_waitの教訓
//     (CLAUDE.mdの履歴: プログラム実行後の無条件の「Hit any
//     key」の一時停止は、ユーザーの次のキー入力を黙って食べて
//     しまう)を覚えておかなければならない。一度引き渡した後、
//     Shell自身はそれ以上することが何も無い。
//   - プロセス間の単一インスタンスのソケット/ファイルの引き渡し
//     (from_main()): Application/セッションシングルトンの関心事
//     であり、Shellのものではない。
//   - 画面描画のプリミティブ: Displayの仕事。このコードベースの
//     他の全ての場所と同じく、所有されているContentView/
//     LineEditorを通じてのみ到達される。
class Shell {
 public:
  Shell(Display &display, const ShellOptions &options);
  ~Shell();

  Shell(const Shell &) = delete;
  Shell &operator=(const Shell &) = delete;

  // -- pty/子プロセスのライフサイクル --
  // ptyを開き、forkし、スレーブ側へoptions.argvをexecし、PS1/
  // プロンプトマーカーの設定コマンドを送る。何らかの失敗(pty
  // 割り当て、fork、または最初のハンドシェイクが完了する前に
  // シェルが終了する)ではfalse。Shellは不活性のままになり、
  // start()はリトライしてよい。
  //
  // `initialDirectory`(2026-09-15追加、直接のユーザー報告
  // "usilic -c ~/.usilic/savedで起動→^[sでカレントディレクトリが
  // ~/.usilic/saved/じゃない"): 空でなければ、セットアップコマンド
  // 自身の最初の文としてここへ`cd`するbashスクリプト文を1つ差し込む
  // ――usilic自身のプロセスは(application.cpp各所のコメント参照)
  // 決してchdir()しないので、これが無いと新しいbash子プロセスは
  // usilic自身のプロセス全体のcwd(典型的にはusilicが最初に起動
  // された時の場所で、今表示中のDirectory一覧とは無関係)をそのまま
  // 継承してしまっていた。空文字列(既定)は以前通りcd文自体を丸ごと
  // 省略する――ShellOptions{}を使う既存のテストの挙動を変えない。
  bool start(const std::string &initialDirectory = "");
  bool alive() const;
  // 現在の子のpid、開始されていなければ-1――純粋に、呼び出し元
  // (例えばK-60/RestartShell自身のテスト)が、再起動が同じ
  // プロセスの再利用ではなく本当に新しいプロセスを生み出したことを
  // 確認できるように公開されている。
  int childPid() const;
  // SIGHUP + 子が死ぬのを待ち、ptyを閉じる。冪等。デストラクタから
  // も呼ばれる。start_shell()の`mess`無し(シャットダウン)分岐と
  // output_shell()のshell_hup/case -1の後始末からの移植で、独立して
  // 後始末すべき別の表示プロセスがもう存在しないので1つの経路へ
  // 折りたたまれている。
  void terminate();

  // ptyのマスターfd、所有者のpoll()/select()ループのため。開始
  // されていなければ-1。
  int ptyFd() const;

  // -- ptyの出力 --
  // ptyFd()が読み取り可能な時に呼ぶ。1回の非ブロッキングread()を
  // 行い、届いたものを処理する。EOF/エラー(シェルの子が終了した、
  // またはptyが無くなった)ではfalseを返す。processOutputBytes()の
  // 薄い接着剤――テストが本物のptyなしで合成バイト列を使ってパース
  // ロジックを行使できるよう、別々に保たれている。
  bool onPtyReadable();
  void processOutputBytes(const std::string &bytes);
  // 出力(か子の終わり)が届いていればtrue。timeoutMsまで待つ。WindowsのネイティブはptyFd()が無い(-1)ので、
  // メインのループはpoll()の代わりにこれで確かめる(2026-10-16、ConPTY)。
  bool outputReady(int timeoutMs = 0);
  // シェルがcmd.exe(Windowsのネイティブで、usilicrcのSHELLにbash等のパスが無い時の既定)。Applicationが
  // SHELLへ打ち込むコマンドの書き方(引用符・cd)をsh系と変えるため。
  bool usesCmdSyntax() const;

  // -- コマンド行 --
  LineEditor &commandLine();
  ContentView &scrollback();

  // 2026-08-25のバグ修正(実際の、注意深く再現された報告: "③を
  // 飛び越えて②へ"――「最新の」スクロールバックエントリは^P経由
  // では全く到達できず、最初の一押しが常にそれを飛び越して直前の
  // ものへ行ってしまっていた)。scrollback_自身のcursorIndex_は、
  // 何もそこから積極的に閲覧して離れていない限り常に
  // lines().size()-1(最新のエントリ)に留まる――Application自身の
  // `browsingHistory`ローカル変数(cursorIndex() != lines().size()-1
  // として計算される)は、両方とも「同じ」cursorIndex_の値なので、
  // 「ここに留まっている、一度も閲覧していない」を「閲覧中で、
  // 現在まさにこのエントリの『上』にいる」と区別できない。実際の
  // 結果: 休止状態からの最初の^P押しはmoveUp()を呼び、それは
  // 最新のエントリの「上に」着地するのではなく無条件でそれを
  // 通り越して減算する――そのため、たった1つの最も有用な履歴
  // エントリ(ちょうど実行したばかりのもの)は、キーボード経由では
  // 一度も再選択できなかった。しかもスクロールバックエントリが
  // 合計1つしか無い場合、インデックス0(その底)からのmoveUp()は
  // 純粋に何もせず、「^Pは何もしない」ように読める。このフラグは、
  // 生のインデックスから切り離された、「積極的に閲覧中」という
  // それ自身の独立した状態を作る: Applicationの^Pハンドラは、
  // (現在の――既に正しい――休止位置の上に着地するだけで、インデック
  // ス変更は全く無い)最初の押しでbeginHistoryBrowsing()経由でこれを
  // 設定し、既にtrueである状態での後続の押しでだけ
  // scrollback().moveUp()を呼ぶ。exitHistoryBrowsing()(フラグの
  // リセットを、既に全ての「生きた編集への復帰」のトリガー――新しい
  // 出力の到着、新しいコマンドの送信、Enterによる選択された行の
  // 再実行とそのままの続行、またはビューを先に元へスナップさせる
  // 他の任意のキー――に伴っていたgoToBottom()呼び出しと束ねる)は、
  // 各呼び出し箇所がそれぞれ個別に覚えておく必要がある代わりに、
  // 全ての出口地点でこの不変条件を確実に守らせる。
  // 2026-08-26(直接のユーザーによる追加、2ラウンドのソクラテス式
  // 対話を経て解決――完全な理由についてはContentView::
  // setCountLivePromptAsLine()自身の宣言コメント参照): 生きた
  // プロンプト行自身の「+1」は、実際にそこに留まっている間だけ
  // 位置フィールドに属し、「本物の」scrollback_の行を閲覧している
  // 間は決して属さない――そのため、このクラス自身のhistoryBrowsing_
  // が切り替わる全ての場所は、まさにscrollback_自身の
  // atLivePromptLine(同じ概念のContentView側の半分)も切り替わる
  // べき場所であり、他の「別の」呼び出し元がそれを個別に覚えて
  // おくことに頼るのではなく、ここで歩調を合わせて保たれる。
  bool historyBrowsing() const { return historyBrowsing_; }
  // 2026-08-26のバグ修正: beginHistoryBrowsing()は以前、状態を
  // 切り替えるだけで、呼び出し元「自身」の後続のredrawPromptRow()/
  // moveDisplayCursorToShellHistoryLine()呼び出し(Applicationの^P
  // ハンドラ)がその変化を可視化することに頼っていた――しかしそれら
  // の「どちらも」全くモードラインに触れない
  // (redrawPromptRow()は生きたプロンプト行自体だけを塗り直す。
  // moveDisplayCursorToShellHistoryLine()は端末自身の点滅する
  // カーソルを動かすだけ)ので、最初の^P押し自身の「+1」位置変更
  // (setAtLivePromptLine()自身のコメント参照)は、何か「後の」、
  // 無関係な再描画がようやく追いついてくるまで、計算済みだが
  // 描画されないまま座っていた――実際のPTYキャプチャは、それが
  // 1回キー入力が遅れてから現れることを発見した。ここでのredraw()
  // は、呼び出し元がそれを知っていることに頼るのではなく、これを
  // 自己完結させる。
  void beginHistoryBrowsing() {
    historyBrowsing_ = true;
    scrollback_.setAtLivePromptLine(false);
    scrollback_.redraw();
  }
  // 2026-08-26のバグ修正(beginHistoryBrowsing()自身のものと一緒に
  // 発見、同じ実際のPTYキャプチャ): 下のgoToBottom()は「内部で」
  // 再描画する(自身のコメント参照)――これが元々そうしていたように、
  // setAtLivePromptLine(true)をそれの「後に」順序付けると、その
  // 内部の再描画は依然として古びたatLivePromptLine_==falseに
  // 対して実行され、「まだ閲覧中」の位置テキストを1回早く塗って
  // しまう。ここでは先に切り替えるので、goToBottom()が既に行う
  // その「同じ」再描画が、自力で正しい、最終的な値を塗る――上の
  // beginHistoryBrowsing()とは異なり、後に別のredraw()呼び出しは
  // 不要。
  void exitHistoryBrowsing() {
    historyBrowsing_ = false;
    scrollback_.setAtLivePromptLine(true);
    scrollback_.goToBottom();
  }

  // Enter: 現在のコマンド行の末尾のパイプライン段が、(設定されて
  // いれば)programLauncher_経由で設定済みの対話的PROGRAMと一致
  // するかを決めて引き渡すか、それ以外の場合は行をptyへそのまま
  // 書き込み、(設定されていれば)historyRecorder_経由でそれを記録
  // する。どちらの場合でもコマンド行はクリアされる。input_shell()
  // の'\r'ケース(パイプ分割/find_prog()検索ロジック自体。
  // do_prog()とPROGRAMテーブル検索はここでは移植されていない、
  // クラスコメント参照)からの移植。
  void submitLine();

  // 生きたプロンプト行(commandLineRow())を再描画する: 太字の
  // プロンプト接頭辞テキスト(promptDecorated_)に続けて
  // commandLine_自身の編集可能フィールド。その行を空白にした
  // かもしれないscrollback_の変更「後」ならどこでも必要
  // ――定義自身のコメント参照。
  void redrawPromptRow();
  // プロンプトの名前(既定は"usilic-1.5b"、2026-10-29: 最初の起動の機能限定SHELLは"USILIC"、
  // Application::setFirstStartPager()参照)。次にプロンプトを描く時から。
  void setPromptName(const std::string &name) { promptName_ = name; }

  // 2026-09-18追加、HELP.txt 2.13章「LIST」の実装
  // (old/src/key.c::make_list()/input_key()参照): TABで候補が複数
  // (曖昧)な場合の一覧表示を運ぶための戻り値。`showList`がtrueの間
  // だけ`candidates`/`partial`が有効――呼び出し元(Application)は
  // これを見てListBrowserMode経由の一覧選択画面へ遷移する。Shell
  // クラス自身はApplicationMode/ListBrowserModeを一切知らない
  // (レイヤリング維持――クラスコメント参照)ので、遷移そのものは
  // 呼び出し元の仕事のまま。
  struct TabOutcome {
    bool showList = false;
    std::vector<std::string> candidates;
    std::string partial;
    // 2026-09-18追加、実機報告(③「^Nで非ハイライトにはなるものの
    // 編集不能」)を受けて追加: partialの「前」にあったバッファの
    // 内容(例えば"cat ")――呼び出し元(Application::showTabCompletionList()
    // /feedTabCompletionListByte())が、一覧の閲覧からの「編集を開始
    // する」遷移時に、この後ろへ選択中の候補を繋いで完全なコマンド
    // 行を再構成するために必要。
    std::string prefixBeforeWord;
  };
  // TAB: 入力バッファ全体に区切り文字が無ければ(shouldSearchPath()
  // 自身のコメント参照)programCompletionProvider_($PATH検索)、
  // そうでなければ従来通りcompletionProvider_(カレントディレクトリの
  // ファイル名)に、カーソルで終わる部分的な単語に一致する候補を
  // 尋ねる。候補1件なら最長共通接頭辞まで補完してコマンド行へ繋ぎ
  // 戻す(2026-09-15までの既存の挙動、無変更)。候補が複数(曖昧)の
  // 場合は最長共通接頭辞まで補完しつつビープ音を鳴らし、直後にもう
  // 一度(間に他の編集操作を挟まずに)TABが来た場合だけ一覧表示を
  // 要求する(`tabArmed_`、legacyの`f_list`フラグと同じ役割――
  // Application::feedShellByte()自身の冒頭が、TAB以外の任意のバイト
  // でこれをリセットする)。
  TabOutcome onTab();
  void setCompletionProvider(
      std::function<std::vector<std::string>(const std::string &partialWord)> provider);
  // 2026-09-18追加: pathProgramCandidates()相当の$PATH検索版
  // Provider。設定されていなければ常にcompletionProvider_(カレント
  // ディレクトリ)を使う――既存のテスト用の合成Providerとの後方
  // 互換のため。
  void setProgramCompletionProvider(
      std::function<std::vector<std::string>(const std::string &partialWord)> provider);
  // Application::feedShellByte()自身の冒頭から、TAB以外の任意の
  // バイトを処理する直前に呼ばれる――tabArmed_自身の宣言コメント
  // 参照。
  void resetTabArm();

  // 生のバイトをコマンド行エディタを完全に迂回してptyへ直接
  // 書き込む――例えばCtrl-Cを転送して、シェルが現在フォアグラウンド
  // で実行しているものを中断させるため。どのキーがここへ、どれが
  // commandLine()へルーティングされるかは呼び出し元が決める。
  void sendControlByte(unsigned char byte);

  // -- ディレクトリ追跡 --
  // 新しく検出されたプロンプトのディレクトリが前のものと異なる
  // 時に常に発火する。write_shell()のプロンプト文字列からの
  // ディレクトリ抽出からの移植で、プロセス全体の直接のchdir()
  // ではなくコールバックとして公開されている――それを気にする
  // 他のコード(例えばファイルブラウザモード)は、Shellが黙って
  // グローバルなプロセス状態を変異させるのではなく、これに反応
  // すべきである。
  void setDirectoryChangedCallback(std::function<void(const std::string &directory)> callback);
  const std::string &currentDirectory() const;

  // 実行中のシェルの子自身のベース名(例えば"bash"、"zsh")。
  // legacy自身のdisp_shell()モードラインの"arg"フィールド
  // (sprintf("%c(%s)", K_z, chp->argv[0])、shell.c参照)に
  // 合わせている――実際のユーザー報告(2026-08-02)がlegacy
  // ビルドと並べて比較し、このフィールドがこの移植自身のSHELL
  // モードラインから完全に欠けていることを発見した。start()の
  // 前(またはoptions_.argvが何らかの理由で空の場合。ShellOptions
  // 自身のデフォルトは決してそれを空のままにしない)は空。
  std::string programName() const;

  // -- PROGRAMの引き渡し/履歴 --
  // 与えられた末尾のパイプライン段のプログラムトークンを実行した
  // (そして今それに責任を持つ)場合はtrueを返す。falseの場合は
  // 代わりにShellがその行をプレーンテキストとしてptyへ書き込む。
  void setProgramLauncher(
      std::function<bool(const std::string &programToken, const std::string &fullLine)> launcher);
  void setHistoryRecorder(std::function<void(const std::string &line)> recorder);
  // 2026-10-19: 子が全画面へ切り替えた(ESC[?1049h等)時に呼ばれる。consumeEscapeByte()参照。
  void setFullScreenCallback(std::function<void()> callback);
  // 引き渡された対話的PROGRAMがまだ実行中かどうか――呼び出し元が
  // 駆動する(Shellは一度引き渡した後は自力で知る方法が無い)。
  // trueの間、他のモードにアクションを拒否させるために使われる。
  // is_inter()からの移植。
  void setBusy(bool busy);
  // 2026-09-06追加、直接のユーザー報告("旧では`^[scat^M`でちゃんと
  // 入力待ちになる...^Gで抜けた後^J.でSHELL busyと出る...環境が
  // どこかで食い違ってるんじゃないの？"、実機PTYで再検証し前回の
  // 誤った判定を訂正): setBusy()自身の宣言コメントが元々認めていた
  // 「Shellは(PROGRAM起動を)一度引き渡した後は自力で知る方法が
  // 無い」という制約は、`cat`のようなPROGRAM一覧に載っていない
  // ただのシェルコマンドが今もフォアグラウンドで実行中かどうかには
  // 全く対応していなかった――legacy自身のis_inter()は、専用の
  // ヘルパープロセスとの往復通信でこれを検出していたが、この移植は
  // 単一プロセスでptyを直接管理しているので、同じ結果はより単純に
  // 得られる: PTYの現在のフォアグラウンドプロセスグループ
  // (tcgetpgrp())が、シェル自身のpid(setsid()で自分自身のセッション
  // /プロセスグループリーダーになっている、start()自身のコメント
  // 参照)と一致しなければ、シェルは何らかの子(cat等)をジョブ
  // コントロールでフォアグラウンドへ渡し、それが終わるのを待って
  // いる最中だと分かる――子プロセス側との一切の通信無しに判定できる。
  bool foregroundJobRunning() const;
  bool busy() const;

  // -- リサイズ --
  // 新しい端末サイズをpty(TIOCSWINSZ)と、所有するContentViewの
  // ビューポート/LineEditorのフィールド幅へ伝播する。
  // input_shell()のinitケースにあるvt_mode('W')の処理からの移植。
  void onResize(int rows, int columns, bool redraw = true);
  // 2026-10-20、直接のユーザー提案(最初の起動のbrew installの間、SHELLの上にHELPの表紙を残す、
  // Application::startShellBanner()): 上の`rows`行を空け、その下だけをSHELLの内容(scrollback_)にする。0で元に戻す。
  void setTopReserve(int rows);

  // K-27(^Oa): displayWidth()(入力エコー/プロンプト再描画の幅
  // 計算、CLAUDE.md内のadd_buff()自身の履歴参照)とcommandLine_
  // 自身のLineEditorエンコーディングが使うエンコーディングを更新
  // する。「古い」エンコーディングの下でptyから既に受け取った
  // scrollback()の行を遡って再デコードは「しない」――Shellは、
  // 既にデコード済みのContentView::Lineテキスト以外に、その
  // デコードを再実行するための生バイトの履歴を全く保持していない
  // (実際の、公開された制限: SHELLセッションの途中で文字コードを
  // 切り替えると、その時点以降の出力にのみ影響する。legacy自身の
  // vt_charset()トグルが影響する実用的な範囲にも合わせている)。
  void setEncoding(Charset::Encoding encoding);

  // handlePromptDetected()が新しいプロンプトを認識するたびに増加
  // する――start()の直後の最初のものを含む。(対話的にそれ自身で
  // 入力/待機することなく、例えばApplicationのCP/MV/RM経由の
  // SHELL引き渡しのように)commandLine()/submitLine()経由でちょうど
  // コマンドを注入した呼び出し元が、この値を事前にスナップショット
  // し、それが変わるのをポーリングすることで「シェルが次のコマンド
  // の準備ができている」ことを検出できるようにする。Shellは、その
  // 呼び出し元が何を待っているかについて何も知る必要が無い。
  int promptGeneration() const;

  // 2026-09-24追加、直接のユーザー報告("bash^M以後、プロンプトが
  // haa15030@mpi:~$になる。^Mを押さないとそれが出ない")+訂正報告
  // ("プロンプトは出るんだが同時に改行も起きるため「~$」の右隣への
  // 入力にならない"): 対話的な子シェル(例えば`bash`)を起動すると、
  // 多くの場合その子自身の~/.bashrc等がPS1をmarkerPrefix_を含まない
  // 値で上書きしてしまう――結果、processOutputBytes()のマーカー
  // 一致は二度と起きず、handlePromptDetected()も呼ばれない。新
  // プロンプトのバイト列は改行で終端されない(端末は入力を待つ間、
  // 行末に改行を送らない)ため、appendBodyByte()自身のpendingLine_に
  // 溜まったまま画面に一切出なかった(1回目の報告)。
  // 最初の修正はpendingLine_をpipeBuffer_へ強制feedしていたが、
  // pipeBuffer_/scrollback_は「閉じた行」のモデルであり、閉じた行の
  // 直後には常に新しい(空の)生きた入力行が続く――本物のプロンプト
  // (行末に改行が来ない、カーソルはまさにその文字列の右隣に留まる)
  // を、閉じた行として描くと、必然的にその下へ改行してしまう(2回目の
  // 報告)。正しい経路は、handlePromptDetected()が使うのと同じ
  // 「生きたプロンプト行」(promptPlain_/promptDecorated_+
  // redrawPromptRow())で、まさにこのpendingLine_の「今の」内容を仮に
  // 表示することであり、pipeBuffer_には一切触れない(pendingLine_は
  // クリアしない――appendBodyByte()側のtentativeLivePrompt_自身の
  // コメント参照)。
  // onPtyReadable()を呼ぶapp/main.cpp自身のpollループが、この
  // タイムアウト(ready<=0、真にPTYが静かになった瞬間)を既に検出
  // できるので、そこから呼ばれる想定。
  //
  // ちなみに、PTY自体から「子が今まさに入力待ちで止まっている」ことを
  // 直接・確実に知る移植可能な手段は無い(だからこそこのプロジェクトは
  // そもそもPS1へマーカーを埋め込むという手法を取っている――
  // expect/pexpect等の端末自動化ツールが軒並み出力のパターンマッチに
  // 頼るのと同じ理由)。ここでの「idle」はあくまで「一定時間(100ms)
  // 新しいバイトが来ない」という間接的な推測に過ぎない。
  void flushIdleOutput();

 private:
  void handlePromptDetected(const std::string &tailBeforeMark);
  void appendBodyByte(unsigned char c);
  void processOutputByte(unsigned char c);  // processOutputBytes()の1バイト分(flushNewScrollbackLines()は呼ばない)
  void flushPendingLine();  // pendingLine_に関するappendBodyByte()自身のコメント参照
  // 子自身の出力からの、本物の端末のANSI/VT100エスケープシーケンス
  // (SGRカラーコード、bracketed paste modeの有効化/無効化など――
  // 生のESCバイトで始まる何でも)を、それらがappendBodyByte()/
  // PipeBufferに到達する「前に」ここで消費し捨てる。実際の対話的な
  // シェルでの実地の使用を通じて必要だと判明した(2026-08-01):
  // PipeBufferの仕事は「プレーンなファイル」の内容に対する文字コード
  // /タブ/オーバーストライク太字の処理であり、「このバイト列は
  // テキストではなく不可視の端末制御データである」という概念は
  // 全く無い――本物のシェルは全て、プレーンな、色の付いていない
  // 合成プロンプトであっても少なくとも「いくつかは」これらを発する
  // (bracketed paste modeだけでもzsh 5.x+のデフォルト)。それらを
  // 解釈せずに通すと、可視のテキスト(生のエスケープバイトが文字通り
  // の文字として表示される)とPipeBuffer自身の幅折り返し計算(全ての
  // バイトを可視の列として数える)の両方を壊してしまう。`c`が
  // エスケープシーケンスの(開始、継続、または完了)の一部として
  // 消費された場合はtrueを返す――その場合、呼び出し元はそれを
  // appendBodyByte()へも渡す「べきではない」。これは取り除きであり
  // 描画ではない: 色/属性情報自体は再現されず、捨てられる――意図的
  // な、公開された範囲の選択(本物のANSIからDisplay::Attributeへの
  // 変換器を構築するのは将来の課題であり、破損を止めるためだけには
  // 必要ない)。
  bool consumeEscapeByte(unsigned char c);
  void flushNewScrollbackLines();
  // includePrompt=false(2026-09-06追加、直接のユーザー報告参照:
  // `cat > /tmp/test.txt`の後に`abc`と打って送信すると、`abc`が
  // 新しいトップレベルのコマンドであるかのように、その左側に
  // "usilic-1.5b:..."プロンプトが挿入されてしまっていた)――
  // submitLine()呼び出し時点で既にbusy()だった場合、今回のEnterは
  // 新しいコマンドではなく、既にフォアグラウンドで動いている`cat`
  // 等への生の追加入力なので、プロンプト接頭辞を一切付けない。
  void freezePromptLine(const std::string &command, bool includePrompt = true);
  void resetSessionState();
  void writeToPty(const std::string &bytes);
  void updateCommandLineField();
  // 2026-09-06追加、直接のユーザー報告("`cat > /tmp/test.txt`の後の
  // `abc`が、cat直下の位置(見えないプロンプトの幅ぶん右)から入力
  // されてしまう"、submitLine()自身のこの症状についてのコメント参照)
  // ――awaitingRealPrompt_の間はcommandLine_のフィールドを列0・全幅
  // へ広げ、そうでなければ本物のプロンプト幅(updateCommandLineField()
  // 自身のpromptCol)へ戻す、その一元化された判定。updateCommandLine
  // Field()を無条件に呼んでいた既存の呼び出し元(flushNewScroll
  // backLines()、onResize())は、cat自身の出力の有無に関わらず
  // (`cat > file`のようにリダイレクトされていても、書き込んだコマンド
  // 行自体のシェル側エコーがpipeBuffer_/flushNewScrollbackLines()を
  // 通るため)、意図せずこの列0への拡張を踏みつぶし、stale状態の
  // promptColへ戻してしまっていた――代わりにこちらを呼ぶことで、
  // redrawPromptRow()自身のawaitingRealPrompt_ガードと矛盾しなくなる。
  void refreshCommandLineField();
  // commandLine_自身の生きたプロンプト/入力が占める行: 今回もまた
  // 固定定数、viewportRows_-2(2026-08-04の改訂――完全な経緯、これが
  // 置き換える以前の2回の反復とそれぞれがなぜまだ正しくなかった
  // かについては、コンストラクタ自身のコメント参照)。scrollback_
  // 自身の内容領域は、恒久的にビューポートより「1行」短い
  // (viewportRows_-1ではなくviewportRows_-2)。これにより、
  // (scrollback_自身のsetBottomAnchored(true)が、内容がどれほど
  // 少なくても最後の内容行が常にこの行のすぐ上にあることを保証する)
  // 本物のスクロールバックの内容とも、(setModeLineRow()経由で本当の
  // 最終行、viewportRows_-1に固定されている)scrollback_自身の
  // モードラインとも決して衝突しない、本当に専用の行がプロンプトの
  // ために解放される。この式がちょうど1箇所にだけ存在するように
  // 共有されている。
  int commandLineRow() const;
  int displayWidth(const std::string &text) const;
  std::string buildSetupCommand(const std::string &initialDirectory) const;
  bool waitForHandshake();

  Display &display_;
  ShellOptions options_;

  int masterFd_ = -1;
  bool handshakeDone_ = false;  // start()のハンドシェイク完了後true(writeToPty()の2026-09-27のコメント参照)
  int childPid_ = -1;
  bool isBash_ = true;
#ifdef _WIN32
  struct PseudoConsole *console_ = nullptr;  // pseudo_console_win.hpp(ConPTY)
  int consoleRows_ = 24;  // ConPTYの行数(vtRow_の上限)
  // ConPTYの出力の今の行(1から)。ConPTYは改行の代わりにカーソルの移動(CSI H等)で下の行へ進むことがあるので、
  // その分の改行を本文へ足すために数える(applyConsoleCursorSequence())。
  int vtRow_ = 1;
  void applyConsoleCursorSequence(const std::string &sequence);
  void advanceConsoleRows(const std::string &bytes);  // 本文へは渡さず、行だけを数える(ハンドシェイクの前の分)
#endif
  bool isCmd_ = false;
  bool isZsh_ = false;  // これがなぜ単に「bashではない」ではなく自身の分岐を必要とするかについてはisZshProgram()自身のコメント参照

  PipeBuffer pipeBuffer_;
  std::string pendingLine_;  // appendBodyByte()自身の行組み立てバッファ、自身のコメント参照
  bool sawBareCr_ = false;   // appendBodyByte()自身のCR対CRLFの先読みフラグ
  // flushIdleOutput()自身の宣言コメント参照: pendingLine_を(クリア
  // せずに)promptPlain_/promptDecorated_へ「仮に」ミラーして生きた
  // プロンプト行として描いた状態かどうか。trueの間、appendBodyByte()
  // 側でpendingLine_が本物の改行で閉じられたら(=仮定が誤りで、実は
  // まだ続く通常の出力行だったと判明)、retireTentativeLivePrompt()で
  // promptPlain_/promptDecorated_を空へ戻す――そうしないと、今や
  // 1つ上の(閉じた)scrollback行として確定した内容が、その下の生きた
  // プロンプト行にも重複して居座ってしまう。
  bool tentativeLivePrompt_ = false;
  void retireTentativeLivePrompt();  // 自身の宣言コメント(tentativeLivePrompt_)参照
  ContentView scrollback_;
  bool historyBrowsing_ = false;  // historyBrowsing()自身の宣言コメント参照
  // submitLine()自身の2026-09-06追加分のコメント参照――平文の
  // シェルコマンド(またはEnterだけの空行)を書き込んだ直後、本物の
  // プロンプトがまだ戻ってきたと確認できていない間はtrue。
  // handlePromptDetected()がマーカーを実際に検出した時にだけfalseへ
  // 戻る。
  bool awaitingRealPrompt_ = false;
  LineEditor commandLine_;
  std::size_t linesConsumed_ = 0;  // pipeBuffer_.lines()のうち既にscrollback_にある数
  // submitToPty()自身のsubmitLine()がちょうど書き込んだコマンド
  // テキストで、flushNewScrollbackLines()の、残されたシェルの行
  // エディタのエコーの痕跡に対する「一度きりの」チェックをまだ
  // 待っている――そのチェックが実行された後は(実際に何かを見つけて
  // 落としたかどうかに関わらず)nullopt。このptyではECHOが無効化
  // されている(start()自身のコメント参照)。それは特に、
  // commandLine_自身のローカルエコーが、入力されたものの唯一の
  // 描画であるようにするためだが、bashのreadlineとzshのzleは、
  // 両方とも、行全体を一度に与えられた時、自分自身の「何か」を
  // それでもエコーする(実際の並べたテストで発見、2026-08-03:
  // bashは送信したテキストそのものを自身の余分な行として
  // エコーバックし、zshは代わりに単一の裸の空行を発する)――生の
  // ttyのECHOフラグとは独立している。どちらの行エディタも、tty
  // ドライバのエコーに頼るのではなく、対話的な編集のために自分自身
  // の画面上の描画を管理しているため。flushNewScrollbackLines()は
  // これを使って、その残された1行をちょうど認識して落とす。legacy
  // 自身のクリーンな出力(そのスクリーンショットは、コマンド送信後、
  // 空行も重複したコマンドテキストも示していない)に合わせている。
  std::optional<std::string> pendingEchoFilter_;

  std::string markerPrefix_;  // promptMark + programName + ":" -- buildSetupCommand()参照
  std::size_t markerMatchPos_ = 0;
  std::string markerMatchBuffer_;  // これまでのところmarkerPrefix_に対して仮に一致しているバイト
  bool inPromptTail_ = false;
  std::string promptTailBuffer_;

  // consumeEscapeByte()自身の状態――現在どの「種類」のエスケープ
  // シーケンス(もしあれば)を通り越して走査中か。
  enum class EscapeKind { None, JustEsc, Csi, Osc, OscEscPending };
  EscapeKind escapeState_ = EscapeKind::None;
  // 2026-09-26追加、直接のユーザー依頼("ページャーとSHELL両方で色を
  // 解釈するよう実装して"): 走査中のCSIシーケンスそのもの。完結した
  // 時点でSGR(`ESC[...m`、色/太字等)だったら、捨てずに本文へ渡す
  // (appendEscapeSequence()、PipeBuffer::applyEscape()が解釈する)。
  std::string escapeSequence_;
  void appendEscapeSequence(const std::string &sequence);

  int viewportRows_ = 0, columns_ = 0;
  int topReserve_ = 0;  // setTopReserve()
  std::string promptPlain_;  // 現在のプロンプトテキスト、埋め込みエスケープコード無し(幅計算用)
  std::string promptName_ = "usilic-1.5b";  // setPromptName()参照
  std::string promptDecorated_;  // 同じテキストに太字のエスケープコードを付けたもの――redrawPromptRow()参照

  std::string currentDirectory_;
  bool busy_ = false;

  int promptGeneration_ = 0;

  std::function<void(const std::string &)> directoryChangedCallback_;
  std::function<bool(const std::string &, const std::string &)> programLauncher_;
  std::function<void(const std::string &)> historyRecorder_;
  std::function<void()> fullScreenCallback_;
  bool alternateScreen_ = false;  // 子が全画面(ESC[?1049h等)の間。その出力は取り込まない
  std::function<std::vector<std::string>(const std::string &)> completionProvider_;
  // 2026-09-18追加、TabOutcome自身の宣言コメント参照。
  std::function<std::vector<std::string>(const std::string &)> programCompletionProvider_;
  // 2026-09-18追加、legacyの`f_list`フラグ(old/src/key.c)と同じ役割:
  // 直前のTABが「まさに今と同じ曖昧な状態」だった場合だけtrue――
  // 次のTABで一覧表示を要求する合図になる。TAB以外の任意のバイトで
  // resetTabArm()経由でfalseへ戻る(Application::feedShellByte()自身の
  // 冒頭)。
  bool tabArmed_ = false;
};

}  // namespace fm
