#pragma once

#include <functional>
#include <string>

#include "display.hpp"
#include "history.hpp"
#include "key_input.hpp"

namespace fm {

// PROGRAMモード(M_prog/M_prog2): 現在の選択に対して任意のシェル
// コマンドラインを、実端末アクセス付きで実行する。捕捉出力型の
// ChildProcess::run()では対応できない対話的/全画面プログラム
// (`less somefile`、`vi somefile`、ユーザー設定のPROGRAMリスト項目)
// のためのもの。mode.cのdo_prog()からの移植。
//
// これは逐語的な移植ではない。do_prog()は、raw termioの退出/再突入
// (vt_init(-2)/vt_init(2))と、ジョブコントロールによる中断からの
// 再開時にこのプロセスのpidを報告する専用のSIGCONTハンドラ
// (mode_cont())を取り巻く、自前のfork()/execlp()/wait()の手順を
// 手作りしていた。fork/exec/waitの仕組みは、今ではChildProcess::
// runInteractive()(このクラスと一緒に追加、本当に再利用可能な
// プリミティブであるため: 「これを、本物の継承stdioで、ptyもパイプ
// も無しにフォアグラウンドで実行する」――ChildProcessの既存の捕捉
// 出力型run()/runPipeline()とも、Shellのpty接続されたSHELLセッション
// とも異なる)になっている。raw modeの退出/再突入はKeyInputの既存の
// leaveRawMode()/reenterRawMode()で、まさにこの目的のために既に
// 構築済みのもの。
//
// 意図的に除外(将来の課題、この段階では対象外):
//   - SIGCONTによるpid報告ハンドラ(do_prog()のmode_cont()):
//     KeyInputには既にSIGCONT/フォアグラウンドpidの仕組み
//     (setForegroundChildPid()/setPidReporter())があるが、それは
//     SHELLの永続的な、pty接続された子プロセス専用に作られたもの
//     ――legacyが実際に両者で挙動を共有するつもりだったと確認も
//     せずここで再利用するのは、移植ではなく単なる憶測になって
//     しまう。未配線のまま。
//   - execする前の`unsetenv(ENV_PROG)`(do_prog()のトリック、
//     PROGRAMモードのコマンドがusilic自身を再帰的に再起動できる
//     ようにする): この1つの呼び出し元に固有すぎて限定的すぎる
//     ため、新しい共有プリミティブChildProcess::runInteractive()へ
//     焼き込むのは、汎用プリミティブを1つの呼び出し元の要求に
//     過剰適合させることになる。
//   - 「どの」コマンドを実行するかの決定(M_progプロンプト、
//     Configuration経由のPROGRAMリスト検索、プログラム実行中に
//     対象ファイルが変化したかの鮮度チェックで再描画すべきか決める
//     こと): Mode/Applicationの仕事、この移植の他の全`*Mode`クラス
//     が既に引いているのと同じ引き渡しの境界。
class ProgramMode {
 public:
  ProgramMode(KeyInput &keyInput, Display &display, History &history);

  // `command`を`sh -c command`として、実端末アクセス付きで
  // フォアグラウンドで実行し(先にraw modeを退出し、後で再突入する)、
  // `recordedCommand`(既定は`command`自身)をHistoryのShellCommand
  // ring(shell.c自身のコマンドが供給するのと同じring、do_prog()
  // 自身の`*histp = save_history(*histp, str, 1)`に相当)へ記録する。
  // 子プロセスの終了ステータスを返す。commandが空、または子プロセス
  // を起動できなかった場合は-1。
  //
  // recordedCommandを「別の」パラメータにしている理由(2026-08-17
  // 追加): executeProgram()自身の`cd '<dir>' && `という接頭辞
  // (そちら自身のコメント参照)は、素のファイル名がどう解決される
  // かという内部実装の詳細であり、ユーザーが入力したものではない
  // ――それを履歴に記録してしまうと、^J^Rのコマンド再呼び出しと
  // ^J^PのPROGRAMリストが、後で再送信しても認識可能なプログラム名
  // からすら始まらない文字列で壊れてしまう。
  int run(const std::string &command, const std::string &recordedCommand = "");

  // run()が実際に`command`をフォアグラウンドで「どう」実行するかを
  // 上書きする。設定済みでtrueを返した場合、run()はフォアグラウンド
  // への受け渡し全体(raw modeの退出/再突入を含む)をそちらが処理し
  // 終えたと信頼し、leaveRawMode()/ChildProcess::runInteractive()/
  // reenterRawMode()を一切スキップする。falseなら、launcherが全く
  // 設定されていない場合と全く同じ通常の経路へフォールバックする。
  // 未設定(既定)の場合は常に直接ChildProcess::runInteractive()を
  // 使い、これが存在する前と変わらない。
  //
  // どこからの移植でもない――legacyにはGUIモードが全く無いため。
  // ただ1つの呼び出し元のために存在する: main.cppのtryEnterGuiPhase()、
  // GraphicalDisplayウィンドウがアクティブなdisplayである間、この
  // プロセス自身が継承したstdioが、`less`/`vi`のようなフォアグラウンド
  // プログラムが直接使える本物の端末ではなくなっている場合(そちらの
  // runProgramInOverlayTerminal()自身のコメント参照、launcherが
  // 実際に何をするか: GUIウィンドウの現在の画面位置に重なる本物の
  // Terminal.appウィンドウを開く)。サブクラス/フラグではなく設定
  // 可能なコールバックとして配線されている。Shell::setProgramLauncher()
  // が既に確立したのと同じ形で、ProgramMode自身はApplicationが既に
  // そうしているのと全く同様にSDL2/GraphicalDisplayの詳細について
  // 一切知らないままにしている(その境界についてはApplication自身の
  // クラスコメント参照)――実際にGUI対応のlauncherを組み立てるのは
  // main.cppだけである。
  void setForegroundLauncher(std::function<bool(const std::string &command)> launcher);

 private:
  KeyInput &keyInput_;
  Display &display_;
  History &history_;
  std::function<bool(const std::string &command)> foregroundLauncher_;
};

}  // namespace fm
