// glibcは、posix_openpt/grantpt/unlockpt/ptsnameのために
// _XOPEN_SOURCEを必要とする。_DEFAULT_SOURCEは、それと一緒に他の
// glibc拡張(例えばNSIG)を見えるままにする。Linux専用にゲートされ
// ている――macOS/BSDのlibcはこれ無しでも両方を公開しており、そこで
// _XOPEN_SOURCEを定義すると、代わりにそれら自身のBSD拡張を隠して
// しまうことがある。このガードのusilic.h自身のバージョンと同じ理由
// (old/src/usilic.h参照)。
#ifdef __linux__
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif
#endif

#include "shell.hpp"

#include <chrono>

#include <algorithm>
#include <csignal>
#include <cstdlib>

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

namespace fm {

namespace {

std::vector<char *> toArgv(const std::vector<std::string> &args) {
  std::vector<char *> argv;
  argv.reserve(args.size() + 1);
  for (const auto &a : args) argv.push_back(const_cast<char *>(a.c_str()));
  argv.push_back(nullptr);
  return argv;
}

void resetSignalsToDefault() {
  for (int i = 1; i < NSIG; i++) std::signal(i, SIG_DFL);
}

// application.cpp自身の同名ヘルパー(anonymous namespace)と全く同じ
// 内容の重複定義――理由もそちらと同じ(2つの別個のLineEditorインス
// タンスをそれぞれ別のTUから扱うため、共有ヘルパーを吊るす共通の
// 所有者が無い)。HELP.txt 2.13章「LIST」の実装、old/src/key.c::
// make_list()の`nc[] = "/<>()|; "`+先頭'.'の除外を参照。
bool shouldSearchPath(const std::string &text) {
  // application.cpp自身の同名ヘルパーの2026-09-18訂正コメント参照
  // (実機報告=gu: 空のバッファでの^I^Iがカレントディレクトリ一覧に
  // 誤って落ちていた――legacyは空文字列も$PATH検索対象に含める)。
  if (!text.empty() && text.front() == '.') return false;
  return text.find_first_of("/<>()|; ") == std::string::npos;
}

std::string baseName(const std::string &argv0) {
  auto pos = argv0.find_last_of('/');
  return (pos == std::string::npos) ? argv0 : argv0.substr(pos + 1);
}

bool isBashProgram(const std::string &argv0) { return baseName(argv0) == "bash"; }

// zshは、Catalina(2019)以来macOS自身のデフォルトのログインシェル
// である――実地での実際の使用を通じて発見された実際のギャップ
// (2026-08-01): 下の設定コマンドは以前、「bashではない」を1つの
// tcsh/csh構文の分岐(`set prompt = "..."`)へまとめていたが、zshは
// それを理解「しない」(その`set VAR = value`という形はtcsh/csh
// 専用。zsh自身の`set`組み込みコマンドはシェル「オプション」用で
// あり、変数代入用ではない)。本物のzshに送られると、その行は
// PROMPTの設定に全く黙って失敗し、ユーザー「自身の」本物の、完全
// にテーマ化されたインタラクティブなプロンプト(oh-my-zsh/
// powerlevel10k/starship等、それが使うどんなANSIカラーコードや
// Unicodeグリフでも)が実際に表示されるものとして残ってしまう
// ――これは、この移植のShell(プレーンな、合成されたプロンプト
// テキストを想定して構築されている)がパースしたり安全に描画したり
// する方法を持たず、まさに報告された文字化けしたスクロールバック
// と「controlCodeがハイライトされた箱として」という症状を生み
// 出す。
bool isZshProgram(const std::string &argv0) { return baseName(argv0) == "zsh"; }

// 移植可能なpty取得(POSIX/Unix98のAPI)、sub.cのopen_pty()からほぼ
// 逐語のまま移植――これがなぜ元々のBSDの/dev/ptyp0..
// "/dev/ptysf"というデバイスノードの走査アプローチ(現代のLinuxや
// macOSには全く存在しなかった)を置き換えたのかについてはCLAUDE.md
// の履歴参照。open_pty()とは異なり、これは常にマスターfdを開いた
// ままにする(legacyのf_open==0という「単に予約するだけ」モードは、
// Shellが書かれた時点でそれを使う呼び出し元がもう残っていなかった)。
int openPty(std::string &slavePathOut) {
  int fd = posix_openpt(O_RDWR | O_NOCTTY);
  if (fd < 0) return -1;
  if (grantpt(fd) < 0 || unlockpt(fd) < 0) {
    close(fd);
    return -1;
  }
  char *slave = ptsname(fd);
  if (!slave) {
    close(fd);
    return -1;
  }
  slavePathOut = slave;
  return fd;
}

}  // namespace

Shell::Shell(Display &display, const ShellOptions &options)
    : display_(display),
      options_(options),
      pipeBuffer_(options.encoding, options.columns, options.scrollbackBudget),
      scrollback_(display),
      commandLine_(display, options.encoding) {
  markerPrefix_ = options_.promptMark + options_.programName + ":";
  viewportRows_ = options_.rows;
  columns_ = options_.columns;
  // key.cのstand_str()からの移植: それ自身の早期return
  // (`f_M == M_shell`)は、legacyが通常の一覧に対して行うのと同じ
  // 方法で、SHELL自身のスクロールバック閲覧(^P/^Nで過去のコマンド
  // を巡る、M_kup/M_kdn)に対して行全体の反転表示カーソルハイライト
  // を決して描画しないことを意味する――legacyに対する実際の並べた
  // スクリーンショット比較(2026-08-04)によって、そこで(ハイライト
  // されたバーではなく)プレーンな端末カーソルが表示されることが
  // 確認された。FileModeが同じ理由で既にこれを無効にしているのに
  // 合わせている(自身のコメント参照)。Application自身の^P/^N
  // ハンドラは、代わりにディスプレイの実際のカーソルをContentView::
  // cursorDisplayRow()へ動かすので、閲覧中も「現在の行」は依然と
  // して可視のままである――色付きのハイライトではなく、単に通常の
  // テキストカーソルとして。
  scrollback_.setCursorHighlightEnabled(false);
  if (!options_.argv.empty()) {
    isBash_ = isBashProgram(options_.argv[0]);
    isZsh_ = isZshProgram(options_.argv[0]);
  }
  // 3つの明確な、永続的に「固定された」行(2026-08-04の改訂):
  // scrollback_自身の「内容」領域はheight_ = viewportRows_-2行の
  // 高さ(0..viewportRows_-3)、生きたプロンプト
  // (commandLineRow()、自身のコメント参照)はそのすぐ後の固定行
  // viewportRows_-2、そしてモードラインはsetModeLineRow()経由で
  // 「本当の」最終行viewportRows_-1に固定される。
  // scrollback_.setBottomAnchored(true)が、これを本当に「固定
  // された」プロンプト行と一緒に機能させるものである: これが
  // あれば、height_未満の行は、ContentViewのデフォルトの上端に
  // 密着する代わりに、内容領域の「下端」に密着して描画される
  // (その上に空白のパディング行)――そのため最後の本物の内容行は、
  // scrollback_が1行だろうと100行だろうと、常にviewportRows_-2の
  // すぐ上にあり、プロンプトは全く動く必要が無い。
  //
  // これは2つの以前の反復を置き換える。それぞれが、以前のものが
  // 完全には解決しなかった実際のバグを修正している:
  //   1. (2026-08-02) commandLine_は、scrollback_自身の最後の
  //      「内容」行を直接オーバーレイしていた(height_が
  //      viewportRows_-1で、予約された行が全く無かった当時)
  //      ――当時は「legacyに合わせている、そこでは生きたプロンプト
  //      は単にスクロールバックの最後の行「である」」と理由付け
  //      られていた。コマンドが送信された後に届く実際の出力も
  //      正当にその同じ行を欲しがり、2つのうちどちらが最後に再描画
  //      したかによって、黙ってもう一方を壊していた(実際のエス
  //      ケープシーケンスの追跡で確認済み、2026-08-03: 全てのコマン
  //      ドの最後の出力行が、-cでは再度現れるプロンプトによって、
  //      -gではその逆で、上書きされていた)。
  //   2. (2026-08-03) プロンプトのために確実に別の行を予約したが、
  //      それを「内容相対」にした(scrollback_が成長するにつれて
  //      大きくなり、内容領域を内容が満たした時点でだけ
  //      viewportRows_-2に落ち着く)。これは、legacy自身の可視の
  //      挙動(プロンプトはその前に来たものの直後、scrollback_が
  //      まだ短い間の不自然な隙間のある固定行ではない)に合わせる
  //      ためだった。これはそれ自身の種類のバグを引き起こした:
  //      次の本物のプロンプトが検出される前に、scrollback_が複数
  //      の別々のpty読み込みにわたって成長し続けている間、
  //      commandLine_のキャッシュされた行やプロンプト自身の画面上
  //      のテキストを同期させ続けるものが何も無かった――実際の
  //      ユーザー報告(2026-08-03/04): "ls[Enter]→一瞬プロンプト
  //      表示→ls結果表示→一瞬プロンプト表示後消失"。2回の追加修正
  //      (全てのフラッシュでcommandLine_のフィールドを再同期し、
  //      その後全てのフラッシュでredrawPromptRow()も再度主張する)
  //      は、それぞれギャップの一部を閉じたが、ユーザー自身の次の
  //      報告("降下中がNG。着地後はOK")が、根底にある churn――
  //      プロンプトの行が実際に、繰り返し、コマンドの途中で変わる
  //      こと――が依然としてそこにあり、単に見えにくくなっただけ
  //      であることを確認した。
  // 代わりに内容を下端固定にすることは、動的な位置決めを完全に
  // 取り除く: プロンプトの行は再び(反復1のように)コンパイル時に
  // 明白な定数だが、scrollback_自体が――Shellではなく――今や、
  // (反復1とは異なり)内容とプロンプトが決して衝突しないことを保証
  // し、(反復2とは異なり)内容が成長するにつれて何かを再同期する
  // 必要が全く無い。再同期「すべき」ものがもう何も残っていない
  // ため。
  scrollback_.setViewport(0, std::max(1, viewportRows_ - 2));
  scrollback_.setModeLineRow(viewportRows_ - 1);
  scrollback_.setBottomAnchored(true);
  updateCommandLineField();
}

Shell::~Shell() { terminate(); }

bool Shell::start(const std::string &initialDirectory) {
  if (masterFd_ >= 0) return true;
  if (options_.argv.empty()) return false;
  resetSessionState();

  std::string slavePath;
  int master = openPty(slavePath);
  if (master < 0) return false;

  // このptyの本物のウィンドウサイズ(bash/`man`がioctl(TIOCGWINSZ)で
  // 読み取るもの)――fork()の「前」に読んでおく: display_自身のrows()/
  // columns()はApplicationの生きた現在値を常に指す参照だが、fork()の
  // 「後」の子プロセス側で読むと(親と子は別アドレス空間の別プロセス
  // なので)意味を持たない。実際に適用される場所については、すぐ下の
  // fork()分岐のTIOCSCTTY直後のコメント、およびwaitForHandshake()
  // 呼び出し直後のコメント参照。
  int realRows = display_.rows();
  int realColumns = display_.columns();

  pid_t pid = fork();
  if (pid < 0) {
    close(master);
    return false;
  }

  if (pid == 0) {
    // 子: シェルになる。start_shell()のfork分岐からの移植。移植
    // されていない: 元々のランデブーファイルによるpidの引き渡し
    // (親がポーリングして読み戻せるよう、自身のgetpid()を一時
    // ファイルへ書き込む)――fork()の戻り値が既に、親へ子のpidを
    // 直接、同期的に与えるので、その一連の動作は、この設計が依然
    // として必要とするものではなく、純粋なlegacyの残骸である
    // (この親自体が二重forkの結果ではないので、回避すべき「本物
    // のpidを失った」という問題が無い)。
    close(master);
    setsid();
    int slaveFd = open(slavePath.c_str(), O_RDWR);
    if (slaveFd < 0) _exit(127);
#ifdef TIOCSCTTY
    ioctl(slaveFd, TIOCSCTTY, 0);
#endif
    dup2(slaveFd, 0);
    dup2(slaveFd, 1);
    dup2(slaveFd, 2);
    close(slaveFd);
    resetSignalsToDefault();
    // 2026-09-06追加、直接のユーザー報告("SHELLプロンプトで`man ls^M`
    // が80桁固定で整形され、64桁の実端末で二重に折り返されて総行数が
    // 異常に膨らむ"、`stty size`で直接確認: シェルのpty自体が「0 0」を
    // 報告していた)――このptyの、カーネルが管理する実際のウィンドウ
    // サイズは、生成された時点では一切設定されておらず、onResize()
    // (Application自身の本物のリサイズ時にだけ呼ばれる)が「後で」
    // 呼ばれるまで未設定(0行0列)のままだった。`man`はこれを検出すると
    // 独自の80桁固定フォールバックへ倒れ、実端末が64桁でもこの移植
    // 自身のPipeBuffer(正しく実際の64桁で折り返す)がそれをさらに二重に
    // 折り返すことで、途中の"["等を巻き込んだ不格好な改行と総行数の
    // 異常な膨張の両方が同時に説明できる。
    // TIOCSWINSZは(実機で直接確認済み)マスター側のfdでは効かない
    // (ENOTTY、スレーブがまだ一度も開かれていないため)――スレーブ側の
    // fd(dup2後はfd 0で代用できる)でのみ有効。
    // 2026-09-06さらに訂正(2回目、実際のdoctest回帰で発覚、bash限定):
    // isBash_の間は、本物の実際のサイズ(realRows/realColumns)を渡さず
    // すぐ下の$COLUMNS=1024と全く同じ理屈の広い仮の値のままにする
    // ――bash自身のreadlineは、カーネルの本物のウィンドウ幅を知って
    // いると、これから送信するセットアップコマンド自身の入力エコー
    // (buildSetupCommand()自身の長い1行、下のwaitForHandshake()呼び
    // 出しのコメント参照: ECHOを切ってもreadline自身の対話的な再描画
    // エコーまでは止められない)をその実際の幅で折り返してしまい、
    // 折り返しの位置がたまたまプロンプトマーク文字列の途中に落ちると、
    // 番兵/マーク検出そのものが文字化けして永久にタイムアウトする
    // (実際にdoctestで再現・確認済み: `<mark>`が`<mar k>`のように
    // 途中に空白が挟まって壊れていた)。本物のサイズをwaitForHandshake()
    // 成功「直後」に送る対処も試したが、既に対話状態にあるbash
    // セッションへ生きたTIOCSWINSZ(SIGWINCH)を送ると、実機のdoctest
    // 回帰で(理由は特定できていないが再現性高く)Shell::terminate()
    // 自身のSIGHUPへの応答が遅くなる副作用が観測されたため、その経路は
    // 採用しなかった――isBash_の間は、本物のリサイズ(Application自身の
    // onResize())が実際に起きるまで、既存通りこの仮の広い値のまま
    // (悪化はしない、この修正が入る前と同じ状態)にとどめる。zsh(この
    // プロジェクトの実運用でのデフォルトシェル、実機PTYで検証した
    // 限りこの折り返し文字化けは再現しなかった)を含むisBash_でない
    // シェルについてだけ、最初から本物のサイズをそのまま使う。
    {
      struct winsize ws {};
      if (isBash_) {
        ws.ws_row = 1024;
        ws.ws_col = 1024;
      } else {
        ws.ws_row = static_cast<unsigned short>(realRows);
        ws.ws_col = static_cast<unsigned short>(realColumns);
      }
      if (ws.ws_row > 0 && ws.ws_col > 0) ioctl(0, TIOCSWINSZ, &ws);
    }
    if (isBash_) {
      // Shellが実際の端末幅を報告する機会を持つ前に、bash自身の
      // 「COLUMNSバイト分のパディングで消去して再描画する」という
      // プロンプト再表示ロジックを抑制する――start_shell()からの
      // 移植。onResize()/buildSetupCommand()は、シェルが実際に起動
      // した時点で実際の値を設定する。
      setenv("COLUMNS", "1024", 1);
    }
    // 2026-08-25(直接のユーザー依頼): usilic.c自身のENV_PROG慣習
    // (progname.hのPROGNAME_UPPER、このビルドでは「USILIC」、
    // legacyのものはそれ自身の2026-08-25の改名の後「U」)からの
    // 移植――このshellの「内側から」呼び出された本物のusilic
    // (例えばプロンプトに入力された「usilic .」、またはそれへの
    // パイプ)は、それ自身の起動時にこれをチェックして「自分は
    // 既に生きているusilicのSHELLモードの内側で動いている」ことを
    // 検出し、もしそうなら、独立した2つ目のセッションを開く代わり
    // に、この引数を「この」プロセスへ引き渡す(受け取る側について
    // はApplication::pollShellHandoff()自身のコメント参照)。単なる
    // 真偽値のマーカー(legacy自身のここでの値はバージョン文字列
    // で、同様に内容としては決してパースされない)――存在だけが
    // チェックされる。
    setenv("USILIC", "1", 1);
    // 2026-08-25(直接のユーザーによる追加依頼、この「両方の」
    // バイナリとlegacy自身のold/src/shell.cのstart_shell()の両方へ
    // 移植された――受け取る側についてはmain.cpp自身の
    // checkInvalidTerminalMarker()参照): 上のUSILICは、「自分は
    // 特に生きているusilicのSHELLの内側にいる」ことだけを証明する
    // ――「この」shellのプロンプトで「別の」バイナリの名前
    // (legacyの「U」)を入力すると、そのような環境変数は全く見えず、
    // 代わりに本当に独立した完全なセッションを起動することへ落ちて
    // しまう(実際の報告: "うっかり'usilic .'とやってしまうと例の
    // 縦長usilicが出てしまう"――そしてその逆、生きているusilicの
    // SHELLの内側でUを入力する)。この、バイナリをまたぐケースを
    // 珍しいptyのボーレート(本物のtermiosフィールドで、それ以外
    // では「疑似」端末に対して完全に無効)経由で解決しようとした
    // 最初の試みは、実際には生き残らないことが判明した――実際の
    // PTYキャプチャが、zsh自身の対話的な起動が、最初のプロンプトが
    // 現れる前に、黙ってそれを通常の速度に戻してしまうことを発見
    // し、シェルの行エディタがそのフィールドに決して触れないという
    // 前提を反証した。2つ目の、共有される、バイナリに依存しない
    // 環境変数ははるかに単純で、termiosの状態とは異なり、シェル
    // 自体によって全く触れられることが決して無い: 両方のバイナリ
    // 自身のShell起動(この呼び出しと、start_shell()の同一のもの)
    // によって「設定され」、両方のバイナリ自身のmain()によって、
    // それ自身のUSILIC/Uと並んでチェックされる――存在するが、
    // バイナリ「自身の」変数が無いことは、「自分は誰か「他の」人の
    // 生きたSHELLの中にいる」ことを意味し、代わりに紛らわしい独立
    // したセッションの起動を拒否する("Invalid Terminal")。
    setenv("USILIC_LIVE_SHELL", "1", 1);
    auto cargv = toArgv(options_.argv);
    execvp(cargv[0], cargv.data());
    _exit(127);  // execvp()が失敗した――決してそのまま通り抜けてusilicとして動作し続けない
  }

  childPid_ = pid;
  masterFd_ = master;
  fcntl(masterFd_, F_SETFL, fcntl(masterFd_, F_GETFL) | O_NONBLOCK);

  // 下の設定コマンドが送信される前にECHO(とECHONL)を無効にする
  // ――そうしないと、その複数行のまとまった送信に対するカーネル
  // 自身の行規律のエコーが、子シェルが実際にそのどれかを読んで
  // 「実行する」よりもずっと前に、ほぼ即座にwaitForHandshake()の
  // 走査バッファへ到達してしまう(ptyへの書き込みと子がそこから
  // 読むことは2つの独立した、競合する操作である。カーネルのエコー
  // だけが書き込み自体と同期している)。古い番兵コマンド(無音の
  // `:`という何もしないコマンド)はそれ自身の出力を全く生み出さな
  // かったので、waitForHandshake()には「番兵『テキスト』が見えて
  // いる」ことと「シェルが実際に番兵行に『到達し』実行した」こと
  // を区別する方法が無かった――それは、本物のシェルがまだそのセッ
  // トアップを開始するまで何秒もある時点で、エコーだけから成功を
  // 報告することができ、実際にそうしていた。そして、その途中の
  // 各セットアップ行に対してそれが描き続けたプロンプト(PROMPT/PS1
  // がまだ効いていないので、シェル自身の「素の」フォーマットで)は
  // その後、この関数自身の下の「番兵までの全てを捨てる」ロジックに
  // 拾われる代わりに、通常のハンドシェイク後の内容としてscrollback_
  // に漏れ出していた。実際のユーザー報告(2026-08-02、スクリーン
  // ショット): SHELLが起動した直後、実際のコマンドが1つも実行され
  // ないうちに、複数の空行と、はぐれた「user@host dir %」スタイル
  // のプロンプトの断片が、本物の最初のプロンプトがあるべき場所より
  // 上に、既にスクロールバックに座っていた――生のバイトストリーム
  // をダンプし、セットアップが本当に終わるずっと前にシェル自身の
  // 「素の」フォーマットのプロンプトテキストが存在することを発見
  // した、PTYテストで再現・確認済み。buildSetupCommand()の番兵は
  // 今や、シェル自身が実際に到達し実行しなければそれを生み出せない
  // 本物の`echo`であり、ここでのECHOがオフであることと組み合わさる
  // ことで、その番兵テキストがこの読み取りループへ到達できる唯一の
  // 方法が、本物の、逐次的なシェルの実行であることを意味する
  // ――まさに、この関数の既存の「番兵まで捨てる」ロジックが常に
  // それに頼るよう設計されていたものである。シェル自身の行エディタ
  // (bashのreadline/zshのZLE)は、対話的な入力/再描画を制御し始める
  // と、ptyのエコー/canonicalの設定を自身のために再構成するので、
  // この早期の上書きは通常のセットアップ後の対話的な使用には
  // 残らない。
  struct termios setupTio;
  if (tcgetattr(masterFd_, &setupTio) == 0) {
    setupTio.c_lflag &= ~(ECHO | ECHONL);
    tcsetattr(masterFd_, TCSANOW, &setupTio);
  }

  writeToPty(buildSetupCommand(initialDirectory));

  if (!waitForHandshake()) {
    terminate();
    return false;
  }
  handshakeDone_ = true;
  return true;
}

bool Shell::alive() const {
  if (childPid_ <= 0) return false;
  return kill(childPid_, 0) == 0;
}

int Shell::childPid() const { return childPid_; }

void Shell::terminate() {
  if (childPid_ > 0) {
    kill(childPid_, SIGHUP);
    int status = 0;
    bool reaped = false;
    for (int i = 0; i < 100 && !reaped; i++) {
      if (waitpid(childPid_, &status, WNOHANG) > 0) reaped = true;
      else usleep(10000);
    }
    if (!reaped && kill(childPid_, 0) == 0) {
      kill(childPid_, SIGKILL);
      // ここでも、ブロッキングなwaitpidではなく、制限されたWNOHANG
      // ポーリング――macOSで実際のbashテストが無期限にハングする
      // ことを通じて発見された: シェルの子(ptyのセッションリーダー)
      // は、SIGKILLのずっと後になっても、時々依然として終了途中
      // (psのSTAT「E」、「終了しようとしている」)として観測された。
      // 理由は、単独のfork/signal/waitシーケンスだけの孤立した再現
      // では再現しなかった――おそらく、より大きなテストバイナリの
      // 内側で実行することに特有の、ptyを制御している端末の後始末
      // の何らかの端のケース。正確な原因が何であれ、呼び出し元を
      // 永遠にブロックし得る後始末経路は、カーネルがSIGKILLの後で
      // すら異常に回収を終えるのが遅い場合にこれが残す稀な放置
      // ゾンビ(このプロセス自体が終了した時点でinitに回収される)
      // よりも悪い。
      for (int i = 0; i < 100 && !reaped; i++) {
        if (waitpid(childPid_, &status, WNOHANG) > 0) reaped = true;
        else usleep(10000);
      }
    }
    childPid_ = -1;
  }
  if (masterFd_ >= 0) {
    close(masterFd_);
    masterFd_ = -1;
  }
  handshakeDone_ = false;
}

int Shell::ptyFd() const { return masterFd_; }

bool Shell::onPtyReadable() {
  if (masterFd_ < 0) return false;
  char buf[4096];
  ssize_t n = read(masterFd_, buf, sizeof(buf));
  if (n > 0) {
    processOutputBytes(std::string(buf, static_cast<std::size_t>(n)));
    return true;
  }
  if (n == 0) return false;  // EOF: シェルの子が終了した
  if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return true;  // 今のところデータが無い
  return false;
}

// output_shell()のメインの読み取りループ(errp[]/f.i_err/
// f.i_errp/f.n_errのバイトごとの状態機械)からの移植で、代わりに
// バッファされた部分文字列のマッチとして作り直されている:
// markerPrefix_(promptMark + programName + ":"、つまりPS1のリテラ
// ルな固定接頭辞)は、markerMatchPos_に対してバイトごとにマッチ
// される。完全にマッチした時点で、バイトはpromptMarkが再び現れる
// (PS1 = mark+program+":"+"\w\$ "+markという周期を閉じる)まで
// promptTailBuffer_にバッファされ、その時点でhandlePromptDetected()
// が、間にあった全て(シェル自身の\wで展開されたcwdにその末尾の
// "$ "/"# "を加えたもの)と共に発火する。誤報であることが判明した
// 部分マッチは、失われるのではなくバイトごとにそのまま普通の内容
// として再生される。同じケースに対するlegacy自身の巻き戻し挙動に
// 合わせている。
void Shell::processOutputBytes(const std::string &bytes) {
  const std::string &mark = options_.promptMark;
  for (unsigned char c : bytes) {
    if (consumeEscapeByte(c)) continue;

    if (inPromptTail_) {
      promptTailBuffer_.push_back(static_cast<char>(c));
      if (!mark.empty() && promptTailBuffer_.size() >= mark.size() &&
          promptTailBuffer_.compare(promptTailBuffer_.size() - mark.size(), mark.size(), mark) == 0) {
        std::string tail = promptTailBuffer_.substr(0, promptTailBuffer_.size() - mark.size());
        inPromptTail_ = false;
        promptTailBuffer_.clear();
        handlePromptDetected(tail);
      } else if (promptTailBuffer_.size() > 4096) {
        // 安全弁: もっともらしい量の出力の中に閉じるマークが現れ
        // なかった。これが本物のプロンプトであることを諦め、永遠
        // にバッファし続けるのではなく、バッファされたバイトを
        // 通常の内容として再生する。
        for (unsigned char b : promptTailBuffer_) appendBodyByte(b);
        promptTailBuffer_.clear();
        inPromptTail_ = false;
      }
      continue;
    }

    if (!markerPrefix_.empty() && c == static_cast<unsigned char>(markerPrefix_[markerMatchPos_])) {
      markerMatchBuffer_.push_back(static_cast<char>(c));
      markerMatchPos_++;
      if (markerMatchPos_ == markerPrefix_.size()) {
        markerMatchPos_ = 0;
        markerMatchBuffer_.clear();
        inPromptTail_ = true;
        promptTailBuffer_.clear();
      }
      continue;
    }

    // 不一致: これまでの仮のマッチは誤報だった――それを通常の内容
    // として再生し、その後現在のバイトを位置0から再試行する
    // (それ自体が新しいマッチを始めるかもしれない)。
    for (unsigned char b : markerMatchBuffer_) appendBodyByte(b);
    markerMatchBuffer_.clear();
    markerMatchPos_ = 0;
    if (!markerPrefix_.empty() && c == static_cast<unsigned char>(markerPrefix_[0])) {
      markerMatchBuffer_.push_back(static_cast<char>(c));
      markerMatchPos_ = 1;
      if (markerMatchPos_ == markerPrefix_.size()) {
        markerMatchPos_ = 0;
        markerMatchBuffer_.clear();
        inPromptTail_ = true;
        promptTailBuffer_.clear();
      }
    } else {
      appendBodyByte(c);
    }
  }
  flushNewScrollbackLines();
}

// クラス自身の宣言コメント参照。
bool Shell::consumeEscapeByte(unsigned char c) {
  switch (escapeState_) {
    case EscapeKind::None:
      if (c != 0x1b) return false;
      escapeState_ = EscapeKind::JustEsc;
      return true;
    case EscapeKind::JustEsc:
      if (c == '[') {
        escapeState_ = EscapeKind::Csi;
        escapeSequence_ = "\x1b[";  // escapeSequence_自身の宣言コメント参照
      } else if (c == ']') {
        escapeState_ = EscapeKind::Osc;
      } else {
        // 単純な2バイトのエスケープ(ESC + 1バイト――例えばテン
        // キー/カーソルモード用のESC =/>/M)。より稀な3バイトの形
        // (例えばESC ( Bという文字集合の指定)に対してはやや性急
        // すぎ、1つの本物のバイト('B')も誤って飲み込んでしまう
        // ――受け入れられた、公開されたトレードオフ: それらは、
        // 全てのセッションで現れるCSI/OSC/bracketed-pasteとは異な
        // り、実際の対話的なシェル自身の出力からは実際には現れない。
        escapeState_ = EscapeKind::None;
      }
      return true;
    case EscapeKind::Csi:
      // パラメータバイト(0x30-0x3F)と中間バイト(0x20-0x2F)は
      // シーケンスを続けさせる。最終バイト(0x40-0x7E)がそれを
      // 終える。
      escapeSequence_.push_back(static_cast<char>(c));
      if (c >= 0x40 && c <= 0x7e) {
        escapeState_ = EscapeKind::None;
        // 2026-09-26追加(escapeSequence_自身の宣言コメント参照): SGRだけは
        // 捨てずに本文へ渡す。ただしプロンプト検出の途中(inPromptTail_/
        // markerMatchBuffer_)には混ぜない――そちらはプロンプト文字列
        // そのものの照合用。
        if (c == 'm' && !inPromptTail_ && markerMatchBuffer_.empty()) appendEscapeSequence(escapeSequence_);
        escapeSequence_.clear();
      }
      return true;
    case EscapeKind::Osc:
      if (c == 0x07) {
        escapeState_ = EscapeKind::None;  // BEL終端子
      } else if (c == 0x1b) {
        escapeState_ = EscapeKind::OscEscPending;  // ST(ESC \)終端子の開始かもしれない
      }
      return true;
    case EscapeKind::OscEscPending:
      // このバイトが本物のSTを完成させる'\'であってもなくても、
      // いずれにせよOSCシーケンスの走査は終わっている。
      escapeState_ = EscapeKind::None;
      return true;
  }
  return false;
}

// 実際のzsh使用を通じて発見された実際のバグ(2026-08-01): zsh
// 自身のZLEは、一般に、1文字を描き、その後リテラルなバックスペー
// スを、その後「完全な」最終テキストを描くことで、コマンドをエコー
// する――例えば"unset ..."を入力すると、回線越しに"u\x08unset ..."
// として届く('u'を描き、それを消し、単語全体を再描画する)――本物
// の端末では完全に不可視の何もしないもの(バックスペースは、再描画
// がそれを上書きする前に、単にカーソルを1列後ろへ動かすだけ)だが、
// appendBodyByte()は、カーソルという概念を全く持たないPipeBuffer
// に、生のバックスペース「バイト」自体をリテラルな内容として供給
// していた: それは単に、それ以外はクリーンなテキストの真ん中に、
// 可視の反転表示の制御文字グリフとして現れていた。pendingLine_は、
// この「クラス」が所有する小さな行組み立てバッファである(PipeBuffer
// 自身の内部の行分割とは別――それは依然として完全な、バックスペース/
// CRが解決された行しか見ない)。そのため、バックスペース/素のCRは、
// PipeBufferに何かが到達する「前に」、本物の端末自身の行バッファが
// そうするのと同じ方法で解決できる:
//   - バックスペース(0x08): 現在の行のために既にバッファされて
//     いる最後のバイトを落とす(プレーンなASCII範囲でのpop――
//     UTF-8を認識しない。これは特にZLE自身によるこの移植自身の
//     合成された、ASCII専用のセットアップコマンドの再描画のために
//     存在するのであり、一般的なマルチバイトの内容のためではない
//     ため)。
//   - 素のCR(\rの直後に\nが「続かない」): 本物の「列0へ戻り、
//     まさに上書きしようとしている」――それが何列幅だったかを
//     推測する必要無く、それを全てバックスペースで消し去るのと
//     同じ正味の効果で、現在の行のためにこれまでバッファされて
//     いたものを何であれ捨てる。
//   - CRの直後にLFが続く: 通常のCRLFの行末、\r自体は何も貢献せず
//     落とされる。
//   - それ以外の何か: 通常通り追加される。末尾の'\n'は、今や完成
//     した行を、1回の呼び出しでpipeBuffer_.feed()へフラッシュ
//     する。
// escapeSequence_自身の宣言コメント参照。直前の裸のCR(sawBareCr_)は、
// appendBodyByte()で次の普通の文字が来た時と同じく「行の上書き」として
// 先に確定させてから積む。
void Shell::appendEscapeSequence(const std::string &sequence) {
  if (sawBareCr_) {
    sawBareCr_ = false;
    pendingLine_.clear();
    retireTentativeLivePrompt();
  }
  pendingLine_ += sequence;
}

namespace {
// pendingLine_末尾に連なるSGR(`ESC[...m`)を除いた、見える部分の終わり
// の位置。バックスペースで消すべき1文字を、色コードの中からではなく
// 本物の文字から選ぶために使う。
std::size_t visibleEndIgnoringTrailingSgr(const std::string &line) {
  std::size_t end = line.size();
  while (end > 0 && line[end - 1] == 'm') {
    std::size_t esc = line.rfind('\x1b', end - 1);
    if (esc == std::string::npos || esc + 1 >= end || line[esc + 1] != '[') break;
    bool params = true;
    for (std::size_t k = esc + 2; k + 1 < end; k++)
      if (!((line[k] >= '0' && line[k] <= '9') || line[k] == ';' || line[k] == ':')) params = false;
    if (!params) break;
    end = esc;
  }
  return end;
}

// 確定前のプロンプト表示(flushIdleOutput())用: SGRを取り除いた文字列
// (promptPlain_は幅計算に使われ、埋め込みのエスケープを含まない約束)。
std::string stripSgr(const std::string &line) {
  std::string out;
  for (std::size_t k = 0; k < line.size(); k++) {
    if (line[k] == '\x1b' && k + 1 < line.size() && line[k + 1] == '[') {
      std::size_t m = line.find('m', k + 2);
      if (m != std::string::npos) {
        k = m;
        continue;
      }
    }
    out.push_back(line[k]);
  }
  return out;
}
}  // namespace

void Shell::appendBodyByte(unsigned char c) {
  if (sawBareCr_) {
    sawBareCr_ = false;
    if (c == '\n') {
      pendingLine_.push_back('\n');
      pipeBuffer_.feed(pendingLine_);
      pendingLine_.clear();
      retireTentativeLivePrompt();
      return;
    }
    pendingLine_.clear();
    retireTentativeLivePrompt();
    // フォールスルー: `c`自体はまだ以下で処理する必要がある
  }
  if (c == '\r') {
    sawBareCr_ = true;
    return;
  }
  if (c == 0x08) {
    // 2026-09-26: 末尾が色コード(appendEscapeSequence())なら、その手前の
    // 本物の文字を消す(色コードは残す)。
    std::size_t end = visibleEndIgnoringTrailingSgr(pendingLine_);
    if (end > 0) pendingLine_.erase(end - 1, 1);
    return;
  }
  pendingLine_.push_back(static_cast<char>(c));
  if (c == '\n') {
    pipeBuffer_.feed(pendingLine_);
    pendingLine_.clear();
    retireTentativeLivePrompt();
  }
}

// tentativeLivePrompt_自身の宣言コメント(shell.hpp)参照。
void Shell::retireTentativeLivePrompt() {
  if (!tentativeLivePrompt_) return;
  tentativeLivePrompt_ = false;
  promptPlain_.clear();
  promptDecorated_.clear();
}

// pendingLine_に蓄積された部分的な(まだ改行で終端されていない)行
// も、呼び出し元がpipeBuffer_.lines()/flush()をまさに参照しようと
// している時は常にpipeBuffer_へ到達する必要がある――そうしないと、
// 実際の出力の丸ごと1行分の進行中の内容が単に消えてしまう。
// handlePromptDetected()自身のpipeBuffer_.flush()呼び出しで共有
// される。
void Shell::flushPendingLine() {
  if (pendingLine_.empty()) return;
  pipeBuffer_.feed(pendingLine_);
  pendingLine_.clear();
}

// 自身の宣言コメント(shell.hpp)参照。pendingLine_はクリアしない
// (tentativeLivePrompt_自身のコメント参照――まだ続く通常の出力行
// だった場合、appendBodyByte()側が改行到達時に正しく後始末する)。
void Shell::flushIdleOutput() {
  if (pendingLine_.empty()) return;
  std::string plainPending = stripSgr(pendingLine_);  // stripSgr()自身のコメント参照
  if (plainPending.empty()) return;
  if (tentativeLivePrompt_ && plainPending == promptPlain_) return;  // 前回描画から変化無し
  awaitingRealPrompt_ = false;  // handlePromptDetected()自身の同名の代入と同じ理由
  promptPlain_ = plainPending;
  promptDecorated_ = plainPending;  // 確証の無い推測なので、usilic-1.5b:のような装飾は付けず生のバイト列のまま
  tentativeLivePrompt_ = true;
  updateCommandLineField();
  redrawPromptRow();
}

void Shell::flushNewScrollbackLines() {
  const auto &plines = pipeBuffer_.lines();
  if (plines.size() <= linesConsumed_) return;
  std::size_t start = linesConsumed_;
  // pendingEchoFilter_自身のコメント参照: それがちょうど送信された
  // ばかりのコマンドに対するシェル自身の行エディタのエコー
  // (リテラルなテキストのbashスタイル、または裸の空行のzsh
  // スタイルのどちらか)であれば、まさに最初の新しい行を落とす
  // ――ここでチェック/クリアされる。送信後、何であれ新しい行が
  // そもそも可視になる最初の地点なので、エコーがいくつの別々のpty
  // 読み込みにまたがって届いたかは決して問題にならない。
  if (pendingEchoFilter_) {
    const std::string &first = plines[start].text;
    if (first == *pendingEchoFilter_ || first.empty()) start++;
    pendingEchoFilter_.reset();
  }
  std::vector<ContentView::Line> newLines(scrollback_.lines());
  newLines.reserve(plines.size());
  for (std::size_t i = start; i < plines.size(); i++)
    newLines.push_back({plines[i].text, std::nullopt, std::nullopt, plines[i].attributes});
  linesConsumed_ = plines.size();
  scrollback_.setLines(std::move(newLines));
  exitHistoryBrowsing();  // 新しい出力の到着は「生きた状態へ戻る」ことを意味する、自身の宣言コメント参照
  // 念のための保険であり、必須ではない(2026-08-04): 今や
  // scrollback_.setBottomAnchored(true)(コンストラクタ自身の
  // コメント参照)が、最後の本物の内容行を、commandLineRow()自身の
  // 今や「固定された」行のすぐ上に永続的に固定し続けるので、ここ
  // での内容の成長は、コマンドの出力がいくつの別々のフラッシュに
  // またがって届こうと、もはやプロンプトと衝突したり上書きしたり
  // することができない――この2つの呼び出しが元々回避するために
  // 追加された、2つの「以前の」、今は時代遅れのバグ(commandLine_
  // のキャッシュされたフィールドが古びること、その後プロンプト
  // 自身の画面上のテキストが後の内容の再描画によって黙って上書き
  // されること)は、構造的に再発し得ない。それでも保持されている:
  // 無害であり(commandLineRow()は今や純粋な定数なので、
  // updateCommandLineField()は単に同じ値を再計算するだけである。
  // 繰り返される同一のredrawPromptRow()は、一握りの書き込みと1回の
  // flushである――自身のコメント参照)、この関数を、他所の呼び出し
  // 元が正しい瞬間にプロンプトを再描画することに頼るのではなく、
  // 自己完結させたままにする。
  // 2026-09-06訂正: 上のコメントの「単に同じ値を再計算するだけ」は
  // もはや正しくない――実際のユーザー報告("`cat > /tmp/test.txt`の
  // 後の`abc`がcat直下位置から入力される")により、`cat > file`の
  // ようにcat自身は何も出力しなくても、書き込んだコマンド行自体の
  // シェル側エコーは(pendingEchoFilter_で表示上は捨てられるとしても)
  // ここを通るため、awaitingRealPrompt_中にsubmitLine()自身が列0・
  // 全幅へ広げたcommandLine_のフィールドを、無条件のupdateCommand
  // LineField()がstaleなpromptColへ踏みつぶしてしまっていた。
  // refreshCommandLineField()(自身の宣言コメント参照)がその区別を
  // 一元的に判断する。
  refreshCommandLineField();
  // 2026-09-06追加: redrawPromptRow()自身が今やawaitingRealPrompt_
  // (自身の宣言コメント参照)を見て太字のプロンプト接頭辞自体の
  // 描画を自制するので、ここは以前通り無条件に呼んでよい――`cat`が
  // タイプした内容をエコーする等でこの関数が何度呼ばれても、本物の
  // プロンプトが戻ってくるまでは行が空欄のままであり続ける。
  redrawPromptRow();
}

// ちょうど送信されたばかりのプロンプト+コマンドの「永続的な」
// 記録を、それ自身のscrollback_の行として(後で再送信できるよう
// Line::shellCommand経由で`command`を付けて)追加する――legacy自身
// のデータモデルからの移植: shell.cのPPの連鎖には別々の「生きた
// プロンプトウィジェット」が全く無く、プロンプト行は他のスクロー
// ルバックの行と全く「同じ」連結リストの単なるもう1つのノードに
// 過ぎない。これがまさに、M_kup/M_kdnがカーソルを直接過去のプロン
// プトへ動かし、'\r'経由でそれを再実行できるようにしているものと
// 同じである(^P/^Nに関するこのクラス自身のコメント、そして
// Application::feedShellByte()のEnter処理参照)。コマンドが実際に
// 送信される「前に」submitLine()から呼ばれるので、凍結された行
// 自身のscrollback_内の位置は、それが送信された瞬間に一致する
// ――コマンドが生み出すどんな出力も、コマンドが出力を生み出し
// 始めた時点でlegacyの連鎖がプロンプトノードを越えて成長するのと
// 同様に、通常のflushNewScrollbackLines()経路経由で自然にその
// 「後に」追加される。
// 2026-08-24の修正、実際のユーザー報告: 長い、CP/MV/ARCによって
// 注入されたコマンド行(完全なパスに複数バイトの日本語ファイル名を
// 加えたものは、ユーザーが手で入力するどんなものよりもずっと簡単
// に`columns_`を超える)が、生きたSHELLのスクロールバックで、行の
// 途中で黙って切り詰められていた(pipeBuffer_自身の既に折り返された
// 出力を読むLOGモードは影響を受けなかった――この行は下参照の通り
// pipeBuffer_を全く通っていなかった)。ContentView自身の行の描画は
// それ自体の折り返しを持たない――他の全てのスクロールバックの行は、
// pipeBuffer_.wrapLine()によって既に`columns_`に収まるよう折り
// 返された状態でそれに到達する(flushNewScrollbackLines()自身の
// コメント参照)が、この関数はそれを完全に迂回し、1つの長い
// ContentView::Lineを直接構築していた。代わりに、同じテキストを
// 使い捨てのPipeBufferインスタンス経由でルーティングする。純粋に
// その既に正しい(そして既に他所でテスト済みの)UTF-8/EUCを認識
// する幅折り返しを再利用するため――ここで文字境界を安全にする
// 折り返しを2度目に再実装するより安価である。`command`(^P/^N
// カーソルによる再送信のため、この関数自身のクラスコメント参照)
// は、最初のものだけでなく「全ての」折り返されたセグメントに付け
// られるので、カーソルがたまたまどの視覚的な行に着地しようと再
// 送信が動作する。
void Shell::freezePromptLine(const std::string &command, bool includePrompt) {
  PipeBuffer wrapper(options_.encoding, columns_, /*maxBytes=*/1 << 20);
  wrapper.feed((includePrompt ? promptPlain_ : std::string()) + command + "\n");
  wrapper.flush();
  std::vector<ContentView::Line> newLines(scrollback_.lines());
  for (const auto &wrapped : wrapper.lines()) newLines.push_back({wrapped.text, command, std::nullopt, {}});
  scrollback_.setLines(std::move(newLines));
  exitHistoryBrowsing();  // 新しく送信されたコマンドは「生きた状態へ戻る」ことを意味する、自身の宣言コメント参照
}

// start()が新しい子を生成する前に、セッションごとの状態の全ての
// 部分をクリアする――legacy自身のdo_shell()からの移植: 基礎となる
// シェルプロセス(そしてそれと共にoutput_shell()自身の中継プロセス)
// が死んだ時点で、SHELLへの「次の」入場は、古いものへ追記し続け
// るのではなく、本当に新しいPPの連鎖(print_shell()自身の
// "SHELL version..."バナー行+最初の空のプロンプト)を割り当てる。
// 実際のユーザー依頼(2026-08-02): "exit[Enter]でSHELLが終了し、
// 再起動では再び冒頭行から始まる...このほうがSHELLの存在意義が
// はっきりする"――ユーザーは、legacyのCUIと並べて比較した後、
// これをこの移植自身の設計(^Gをまたいで生き残る単一の長命な
// セッションとしてのShell)より明確に好んだ。まさに、それがSHELL
// を、永続的な端末の代替ではなく、ファイル操作/コマンドのための
// 薄い、目的に特化した起動器であることを明確にするためである。
// これが無いと、terminate()の後に再びstart()を呼ぶと、ここの他の
// 全てのメンバー(scrollback_のlines_、pipeBuffer_の部分的に折り
// 返された状態、markerPrefix_のマッチの状態等)が依然として死んだ
// セッション自身の残骸を保持したまま、新しい子を生成することに
// なる。
void Shell::resetSessionState() {
  pipeBuffer_ = PipeBuffer(options_.encoding, columns_, options_.scrollbackBudget);
  pendingLine_.clear();
  linesConsumed_ = 0;
  pendingEchoFilter_.reset();
  markerMatchPos_ = 0;
  markerMatchBuffer_.clear();
  inPromptTail_ = false;
  promptTailBuffer_.clear();
  escapeState_ = EscapeKind::None;
  promptPlain_.clear();
  currentDirectory_.clear();
  scrollback_.setLines({});
  commandLine_.setText("");
  awaitingRealPrompt_ = false;  // 自身の宣言コメント参照、新しいセッションは常に真っ新から始まる
  tentativeLivePrompt_ = false;  // 同上
}

int Shell::promptGeneration() const { return promptGeneration_; }

void Shell::handlePromptDetected(const std::string &tailBeforeMark) {
  promptGeneration_++;
  // awaitingRealPrompt_自身の宣言コメント参照――マーカーが実際に
  // 現れた、すなわち本物のプロンプトが本当に戻ってきたことが今
  // 確認できたので、submitLine()自身が空欄のままにしていた行を、
  // このすぐ下のredrawPromptRow()が正しい内容で描き直す。
  awaitingRealPrompt_ = false;
  // 本物のマーカー一致による確定なので、flushIdleOutput()自身の
  // 仮の推測(tentativeLivePrompt_)はもう無関係――以下で無条件に
  // promptPlain_/promptDecorated_を上書きするので、フラグだけ戻す。
  tentativeLivePrompt_ = false;
  // シェル自身の\w(bash)/%~(csh)展開に、そのリテラルなプロンプト
  // 文字('$'/'#'/'%')と1つの空白が続く――クリーンなディレクトリ
  // 文字列を取り戻すため、その末尾の2バイトを取り除く。これは、
  // シェル自身の描画されたプロンプトテキストに対するテキストの
  // ヒューリスティックである(シェルに直接cwdを尋ねる裏チャネルは
  // 無い)。legacyのwrite_shell()が取ったのと同じアプローチだが、
  // write_shell()の固定バッファ+strcpy(そもそもそれが、あの関数の
  // 重複コピーのバグを可能にしたもの、CLAUDE.mdの履歴参照)の
  // 代わりにstd::stringで構築されている。
  std::string dir = tailBeforeMark;
  if (dir.size() >= 2 && dir.back() == ' ' &&
      (dir[dir.size() - 2] == '$' || dir[dir.size() - 2] == '#' || dir[dir.size() - 2] == '%')) {
    dir.erase(dir.size() - 2);
  }
  // 2026-08-17の修正: zshのデフォルトPS1(`%~`)――そしてbashの
  // `\w`も――は、cwdがそれ自体$HOMEであるかそのサブディレクトリで
  // ある時は常に、それを先頭の'~'で省略する(例えば「~」や
  // 「~/git/usilic」)。これは以前、完全に展開されないままcurrentDirectory_
  // に保存されていた。実際の報告("SHELLで'less CLA'^I（補完が利か
  // ない）"、legacy自身の動作する補完と直接比較して)が、
  // setCompletionProvider()のcompleteFilesystemPath()が、この生の
  // 「~」接頭辞付きの文字列に対してstd::filesystem::
  // directory_iterator()を呼んでいたことを突き止めた。それは本物の
  // ファイルシステムパスではない(チルダ展開はシェルの慣習であり、
  // OSやstd::filesystemが理解するものでは決してない)――シェルのcwd
  // が$HOMEの下のどこか(実際にはほぼ常にそうである)にある時は常に、
  // 黙って0個の候補を返していた。ここで展開され、currentDirectory_
  // の全ての消費者(補完、モードライン自身のtrailing2表示、
  // directoryChangedCallback_)が既に漏斗のように通る唯一の場所。
  if (const char *home = std::getenv("HOME"); home && *home) {
    std::string homeStr = home;
    if (dir == "~") {
      dir = homeStr;
    } else if (dir.size() > 1 && dir[0] == '~' && dir[1] == '/') {
      dir = homeStr + dir.substr(1);
    }
  }
  if (dir != currentDirectory_) {
    currentDirectory_ = dir;
    if (directoryChangedCallback_) directoryChangedCallback_(dir);
  }

  flushPendingLine();
  pipeBuffer_.flush();
  flushNewScrollbackLines();

  // 2026-08-17の変更: 可視のプロンプト接頭辞は「usilic-1.5b:」で
  // あり、options_.programName(依然として単に「usilic」)では
  // 「ない」――programName自体は手を付けられないままにされている。
  // それはmarkerPrefix_(内部のPTY出力検出の番兵、画面に実際に
  // 描かれるものとは無関係な概念)とShell::programName()(モード
  // ラインの「(zsh)」/「(bash)」バッジ、この移植ではなく「子」の
  // シェルの名前)にも供給されるため。直接依頼された通り、太字は
  // 今やコロンを含む「usilic-1.5b:」全体を覆う(以前は名前自体だけ
  // が太字で、コロンはプレーンなままだった)。これにより、LOG機能
  // (docs/CHANGES.md、LOGセクション)は後で、ログに記録された
  // プロンプト行を、ユーザーがプロンプトで同じプレーンテキストの
  // 文字列を手で入力したものと区別できる。
  static const std::string kPromptDisplayName = "usilic-1.5b";
  promptPlain_ = kPromptDisplayName + ":" + tailBeforeMark;
  updateCommandLineField();
  commandLine_.setText("");

  promptDecorated_ = display_.attributeEscape(Display::Attribute::Bold) + kPromptDisplayName + ":" +
                      display_.attributeEscape(Display::Attribute::BoldOff) + tailBeforeMark;
  redrawPromptRow();
}

// SHELLが起動した直後(またはそれ以外の理由でscrollback_が完全な
// ビューポートに満たない間)、どんなscrollback_の変更(freezePromptLine()、
// moveUp()/moveDown()(^P/^N)、goToBottom())も、行0からheight_-1
// までの全ての行を無条件にクリアする「完全な」ContentView::
// redraw()を実行する。これにはcommandLineRow()(scrollback_自身の
// 最後の内容行、まさに生きたプロンプトが住む場所)も含まれる
// ――lines_.size()が完全なビューポート未満になった時点で、
// ContentViewはそこに描くべき行を持たないので、その行は単に空白の
// ままにされる。プレーンなcommandLine_.redraw()は、「編集可能な
// フィールド」の部分だけを塗り直す(promptCol以降、
// updateCommandLineField()参照)――太字の「プロンプト接頭辞
// テキスト自体」は、handlePromptDetected()の内側でだけ、別に、
// 一度だけ描画されるので、他の何もそれを再発行することは決して
// 無かった。実際のユーザー報告(2026-08-02): "最初の起動直後
// および^Pで SHELLプロンプトが消える"――両方のケースがPTY経由で
// 再現された: ensureShellActive()自身のscrollback_.redraw()呼び出し
// が、プロンプトがハンドシェイク中に既に描画された「後」に実行
// された直後、そして^Gのmoveup()(今はscrollback_自身のカーソルを
// 直接駆動する、Application::feedShellByte()自身のコメント参照)
// が、短いスクロールバックで同じ完全な再描画を引き起こした後。
// scrollback_の変更の後に素のcommandLine_.redraw()を行う全ての
// 呼び出し箇所は、代わりにこれになる必要があった。
void Shell::redrawPromptRow() {
  int promptRow = commandLineRow();
  display_.moveCursor(promptRow, 0);
  display_.clearToEndOfLine();
  // 2026-09-06追加、直接のユーザー報告("`^[scat^M`の挙動が新旧で
  // 同じかどうかに尽きる"、実機PTYの生バイト列の突き合わせで発覚)
  // ――awaitingRealPrompt_自身の宣言コメント参照。以前はこの関数自体
  // が常に無条件で太字の「準備完了」プロンプト接頭辞(promptDecorated_)
  // を書いていたため、submitLine()側だけをガードしても、
  // flushNewScrollbackLines()(catが何か新しい出力を1バイトでも生む
  // たびに呼ばれる、他にもApplication側の多数の呼び出し箇所がある)
  // 経由でこの関数が「別途」呼ばれた瞬間に、せっかく空欄のままにした
  // 行へすぐさま同じ太字プロンプトが描き直されてしまっていた――
  // 呼び出し箇所ごとに個別にガードするのではなく、この関数自体に
  // 一元化する方が、将来の新しい呼び出し元を見落とすリスクが無い。
  // commandLine_.redraw()自体は無条件のまま――commandLine_自身の
  // テキストはこの時点で常に空(submitLine()自身のcommandLine_.
  // setText("")参照)なので、何も見える形では描かれず、実質的に
  // 行全体が空欄のまま、という見た目がそのまま保たれる。
  if (!awaitingRealPrompt_) display_.write(promptDecorated_);
  display_.flush();
  commandLine_.redraw();
  // 2026-09-06追加、直接のユーザー報告(スクリーンショット添付)
  // ――awaitingRealPrompt_の間、commandLine_自身のテキストは常に空
  // (submitLine()自身のcommandLine_.setText("")参照)だが、
  // commandLine_.redraw()はそれでもカーソルをcol0_、すなわち
  // プロンプト文字列の直後(promptCol、updateCommandLineField()参照)
  // へ置く――これはちょうど1行上の、freezePromptLine()が確定させた
  // 「cat」等コマンド文字列の真下に当たり、まだ入力を受け付けない
  // 状態なのにカーソルだけが宙に浮いているように見えて違和感がある
  // (legacyはこの行を完全に空欄のままにするので、カーソルも自然に
  // 左端に留まる)。この状態の間だけ、カーソルを行の左端(列0)へ
  // 明示的に動かす。
  if (awaitingRealPrompt_) {
    display_.setVisibleCursor(promptRow, 0);
    display_.flush();
  }
}

void Shell::updateCommandLineField() {
  int promptCol = displayWidth(promptPlain_);
  int width = std::max(1, columns_ - promptCol);
  commandLine_.setField(commandLineRow(), promptCol, width);
}

void Shell::refreshCommandLineField() {
  if (awaitingRealPrompt_) {
    commandLine_.setField(commandLineRow(), 0, columns_);
  } else {
    updateCommandLineField();
  }
}

int Shell::commandLineRow() const {
  // 再び固定された定数(2026-08-04の改訂)――なぜこれが今安全なのか
  // についてはコンストラクタ自身のコメント参照: scrollback_自身の
  // 下端固定は、最後の本物の内容行が常にこの行のすぐ上にあること
  // を保証するので、以前の(2026-08-03の)内容相対の改訂がそうして
  // いたように、現在どれだけの内容が存在するかを追跡する必要が
  // ここには全く無い。
  if (viewportRows_ <= 1) return 0;
  return viewportRows_ - 2;
}

int Shell::displayWidth(const std::string &text) const {
  if (options_.encoding != Charset::Encoding::Utf8) return static_cast<int>(text.size());
  int width = 0;
  std::size_t i = 0;
  const auto *bytes = reinterpret_cast<const unsigned char *>(text.data());
  while (i < text.size()) {
    int len = Charset::sequence_length(bytes[i]);
    if (i + static_cast<std::size_t>(len) > text.size()) len = static_cast<int>(text.size() - i);
    width += Charset::display_width(bytes + i, len);
    i += static_cast<std::size_t>(len);
  }
  return width;
}

// output_shell()のPS1/eosセットアップ行からの移植。移植されて
// いない: PAGERのexport(legacyはそれを、s_S(S_usilic)経由でusilic
// バイナリ自体へ向け、usilic自身の内部ページャーモードと統合していた
// ――このC++移植には、それが向かうべき等価物がまだ存在しないので、
// 意味の無いものへ向けるのではなく省略されている。ページャーモード
// が存在するようになったら再検討する)。
// output_shell()のPS1/eosセットアップ行からの移植だが、下の番兵の
// echoを含む「全ての」文が、(この方法の以前のバージョンがそうして
// いたようないくつかの独立した\n終端のコマンドとして送られるので
// はなく)legacy自身の単一のセミコロンで連結された行(shell.c)に
// 合わせて、バックスラッシュ改行の継続を介して「1つの」論理的な
// シェルコマンドへ連結される。この方法を形作った2つの歴史的な
// バグは、異なる軸に位置していることが判明し、両方を同時に満たさ
// なければならなかった:
//   - 実際のzsh使用を通じて発見された実際のバグ(2026-08-01):
//     約80桁を超えるほど長い単一の「物理」行(mark+programName+
//     代入全体は簡単にそうなる)は、シェル自身の行エディタ(zshの
//     ZLE、bashのreadlineも)がそれを実行するに至る「前に」
//     「折り返される」。そして折り返された入力行自身の再描画ロジ
//     ックはカーソルを動かし、既に画面上にあるものを選択的に上書
//     きする――「このバイトを追加する」ことだけを理解し、カーソル
//     移動/上書きという概念を全く持たない、このクラス自身のバイト
//     消費者(appendBodyByte() -> PipeBuffer)とは根本的に非互換
//     である。症状: スクロールバック内の重複したコマンドテキスト
//     の断片(例えば「uunset」、「tt PROMPT」)。
//   - 実際のユーザー報告(2026-08-02、スクリーンショット): SHELLが
//     起動した直後、実際のコマンドが1つも実行されないうちに、
//     複数の空行と、はぐれた「user@host dir %」スタイルのプロンプ
//     トの断片が、本物の最初のプロンプトがあるべき場所より上に
//     既にスクロールバックに座っていた。根本原因: 各文をそれ自身
//     の別々の\n終端のコマンドとして送ることは、それぞれを完全に
//     別々の「対話的な」コマンドにし、対話的なシェルはそれらの
//     全ての後に新しいプロンプトを描く――下のPROMPT/PS1がまだ効い
//     ていない初期のものも含めて。そのためそれらはシェル自身の
//     素の/変更されていないプロンプトフォーマットを使っていた。
//     processOutputBytes()のmarkerPrefix_の走査は、「この」クラス
//     自身のmark+prog+":"フォーマットのプロンプトだけを認識する
//     ので、それらの初期の間違ったフォーマットのプロンプトの描画
//     はそれぞれ、プロンプトとして認識され消費される代わりに、
//     通り抜けてリテラルなテキストとしてスクロールバックへ漏れ
//     出していた。
// バックスラッシュによる継続は両方を一度に解決する: 個々の「物理」
// 行はそれぞれ短いままであり(最初のバグを回避)、シェルはそれの
// どれかを実行したり新しいプロンプトを描いたりする前に、それ全体
// を「1つの」論理的なコマンドとして読む(2番目のバグを回避)
// ――セットアップ全体に対して正確に1回のプロンプト再描画が起き、
// それはPROMPT/PS1が既に効いた後にだけ起きる。
namespace {
// application.cpp自身のshellQuoteArg()/configuration.cpp自身の
// shellQuoteSinglePath()と同じPOSIXのシングルクオートエスケープの
// 形――このファイルもまた、それぞれの理由(各コメント参照)で自身の
// 小さなローカルヘルパーを持つ、という既に確立した慣習に倣う。
std::string shellQuotePath(const std::string &path) {
  std::string quoted = "'";
  for (char c : path) {
    if (c == '\'')
      quoted += "'\\''";
    else
      quoted += c;
  }
  quoted += "'";
  return quoted;
}
}  // namespace

std::string Shell::buildSetupCommand(const std::string &initialDirectory) const {
  const std::string &mark = options_.promptMark;
  const std::string &prog = options_.programName;
  // 2026-09-15追加、initialDirectory自身の宣言コメント(shell.hpp)
  // 参照。cd自体の成功/失敗に関わらず後続の文は続行してよいので
  // (存在しなくなったディレクトリを渡された場合でも、シェル自体が
  // 死ぬ理由には全くならない)、`;`で区切る他の全ての文と同じ扱いで
  // よい――他の文と違って`&&`にする必要は無い。
  std::vector<std::string> cdStatement;
  if (!initialDirectory.empty()) cdStatement = {"cd " + shellQuotePath(initialDirectory)};
  // ここでは本物の`echo`であり、これが以前そうだった無音の`:`という
  // 何もしないコマンドではない――理由については(これが送られる前に
  // ptyでECHOが今は無効にされている場所である)start()自身のコメント
  // 参照: カーネルの入力エコーがオフの状態では、番兵のテキストが
  // waitForHandshake()の読み取りループへ到達できる方法は、シェルが
  // 実際にこの行を「実行し」それを本物の出力として印字することだけ
  // であり、それこそがまさに「番兵までの全てを捨てる」が、それ以前
  // の全てのセットアップ文自身の(古びたプロンプトフォーマットかも
  // しれない)出力も安全に拾い上げるために必要な保証である。
  // childPid_の桁をリテラルに綴り出すのではなく`$$`(シェル自身の
  // PID、この子は中間のラッパー無しで直接fork/execされるので、常に
  // childPid_と等しい)を使う――実際のユーザー報告(2026-08-02、
  // スクリーンショット)が、SHELLが起動した直後にスクロールバック
  // に残る、しつこく漏れ続ける「#eos<pid>#」という行を、まさに
  // これへ辿り着いた: シェルがまだptyのecho/canonicalの設定に対する
  // 自身の制御を再主張していない場合(start()のECHO無効化は一度、
  // 早期に起こり、明らかに子自身の起動を常に生き延びるわけではない)、
  // ここで送られるリテラルな、展開されていないコマンドテキスト
  // ――「echo '#eos12345#'」――は、シェルがそれを実行する「前に」
  // エコーバックされ、そのエコーされたテキスト自体が既に、正確な
  // 番兵の部分文字列「#eos12345#」を(本物の出力としてではなく
  // 単に`echo '...'`に包まれた形で)含んでいる。waitForHandshake()
  // の走査は、シェル自身の後の、本物の出力の代わりに、この、早すぎ
  // る誤検出にマッチしてしまう。代わりに`$$`で綴ることで、ここで
  // 送られる「生の」コマンドテキストはリテラルに「#eos$$#」を含む
  // ――数字の連続は全く無い――ので、それが完全に展開された番兵の
  // 文字列自体を偶然含むことは決して無い。シェル「自身の」`$$`の
  // 評価(この行が実際に実行された時点でだけ起き、定義上、同じ連結
  // されたコマンド内でそれより前にある全ての後になる)だけがマッチ
  // を生み出せる。シングルクオートではなく特にダブルクオートで
  // 囲む。それにより`$$`は依然として展開される(内側の`#`は、
  // クオートの中の単語の途中にあるので、どちらにせよコメントの
  // パースからは安全なままである)。
  // PAGER/MANPAGERのexport(2026-08-11のoutput_shell()移植コメントが
  // "移植されていない...ページャーモードが存在するようになったら
  // 再検討する"と明記していた宿題): Application::pollShellHandoff()
  // (main.cpp自身のtryHandoffToParentShell()の「子」側と対をなす)が、
  // まさにこの「ページャーモード」――パイプされたstdinをsocket経由で
  // fileMode_.loadStream()へ流し込み、"(stdin)"として表示する――に
  // 相当する形で既に実装済みだったと判明した(2026-09-20、直接の
  // ユーザー報告"SHELLプロンプトでman ls...旧版のように(stdin)で
  // 表示して欲しい"を受けて調査した際に発見)。legacyのexport PAGER=
  // '<usilic自身> -p <pid>'とは異なり、この移植のtryHandoffToParentShell()
  // 自身の待ち合わせパスはgetppid()から独立に導出される(自身のコメント
  // 参照)ので、追加の引数は一切不要――実行ファイル自身の絶対パスを
  // そのままPAGERへ渡すだけでよい。options_.pagerPathが空(デフォルト、
  // 主にShellOptions{}を使うテスト用)の間は、この2行を丸ごと省略する
  // ――実際のバイナリパスを知らないまま無関係な子プロセスがうっかり
  // $PAGERとしてusilic自身を起動してしまう事故を避ける。
  // `$$`(シェル自身のPID、この文が実行される時点=この後で本物の
  // manPAGERとして起動される「前」のbash/zsh自身のPID)を、二重引用符
  // で単一引用符から抜けて展開させ、main.cpp自身のtryHandoffToParentShell()
  // が認識する`--usilic-pager <pid>`として埋め込む(このコメントの
  // すぐ上のpagerStatements自身の宣言コメント参照)。
  std::vector<std::string> pagerStatements;
  if (!options_.pagerPath.empty()) {
    std::string pagerCommand = "'" + options_.pagerPath + "'\" --usilic-pager $$\"";
    pagerStatements = {"export PAGER=" + pagerCommand, "export MANPAGER=" + pagerCommand};
  }
  std::string eos = "echo \"#eos$$#\"";
  // 文を1つの論理的なコマンドへ結合する: 各ペアの間に" ; \<改行>"
  // (文の区切り+行の継続。これによりシェルは各物理行の後に実行/
  // プロンプトするのではなく読み続ける)、そして最後の文の後には、
  // 実際にそれを送信するためのプレーンな改行。
  auto chain = [](const std::vector<std::string> &statements) {
    std::string result;
    for (std::size_t i = 0; i < statements.size(); i++) {
      result += statements[i];
      result += (i + 1 < statements.size()) ? " ; \\\n" : "\n";
    }
    return result;
  };
  if (isBash_) {
    std::vector<std::string> statements = {
        "unset PROMPT_COMMAND",
        "export PS1='" + mark + prog + ":\\w\\$ " + mark + "'",
        "set -o ignoreeof",
        "export COLUMNS=" + std::to_string(columns_),
        "unset HISTFILE",
    };
    statements.insert(statements.begin(), cdStatement.begin(), cdStatement.end());
    statements.insert(statements.end(), pagerStatements.begin(), pagerStatements.end());
    statements.push_back(eos);
    return chain(statements);
  }
  if (isZsh_) {
    // zsh自身のプロンプト展開シーケンス(cwd用の%~、末尾のroot
    // なら'#'/それ以外なら'%'というプロンプト文字用の%#)は、下の
    // tcsh/cshフォールバックで既に使われているのと「同じ」もので
    // ある――それらはcsh固有の構文ではなく、単にzshがたまたま
    // tcshと共有しているプロンプト展開トークンである。zshが共有
    //「しない」のは、tcshの`set VAR = value`という代入の形(zsh
    // 自身の`set`組み込みコマンドはシェル「オプション」用であり、
    // 変数用ではない)である――そのため、これは「代入」構文だけの
    // ためにそれ自身の分岐を必要とする。precmd_functions=()と
    // RPROMPT=''は、上のbashの`unset PROMPT_COMMAND`に合わせて、
    // そうしないとこの合成プロンプトの周りにANSI/Unicodeの内容を
    // 依然として注入するであろう、どんなテーマ自身の動的なプロンプ
    // トフック(oh-my-zsh/powerlevel10k/starshipのprecmdフック、
    // 右側のプロンプトテキスト)も黙らせる。IGNORE_EOFはzshの
    // `set -o ignoreeof`の等価物である(^Dはこの移植の足元でシェル
    // の子を殺せてはならない)。PROMPT_EOL_MARK=''は、zsh自身の
    // 「直前の出力が改行で終わらなかった」という指標(その実際の、
    // 文書化されたデフォルトの挙動――端末幅にパディングされた反転
    // 表示の'%')を無効にする――実際の使用を通じて発見された:
    // consumeEscapeByte()が今その周りの生のANSIコードを取り除いて
    // いても、'%'という「グリフ自体」は、エスケープシーケンスでは
    // なく実際の印字可能な内容なので、取り除くだけではそれを削除
    // できない。その機能を発生源で無効にする方が、zsh自身の特定の
    // eolマークのテキストを認識して隠すことも試みるよりも単純で
    // ある。
    std::vector<std::string> statements = {
        "unset PROMPT_COMMAND 2>/dev/null",
        "precmd_functions=()",
        "export PROMPT='" + mark + prog + ":%~%# " + mark + "'",
        "RPROMPT=''",
        "PROMPT_EOL_MARK=''",
        "setopt IGNORE_EOF",
        "export COLUMNS=" + std::to_string(columns_),
        "unset HISTFILE",
    };
    statements.insert(statements.begin(), cdStatement.begin(), cdStatement.end());
    statements.insert(statements.end(), pagerStatements.begin(), pagerStatements.end());
    statements.push_back(eos);
    return chain(statements);
  }
  // csh/tcshのフォールバック――上のbash/zshとは異なり、実際の
  // csh/tcshセッションに対して検証されていない(どちらかが実際に
  // 行使されたことがある証拠は無い)。これ以上推測するのではなく、
  // 既存のベストエフォートのデフォルトとして保たれている。今、
  // 上のbash/zshと同じ本物の`echo`の番兵と同じ1コマンドへの連結
  // アプローチを使う(裸の「#eos<pid>#」というコメント行は、ECHOが
  // 無効な状態では全く何も印字しないだろう――start()自身のコメント
  // 参照――そしてcsh/tcshの`#`は無条件にコメント文字なので、
  // どちらにせよ`:`スタイルの何もしない接頭辞に頼ることもできな
  // かった)。
  // tcsh/csh(2026-09-25拡張、直接のユーザー依頼): それまではプロンプトと
  // ignoreeofだけで、bash/zshが設定するPAGER/MANPAGER(SHELLで実行した
  // man等の出力をusilicで読む仕組み)とCOLUMNSが無かった。csh系には
  // exportが無いのでsetenvで同じ値を設定する(pagerCommand自身の
  // '...'"..."の引用は、csh系でもそのまま同じ意味になる)。
  std::vector<std::string> cshStatements = {
      "set prompt = \"" + mark + prog + ":%~%# " + mark + "\"",
      "set ignoreeof",
      "setenv COLUMNS " + std::to_string(columns_),
  };
  if (!options_.pagerPath.empty()) {
    std::string pagerCommand = "'" + options_.pagerPath + "'\" --usilic-pager $$\"";
    cshStatements.push_back("setenv PAGER " + pagerCommand);
    cshStatements.push_back("setenv MANPAGER " + pagerCommand);
  }
  cshStatements.push_back(eos);
  cshStatements.insert(cshStatements.begin(), cdStatement.begin(), cdStatement.end());
  return chain(cshStatements);
}

// 一度きりの起動時ハンドシェイク: output_shell()のメインループ前
// の走査とまさに同様、自身の「#eos<pid>#」番兵までの全てを捨てる
// ――端末エミュレータ(例えばGNOME Terminal/VTEの
// /etc/profile.d/vte.sh)は、シェルがその設定コマンドを読む前に、
// 上の設定コマンドがそれをunset/クリアする「前に」立っていた
// それ自身のプロンプトフックの仕組みを使って、既にシェルのまさに
// 最初のプロンプトを自動で描いていたかもしれない。それを飛び越えて
// スキップしないと、その残された出力は、そうしないとメインループ
// によって本物のプロンプト/内容のバイトとして誤ってパースされて
// しまう。なぜ裸の'#'では不十分なのか(それはその残された出力に
// 偶然現れることがある)についてはCLAUDE.mdの履歴参照――このpidに
// 基づく番兵は、bashだけでなく、buildSetupCommand()が今それを
// echoする全てのシェルの系統(bash/zsh/csh-tcsh全て同様)に対して
// それを避けるのに十分ユニークである。
//
// 実際のzsh使用を通じて発見された実際のバグ(2026-08-01): 番兵が
// 見つかった時点で、ここでその「地点」までに読まれた全ては正しく
// 捨てられる(appendBodyByte()に決して到達しない)――しかし1回の
// read()は、しばしば番兵と、まさに「次の」本物のプロンプトの開始を
// 「同じ」チャンクで返す(シェルはセットアップ行を終えることと
// 次のプロンプトを描くことの間で一時停止しない)。古いコードは
// マッチ時に走査バッファ「全体」を無条件に捨てており、その末尾の
// 本物の内容――しばしば次のプロンプト自身のエスケープシーケンス
// の先頭のESCバイト――を黙って落とし、シーケンスの「残り」だけ
// (例えば前にESCの無い「[27m」)をリテラルなゴミテキストとして
// processOutputBytes()へ流していた。番兵の後のバイトは、捨てられる
// のではなく通常通り供給される――しかし、なぜ「番兵を見つけた」
// ことだけではまだ「その後に続くものを供給しても安全」ではないの
// かについては下参照。
//
// 実際のユーザー報告(2026-08-02、スクリーンショット): 番兵自体が
// 本物の、シェルが発する`echo`になり(buildSetupCommand()自身の
// コメント参照)、ptyでECHOが無効にされた(start()自身のコメント
// 参照)後でさえ、短い漏れた断片(zsh自身の「> 」PS2継続プロンプト
// 表示、ZLEがバックスラッシュ継続されたセットアップコマンドを
// それ自身のread()呼び出しが返ったまさにその瞬間にまだ読み込み中
// だったことによる)が、SHELLが起動した直後、依然として時々
// scrollback_に入り込んでしまうことがあった。番兵自身のechoは
// シェルがセットアップコマンドを「実行した」ことを証明するが、
// シェルがその「結果として」(今や正しく設定されたmark+prog+":"
// フォーマットでの、本物の最初のプロンプト)書こうとしている全ての
// バイトが、この読み取りループのバッファに既に到着しているかどう
// かについては何も言わない――ptyの両端での読み取りと書き込みは
// 独立した、競合する操作である。番兵を含む最初のread()を信頼する
// のではなく、これは今、それ以上何も届かないまま20msのポーリング
// 間隔が丸ごと過ぎるまで(「落ち着く」隙間、シェル自身のセット
// アップ後のまとまった送信は常に連続的で速いので検出は安価である)、
// (同じmaxAttemptsループによって制限されて)排出し続け、その後
// markerPrefix_(mark+prog+":"、通常の動作中にこのクラスが既に
// 認識する方法を知っている「本物の」プロンプトの、固定された、
// 見間違えようのない接頭辞)を、番兵の後に届いた全てのものの中で
// 探す――「その」地点以降からだけ供給するので、番兵と本物の
// プロンプトの間のはぐれたPS2/他の断片は、リテラルなテキストとして
// スクロールバックへ漏れ出すのではなく捨てられる。markerPrefix_が
// 落ち着く隙間より前に一度も現れなければ(例えば予期しないプロンプ
// トフォーマット)、番兵の後の全ては、この修正の前にこの関数が
// していたのと同じように、フィルタされずに供給される――退行では
// なく、単に「唯一の」挙動ではなくなっただけである。
bool Shell::waitForHandshake() {
  std::string sentinel = "#eos" + std::to_string(childPid_) + "#";
  std::string scan;
  bool sentinelFound = false;
  std::string postSentinel;
  const int maxAttempts = 500;  // 500 * 20ms = up to ~10s
  for (int attempt = 0; attempt < maxAttempts; attempt++) {
    struct pollfd pfd {
      masterFd_, POLLIN, 0
    };
    int rc = poll(&pfd, 1, 20);
    if (rc < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    if (rc == 0) {
      if (sentinelFound) break;  // 落ち着く隙間: 番兵後のまとまった送信が届き終わった
      continue;
    }
    if (pfd.revents & (POLLHUP | POLLERR)) return false;
    char buf[512];
    ssize_t n = read(masterFd_, buf, sizeof(buf));
    if (n <= 0) {
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) continue;
      return false;
    }
    if (!sentinelFound) {
      scan.append(buf, static_cast<std::size_t>(n));
      if (std::size_t pos = scan.find(sentinel); pos != std::string::npos) {
        sentinelFound = true;
        postSentinel = scan.substr(pos + sentinel.size());
      } else if (scan.size() > 4096) {
        scan.erase(0, scan.size() - sentinel.size());
      }
      continue;
    }
    postSentinel.append(buf, static_cast<std::size_t>(n));
  }
  if (!sentinelFound) return false;
  if (!markerPrefix_.empty()) {
    if (std::size_t markerPos = postSentinel.find(markerPrefix_); markerPos != std::string::npos) {
      postSentinel.erase(0, markerPos);
    }
  }
  if (!postSentinel.empty()) processOutputBytes(postSentinel);
  return true;
}

LineEditor &Shell::commandLine() { return commandLine_; }
ContentView &Shell::scrollback() { return scrollback_; }

void Shell::submitLine() {
  std::string line = commandLine_.text();
  std::size_t start = line.find_first_not_of(' ');
  std::string trimmed = (start == std::string::npos) ? "" : line.substr(start);

  // 2026-09-06追加、直接のユーザー報告("`cat > /tmp/test.txt`の後に
  // `abc`と打って^Mすると、abcの左側にプロンプトが挿入されてしまう
  // ...cat直下位置へ移動してそこからの入力となり"): 何かを書き込む
  // 「前」にbusy()を保存しておく――これがtrueなら、今回のEnterは
  // 新しいトップレベルのコマンドではなく、既にフォアグラウンドで
  // 動いている`cat`等への生の追加入力(標準入力の1行)である。PROGRAM
  // 一覧との照合(find_prog()相当、下のprogramLauncher_呼び出し)も
  // freezePromptLine()自身のプロンプト接頭辞付与も、コマンド履歴
  // (historyRecorder_)への記録も、どれも「新しい対話的コマンドを
  // 起動する」時にだけ意味を持つ処理であり、単なるstdinへのデータ行
  // には当てはまらない。
  bool wasAlreadyBusy = busy();

  if (!trimmed.empty()) {
    freezePromptLine(trimmed, /*includePrompt=*/!wasAlreadyBusy);

    bool handled = false;
    if (!wasAlreadyBusy) {
      // 末尾のパイプライン段のプログラムトークン('|'で分割し、
      // 最後の段を取り、その最初の空白区切りの単語を取る)。
      // input_shell()のコマンドディスパッチロジックからの移植。
      // PROGRAMテーブルの検索自体(find_prog())はここでは移植されて
      // いない――クラスコメント参照。
      std::size_t pipePos = trimmed.find_last_of('|');
      std::string lastStage = (pipePos == std::string::npos) ? trimmed : trimmed.substr(pipePos + 1);
      std::size_t s = lastStage.find_first_not_of(' ');
      std::string stageTrimmed = (s == std::string::npos) ? "" : lastStage.substr(s);
      std::size_t e = stageTrimmed.find_first_of(" \t");
      std::string programToken = (e == std::string::npos) ? stageTrimmed : stageTrimmed.substr(0, e);
      handled = programLauncher_ && !programToken.empty() && programLauncher_(programToken, trimmed);
    }
    if (!handled) {
      pendingEchoFilter_ = trimmed;  // 自身のコメント参照――flushNewScrollbackLines()によって消費される
      // 2026-09-28修正、直接のユーザー報告("sudo apt install...でパスワード
      // (スペース1文字)待ちになるものの入力してもはねられる"): 既に動いて
      // いるプログラムへの入力(パスワード等)は、先頭の空白も含めて打った
      // ままを送る――空白を削るのは新しいコマンドを起動する時だけ。
      writeToPty((wasAlreadyBusy ? line : trimmed) + "\n");
      if (!wasAlreadyBusy && historyRecorder_) historyRecorder_(trimmed);
      awaitingRealPrompt_ = true;
    }
  } else {
    // 上の2026-09-28修正参照: 空白だけの行(スペース1文字のパスワード等)も
    // 動いているプログラムへはそのまま送る。
    writeToPty((wasAlreadyBusy ? line : std::string()) + "\n");
    awaitingRealPrompt_ = true;
  }

  commandLine_.setText("");
  // 2026-09-06追加、同じユーザー報告のもう1つの症状("aと打ったとたん
  // ブロックカーソルがcat直下位置へ移動してそこからの入力となり
  // (正しくはもちろん画面左端から)"): awaitingRealPrompt_の間、
  // redrawPromptRow()自身は太字のプロンプト接頭辞を描かない(この
  // すぐ下の呼び出し自体のコメント参照)が、commandLine_(LineEditor)
  // 自身のフィールド開始列(col0_、updateCommandLineField()参照)は
  // 直前の本物のプロンプトの幅(promptCol)のまま据え置かれていた
  // ――そのため、まだ何も見えるプロンプトが無いこの行でユーザーが
  // 実際にタイプし始めると(cat等への追加入力)、文字とカーソルの
  // 両方が、あたかも見えないプロンプトの直後であるかのように、行の
  // 途中から描かれてしまっていた。refreshCommandLineField()(自身の
  // 宣言コメント参照)がこの間だけフィールドを列0・全幅へ広げる
  // ――handledブランチ(awaitingRealPrompt_のまま偽)では、これは
  // updateCommandLineField()と全く同じ、既に正しいpromptColを
  // 無害に再計算するだけである。
  refreshCommandLineField();
  // 2026-09-06追加、直接のユーザー報告("旧では`cat`が入力待ちになる
  // ...`^[scat^M`の挙動が新旧で同じかどうかに尽きる")――実機PTYの
  // 生バイト列を1バイト単位で突き合わせた結果判明した相違点。
  // legacy自身は"cat"を送った直後、次の行を完全に空欄のまま(カーソル
  // をそこへ置くだけ)にする――太字の"U v1.5b:..."という「準備完了」
  // に見えるプロンプト行を、本物のプロンプトが戻ってくるまで「決して」
  // 描かない。writeToPty()自体は非同期(書き込むだけで、シェルが実際に
  // 読んで処理し終えるのを待たない)なので、`cat`のようにまだ何も
  // 終わっていない相手に対して即座にredrawPromptRow()を呼ぶと、
  // いかにも「もう次の入力を受け付けられます」という見た目の太字
  // プロンプトを一瞬で描いてしまう。redrawPromptRow()自身の
  // 2026-09-06追加分のコメント参照――awaitingRealPrompt_である間は
  // その太字プロンプト接頭辞自体を描かないので、ここは常に無条件に
  // 呼んでよい(handled、すなわちprogramLauncher_経由で既に同期的に
  // 完了している場合はawaitingRealPrompt_がfalseのままなので、元通り
  // 即座に本物の準備完了プロンプトが描かれる)。
  redrawPromptRow();
}

Shell::TabOutcome Shell::onTab() {
  TabOutcome outcome;
  if (!completionProvider_) return outcome;
  const std::string &text = commandLine_.text();
  int cursor = commandLine_.cursorBytePos();
  std::size_t searchFrom = (cursor > 0) ? static_cast<std::size_t>(cursor - 1) : 0;
  std::size_t wordStart = text.find_last_of(" \t", searchFrom);
  wordStart = (wordStart == std::string::npos) ? 0 : wordStart + 1;
  if (static_cast<int>(wordStart) > cursor) wordStart = static_cast<std::size_t>(cursor);
  std::string partial = text.substr(wordStart, static_cast<std::size_t>(cursor) - wordStart);

  // 2026-09-18追加、HELP.txt 2.13章「LIST」の実装: shouldSearchPath()
  // 自身のコメント参照――バッファ「全体」に区切り文字が無ければ
  // (=まだコマンド名自体を入力中)$PATH検索、そうでなければ従来通り
  // カレントディレクトリのファイル名補完。programCompletionProvider_
  // が設定されていない場合(テスト用の合成Provider等)は常に後者へ
  // フォールバックする。
  bool searchPath = programCompletionProvider_ && shouldSearchPath(text);
  auto candidates = searchPath ? programCompletionProvider_(partial) : completionProvider_(partial);
  if (candidates.empty()) {
    tabArmed_ = false;
    return outcome;
  }
  // 2026-09-15修正、直接のユーザー依頼("PROGRAMのプロンプトで'less '
  // と打ったところで^Iとやっても候補が出ない...最長共通接頭辞まで
  // 補完"): 以前は候補がちょうど1件の時だけ補完し、複数件(曖昧)なら
  // 何もしなかった――bashの素のTabと同じ、候補全体の最長共通接頭辞
  // まで補完する形にした(候補が1件ならその候補自身がそのままlcpに
  // なるので、以前の単一候補の場合の挙動は変わらない)。
  std::string lcp = candidates[0];
  for (std::size_t i = 1; i < candidates.size(); i++) {
    std::size_t j = 0;
    while (j < lcp.size() && j < candidates[i].size() && lcp[j] == candidates[i][j]) j++;
    lcp.resize(j);
  }
  bool ambiguous = candidates.size() > 1;
  // 2026-09-18追加、HELP.txt 2.13章「LIST」の実装(old/src/key.c::
  // input_key()の`f_list`フラグと同じ役割): 曖昧な状態のまま直後に
  // もう一度TABが来た場合(tabArmed_が既にtrue)だけ一覧表示を要求
  // する――このメソッド自身のクラスコメント参照、実際にListBrowser
  // へ遷移するのは呼び出し元(Application)の仕事。
  if (ambiguous && tabArmed_) {
    tabArmed_ = false;
    outcome.showList = true;
    outcome.candidates = std::move(candidates);
    outcome.prefixBeforeWord = text.substr(0, wordStart);
    outcome.partial = std::move(partial);
    return outcome;
  }
  if (lcp.size() > partial.size()) commandLine_.paste(lcp.substr(partial.size()));
  if (ambiguous) {
    display_.putChar(7);  // BEL、legacy自身のvt_putchar(K_g)から移植
    tabArmed_ = true;
  } else {
    tabArmed_ = false;
  }
  return outcome;
}

void Shell::setCompletionProvider(
    std::function<std::vector<std::string>(const std::string &partialWord)> provider) {
  completionProvider_ = std::move(provider);
}

void Shell::setProgramCompletionProvider(
    std::function<std::vector<std::string>(const std::string &partialWord)> provider) {
  programCompletionProvider_ = std::move(provider);
}

void Shell::resetTabArm() { tabArmed_ = false; }

void Shell::sendControlByte(unsigned char byte) { writeToPty(std::string(1, static_cast<char>(byte))); }

void Shell::writeToPty(const std::string &bytes) {
  if (masterFd_ < 0) return;
  std::size_t off = 0;
  // 書き込みが進まない状態の開始時刻(下の諦める判定用)。回数ではなく時間で
  // 数える――起動直後はpoll()がすぐ返ってwrite()がEAGAINのまま空回りする
  // ことがあり、回数の上限では一瞬で諦めてしまう。
  auto stallStart = std::chrono::steady_clock::now();
  while (off < bytes.size()) {
    ssize_t n = write(masterFd_, bytes.data() + off, bytes.size() - off);
    if (n > 0) {
      off += static_cast<std::size_t>(n);
      stallStart = std::chrono::steady_clock::now();
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
      // masterFd_はノンブロッキングであり(start()参照)、fork()の
      // 直後、子が実際にスレーブ側を開くまで、ptyの入力キューは
      // 書き込みを完全に拒否することがある――実際のbashテストが
      // start()を完全にハング/失敗させることを通じて発見された:
      // 最初のPS1セットアップコマンドが、まさにこの競合で全ての
      // バイトを黙って失い、bashはその素のプロンプトのまま起動し、
      // 「#eos<pid>#」ハンドシェイクの番兵は一度も現れなかった。
      // 最初の一時的なEAGAINで書き込みの残りを黙って落とすのでは
      // なく、ptyがもっと受け入れるようになるのを短く待って再試行
      // する。
      //
      // 2026-09-27修正、直接のユーザー報告("~/.usilic/data/の._ファイル群を
      // 削除しようとするとSHELLで止まる"): 長いコマンドライン(8個の長い
      // ファイル名を並べたrm、約2.1KB)を書いている途中で、bashは受け取った
      // 文字をエコーとして出力へ書くが、usilicはこのループの中にいて出力を
      // 読まないため、ptyの出力側が満杯になるとbashは止まり、入力も読まなく
      // なる(macOSでは約2KBで詰まることを実測で確認)。その後の書き込みは
      // 捨てられ、bashには途中で切れたコマンドライン(閉じていない引用符)が
      // 届いて">"の継続プロンプトで入力待ちになっていた。書き込めない間は、
      // 出力を読んで通常どおり処理する(onPtyReadable()と同じ)ことで
      // bashを先へ進ませる。
      struct pollfd pfd {
        masterFd_, POLLOUT | POLLIN, 0
      };
      // ハンドシェイク前(start()自身のセットアップコマンドの書き込み中)は
      // 読まない――waitForHandshake()が待っている番兵を横取りしてしまう。
      if (!handshakeDone_) pfd.events = POLLOUT;
      if (poll(&pfd, 1, 100) > 0 && (pfd.revents & POLLIN)) {
        char buf[4096];
        ssize_t r = read(masterFd_, buf, sizeof(buf));
        if (r > 0) processOutputBytes(std::string(buf, static_cast<std::size_t>(r)));
      }
      if (std::chrono::steady_clock::now() - stallStart > std::chrono::seconds(10)) break;  // 無限ループの防止
      continue;
    }
    break;  // 本物の書き込みエラー――諦める
  }
}

void Shell::setDirectoryChangedCallback(std::function<void(const std::string &)> callback) {
  directoryChangedCallback_ = std::move(callback);
}

const std::string &Shell::currentDirectory() const { return currentDirectory_; }

std::string Shell::programName() const {
  return options_.argv.empty() ? "" : baseName(options_.argv[0]);
}

void Shell::setProgramLauncher(
    std::function<bool(const std::string &, const std::string &)> launcher) {
  programLauncher_ = std::move(launcher);
}

void Shell::setHistoryRecorder(std::function<void(const std::string &)> recorder) {
  historyRecorder_ = std::move(recorder);
}

void Shell::setBusy(bool busy) { busy_ = busy; }
// foregroundJobRunning()自身の宣言コメント(shell.hpp)参照。
bool Shell::foregroundJobRunning() const {
  if (masterFd_ < 0 || childPid_ <= 0) return false;
  pid_t fg = ::tcgetpgrp(masterFd_);
  return fg > 0 && fg != static_cast<pid_t>(childPid_);
}
bool Shell::busy() const { return busy_ || foregroundJobRunning(); }

void Shell::onResize(int rows, int columns) {
  viewportRows_ = rows;
  columns_ = columns;
  scrollback_.setViewport(0, std::max(1, rows - 2));  // コンストラクタ自身のコメント参照
  scrollback_.setModeLineRow(rows - 1);
  // flushNewScrollbackLines()自身の2026-09-06訂正と同じ理屈――
  // awaitingRealPrompt_中にウィンドウがリサイズされても、列0・全幅
  // への拡張をstaleなpromptColで踏みつぶさない。
  refreshCommandLineField();
  if (masterFd_ >= 0) {
    struct winsize ws {};
    ws.ws_row = static_cast<unsigned short>(rows);
    ws.ws_col = static_cast<unsigned short>(columns);
    ioctl(masterFd_, TIOCSWINSZ, &ws);
  }
  scrollback_.redraw();
  redrawPromptRow();
}

void Shell::setEncoding(Charset::Encoding encoding) {
  options_.encoding = encoding;
  commandLine_.setEncoding(encoding);
}

}  // namespace fm
