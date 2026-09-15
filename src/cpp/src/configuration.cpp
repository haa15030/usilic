#include "configuration.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace fm {

namespace {

const std::vector<std::string> &directiveKeywords() {
  // str.cのusilicrc[]テーブルからの移植、同じ順序(順序自体には意味は
  // 無い――ディレクティブはusilicrc内でどの順序でも現れてよい――
  // が、str.cとの照合を容易にするために保持されている)。
  static const std::vector<std::string> kKeywords = {
      "ARCHIVE", "FILE", "EXEC",   "EDITOR", "SHELL", "GREP", "PROGRAM", "BUFFER", "CODE",
      "HISTORY", "WAIT", "SCROLL", "LS",     "KEY",   "CP",   "MV",      "RM",
      // 2026-09-12追加、直接のユーザー依頼(columnThreshold()自身の
      // コメント参照): このC++移植自身の新設ディレクティブなので
      // legacy str.cのusilicrc[]には対応物が無い――既存の並びの
      // 末尾に追記する(既存インデックスの番号を一切ずらさないため)。
      "COLUMN",
  };
  return kKeywords;
}

std::vector<std::string> splitLines(const std::string &text) {
  std::vector<std::string> lines;
  std::size_t start = 0;
  while (start <= text.size()) {
    std::size_t nl = text.find('\n', start);
    if (nl == std::string::npos) {
      if (start < text.size()) lines.push_back(text.substr(start));
      break;
    }
    lines.push_back(text.substr(start, nl - start));
    start = nl + 1;
  }
  return lines;
}

// 空白、コメントのみ、または空白文字で始まる行は、新しいディレク
// ティブキーワードではなく、現在のセクションの「継続」である
// ――init_config()の`strchr(s_S(S_nop), *str)`チェックからの移植、
// s_S(S_nop)はリテラルの"# \t\n"である。
bool isContinuationLine(const std::string &line) {
  if (line.empty()) return true;
  char c = line[0];
  return c == '#' || c == ' ' || c == '\t';
}

std::string firstToken(const std::string &line) {
  std::size_t end = line.find_first_of(" \t");
  return (end == std::string::npos) ? line : line.substr(0, end);
}

// get_cp()からの移植: 最初の'#'(コメント)で切り詰め、その後先頭の
// 空白/タブを取り除く。結果が空(空白行またはコメントのみの行)の
// 場合はnulloptを返す。注記(単純化ではなく、legacyに正確に合わせ
// ている): クオートされたトークン内の'#'はここでは保護され「ない」
// ――それはKEYセクションのget_str()にとってのみ重要であり、それは
// このクラスがパースしない(クラスコメント参照)。
std::optional<std::string> trimCommentAndLeadingWs(const std::string &rawLine) {
  std::string line = rawLine;
  std::size_t hash = line.find('#');
  if (hash != std::string::npos) line.erase(hash);
  std::size_t start = line.find_first_not_of(" \t");
  if (start == std::string::npos) return std::nullopt;
  return line.substr(start);
}

// save_child()のトークンごとのループからの移植: '...'または
// "..."でクオートされた範囲(同じクオート文字の次の出現まで逐語的
// に取られる、エスケープ無し)、または裸の空白区切りのトークン。
//
// 2026-08-24の修正(docs/CHANGES.md項目417): ここでは' 'だけが区切り
// として扱われており、'\t'は決して扱われていなかった――しかし
// usilicrc自体(このリポジトリ自身が同梱するテンプレートと、実際の
// 全ユーザー設定の両方)は、FILE/EXEC/ARCHIVEディレクティブのフィー
// ルドを空白ではなく実際のTAB文字で区切っている(実際の
// ~/.usilic/usilicrcに対して`sed -n 'l'`で確認済み:
// "\t.mp4\topen\t-a\tIINA$")。空白が全く無いタブ区切りの行は、
// (埋め込まれたタブも全て込みで)「1つの」単一のトークンになって
// しまい、それをparseSuffixLine()の`allTokens.size() < 2`ガードが
// 「コマンドが全く無いsuffix」として黙って捨てていた――ディレク
// ティブ「全体」が、エラーもメッセージも何も無く、Configurationから
// 消えていた。直接のユーザー報告がこれを具体的に捉えた: `.mp4`用の
// EXECエントリ(`open -a IINA`、別の問題を回避するために追加された、
// 項目416参照)が全く発火しなかった――`.mp4`ファイルへのEnterは、
// この「黙って消える」バグがまさに生み出す症状の通り、単にプレーン
// な内容表示へ落ちていった。他の全てのディレクティブキーワード
// 自身のヘルパー(trimCommentAndLeadingWs()/firstToken()、すぐ上)
// は既に' '/'\t'を同等の空白として扱っている――tokenize()だけが
// 例外だった。下の、先頭の空白をスキップするループと、空白まで
// 読むループの両方で、'\t'を' 'と並ぶ通常の区切りとして扱うことで
// 修正した(クオートされた範囲はどちらにせよ影響を受けない。legacy
// 自身のsave_child()の意味論に合わせている――クオートされたトークン
// は、本当にそれを望む人がいれば依然としてリテラルなタブを含める)。
std::vector<std::string> tokenize(const std::string &s) {
  std::vector<std::string> out;
  std::size_t i = 0;
  while (i < s.size()) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
    if (i >= s.size()) break;
    if (s[i] == '\'' || s[i] == '"') {
      char quote = s[i];
      i++;
      std::size_t start = i;
      while (i < s.size() && s[i] != quote) i++;
      out.push_back(s.substr(start, i - start));
      if (i < s.size()) i++;  // 閉じるクオートをスキップする
    } else {
      std::size_t start = i;
      while (i < s.size() && s[i] != ' ' && s[i] != '\t') i++;
      out.push_back(s.substr(start, i - start));
    }
  }
  return out;
}

// 空白でも無くコメントのみでも無いデータ行が見つかるまで(そのトリ
// ムされたテキストを返し、`i`をその直後に残す)、またはディレクティ
// ブキーワード行/EOFに到達するまで(nulloptを返し、`i`はそのキー
// ワード行を指すか、lines.size()と等しいまま残す)継続行を消費する。
std::optional<std::string> nextDataLine(const std::vector<std::string> &lines, std::size_t &i) {
  while (i < lines.size() && isContinuationLine(lines[i])) {
    auto trimmed = trimCommentAndLeadingWs(lines[i]);
    i++;
    if (trimmed) return trimmed;
  }
  return std::nullopt;
}

// get_val()からの移植: nextDataLine()と似ているが、10進数字で
// 始まらないデータ行をスキップし、結果を最初の空白/タブで切り
// 詰める(なので"27 trailing junk"は"27"になる)。
std::optional<std::string> nextDigitToken(const std::vector<std::string> &lines, std::size_t &i) {
  while (i < lines.size() && isContinuationLine(lines[i])) {
    auto trimmed = trimCommentAndLeadingWs(lines[i]);
    i++;
    if (!trimmed || trimmed->empty()) continue;
    if (!std::isdigit(static_cast<unsigned char>((*trimmed)[0]))) continue;
    std::size_t sp = trimmed->find_first_of(" \t");
    return (sp == std::string::npos) ? *trimmed : trimmed->substr(0, sp);
  }
  return std::nullopt;
}

// save_child()のパイプ分割からの移植: '|'は最大1個まで許される。
// その前の部分は上流のパイプライン段になり、後の部分(同じsuffix
// に再度紐付けられる)がメインのコマンドになる。
struct SuffixLine {
  std::string suffix;
  ProgramCommand command;
};

std::optional<SuffixLine> parseSuffixLine(const std::string &trimmedLine, std::vector<std::string> &errors) {
  // suffixを切り分ける前に行「全体」をトークン化する(クオートを
  // 認識する)――suffixトークン自体もクオートされ得る(save_child()
  // はその最初のトークンの抽出を特別扱いしない)ので、(この関数の
  // 以前のバージョンがそうしていたように)最初に生の空白で分割する
  // と、`'.pdf' 'xpdf' '-remote'`のような行で壊れる。
  auto allTokens = tokenize(trimmedLine);
  if (allTokens.size() < 2) return std::nullopt;  // コマンドが全く無いsuffix
  std::string suffix = allTokens[0];
  std::vector<std::string> rest(allTokens.begin() + 1, allTokens.end());

  auto pipeIt = std::find(rest.begin(), rest.end(), "|");
  std::optional<std::vector<std::string>> pipedFrom;
  std::vector<std::string> argv;
  if (pipeIt != rest.end()) {
    if (std::find(std::next(pipeIt), rest.end(), "|") != rest.end()) {
      errors.push_back("more than one '|' in directive line: " + trimmedLine);
      return std::nullopt;
    }
    pipedFrom = std::vector<std::string>(rest.begin(), pipeIt);
    argv = std::vector<std::string>(std::next(pipeIt), rest.end());
  } else {
    argv = rest;
  }
  if (argv.empty()) return std::nullopt;
  SuffixLine result;
  result.suffix = suffix;
  result.command.argv = argv;
  result.command.pipedFromArgv = pipedFrom;
  return result;
}

#ifdef __APPLE__
// setUserLocateDatabasePath()自身の`path`(絶対ファイルシステムパス、
// 攻撃者が制御できるものではないが、それでも防御的にシングルクオート
// される――さもないとその中の迷った`'`は、それが埋め込まれる
// `sh -c`スクリプトを壊してしまう)向けのクオート処理。
// application.cpp自身のshellQuoteArg()(別の翻訳単位で、共有されて
// いない――このファイルは、同じ理由(parseSuffixLine()の上参照)で
// 既に自身の小さなローカルヘルパーを持っている)と同じPOSIXの
// シングルクオートエスケープの形。
std::string shellQuoteSinglePath(const std::string &path) {
  std::string quoted = "'";
  for (char c : path) {
    if (c == '\'')
      quoted += "'\\''";
    else
      quoted += c;
  }
  quoted += "'";
  return quoted;
}
#endif

}  // namespace

Configuration::Configuration() {
  // config.hの組み込みデフォルトは"du -ab"(全ファイル、バイト単位
  // で正確)だった――GNU専用なので、macOSのBSD版duではそもそも起動
  // すらできなかった(CLAUDE.mdの「今後の大きな方向性」節、
  // 2026-07-22の項目参照)。"-a"(全ファイル)はPOSIX/移植可能だが、
  // "-b"(バイト単位で正確)はそうではなく、それを丸ごと落とすと
  // 各プラットフォーム自身のデフォルトのブロックサイズ(BSDでは
  // 512バイト、GNUでは1024バイト)が黙って食い違ったままになって
  // しまう――"-k"(キロバイトブロック)はPOSIXで移植可能「かつ」
  // DuModeに両プラットフォームでパースするための単一の、一貫した
  // 単位を与えるので、それら片方だけでバイト単位の正確なサイズを
  // 保とうとする代わりにここで使われている。
  roleCommands_[ConfiguredRole::Du] = {{"du", "-ak"}, std::nullopt};
  // lsの正確な設定デフォルトはconfig.hに直接与えられていない
  // (DU/CP/MV/RM/CHARSET/TMP/LIBだけがある)――get_col()(scroll.c)
  // 自身の、`ls`出力が`ls -alF`フォーマットであるという文書化された
  // 前提から推測されている(CLAUDE.mdの履歴項目13参照)。
  roleCommands_[ConfiguredRole::Ls] = {{"ls", "-alF"}, std::nullopt};
  roleCommands_[ConfiguredRole::Cp] = {{"cp", "-vRi"}, std::nullopt};   // config.hのCPデフォルト
  roleCommands_[ConfiguredRole::Mv] = {{"mv", "-vi"}, std::nullopt};    // config.hのMVデフォルト
  roleCommands_[ConfiguredRole::Rm] = {{"rm", "-vri"}, std::nullopt};   // config.hのRMデフォルト
  // groffの実際のlegacyデフォルトは、実行時のgroffバージョンの探り
  // (-Rか-Tutf8スタイルのフラグかを選ぶための)を含んでおり、ここ
  // では再現されていない。これは妥当な固定のデフォルトであり、
  // 逐語的な移植ではない。2026-09-20追加: `-mandoc`(manマクロ/mdoc
  // マクロを自動判別して読み込む)が無いと、looksLikeRoffDocument()
  // が検出する`.TH`/`.SH`等はgroffにとって単なる未定義マクロで
  // あり、警告付きで無視されるだけで章立てが一切反映されない
  // ――このrole自体が2026-09-20まで一度も実際には呼ばれておらず
  // (定義のみ)、この不備が今まで表面化していなかった。
  // `-Kutf8`(入力エンコーディングの明示、preconv向け)が無いと、
  // 環境によっては日本語部分が文字化けする(実機検証で確認済み――
  // "invalid input character code"警告と共に化ける)。
  // 2026-09-15追加、直接のユーザー依頼("usilic.1を表示の際の
  // エスケープシーケンスの反映もよろしく"、docs/ideas/IDEAS.md
  // "PAGERモードがANSIエスケープシーケンスを解釈せず生のまま表示
  // してしまう"の同種の症状): groffの-Tutf8出力デバイスは、太字/
  // 下線を(legacy自身のnroffが使う"X\bX"/"_\bX"重ね打ち規約ではなく)
  // ANSI SGRエスケープ(実機確認: 太字は"\x1b[1m"..."\x1b[0m"、下線は
  // "\x1b[4m"..."\x1b[24m")で表現する――このポートのPipeBuffer::
  // wrapLine()は前者(legacy由来の重ね打ち規約)だけを検出するため、
  // 生のANSIバイト列がそのまま(端末には正しく解釈されるにせよ)
  // 幅計算・検索・AttributeRun生成のどれからも素通りしてしまって
  // いた。`GROFF_NO_SGR=1`(groff自身がサポートする環境変数、実機で
  // 動作確認済み)は出力を"X\bX"/"_\bX"の重ね打ちへ切り替える――
  // ChildProcessOptionsに新しい汎用の環境変数機構を追加する代わりに、
  // `env`経由でこの1個の変数だけをgroffの子プロセスへ渡す、既存の
  // 仕組みだけで完結する最小の変更。
  roleCommands_[ConfiguredRole::Groff] = {
      {"env", "GROFF_NO_SGR=1", "groff", "-mandoc", "-Kutf8", "-Tutf8"}, std::nullopt};
  roleCommands_[ConfiguredRole::Man] = {{"man"}, std::nullopt};
  roleCommands_[ConfiguredRole::Locate] = {{"locate"}, std::nullopt};
  // K-39(^Jl、「LOCATE: Execute `locate'」)、docs/ideas/IDEAS.mdの
  // LOCATE節に基づく: legacyにはupdatedbリフレッシュ機能が全く無い
  // (m_locate()は単に検索を再実行/再表示するだけ)――このroleと
  // LocateMode::updateDb()は、移植ではなく、その上に追加された
  // 新機能である。Linuxの素の`updatedb`デフォルト。macOSにはPATH上
  // に裸の`updatedb`が全く無い――等価なプログラムは
  // `/usr/libexec/locate.updatedb`である(CLAUDE.mdの履歴項目7
  // 参照)。2026-08-13の訂正: このコメントの以前のバージョンは、
  // その違いをDU自身の追跡されていないmacOSフラグの不一致と同じ
  // ものとして扱い、「既知の、意図的に対応していないギャップ」と
  // 呼んでいた――直接のユーザーによる訂正が、その推論が誤っている
  // ことを指摘した: "updatedbの機能を果たすプログラムがあるのに
  // なぜスコープ外なの？"(このプラットフォームでupdatedbが行う
  // ことを行うプログラムは「存在する」のに、なぜそれをスコープ外
  // として扱うのか?)――このroleは、文字列"updatedb"に逐語的に
  // 一致させるためではなく、「現在のプラットフォームでこの『機能』
  // を果たすプログラムが何であれ」実行するために存在する。#ifdef
  // __APPLEを介して分岐しており、この移植自身の既存のMac専用パスの
  // 慣習(mac_font_resolver.cpp/mac_cursor.mm参照)に合わせている。
  // それでも通常、/var/db/locate.databaseへ実際に書き込むには
  // rootが必要である――ここでは意図的に`sudo`で包んでいない
  // (この移植が生成する子プロセスからの対話的なパスワードプロンプ
  // トは、推測で済ませるのではなくユーザーへ差し戻すべき、本物の、
  // 別の設計/セキュリティ上の疑問である)。これをroot以外のユーザー
  // として実行すると、依然としておそらく権限エラーで失敗する。ただ
  // それは「command not found」ではなく、正しいバイナリからの本物の
  // エラーというだけである。
#ifdef __APPLE__
  roleCommands_[ConfiguredRole::UpdateDb] = {{"/usr/libexec/locate.updatedb"}, std::nullopt};
#else
  roleCommands_[ConfiguredRole::UpdateDb] = {{"updatedb"}, std::nullopt};
#endif
  // 2026-09-05変更、直接のユーザー依頼("fgrepとegrepの代わりにgrep -F
  // とgrep -Eで代用"): `fgrep`/`egrep`は独立したバイナリとしては非推奨
  // 扱いになりつつある(多くのディストリでは`grep`へのシンボリック
  // リンクに過ぎず、GNU grepは実行時に"fgrep/egrep is obsolete"と警告
  // する)。`grep -F`/`grep -E`は同じ機能を、廃止予定でない形で提供する
  // ――引数の意味(-nは行番号、-eはパターンを明示するフラグでパターン
  // 自体が"-"始まりでもオプションと誤認されない)は変えていない。
  roleCommands_[ConfiguredRole::Egrep] = {{"grep", "-En"}, std::nullopt};
  roleCommands_[ConfiguredRole::Fgrep] = {{"grep", "-Fne"}, std::nullopt};
  // 2026-09-05追加(直接のユーザー依頼、"新たにGREP（grep）を新設"):
  // プレーンな`grep`(基本正規表現、-F/-Eどちらのフラグも無し)。
  roleCommands_[ConfiguredRole::Grep] = {{"grep", "-n"}, std::nullopt};
  roleCommands_[ConfiguredRole::GzipDecompress] = {{"gzip", "-dc"}, std::nullopt};
  roleCommands_[ConfiguredRole::Bzip2Decompress] = {{"bzip2", "-d"}, std::nullopt};
  roleCommands_[ConfiguredRole::Tar] = {{"tar"}, std::nullopt};
  roleCommands_[ConfiguredRole::LhaExtract] = {{"lha", "p"}, std::nullopt};
  roleCommands_[ConfiguredRole::UnzipExtract] = {{"unzip", "-p"}, std::nullopt};
  roleCommands_[ConfiguredRole::Rpm] = {{"rpm"}, std::nullopt};
  roleCommands_[ConfiguredRole::Rpm2Cpio] = {{"rpm2cpio"}, std::nullopt};
  roleCommands_[ConfiguredRole::DebExtract] = {{"ar", "p"}, std::nullopt};
  // unar(独占的な`unrar`ではない)――ConfiguredRole::RarExtract自身の
  // 宣言コメント参照。"-o -"は標準出力へ展開し、"-q"はunarがさもな
  // ければ実際に展開されるバイト列と混ぜてしまうエントリごとの進捗
  // テキストを抑制する。
  roleCommands_[ConfiguredRole::RarExtract] = {{"unar", "-f", "-o", "-", "-q"}, std::nullopt};
  roleCommands_[ConfiguredRole::SevenZipExtract] = {{"7z", "x", "-so"}, std::nullopt};

  // str.cの組み込みARCHIVE[]テーブルからの移植、逐語的な
  // suffix->commandの対応付け。
  auto addBuiltinArchive = [&](const std::string &suffix, ArchiveKind kind, ProgramCommand command) {
    SuffixEntry e;
    e.suffix = suffix;
    e.command = std::move(command);
    e.archiveKind = kind;
    builtinArchiveEntries_.push_back(e);
    suffixEntries_.push_back(e);
  };
  addBuiltinArchive(".tar", ArchiveKind::Tar, {{"tar", "tvf"}, std::nullopt});
  addBuiltinArchive(".tgz", ArchiveKind::Tar, {{"tar", "tvf", "-"}, std::vector<std::string>{"gzip", "-dc"}});
  addBuiltinArchive(".tar.bz2", ArchiveKind::Tar,
                     {{"tar", "tvf", "-"}, std::vector<std::string>{"bzip2", "-dc"}});
  // 2026-08-26の追加(直接のユーザー依頼、src/へ投入された実際の
  // .xz/.rar/.7zフィクスチャでテスト): すぐ上の.tgz/.tar.bz2と同じ
  // 形――xz -dcが素のtarストリームへ展開し、TarReaderがそれを他の
  // どんなtar一覧とも全く同じようにパースする。legacyの先例は無い
  // (xzはこのプロジェクトのオリジナルC時代を10年以上過ぎてから
  // 登場した)。
  addBuiltinArchive(".tar.xz", ArchiveKind::Tar,
                     {{"tar", "tvf", "-"}, std::vector<std::string>{"xz", "-dc"}});
  // 2026-09-15追加(上の.tar.xzと同じ理由での直接のユーザー依頼、
  // "対応可/不可なsuffixは知りたい"に対する回答の一環で追加)。zstdは
  // このマシンに実際にインストール済みであることを確認済み。legacy
  // の先例は無い(xz同様、このプロジェクトのオリジナルC時代を過ぎて
  // から登場したフォーマット)。usilicrc側のARCHIVEエイリアス機構
  // (findBuiltinArchiveAlias())では、既存の組み込みへの別名追加しか
  // できず、この`zstd -dc`のような新しいコマンド自体をゼロから
  // 定義することはできないため、.tzst等の追加suffixはこの組み込み
  // エントリへのエイリアスとして使う想定(../usilicrc自身のARCHIVE
  // 節コメント参照)。
  addBuiltinArchive(".tar.zst", ArchiveKind::Tar,
                     {{"tar", "tvf", "-"}, std::vector<std::string>{"zstd", "-dc"}});
  // legacy自身の組み込みテーブルは単に".lzh lha"(明示的な一覧表示
  // フラグ無し)を列挙している――これに対して実際の呼び出しを検証
  // すべき`lha`バイナリがこのプロジェクトの歴史のどこにも利用可能
  // でなかった(同じ理由によるCLAUDE.mdの履歴項目22、LhaReader自身
  // の合成フィクスチャの例外参照)ので、推測ではなく逐語的に移植
  // されている。
  addBuiltinArchive(".lzh", ArchiveKind::Lha, {{"lha"}, std::nullopt});
  addBuiltinArchive(".zip", ArchiveKind::Zip, {{"unzip", "-l"}, std::nullopt});
  addBuiltinArchive(".rpm", ArchiveKind::Rpm, {{"rpm", "-qplv"}, std::nullopt});
  addBuiltinArchive(".deb", ArchiveKind::Deb, {{"ar", "tv"}, std::nullopt});
  // 2026-08-26の追加(上の.tar.xzと同じ直接のユーザー依頼、src/へ
  // 投入された実際の.rar/.7zフィクスチャ)。legacyの先例は無い――
  // 両フォーマットともこのプロジェクトのオリジナルC時代を完全に
  // 過ぎてから登場した。独占的な`unrar`ではなく`lsar`/`unar`
  // (The Unarchiverのコマンドラインツール)。理由についてはRarReader
  // /ConfiguredRole::RarExtract自身のコメント参照。
  addBuiltinArchive(".rar", ArchiveKind::Rar, {{"lsar", "-l"}, std::nullopt});
  addBuiltinArchive(".7z", ArchiveKind::SevenZip, {{"7z", "l"}, std::nullopt});
}

std::optional<ProgramCommand> Configuration::findBuiltinArchiveAlias(const std::string &searchText) const {
  // conf_arc()のusilicrc駆動の分岐からの移植: ユーザーのARCHIVE行は
  // 「<新しいsuffix> <検索テキスト>」であり、各組み込みエントリ
  // 自身の「<suffix> <argv...>」形式の行に対してsearchTextを前方
  // 一致させることで、組み込みテーブルと照合される。ゼロからの
  // コマンド定義ではなく、既存の組み込みへの純粋なエイリアスである。
  for (const auto &entry : builtinArchiveEntries_) {
    std::string formatted = entry.suffix;
    if (entry.command.pipedFromArgv) {
      for (const auto &tok : *entry.command.pipedFromArgv) formatted += " " + tok;
      formatted += " |";
    }
    for (const auto &tok : entry.command.argv) formatted += " " + tok;
    if (formatted.rfind(searchText, 0) == 0 || formatted.compare(0, searchText.size(), searchText) == 0) {
      return entry.command;
    }
  }
  return std::nullopt;
}

bool Configuration::loadRcText(const std::string &text, bool fileManagerMode) {
  auto lines = splitLines(text);
  const auto &keywords = directiveKeywords();
  std::size_t i = 0;

  while (true) {
    while (i < lines.size() && isContinuationLine(lines[i])) i++;
    if (i >= lines.size()) break;

    std::string keyword = firstToken(lines[i]);
    auto it = std::find(keywords.begin(), keywords.end(), keyword);
    if (it == keywords.end()) {
      errors_.push_back("unrecognized directive: " + lines[i]);
      return false;
    }
    int index = static_cast<int>(it - keywords.begin());
    i++;  // キーワード行自体を消費する

    switch (index) {
      case 0: {  // ARCHIVE
        while (auto data = nextDataLine(lines, i)) {
          std::size_t sp = data->find_first_of(" \t");
          if (sp == std::string::npos) continue;
          std::string newSuffix = data->substr(0, sp);
          std::string searchText = data->substr(sp + 1);
          std::size_t nonSpace = searchText.find_first_not_of(" \t");
          if (nonSpace == std::string::npos) continue;
          searchText = searchText.substr(nonSpace);
          if (auto aliased = findBuiltinArchiveAlias(searchText)) {
            SuffixEntry e;
            e.suffix = newSuffix;
            e.command = *aliased;
            for (const auto &builtin : builtinArchiveEntries_) {
              // エイリアス先のエントリのArchiveKindも復元する。
              if (builtin.command.argv == aliased->argv &&
                  builtin.command.pipedFromArgv == aliased->pipedFromArgv) {
                e.archiveKind = builtin.archiveKind;
                break;
              }
            }
            suffixEntries_.push_back(e);
          }
        }
        break;
      }
      case 1:    // FILE
      case 2: {  // EXEC
        bool exec = (index == 2);
        while (auto data = nextDataLine(lines, i)) {
          if (auto parsed = parseSuffixLine(*data, errors_)) {
            SuffixEntry e;
            e.suffix = parsed->suffix;
            e.command = parsed->command;
            e.isExec = exec;
            suffixEntries_.push_back(e);
          }
        }
        break;
      }
      case 3: {  // EDITOR
        std::vector<std::string> last;
        while (auto data = nextDataLine(lines, i)) {
          auto tokens = tokenize(*data);
          if (!tokens.empty()) last = tokens;
        }
        if (!last.empty()) editorProgram_ = last;
        break;
      }
      case 4: {  // SHELL
        std::vector<std::string> last;
        while (auto data = nextDataLine(lines, i)) {
          auto tokens = tokenize(*data);
          if (!tokens.empty()) last = tokens;
        }
        if (!last.empty()) shellProgram_ = last;
        break;
      }
      case 5: {  // GREP
        // 2026-09-11削除、直接のユーザー依頼(startGrepPrompt()自身の
        // コメント参照、"FGREPおよびEGREPを同じ仕様に統一...候補一覧は
        // 廃棄"): 以前はここでこのセクションの各パターンを解析し、
        // grepSeedPatterns_(EGREP/CUT-Regexの候補一覧の種)へ格納して
        // いたが、その候補一覧自体が上の統一で廃止されたため、格納先
        // ごと不要になった。GREP:ディレクティブ自体はdirectiveKeywords()
        // に残したまま(既存のusilicrcファイルにこのセクションが残って
        // いても「不明なディレクティブ」エラーにしないため)、内容は
        // nextDataLine()で読み飛ばすだけにする。
        while (nextDataLine(lines, i)) {
        }
        break;
      }
      case 6: {  // PROGRAM
        while (auto data = nextDataLine(lines, i)) {
          if (fileManagerMode) programSeedCommands_.push_back(*data);
        }
        break;
      }
      case 7: {  // BUFFER
        if (auto v = nextDigitToken(lines, i)) {
          int mb = std::atoi(v->c_str());
          if (mb > 100 || mb == 0) {
            errors_.push_back("BUFFER: value out of range (1-100): " + *v);
            mb = 1;
          }
          bufferSizeLimitBytes_ = mb * 1024 * 1024;
        }
        break;
      }
      case 8: {  // CODE
        if (auto data = nextDataLine(lines, i)) {
          std::string result;
          std::size_t p = 0;
          while (p < data->size()) {
            while (p < data->size() && !std::isdigit(static_cast<unsigned char>((*data)[p]))) p++;
            if (p >= data->size()) break;
            std::string numStr;
            for (int k = 0; k < 2 && p < data->size() && std::isdigit(static_cast<unsigned char>((*data)[p]));
                 k++, p++)
              numStr.push_back((*data)[p]);
            int code = std::atoi(numStr.c_str());
            if (code < 32) {
              if (code == 0 && result.empty()) break;
              result.push_back(static_cast<char>(code));
            }
          }
          allowedControlBytes_ = result;
        }
        break;
      }
      case 9: {  // HISTORY
        if (auto v = nextDigitToken(lines, i)) historyRetentionCount_ = std::atoi(v->c_str());
        break;
      }
      case 10: {  // WAIT
        if (auto v = nextDigitToken(lines, i)) waitIntervalMicros_ = std::atoi(v->c_str()) * 10000;
        break;
      }
      case 11: {  // SCROLL
        if (auto v = nextDigitToken(lines, i)) smoothScrollEnabled_ = std::atoi(v->c_str()) != 0;
        break;
      }
      case 12: {  // LS
        if (auto v = nextDigitToken(lines, i)) lsMode_ = std::atoi(v->c_str());
        break;
      }
      case 13: {
        // KEY――意図的にパースされない(クラスコメント参照)。それ
        // 自身のサブ文法は、セクション内にインデントされていない
        // マーカー行(F-0/K-?/M-?/P-?等)を持っており、汎用の
        // isContinuationLine()のインデントチェックはそれを次の
        // トップレベルディレクティブと誤読してしまう。そのため代わり
        // に正確なキーワード一致でスキップする: 最初のトークンが
        // 18個の既知のディレクティブキーワードのどれかに正確に等しい
        // 次の行までの全てを消費する。
        while (i < lines.size()) {
          if (std::find(keywords.begin(), keywords.end(), firstToken(lines[i])) != keywords.end()) break;
          i++;
        }
        break;
      }
      case 14:
      case 15:
      case 16: {  // CP / MV / RM
        ConfiguredRole role = (index == 14) ? ConfiguredRole::Cp : (index == 15) ? ConfiguredRole::Mv : ConfiguredRole::Rm;
        std::vector<std::string> last;
        while (auto data = nextDataLine(lines, i)) {
          auto tokens = tokenize(*data);
          if (!tokens.empty()) last = tokens;
        }
        if (!last.empty() && fileManagerMode) roleCommands_[role] = {last, std::nullopt};
        break;
      }
      case 17: {  // COLUMN(columnThreshold()自身のコメント参照)
        if (auto v = nextDigitToken(lines, i)) columnThreshold_ = std::atoi(v->c_str());
        break;
      }
      default:
        while (i < lines.size() && isContinuationLine(lines[i])) i++;
        break;
    }
  }
  return true;
}

const std::vector<std::string> &Configuration::errors() const { return errors_; }

std::optional<ProgramCommand> Configuration::programForSuffix(const std::string &filename,
                                                                ArchiveKind *archiveKindOut) const {
  const SuffixEntry *best = nullptr;
  for (const auto &entry : suffixEntries_) {
    if (entry.suffix.size() > filename.size()) continue;
    if (!std::equal(entry.suffix.rbegin(), entry.suffix.rend(), filename.rbegin())) continue;
    if (!best || entry.suffix.size() >= best->suffix.size()) best = &entry;
  }
  if (!best) return std::nullopt;
  if (archiveKindOut) *archiveKindOut = best->archiveKind;
  return best->command;
}

bool Configuration::isExec(const std::string &filename) const {
  const SuffixEntry *best = nullptr;
  for (const auto &entry : suffixEntries_) {
    if (entry.suffix.size() > filename.size()) continue;
    if (!std::equal(entry.suffix.rbegin(), entry.suffix.rend(), filename.rbegin())) continue;
    if (!best || entry.suffix.size() >= best->suffix.size()) best = &entry;
  }
  return best && best->isExec;
}

std::vector<std::string> Configuration::editorArgv(const std::string &path, int lineNumber) const {
  std::vector<std::string> result = editorProgram_;
  result.push_back("+" + std::to_string(lineNumber));
  result.push_back(path);
  return result;
}

std::vector<std::string> Configuration::shellArgv() const {
  std::vector<std::string> result = shellProgram_.empty() ? fallbackShellArgv_ : shellProgram_;
  if (result.empty()) return result;
  result.push_back("-i");
  return result;
}

void Configuration::setFallbackShellArgv(std::vector<std::string> argv) { fallbackShellArgv_ = std::move(argv); }

void Configuration::setUserLocateDatabasePath(const std::string &path) {
#ifdef __APPLE__
  roleCommands_[ConfiguredRole::LocateUserDb] = {{"locate", "-d", path}, std::nullopt};
  // /usr/libexec/locate.updatedb自身の実際のパイプライン
  // (`find -s $SEARCHPATHS ... -or -print | $mklocatedb -presort >
  // $tmp`、続いて`install $tmp $FCODES`)を忠実に反映する――ただし
  // "/"ではなく$HOMEにスコープされ、`su -fm nobody`ではなく実際の
  // ユーザー自身のアカウントで実行される(要点: `nobody`は`750`の
  // ホームディレクトリすら一覧できない、enum自身のコメント参照)。
  // `-s`はlocate.updatedb自身のフラグ(ソート済み出力、`-presort`
  // が要求する)に一致する。`.tmp`の兄弟ファイルへ構築し、
  // locate.updatedb自身の`install $tmp $FCODES`と同様、それを
  // アトミックに`mv`で所定の場所へ移す――そのため並行するLocateUserDb
  // 検索が、書きかけのデータベースを読むことは決してない。最後に
  // `chmod 600`: データベースは$HOME下の全パスの平坦な一覧(読み取り
  // 側が開けなかったファイルであってもメタデータは読める)であり、
  // ~/.usilic/自体は`755`(以前の、無関係なデフォルト――main()自身の
  // usilicDirのmkdir(2)コメント参照)なので、このファイルはディレク
  // トリのものを継承するのではなく、それ自身の明示的な制限を必要
  // とする。
  std::string tmpPath = path + ".tmp";
  std::string script = "find -s \"$HOME\" -print 2>/dev/null | /usr/libexec/locate.mklocatedb -presort > " +
                        shellQuoteSinglePath(tmpPath) + " && mv -f " + shellQuoteSinglePath(tmpPath) + " " +
                        shellQuoteSinglePath(path) + " && chmod 600 " + shellQuoteSinglePath(path);
  roleCommands_[ConfiguredRole::LocateUserDbBuild] = {{"sh", "-c", script}, std::nullopt};
#else
  (void)path;  // enum自身のコメント参照――Linux専用ビルドは両方のroleを未設定のままにする
#endif
}

ProgramCommand Configuration::commandFor(ConfiguredRole role) const {
  auto it = roleCommands_.find(role);
  return (it != roleCommands_.end()) ? it->second : ProgramCommand{};
}

void Configuration::setCommandForTest(ConfiguredRole role, ProgramCommand command) {
  roleCommands_[role] = std::move(command);
}

const std::vector<std::string> &Configuration::programSeedCommands() const { return programSeedCommands_; }

const std::string &Configuration::allowedControlBytes() const { return allowedControlBytes_; }
int Configuration::bufferSizeLimitBytes() const { return bufferSizeLimitBytes_; }
int Configuration::historyRetentionCount() const { return historyRetentionCount_; }
int Configuration::waitIntervalMicros() const { return waitIntervalMicros_; }
bool Configuration::smoothScrollEnabled() const { return smoothScrollEnabled_; }
int Configuration::lsMode() const { return lsMode_; }
int Configuration::columnThreshold() const { return columnThreshold_; }

void Configuration::setEnvironmentCharset(std::optional<std::string> envCharsetValue) {
  envCharsetValue_ = std::move(envCharsetValue);
}

bool Configuration::charsetExplicitlySet() const { return envCharsetValue_.has_value(); }

Charset::Encoding Configuration::defaultCharset() const {
  if (envCharsetValue_) {
    const std::string &v = *envCharsetValue_;
    if (v == "ascii") return Charset::Encoding::Ascii;
    if (v == "euc") return Charset::Encoding::Euc;
    if (v == "sjis") return Charset::Encoding::Sjis;
    if (v == "jis") return Charset::Encoding::Jis;
    if (v == "utf8" || v == "utf-8") return Charset::Encoding::Utf8;
  }
  // 未設定、空、または認識できない: 文書化された組み込みデフォルト
  // (config.hのCHARSET="utf-8")であり、Eucへの黙ったフォール
  // バックは決してしない――クラスコメントのCLAUDE.mdの履歴項目13
  // 参照。
  return Charset::Encoding::Utf8;
}

}  // namespace fm
