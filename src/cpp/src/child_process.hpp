#pragma once

#include <string>
#include <vector>

namespace fm {

// 外部プログラムを実行しその出力を捕捉する。fork.cの繰り返される
// pipe()/fork()/dup2()/execvp()の手順からの移植――fork_key()/
// fork_bin()/fork_chp()/fork_arc()の複数のパイプライン段階それぞれが、
// 少しずつの違い(単一コマンド vs 2段階パイプライン、LC_ALL=Cの強制、
// バッファからの標準入力)がありつつも、同じひとにぎりのシステム
// コールを独立に手書きしていた。ChildProcessは、それを8回ではなく
// 1回だけ実装するために存在する。
//
// legacyのコード自身の歴史がこれの良い論拠になっている: 「execvp()
// 失敗後、子プロセスがそのまま処理を続けtcbとして動き続けてしまう」
// バグ(fork_bin()、exit(0)の追加で修正)や、strcpyの重なりに起因する
// 一連のバグは、それぞれこれらの手書きの呼び出し箇所のどれかで独立に
// 発見された。単一の、テスト済みの実装があれば、ここでの修正が
// 一度に全てをカバーし、新しく追加された呼び出し箇所が同種のバグの
// 別バリアントを再導入することもできなくなる。
//
// 意図的に除外(将来のクラスの仕事であり、ここではない):
//   - あるファイルをどのプログラムが扱うか、およびtcbrcのCHILD/EXEC/
//     EDITOR/SHELLディレクティブのパース(child.cのsave_child()/
//     get_child()/is_child()等): これはまさしくConfigurationの仕事
//     (tcbrcのパース)とModeの仕事(拡張子マッチからT_roff/T_tar/
//     T_execのディスパッチを決定する)の組み合わせ――ChildProcessは
//     既に決定済みのargvを実行するだけで、そのargvが何であるべきか
//     を決めることはしない。
//   - 生きたPTYを持つ長寿命/対話的な子プロセス(SHELLモード): 根本的に
//     異なる形(ストリーミング、双方向、端末に接続、tcbがその周りで
//     描画を続けられるよう自前のptyが必要)――Shellの仕事。下の
//     runInteractive()はこれを必要としないずっと狭いケースである:
//     本物の端末のstdioを直接継承する(ptyもパイプ/捕捉も無し)一方、
//     tcb自身は一時的に退いている(KeyInput経由でraw modeを退出、
//     描画は起きていない)、使い切りのフォアグラウンドプログラム
//     (mode.cのdo_prog()、例えば`less somefile`のようなPROGRAM
//     モードのコマンド)――SHELLモードが持つような「並行してtcb
//     自身のUIも生かし続ける」という要求は無い。
//   - alloc_buff()にあった、読み込み中のキーボード割り込みと、
//     伸長可能なチャンクの連結リストによるバッファリング: 前者は
//     UIの関心事(ChildProcess自体にはそもそもキーボードを確認する
//     手立てが無いし、そうすべきでもない)、後者は伸長可能な
//     コンテナが無かった時代の1990年代の回避策――std::stringは
//     既に自分で伸長する。
// ChildProcess呼び出し用のオプション。意図的にChildProcessの内側に
// ネストさせず、名前空間スコープで定義してある: ネストした構造体の
// デフォルトメンバ初期化子は、外側のクラスがそれを参照するメンバ
// 関数のデフォルト引数をパースする時点ではまだ見えない(実際に
// 起きた、クラス解析が2パスであることに起因する癖――この構造体を
// ネストしたまま下で`= false`/`= nullptr`と書くとコンパイルが通ら
// ず、「default member initializer ... needed within definition of
// enclosing class ... outside of member functions」というエラーに
// なる)。Optionsをここに外出しすることで、通常通りのデフォルトメンバ
// 初期化子を持てるようになり、素の`Options options;`(波括弧無し)が、
// forceCLocale/input/stderrLogPathを不定のゴミ値のまま残すのではなく
// 構造的に安全になる――これはまさに、このクラスの以前のバージョンが
// この構造体を外に出す代わりに初期化子を削除してコンパイルエラーを
// 回避しようとした際に実際に起きたことである: あるテストケースの
// `Options options;`はstderrLogPathをゴミのnullでないポインタのまま
// 残し、prepareChild()がそれを参照外しした際にfork後の子プロセスが
// SIGSEGVでクラッシュした。
struct ChildProcessOptions {
  // 子プロセス内でのみLC_ALL=Cを強制する。fork_bin()のT_dirケース
  // からの移植: `ls`の日付列は後段でバイト単位にパースされる
  // (get_col())ため、ロケール化された月名だと、それ以降の全ての
  // カラム位置が狂ってしまう。
  bool forceCLocale = false;
  // 設定されていれば、子プロセスの標準出力から何かが読まれる前に、
  // それが子プロセスの標準入力へ書き込まれる(小さな専用のwriter
  // プロセス経由――そうすることで、大きな入力が満杯の出力パイプに
  // 対してデッドロックすることがない。legacy自身のfork_arc()/
  // fork_chp()が、同一プロセス内でのブロッキング書き込みではなく
  // この方式でパイプライン入力を書いているのと同じ理由)。
  const std::string *input = nullptr;
  // 設定されていれば、子プロセスの標準エラーは親のものを継承する
  // 代わりにこのファイルへ追記される。dup_error()からの移植。
  // 隠れたConfiguration参照ではなく素のパスとして公開してあるのは、
  // このクラスがそちらについて知る必要をなくすため――KeyInputが
  // sub.cを直接呼ぶ代わりにsetPidReporter()を公開したのと同じ考え方。
  const std::string *stderrLogPath = nullptr;
};

class ChildProcess {
 public:
  struct Result {
    std::string output;
    bool ok = false;       // 起動され、ステータス0で正常終了した
    int exitStatus = -1;   // 生の終了コード。正常終了しなかった場合は-1
  };

  using Options = ChildProcessOptions;

  static Result run(const std::vector<std::string> &argv, const Options &options = {});

  // 2段階パイプライン: argv1 | argv2、argv2の標準出力を捕捉する。
  // fork_arc()の.deb/.tgz展開チェーンと、fork_chp()のフィルタ
  // チェーン(例えば`ar p file data.tar.gz | tar xvf -`)からの移植。
  static Result runPipeline(const std::vector<std::string> &argv1,
                             const std::vector<std::string> &argv2,
                             const Options &options = {});

  // argvを、本物の端末の標準入力/標準出力/標準エラーを直接継承した
  // フォアグラウンドで実行する(パイプ無し、出力の捕捉無し)――本物の
  // 端末アクセスが必要な使い切りの対話的プログラム(例えばPROGRAM
  // モードから起動する`less`/`vi`)のためのもの。do_prog()の
  // fork()/execlp()/wait()(素の子プロセス管理を行う半分。呼び出しの
  // 前後でraw termioを退出/再突入するのは、KeyInput経由での呼び出し
  // 側の仕事――このクラスが他の場所でも端末モードに一切触れないのと
  // 同じ)からの移植。終了ステータスを返す。子プロセスを起動でき
  // なかった、または正常終了しなかった場合は-1。
  static int runInteractive(const std::vector<std::string> &argv);

  // argvをバックグラウンドで起動する: 上の他の呼び出しと同様にfork
  // されexecされるが、完了を待つことも出力を捕捉することもせず
  // すぐに戻る――呼び出し側が(Application::runLocateUpdateDb()の
  // ように)run()のように同期的にブロックするのではなく、自分自身の
  // 入力ループと並行してポーリングし続けたい使い切りの保守コマンド
  // (例えば`updatedb`、実際のファイルシステム上では文字通り数分
  // かかりうる)のためのもの。標準出力/標準エラーは/dev/nullへ
  // リダイレクトされる(それらを読むために残しておくものは無く、
  // 呼び出し側が既に先へ進んでしまった後では、このクラス自身の
  // 捕捉済みResultという形は当てはまらない)。子プロセスのpidを
  // 返す。起動できなかった場合は-1。完了したら回収するために
  // hasExited()と組み合わせて使う。
  static pid_t runDetached(const std::vector<std::string> &argv);

  // runDetached()の子プロセスが既に終了しているかどうかの
  // ノンブロッキングチェック(waitpid(pid, ..., WNOHANG))――終了して
  // いると確認できた瞬間にそれを回収する(ゾンビ化を避ける)。既に
  // 回収済みの後を含め、繰り返し呼んでも安全(その場合waitpidは
  // ECHILDで失敗するが、これは呼び出し側が区別すべきエラーとしてで
  // はなく「終了済み」と同じ扱いにする――どちらにせよポーリングする
  // 対象は何も残っていないため)。
  static bool hasExited(pid_t pid);
};

}  // namespace fm
