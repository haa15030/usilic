#pragma once

#include <string>
#include <vector>

#include "child_process.hpp"
#include "configuration.hpp"
#include "key_input.hpp"

namespace fm {

enum class WriteCheck {
  Plain,             // 通常のRM/MVのソース: シンボリックリンク、W_OK、または現在ユーザー所有
  MustBeDirectory,    // MVの宛先チェック、既にディレクトリとして存在していなければならない場合
  MissingIsWritable,  // MVの宛先チェック、対象が存在しなくてもよい(作成される)場合
};

// CP/MV/RM(mode.cのcommand()、T_cp/T_mv/T_rmのシェルスクリプト
// 組み立て担当の半分): 対象ファイル群をコピー・移動・削除する。
// command() + is_write()からの移植――ただし意図的に引き継いでいない
// 部分については下記参照。
//
// これはcommand()の逐語的な移植ではない。legacy版は、完全な
// シェルスクリプト("cd '<dir>'\nif ! cp -vRi ... \n")を組み立てて
// forkした`sh`へパイプする。これが必要な理由は(a)まず正しい
// ディレクトリへchdirする必要がある(その対象一覧は、現在表示中の
// バッファを基準とした素のファイル名の集合であるため)、(b)いくつかの
// バッファのソース固有のケース(T_arc/T_arc3のメンバはディスク上の
// 実際のパスからコピーするのではなくアーカイブから抽出される、T_du
// のパスは別途計測された`du`サブディレクトリを基準とした相対パス
// である)をインラインで処理する必要があるため。このクラスは代わりに
// 既に解決済みの絶対パス(DirectoryMode::targetEntries()は既に
// DirectoryMode自身のcurrentDirectory()を基準とした実際のパスを
// 返す)を受け取り、ChildProcess経由で直接cp/mv/rmを呼ぶ――chdirも
// 無く、シェルスクリプトの生成も無く、文字列を組み立ててから
// 再パースすることも無い。
//
// 意図的に除外(将来の課題、この段階では対象外):
//   - アーカイブ内(T_arc/T_arc3)や`du`計測済みのサブディレクトリ
//     (T_du)に存在する項目のコピー/移動/削除: どちらもそもそも
//     cp/mv/rmへ渡すための、解決済みのディスク上のソースパスが必要
//     だが、それらのバッファのソースには、追加の作業(アーカイブ
//     メンバを一時ファイルへ抽出する、du相対パスを解決する、いずれも
//     まだ実装されていない)無しにはそれが無い。
//   - 対話的な確認プロンプト(cp/mv/rm自身の`-i`フラグ、既に
//     ConfigurationのデフォルトのCP/MV/RMロールコマンドに焼き込み
//     済みで、標準入力への問い合わせによりOSレベルでこれを処理する
//     ――ただしこのクラスが使うChildProcess::run()は本物の端末を
//     繋がない。対話的な-i確認をしたい呼び出し側は、代わりに
//     ChildProcess::runInteractive()を経由すべきで、これは
//     ProgramMode/EditMode/ExecModeが使うのと同じ実行の形)。
//   - do_rm()(mode.c): 名前に反して、この関数はファイルシステムの
//     削除とは一切関係が無い――LISTブラウザのピッカーメニュー
//     (最近使ったファイル/ディレクトリ/manページ/プログラム/grep
//     パターン)の1つから保存済みの項目を削除するもので、
//     Configuration/Historyの編集可能なリストカテゴリに紐づく全く
//     別の機能である。このクラスの関心事ではない。明白な名前の
//     混同を未然に防ぐためだけにここに記す。
class CpMvRmMode {
 public:
  // keyInput: 下の*Interactive()系(K-51/K-53/K-55、「SHELLウィンドウ
  // あり」)だけが、ChildProcess::runInteractive()の前後でraw modeを
  // 退出/再突入するために使う。ExecMode/ProgramMode/EditModeが既に
  // 使っているのと同じ形――それぞれ自身のコメント参照。
  //
  // errorLogPath: nullでなければ、非対話的な実行全て(copyTo()/
  // moveTo()/remove()/run()――*Interactive()系は対象外、そちらの子
  // プロセスは既に本物の端末を持っており標準エラーもそこに含まれる)
  // の標準エラーが、ChildProcess::Options::stderrLogPath経由でそこへ
  // リダイレクトされる。K-67(^X^Ke、「ERROR」)自身のデータ源――
  // Application自身のerrorLogPath_のコメント参照。null(既定)なら
  // これを無効化する、例えばそれを気にしないテストのために。
  CpMvRmMode(const Configuration &configuration, KeyInput &keyInput, const std::string *errorLogPath = nullptr);

  // is_write()からの移植: pathが、与えられたチェックの下で有効な
  // RM/MV対象かどうか。root(uid 0)は常に書き込み可能。シンボリック
  // リンク、access(W_OK)で書き込み可能なパス、現在のユーザーが所有
  // するパスは、いずれもPlain/MustBeDirectoryにおいて書き込み可能と
  // 数える。存在しないパスはMissingIsWritable(MVの「宛先はまだ存在
  // していなくてよい」ケース)の下でのみ書き込み可能で、それ以外では
  // 拒否される。MustBeDirectoryはさらに、(存在する)パスが実際に
  // ディレクトリであることを要求する。
  static bool isWritable(const std::string &path, WriteCheck check = WriteCheck::Plain);

  // sources(絶対パス)を、設定済みのcp/mvコマンド(Configuration::
  // commandFor(Cp/Mv))を使ってdestinationへコピー/移動する。本物の
  // 端末を繋ぐのではなく出力(-vの進捗行)を捕捉する――-i確認について
  // はクラスコメント参照。これはusilicrcのK-52/K-54
  // (「SHELLウィンドウ無し」)。
  ChildProcess::Result copyTo(const std::vector<std::string> &sources, const std::string &destination);
  ChildProcess::Result moveTo(const std::vector<std::string> &sources, const std::string &destination);

  // targets(絶対パス)を、設定済みのrmコマンドを使って削除する。
  // K-56(「SHELLウィンドウ無し」)。
  ChildProcess::Result remove(const std::vector<std::string> &targets);

  // K-51/K-53/K-55(^X^Kc/^X^Km/^X^Kr、「SHELLウィンドウあり」):
  // copyTo()/moveTo()/remove()と同じcp/mv/rm呼び出しだが、捕捉型の
  // run()ではなくChildProcess::runInteractive()経由――実行中の本物の
  // コマンド自身の出力(例えば-vの進捗行)が、後でResult::output経由
  // でしか得られないのではなく、実行中にそのまま本物の端末上で見える。
  // 子プロセスの終了ステータスを返す。sourcesが空、またはそのロール
  // に対してコマンドが設定されていない場合は-1(ExecMode::run()自身の
  // -1という慣習と同じ)。
  int copyToInteractive(const std::vector<std::string> &sources, const std::string &destination);
  int moveToInteractive(const std::vector<std::string> &sources, const std::string &destination);
  int removeInteractive(const std::vector<std::string> &targets);

  // 任意のargvを、本物の端末を子プロセスに渡した状態で実行する――
  // 上の*Interactive()系メソッドが既に行っているのと「同じ」raw mode
  // の退出/再突入によるラッピングだが、それらのcp/mv/rm固有の
  // 「sources... destination」というargvの形は無い。ArchiveMode自身の
  // K-57(^X^Ka、「ARC: SHELLウィンドウあり」)の抽出処理と共有して
  // おり、そちらは自前のtar/unzip argvを組み立て(Applicationの仕事)、
  // このクラスのcp/mv/rmロジックではなくKeyInputの切り替え機能だけを
  // 必要とする。argvが空なら-1。
  int runInteractive(const std::vector<std::string> &argv);
  // 捕捉型の、非対話版――K-58(「SHELLウィンドウ無し」)。argvが空なら
  // 空のResult。
  ChildProcess::Result run(const std::vector<std::string> &argv);

 private:
  std::vector<std::string> buildArgv(ConfiguredRole role, const std::vector<std::string> &sources,
                                      const std::string *destination) const;
  ChildProcess::Result runWithSources(ConfiguredRole role, const std::vector<std::string> &sources,
                                       const std::string *destination);
  int runInteractiveWithSources(ConfiguredRole role, const std::vector<std::string> &sources,
                                 const std::string *destination);

  const Configuration &configuration_;
  KeyInput &keyInput_;
  const std::string *errorLogPath_;
};

}  // namespace fm
