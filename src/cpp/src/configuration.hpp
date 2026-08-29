#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "charset.hpp"

namespace fm {

// ARCHIVEディレクティブの拡張子が、具体的にどの展開器へマッピング
// されるか――is_chp()のプログラム名分類(legacyの番号ではtar=5、
// lha=4、zip=3、bzip2=2、rpm=8、deb=9)からの移植。ここでは列挙型へ
// 改名してある。数値自体には意味が無かったため。呼び出し側が、
// 与えられた拡張子に対応する既に移植済みのArchiveReaderサブクラス
// (TarReader/LhaReader/ZipReader/RpmReader)を選べるようにする。
// Rar/SevenZip(2026-08-26追加): 保持すべきlegacyの番号が無い(両
// フォーマットともこのプロジェクト本来のC時代より後に登場した)
// ――既存の値がその数値上の同一性を保てるよう、間に挟むのではなく
// 末尾に追加してある。
enum class ArchiveKind { None, Tar, Lha, Zip, Bzip2, Rpm, Deb, Rar, SevenZip };

// 実行するプログラム、ChildProcess::run()/runPipeline()が既に期待して
// いる形で: argvは最終(または唯一)の段階。pipedFromArgvは、設定
// されていれば、その標準出力がこちらの標準入力へ供給される上流の
// 段階(例えば".tgz": pipedFromArgv={"gzip","-dc"}、
// argv={"tar","tvf","-"})。legacyのCHILD自身のこれを表現する方法
// ――下流コマンドの空いているargv[]スロットに押し込まれた、生の
// `(CHILD*)`キャストされたポインタ(save_child()のパイプ分割分岐)
// ――を、2つの普通のフィールドで置き換えている。
struct ProgramCommand {
  std::vector<std::string> argv;
  std::optional<std::vector<std::string>> pipedFromArgv;
};

// ファイル拡張子ではなく役割によって検索される組み込みプログラム
// ――ls/du/cp/mv/rm/groff/man/locate/egrep/fgrep、各アーカイブ
// フォーマットの展開/抽出ヘルパー。legacyのchild[]テーブル(str.c)
// からの移植で、そちら自体はS_ls/S_du/...という列挙値でキーされる
// 位置ベースのchar*[]だった――ここでは代わりに名前を付けてある。
// 「文字列の配列内での位置」という間接参照は純粋な実装の詳細であり、
// 意味のある設定の形ではなかったため。
enum class ConfiguredRole {
  Ls,
  Du,
  Cp,
  Mv,
  Rm,
  Groff,
  Man,
  Locate,
  UpdateDb,
  Egrep,
  Fgrep,
  GzipDecompress,
  Bzip2Decompress,
  Tar,
  LhaExtract,
  UnzipExtract,
  Rpm,
  Rpm2Cpio,
  DebExtract,
  // LOCATE自身の個人用データベース(2026-08-22追加、docs/CHANGES.md
  // 406番――setUserLocateDatabasePath()自身のコメント参照): macOS
  // 専用。システム全体のデータベース(ConfiguredRole::UpdateDb/
  // Locate)は`nobody`として動く`locate.updatedb`によって構築される
  // が、これは現代のmacOSではユーザー自身のホームディレクトリの
  // 「中を」全く読めない(新規アカウントは既定で`750`、「other」への
  // アクセス無し)――そのため、`~`の直下に置かれた素の`.txt`は、
  // システムデータベースをどれだけ頻繁に更新してもインデックスされる
  // ことが無い。この2つのロールは、代わりに本物のユーザー自身の
  // アカウントの下で完全に実行される(rootは不要)第2の、個人用の
  // データベースを構築/検索し、まさにシステム側が持つ死角をカバー
  // する。
  LocateUserDb,       // 検索: `locate -d <個人用DBパス>`
  LocateUserDbBuild,  // 構築: `find`+`locate.mklocatedb`のパイプライン、macOS専用
  // 2026-08-26追加(ユーザーからの直接依頼、src/へ投入された実際の
  // .rar/.7zフィクスチャでテスト): どちらのフォーマットにも保持すべき
  // legacyの前例は無い(両方ともこのプロジェクト本来のC時代より後に
  // 登場した)。RarExtractは`unar`(The Unarchiver)自身の標準出力への
  // 抽出モードを実行する――プロプライエタリな`unrar`バイナリでは
  // ない。それは自由に再配布できず、この開発マシンにもインストール
  // されていなかった。`unar`は、同じRAR形式を読める、一般的な
  // brewでインストール可能な代替品である。
  RarExtract,
  SevenZipExtract,
};

// usilicrc(tcbrc)をパースし、明示的なアクセサメソッド経由で設定の
// 問い合わせに答える。str.cのinit_config()と、そのconf_*()という
// ディレクティブごとのサブパーサ(conf_arc/conf_bin/conf_grep/
// conf_prog/conf_bsize/conf_code/conf_history/conf_wait/conf_scroll/
// conf_ls)、それにchild.cのsave_child()/init_child()(これらの
// ディレクティブが組み立てるCHILDレジストリ)からの移植。
//
// これは逐語的な移植ではない。3つの構造上の変更点:
//   - legacyの設定ストレージは、このプロジェクトの既に移植済みの
//     全クラスが直接呼ぶのを意図的に避けてきたグローバルなs_S()/
//     s_P()文字列/テーブル参照そのものである(KeyInputの
//     setPidReporter()/setErrorPending()、ChildProcessの
//     stderrLogPath、ShellのOptions構造体全体が、そこへ手を伸ばさず
//     に済むようにするために部分的に存在している)。Configurationは、
//     その回避が結実する場所である: パース済みデータを普通のメンバ
//     フィールドとして所有し、名前付きメソッド経由で公開する。
//     s_S()/s_P()の「90項目のヘッダーの列挙値でインデックスされる、
//     1つのフラットな配列」という形の同等物は無い。
//   - ここではファイルI/Oを行わない。loadRcText()はusilicrcの内容を
//     既に読み込み済みの文字列として受け取る。ファイルの場所を特定
//     すること(legacyのset_tcbrc(): $HOME/.usilic/usilicrc、無ければ
//     LIB/usilicrc、それも無ければ起動を拒否)は呼び出し側の仕事。
//     これにより、本物のファイルシステム/$HOME無しでもパース自体を
//     純粋かつユニットテスト可能に保てる。PipeBuffer/ArchiveReaderが
//     ファイルを自分で読むのではなくバイト列を受け取るのと同じ。
//   - 文字コードのブートストラップは明示的であり、隠れたgetenv()
//     呼び出しではない。setEnvironmentCharset()は、getenv(ENV_CHARSET)
//     が返したもの(未設定ならnullopt)を素のパラメータとして受け取る、
//     Shell/ChildProcessがそれぞれのOptionsで使っているのと同じ
//     パターン。これはCLAUDE.mdの作業ログ13番を直接修正する:
//     legacyの`if ((tcbcharset = getenv(ENV_CHARSET)))`はNULLかどうか
//     しかテストしていなかったため、`export TCBCHARSET=`(明示的に
//     exportされているが空)は、文書化されたUTF-8ではなく静かに
//     未認識の値の分岐を通ってEUC既定値へ落ちてしまっていた。
//     ここでは、「明示的に設定されたか」と「その値は何か」という
//     2つの独立した、正直な事実として追跡する――
//     charsetExplicitlySet()は空の明示的なexportに対してもtrueで
//     あり、空または未認識の値は文書化されたUTF-8既定値以外の
//     どこへも決してフォールバックしない。
//
// 意図的に除外(他のクラスの仕事、または先送り):
//   - KEY章(F-*/K-*/M-*のファンクションキー、コマンドキー、
//     M-0..M-19マクロバインディング): そのバインディングは、この
//     C++移植にまだ存在しないモード/コマンド番号を対象としている
//     (それは将来のMode/Applicationの関心事)――今それをパースしても、
//     まだ誰も消費できないテーブルを生成するだけになる。Modeが存在
//     するまで先送り。loadRcText()は、KEY章の本体を(パースするだけ
//     で実行時の効果は無いという他の全ディレクティブと同じ扱いに
//     合わせて)拒否するのではなくエラー無くスキップする。
//   - プログラム(ChildProcess)や対話的シェル(Shell)を実際に実行
//     すること: Configurationは、ある問い合わせにどのargvが答えるか
//     「だけ」を決め、自分では何も実行しない――どちらのクラスも
//     自身のヘッダーで既にそう述べている。
//   - 単純な拡張子マッチを超えた内容判定によるファイル種別
//     ディスパッチ(is_man()風の検出、FILEディレクティブの、
//     file(1)が報告するものに対するis_chp()ベースのアーカイブ種別
//     相互分類): 将来のModeの領分であり、このクラスの拡張子テーブル
//     と組み合わせて動作するが、このクラスが所有するものではない。
//   - 設定済みプログラムが実際にインストールされているかの確認
//     ($PATH走査、legacyのis_inst()): legacyは、プログラムが見つから
//     ないディレクティブ行を静かに捨て、致命的でない警告を記録して
//     いた。ここでは再現していない――ChildProcess::run()経由で存在
//     しないプログラムを呼び出す呼び出し側は既にきれいに失敗する
//     (ChildProcess自身のテスト参照)ので、これは安全上のギャップ
//     ではなく、単にまだ引き継がれていない起動時のUX上の親切さに
//     過ぎない。
//   - ~/.usilic/history自身のファイル形式とカーソル位置の保存
//     (将来のHistoryクラス): 両方のファイルを読んで確認済みだが、
//     どちらも~/.usilic/の下に住んでいること以外、usilicrcとは
//     実質的にパースロジックを共有していない――唯一の本当の重複は
//     HISTORYディレクティブの保持数の整数で、Historyがコンストラクタ
//     パラメータとして受け取れるよう、ここではhistoryRetentionCount()
//     経由で公開されている。
//   - パースした設定を、パースする副作用として適用すること
//     (legacyのconf_scroll()は、SCROLLをパースしながら直接
//     vt_mode()/vt_cs()を呼ぶ。KEY章のエラーはkey_err()を呼び、
//     それはパースの途中でfclose()してexit(1)する)。loadRcText()は
//     常にデータを組み立ててerrors()へ追加するだけ――例えば
//     smoothScrollEnabled()をDisplayへ適用するのは完全に呼び出し側の
//     仕事。
class Configuration {
 public:
  Configuration();

  // usilicrcの既に読み込み済みのテキストをパースする。ディレクティブ
  // は現れた順に適用され、目にするたびにコンストラクタの組み込み
  // 既定値を上書きする。認識できないディレクティブキーワードは、
  // その時点でパースを止める(legacyがまさにそのケースだけ致命的に
  // 扱っているのと同じ――他の全ての不正な行のケース、例えば範囲外の
  // BUFFER値や(ここでは再現していない)インストール確認に失敗する
  // プログラムは、legacyでも致命的ではなく、ここでも致命的ではない
  // ままで、代わりにerrors()経由で記録される)。falseを返すのは
  // その1つの致命的なケースだけ。fileManagerModeはlegacyのis_fm()を
  // 踏襲する: PROGRAM/CP/MV/RMディレクティブはtrueの時にのみ効果を
  // 持つ(pagerモード自身のより狭いディレクティブ集合と一致)。
  //
  // 書式についての注記(init_config()自身の行分類から正確に移植、
  // ディレクティブキーワードだけからは自明ではない): あるディレク
  // ティブの本体/データ行は、そのセクションに属するデータとして
  // 認識されるために、空白かタブで始まる(または空行、または'#'の
  // コメント)必要が「ある」。それ以外の何かで列0から始まる行は
  // 常に次のディレクティブキーワードとして扱われ、その場で現在の
  // セクションを終える――独立したセクション終端マーカーは無い。
  // 整った形のusilicrcは全てのデータ行をインデントする。例:
  //   FILE
  //    .pdf 'xpdf' '-remote'
  //   EXEC
  //    .html firefox
  bool loadRcText(const std::string &text, bool fileManagerMode);
  const std::vector<std::string> &errors() const;

  // -- 拡張子ディスパッチ(ARCHIVE/FILE/EXECディレクティブ) --
  // filenameの拡張子は、登録済みの全拡張子に対し「filenameがこの
  // 拡張子で終わるか」で照合される。「最長」一致の拡張子が勝つ
  // (そのため"foo.tar.bz2"というファイルに対しては、より素の
  // ".bz2"よりも専用の".tar.bz2"エントリが優先される)。同じ長さの
  // 拡張子の間では、usilicrcが与えたエントリが組み込みのものより
  // 勝つ――これは意図的な選択であり、legacy自身の拡張子リスト走査
  // 順の逐語的な移植ではない(理由は.cpp参照: get_child()の正確な
  // タイブレーク規則は、この移植のためのソース読解では完全には
  // 復元できなかった。そしてlegacyの走査順が実際に何をしていたかに
  // 関わらず、「ユーザーの上書きが勝つ」が設定上書きシステムにとって
  // 唯一擁護できる挙動である)。
  std::optional<ProgramCommand> programForSuffix(const std::string &filename,
                                                  ArchiveKind *archiveKindOut = nullptr) const;
  bool isExec(const std::string &filename) const;

  // -- エディタ/シェル(EDITOR/SHELLディレクティブ) --
  // legacyは固定幅の"+??????????"というプレースホルダのargvスロット
  // を仕込んでおき、起動時にその場でパッチしていた(文字列を簡単には
  // 伸長できなかった1990年代の回避策)。これは単に本物の
  // "+<lineNumber>"引数を直接組み立てるだけ。
  std::vector<std::string> editorArgv(const std::string &path, int lineNumber) const;
  std::vector<std::string> shellArgv() const;
  // usilicrcにSHELLディレクティブが全く無い場合に使われる――legacyは
  // その場合、$SHELL/file(1)による検出でログインシェルを推測する
  // ことにフォールバックし、それでもSHELLディレクティブでも
  // bash/tcshに解決できなければ起動を拒否していた。Configurationは
  // 自分ではgetenv()を呼んだりファイルシステムを嗅ぎ回ったりしない
  // (setEnvironmentCharset()と同じ「隠れたグローバル状態を持たない」
  // 原則)――呼び出し側がフォールバックを解決し、ここへ渡す。これを
  // 全く呼ばなければ、SHELLディレクティブが無い場合shellArgv()は
  // 空のままになる。
  void setFallbackShellArgv(std::vector<std::string> argv);

  // ConfiguredRole::LocateUserDb/LocateUserDbBuildの既定コマンドを
  // `path`(main.cpp自身の`usilicDir + "locate.db"`、loadRcText()の
  // 直後に呼ばれる――上のsetFallbackShellArgv()と同じ「呼び出し側が
  // ファイルシステムの詳細を解決し、Configurationはそれを保存する
  // だけ」という境界であり、理由も同じ: Configuration自身は決して
  // getenv()を呼んだりファイルシステムに触れたりしない)へ配線する。
  // これを全く呼ばない場合(これを配線せずに素のConfigurationを
  // 構築する既存の全テストのように)、両ロールとも未設定のままになる
  // ――commandFor()は空のProgramCommandを返し、LocateModeは既に空の
  // argvを「この機能は利用不可」として扱うので、個人用データベース
  // は絶対条件ではなく静かに無効化されるだけになる。Linux専用ビルド
  // (`#else`分岐)では何もしない――これがmacOS固有である理由に
  // ついては列挙型自身のコメント参照(ユーザーからの直接の判断:
  // 「macOS限定項目は、すでにGUIもあるので同様でいい」)。
  void setUserLocateDatabasePath(const std::string &path);

  // -- 役割ベースの組み込みコマンド(ls/du/cp/mv/rm/groff/man/... ) --
  // usilicrcディレクティブで上書きできるのはCp/Mv/Rmだけ(CP/MV/RM
  // ――legacyで唯一「既存のCHILDノードを新しいものへ差し替える」
  // ディレクティブ)。他の全てのロールはそれぞれの組み込み既定値に
  // 固定されている。
  ProgramCommand commandFor(ConfiguredRole role) const;
  // あるロールのコマンドを、usilicrcのパースを完全にバイパスして
  // 直接上書きする――実際の既定プログラムが自動テストスイートには
  // 不向きなロールをテストから分離するために存在する(例えば
  // ConfiguredRole::UpdateDbの実際のmacOS/Linuxコマンドは本当に
  // ファイルシステム全体を走査し、遅い上に通常完了にはrootが必要
  // ――2026-08-13、macOSの既定値が、存在しないバイナリではなく実際に
  // 実行可能なバイナリを指すようになった際に、実際の`make test`実行
  // がまさにそれでハングしたことを受けて追加)。「どのロールでも
  // usilicrcで設定可能にする」という一般的な抜け道ではない――どの
  // ロールにこれが当てはまるかについてはcommandFor()自身のコメント
  // 参照。
  void setCommandForTest(ConfiguredRole role, ProgramCommand command);

  // -- シードリスト(GREP/PROGRAMディレクティブ) --
  // FGREP/EGREPおよびPROGRAMプロンプトで、履歴/自動補完の候補として
  // 提供されるあらかじめ用意されたパターン/コマンド――単純な順序
  // 付きリスト。legacyの並行するLIST*/STR*木のペアは、純粋に内部
  // 検索構造上の最適化であり、「これらの文字列の順序付き列」という
  // 以上のユーザーに見える形は持っていなかった。
  const std::vector<std::string> &grepSeedPatterns() const;
  const std::vector<std::string> &programSeedCommands() const;

  // -- スカラー設定 --
  // CODEディレクティブ: 名前に反して、文字コードのホワイトリストでは
  // 「ない」――バイナリ内容検出が「これはバイナリらしい」の証拠として
  // 扱うのではなく許容すべき、生の制御バイトの「値」(0-31)の
  // ホワイトリスト。legacyがそう呼んでいたものではなく、実際に何を
  // するかにちなんで命名してあり、その食い違いを再導入しないように
  // している。
  const std::string &allowedControlBytes() const;  // CODE
  int bufferSizeLimitBytes() const;                // BUFFER
  int historyRetentionCount() const;               // HISTORY
  int waitIntervalMicros() const;                  // WAIT
  bool smoothScrollEnabled() const;                // SCROLL
  int lsMode() const;                              // LS

  // -- 文字コードのブートストラップ --
  // これがCLAUDE.mdの作業ログ13番をなぜ直接修正するかについては
  // クラスコメント参照。
  void setEnvironmentCharset(std::optional<std::string> envCharsetValue);
  bool charsetExplicitlySet() const;
  Charset::Encoding defaultCharset() const;

 private:
  std::optional<ProgramCommand> findBuiltinArchiveAlias(const std::string &searchText) const;

  struct SuffixEntry {
    std::string suffix;
    ProgramCommand command;
    bool isExec = false;
    ArchiveKind archiveKind = ArchiveKind::None;
  };

  std::vector<SuffixEntry> suffixEntries_;
  std::vector<SuffixEntry> builtinArchiveEntries_;  // ARCHIVE[]の既定値、別名検索のため別に保持

  std::vector<std::string> editorProgram_;
  std::vector<std::string> shellProgram_;
  std::vector<std::string> fallbackShellArgv_;

  std::map<ConfiguredRole, ProgramCommand> roleCommands_;

  std::vector<std::string> grepSeedPatterns_;
  std::vector<std::string> programSeedCommands_;

  std::string allowedControlBytes_;
  int bufferSizeLimitBytes_ = 1 * 1024 * 1024;
  int historyRetentionCount_ = 200;
  int waitIntervalMicros_ = 100000;  // conf_wait()の静的k_wait初期化値(100000)
  bool smoothScrollEnabled_ = false;
  int lsMode_ = 0;

  std::optional<std::string> envCharsetValue_;

  std::vector<std::string> errors_;
};

}  // namespace fm
