#include "archive_reader.hpp"

#include <algorithm>
#include <cctype>

namespace fm {

namespace {

std::vector<std::string> splitLines(const std::string &text) {
  std::vector<std::string> lines;
  std::size_t pos = 0;
  while (pos < text.size()) {
    std::size_t eol = text.find('\n', pos);
    std::string line = (eol == std::string::npos) ? text.substr(pos) : text.substr(pos, eol - pos);
    if (!line.empty()) lines.push_back(line);
    pos = (eol == std::string::npos) ? text.size() : eol + 1;
  }
  return lines;
}

// alloc.cのalloc_head() T_tar/T_debケースからの移植: 1つのtar一覧の
// `ls -l`風の行の中で、SIZEフィールドとファイル名の開始位置を特定
// する。tar tvfは、それを書いたtarによって形が異なる2つのレイアウト
// を持つ――GNU tarは('/'を含む)結合された「owner/group」フィールド
// を印字する。BSD tar(macOS)はownerとgroupを別々の2つのフィールド
// として印字し、その領域には'/'が全く無い。これは行の末尾から
// 逆方向にその'/'を探し、それが一度も見つからなかった場合にだけ、
// 先頭からの固定4フィールドのスキップ(mode、links、owner、group)
// へフォールバックする。
ArchiveEntry parseTarLine(const std::string &rawLine) {
  ArchiveEntry entry;

  std::string line = rawLine;
  auto arrow = line.find(" -> ");
  if (arrow != std::string::npos) line.resize(arrow);  // "-> symlink-target"を取り除く

  int n = static_cast<int>(line.size());
  int ce = n - 1;
  int cs = 0;

  // mode links owner group SIZE mon day time/year name -- BSD tar
  // (macOS)のフォールバックの形。下のce==0のケースと、GNU分岐自身の
  // 誤検出からの回復(自身のコメント参照)の両方が、まさにこの同じ
  // 4フィールドのスキップを実行する必要があるので、独立した関数へ
  // 括り出されている。
  auto skipBsdFields = [n, &line]() {
    int c = 0, k = 0;
    while (k < 4 && c < n) {
      if (line[c] == ' ') {
        while (c + 1 < n && line[c + 1] == ' ') c++;
        k++;
      }
      c++;
    }
    return c;
  };

  for (int i = 0;; i++) {
    for (; ce > 0; ce--)
      if (line[ce] == ' ' || (i && line[ce] == '/')) break;
    if (i == 0) cs = ce + 1;  // ファイル名は最後の空白の直後から始まる
    if (i && line[ce] == '/') {
      // GNUフォーマット: "owner/group"からsizeフィールドへ前方へ歩く。
      // 2026-08-19の修正、実際の報告+再現(自身の名前が全エントリの
      // パス接頭辞として再度現れるディレクトリのtar、例えば
      // "utf8/foo.txt")――上の逆方向走査には、本物のGNU
      // 「owner/group」区切りと、単に「ファイル名自体」の一部である
      // '/'(サブディレクトリの下にネストされたエントリは全てそれを
      // 持つ)とを区別する方法が無い。誤検出のマッチは、このループを
      // 任意のファイル名のバイト列の中を前方へ、その先のどこにも
      // 存在しないかもしれない「数字の後に空白/カンマ」というパター
      // ンを探して歩かせる――以前は、`ce`/`p`の両方が`n`に達した時点で
      // (下のどちらの走査もそれ以上進まず、`p < n`は永遠にfalseの
      // まま)制限の無い無条件の`while (true)`であり、エラーで抜ける
      // のではなくCPU100%で回転していた。今は境界がある: 有効な
      // sizeフィールドを一度も見つけないまま末尾を走り抜けた場合、
      // これはそもそも本物のGNUフォーマットの「owner/group」データ
      // では「なかった」――下のce==0分岐が既に使っているのと同じ
      // 信頼できるBSDフィールドスキップへフォールバックする(この
      // システム自身の`tar tvf`出力はBSDフォーマット「である」。誤
      // 検出は常にファイル名自身の'/'から生じるだけで、本物のフォー
      // マットの不一致からは決して生じない)。
      bool foundSize = false;
      while (ce < n) {
        while (ce < n && line[ce] != ' ') ce++;
        while (ce < n && line[ce] == ' ') ce++;
        if (ce >= n) break;
        int p = ce;
        while (p < n && std::isdigit(static_cast<unsigned char>(line[p]))) p++;
        if (p < n && (line[p] == ' ' || line[p] == ',')) {
          foundSize = true;
          break;
        }
        ce = p;
      }
      if (!foundSize) ce = skipBsdFields();
      break;
    } else if (ce == 0) {
      ce = skipBsdFields();
      break;
    } else {
      while (ce > 0 && line[ce] == ' ') ce--;
    }
  }

  long size = 0;
  while (ce < n && line[ce] != ' ') {
    if (line[ce] == ',') entry.isSpecialDevice = true;
    else if (!entry.isSpecialDevice) size = size * 10 + (line[ce] - '0');
    ce++;
  }

  // ファイル名の開始位置を、ここから「前方へ」再導出する(2026-08-19
  // の修正、上のGNU分岐のハングと同じ再現で発見――実際のファイル名
  // には、リテラルな空白が含まれ「得る」し実際に含まれることがある、
  // 例えば"utf8/報道ステーション 速報 森保監督...")。上のi==0で計算
  // される`cs`は、行の末尾から「後方へ」最後の空白を探して走査する
  // が、ファイル名自体にそれが含まれている場合はいつでも誤りである
  // ――それは、真の開始位置ではなくファイル名の途中に着地し、表示
  // される名前を切り詰めてしまう(確認済み: このまさにこのエントリ
  // は"utf8/報道ステーション　速報 森保監督が会見…"ではなく
  // "森保監督か会見…"として描画され、埋め込まれた空白までの全てが
  // 欠けていた)。BSDとGNU両方のtarの`tar tvf`出力では、正確に3つの
  // 空白区切りフィールド(月、日、時刻または年)が常にsizeフィールド
  // とファイル名の間に座っているので、ちょうどパースしたばかりの
  // sizeフィールド――既に正しいと分かっている位置――から「前方へ」
  // それら3フィールド分数えることで、ファイル名自体が何を含んで
  // いても真のファイル名開始位置に到達する。行が実際にスキップす
  // べき3つ以上のフィールドを持つ場合にだけ`cs`を上書きする。予期
  // せず短い/不正な形の行は、部分的な前方カウントを信頼するのでは
  // なくi==0の後方走査による推測を維持する。
  {
    int fs = ce;
    for (int k = 0; k < 3 && fs < n; k++) {
      while (fs < n && line[fs] == ' ') fs++;
      while (fs < n && line[fs] != ' ') fs++;
    }
    while (fs < n && line[fs] == ' ') fs++;
    if (fs < n) cs = fs;
  }

  entry.name = line.substr(cs);
  entry.size = entry.isSpecialDevice ? 0 : size;
  entry.isDirectory = !line.empty() && (line[0] == 'd' || line[0] == 'l');
  return entry;
}

}  // namespace

std::vector<ArchiveEntry> TarReader::parseListing(const std::string &listing) const {
  std::vector<ArchiveEntry> entries;
  for (const auto &line : splitLines(listing)) entries.push_back(parseTarLine(line));
  return entries;
}

// 8進数ASCIIフィールド(tar自身のsize/mode等の数値符号化、最大
// `len`桁バイト、空白/NULで終端)。
long parseTarOctalField(const unsigned char *p, int len) {
  long v = 0;
  for (int i = 0; i < len; i++) {
    unsigned char c = p[i];
    if (c < '0' || c > '7') break;
    v = v * 8 + (c - '0');
  }
  return v;
}

// find_tar_header()からの移植: tarのヘッダーは、アーカイブの先頭から
// 常に512バイト境界に整列しているので、その整列に一致する位置だけが
// チェックされる――そうしないと、ヘッダーのテキストが`name`を単に
// 部分文字列として「含んでいる」だけの隠しエントリ(例えばmacOSの
// "._name" AppleDoubleサイドカー、"tar tvf"は一覧に載せないが、
// バイトストリーム中の本物のヘッダーである)がそれと誤認されて
// しまう。これが修正した元のバグについてはCLAUDE.mdの履歴項目4
// 参照。
//
// 2026-08-19の再設計(報告=dj/=dk: "サイズ111...以外内容が表示
// されない"――名前がたまたま濁点/半濁点付きの仮名を含まないもの
// 以外の全エントリが空で返ってきていた)。この関数の「以前の」
// バージョン(同日、別の報告――"何も無し"、PAXの100バイト名前
// オーバーフロー――のために追加された)は、候補となるヘッダーを、
// それら自身の名前(古典的なフィールド、またはPAXレコードの
// "path="値)を`entries_[i].name`とバイト単位で比較することで一致
// させていた。その名前は、TarReader::parseListing()自身のテキスト
// ベースの`tar tvf`の標準出力のパースから来ている――そしてmacOSでは、
// `tar tvf`は、CJKの濁点/半濁点付き文字(例えば「デ」/「が」)を、
// アーカイブ自身の合成済みNFC形式(PAXの"path="レコードの生バイト
// に実際に格納されているもの)から、それ自身の人間が読める一覧出力
// のために分解済みNFD形式(基底の仮名のコードポイント+別々の結合
// U+3099/U+309Aマーク、そのような文字1つにつきNFC版より3バイト長い)
// へ黙って再符号化する。直接確認済み: "...データセンター建設が争点
// に..."の生のPAXバイトをダンプすると"デ"=e3 83 87(NFC、1コード
// ポイント)、"が"=e3 81 8c(NFC)が見えた――一方、同じエントリに
// 対する`tar tvf`自身の標準出力は"テ"+U+3099と"か"+U+3099(NFD、
// それぞれ2コードポイント)を見せていた。そのような文字を「1つも
// 含まない」名前(この報告の唯一動いていたエントリ、"...ウクライナ
// を支える...")は、どちらの方法でもラウンドトリップが同一になる。
// これがそれだけが動いていた理由である。
//
// 同じ名前の2つの表現を突き合わせるためだけにUnicode正規化ライブ
// ラリを引き込むのではなく、これは名前マッチング(とPAXパス抽出)を
// 完全に捨てる: `tar tvf`自身の一覧順序はアーカイブの本物の物理的
// 順序である(それが「アーカイブを一覧表示する」ことの意味だ)。
// そしてそこから欠けている唯一のエントリは、まさにこれらの
// "._"AppleDoubleサイドカーである――Unicodeに関係なく、それら自身の
// リテラルなASCIIの"."、"_"接頭辞バイトによって確実に識別できる
// (「本物の」ヘッダー、つまりその前にあるPAXレコードを解決した後
// でチェックされる。bsdtarは、PAXオーバーフローした名前であって
// さえ、ヘッダー自身の切り詰められた/プレースホルダーの名前
// フィールドに、"._"接頭辞付きの真の開始部分を常に保持するため)。
// そのためこれは今、ヘッダーをヘッダーごとに前方へ歩き、PAXレコード
// (それら自身の宣言されたデータ長を丸ごと飛び越える)と、名前が
// "._"で始まる結果として得られる本物のヘッダーをスキップし、
// サイドカー「ではない」最初のものの位置を返す――entries_と生の
// アーカイブは順序について一致しているので、それはこの呼び出しが
// 実際に見つけるよう頼まれたエントリであることが保証されている。
// 本物のヘッダー自身の古典的な(100バイト、NUL終端の)名前フィール
// ドがAppleDoubleサイドカーかどうか――つまり、そのフィールド自身の
// 最初の2バイトとは限らず、その「ベース名」が"._"で始まるか。実際の
// アーカイブで直接観測された2つの形があった: PAXオーバーフローを
// 一度も必要としなかった短い名前は、古典的フィールドに完全な
// "dir/._name"パスを保持する(例えばこの移植自身の合成テスト
// フィクスチャの"src/._world.txt")。一方、PAXレコードへ実際に
// オーバーフローした長い名前は、bsdtar自身のプレースホルダーの
// 古典的フィールド値が先頭のディレクトリ部分を完全に落とし、
// 接頭辞が全く無い単なる"._<切り詰められた名前>"だけを残す(この
// 移植自身の実際のutf8.tar再現アーカイブ)。フィールド内の「最後の」
// '/'を探すことで両方を扱える: '/'が全く無ければ何もしない
// (フィールド全体が既にベース名である)。
bool isSidecarHeader(const unsigned char *header, const unsigned char *end) {
  const unsigned char *limit = std::min(header + 100, end);
  const unsigned char *nameEnd = header;
  while (nameEnd < limit && *nameEnd != '\0') nameEnd++;
  const unsigned char *base = header;
  for (const unsigned char *q = header; q < nameEnd; q++) {
    if (*q == '/') base = q + 1;
  }
  return base + 2 <= nameEnd && base[0] == '.' && base[1] == '_';
}

const unsigned char *findTarHeader(const unsigned char *cp, const unsigned char *base, const unsigned char *end) {
  const unsigned char *p = base + (((cp - base) + 511) & ~511);
  while (p < end) {
    const unsigned char *header = p;
    if (p + 157 <= end && (p[156] == 'x' || p[156] == 'X')) {
      long paxSize = parseTarOctalField(p + 124, 12);
      const unsigned char *paxData = p + 512;
      header = (paxSize > 0 && paxData + paxSize <= end)
                   ? paxData + ((static_cast<std::size_t>(paxSize) + 511) / 512) * 512
                   : p + 512;  // 不正な形のPAXレコード――ベストエフォート、単に通り過ぎる
    }
    if (header >= end || !isSidecarHeader(header, end)) return header;
    long entrySize = parseTarOctalField(header + 124, 12);
    std::size_t entryBlocks = 1 + (static_cast<std::size_t>(std::max<long>(0, entrySize)) + 511) / 512;
    p = header + entryBlocks * 512;
  }
  return end;
}

void TarReader::locateContent(const unsigned char *data, std::size_t size,
                               std::vector<ArchiveEntry> &entries) const {
  if (entries.empty() || !size) return;
  const unsigned char *end = data + size - 1;  // alloc_tar()の`end--`からの移植
  const unsigned char *cp = data;

  for (auto &entry : entries) {
    if (cp < end) cp = findTarHeader(cp, data, end);
    cp += 512;
    if (cp > end) cp = end;
    entry.contentOffset = cp - data;
    if (entry.isDirectory) continue;
    long remaining = entry.size;
    while (remaining && cp < end) { cp++; remaining--; }
    if (cp == end) entry.contentTruncated = true;
  }
}

// alloc.cのalloc_head() T_unzipケースからの移植、これを実際の
// `unzip -l`出力に対してテストしていて見つかった2つの訂正付き:
//
// 1. ディレクトリの検出: legacyは、tar/rpmのls風の出力に対しては
//    意味があるが`unzip -l`の行(そのような列が全く無い)に対して
//    は決して真にならないモード文字のチェック('d'/'l'が整形された
//    表示行の先頭にあるか)を再利用していた。代わりに名前が'/'で
//    終わることでディレクトリを検出する――legacyが既にLHAに対して
//    使っている(alloc_head()のT_lhaケース参照)のと同じ技法――は、
//    単純で、正しく、姉妹のリーダーとも一貫している。
//
// 2. フッターの要約行(例えば「       12          2 files」、
//    閉じる「---------」のルールの後に印字される)は、実際のエント
//    リ行(先頭の空白、続いて数字)とまさに同じ形をしており、空の
//    名前を持つ偽のエントリとしてパースされてしまっていた――legacy
//    には同じ弱点がある(そのバージョンは同じ箇所でゴミ/空の内容を
//    持つ偽のエントリを生成する)。unzip -lの一覧は、正確に2つの
//    「---------」ルール行(エントリブロックを開くものと閉じるもの)
//    を持つ。これは、ルール行を個別に認識するのではなく2つ目の
//    ところで止まるので、その後のフッターに全く到達しない。
std::vector<ArchiveEntry> ZipReader::parseListing(const std::string &listing) const {
  std::vector<ArchiveEntry> entries;
  int ruleLinesSeen = 0;
  for (const auto &rawLine : splitLines(listing)) {
    std::size_t i = 0;
    while (i < rawLine.size() && rawLine[i] == ' ') i++;
    if (i < rawLine.size() && rawLine[i] == '-') {
      if (++ruleLinesSeen >= 2) break;  // 閉じるルール――フッターの前で止まる
      continue;
    }
    if (ruleLinesSeen < 1) continue;  // まだ「Archive:」/列ヘッダーの前置き部分
    if (i >= rawLine.size() || !std::isdigit(static_cast<unsigned char>(rawLine[i]))) continue;

    long size = 0;
    while (i < rawLine.size() && rawLine[i] != ' ') {
      size = size * 10 + (rawLine[i] - '0');
      i++;
    }
    // NAMEに到達するためDATEとTIMEの列をスキップする。
    for (int field = 0; field < 2; field++) {
      while (i < rawLine.size() && rawLine[i] == ' ') i++;
      while (i < rawLine.size() && rawLine[i] != ' ') i++;
    }
    while (i < rawLine.size() && rawLine[i] == ' ') i++;

    ArchiveEntry entry;
    entry.name = rawLine.substr(i);
    entry.size = size;
    entry.isDirectory = !entry.name.empty() && entry.name.back() == '/';
    entries.push_back(std::move(entry));
  }
  return entries;
}

// alloc_unzip()からの移植: 生のバイト列は、一覧順での、全エントリの
// 内容の展開済みの連結である(`unzip -p <archive>`が、全メンバーを
// 標準出力へ次々と展開することで実際に生み出すものに一致する)
// ――tarとは異なり、探すべきエントリごとのヘッダーは無い。
void ZipReader::locateContent(const unsigned char *data, std::size_t size,
                               std::vector<ArchiveEntry> &entries) const {
  const unsigned char *end = data + size;
  long pos = 0;
  for (auto &entry : entries) {
    if (entry.isDirectory) continue;
    entry.contentOffset = pos;
    long remaining = entry.size;
    while (remaining && data + pos < end) { pos++; remaining--; }
    if (data + pos == end) entry.contentTruncated = true;
  }
}

// alloc.cのalloc_head() T_lhaケースからの移植。legacyソース内の文書化
// されたヘッダーフォーマットのコメント("PERMSSN UID GID SIZE RATIO
// STAMP NAME" / "----- ... -----")に合わせて構築されていた(移植時点
// では実際の`lha`バイナリが利用不可だった)。
//
// 2026-09-15修正、直接のユーザー報告("src/にlzhファイルがあったはず
// だが、あれのARCがNG")+実際のLhasa版`lha`(Homebrew、v0.6.0)の
// 実機出力による調査で2件のバグを発見:
//
// (1) 比率マーカーの探索が'%'「と」'*'の両方を候補にしていたが、
//     Lhasaの実際の出力ではUID/GID欄が不明な場合"*****/*****"の
//     ように'*'で埋められる――この'*'が本物の比率マーカー('%'、
//     ずっと後ろの列)より「先に」見つかってしまい、そこを起点に
//     前方/後方への歩行が丸ごとズレて、SIZE/NAMEが完全に別の場所
//     (STAMPの残り+ゴミ)を指してしまっていた。'%'のみを探すように
//     変更した(旧ソースが本当に単独の'*'だけで比率を表す出力を
//     意図していたのだとしても、UID/GID欄の'*'より必ず後ろにある
//     はずの本物の比率'%'を優先すべきだった)。
// (2) フッターの合計行("Total  N files  <size>  NN.N% <date>")も
//     '%'を含むため、(1)の修正後は代わりにこちらが誤ってエントリ
//     として拾われてしまっていた(名前欄が空文字列のまま、SIZEには
//     合計バイト数)――名前欄に何も残らない(前方への歩行が行末を
//     超える)行は、そもそも本物のエントリではないとして除外する
//     ようにした。
std::vector<ArchiveEntry> LhaReader::parseListing(const std::string &listing) const {
  std::vector<ArchiveEntry> entries;
  for (const auto &line : splitLines(listing)) {
    int n = static_cast<int>(line.size());
    int marker = 0;
    while (marker < n && marker < 40 && line[marker] != '%') marker++;
    if (marker >= 40 || marker >= n) continue;  // ヘッダー/ルール行、エントリではない

    // 比率マーカーから前方へ、STAMP(日付+時刻)を越えて、NAMEまで
    // 歩く。
    int ce = marker;
    for (int field = 0; field < 4; field++) {
      while (ce < n && line[ce] != ' ') ce++;
      while (ce < n && line[ce] == ' ') ce++;
    }
    if (ce >= n) continue;  // NAME欄が無い(フッターの合計行)、本物のエントリではない

    // 比率マーカーから後方へ、2フィールド(GID、UID)を越えて、SIZE
    // まで歩く。
    int cs = marker;
    for (int field = 0; field < 2; field++) {
      while (cs > 0 && line[cs] == ' ') cs--;
      while (cs > 0 && line[cs] != ' ') cs--;
    }
    cs++;

    long size = 0;
    while (cs < n && line[cs] != ' ') {
      size = size * 10 + (line[cs] - '0');
      cs++;
    }

    ArchiveEntry entry;
    entry.name = line.substr(ce);
    entry.size = size;
    entry.isDirectory = !entry.name.empty() && entry.name.back() == '/';
    entries.push_back(std::move(entry));
  }
  return entries;
}

// alloc_lha()からの移植: 生のバイトストリームでは、各エントリの
// 前に3行のヘッダー("::::::::"/ファイル名/"::::::::")がある。
// ディレクトリだけは例外で、それが全く無い。
void LhaReader::locateContent(const unsigned char *data, std::size_t size,
                               std::vector<ArchiveEntry> &entries) const {
  const unsigned char *end = data + size;
  const unsigned char *cp = data;

  auto skipLine = [&]() {
    while (cp < end && *cp != '\n') cp++;
    if (cp < end) cp++;
  };

  for (auto &entry : entries) {
    if (!entry.isDirectory) { skipLine(); skipLine(); skipLine(); }
    entry.contentOffset = cp - data;
    if (entry.isDirectory) continue;
    long remaining = entry.size;
    while (remaining && cp < end) { cp++; remaining--; }
    if (cp == end) entry.contentTruncated = true;
  }
}

// alloc.cのalloc_head() T_rpmケースからの移植、1つの訂正付き:
// legacyは数値のSIZEフィールドをチェックする前に3つの空白区切り
// フィールド(mode、links、owner)だけをスキップしており、代わりに
// GROUPに着地していた――それは決して数値ではないので、legacyの
// sizeは全てのRPMエントリに対して常に0だった。これを移植している
// 間に取得した実際の`rpm -qplv`の行("-rw-r--r--    1 root
// root    6 Jul 28 01:01 /usr/share/testpkg/file1.txt")に対して
// 確認済み: mode/links/owner/groupは4フィールドであり、TarReaderの
// BSDフォーマット分岐が既に正しくスキップしているのと同じ
// レイアウトに一致する。ここで3ではなく4フィールドをスキップする
// ことでそれを修正する。
std::vector<ArchiveEntry> RpmReader::parseListing(const std::string &listing) const {
  std::vector<ArchiveEntry> entries;
  for (const auto &rawLine : splitLines(listing)) {
    std::string line = rawLine;
    auto arrow = line.find(" -> ");
    if (arrow != std::string::npos) line.resize(arrow);

    int n = static_cast<int>(line.size());
    int ce = n - 1;
    while (ce > 0 && line[ce] != ' ') ce--;
    int cs = ce + 1;

    ce = 0;
    for (int field = 0; field < 4; field++) {
      while (ce < n && line[ce] != ' ') ce++;
      while (ce < n && line[ce] == ' ') ce++;
    }

    long size = 0;
    bool special = false;
    if (ce < n && std::isdigit(static_cast<unsigned char>(line[ce]))) {
      while (ce < n && line[ce] != ' ') {
        if (line[ce] == ',') special = true;
        else if (!special) size = size * 10 + (line[ce] - '0');
        ce++;
      }
    }

    ArchiveEntry entry;
    entry.name = (cs <= n) ? line.substr(cs) : std::string();
    entry.size = special ? 0 : size;
    entry.isSpecialDevice = special;
    entry.isDirectory = !line.empty() && line[0] == 'd';
    entries.push_back(std::move(entry));
  }
  return entries;
}

// 実際の`lsar -l`のキャプチャからの移植(クラス自身の宣言コメント
// 参照)――legacyの先例は無い、RARはこのプロジェクトのオリジナルC
// 時代を完全に過ぎてから登場したもの。実際の出力の形:
//
//   /path/to/archive.rar: RAR
//        Flags  File size   Ratio  Mode  Date       Time   Name
//        =====  ==========  =====  ====  ========== =====  ====
//     0. -----       12292  91.6%  Nr29  2009-10-03 00:23  Extra/.DS_Store
//   422. D----           0  -----  None  2009-10-03 00:18  a/directory/
//   (Flags: D=Directory, R=Resource fork, L=Link, E=Encrypted, ...)
//   (Mode: Nr29=Normal v2.9)
//
// フィールドは(SevenZipReader自身の一覧とは異なり)固定幅の列では
// なく空白区切りである――index/flags/size/ratio/mode/date/timeは、
// 位置的に通り過ぎて歩く7個のトークンである(上でTarReader/
// ZipReader/LhaReaderが既に使っているのと同じ「N個の空白区切り
// フィールドをスキップする」というイディオム)。行に残っている
// 全て(空白も含む)がNAMEとして取られる。
std::vector<ArchiveEntry> RarReader::parseListing(const std::string &listing) const {
  std::vector<ArchiveEntry> entries;
  bool inTable = false;
  for (const auto &line : splitLines(listing)) {
    std::size_t firstNonSpace = line.find_first_not_of(' ');
    if (firstNonSpace == std::string::npos) continue;
    if (!inTable) {
      // "=====  ==========  ..."のルール行は、(本物のエントリが
      // 始まる前で)最初の非空白文字が'='である唯一の行である。
      if (line[firstNonSpace] == '=') inTable = true;
      continue;
    }
    if (line[firstNonSpace] == '(') break;  // "(Flags: ...)"/"(Mode: ...)"のフッターの注記

    std::size_t i = firstNonSpace;
    while (i < line.size() && line[i] != ' ') i++;  // index + '.'
    while (i < line.size() && line[i] == ' ') i++;
    std::size_t flagsStart = i;
    while (i < line.size() && line[i] != ' ') i++;  // flags(例えば"-----"/"D----")
    std::string flags = line.substr(flagsStart, i - flagsStart);
    while (i < line.size() && line[i] == ' ') i++;
    long size = 0;
    while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) {
      size = size * 10 + (line[i] - '0');
      i++;
    }
    // 残りの4フィールド(ratio%、mode、date、time)を実際にスキップ
    // するには4回ではなく5回の反復が必要: `i`はこのループに入る
    // 時点で既にSIZEの桁のすぐ後の空白の「上に」座っている(フィー
    // ルドの「開始」ではなく「境界」)ので、最初の反復自身の「非空白
    // をスキップする」ステップは何もせず、その「空白をスキップする」
    // 半分だけが実際の仕事をする――それは単に境界からratio自身の
    // 開始位置まで歩くだけで、ratio自体は消費しない。それ以降の
    // 「各」反復は正確に1フィールドずつ消費する(それ自身の「非空白
    // をスキップする」が、「前の」反復の「空白をスキップする」半分
    // が歩いて乗った先のフィールドを食べる)。正味の効果: 4つの
    // 本物のフィールドを消費するには4回ではなく5回のループが必要
    // ――実際に失敗するテスト(この修正の前、name フィールドが
    // "00:23  Extra/..."として届いていた――TIMEフィールドがnameに
    // くっついたままだった)で確認済み。
    for (int field = 0; field < 5; field++) {  // ratio%, mode, date, time
      while (i < line.size() && line[i] != ' ') i++;
      while (i < line.size() && line[i] == ' ') i++;
    }
    if (i >= line.size()) continue;

    ArchiveEntry entry;
    entry.name = line.substr(i);
    entry.size = size;
    entry.isDirectory = flags.find('D') != std::string::npos;
    entries.push_back(std::move(entry));
  }
  return entries;
}

// この逐次的な走査がここで有効な理由についてはクラス自身の宣言
// コメント参照(実際のアーカイブに対して検証済み: 展開された合計
// バイト数と最初のファイルの内容の両方が一覧と正確に一致した)。
void RarReader::locateContent(const unsigned char *data, std::size_t size,
                               std::vector<ArchiveEntry> &entries) const {
  const unsigned char *end = data + size;
  long pos = 0;
  for (auto &entry : entries) {
    if (entry.isDirectory) continue;
    entry.contentOffset = pos;
    long remaining = entry.size;
    while (remaining && data + pos < end) { pos++; remaining--; }
    if (data + pos == end) entry.contentTruncated = true;
  }
}

// 実際の`7z l`のキャプチャからの移植(クラス自身の宣言コメント
// 参照)――legacyの先例は無い、7zはこのプロジェクトのオリジナルC
// 時代を完全に過ぎてから登場したもの。実際の出力の形:
//
//    Date      Time    Attr         Size   Compressed  Name
//  ------------------- ----- ------------ ------------  ------------------------
//  2016-02-01 09:02:00 D....            0            0  config
//  2015-01-03 17:56:26 ....A         1142      2735622  7-Zip Extra License.txt
//  2016-02-03 01:09:59 ....A        26626               changelog.txt
//  ------------------- ----- ------------ ------------  ------------------------
//  2016-02-03 01:13:20           35479286      5787093  53 files, 5 folders
//
// RarReader自身の空白でトークン化されたフィールドとは「異なり」、
// このフォーマットは本当に「固定幅」の列を使う(ルール行自身の
// ダッシュの連続の長さに対して直接確認済み: date+timeが19文字、
// 続いて1文字幅のギャップ、attrが5文字、ギャップ、sizeが12文字、
// ギャップ、compressedが12文字、「その後」nameが列53から始まる
// 前に2文字幅のギャップ)――compressed-size列は、ソリッドブロック
// の後の方のメンバーに対しては完全に「空白」になり得る(上の
// "changelog.txt"参照)ので、これはRarReaderのやり方のように空白の
// 連続で安全にトークン化はできない。固定オフセットが、NAMEが実際
// にどこから始まるかを見つける唯一の信頼できる方法である。
std::vector<ArchiveEntry> SevenZipReader::parseListing(const std::string &listing) const {
  constexpr std::size_t kAttrStart = 20;
  constexpr std::size_t kSizeStart = 26;
  constexpr std::size_t kSizeEnd = 38;
  constexpr std::size_t kNameStart = 53;

  std::vector<ArchiveEntry> entries;
  int ruleLinesSeen = 0;
  for (const auto &line : splitLines(listing)) {
    if (line.size() >= 19 && line[0] == '-' && line.find_first_not_of("- ") == std::string::npos) {
      if (++ruleLinesSeen >= 2) break;  // 閉じるルール――「N files, M folders」フッターの前で止まる
      continue;
    }
    if (ruleLinesSeen < 1) continue;  // まだ「7-Zip [64]...」/列ヘッダーの前置き部分
    if (line.size() <= kNameStart) continue;

    std::string attr = line.substr(kAttrStart, kSizeStart - 1 - kAttrStart);
    long size = 0;
    for (std::size_t i = kSizeStart; i < kSizeEnd && i < line.size(); i++) {
      if (line[i] == ' ') continue;
      size = size * 10 + (line[i] - '0');
    }

    ArchiveEntry entry;
    entry.name = line.substr(kNameStart);
    entry.size = size;
    entry.isDirectory = !attr.empty() && attr[0] == 'D';
    entries.push_back(std::move(entry));
  }
  return entries;
}

// この逐次的な走査がここで有効な理由についてはクラス自身の宣言
// コメント参照(実際のアーカイブに対して検証済み: 展開された合計
// バイト数と最初のファイルの内容の両方が一覧と正確に一致した)。
void SevenZipReader::locateContent(const unsigned char *data, std::size_t size,
                                    std::vector<ArchiveEntry> &entries) const {
  const unsigned char *end = data + size;
  long pos = 0;
  for (auto &entry : entries) {
    if (entry.isDirectory) continue;
    entry.contentOffset = pos;
    long remaining = entry.size;
    while (remaining && data + pos < end) { pos++; remaining--; }
    if (data + pos == end) entry.contentTruncated = true;
  }
}

}  // namespace fm
