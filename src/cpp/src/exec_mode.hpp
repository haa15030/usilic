#pragma once

#include <string>

#include "configuration.hpp"
#include "key_input.hpp"

namespace fm {

// EXEC起動(mode.cのdo_exec()、T_exec): ファイルを丸ごと外部プログラム
// (EXECディレクティブの拡張子マッチ、例: 設定済みのPDFビューア)へ
// 実端末アクセス付きで引き渡す。
//
// これは逐語的な移植ではない。do_exec()は`command`のargvから1本の
// コマンドライン文字列を組み立て(対象ファイル名をクォート)、それを
// alloc_arg()(小さなシェル単語トークナイザ)で再びargvへ分割し直して
// からexecする――これは「文字列化してから再パースする」往復であり、
// CHILD自身のargvがそもそも1本の保存済みコマンドラインから組み立て
// られていたために存在していたに過ぎない。Configuration::programForSuffix()
// は既に本物の`std::vector<std::string>`型argvを返すため、ExecModeは
// 対象パスを1要素として末尾に追加して実行するだけで、文字列組み立ても
// 再分割も不要である。実行自体はEditMode/ProgramModeと同じ
// ChildProcess::runInteractive() + KeyInputのraw mode退出/再突入という形。
//
// 意図的に除外(将来の課題、この段階では対象外): あるサフィックスが
// EXECに割り当てられていること自体の判定(Configuration::isExec()、
// 既に実装済み)と、実行後どうするか(Mode/Applicationの仕事)。
class ExecMode {
 public:
  explicit ExecMode(KeyInput &keyInput);

  // command.argvの末尾にpathを追加して実行する。
  // 終了ステータスを返す。command.argvが空なら-1。
  int run(const ProgramCommand &command, const std::string &path);

 private:
  KeyInput &keyInput_;
};

}  // namespace fm
