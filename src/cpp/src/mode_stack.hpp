#pragma once

#include <cstddef>
#include <deque>
#include <memory>
#include <optional>
#include <string>

namespace fm {

// 保存されたナビゲーションフレーム: ユーザーが降りていった先の画面
// (ディレクトリ一覧、アーカイブのエントリ、CUT/man/prog/listへの
// 寄り道、またはネストしたgrep結果検索)から戻るために必要な全て。
// legacyのSTACK構造体とGREP構造体(usilic.h)からの移植で、1つの
// ペイロード型へ統合してある――理由はModeStackのクラスコメント参照。
//
// legacy(usilic.hのSTACK/LINE/CUT/GREP)へのフィールドごとの対応:
//   mode              <- STACK.f.M / GREP.f_M(不透明なモード識別子。
//                         このクラス自体はこれに何の意味も与えない
//                         ――解釈するのはMode/Applicationの仕事)
//   previousMarker    <- LINE::prev(1段さらに戻った位置のモード
//                         ラインのパンくず。ModeStack::kFloorMarker
//                         はlegacyの「これより上には何も無い」という
//                         番兵、pushFloor()参照)
//   name              <- STACK::name / GREP::name(現在の項目の
//                         パス/ラベル)
//   directory         <- LINE::dir
//   previousDirectory <- LINE::old
//   modeLineArg       <- LINE::arg
//   cutName           <- CUT::name
//   cutSearchText     <- CUT::str
//   cutBuffer         <- CUT::buff(不透明な、型消去されたハンドル
//                         ――バッファの所有権についてはクラス
//                         コメント参照)
//   cutProgram        <- CUT::chp、ただし共有ポインタではなく素の
//                         プログラム名文字列へ解決済み。この同じ
//                         legacyフィールドについてのHistory::
//                         SessionStateの全く同じ簡略化と同じ
//                         (Configuration経由でそれを再解決するのは
//                         いずれにせよ呼び出し側の仕事)
//   view              <- STACK::bp / GREP::bp(アクティブだったバッファ
//                         /ビューが何であれそれへの、不透明な、型
//                         消去されたハンドル)
//   searchPattern     <- GREP::str、legacyのGREPがSTACKには無かった
//                         「唯一の」フィールド――通常のナビゲーション
//                         フレームではnullopt、このフレームが「この
//                         パターンでgrep結果を見ていた」ことを表す
//                         場合にのみ設定される
struct ModeStackFrame {
  int mode = 0;
  std::string previousMarker;
  std::string name;
  std::string directory;
  std::string previousDirectory;
  std::string modeLineArg;
  std::string cutName;
  std::string cutSearchText;
  std::shared_ptr<void> cutBuffer;
  std::string cutProgram;
  std::shared_ptr<void> view;
  std::optional<std::string> searchPattern;
};

// ナビゲーションフレームのLIFOスタック――ユーザーが降りていった先
// (サブディレクトリ、アーカイブのエントリ、CUT/man/prog/listへの
// 寄り道、ネストしたgrep結果)から後退する時に「何へ戻るか」。
// sub.cのsave_stack()/load_stack()/save_stack_dir()/read_stack()
// (STACKの仕組み)とsave_grep()/load_grep()(GREPの仕組み)からの
// 移植。
//
// これは逐語的な移植ではない。2つの最大の変更点:
//   - legacyには2本の並行するLIFOチェーン(STACKとGREP)があり、
//     grepに入る瞬間(mode.cのmode()、case M_egrep/M_fgrep)に、
//     両者を同期させ続けるための「STACKフレームの中身を奪って新しい
//     GREPノードの種にし、そのコピーをSTACKへ戻す」という手作りの
//     一連の動作がある。両方の構造体(usilic.h)を並べて読むと、GREPの
//     ペイロードはSTACKのペイロードに、フィールドが1つ(検索パターン)
//     加わっただけのものであることが分かる――このクラスを設計する
//     過程で独立に確認済みで、このプロジェクト自身の以前のCRC
//     カード設計セッションが既に到達していた結論(CLAUDE.mdの作業
//     ログ18番: 「GREPは実質STACKの特殊ケース」)とも一致する。
//     ここでは、フレーム型が省略可能な検索パターンフィールドを持つ
//     1本のスタックへ統合し、2本目のチェーンとフレームを奪う
//     トリックを完全に排除している――呼び出し側は、grep結果に入る
//     時にsearchPatternを設定したフレームをpushするだけで、他の
//     どのpushとも変わらない。
//   - legacyのバッファ寿命管理(STACK::f.bp、「このフレームのbpは
//     共有されるhp->base[12]の再利用可能スロットプールを指しており、
//     popする時にfree()してはならない」対「このbpはまさにこの1回の
//     寄り道のためだけにcalloc()されたものであり、「free()すべき」」
//     を意味する手作業で立てるビット)は、ModeStackFrame::view/
//     cutBufferに対する通常のstd::shared_ptr<void>の参照カウントに
//     置き換えられている。「プールのスロット、実際には決して破棄
//     されない」という意味を持たせたい呼び出し側は、そのスロットを
//     何もしないデリータ付きのshared_ptrで包む。「使い切り、参照
//     されなくなったら解放」という意味を持たせたい呼び出し側は、
//     通常の所有権を持つshared_ptrを使う。どちらの場合でも、
//     ModeStack自体はどちらのケースを保持しているか知る必要も、
//     手動でfree()するかしないか決める必要も無い――手作業で追跡する
//     所有権フラグが実態と食い違ってしまうという、このプロジェクトが
//     他の場所で繰り返し発見・修正してきたのとまさに同種のバグが、
//     構造的にここでは起こりえない。
//
// 意図的に除外:
//   - 行/列/スクロールのカーソル位置: このクラスの仕事ではないし、
//     legacyのSTACK/GREPの仕事でも一度も無かった――両方の構造体を
//     読んで確認済み、どちらもPN位置を全く持たない。それはLLの仕事
//     であり、LLは既に移植済みのHistoryクラス(positionFor()/
//     setPosition())で完全にカバーされている――ギャップも重複も無い。
//   - ディスクへの永続化: Historyとは異なり、legacyのSTACK/GREPは
//     純粋にメモリ上/セッションごとのものである(確認済み:
//     read_list()/write_list()はどちらも一切参照しない)。ModeStack
//     には自前のファイルI/Oは無い。
//   - clear_stack()の、停止条件付きの一括巻き戻しロジック(mode.c):
//     その6つの停止条件は、この C++移植にはまだどこにも存在しない
//     具体的なモード定数やUIの概念(M_fgrep/M_egrepのprevマーカー
//     チェック、T_pipe2の名前チェック、特定のヘルプバッファプール
//     スロット)を参照しており、Mode/Applicationが存在してそれらに
//     意味を与えるようになる前に今それらを組み込むのは、単なる憶測に
//     なってしまう。ModeStackは、そのような一括巻き戻しが必要とする
//     プリミティブ(push/pop/peek/find)を公開する。「これこれが真で
//     ある間popし続ける」という方針は、KeyInputが生のキーを、
//     あるモードがそれで何をするか決めずに報告するのと同じように、
//     Mode/Applicationがその上に構築する仕事である。
//   - push/popによって引き起こされる実際のモードディスパッチ/画面
//     描画ロジック(`^G`が何をするか、何が再描画されるか):
//     Mode/Applicationの仕事。ModeStackはフレームを覚えているだけ。
class ModeStack {
 public:
  // legacyの「これより上には何も無い」という番兵(save_stack_dir()の
  // リテラルなK_z直後にNULという文字列)。呼び出し側/テストがその
  // マジックバイトを知らなくて済むよう、名前付き定数として公開して
  // ある。あるフレームのpreviousMarkerがこれと厳密に等しい時、それが
  // floorフレームである――isFloor()参照。
  static const std::string kFloorMarker;

  void push(ModeStackFrame frame);

  // 先頭(最も最近pushされた)フレームを取り除いて返す。スタックが
  // 空ならnullopt。load_stack()から、そのバッファ所有権の管理
  // (クラスコメント参照)とGREPチェーンの協調処理(もはや不要、
  // 上記参照)を除いたものからの移植。
  std::optional<ModeStackFrame> pop();

  // 先頭フレームを取り除かずに返す。空ならnullptr。
  const ModeStackFrame *top() const;

  bool empty() const;
  std::size_t size() const;

  // セッション開始時にスタックの底を確立する――save_stack_dir()
  // からの移植。まず既存のフレームを全てクリアする(新しい底は常に
  // 新しいスタックを開始する、init_menu()でのlegacyの一回限りの
  // 使用と同じ)。
  void pushFloor(const std::string &directory, const std::string &previousDirectory,
                 std::shared_ptr<void> view, int mode);

  bool isFloor(const ModeStackFrame &frame) const;

  // 読み取り専用の存在チェック――read_stack()からの移植、legacyは
  // これを純粋に「これこれのフレームが既にスタックのどこかにあるか」
  // に答えるためだけに使っていた(例えば重複をpushする代わりに既存の
  // フレームへジャンプするため)。底(最も古い)フレームから走査する、
  // legacy自身の走査方向と同じ。最初に一致したものかnullptrを返す。
  // viewPtrは値ではなく同一性で比較される(frame.view.get() ==
  // viewPtr)。
  const ModeStackFrame *findByView(const void *viewPtr) const;
  const ModeStackFrame *findByName(const std::string &name) const;

 private:
  std::deque<ModeStackFrame> frames_;  // back()がスタックの先頭
};

}  // namespace fm
