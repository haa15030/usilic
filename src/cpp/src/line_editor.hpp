#pragma once

#include <string>
#include <vector>

#include "charset.hpp"
#include "display.hpp"

namespace fm {

// 単一行の編集可能なテキストフィールド: 挿入/削除/カーソル移動、
// kill/yank、クォート付き挿入、再描画。key.cのedit_line()/add_buff()
// と、その隣にあるUTF-8ヘルパー群(char_bytes_before/b2col/
// utf8_total_width/utf8_right_shift/need_scroll)、さらに描画には
// sub.cのprint_buff()から移植。
//
// これは逐語的な1:1の移植ではない。legacyのedit_line()は、全ての
// 呼び出し元(^U、^K、^Y、paste、backspace、delete-forward、^B/^F
// カーソル移動、add_buff自身の挿入経路――計9箇所の呼び出し元)に、
// 画面上の状態と同期を保つための3つのバイトオフセット追跡変数
// (col/cole/shift)をそれぞれ独立に再計算・直接変更させていた。この
// プロジェクトの以前の歴史で修正されたUTF-8バグのほとんど(K_e、
// K_u、K_y、paste、backspace、delete-forward、カーソル移動、TAB補完
// のスクロール、add_buff自身のインクリメントロジック)は、まさに
// その1つのパターン――同じバイトvsカラムの算術を9通りわずかに違う
// 形で再実装しており、それぞれが独立に間違いうる――に遡る。
//
// ここでは、その追跡状態は一切外に出さない。編集用の各メソッドは
// 常に(バッファの内容、カーソルのバイトオフセット)だけに触れる
// ――何が見えるか、カーソルが画面上のどこに来るかは、redraw()が
// 1箇所でゼロから完全に再計算する。これはlegacyが別々のまま(かつ
// 不整合なまま)にしていた2つの挙動も統一する: ^B/^Fはカーソルが端を
// 越えるたびにビューポートを最小限だけスクロールしていたのに対し、
// ^E/^U/^Y/paste/insertは、カーソルが元々どこにあったかに関わらず
// 常に新しく右揃えのビューへ飛んでいた。ここでのredraw()は常に
// 前者を行う(ビューポートを安定させたまま、カーソルを見える状態に
// 保つのに必要な最小限だけスクロールする)――意図的な、かつ厳密に
// より予測可能な挙動変更である。
//
// 意図的に除外(将来のクラスの仕事であり、LineEditorのものではない):
//   - input_key()(フィールドの種類ごとのEnterの意味、パス/チルダ
//     展開、TABで補完を起動する配線): Mode/Applicationの仕事。
//     LineEditorはテキストを編集するだけ。
//   - make_list()/make_cmd()(TAB補完候補の生成、ファイルシステム/
//     履歴の検索): 将来のPathCompleter的なクラスの仕事。
class LineEditor {
 public:
  LineEditor(Display &display, Charset::Encoding encoding);

  void setEncoding(Charset::Encoding encoding);

  // このフィールドが画面上のどこにあり、幅がいくつか(カラム数)。
  void setField(int row, int col0, int width);

  // -- 内容 --
  void setText(const std::string &text);
  const std::string &text() const;
  int cursorBytePos() const;

  // -- 編集 --
  // 生バイトを1個ずつ与える(KeyInput::readByte()から)。UTF-8の
  // 継続バイトは、完全な文字が揃うまで内部でバッファされる。legacyの
  // add_buff()と全く同じ――途中で切れたマルチバイトシーケンスの
  // 状態でredraw()(またはあらゆるDisplayへの書き込み)を呼ぶことが、
  // このプロジェクトのLineEditor時代における最初のUTF-8バグの原因
  // だった(CLAUDE.mdの作業ログ参照: 各バイトの値自体は正しいのに、
  // 端末が中断されたシーケンスを置換文字として描画してしまう)。
  void insertByte(unsigned char byte);
  void backspace();
  void deleteForward();
  void moveHome();
  void moveEnd();
  void moveLeft();
  void moveRight();
  // M-f/M-b(2026-09-26追加、emacs_tutorial.txtの単語移動): 単語の文字は
  // ASCIIの英数字と非ASCII(マルチバイト文字)、それ以外(空白や/ . -等の
  // 記号)が区切り。バイト単位で判定しても、マルチバイト文字の先頭・継続
  // バイトはどちらも0x80以上なので、文字の途中で止まることは無い。
  void moveWordLeft();
  void moveWordRight();
  // M-DEL/M-d: 上と同じ単語の範囲をkillバッファへ切り取る(^Yで戻せる)。
  void killWordBackward();
  void killWordForward();
  // C-_(2026-09-26追加): 直前の編集を取り消す。連続した文字入力は
  // Emacsと同じく20文字までを1回分にまとめる。取り消す物が無ければBEL。
  void undo();
  // 取り消しの履歴を捨てる。setText("")も同じことをする(新しい行)。
  void clearUndo();

  // 2026-09-11追加、直接のユーザー報告(docs/CHANGES.md項目439参照:
  // LOCATEプロンプトが前回クエリを事前入力するが、カーソルが末尾に
  // あるため、そのまま新しいクエリを打つと末尾へ追記されて
  // `.txt.txt`のような意図しない連結が生じ、0件になっていた)への
  // 対応。呼び出し元(activatePrompt())がsetText()で事前入力した
  // 直後にこれを呼ぶと、「次に来る最初の編集」を全文選択状態として
  // 扱う: 続く最初のinsertByte()/paste()/yank()はバッファ全体を
  // 事前入力テキストごと消してから新しい入力だけを挿入し(ブラウザの
  // アドレスバー等と同じ「選択済みテキストへの入力は置き換え」の
  // 慣習)、続く最初のbackspace()/deleteForward()も同様に全体を
  // 削除する。一方、moveHome()/moveEnd()/moveLeft()/moveRight()/
  // killToStart()/killToEnd()(=カーソル位置を明示的に操作する
  // 操作)はこの保留状態を、何も削除せずに解除するだけにする――
  // ユーザーが意図的に事前入力テキストを部分編集しようとしている
  // ことの表れなので、以後は通常の編集に戻る。
  void selectAll();

  // ^U: フィールド先頭からカーソルまでをkillバッファへ切り取る。
  void killToStart();
  // ^K: カーソルからフィールド末尾までをkillバッファへ切り取る。
  void killToEnd();
  // ^Y: killバッファの内容をカーソル位置へ再挿入する。
  void yank();
  // 2026-08-31、直接のユーザー依頼(COPY(^O^C)の確定内容を、^Y経由で
  // 汎用エリアから取り出せるようにしたい、emacsのkill-ring-save(M-w)
  // /yank(C-y)がこの同じkillバッファを共有するのに合わせて):
  // Application::feedCopyByte()がCopyMode::confirmMark()の結果を
  // ここへ書き込む――killToStart()/killToEnd()による通常の削除と
  // 全く同じ着地点であり、以後の^Yはどちらの由来かを区別しない。
  void setKillBuffer(const std::string &text);
  // M_paste: 外部のクリップボードテキストをカーソル位置へ挿入する
  // (^U/^Kのkillバッファには触れない――legacyの`copy`パラメータと
  // その静的な`yank[]`の関係と同じく、別の入力源として移植)。
  void paste(const std::string &text);

  // ^Q: insertByte()へ渡される次の1バイトは、このクラスを呼んでいる
  // 側による編集コマンドとして解釈されるのではなく、そのまま
  // リテラルに挿入される(制御バイトであればredraw()が反転表示で
  // 描画する)(そのディスパッチ自体はMode/Applicationの仕事――この
  // クラスは、そのトグルが起きたことを覚えているだけ)。
  void toggleQuotedInsert();
  bool quotedInsertPending() const;
  // 呼び出し側がそのフラグを消費した(続くバイトをinsertByte()へ
  // 与えた)後、それをクリアする。toggleQuotedInsert()とは別にして
  // あるのは、呼び出し側が「オフにするには2回トグルする」と考え
  // なくて済むようにするため。
  void clearQuotedInsert();

  // -- 描画 --
  // 表示ウィンドウとカーソル位置をゼロから再計算し、フィールドを
  // 再描画する。何か編集した後、または強制的に再描画したい場合
  // (例えば外部から画面がクリアされた後)に呼ぶ。
  void redraw();
  // 2026-11-06、直接のユーザー報告(mpr、=ob.png、最初の起動のSHELLで"Password:jjjj"): trueの間、redraw()は字を
  // 描かない(打った字は持ったまま、カーソルは欄の先頭)。SHELLで動いているコマンドが端末のエコーを切っている間
  // (sudoのパスワード等、Shell::inputEchoOff())に使う。
  void setConcealed(bool on) { concealed_ = on; }
  bool concealed() const { return concealed_; }

 private:
  // legacyのCにおけるN_line(1024)だった。2026-09-27、64KBへ拡大――直接の
  // ユーザー報告("~/.usilic/data/の._ファイル群を削除しようとするとSHELLで
  // 止まる"): CP/MV/RM(やARC展開、^J^W)は組み立てたコマンドをSHELLの
  // コマンドライン(このクラス)へsetText()してから送信するので、長い
  // ファイル名を複数選ぶと1023バイト目以降が黙って切り捨てられ、閉じて
  // いない引用符のままbashへ届いて">"の継続プロンプトで止まっていた。
  static constexpr int kMaxLength = 65536;

  // selfInsertは1文字ずつの入力(insertByte())――undoのまとまりに使う。
  void insertText(const std::string &bytes, bool selfInsert = false);
  void pushUndo();
  int wordLeftPos() const;
  int wordRightPos() const;
  int charBytesBefore(int pos) const;  // posで「終わる」文字のバイト数
  int charBytesAt(int pos) const;      // posで「始まる」文字のバイト数
  int byteToColumn(int shift, int bytePos) const;
  int totalWidth(const std::string &text) const;
  int rightShift(const std::string &text, int fieldWidth) const;
  void adjustViewport();
  void renderVisible(const std::string &visible, int startBytePos);

  Display &display_;
  Charset::Encoding encoding_;

  std::string buffer_;
  bool concealed_ = false;  // setConcealed()参照
  int cursorPos_ = 0;   // buffer_へのバイトオフセット
  int shift_ = 0;       // 表示されている最も左のバイトのオフセット

  int row_ = 0, col0_ = 0, width_ = 0;

  std::string killBuffer_;
  bool quotedInsertPending_ = false;
  bool selectAllPending_ = false;

  struct UndoState {
    std::string buffer;
    int cursorPos;
  };
  static constexpr int kMaxUndo = 100;
  static constexpr int kUndoInsertGroup = 20;  // Emacsの「文字の挿入は20個ずつ」
  std::vector<UndoState> undoStack_;
  int undoInsertRun_ = 0;  // 直前の記録から続いている文字入力の数(0=まとまり無し)

  // insertByte()用のUTF-8部分シーケンス累積バッファ、複数回の呼び出しをまたぐ。
  std::string pendingUtf8_;
};

}  // namespace fm
