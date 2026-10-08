#include "configuration.hpp"

#include <unistd.h>
#include <cstdlib>
#include "key_bindings.hpp"
#include "platform.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace fm {

// configuration.hpp自身の宣言コメント参照(2026-09-27)。
std::string sevenZipCommand() {
  static const std::string name = [] {
    std::vector<std::string> dirs;
    if (const char *path = std::getenv("PATH")) {
      std::string all = path;
      std::size_t start = 0;
      while (start <= all.size()) {
        std::size_t colon = all.find(':', start);
        std::string dir = all.substr(start, colon == std::string::npos ? std::string::npos : colon - start);
        if (!dir.empty()) dirs.push_back(dir);
        if (colon == std::string::npos) break;
        start = colon + 1;
      }
    }
    dirs.push_back("/opt/homebrew/bin");  // Finderからの起動ではPATHに無い
    dirs.push_back("/usr/local/bin");
    for (const char *candidate : {"7z", "7zz"})
      for (const auto &dir : dirs)
        if (::access((dir + "/" + candidate).c_str(), X_OK) == 0) return std::string(candidate);
    return std::string("7z");
  }();
  return name;
}

namespace {

const std::vector<std::string> &directiveKeywords() {
  // str.cのusilicrc[]テーブルからの移植、同じ順序(順序自体には意味は
  // 無い――ディレクティブはusilicrc内でどの順序でも現れてよい――
  // が、str.cとの照合を容易にするために保持されている)。
  static const std::vector<std::string> kKeywords = {
      "ARCHIVE", "FILE", "EXEC",   "EDITOR", "SHELL", "GREP", "PROGRAM", "BUFFER", "CODE",
      // "LINE"は2026-10-02にHISTORYから改名(.steering/20261002-help-key-usilicrc-reorg/)。
      "LINE",    "WAIT", "SCROLL", "LS",     "KEY",   "CP",   "MV",      "RM",
      // 2026-09-12追加、直接のユーザー依頼(columnThreshold()自身の
      // コメント参照): このC++移植自身の新設ディレクティブなので
      // legacy str.cのusilicrc[]には対応物が無い――既存の並びの
      // 末尾に追記する(既存インデックスの番号を一切ずらさないため)。
      "COLUMN",
      // 2026-09-29追加(serifFontCandidates()自身のコメント参照)。
      "SERIF", "SANS-SERIF",
      // 2026-10-02追加(Configuration::speedValues()/macros()参照)。
      "SPEED", "DRAG", "FONT", "MACRO",
      // 2026-10-04追加(urlHandlers()参照)。
      "URL",
      // 2026-10-08追加(arrowSpeedValues()参照)。
      "ARROW",
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
  roleCommands_[ConfiguredRole::Cp] = {{"cp", "-vR"}, std::nullopt};   // config.hのCPデフォルト(2026-09-27、-iを外した: 直接のユーザー依頼)
  roleCommands_[ConfiguredRole::Mv] = {{"mv", "-v"}, std::nullopt};    // config.hのMVデフォルト(同上)
  roleCommands_[ConfiguredRole::Rm] = {{"rm", "-vr"}, std::nullopt};   // config.hのRMデフォルト(同上)
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
  // 2026-09-25修正: Egrep/Grepには-eが無く、"-"で始まるパターン(例えば
  // TUTORIALのEGREP例の"-[a-z]*v[a-z]*")が"grep: invalid option"になって
  // いた――Fgrepと同じく-eを付ける(パターンは常にargvの最後に付く、
  // GrepMode::search()/CutMode自身の呼び出し参照)。
  roleCommands_[ConfiguredRole::Egrep] = {{"grep", "-Ene"}, std::nullopt};
  roleCommands_[ConfiguredRole::Fgrep] = {{"grep", "-Fne"}, std::nullopt};
  // 2026-09-05追加(直接のユーザー依頼、"新たにGREP（grep）を新設"):
  // プレーンな`grep`(基本正規表現、-F/-Eどちらのフラグも無し)。
  roleCommands_[ConfiguredRole::Grep] = {{"grep", "-ne"}, std::nullopt};  // -eは上のEgrep自身のコメント参照
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
  roleCommands_[ConfiguredRole::SevenZipExtract] = {{sevenZipCommand(), "x", "-so"}, std::nullopt};

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
  addBuiltinArchive(".7z", ArchiveKind::SevenZip, {{sevenZipCommand(), "l"}, std::nullopt});

  // 2026-09-25追加、直接のユーザー依頼(ARCHIVEの要否の検討で): それまで
  // 同梱のusilicrcのARCHIVE節に頼っていた定番の別名を組み込みにした――
  // usilicrcが読めない環境でも、.tar.gz等がARCモード(開く/一覧/取り出し/
  // ^Oa/入れ子)で同じように扱われるように。別名は形式(一覧/取り出しの
  // コマンドと、ArchiveKindごとの読み取り方)を組み込みからそのまま借りる
  // だけ(ARCHIVE節と同じ)。ARCHIVE節は、.epub/.cbz等「中身が既存の形式」
  // という珍しい拡張子を利用者が足すために残す。
  auto addBuiltinAlias = [&](const std::string &newSuffix, const std::string &builtinSuffix) {
    for (const auto &builtin : builtinArchiveEntries_) {
      if (builtin.suffix != builtinSuffix) continue;
      SuffixEntry e = builtin;
      e.suffix = newSuffix;
      suffixEntries_.push_back(e);
      return;
    }
  };
  addBuiltinAlias(".tar.gz", ".tgz");
  addBuiltinAlias(".taz", ".tgz");
  addBuiltinAlias(".tar.z", ".tgz");
  addBuiltinAlias(".jar", ".zip");
  addBuiltinAlias(".tbz2", ".tar.bz2");
  addBuiltinAlias(".txz", ".tar.xz");
  addBuiltinAlias(".tzst", ".tar.zst");
  // 2026-09-29追加、直接のユーザー依頼("(ls)で^OeならARC"): .epubの中身はzip。
  // (ls)の[Enter]・FILE履歴等はApplication側でEPUBモードを優先し、ARCで開くのは
  // ^Oe(Application::openEpubAsArchive())だけ。
  addBuiltinAlias(".epub", ".zip");
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
    // 2026-09-27、直接のユーザー依頼("usilicrcのGREP（←「EGREP」に変更
    // よろしく）"): 節名はEGREPが正式。以前のusilicrcのGREPも同じ節として
    // 受け付ける(内部の添字は元のGREPのまま)。
    if (keyword == "EGREP") keyword = "GREP";
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
        // 2026-09-11に一度削除(FGREP/EGREPの統一で専用の候補一覧を廃止
        // したため、読み飛ばすだけにしていた)。2026-09-25再実装、直接の
        // ユーザー依頼("usilicrcのGREPをTUTORIAL版に置き換えて実装して"):
        // 格納先は廃止された候補一覧ではなく、統一後のEGREP/CUT(正規表現)
        // の履歴の一覧(Application::startPromptHistoryRecall()参照)。
        // legacy str.cのconf_grep()と同じく、各行の最初のトークン(引用符
        // '"'/'\''で始まれば対応する引用符まで、それ以外は最初の空白/タブ
        // まで)を積む。
        while (auto data = nextDataLine(lines, i)) {
          const std::string &line = *data;
          if (line.empty()) continue;
          std::string token;
          if (line[0] == '"' || line[0] == '\'') {
            std::size_t close = line.find_first_of("\"'", 1);
            token = line.substr(1, close == std::string::npos ? std::string::npos : close - 1);
          } else {
            token = line.substr(0, line.find_first_of(" \t"));
          }
          if (!token.empty()) {
            grepPresets_.push_back(token);
            std::string raw = lines[i - 1];  // nextDataLine()はコメントを除くので、表示には元の行を使う
            raw.erase(0, raw.find_first_not_of(" \t"));
            while (!raw.empty() && (raw.back() == '\r' || raw.back() == ' ' || raw.back() == '\t')) raw.pop_back();
            grepPresetLines_.push_back(raw);
          }
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
            mb = 64;  // 既定と同じ(bufferSizeLimitBytes_自身のコメント参照。legacyは1)
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
      case 9: {  // LINE(旧HISTORY)
        if (auto v = nextDigitToken(lines, i)) historyRetentionCount_ = std::atoi(v->c_str());
        break;
      }
      case 10: {  // WAIT
        if (auto v = nextDigitToken(lines, i)) waitIntervalMicros_ = std::atoi(v->c_str()) * 10000;
        break;
      }
      case 11:    // SCROLL
      case 12: {  // LS(どちらも2026-10-02に廃止、Configuration::overrideWait()の宣言のコメント参照)
        while (i < lines.size() && isContinuationLine(lines[i])) i++;
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
        //
        // 2026-09-25: 各行を解析するようになった(KeyBindings自身のコメント、
        // .steering/20260925-usilicrc-key-bindings/参照)。行の最初の
        // トークンが"F-n"/"K-n"なら、残りをそのキー表記として保持する
        // (空なら割り当て無し)。それ以外の行(旧書式の"F-?"/"K-?"等の見出し
        // やコメントだけの行)は無視する。キー表記自体の検査は、組み込みの
        // 表と突き合わせるKeyBindings::configure()が行う。
        while (i < lines.size()) {
          if (std::find(keywords.begin(), keywords.end(), firstToken(lines[i])) != keywords.end()) break;
          auto trimmed = trimCommentAndLeadingWs(lines[i]);
          i++;
          if (!trimmed) continue;
          std::string line = *trimmed;
          std::size_t sp = line.find_first_of(" \t");
          std::string head = line.substr(0, sp);
          std::string spec = (sp == std::string::npos) ? "" : line.substr(sp + 1);
          while (!spec.empty() && (spec.back() == ' ' || spec.back() == '\t')) spec.pop_back();
          if (head.size() >= 3 && (head[0] == 'F' || head[0] == 'K') && head[1] == '-' &&
              head.find_first_not_of("0123456789", 2) == std::string::npos) {
            int number = std::atoi(head.c_str() + 2);
            if (head[0] == 'F') keyPrefixSpecs_[number] = spec;
            else keyEntrySpecs_[number] = spec;
          }
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
      case 18:    // SERIF
      case 19: {  // SANS-SERIF(serifFontCandidates()自身のコメント参照)
        auto &candidates = (index == 18) ? serifFontCandidates_ : sansSerifFontCandidates_;
        candidates.clear();
        while (auto data = nextDataLine(lines, i)) {
          std::string path = *data;
          while (!path.empty() && (path.back() == ' ' || path.back() == '\t')) path.pop_back();
          if (!path.empty()) candidates.push_back(path);  // フォントファイルの絶対パス
        }
        break;
      }
      case 20:    // SPEED
      case 21:    // DRAG(speedValues()自身のコメント参照)
      case 25: {  // ARROW(arrowSpeedValues()参照)
        auto &values = (index == 20) ? speedValues_ : (index == 21) ? dragValues_ : arrowSpeedValues_;
        values.clear();
        while (auto data = nextDataLine(lines, i))
          for (const auto &token : tokenize(*data)) {
            if (token.empty() || token.find_first_not_of("0123456789") != std::string::npos) {
              errors_.push_back(std::string(index == 20 ? "SPEED" : index == 21 ? "DRAG" : "ARROW") +
                                ": not a number: " + token);
              continue;
            }
            values.push_back(std::atol(token.c_str()));
          }
        break;
      }
      case 22: {  // FONT
        while (auto data = nextDataLine(lines, i)) {
          std::string path = *data;
          while (!path.empty() && (path.back() == ' ' || path.back() == '\t')) path.pop_back();
          if (!path.empty()) fontPath_ = path;
        }
        break;
      }
      case 23: {  // MACRO(macros()自身のコメント参照)
        macros_.clear();
        while (i < lines.size()) {
          if (std::find(keywords.begin(), keywords.end(), firstToken(lines[i])) != keywords.end()) break;
          auto trimmed = trimCommentAndLeadingWs(lines[i]);
          i++;
          if (!trimmed) continue;
          // 「M-n キー 要素...」。要素はK-nか"文字列"(空白を含んでよい。"自体は書けない)。
          const std::string &line = *trimmed;
          std::size_t p = 0;
          auto word = [&]() {
            while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) p++;
            std::size_t start = p;
            if (p < line.size() && line[p] == '"') {
              std::size_t close = line.find('"', p + 1);
              p = (close == std::string::npos) ? line.size() : close + 1;
            } else {
              while (p < line.size() && line[p] != ' ' && line[p] != '\t') p++;
            }
            return line.substr(start, p - start);
          };
          std::string head = word(), key = word();
          if (head.size() < 3 || head.compare(0, 2, "M-") != 0 ||
              head.find_first_not_of("0123456789", 2) != std::string::npos || key.empty())
            continue;  // "--"(^X^Sが次に書き込む位置の印)等
          MacroEntry entry;
          entry.number = std::atoi(head.c_str() + 2);
          entry.keySpec = key;
          bool ok = true;
          for (std::string item = word(); !item.empty(); item = word()) {
            KeyBindings::MacroItem macroItem;
            if (item.front() == '"') {
              macroItem.literal = item.substr(1, item.size() >= 2 && item.back() == '"' ? item.size() - 2 : item.size() - 1);
            } else if (item.size() >= 3 && item.compare(0, 2, "K-") == 0 &&
                       item.find_first_not_of("0123456789", 2) == std::string::npos) {
              macroItem.keyNumber = std::atoi(item.c_str() + 2);
            } else {
              errors_.push_back("MACRO: " + head + ": not a K-number or a \"string\": " + item);
              ok = false;
              break;
            }
            entry.items.push_back(macroItem);
          }
          if (ok && !entry.items.empty()) macros_.push_back(entry);
        }
        break;
      }
      case 24: {  // URL(urlHandlers()自身のコメント参照)
        urlHandlers_.clear();
        while (auto data = nextDataLine(lines, i)) {
          auto tokens = tokenize(*data);
          if (tokens.empty()) continue;
          std::string name = tokens[0];
          for (char &c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
          static const std::vector<std::string> kNames = {"open", "pdf", "dump", "term", "copy", "look"};
          if (std::find(kNames.begin(), kNames.end(), name) == kNames.end()) {
            errors_.push_back("URL: not Open/Pdf/Dump/Term/Copy/Look: " + tokens[0]);
            continue;
          }
          name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
          std::vector<std::string> argv(tokens.begin() + 1, tokens.end());
          const char *home = std::getenv("HOME");
          if (!argv.empty() && argv[0].compare(0, 2, "~/") == 0 && home) argv[0] = home + argv[0].substr(1);
          urlHandlers_.push_back({name, argv});
        }
        break;
      }
      default:
        while (i < lines.size() && isContinuationLine(lines[i])) i++;
        break;
    }
  }
  // KEY節(2026-09-25実装): 組み込みの表と突き合わせて置き換え表を組み、
  // 不正な指定はこのusilicrcのエラーとして積む(keyBindings()参照)。
  std::vector<KeyBindings::Macro> macros;
  for (const auto &m : macros_) macros.push_back({m.number, m.keySpec, m.items});
  keyBindings_.configure(keyPrefixSpecs_, keyEntrySpecs_, &errors_, macros);
  return true;
}

const std::vector<std::string> &Configuration::errors() const { return errors_; }

// configuration.hpp自身の宣言コメント参照。
const std::vector<Configuration::UrlHandler> &Configuration::urlHandlers() const {
  if (!urlHandlers_.empty()) return urlHandlers_;
  static const std::vector<UrlHandler> kDefaults = [] {
#ifdef __APPLE__
    std::vector<UrlHandler> list = {
        {"Open", {"open"}}, {"Dump", {"w3m", "-dump"}}, {"Term", {"w3m"}}, {"Copy", {"pbcopy"}}, {"Look", {}}};
#else
    std::vector<UrlHandler> list = {{"Open", {"xdg-open"}},
                                    {"Dump", {"w3m", "-dump"}},
                                    {"Term", {"w3m"}},
                                    {"Copy", {"xclip", "-selection", "clipboard"}}};
    // 2026-10-05: WSL2ではWindowsの既定のブラウザ・クリップボードへ(platform.hpp参照。URLは
    // ASCIIなのでclip.exeへそのまま渡してよい)。
    if (isWsl()) list = {{"Open", {"explorer.exe"}}, {"Dump", {"w3m", "-dump"}}, {"Term", {"w3m"}}, {"Copy", {"clip.exe"}}};
    // MSYS(Git Bash、.steering/20261005-windows-gitbash/): CopyはURLを標準入力で受けて/dev/clipboardへ。
    if (isMsys())
      list = {{"Open", {"explorer.exe"}}, {"Dump", {"w3m", "-dump"}}, {"Term", {"w3m"}},
              {"Copy", {"sh", "-c", "cat > /dev/clipboard"}}};
#endif
    // Pdf(2026-10-04): make installが入れるurl2pdfがあればOpenの次に。
    const char *home = std::getenv("HOME");
    std::string url2pdf = std::string(home ? home : "") + "/.local/lib/usilic/url2pdf";
    if (home && ::access(url2pdf.c_str(), X_OK) == 0) list.insert(list.begin() + 1, {"Pdf", {url2pdf}});
    return list;
  }();
  return kDefaults;
}

void Configuration::reloadKeysAndMacros(const std::string &rcText) {
  Configuration fresh;
  fresh.loadRcText(rcText, /*fileManagerMode=*/true);
  keyPrefixSpecs_ = fresh.keyPrefixSpecs_;
  keyEntrySpecs_ = fresh.keyEntrySpecs_;
  macros_ = fresh.macros_;
  keyBindings_ = fresh.keyBindings_;
}

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
  // 2026-10-08、直接のユーザー依頼("`EDITOR=vi usilic^M`で起動時はusilicrcのEDITORよりも優先で"): 環境変数
  // EDITORがあれば、usilicrcのEDITORより先にそれを使う(空白で区切って引数にする)。
  if (const char *env = std::getenv("EDITOR")) {
    std::vector<std::string> words;
    std::istringstream in(env);
    for (std::string word; in >> word;) words.push_back(word);
    if (!words.empty()) result = words;
  }
  result.push_back("+" + std::to_string(lineNumber));
  result.push_back(path);
  return result;
}

std::vector<std::string> Configuration::shellArgv(const std::string &envShell) const {
  auto supported = [](const std::string &program) {
    std::string base = program.substr(program.find_last_of('/') + 1);
    return base == "bash" || base == "zsh" || base == "tcsh" || base == "csh";
  };
  if (!envShell.empty() && supported(envShell)) return {envShell};
  if (!shellProgram_.empty()) return shellProgram_;
  return {"bash"};
}

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
const std::vector<std::string> &Configuration::grepPresets() const { return grepPresets_; }
const std::vector<std::string> &Configuration::grepPresetLines() const { return grepPresetLines_; }
const std::map<int, std::string> &Configuration::keyPrefixSpecs() const { return keyPrefixSpecs_; }
const std::map<int, std::string> &Configuration::keyEntrySpecs() const { return keyEntrySpecs_; }
const KeyBindings &Configuration::keyBindings() const { return keyBindings_; }
int Configuration::bufferSizeLimitBytes() const { return bufferSizeLimitBytes_; }
int Configuration::historyRetentionCount() const { return historyRetentionCount_; }
int Configuration::waitIntervalMicros() const { return waitIntervalMicros_; }
bool Configuration::overrideWait(int tenMillis) {
  if (tenMillis < 0) return false;
  waitIntervalMicros_ = tenMillis * 10000;
  return true;
}
bool Configuration::overrideBuffer(int megabytes) {
  if (megabytes < 1 || megabytes > 100) return false;  // BUFFER節と同じ範囲
  bufferSizeLimitBytes_ = megabytes * 1024 * 1024;
  return true;
}
bool Configuration::overrideLine(int lines) {
  if (lines < 0) return false;
  historyRetentionCount_ = lines;
  return true;
}
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
