#pragma once

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "archive_reader.hpp"
#include "configuration.hpp"
#include "content_view.hpp"
#include "history.hpp"

namespace fm {

// アーカイブ閲覧モード: アーカイブの内容(.tar/.tgz/.tar.bz2/.zip/
// .lha/.rpm/.deb)を一覧表示し、選択されたエントリの生バイト列を
// 抽出する。mode.cのT_tar/T_lha/T_unzip/T_rpm/T_dephケースが、
// init.cのinit_arc()(それ自体はfork_arc()を呼んで一覧コマンドを
// 実行し、続いてalloc.cのalloc_head()/alloc_tar()/alloc_lha()/
// alloc_unzip()でそれをパースする)へ手を伸ばす形からの移植――
// ただしこれは実質的には既に移植済みのクラス群の上に乗るだけの
// 配線であり、新規の移植ではない: Configurationは既に、与えられた
// 拡張子に対しどのコマンドで一覧表示/展開するかを解決し
// (programForSuffix()、.tgz/.tar.bz2用のARCHIVEディレクティブによる
// パイプ分割も含む)、ChildProcessは既にそれを実行し(run()/
// runPipeline())、ArchiveReader系クラス群は既に一覧テキストをパース
// し生バイト列内でエントリの内容を位置特定する。ArchiveModeの本当の
// 仕事は、それら3つを配線しContentViewへ結果を渡すことだけ――
// FileModeがPipeBufferをContentViewへ配線しているのと同じ形。
//
// 意図的に除外(将来の課題、この段階では対象外):
//   - RPM/.debの内容抽出: RpmReader::locateContent()は意図的に何も
//     しない(archive_reader.hpp参照)――RPMのエントリの内容は、
//     tar/zip/lhaのエントリのように.rpmファイル内のバイト範囲では
//     なく、アーカイブ全体に対してrpm2cpioを再度呼び出す必要が
//     ある。.debの一覧は「外側の」`ar`コンテナ(control.tar.gz/
//     data.tar.gz/debian-binary)であって内側のペイロードでは
//     ないため、その中の実ファイルへ到達するには、この段階では
//     まだ構築していない多段パイプライン(`ar p | gzip -dc | tar
//     xf -`)が必要になる。extractSelected()はどちらに対しても
//     nulloptを返す。
//   - ネストしたアーカイブ(あるアーカイブの展開済み内容の中に
//     見つかったアーカイブを開く)と、展開済みの内容をFileModeや
//     さらなる閲覧へ引き渡すこと: 調整するのはMode/Applicationの
//     仕事、DirectoryMode/FileModeが自身の引き渡しについて既に
//     文書化しているのと同じ境界。
//   - メモリに快適に収まらない巨大なアーカイブのために、実際の
//     一時ファイルへ展開すること: この段階ではlocateContent()の
//     ためにアーカイブファイル全体をメモリへ読み込む。ArchiveReader
//     自身の既存のAPIの形(ストリームではなくdata+size)に合わせて
//     いる。
class ArchiveMode {
 public:
  ArchiveMode(ContentView &view, History &history, const Configuration &configuration);

  // Configuration::programForSuffix()経由でpathの一覧表示コマンド
  // (.tgzのようなパイプ形式ならその展開段階も)を解決し、それを実行
  // して、結果を対応するArchiveReaderのサブクラスでパースする。
  // このパスに対して記憶済みのカーソル位置がHistoryにあればそれを
  // 復元する(PositionCategory::Screenの下に保存――legacy自身が
  // アーカイブ一覧のカーソル記憶に使っていた雑多カテゴリ、History
  // 自身のクラスコメント参照)。pathの拡張子が設定済みのアーカイブ
  // フォーマットでない場合、または一覧表示コマンドが失敗した場合は
  // false。
  bool loadArchive(const std::string &path);

  // 2026-09-20追加、直接のユーザー報告("debian-keyring...tar.xzを
  // 踏んだら反応が無くなった。LOCATE同様、^Gで読み込みを中断できる
  // ようにして"): loadArchive()自身の「一覧コマンドを実行して結果を
  // パースする」の後半(パース+状態確定)だけを取り出したもの――
  // Application側がChildProcess::startCapturing()で非同期に起動し、
  // ポーリング/^G中断でその時点までに集めた出力を渡す際に使う
  // (loadArchive()自身はこれを呼ぶ形に作り直され、他の全ての
  // 呼び出し元(ネストしたアーカイブへの降下等)は無改造で今まで通り
  // 同期的に動く)。outputの再パースに必要なコマンド解決
  // (configuration_.programForSuffix())をここでもう一度行う――
  // プロセスは起動し直さない、文字列比較だけの安価な処理。
  bool finishLoadFromOutput(const std::string &path, const std::string &output);

  const std::string &currentArchive() const;
  // currentArchive()の具体的なフォーマット(loadArchive()呼び出し前は
  // None)――K-57/K-58(^X^Ka/^Xa、「ARC: Expand archive」)のために
  // 公開されている。そちらは、正しい抽出コマンドを組み立てるために
  // フォーマットを知る必要がある(Applicationの仕事、そちら自身の
  // descendToNestedArchive()/K-57のコメント参照――ArchiveMode自体は
  // フォーマットを公開するだけで、宛先への抽出コマンドを自分で実行
  // することはしない)。
  ArchiveKind archiveKind() const;
  const std::vector<ArchiveEntry> &entries() const;
  const ArchiveEntry *selectedEntry() const;

  // 選択されたエントリの内容を抽出する: .tarの場合は、生のアーカイブ
  // ファイルのバイト列内にある本物のtarヘッダーを直接パースする
  // ことで。.zip/.lhaの場合は、代わりにそのフォーマットの標準出力へ
  // 抽出するコマンド(`unzip -p`/`lha p`)を実行し、その展開済み出力
  // の中でエントリを位置特定することで(この2つのフォーマットが
  // なぜ異なるデータ源を必要とするかは.cpp参照)。選択が無い、選択が
  // ディレクトリエントリである、そのフォーマットがこれに全く対応
  // していない(RPM/.deb、クラスコメント参照)、または基盤となる
  // コマンドの読み込み/実行に失敗した場合はnullopt。
  std::optional<std::vector<unsigned char>> extractSelected();

  void saveCursorPosition();

  // -- 選択マーキング(K-57/K-58、^X^Ka/^Xa自身の「抽出する特定の
  // エントリをマークする」モード――HELP.jp 2.11自身の例では、
  // Destinationを与える前に^Mで2つのTOCエントリをマークする) --
  // DirectoryMode::toggleMark()/isMarked()/clearMarks()/markAll()/
  // targetEntries()を正確に踏襲している(この移植自身が確立した、
  // legacyの実際のモードごとのマーキング機構を^Spaceベースで簡略化
  // したもの――ここでのマークがentries_へのインデックスであり、
  // legacyのpp->splitによるその場でのマーキングではなく「現在の」
  // 一覧に限定されている理由はDirectoryMode自身のクラスコメント
  // 参照)。loadArchive()が新しい一覧を読み込むたびに(ネストした
  // アーカイブへの降下を含む――親アーカイブのTOCからのマークは
  // 引き継がれない)クリアされる。
  void toggleMark(int index);
  bool isMarked(int index) const;
  std::size_t markedCount() const;
  void clearMarks();
  void markAll();

  // 一覧順でのマーク済みエントリ、またはもし何も明示的にマークされて
  // いなければ現在選択中の1エントリだけ(DirectoryMode::
  // targetEntries()が既に確立しているのと同じ「カーソル下にあるものに
  // 対して作用する」という慣習)。マークが無く、有効な選択も無い
  // 場合は空。
  std::vector<ArchiveEntry> targetEntries() const;

  // 「ARC」という名前フィールドの代わりに`message`を下線付きで表示
  // する――2026-08-26、DirectoryMode::reportError()自身の全く同じ
  // 仕組みをそのまま踏襲(そちら自身のコメント参照)、^X^M/^X aの
  // 宛先検証エラー(例えば「<path>: No such directory」)のためのもの。
  void reportError(const std::string &message);
  // reportError()が設定したものをクリアする。Application::
  // handleCommand()により、ディスパッチされたコマンドごとに1回、
  // directoryMode_.clearEditError()と並んで呼ばれる――そちら自身の
  // 呼び出し箇所のコメント参照。
  void clearEditError();

 private:
  static std::string formatEntryLine(const ArchiveEntry &entry, std::size_t sizeWidth);

  ContentView &view_;
  History &history_;
  const Configuration &configuration_;

  std::string currentArchive_;
  ArchiveKind archiveKind_ = ArchiveKind::None;
  // archiveKind_に対して実際に設定されているプログラム名(例えば
  // "tar"/"unzip"/"lha"/"rpm")――モードラインプロバイダ自身の
  // "(<command>)"バッジのコメント参照。loadArchive()の時点で
  // ProgramCommand::argv.front()から読む(コマンドを実行するために
  // loadArchive()自身が既に参照しているのと同じ情報源)。DirectoryMode
  // 自身の"(ls)"バッジの慣習と全く同じ。
  std::string archiveCommand_;
  std::vector<ArchiveEntry> entries_;
  std::set<int> markedIndices_;
  bool showEditError_ = false;
  std::string editErrorMessage_;
};

}  // namespace fm
