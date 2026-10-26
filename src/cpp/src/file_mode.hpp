#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "annotation_stack.hpp"
#include "charset.hpp"
#include "configuration.hpp"
#include "content_view.hpp"
#include "epub_to_help_markup.hpp"
#include "grep_mode.hpp"
#include "history.hpp"
#include "pipe_buffer.hpp"

namespace fm {

// ファイル閲覧モード: 通常ファイルの内容を表示する。mode.cのT_file
// ケースが、init.cのinit_bin()(read_buff()/check_type()/set_pp()の
// 連鎖)へ手を伸ばす形からの移植――ただし「プレーンテキストファイル」
// の経路のみ。legacyのT_file処理自体、M_error/M_log/M_old/M_duも
// 使うもっと大きな共有機構(check_type()の内容判定、外部の
// `file`(1)/roff/アーカイブハンドラへ引き渡すかどうかの決定、
// バイナリ内容の16進ダンプ表示モード)の小さなラッパーに過ぎない
// ――その内容種別ディスパッチはここでは一切移植していない。
// スコープについてはクラスコメント参照。
//
// これはinit_bin()/check_type()/set_pp()の逐語的な移植ではない。
// それらは合わさって、文字コード判定/変換、タブ展開、nroff
// オーバーストライクの太字/下線検出を、その場でゼロから再導出
// していた。それら全ては既に、既に移植済みのPipeBufferクラスという
// きれいな、テスト済みの居場所を持っている――FileModeは本当に
// ただ: ファイルの生バイトを読み、PipeBufferへ渡し、PipeBufferの
// 折り返し/変換済みの行をContentViewへ渡すだけ。文字コード/
// オーバーストライクのロジックはここでは一切再実装していない。
//
// 意図的に除外(将来の課題、この段階では対象外):
//   - 内容種別の判定/ディスパッチ(check_type()の、プレーンテキスト
//     として表示する代わりに外部プログラム、roff描画、またはアーカイブ
//     自動検出へファイルを引き渡すという決定): Mode/Applicationの
//     仕事、そのディスパッチ層が存在するようになった時点で、
//     Configurationのprogramomforsuffix()/archiveHandler風の検索
//     (既に移植済み)の上に構築される。
//   - バイナリ内容の16進ダンプ表示: legacyのC_binケース。PipeBufferは
//     既に同じ理由(まだ存在しないBuffer側の前処理に依存する)で
//     この除外を文書化済み。バイナリファイルの生バイトは、
//     判定不能なコンテンツに対するPipeBuffer自身のパススルー挙動と
//     同じく、ここでは単にそのまま表示される。
//   - M_error/M_log/M_old/M_duの代替データ源(ログファイル、About
//     バナー、`du`出力): それらは「通常ファイルを閲覧する」ことでは
//     全く無く、たまたまinit_bin()のC実装を共有していただけのコード
//     経路である。
//   - スクロール/ページング: これは全くこのクラスの仕事ではない
//     ――呼び出し側は同じContentView参照を直接駆動する(moveUp()/
//     moveDown()/pageUp()/pageDown()等、既に移植済み)、DirectoryMode
//     もContentView自身のナビゲーションを包んでいないのと同じ。
//   - 以前アクティブだったモード(DirectoryMode、または将来の
//     呼び出し元)へ戻ること: FileModeはModeStackに一切触れない。
//     DirectoryModeも「自分自身の」呼び出し元へどう戻るか知らない
//     のと同じ――「T_dirがこれを開いたので、^Gはそこへ戻るべき」を
//     2つのモードクラスをまたいで調整するのはMode/Applicationの
//     仕事であり、どちらのモードクラス自身の仕事でもない。
// 2026-09-04追加、直接のユーザー依頼("usilic ~/git/usilic/src/HELP.txt
// をサポートして"): file_mode.cpp自身のコメント参照。CLIのファイル
// 引数起動(Application::startPagerFileList()/navigateToPagerFile())が、
// このファイルに対してのみFileMode::loadFile()相当の全読み込み+
// expandHelpMarkup()展開の経路を選ぶかどうかの判定に使う。
bool isHelpMarkupPagerFile(const std::string &path);

// 2026-09-22追加、直接のユーザー依頼("せっかく.mdフォーマットで書かれているので
// usilicに文字装飾機能を実装してほしい"): file_mode.cpp自身のコメント参照。
// isHelpMarkupPagerFile()と同じ位置でFileMode::loadAndTransformContent()から呼ばれ、
// markdownToHelpMarkup()+expandHelpMarkup()の二段展開を発動する。
bool isMarkdownFile(const std::string &path);

// 2026-09-28追加(.steering/20260928-epub-viewer-stage1/参照): 拡張子.epub(大小無視)。
// FileMode::loadAndTransformContent()はバイナリ判定より前にepubToHelpMarkup()で
// 本文のテキストへ変換し、isBinaryFile()はfalseを返す(クイックルックへ渡さない)。
bool isEpubFile(const std::string &path);

// 2026-10-04追加、直接のユーザー依頼("画像ファイルJPEG、PNG、GIF(アニメ無し)、BMPをEPUBモードで表示可能なら
// (ls)でも表示できてほしい"): 拡張子.jpg/.jpeg/.png/.gif/.bmp(大小無視)。-gではFileMode::readAndWrap()が
// EPUBの画像と同じ画像の行(ContentView::Line::imageKey、keyは「パス\n\x01更新時刻:大きさ」)にして画面に収めて
// 描く(Application::openExternallyIfApplicable()もクイックルックへ渡さない)。
bool isViewableImageFile(const std::string &path);

// 2026-09-20追加、直接のユーザー依頼("usilic -m src/QUICK.txt > .../
// usilic.man...HELPは同ファイルを「C-j b」でCUTする"): legacy自身の
// is_man()(old/src/code.cから移植、readAndWrap()自身のコメント参照)
// ――ファイル先頭N_man(0x1000)バイトを行頭ごとに走査し、troffの
// 「短い(1〜2文字の)マクロ名+区切り」という見た目のパターン
// (".\\\""コメント行、".B "/".I "、または".XY "/".XY\n"の形の
// 2文字マクロ名)に一致する行が見つかれば、その通常ファイルは
// ms/manマクロ形式のroffドキュメントだと判定する――拡張子は一切
// 見ない(legacy自身のcheck_ext()も、既知の拡張子テーブルで一致
// しなかった場合の「最後の手段」としてのみこれを使っていた)。
bool looksLikeRoffDocument(const std::string &content);

class FileMode {
 public:
  FileMode(ContentView &view, History &history, const Configuration &configuration,
           Charset::Encoding encoding, int width);

  // pathの全内容を読み、PipeBuffer経由で変換/折り返しし、表示する。
  // このパスに対して記憶済みのカーソル位置がHistoryにあればそれを
  // 復元する。ファイルが読めない場合(存在しない、権限が無い、等)は
  // false。
  // shouldAbort(2026-09-25追加): 読み込みの途中で呼び、trueなら中断して
  // そこまでを表示する(readAndWrap()自身のコメント参照)。
  bool loadFile(const std::string &path, const std::function<bool()> &shouldAbort = {});
  // 2026-09-25追加、usilicrcのCODE(バイナリ判定)用――binaryTypeText()
  // 自身のコメント参照。バイナリと判定したファイルの種類をlegacyと同じ
  // くERRORログへも記録するため、そのパスを受け取る(CpMvRmModeと同じ
  // ポインタ渡し。nullptrなら記録しない)。
  void setErrorLogPath(const std::string *path) { errorLogPath_ = path; }

  // 2026-09-14追加、直接のユーザー依頼("（ls）での^O^M/^[^Mを実装
  // して"、QUICK.txt自身のK-23/K-24自身の説明): loadFile()と全く同じ
  // だが、以後(reflow()経由の再折り返し含む)このファイルの内容を
  // 折り返さない(K-23、^O^M、"ファイルを１行ずつ読み込む"――legacy
  // 自身の実際の実装(alloc_buff())を追った結果、実質的な違いは
  // 「読み込み中の中断キー入力チェックの有無」だけと判明したが、この
  // ポートにはその概念が無いため、現代的に意味のある解釈として
  // 「long行を折り返さない」(less -Sと同じ)を採用した)。長い行は
  // ContentView::renderLine()自身の既存のmaxCol打ち切りにそのまま
  // 任せる。
  //
  // 2026-09-15訂正: 上のalloc_buff()調査は浅かった――old/src/menu.c
  // のcheck_type()/check_ext()まで遡ると、fp->cr==2(^O^M)はfp->cr==1
  // (plain Enter)と同じくcheck_ext()を通る(HELP-markup展開/roff文書
  // のgroff整形が適用される)一方、read_buff()自体はm_pipe()経由の
  // パイプ越しに行われる、という「読み込み経路」の違いも実在した。
  // このメソッド自体(直接呼び出し元のApplication::
  // openSelectedEntryNoWrap()のARCHIVE分岐)はそのままでよいが、通常
  // ファイル分岐は代わりにloadFileAsPipe()(このヘッダの少し上)を
  // 使う――そちらのコメント参照。
  bool loadFileNoWrap(const std::string &path);

  // 2026-09-14追加、同上のユーザー依頼: loadFile()と全く同じだが、
  // 以後このファイルのnroff重ね打ち("X\bX"=太字、"_\bX"=下線)を
  // 一切解釈しない(K-24、^[^M、"制御コードをそのまま可視化"――
  // QUICK.txt自身の説明"^A^B^Cはハイライト表示のABCで表示"通り、
  // バックスペース等の生の制御バイトはContentView::renderLine()
  // 自身の既存の制御バイト表示規約でそのまま可視化される)。
  //
  // 2026-09-15追加、直接のユーザー指摘("^[^Mは「as is」（msマクロ）
  // になってない"): loadAndTransformContent()自身のコメント参照
  // ――このメソッドが設定するrawControlCodesMode_は、上のオーバー
  // ストライク解釈の有無だけでなく、HELP-markup展開/roff文書の
  // groff整形も丸ごとスキップするようになった。legacy自身のM_code
  // (^[^M、fp->cr=0)がcheck_ext()自体を呼ばないのと同じ「変換ゼロ」
  // が、この"as is"の実体だった。
  // 2026-09-16追加、直接のユーザー報告("debian-keyring...tar.xz(75MB)
  // の^[^Mが^Gで中断不能")+提案("入力待ち無しのgetchっぽい関数...
  // 一回の読み込み分が終わったらそこを通り、^Gが投入されていたら
  // 中断": シェルスクリプトは巨大な2進ファイルを一括でPipeBuffer::
  // feed()する(readAndWrap()自身)代わりに、shouldAbortが渡された
  // 場合はreadAndWrapChunked()(自身の宣言コメント参照)でチャンク単位
  // に処理する。shouldAbortを渡さない既存の呼び出し元(テスト等)は
  // 完全に無変更のまま(空のstd::functionはfalsyなので、下のloadFileImpl()
  // 自身の分岐で無視される)。
  bool loadFileRaw(const std::string &path, const std::function<bool()> &shouldAbort = {});

  // 2026-09-15追加、直接のユーザー指摘("^O^Mで(stdin)形式になって
  // おらず"): old/src/menu.cのM_fp(^O^M、fp->cr=2)は、fp->cr==1
  // (plain Enter)と同じくcheck_ext()を通す(=HELP-markup展開/roff
  // 文書のgroff整形はloadFile()と同じく適用される――そこはK-24の
  // "as is"とは違う)一方、read_buff()自身へ`m_pipe()`経由でパイプ
  // 越しに読ませる。この2点目を、既存のloadStream()(元々は本物の
  // STDINページャー/SHELL引き渡し用)の再利用で表現する――選択された
  // ファイルをloadAndTransformContent()で変換した後、その結果を
  // 一時ファイルへ書き出し、開き直したfdをloadStream()へ渡す(生の
  // OSパイプへ直接書き込むと、内容がパイプバッファ容量を超えた
  // 場合にwrite()がブロックし、まだ誰も読み出していないこの同じ
  // 呼び出し中にデッドロックする――一時ファイル経由ならその心配が
  // 無い)。loadStream()自身の"(stdin)"ラベル付けを呼び出し元
  // (Application::openSelectedEntryNoWrap())からsetSearchOrigin()
  // 経由でそのまま流用できる。ファイルが読めない場合はfalse。
  bool loadFileAsPipe(const std::string &path);

  // 2026-09-21追加、直接のユーザー指摘("^O^Mを.tar.gz等に適用すると
  // ~/.usilic/配下に実在の<entry>.tvfファイルが作られ、しかも^Gで
  // 戻ると（ls）の元のディレクトリではなくそちらへ移動してしまう
  // ――fork()で子プロセスの出力を直接取り込めば済むのでは？"):
  // Application::openSelectedEntryNoWrap()のARCHIVE分岐は、既に
  // ChildProcess::run()/runPipeline()(=fork()+exec()+pipe読み取り)で
  // tar tvf等の出力をメモリ上に丸ごと捕捉し終えているにも関わらず、
  // それを一旦ディスク上の固定名一時ファイル(materializeArchiveTempFile()、
  // tempRootDirectory_直下)へ書き出し、loadFileNoWrap()という「実在
  // するファイルパスを開く」経路へ渡していた。この結果2つの実害が
  // 出ていた: (1) そのファイルが実際にディスク上に残り続ける(ユーザー
  // が発見した.tvfファイル)。(2) FileMode::currentFile()がその一時
  // パスを指すため、Command::Back→returnToDirectory()の「本物の
  // ファイルの所属ディレクトリを辿る」ロジック(2026-08-16修正、
  // bannerFilePath()自身の特例コメント参照)が誤発動し、directoryMode_を
  // 実際にユーザーがいたディレクトリ(例: ~/)ではなくtempRootDirectory_
  // (~/.usilic/配下)へ切り替えてしまっていた。
  // 根本原因は「メモリ上に既にある内容を、わざわざ実ファイルとして
  // 経由させていた」ことそのもの――loadFileAsPipe()自身が、まさに
  // この同じ問題(K-23の"(stdin)"化)を、mkstemp()+unlink()の無名の
  // 一時fd(ディスク上に残らない)へ書き、currentFile_を空のまま
  // (setSearchOrigin("", "(stdin)"))保つことで既に解決していた――
  // このメソッドはその共有部分(mkstemp/write/loadStream呼び出し)を
  // 公開し、ARCHIVE分岐がChildProcess::run()の出力を、loadAndTransformContent()
  // による再読み込み/groff変換無しに、直接そこへ渡せるようにする。
  bool loadContentAsPipe(const std::string &content);
  // 2026-10-11、直接のユーザー依頼(-H・最初の起動で開く子のHELP.ja.1を"本物のPager"で、平文・"(groff)"付きで):
  // roffの文書等をloadAndTransformContent()でgroffに組んだ平文を、CLIのページャー(loadStream()、名前は`path`)
  // として開く。モードラインのバイト数の後ろに通したプログラム("(groff)")を出し、幅が変われば組み直す。
  bool loadTransformedPagerFile(const std::string &path, int initialLineBudget, const std::string &label = "");
  // モードラインに出す、表示に通したプログラム(contentProgram_)を後から決める(2026-10-11、平文のHELPの"(groff)"、
  // 一時ファイルに書いたものを読むのでloadAndTransformContent()では分からない)。
  // reflow()の読み直しでcontentProgram_が作り直されても残るよう別に持ち、次のloadFile()で消す。
  void setContentProgram(const std::string &program) { contentProgramOverride_ = program; }
  // 今の表示に通したプログラム(モードラインの"(groff)"等、無ければ空、2026-10-11、EDITの判定)。
  std::string displayProgram() const {
    if (pagerMode_) return pagerProgram_;
    return contentProgramOverride_.empty() ? contentProgram_ : contentProgramOverride_;
  }

  const std::string &currentFile() const;

  // K-27(^Oa、M_ascii、「Switch ascii mode」): 「次の」loadFile()/
  // reflow()呼び出しで使われるエンコーディングを変更する――既に
  // 表示されているものを自動的に遡って再デコードすることはしない。
  // セッション途中で文字コードを切り替える呼び出し側は、これに
  // 続けてreflow(width)を呼び、「現在」開いているファイルを新しい
  // エンコーディングで実際に再折り返しすべき(Applicationの仕事――
  // そちら自身のcycleCharset()参照)。
  void setEncoding(Charset::Encoding encoding);

  // 現在のカーソル位置を、現在のファイルのパスの下でHistoryへ記録
  // する。T_fileを離れる際のlegacyの保存(mode.c/menu.cのsave_ll())
  // からの移植――スクロールの自動的な副作用としてではなく、明示的な
  // メソッドとして公開してある。「離れる」ことが実際にいつ起きるかは
  // 呼び出し側だけが知っているため(DirectoryModeの全く同じ
  // saveCurrentCursorPosition()パターン、そちらではopenSelected()/
  // goBack()から呼ばれる、と同じ)。
  void saveCursorPosition();
  // 2026-10-08、直接のユーザー依頼("通常ファイル上での^Lでは日時の更新を表示にも反映して欲しい"): 今のファイルが
  // 読み込んだ後に書き換えられていれば(更新時刻か大きさが違えば)、同じ読み方でカーソルの位置を保って読み直す。
  // 読み直したらtrue。ページャー(標準入力・引数)やストリームで開いたものは対象外。
  // 2026-10-19、直接のユーザー依頼("通常ファイルの^Lでは読み込みも実行（画像ファイルでの^Lと同じ）。ページャー機能
  // では^J^Jが使えないことへの対処。"): 書き換えられていなくても読み直す(reloadIfChanged()から改名)。引数で開いた
  // ページャーも、同じ開き方で読み直す(標準入力・^O^M等の名前の無いストリームは対象外のまま)。
  bool reload();
  // 2026-10-01追加、直接のユーザー報告("^Cf^I^Mで file をカットする場合、FILE履歴には file "^I"が
  // 入るだけのはずなのに file が必ず入る...file "^I"とfileが延々FILE履歴リストに積み上がる"):
  // 次の1回のloadFile()では、FILE履歴(RingCategory::RecentFile)にパスを積まない。ファイルCUT
  // (^Cf・「パス "パターン"」の開き直し)は、読み込んだ後に合成エントリだけを積む。
  void skipNextRecentPush() { skipNextRecentPush_ = true; }

  // 端末サイズ変更のために、現在のファイルを再度読み、newWidthで
  // 再度折り返す。alloc.cのalloc_winch()からの移植――ただし逐語的な
  // 移植ではない: alloc_winch()は、ファイルシステムに再度触れる
  // ことなく、「既に読み込み済みの」PPリスト自身の生バイトから
  // 再折り返しする。これは単に、loadFile()が既に使っているのと
  // 同じ読み込み+折り返しの経路を、新しい幅で再度呼び出す――他に
  // 再折り返しする元となる別個の生バイトキャッシュが無いため(実際の、
  // 明示された違いである――初回読み込みとこのリサイズの間にディスク
  // 上で変更された内容も、legacy自身の厳密にメモリ内だけの再折り返し
  // とは異なり、拾われることになる)。loadFile()とは異なり、記憶済み
  // の位置についてHistoryを参照「しない」――リサイズ契機の再折り返し
  // の目的全体は、ユーザーのカーソルが「この」セッションで既にどこに
  // あったかを保つことであり、古い保存済み位置へ飛び戻ることではない
  // ――そのため現在のカーソルインデックスは単に新しい行数へクランプ
  // されるだけになる(legacy自身のalloc_winch()後のset_rv()も、
  // 実際には意味/同一性ベースの基準ではなく、同じ行インデックス
  // ベースのクランプに相当する)。現在ファイルが読み込まれていない
  // 場合はfalse(何もしない)。
  bool reflow(int newWidth, bool draw = true);  // draw: 組み直した後に描く(縦書きの裏の画面では描かない)

  // 2026-08-30、実際のユーザー報告("フォントはOKだが文字数がおかしい"
  // ――GUIでフォント/ウィンドウサイズが変わった時、File以外のモード
  // (典型的にはDirectory、ディレクトリを見ている間)がアクティブ
  // だったせいで、上のreflow()自身のApplication::handleResize()から
  // の呼び出しが完全にスキップされていた: width_はApplication自身の
  // 構築時の値のまま古びて残り、後で(Directoryモードから)ファイルを
  // 開いた時のloadFile()がその古い幅で折り返してしまっていた)。
  // reflow()と違い、表示中の内容の再折り返し/redraw/カーソル同期は
  // 一切行わない――width_を更新するだけの軽量な版。Applicationは
  // どのモードがアクティブかに関わらずリサイズのたびにこれを呼び、
  // Fileモードが「今」アクティブな時だけ追加でreflow()自身を呼んで
  // 実際に再描画する(次にloadFile()/loadStream()が呼ばれた時点で、
  // このwidth_を使って最初から正しく折り返されるので十分)。
  void setWidth(int newWidth) { width_ = newWidth; }
  int width() const { return width_; }

  // モードラインの名前フィールド(通常は素のファイル名)と、その
  // trailing2フィールド(通常はファイルが属するディレクトリ、
  // コンストラクタ自身のコメント参照)を上書きする(name+trailing2の
  // 「両方」、LOCATE/INFO等)か、trailing2「だけ」を上書きする(GREP/
  // CUT等、ファイル自身の名前はそのまま)かの2種類の注釈を、
  // `AnnotationStack<FileAnnotationKind>`へ統合した(2026-09-20、
  // .steering/20260920-modeline-annotation-stack/design.md参照)。
  //
  // 以前は`searchOriginLabel_`/`searchOriginDetail_`(name+trailing2上書き
  // ――Application::openSelectedLocateResult()のFile分岐等)と
  // `searchAnnotation_`(trailing2のみ上書き――GREPのマッチにEnter経由で
  // 着地した場合等)という独立フィールド+優先順位チェックで表現されて
  // いたが、実際にはどちらか一方だけが常に「今表示すべき唯一の理由」
  // であり、スタックのトップが常にそれを表すため、優先順位チェック
  // 自体が丸ごと不要になった。全てのloadFile()/loadStream()呼び出しの
  // 先頭で自動的にclear()される。
  //
  // `push(kind, text)`: trailing2のみ上書き(旧setSearchAnnotation()相当)。
  // `pushWithNameOverride(kind, label, detail)`: name+trailing2を上書き
  // (旧setSearchOrigin()相当、labelがname、detailがtrailing2)。
  enum class FileAnnotationKind { Search };
  AnnotationStack<FileAnnotationKind> &annotations() { return annotations_; }
  const AnnotationStack<FileAnnotationKind> &annotations() const { return annotations_; }

  // in-place GREP関連のメソッドはContentView自身へ一本化された
  // (DirectoryMode.hpp自身の同じコメント参照、2026-09-11)。

  // 2026-08-25(legacyとless(1)の両方と比較した、ユーザーからの直接の
  // 依頼): CLIページャ起動(main.cpp自身の「通常ファイル引数」/
  // stdin-pipeケース、Application::startPagerStream()自身のコメント
  // 参照)は、上のloadFile()のように入力全体を事前に読み込んでは
  // ならない――実際のlegacyバイナリに対して直接確認済み(PTY採取、
  // 素のファイル引数とパイプされた`seq 1 100000`の両方): 位置カウンタ
  // は、ユーザーが実際にさらにスクロールするまでは約23-25行(この
  // 移植自身の既定のビューポート高さ)にしか届かず、その時点でさらに
  // 読み込まれる。パイプされた入力源では特に重要(無限/非常に長い
  // ストリームは、最初の画面すら描画できる前に全部バッファされて
  // しまってはならない)であり、実際に観察された挙動との一致のため
  // 本物のFILE引数についても同じにしてある。
  //
  // `fd`: 既に開かれた、読み込み可能なファイル記述子――通常のファイル
  // (Application::startPagerStream()自身の::open()呼び出し)か、
  // パイプされた標準入力のためのfd 0自体のどちらか(KeyInputは
  // キーボード入力を、fd 0では絶対になく別途開いた"/dev/tty"から読む
  // ――自身のクラスコメント参照――そのためfd 0はここでページャの
  // 内容として消費するのに全く衝突無く自由に使える)。`ownsFd`:
  // EOFに達するか新しいload/loadFile()がそれを置き換えた時に、
  // 「この」呼び出しが::close()すべきかどうか(標準入力の場合は
  // false――プロセスの途中でfd 0を閉じるのはこのクラスの仕事ではない)。
  // `displayName`: モードライン自身の名前フィールドにそのまま入る
  // (ファイル引数なら完全パス、標準入力なら空――これがloadFile()の
  // 素のファイル名の慣習とどう違うかについてはコンストラクタ自身の
  // ModeLineFieldsProviderのコメント参照)。同時にRecentFile履歴の
  // キーでもあり、空の場合は完全にスキップされる(パイプには記憶
  // すべき安定したパスが無いため)。`initialLineBudget`: 事前に何行
  // 読むか(Applicationは現在のビューポート高さに少し余裕を足した
  // ものを渡す)。`noWrap`: 2026-09-15追加、loadFileAsPipe()専用
  // ――そちら自身のコメント参照。他の全ての(本物のSTDINパイプ/SHELL
  // 引き渡しの)呼び出し元は省略してfalseのままにする――このメソッド
  // 自身がここでnoWrapMode_を明示的に上書きするので、以前の呼び出し
  // (loadFileNoWrap()/loadFileAsPipe()等)がtrueのまま残していても、
  // 次の本物のストリームセッションへ意図せず引き継がれることはない。
  bool loadStream(int fd, bool ownsFd, const std::string &displayName, int initialLineBudget,
                   bool noWrap = false);
  // `targetLineIndex`が現在読み込み済みの末尾以上で、かつストリームが
  // まだEOFに達していなければ、まだ開いているストリームからさらに
  // 読み込む――Applicationが、スクロール/移動コマンドをビューへ委譲
  // する直前に、hasActiveStream()がtrueである間「だけ」呼ぶ(他の
  // 全てのFile mode入口――上のloadFile()――にはストリーミングする
  // ものが無いので、そちらでは常に何もしない)。EOFに達した後、
  // またはそもそもストリームがアクティブでない場合はfalse(純粋に
  // 何もしない)。
  // 2026-09-06再訂正(3回目)、実機PTYでの旧との詳細な逐一比較を経て
  // rowsパラメータは撤回した――ensureLoadedThrough()自身の定義コメント
  // 参照: SHELLの生きたPAGERハンドオフである間は、呼び出し元が渡す
  // targetLineIndexの大小に関わらず常に「あと1行」だけを読み進める
  // ため、画面の高さをここで知る必要が無くなった。
  // `bypassPacing`(2026-09-06さらに追加、直接のユーザー報告
  // "`man ls|usilic -c`→「00001/00050」"「SHELLプロンプトで`man ls^M`
  // 後の最初の画面が瞬時に出ない"): legacy自身の実機`U`を新たに
  // 逐一比較した結果(この関数自身の2026-09-06追加分のコメント参照)、
  // 上の「常にあと1行」という一般化は誤りだったと判明した――legacy
  // 自身は「まだ画面が一度も埋まっていない最初の1画面」は(データが
  // 既に届いていれば)ほぼ瞬時に埋め切り、「画面を一度でも埋め終えた
  // 後」の通常のスクロールでだけ先読み分を1行ずつに絞る。呼び出し元
  // (pollPagerExitNotice()自身のコメント参照)が、今回の要求が前者
  // (まだ最初の1画面を埋めている最中)だと分かっている時だけtrueを
  // 渡し、この関数自身のペーシングを一時的に迂回する。
  bool ensureLoadedThrough(int targetLineIndex, bool bypassPacing = false);
  // 2026-09-20追加、直接のユーザー報告("最終行へ来てもいないのに
  // （Exit）が出てこそ価値がある"、legacy実機での再現確認: "(stdin)
  // は1ページ分ずつ読み込みながら表示するうちEOFに出くわす時が必ず
  // 来るんだが旧版では最終ページよりも手前が常だった")――
  // readMoreIntoPipe()自身のO_NONBLOCK化のコメント参照。
  // ensureLoadedThrough()の直前の呼び出しが、既に十分読み込み済み
  // だったため何も試みなかった(false)のか、実際に読もうと試みた
  // ものの、その瞬間には「頼んだ分まるごとには」届かなかった
  // (false、しかし再試行の余地はある――streamAtEof_はまだfalseの
  // まま)のかを、呼び出し元(Application::handleCommand()、
  // showPagerExitNotice()自身のコメント参照)が区別できるようにする。
  // 2026-09-20再訂正、直接のユーザー報告("旧の動きを見てみたら、
  // モードラインの行情報が１行ずつ進んでいる。つまり実際に一行ずつ
  // 読んでるわけ？")――legacy自身のlinein()(old/src/alloc.c)は
  // 実質1行単位でしか要求しないため、「その1行」が届かなければ即座に
  // 検出できる。この移植は意図的に大きな先読み予算を一度に要求する
  // ので、「合計で1行でも増えたか」ではなく「頼んだ分にまるごと
  // 届いたか」を基準にする――部分的に(頼んだより少なく)しか届かな
  // かった場合も、その部分自体はensureLoadedThrough()の戻り値
  // (true)経由で正しく表示に反映されつつ、これは「まだ頼んだ全部には
  // 届いていない」ことを別途伝える。直後の1回のensureLoadedThrough()
  // 呼び出しについてだけ意味を持つ。
  // 2026-09-06再訂正(5回目、直接のユーザー報告「実際の内容(250行)を
  // ほぼ全部捨てて早期にソケットを閉じてしまうバグが旧にあるのなら、
  // なぜ最後まで読めるの？」を経て): streamAtEof_を無条件でORして
  // いる――lastLoadAttemptYieldedNothing_自身はensureLoadedThrough()
  // の毎回の呼び出し冒頭でfalseにリセットされる一時的な変数なので、
  // 「本物のEOFにloadStream()自身の初回読み込みで(ensureLoadedThrough()
  // が一度も呼ばれる前に)当たった」場合はこの変数を立てる機会自体が
  // 無い――streamAtEof_(それ自身は既にloadStream()でのみリセットされる
  // sticky状態)を直接ORすることで、いつ/どの経路でEOFに当たったかに
  // 関わらず常に正しくなる。
  bool lastLoadAttemptYieldedNothing() const { return lastLoadAttemptYieldedNothing_ || streamAtEof_; }
  // 2026-09-06再訂正(3回目)、直接のユーザー報告("④実際は２つ目の`j`
  // で（Exit)"、legacy自身の実機を`seq 1 500 | U`(内容は明らかに
  // まだ大量に残っている)で確認した結果): legacy自身の"(Exit)"は
  // 一度出ると、bell自体は最初の1回きりだが、テキスト表示("(Exit)")
  // 自体はその後の全てのナビゲート操作でも消えずに描画され続ける
  // (実際のPTYキャプチャで確認済み: 初回はBELを伴うが、以降の
  // 各`j`の応答には毎回"Exit"の文字列だけが含まれ、BELは含まれない)。
  // これはlastLoadAttemptYieldedNothing()自身(「直後の1回の呼び出し
  // についてだけ意味を持つ」、その宣言コメント参照)とは異なる、
  // このpagerReturnsToShell()セッション自身の「一度でも本物のEOFに
  // 当たったことがあるか」という粘着性のある(sticky)状態。一瞬の
  // EAGAIN/EWOULDBLOCK(まだ開いたまま、単に今この瞬間だけ届いて
  // いない)は意図的にこれのトリガーから除外している――O_NONBLOCK化
  // されたstreamFd_に対してはごくありふれて起こる、生きた相手に
  // とって何も異常ではない状態であり、これも含めてしまうと(実機PTYで
  // 確認済み)man/groffがまだ全然出力し終えていない読み込み開始
  // 直後から(Exit)が誤って粘着的に出てしまう。
  // Application::pollPagerExitNotice()自身のコメント参照: これを
  // 毎回の反復で無条件にチェックして描画し続けることで、legacyの
  // 「BELは一度きり、テキストは消えない」という見た目を、個々の
  // redraw()呼び出し箇所をいちいちフックすることなく再現する。
  // 2026-09-06追記: 専用のsticky変数ではなく、streamAtEof_(それ自身
  // 既にloadStream()でのみリセットされる、まさに同じ意味でsticky)
  // から直接計算する――専用の変数だと、loadStream()自身の初回の
  // readMoreIntoPipe()呼び出し(setPagerReturnsToShell(true)が呼ばれる
  // "前"に実行される)が本物のEOFに当たった場合、それを立てる機会
  // 自体が無い(フラグを立てるコードはensureLoadedThrough()の中に
  // しか無く、その時点ではまだ一度も呼ばれていない)という発見漏れが
  // あった。streamAtEof_から直接計算すれば、いつ/どの経路でEOFに
  // 当たったかに関わらず常に正しい。
  bool pagerExitEncountered() const { return pagerReturnsToShell_ && streamAtEof_; }
  bool hasActiveStream() const { return streamFd_ >= 0; }
  bool streamAtEof() const { return streamAtEof_; }
  // 2026-08-25のバグ修正: 上の`hasActiveStream()`は、ストリームが
  // 本当のEOFに達すると(streamFd_が-1にリセットされる、
  // readMoreIntoPipe()自身のコメント参照)falseになる――「まだ読む
  // ものがあるか」を判定するのには正しいが、「このファイル/標準入力
  // はそもそもCLIページャ起動で開かれたものか」の判定には「間違って
  // いる」。後者は、「同じ」loadStream()呼び出しの内容が画面に表示
  // され続けている限りtrueを返し続ける必要がある(実際のPTYバグ:
  // 既に完全に読み終わったパイプ入力源(例えば短い`ls -l /tmp |
  // usilic`)に対して^Gを押すと、hasActiveStream()が^Gが押された
  // 時点で既にfalseになっていたため、そのままreturnToDirectory()へ
  // 落ちてしまい、空のディレクトリ一覧が表示されていた)。読み込みの
  // 進み具合とは無関係に「これはページャ起動されたコンテンツか」を
  // 知る必要があるものは「これ」を使うこと(Command::Back自身の
  // File-modeの分岐)。pagerMode_自体は、EOFに達したことでは決して
  // リセットされず、「次の」loadFile()/loadStream()呼び出しでのみ
  // リセットされる。
  bool isPagerMode() const { return pagerMode_; }
  bool isRawControlCodesMode() const { return rawControlCodesMode_; }  // ^[M(loadFileRaw())で開いたか

  // 2026-08-25(ユーザーからの直接の依頼、実際のlegacyバイナリへの
  // 再確認の後、同日中に修正――素のFILEの受け渡しがなぜもはやこれを
  // 全く設定しないかについてはApplication::pollShellHandoff()自身の
  // コメント参照): 独立したCLIページャ起動(本物の端末、生きた親
  // usilicは無し)には前のモードが全く無いので、^G/'q'は文書通りの
  // まま(^Gは無効、'q'はプロセス全体を終了、「usilic v1.5b」という
  // rightWatermarkフォールバックが表示される――全て実際のlegacy
  // バイナリに対して確認済み)。「パイプされた標準入力」の
  // SHELLモードによる再exec受け渡しは具体的には(これをtrueに設定
  // し続ける、pagerMode_の残る唯一のケース)前のモード――それが
  // 入力されたその生きたSHELLセッション――を「持っている」ので、
  // ^G/'q'は何もしない/終了する代わりにそこへ戻り、モードラインは
  // 代わりにtrailing3にハイライト表示(反転表示、太字のrightWatermark
  // ではなく)の「SHELL」を表示する(ユーザーからの直接の訂正:
  // 「戻り値がハイライト表示の『SHELL』であるべき」――rightWatermark
  // の太字スタイルはこれには間違ったスロットだった)。既定はfalseで、
  // 全てのloadFile()/loadStream()呼び出しでリセットされる――
  // pollShellHandoff()自身のパイプされた標準入力の分岐だけが、
  // これをtrueに設定する唯一の呼び出し元である。
  void setPagerReturnsToShell(bool value) { pagerReturnsToShell_ = value; }
  bool pagerReturnsToShell() const { return pagerReturnsToShell_; }

  // 2026-09-15追加、直接のユーザー指摘("^O^MはSTDINなページャー仕様
  // へ完全移行していて^Gで（ls）へ戻れない"): loadFileAsPipe()(K-23、
  // ^O^M)はloadStream()を再利用するため、上のpagerReturnsToShell_と
  // 同じく既定でfalseのままだと、すぐ上のコメントが説明する「独立
  // したCLIページャ起動には戻り先が無い」という前提に巻き込まれ、
  // ^Gが無条件no-opになってしまっていた――しかしK-23は実際には
  // Directory一覧からの選択という、正真正銘の「前のモード」を持つ
  // (SHELLへの引き渡しと同じ理屈、戻り先がSHELLではなくDirectoryな
  // だけ)。pagerReturnsToShell_を「SHELLへ戻る」専用の意味のまま
  // 汚さず、並行するこの専用フラグで表現する――loadFileAsPipe()
  // 自身が設定し、他の全てのloadFile()/loadStream()呼び出しでは
  // (pagerReturnsToShell_と同じく)falseにリセットされる。
  void setPagerReturnsToDirectory(bool value) { pagerReturnsToDirectory_ = value; }
  bool pagerReturnsToDirectory() const { return pagerReturnsToDirectory_; }

  // 2026-09-17追加、Application::tryPollProgramHandoff()自身の宣言
  // コメント参照: ^Jp(PROGRAM)経由のパイプされたstdin引き渡し
  // ("usilic < file"、SHELLプロンプトでの"cat file|usilic"に相当する
  // PROGRAM版)専用――pagerReturnsToShell_の"SHELL"バッジと全く同じ
  // 考え方だが、こちらは戻り値ラベルが"PROGRAM"になる(実際の戻り先
  // モードはfileReturnMode_が別途保持する、呼び出し時にApplicationが
  // 実際にいたモード)。既定はfalse、他の全てのloadFile()/loadStream()
  // 呼び出しでリセットされる。
  void setPagerReturnsToProgram(bool value) { pagerReturnsToProgram_ = value; }
  bool pagerReturnsToProgram() const { return pagerReturnsToProgram_; }

  // 2026-09-21追加、直接のユーザー指摘("モードライン不良...正しくは
  // 「tcb-1.5b.linux.tgz」「00001/00025 TOP」「1894」「(tar)」
  // 「/Users/haa15030/」"): loadContentAsPipe()経由のARCHIVE分岐
  // (Application::openSelectedEntryNoWrap())専用のtrailing3上書き
  // ――ArchiveMode自身のModeLineFieldsProvider(name="ARC"、
  // trailing2="(tar)"のようなバッジ、trailing3=アーカイブの完全パス、
  // fillTrailingToEdge)と揃えたいが、ここは「アーカイブそのもの」
  // ではなく「アーカイブの一覧を見ているFileモード」なので、nameには
  // (固定の"ARC"ではなく、setSearchOrigin()経由で)実際のファイル名、
  // trailing3には(nameが既にファイル名を持っているのでフルパスでは
  // なく)所属ディレクトリだけを置く――pagerReturnsToShell_の"SHELL"
  // と同じtrailing3スロットを共有する(自身の宣言コメント参照、
  // 両立しない: ARCHIVE分岐がSHELLへ戻ることは無いため)。既定は空、
  // loadStream()の全ての呼び出しでリセットされる(pagerReturnsToShell_/
  // pagerReturnsToDirectory_と同じく)。
  void setPagerListingDirectory(const std::string &directory) { pagerListingDirectory_ = directory; }

  // 以前のストリーム状態を全てリセットする(所有しているfdを閉じる)
  // ――loadStream()の先頭(2つ目のCLI起動されたファイルが1つ目を
  // 置き換える場合、例えばCommand::PagerNextFile)と、loadFile()の
  // 先頭(防御的に: 通常の即時読み込みが古いストリームを宙ぶらりんの
  // まま残すことは決して無いようにする)で呼ばれる。
  // 2026-09-06追加、直接のユーザー報告("最初のman ls用のパイプが
  // 解放されていない...（stdin）を抜けたら同プロセスをkillしてパイプを
  // 解放して欲しい"): 元々この2箇所の内部用途だけを想定してprivate
  // だったが、Application::ensureShellActive()等がSHELLの生きたPAGER
  // ハンドオフセッション(pagerReturnsToShell())を(^G/'q'に限らず
  // ESC-s等の「他のどんな手段」であれ)離れる際にも、明示的にここを
  // 呼んでOS側のfd(ソケット)を閉じる必要が出たため公開した――
  // 呼び出し元自身のコメント参照。
  void closeStream();

  // usilicrcのCODEで見てバイナリか(先頭64KBだけ読む、binaryTypeText()と同じ
  // 判定)。2026-09-27追加、Application::openExternallyIfApplicable()用。
  bool isBinaryFile(const std::string &path) const;

  // 2026-09-28追加: EPUB(zip)のメンバーを`unzip -p`(ConfiguredRole::
  // UnzipExtract)で取り出す関数。unzipはメンバー名をglobとして解釈するので
  // Application::escapeArchiveMemberPattern()と同じ文字をエスケープする。
  // 本文の変換(loadAndTransformContent())と、CUTの一覧からの画像の
  // クイックルック(Application::openSelectedSegment())が使う。
  EpubMemberReader epubMemberReader(const std::string &epubPath) const;

  // 2026-09-28追加、直接のユーザー提案("選択できる章はタグジャンプとしての
  // アンダーラインを付加し returnで当該行へ飛んではどうか"): EPUBの本文で、
  // カーソル行に本の中へのリンク(下線)があれば、リンク先の行へカーソルを
  // 移してそこを画面の先頭にする。リンクが無ければfalse。
  bool followEpubLink();
  bool hasEpubLinkAt(int index) const;  // 画面の行indexにリンクがあるか(クリック用)
  std::optional<int> epubLinkTargetForRawLine(int rawLine) const;  // 縦書きのクリック用
  // http(s)のリンクのURL(EpubLayoutHints::webLinks、2026-09-30)。
  std::optional<std::string> epubWebLinkForRawLine(int rawLine) const;
  // 元の行番号rawLineの行へ飛ぶ(^X^Xでここへ戻れる、followEpubLink()と同じ)。
  bool jumpToEpubRawLine(int rawLine);
  // 縦書きの本の中の横書き/扉の行の範囲(rawLineNumber、EpubLayoutHints参照)。
  const EpubLayoutHints &epubLayoutHints() const { return epubLayoutHints_; }
  // 2026-10-03追加(横書きのEPUBの画像、.steering/20261003-epub-image-page/のフェーズ5): 画像の帯の行数を
  // 返す関数(本のパス、画像の名前、画面の幅(桁)→行数、0なら置き換えない)。Applicationが-gの時だけ渡す。
  // あればreadAndWrap()がEPUBの画像だけの行を画像の行(ContentView::Line::imageKey)に置き換える。
  using EpubImageRows = std::function<int(const std::string &epubPath, const std::string &imageName, int width)>;
  void setEpubImageRows(EpubImageRows rows) { epubImageRows_ = std::move(rows); }
  // 読み込みを始める前(開けると分かった後、描く前)に呼ぶ関数。Applicationが、前に見ていたファイル
  // だけに関わる表示の状態(縦書きのEPUBの横書きの範囲で見せる行の範囲等)を消すのに使う(2026-10-06)。
  // 2026-10-23: 次の読み込みの、読んだ後の変換(expandHelpMarkup())の間にも時々呼ぶ(Application::readingAbortCheck()の
  // 「Reading...」)。読み込みが終わると外す。
  void setLoadProgressTick(std::function<void()> tick) { loadProgressTick_ = std::move(tick); }
  void setBeforeLoad(std::function<void()> callback) { beforeLoad_ = std::move(callback); }

 private:
  // loadFile()/loadFileNoWrap()/loadFileRaw()が共有する本体――呼び出し元が
  // 先にnoWrapMode_/rawControlCodesMode_をセットしてから呼ぶ。shouldAbort
  // はloadFileRaw()自身のコメント参照、rawControlCodesMode_が真かつ
  // 非空の場合だけreadAndWrapChunked()へ回す。
  bool loadFileImpl(const std::string &path, const std::function<bool()> &shouldAbort = {});
  // loadFileRaw()自身の宣言コメント参照: readAndWrap()と同じ変換
  // ゼロ・同じ折り返し規約だが、ファイルをkChunkBytes単位で読んで
  // 都度PipeBuffer::feed()し、各チャンクの後にshouldAbort()を呼ぶ
  // ――trueが返ればそこまでに溜まった分だけでpipe.flush()して打ち切る
  // (呼び出し元は既に読み込んだ範囲のPipeBuffer出力を受け取る、
  // 途中経過であっても構造上壊れたものにはならない――PipeBufferの
  // 行分割は既に確定した完全な行だけをlines()へ積む設計のため)。
  std::vector<ContentView::Line> readAndWrapChunked(const std::string &path,
                                                     const std::function<bool()> &shouldAbort) const;
  // readAndWrap()/loadFileAsPipe()が共有する: pathの生バイトを読み、
  // rawControlCodesMode_が真でなければHELP-markup展開/roff文書の
  // groff整形を適用する――loadAndTransformContent()自身のコメント参照。
  std::string loadAndTransformContent(const std::string &path) const;
  std::vector<ContentView::Line> readAndWrap(const std::string &path, const std::function<bool()> &shouldAbort = {}) const;
  // loadStream()/ensureLoadedThrough()が共有する: streamFd_から
  // 有限サイズのチャンクで読み込み、それぞれをstreamPipe_へ供給
  // する。streamPipe_->lines()がtargetLineCountに達するか、EOFに
  // 当たる(これはstreamPipe_をflush()もし、まだ保留中の末尾の
  // 途中の行を確定させ、streamOwnsFd_ならstreamFd_を閉じる)まで
  // 続ける。
  // 2026-09-20追加、直接のユーザー報告("旧の動きを見てみたら、
  // モードラインの行情報が１行ずつ進んでいる。つまり実際に一行ずつ
  // 読んでるわけ？")への調査結果を反映する戻り値: legacy自身の
  // linein()(old/src/alloc.c)は本当に1バイト単位(実質1行単位)でしか
  // 要求しないため、パイプの向こう側がほんの一瞬でも書き込みを
  // 止めれば即座に「今回は届かなかった」と分かる。この移植は
  // 効率のため意図的に大きな先読み(targetLineCount、複数行分)を
  // 一度に要求するので、「合計の行数が1行でも増えたか」だけを見る
  // 判定では、大きなバッチの途中どこかで一瞬でも止まったという
  // legacyと同じ粒度の信号を拾えない――バッチ全体としては「頼んだ分
  // より少ないが、それでも前進はした」という結果にしかならず、
  // 「一瞬でも止まったか」自体が呼び出し元から見えなくなってしまう。
  // 戻り値は、ループが「頼んだ行数に到達したから」でも「本物のEOFに
  // 当たったから」でもなく、真にEAGAIN/EWOULDBLOCK(または他の
  // read()失敗)によって「今はこれ以上何も無い」を理由に抜けた場合
  // にだけtrueを返す――ensureLoadedThrough()自身のlastLoadAttemptYieldedNothing()
  // が、これを直接使ってlegacyと同じ粒度でこの一瞬の停止を検出する。
  bool readMoreIntoPipe(int targetLineCount);
  std::vector<ContentView::Line> convertPipeLines() const;
  // reflow()自身の2026-09-08追加コメント参照――再折り返しの前後で
  // カーソルが指す「生の」行番号を保つための一対のヘルパー。
  std::optional<int> rawLineNumberAt(int index) const;
  int translateCursorAfterReflow(int beforeCursor, std::optional<int> beforeRawLine) const;

  ContentView &view_;
  History &history_;
  const Configuration &configuration_;
  Charset::Encoding encoding_;
  const std::string *errorLogPath_ = nullptr;  // setErrorLogPath()参照
  std::optional<std::string> binaryTypeText(const std::string &path, const std::string &content) const;
  bool looksBinary(const std::string &content) const;  // binaryTypeText()の判定部分だけ
  int width_;
  // 2026-09-14追加、直接のユーザー依頼("（ls）での^O^M/^[^Mを実装
  // して"): loadFileNoWrap()/loadFileRaw()自身のコメント参照。
  // 「次に」readAndWrap()/reflow()が使うモードを覚えておく――
  // loadFile()(通常の読み込み)は毎回両方falseへリセットする。
  bool noWrapMode_ = false;
  bool rawControlCodesMode_ = false;
  // followEpubLink()用: EPUBの本文のリンクがある行→リンク先の行(どちらも
  // rawLineNumber)。loadAndTransformContent()(const)が変換のたびに作り直す。
  mutable std::map<int, int> epubLinks_;
  // 表示用に最後に通したプログラム(FILE節のgzip等、roffならgroff)。モードラインの"(groff)"等
  // (loadAndTransformContent()参照、2026-10-08)。
  mutable std::string contentProgram_;
  std::function<void()> loadProgressTick_;  // setLoadProgressTick()参照
  mutable EpubLayoutHints epubLayoutHints_;  // epubLinks_と同じく変換のたびに作り直す
  std::vector<ContentView::Line> expandEpubImageLines(std::vector<ContentView::Line> lines, const std::string &path,
                                                     int width) const;
  EpubImageRows epubImageRows_;  // setEpubImageRows()参照
  std::function<void()> beforeLoad_;  // setBeforeLoad()参照
  std::string currentFile_;
  bool skipNextRecentPush_ = false;  // skipNextRecentPush()参照
  bool skipRecentPushNow_ = false;   // 今のloadFile()の間だけのskipNextRecentPush_
  AnnotationStack<FileAnnotationKind> annotations_;
  // pagerMode_(2026-08-25): loadStream()が設定し、loadFile()が
  // クリアする――ページャ起動されたファイルのモードラインが、
  // 通常のloadFile()で開かれたファイルとは異なり、サイズ/ディレクト
  // リのtrailingフィールド無しで完全パス(標準入力の場合は何も無し)
  // を表示する理由についてはコンストラクタ自身のModeLineFieldsProvider
  // のコメント参照。
  bool pagerMode_ = false;
  bool pagerReturnsToShell_ = false;
  bool pagerReturnsToDirectory_ = false;  // setPagerReturnsToDirectory()自身の宣言コメント参照
  bool pagerReturnsToProgram_ = false;    // setPagerReturnsToProgram()自身の宣言コメント参照
  // loadTransformedPagerFile()で開いたページャーで、通したプログラム(モードラインの"(groff)")。空なら素のファイル。
  // reflow()はcurrentFile_を開き直さず、組み直したものを読む。loadStream()が毎回消す。
  std::string pagerProgram_;
  std::string pagerLabel_;
  bool pagerTransformed_ = false;  // loadTransformedPagerFile()で開いた(reload()は同じく組み直して開く)
  std::string contentProgramOverride_;  // setContentProgram()参照  // loadTransformedPagerFile()の`label`(モードラインの名前、空ならパス)。loadStream()が消す
  std::string pagerListingDirectory_;     // setPagerListingDirectory()自身の宣言コメント参照
  int streamFd_ = -1;
  bool streamOwnsFd_ = false;
  bool streamAtEof_ = false;
  bool lastLoadAttemptYieldedNothing_ = false;  // lastLoadAttemptYieldedNothing()自身の宣言コメント参照
  // 2026-08-25(ユーザーからの直接の訂正): ページャのモードライン
  // 自身のtrailingバイト数フィールドは、以前は表示済みの行テキストを
  // 「カーソル」の行まで合計していた。Shell自身の全く同じフィールド
  // を踏襲したものだったが、Shell版がそう「見える」のは、その
  // カーソルが出力がストリーミングされてくるにつれ常に最新の行に
  // 位置しているからに過ぎない――この移植自身の遅延読み込み型
  // ページャは、カーソルがどこにでも位置しうる(実際の報告: さらに
  // 下まで読み込んだ後に「上」へページを戻しても、カウントは
  // 「縮小してはならない」――「ページ戻しが起きたとしても...
  // バイト数は変わらない」)。正式に定まった仕様(「実際に読み込まれた
  // バイト数としたい」)はもっと単純で、カーソルとは全く無関係:
  // readMoreIntoPipe()がこれまでに実際にstreamFd_から引き出した
  // 生バイトの合計であり、単調増加し、表示済みの行の長さから再構成
  // するのではなく、そこで直接追跡される(自身のコメント参照)。
  std::size_t streamBytesRead_ = 0;
  std::unique_ptr<PipeBuffer> streamPipe_;
  // 2026-09-06追加、直接のユーザー報告("特に-g起動ではENDページになる
  // までガタガタなスクロールで滑らかさがない...１行単位の読み込みに
  // 出来ない？")――readMoreIntoPipe()自身のコメント参照。「あと1行」
  // というペーシング自体はensureLoadedThrough()の正確さのために必要
  // だが、それを1バイトずつのread()システムコールの連打で実現する
  // 必要は無い――1回のread()でまとめて読み、その中に含まれる「次の
  // 改行の直後」までだけをstreamPipe_へ供給し、それを超えて読めて
  // しまった分(次の行の先頭部分)はここへ一時的に留め置いて、次回の
  // 呼び出しで真っ先に消費する。これにより、システムコール数を実質
  // 1行につき1回まで減らしつつ、streamPipe_->lines()が要求した行数を
  // 絶対に超えて増えないという既存の保証はそのまま保たれる。
  std::string pendingReadAhead_;
};

}  // namespace fm
