#pragma once

#include <functional>
#include <optional>
#include <string>

#include "display.hpp"

namespace fm {

// 生のキーボード入力: 制御端末からバイトを読む、raw/cooked termio
// モードの切り替え、特殊キー(矢印/ページアップ/ダウン)のための
// エスケープシーケンス認識、端末サイズ変更(SIGWINCH)とジョブ
// コントロール(SIGTTOU/SIGCONT)のシグナル処理、生バイトマクロの
// 再生。key.cのread_m()/read_esc()と、vt.cのvt_getch()/vt_init()
// (vt_init()のraw termio半分。代替スクリーン/属性半分はDisplayに
// 属する、display.hpp参照)からの移植。
//
// 意図的に除外(将来のクラスの仕事であり、KeyInputのものではない):
//   - SIGSEGV(クラッシュ回復)/SIGTERM/SIGHUP(セッションのクリーン
//     アップ): キーボード入力ではなくApplicationのライフサイクル。
//     legacyのsave_signal()は、これら全てとKeyInputのシグナルを
//     1つの関数の中でまとめて設定していた。その責務はここで分割
//     されている。
//   - 生のキーまたは解決済みのSpecialKeyを、モードのコマンド番号
//     へマッピングすること(legacyのread_m()内のM_*/T_*ディスパッチ):
//     Mode/Applicationの仕事。KeyInputは何が押されたかを報告する
//     だけ。
//   - edit_line()/make_list()(コマンドライン用テキスト編集
//     ウィジェット): LineEditorの仕事。
//   - stand_str()(ファイル名ハイライト描画): ContentView/Displayの
//     仕事。
//   - 記録されたコマンドのキーボードマクロ(legacyの`p_km`、M_ms/
//     M_mm/M_me経由で記録される): legacyのコードを追うと、これらは
//     生のキーバイトではなく「解決済みのM_*/T_*コマンド番号」を
//     記録・再生していることが分かる――再生は再ディスパッチを完全に
//     バイパスし、解決済みの値をread_m()の呼び出し元へ直接手渡す。
//     それはMode/Applicationレベルの関心事(ディスパッチャが決定した
//     ことを記録する)であり、バイトしか見ないKeyInputが意味のある
//     形で所有できるものではない。ここで実際に移植されているのは、
//     もう一方の、本当に異なる仕組みである: usilicrcのあらかじめ
//     用意されたM-0..M-19マクロ(legacyの`p_bm`)で、これは実際の
//     キー入力が使うのと同じ低レベルの読み込み経路へ生バイトとして
//     戻して再生される――playRawMacro()参照。
class KeyInput {
 public:
  // Shift*(2026-10-03、EPUBの章送り): Shift+矢印(端末は"\x1b[1;2A"等、-gはKMOD_SHIFT)。
  enum class SpecialKey { Up, Down, Right, Left, PageUp, PageDown, ShiftUp, ShiftDown, ShiftRight, ShiftLeft };

  explicit KeyInput(Display &display);
  virtual ~KeyInput();

  // open()が開いた制御端末のfd、開いていなければ-1――他のfd
  // (例えばShellセッション自身のpty)と一緒にこれを多重化する必要が
  // ある呼び出し側自身のpoll()/select()ループのためのもの。
  // ChildProcess/Shellが、ブロッキング読み込みだけを提供するのでは
  // なく既に自前のfdを公開しているのと同じ理由。readByte()自体は、
  // プラットフォームに関わらず常にこのfdから正しく読み込む。
  //
  // 実際の実行ループ(app/main.cpp)を構築中に見つかった注意点:
  // このfd(open("/dev/tty", O_RDONLY)経由で開いたもの)をそのまま
  // poll()すると、fdが実際には開いておりそこからの読み込みも問題
  // なく動作するにもかかわらず、macOS/Darwinでは無条件にPOLLNVALが
  // 報告される――実際のpty相手の分離テストで確認済みで、Linuxでは
  // 発生せず正しくpollされることも確認済み。stdinが同じ制御端末で
  // あると分かっている場合(本物の対話的セッションでは常にそうで
  // あり、これが問題になるのはその文脈だけ)、Darwin上でこのfdを
  // pollする呼び出し側は代わりにSTDIN_FILENOをpollすべきである
  // ――実際にどこでこれが回避されているかについてはapp/main.cpp
  // 自身のコメント参照。
  int fd() const;

  // -- 生バイトの読み込み --
  // 生バイトを1個読む。playRawMacro()でキューされたシーケンスが
  // アクティブならそこから、そうでなければ制御端末から
  // (open()/setPolling()で設定されたVTIMEタイムアウトまでブロック
  // する)。タイムアウト/EOF/エラー時は0を返す。端末から読まれた
  // リテラルなNULバイト(多くの端末でのCtrl-Space)は、legacyの
  // vt_getch()のK_at番兵と同じく0x80として報告される――呼び出し側は
  // 既に、legacyの呼び出し側が行っていたのと全く同じようにこれを
  // 特別扱いする必要があり、これは新たな曖昧さではない。
  //
  // 本物の端末より先にplayRawMacro()のキューを確認する――これは
  // また、テストが本物の端末無しにreadEscapeSequence()の実際の
  // マッチングロジックをエンドツーエンドで検証する方法でもある
  // (playRawMacro()でバイトをキューするだけで、偽の端末やサブ
  // クラスは不要)。これは仮想関数でもあり、呼び出し側が必要なら
  // 全く別のバイト源に差し替えられる。(legacyは、自身の無関係な
  // キーボードマクロ再生機能のために「バイト源を差し替える」という
  // 問題を、2引数の関数を0引数の関数ポインタ型へキャストし、未使用の
  // 引数レジスタに残っていたゴミ値に頼るという方法で解決していた。
  // それは未定義動作であり、実際に、以前修正済みのセグフォルトを
  // 引き起こした。CLAUDE.mdの作業ログ9番参照。playRawMacro()も
  // これが仮想関数であることも、その危険を再現しない。)
  virtual int readByte();

  // -- raw/cookedモード、ttyのライフサイクル --
  // ttyPathを開き、raw modeへ切り替える(ICANON/ECHO/ISIG無し、
  // VMIN=0、VTIME=1デシ秒)。vt_init(1)のtermios半分からの移植。
  void open(const std::string &ttyPath);
  // ttyをopen()以前のモードへ戻し、閉じる。vt_init(-1)のtermios
  // 半分からの移植。
  void close();
  // ttyをopen()以前のモードへ戻すが、開いたままにする――一時的に
  // raw modeから抜け(例えば外部プログラムを実行するため)、後で
  // reenterRawMode()経由で戻るためのもの。vt_init(-2)のtermios
  // 半分からの移植。
  void leaveRawMode();
  // SIGCONT(ジョブコントロールによる再開)の後、またはleaveRawMode()
  // の後にraw modeを再適用する。vt_init(-9)/(3)からの移植。
  void reenterRawMode();
  // 読み込みタイムアウトを、ポーリング(VTIME=1、既定)とブロッキング
  // (VTIME=0、マクロをフル速度で再生する間に使う)の間で切り替える。
  // vt_init(3)/(-3)からの移植。
  void setPolling(bool polling);

  // -- エスケープシーケンス認識 --
  // readByte()がESC(0x1b)を返した直後に呼ぶ。Displayの6つのキー
  // シーケンスのどれか1つを識別するのに必要な、それ以降のバイトを
  // 読んで消費し、どれが一致したかを返す。ESCの後に認識可能な
  // シーケンスが続かなかった場合(何も続かない裸のESCも含む)は
  // nulloptを返す。read_esc()からの移植だが、1点意図的に修正して
  // ある: legacyのis_esc()は、何も一致しなかった場合とUp(インデックス
  // 0)が一致した場合の両方で素のint値0を返し、呼び出し側にはその
  // 区別がつかなかった――key_upという能力を定義しているあらゆる
  // 端末(事実上全て)で、認識されないエスケープシーケンスが静かに
  // Upとして扱われていた。std::optionalは、構造として「一致無し」と
  // 「Upに一致」を別物にする。
  std::optional<SpecialKey> readEscapeSequence();
  bool windowsPolling_ = true;  // setPolling()のWindowsのネイティブの分(コンソールを待つか)

  // 直近のreadEscapeSequence()呼び出しがESCの後に読んだバイト
  // (ESC自体は含まない)、既知のシーケンスに一致したかどうかに
  // 関わらず――何も続かない裸のESCに対する空文字列も含む。これは
  // Application自身のクラスコメントが以前文書化していたギャップの
  // 修正である: readEscapeSequence()はDisplayの既知の端末機能
  // シーケンスを消費・照合するために存在するが、呼び出し側が、
  // 自分「自身」のアプリケーションレベルのESCリーダーバインディング
  // (例えばusilicのSHELL用^[s、^[自体はDisplayのKeyでは全く無い)
  // と照合するために一致しなかった末尾バイトを回収する手段が無
  // かった。それを行う呼び出し側は、今ではreadEscapeSequence()の
  // nullopt返り値を読み、諦める前にlastEscapeBytes()で自分自身の
  // バインディングを確認する。
  //
  // これでも取り繕われていない、受け継がれた1つの癖: readEscapeSequence()
  // は、ESCの後「固定」数のバイトを読む(Up自身のキーシーケンス長、
  // 意図的に保持されているread_esc()の癖――自身のコメント参照)。
  // 「実際に押されたシーケンスが必要とする分だけ」ではない。^[sの
  // ような本物の1バイトバインディングの場合、これは、ユーザーが
  // 十分速く入力して本物の次のキー入力が同じVTIMEウィンドウ内に
  // 届いた場合、その固定長の読み込みが、's'と一緒にその2バイト目
  // としてそのキー入力も捕捉してしまいうることを意味する――
  // (この移植のApplicationがそうしているように)単一の先頭バイトと
  // 照合する呼び出し側には影響しないが、無関係な2バイト目が時折
  // 便乗してくることがあると承知しておくべきである。
  const std::string &lastEscapeBytes() const;

  // -- シグナル --
  // このインスタンス(アクティブなKeyInputは1つだけ――usilicは
  // フォアグラウンドの入力ハンドラを常に一度に1つしか動かしたこと
  // が無い、TerminalDisplayが自身のシグナル隣接のグローバル状態に
  // ついて置いているのと同じ前提、そちらのoutc()のコメント参照)の
  // ために、SIGWINCH/SIGTTOU/SIGCONTハンドラを設定する。
  // save_signal()のうちKeyInputの関心事である部分からの移植
  // (意図的に除外されているものについてはクラスコメント参照)。
  void installSignalHandlers();

  // 前回の呼び出し以降にSIGWINCHが届いていればtrue(そのフラグを
  // 消費する)。制御端末のサイズも再問い合わせし、それを使って
  // display_.setSize()を呼ぶ。read_m()の`Key.f.winch`チェックと
  // vt_winsize()からの移植。
  bool consumeResizeSignal();

  // それがSHELLのフォアグラウンドプロセスである間にSIGTTOUによる
  // サスペンドが起きた場合、どの子プロセスのpidにSIGCONTを送るべきか
  // を記録する。tc_cursor()/key_winch()のspid管理からの移植
  // (legacyの紛らわしい名前vt_cursor()にもかかわらず、そのロジックが
  // なぜDisplayに属さないかについてはdisplay.hpp参照)。
  void setForegroundChildPid(int pid);

  // エラーメッセージが現在表示待ちかどうか――SIGTTOUがこのプロセス
  // だけをサスペンドするか、プロセスグループ全体をサスペンドするか
  // に影響する。key_ttou()の`s_S(S_error)`チェックからの移植。隠れた
  // Configuration/セッション状態の参照ではなく素のsetterとして公開
  // してあるので、KeyInputはそのクラスについて知る必要が無い。
  void setErrorPending(bool pending);

  // SIGTTOUまたはSIGCONTが発生するたびに、(設定されていれば)この
  // プロセスのpidと共に呼ばれる――key_ttou()/key_cont()の
  // `if (s_S(S_pid)) print_pid(getpid())`という報告フックからの
  // 移植で、sub.cへの直接依存の代わりにコールバックとして公開して
  // ある。
  void setPidReporter(std::function<void(int pid)> reporter);

  // -- 生バイトマクロの再生 --
  // `bytes`をキューし、以降のreadByte()呼び出しが、尽きるまで本物の
  // tty入力の代わりにそれを1バイトずつ返すようにする。read_m()の
  // `p_bm`再生(usilicrcのあらかじめ用意されたM-0..M-19マクロ)からの
  // 移植――legacyの「もう一方の」マクロの仕組み(`p_km`、記録済み
  // コマンドの再生)がここで移植されていない理由についてはクラス
  // コメント参照。
  void playRawMacro(const std::string &bytes);
  bool isPlayingRawMacro() const;

  // installSignalHandlers()が設定する、プロセス全体のSIGTTOU/SIGCONT
  // トランポリン(本物のシグナルハンドラであるため、通常のprivate
  // メンバ呼び出しとしてではなく、アクティブなKeyInputは1つだけと
  // いうポインタ経由でのみこのインスタンスへ到達できる)から呼ばれる
  // ――他の呼び出し元向けではない。
  void reportPid();
  bool errorPending() const;

 private:
  Display &display_;
  int fd_ = -1;
  struct RawState;
  RawState *raw_;  // pimpl: このヘッダーから<termios.h>を締め出す

  std::string macroQueue_;
  std::size_t macroPos_ = 0;

  std::string lastEscapeBytes_;

  bool errorPending_ = false;
  std::function<void(int)> pidReporter_;
};

}  // namespace fm
