#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace fm {

// アーカイブのテキスト一覧("tar tvf"/"unzip -l"/"rpm -qplv"風の行)
// からパースされた1エントリと、(対応するフォーマットの場合)その
// 内容がアーカイブの生バイト列のどこから始まるか。
struct ArchiveEntry {
  std::string name;
  bool isDirectory = false;
  bool isSpecialDevice = false;  // tar/rpmのデバイスファイルエントリ: 実際のサイズを持たない
  long size = 0;

  // locateContent()が設定する。-1は「まだ位置特定されていない」
  // (またはそのフォーマットが全く対応していない、RpmReader参照)を
  // 意味する。
  long contentOffset = -1;
  // このエントリの申告サイズが完全にはカバーされる前に、アーカイブの
  // 生バイト列が終わってしまっている(切り詰められた/不完全な読み
  // 込み)場合にtrue――alloc_tar()/alloc_lha()/alloc_unzip()の
  // `split = -2`という番兵からの移植。
  bool contentTruncated = false;
  // 一覧のコマンドが出した順(0から)。ArchiveMode::finishLoadFromOutput()が並べ替えた
  // (.epubを名前順に、2026-10-01)後も、locateContent()は生のバイト列と同じこの順で
  // 辿る必要がある(ArchiveMode::selectedContent()参照)。
  int listingIndex = -1;
};

// あるアーカイブフォーマットのテキスト一覧をエントリ群へパースし、
// (対応するフォーマットの場合)各エントリの内容をアーカイブの生
// バイト列内で位置特定する。alloc.cのalloc_head()(フォーマットごとの
// 一覧行パーサ)と、alloc_tar()/alloc_lha()/alloc_unzip()(フォーマット
// ごとの生バイト内容位置特定処理)からの移植。
//
// 意図的に除外(将来のChildProcessクラスの仕事であり、ここではない):
// 実際に`tar`/`unzip`/`lha`/`rpm`を実行してその出力を捕捉すること、
// およびアーカイブフォーマット固有の展開パイプライン(例えば.debの
// data.tar.gzはまず`ar p | gzip -dc`が必要)――これらはfork.cの
// fork_arc()からの移植で、そちらは密なマルチレベルのfork()/pipe()の
// 配管とフォーマット選択が絡み合っている。そのプロセス管理の関心事は、
// 「この」クラスがすること(何らかの手段で既に得られたテキスト/
// バイト列の純粋なパース)とは全く直交しており、そのクラスがまだ
// 実際には存在しないのにここでChildProcess風のAPIを考案するのは
// 憶測になってしまう。現時点での呼び出し側は、自分で`listing`と
// `data`を取得し、このクラスへ渡すことが期待されている。
//
// legacyのCからのデータモデルの変更: alloc_head()は各エントリを
// 単一の表示用文字列(パーミッション風の接頭辞+固定幅のサイズ
// フィールド+ハードコードされたバイトオフセット20の位置にある
// ファイル名)へフォーマットし、後でalloc_tar()/alloc_lha()/
// alloc_unzip()が、生バイト列の中で何を検索すべきか知るために、
// その「同じ」フォーマット済み文字列を(`&pp->cs[20]`で)再び
// 切り出してファイル名を再抽出していた――これは、PPが全てを保持
// するための文字列フィールドを1つしか持っていなかったために存在
// していた「フォーマットしてから再パースする」往復に過ぎない。
// ArchiveEntryは最初からnameとsizeをそれぞれ別個のフィールドとして
// 保持するので、locateContent()はentry.nameを直接使う。ここでは
// 表示用文字列を組み立てることも再パースすることも全く不要である
// (これらのエントリが与えられた上で画面表示用にフォーマットする
// のはContentViewの仕事)。
class ArchiveReader {
 public:
  virtual ~ArchiveReader() = default;

  virtual std::vector<ArchiveEntry> parseListing(const std::string &listing) const = 0;

  // entriesは既に.sizeが(parseListing()により)設定済みでなければ
  // ならない――これは.contentOffset/.contentTruncatedだけを埋める。
  virtual void locateContent(const unsigned char *data, std::size_t size,
                              std::vector<ArchiveEntry> &entries) const = 0;
};

// .tarと.deb(.debのdata.tar.gzペイロードは、展開すればそれ自体が
// tarストリームである――legacyのalloc_head()/alloc_tar()がまさに
// この理由で同じT_tar/T_deb分岐を共有していた。init.cのT_tar/T_deb
// ケースがどちらもalloc_tar()を呼んでいるのを参照)。
class TarReader : public ArchiveReader {
 public:
  std::vector<ArchiveEntry> parseListing(const std::string &listing) const override;
  void locateContent(const unsigned char *data, std::size_t size,
                      std::vector<ArchiveEntry> &entries) const override;
};

class ZipReader : public ArchiveReader {
 public:
  std::vector<ArchiveEntry> parseListing(const std::string &listing) const override;
  void locateContent(const unsigned char *data, std::size_t size,
                      std::vector<ArchiveEntry> &entries) const override;
};

// alloc.c内の文書化されたヘッダー形式コメントからの移植(これを移植
// する際、実際の`lha`バイナリが手元に無く実際のサンプル出力を採取
// できなかった――当時代わりに使った、文書化された列レイアウトに
// 合わせて作った合成フィクスチャについてはcpp/tests/
// test_archive_reader.cpp参照)。
//
// 2026-09-15追加: 実際のLhasa版`lha`(Homebrew、v0.6.0)による実機
// 検証で、parseListing()に実在するバグを発見・修正した――UID/GID欄が
// 不明な場合"*****/*****"のように'*'で埋められる実際の出力に対して、
// 比率マーカー探索が'*'も候補にしていたため、本物の'%'より先にそこで
// 誤ってマッチしていた。詳細・回帰テストはparseListing()自身のコメント
// およびcpp/tests/test_archive_reader.cppの実機出力を貼ったテスト
// 参照。locateContent()側(3行ヘッダー "::::::::"/ファイル名/
// "::::::::")は同じ実機検証で形式が一致することを確認済み。
class LhaReader : public ArchiveReader {
 public:
  std::vector<ArchiveEntry> parseListing(const std::string &listing) const override;
  void locateContent(const unsigned char *data, std::size_t size,
                      std::vector<ArchiveEntry> &entries) const override;
};

// 一覧のみ。legacyはRPMについて内容の位置特定を一度も実装しなかった
// (init.cのT_rpmケースはalloc_tar/alloc_lha/alloc_unzipのどれも
// 呼んでいない)――RPMのエントリは、アーカイブ全体に対してrpm2cpioを
// 再度呼び出さない限り閲覧できず、それはまさにまだ実装されていない
// ChildProcessの仕事であり、ここでバイトオフセットを偽装するような
// ものではない。
class RpmReader : public ArchiveReader {
 public:
  std::vector<ArchiveEntry> parseListing(const std::string &listing) const override;
  void locateContent(const unsigned char *, std::size_t,
                      std::vector<ArchiveEntry> &) const override {}
};

// 2026-08-26追加(ユーザーからの直接依頼、実際の.rarフィクスチャで
// テスト): `lsar -l <archive>`の出力をパースする(プロプライエタリな
// `unrar`ではなくlsar/unarを使う理由についてはこのクラス自身の宣言
// コメント参照)。legacyの前例は無い(RARはこのプロジェクトの本来の
// C時代より後に登場した)。locateContent()はZipReader/LhaReaderと
// 「同じ」逐次走査方式を使う(ZipReader::locateContent()自身の
// コメント参照)――`unar -f -o - -q <archive>`は全エントリを、
// lsar -lが一覧表示するのと「同じ」順序で連結して標準出力へ抽出する
// (直接確認済み: 抽出されたストリーム自身の総バイト数が、一覧に
// 載っている全ファイル自身のサイズの合計と厳密に一致し、先頭バイト
// も一覧の最初のファイル自身の既知の内容と一致する)。
class RarReader : public ArchiveReader {
 public:
  std::vector<ArchiveEntry> parseListing(const std::string &listing) const override;
  void locateContent(const unsigned char *data, std::size_t size,
                      std::vector<ArchiveEntry> &entries) const override;
};

// 2026-08-26追加(ユーザーからの直接依頼、実際の.7zフィクスチャで
// テスト): `7z l <archive>`の出力をパースする。legacyの前例は無い
// (7zはこのプロジェクトの本来のC時代より後に登場した)。
// locateContent()はZipReader/LhaReader/RarReaderと「同じ」逐次走査
// 方式を使う――`7z x -so <archive>`は全エントリを、`7z l`が一覧
// 表示するのと「同じ」順序で連結して標準出力へ抽出する(RarReader
// 自身のコメントが説明するのと同じ方法で確認済み: 総バイト数と
// 最初のファイルの内容の両方が一致する)。
class SevenZipReader : public ArchiveReader {
 public:
  std::vector<ArchiveEntry> parseListing(const std::string &listing) const override;
  void locateContent(const unsigned char *data, std::size_t size,
                      std::vector<ArchiveEntry> &entries) const override;
};

}  // namespace fm
