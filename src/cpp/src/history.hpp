#pragma once

#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "charset.hpp"

namespace fm {

// 名前付きのカーソル位置記憶カテゴリ――LLのfile/dir/man/modeフィールド
// (tcb.h)からの移植。「Screen」はlegacyのモードをキーとした雑多な
// 受け皿(パス名で自然にキーできないあらゆる画面――LOCATE/EGREP結果、
// DU、PROGRAM/MANメニュー自体、split-bufferの識別、等)であり、
// 文字通りの表示「mode」だけを指すものではない。
enum class PositionCategory { File, Directory, ManPage, Screen };

// 単純な、最初の出現が勝つ名前集合――LLのgrep/cmdフィールドが
// read_list()/write_list()で具体的に(履歴ファイルのCUT章とCOMMAND章)
// 使われている形からの移植。Configuration自身のGREP/PROGRAMシード
// リストとは意図的に別物――そちらは、legacyでは同じLIST/STRという
// C構造体をたまたま再利用しているだけの、別の・編集可能な機能であり、
// 履歴ファイルへ書き込まれることは無い――このクラスがなぜ両者を
// 統一しようとしないかについてはクラスコメント参照。
enum class NameSetCategory {
  CutPattern,        // " CUT"章: CUTモードの抽出に使われたパターン
  CommandModeLabel,  // " COMMAND"章: COMMANDモードの画面ラベル
};

// 「最近訪れたN件」のリング履歴――SSのhist/hfile/hdir/hman/hlocate/
// hfgrep/hegrepフィールドからの移植。
enum class RingCategory {
  ShellCommand,        // " HISTORY": 実行したシェルコマンド/cd/PROGRAMモードのコマンド
  RecentFile,          // " FILE_HISTORY"
  RecentDirectory,     // " DIR_HISTORY"
  RecentManPage,       // " MAN_HISTORY"
  RecentLocateQuery,   // " LOCATE_HISTORY"
  RecentFgrepPattern,  // " FGREP_HISTORY"
  RecentEgrepPattern,  // " EGREP_HISTORY"
};

// カーソル位置の記憶、最近使ったものの一覧、いくつかのセッション
// レベルの設定を、実行をまたいで永続化する。sub.cのread_list()/
// write_list()(~/.usilic/historyファイル形式)と、それらが
// save_list()/load_list()/save_str()/save_history()/free_history()
// 経由で組み立てるLIST(二分探索木)/STR(連結リスト)の木からの移植。
//
// これは逐語的な移植ではない。いくつかの構造上の変更点:
//   - ここではファイルI/Oを行わない、Configurationのloadrcの分離
//     (loadRcText())と同じ: loadHistoryText()/serialize()は、既に
//     読み込み済み/これから書き込むテキストを受け取る/返すだけ。
//     ファイルの場所を特定すること、`.old`バックアップしてから
//     上書きするという一連の動作、主ファイルが切り詰められている/
//     壊れているように見える場合に`.old`へフォールバックすること
//     は、呼び出し側(将来のApplicationクラス)の仕事――これにより、
//     本物の$HOME無しでもパース自体をユニットテストできる状態を
//     保っている。loadHistoryText()は、テキストが(下のisComplete()
//     参照)期待されるEOF番兵で終わっているかどうかは引き続き報告
//     するので、呼び出し側は`.old`へのフォールバックが妥当かどうか
//     判断でき、このクラスがファイルシステムに触れる必要無く
//     legacyの破損シグナルを保っている。
//   - LISTの二分探索木とSTRの単方向連結リストは、実際にはそれぞれ
//     単に文字列をキーとするマップと、順序付き文字列列に過ぎない
//     (sub.c参照: load_list()はソート順を使って枝刈りするのではなく
//     「両方の」子を無条件に走査しており、木の見た目をしたO(n)の
//     走査になっている。このファイルでのsave_str()の「木」の使い方は
//     常に退化しており、名前だけのもの)。std::map<std::string,
//     CursorPosition>とstd::vector<std::string>/
//     std::deque<std::string>へ置き換えてある。ContentViewのPP
//     連結リストやArchiveEntryのフィールドに対して行ったのと同じ
//     「手作りのデータ構造を、同じ仕事をする標準のものへ置き換える」
//     という選択と同じ。
//   - F6/F10風の番兵ポインタによる呼び出し規約(legacyの
//     save_history(F6, ...)が、本物のSTR*パラメータをマジックな
//     (void*)-6でオーバーロードし「実はグローバルな保持上限の方を
//     設定せよ」を意味させていたもの)は無い。保持数は素の
//     コンストラクタパラメータであり、呼び出し側が
//     Configuration::historyRetentionCount()から取得する。
//   - 保持数は直接、一貫して適用される。legacyのCで新たに見つかった
//     実際のバグは、意図的にここでは再現していない: save_history()の
//     F6分岐には`if (hist == F6)`という同一の条件が2つ連続しており、
//     2つ目は到達不能なデッドコードである(1つ目が常に勝つ)。
//     init_menu()は、設定済みの保持数を「読み戻す」ためにその2つ目の
//     分岐に依存しているが、それが到達不能なため、読み戻しは常に
//     ハードコードされた既定値200を返していた――そのため、usilicrc
//     内のカスタムな`HISTORY`値は、legacyバイナリを実行するたびに
//     毎回静かに捨てられ200へリセットされていた。Configurationが
//     既にこのクラスへきれいな整数を直接渡すので、間違えようのある
//     同等の往復処理は存在しない。
//   - 文字コードの復元優先順位は、隠れたenv_charset()グローバル
//     チェックではなく明示的である。loadHistoryText()は
//     environmentCharsetExplicitlySetを素のパラメータとして受け取る
//     (呼び出し側がConfiguration::charsetExplicitlySet()を渡す)。
//     それ自体がそのグローバルへ手を伸ばすのではない――これは
//     CLAUDE.mdの作業ログ13番/3番ステップ6の修正の読み込み側半分
//     であり、Configuration自身の移植は既に「USILICCHARSETが明示的に
//     exportされていたか」という半分をカバーしている。
//
// 意図的に除外(他のクラスの仕事):
//   - これらのいずれかを画面上の「最近使ったファイル/ディレクトリ
//     /...」メニューとして描画すること: 将来のBuffer/ContentView
//     描画の関心事であり、永続化の話ではない。
//   - Configuration自身の編集可能なGREP/PROGRAMシードリスト
//     (legacyのrprog/rgrepというSSフィールドと、それらを再構築する
//     メニューからの削除の仕組み): sub.cを直接読んで確認済みだが、
//     この2つのフィールドはread_list()/write_list()には全く現れ
//     ない――基盤となるC構造体はlegacyでは共有されていても、これは
//     このクラスの関心事ではない。
//   - 履歴エントリを生成する実際のプログラム/シェルの実行
//     (ChildProcess/Shell/将来のMode): このクラスは、それらの
//     サブシステムからの呼び出し経由で、何かが起きたことを記録
//     するだけ。
//   - プログラム名の文字列を実際に実行可能なコマンドへ再解決する
//     こと: SessionState::pendingCutProgramは素の文字列として
//     保存される(legacy: `lp->cut.chp->argv[0]`、読み込み時に
//     get_child()経由で、保存時にcheck_ext()経由で再解決される)
//     ――Configuration経由でそれを解決するのは呼び出し側の仕事で
//     あり、ChildProcess/Shellも自分自身ではConfigurationへ手を
//     伸ばさないのと同じように、HistoryをConfigurationから切り離した
//     ままにしている。
class History {
 public:
  struct CursorPosition {
    int row = 0;
    int totalRows = 0;
    unsigned top = 0;
    int lineNumber = -1;
  };

  // 「現在のバッファ」行: 前回のセッションがどこで終わったか。
  // フィールド名はlegacyのLINE構造体(dir/old/arg/prev)にCUTモード
  // バッファを加えたものをそのまま踏襲している。これ以上改名して
  // いないのは、既に十分に自己説明的であり、このプロジェクト自身の
  // LINEに隣接するコード(Shell等)も他にこの構造体には触れないため。
  struct SessionState {
    std::string previousDirectory;    // LINE::old
    std::string currentDirectory;     // LINE::dir
    std::string modeLineArg;          // LINE::arg
    std::string previousScreenMarker; // LINE::prev
    std::string lastOpenName;         // セッション終了時に閲覧していた名前
    std::string pendingCutBuffer;     // CUT::str -- 進行中のCUTモード抽出テキスト
    std::string pendingCutProgram;    // CUT::chpのプログラム名、未解決(クラスコメント参照)
  };

  explicit History(int retentionCount = 200);

  // -- カーソル位置の記憶 --
  std::optional<CursorPosition> positionFor(PositionCategory category, const std::string &name) const;
  void setPosition(PositionCategory category, const std::string &name, CursorPosition position);

  // -- プレーンな名前の集合(先勝ち、無制限、挿入順) --
  bool containsName(NameSetCategory category, const std::string &name) const;
  void rememberName(NameSetCategory category, const std::string &name);
  const std::vector<std::string> &namesIn(NameSetCategory category) const;

  // -- リング履歴 --
  // nameをpushし、リングが既に保持数に達していれば最も古い1件を
  // 追い出す。抑制するのは、直近にpushされた1件との「直接の」重複
  // (ring.back()==name)だけである――find_hist()自身の走査(リスト
  // 全体ではなく「最後の」ノードとだけ比較する)から正確に移植して
  // ある(2026-08-04の訂正: このコメント/実装の以前のバージョンは
  // リング全体での最初の出現が勝つ重複排除だと主張していたが、それは
  // 誤った移植だった)。(直前の1件ではない)より古いエントリを再訪
  // すると、本当に重複ごと再度追加される――例えばA、B、Aは生の
  // リング[A, B, A]になる。これは意図的な設計である: このリングは
  // 生の、時系列のアクセスログ("HISTORY"――Application::
  // toggleListBrowserHistory()自身のコメント参照)そのものである。
  // ソート済みで重複排除された「重複の無い最近の項目」ビューを
  // 望む呼び出し側(例えばApplication::startListBrowser()の
  // FILE/DIRピッカー)は、表示時にrecentEntries()に対して自分で
  // 重複排除を行う。
  void pushRecent(RingCategory category, const std::string &name);
  const std::deque<std::string> &recentEntries(RingCategory category) const;  // 古い順..新しい順
  int retentionCount() const;

  // -- セッションレベルのトレーラ設定 --
  bool smoothScrollEnabled() const;
  void setSmoothScrollEnabled(bool enabled);
  int lsMode() const;
  void setLsMode(int mode);
  // 未設定(新規の履歴)の場合、または読み込み時に環境の文字コードが
  // 優先された場合(loadHistoryText()参照)はnullopt――呼び出し側は
  // どちらの場合でも、Eucへではなく自分自身の文書化された既定値
  // (Utf8)へフォールバックすべき。CLAUDE.mdの作業ログ13番についての
  // クラスコメント参照。
  std::optional<Charset::Encoding> savedCharset() const;
  void setSavedCharset(Charset::Encoding encoding);

  // GUIウィンドウの状態(2026-08-24、docs/CHANGES.md 427番)――実際の
  // ユーザーの質問(「前回GUIウィンドウ情報を記憶してもあまり役に
  // 立たないと思う...その場での取得が可能ならusilicrcへの設定を行う
  // （終了時にでも）」)、項目425/426(ライブなドラッグリサイズと
  // Cmd+=/Cmd+-/Cmd+Kによるライブなフォント/色調整)の続きとして。
  // GraphicalDisplay::Colorが示唆するような素のgetter構造体には
  // 意図的にしていない――このクラスは既に特定のDisplayバックエンド
  // への依存を避けている(GraphicalDisplayはSDL2固有、Historyは
  // そうではない)ので、GuiPreferencesはGraphicalDisplay::Colorの
  // 代わりに素のint/unsigned charを使う、それ自身の小さなPOD型で
  // ある。main.cppが、他の場所で既にConfiguration/Display型を変換
  // しているのと同じように、境界で(GraphicalDisplay::Color <->
  // GuiPreferences自身のfgR/fgG/fgB)変換する。0/false値のフィールド
  // は「まだ何も保存されていない」(新規の履歴、またはこの項目より
  // 前に書かれた古い履歴ファイル)を意味する――呼び出し側は、
  // savedCharset()のnulloptが文字コードについて既に行っているのと
  // 全く同じように、GraphicalDisplay::create()自身のコンパイル時
  // 既定値へフォールバックする。
  struct GuiPreferences {
    int fontPointSize = 0;      // 0 == 未設定
    int rows = 0, columns = 0;  // 0 == 未設定(常に両方設定済みか両方未設定のどちらか)
    bool hasColors = false;
    unsigned char fgR = 0, fgG = 0, fgB = 0, bgR = 0, bgG = 0, bgB = 0;
  };
  GuiPreferences guiPreferences() const;
  void setGuiPreferences(const GuiPreferences &preferences);

  const SessionState &sessionState() const;
  void setSessionState(SessionState state);

  // -- シリアライズ --
  // environmentCharsetExplicitlySet: Configuration::
  // charsetExplicitlySet()を渡す――trueの場合、トレーラの保存済み
  // 文字コードはパースされるが適用されない(savedCharset()はnullopt
  // のまま)。legacyの`if (!env_charset(-1)) vt_charset(...)`という
  // 復元優先順位チェックと同じ。
  bool loadHistoryText(const std::string &text, bool environmentCharsetExplicitlySet);
  // loadHistoryText()が、legacyのread_list()がファイルを信頼する前に
  // チェックする末尾のEOF番兵(その`fseek(fp,-8,2); ...
  // strchr(str, K_z)`チェックからの移植)を見た時点でtrueになる――
  // 失敗した/部分的な読み込みの後はfalseになり、呼び出し側に
  // `.old`バックアップへのフォールバックが妥当かもしれないと知らせる。
  bool isComplete() const;
  const std::vector<std::string> &errors() const;

  std::string serialize(const std::string &programName, const std::string &timestampText) const;

 private:
  std::map<PositionCategory, std::map<std::string, CursorPosition>> positions_;
  std::map<NameSetCategory, std::vector<std::string>> nameSets_;
  std::map<RingCategory, std::deque<std::string>> rings_;

  int retentionCount_;
  bool smoothScrollEnabled_ = false;
  int lsMode_ = 0;
  GuiPreferences guiPreferences_;
  std::optional<Charset::Encoding> savedCharset_;
  SessionState sessionState_;

  bool complete_ = false;
  std::vector<std::string> errors_;
};

}  // namespace fm
