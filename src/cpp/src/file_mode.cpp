#include "file_mode.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#include "help_text.hpp"
#include "pipe_buffer.hpp"

namespace fm {

// 2026-09-04追加、直接のユーザー依頼("usilic ~/git/usilic/src/HELP.txt
// をサポートして"): CLIのファイル引数起動(usilic HELP.txt/usilic
// QUICK.txt)で、<indentN>/<i>/<b>/<u>markupがそのままリテラルなタグと
// して表示されてしまうのではなく、HELP画面(^Jh)自身のkHelpTextJa/
// kHelpTextGuiJaと同じexpandHelpMarkup()展開が効いてほしい、という
// 要求。汎用的な「内容を見て自動判定」も検討したが、ユーザー自身の
// 判断で「HELP.txt/QUICK.txtという名前の時だけ発動すれば十分」に
// 絞られた――任意のプレーンテキストファイルがたまたま`<b>`等の
// 文字列を含んでいて誤検出する心配が無くなる。ファイル名(拡張子
// 込みのbasename)だけを見る、パス非依存の判定。
bool isHelpMarkupPagerFile(const std::string &path) {
  std::string name = std::filesystem::path(path).filename().string();
  return name == "HELP.txt" || name == "QUICK.txt";
}

FileMode::FileMode(ContentView &view, History &history, Charset::Encoding encoding, int width)
    : view_(view), history_(history), encoding_(encoding), width_(width) {
  // put_str()自身のf_markロジックからの移植(ContentView::
  // setCursorHighlightEnabled()のコメント参照): 実際のCUIでは、
  // Directoryモードの一覧(この移植は、このフラグが存在するずっと
  // 前から既に正しくハイライトしていた)とは異なり、プレーンな
  // ファイル内容は行全体の反転表示カーソルハイライトを決して受けない
  // ――実際のCUI/GUIの並べたスクリーンショットで確認済み。
  view_.setCursorHighlightEnabled(false);

  // モードラインフィールド、disp_window()自身のページャーケース向け
  // フィールドレイアウト('/'で始まらないname0は「そのままコピー」の
  // 分岐を取る、そのコメント参照)からの移植: 左側に(実際にCUIが表示
  // するものである)裸のファイル名――完全なパスでは「ない」――、
  // 右側にファイルのバイトサイズとそれを含むディレクトリ。
  // loadFile()を呼ぶたびに再設定するのではなく、ここで一度だけ
  // 登録される。currentFile_への生きたコールバックであり、
  // スナップショットではないため。
  view_.setModeLineFieldsProvider([this](int) -> ContentView::ModeLineFields {
    // pagerMode_(2026-08-25、loadStream()自身のコメント参照): CLI
    // ページャーの起動は、本当に異なる、より単純なモードラインを
    // 表示する――`name`として完全なパスをそのまま(stdinの場合は
    // 何も無い)、ディレクトリのtrailing2フィールドは全く無し、
    // そしてrightWatermarkフォールバック(legacy自身の
    // disp_window()は、そのスロットに置くべき「前モード」テキストが
    // 無い時は常にこれを表示する。下の他の全てのFileモードの
    // エントリは常にそれを持っているので、rightWatermarkはそこでは
    // 空のままである)。name/trailing2なし/透かしは、実際のlegacy
    // バイナリに対して直接確認済み(ページャー起動されたファイルの
    // PTYキャプチャ)。しかし位置フィールドとこの末尾バイト数は、
    // 直接の2026-08-25の追加依頼("SHELL同様、行数を5桁とバイト数
    // 表示をよろしく")であり、legacy自身のページャーのモードライン
    // を正確に複製する(そこには全くバイト数が無い、同じPTY
    // キャプチャで確認済み)のではなく、Shell自身の
    // setUseFixedWidthPosition(true)の設定(下のloadStream()で設定
    // される――自身のコメント参照)を再利用している――これは意図的な、
    // 依頼された逸脱であり、見落としではない。
    //
    // 2026-08-25の修正(実際のlegacyバイナリを再確認した上での、
    // 直接のユーザーによる追加): pagerMode_/このブランチ全体は、
    // 本物のCLIページャー起動(main.cpp自身の位置引数のファイル/
    // パイプされたstdinの起動)またはSHELL引き渡しのPIPE
    // (Application::pollShellHandoff()自身のソケット分岐)によって
    // だけ到達される――素のFILE引数のSHELL引き渡しは、もはや全く
    // ここに到達しない(pollShellHandoff()は今はloadStream()の代わりに
    // 通常のfileMode_.loadFile()を呼ぶので、下の、変更されていない
    // プレーンなDirectoryから到達したファイルの分岐に落ちる。
    // "SHELLプロンプトでusilic起動したときの引数の画面になれば
    // いいだけ...つまりはls画面で「.bashrc」を選択した場合と同じ"
    // に合わせている)。
    if (pagerMode_) {
      // 2026-08-25の修正(直接のユーザーによる追加): このフィールドは
      // 以前、Shell自身の同一のフィールドを反映して、表示されている
      // 行のテキストを「カーソルの」行まで合計していた――しかし
      // Shellのバージョンがそう見えるのは、出力がストリームで入って
      // くる際にそのカーソルが常に最新の行に座っているからに過ぎ
      // ない。この移植自身のページャーカーソルはどこにでも座り得る
      // (例えば下にページングした後上に戻った場合)し、確定した仕様
      // はカーソルとは全く関係が無い: "実際に読み込まれたバイト数
      // としたい...ページ戻しが起きたとしてもすでに読み込んだ分量
      // としてのバイト数は変わらない"――生の合計streamBytesRead_は、
      // これまでにストリームから実際に引き出した量である(自身の
      // 宣言コメント参照)。スクロール位置に関わらず単調増加する。
      std::size_t bytes = streamBytesRead_;
      // pagerReturnsToShell_(2026-08-25、自身の宣言コメント):
      // 今は「SHELL引き渡しのPIPE」(上参照)に対してだけtrueになる
      // ――独立したCLIページャー起動(ファイルまたはstdin、本物の
      // 端末から)は前モードが全く無いので、rightWatermarkは以前
      // 通り正確に"usilic v1.5b"(legacy自身の前モードなしの
      // フォールバック)のままである。SHELL引き渡しのパイプは
      // 「1つ」持っている――生きたSHELLセッション――が、直接の
      // ユーザーによる訂正は、それにrightWatermarkの太字スタイルを
      // 再利用することを拒否した("戻り値がハイライト表示の「SHELL」
      // となる...でよろしく"――太字ではなくハイライト/反転表示):
      // trailing3(自身の宣言コメント参照。既にまさに「3番目の
      // 独立してハイライトされる末尾セグメント」であり、以前は
      // ArchiveModeだけが使っていた)が、実際にその方法で描画する
      // 既存の仕組みなので、これは代わりに「それ」を使い、ここでは
      // rightWatermarkを空のままにする――他の全てのFileモードの
      // エントリの「前モードがあるので透かしは無い」慣習(このコメント
      // 全体自身の冒頭の段落参照)に合わせている。
      //
      // searchOriginDetail_(2026-08-25、Application::
      // pollShellHandoff()自身のコメント参照): 通常のCLIページャー
      // 起動では空(影響なし、そこではtrailing2は表示されない、
      // 実際のlegacyのPTYキャプチャに合わせている)。SHELLモードの
      // パイプ引き渡しに特有の"(stdin)"に設定される。Directoryの
      // "(ls)"/Archiveの"(tar)"バッジが占めているのと同じtrailing2
      // スロットに――直接のユーザー依頼であり、上の修正によって
      // 「変更されていない」(戻り値の表示自体だけが移動した)。
      ContentView::ModeLineFields fields;
      fields.name = currentFile_;
      fields.trailing = std::to_string(bytes);
      fields.rightWatermark = pagerReturnsToShell_ ? "" : "usilic v1.5b";
      fields.trailing2 = searchOriginDetail_;
      fields.trailing3 = pagerReturnsToShell_ ? "SHELL" : "";
      // 2026-08-25の修正(HISTORY.md:9462を引用した直接のユーザー
      // 報告、"基本的に右端まで埋まっていないモードラインは全部
      // 不良"): trailing3は「既に」fillTrailingToEdgeサポートを
      // 持っている(自身の宣言コメント参照、以前ArchiveMode向けに
      // 追加された)――ここでは単に一度も有効にされていなかった
      // ので、"SHELL"は他の全モード自身の最後のフィールドのように
      // パディングされる代わりに、行の右端の手前で止まっていた。
      // pagerReturnsToShell_に特有にスコープされている(無条件では
      // ない): 独立起動のケースはtrailing3を全く持たず、代わりに
      // rightWatermark自身の別のmoveCursor()による端への配置に
      // 依存しており、それは既に自力で端に到達している――そこでも
      // これを有効にすると、この報告の対象では決してなく、望まれた
      // ものとして確認もされていない、バイト数と"usilic v1.5b"の
      // 間のプレーンなギャップを新たに反転表示で埋めてしまうことに
      // なる。
      fields.fillTrailingToEdge = pagerReturnsToShell_;
      return fields;
    }
    if (currentFile_.empty()) return {};
    std::filesystem::path path(currentFile_);
    std::string directory = path.parent_path().string();
    if (!directory.empty() && directory.back() != '/') directory += '/';

    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    // バイトサイズとディレクトリは、1つの文字列ではなく、2つの
    // 「別々の」ModeLineFields::trailing/trailing2セグメントである
    // ――2026-08-10に追加。この修正の「以前の」バージョンは、実際の
    // 依頼("「1108009」と「/Users/.../」の間...「CLAUDE.md」と
    // 「00036/13736  0%」みたいにして欲しい")を、単に1つのハイライト
    // された文字列「内」の空白を広げることで解決しようとしたが、
    // (2つの参照スクリーンショットの実際のピクセル色を比較して確認
    // 済みの通り)それは依然として1つの途切れない反転表示の連続の
    // ままだった――要求された見た目は、"CLAUDE.md"と"00034/13761
    // 0%"の間に、本物のプレーンな(ハイライトされていない)ギャップ
    // があり、それを再現できるのは2つの独立してstandout化された
    // セグメント(trailing2自身のコメント参照)だけである。余分な
    // 空白を含む1つの文字列ではできない。drawSegment()は与えられた
    // 全てを一様にハイライトするため。サイズ自体は、プレーンな、
    // グループ化されていないstd::to_string()のままである
    // (別の、以前の誤読が既に確立し撤回した"ファイルサイズは
    // 「1108009」のままでいい")。
    std::string trailing = ec ? directory : std::to_string(size);
    // searchOriginLabel_/searchOriginDetail_(setSearchOrigin()自身の
    // コメント参照)は、設定されている時は常にname/trailing2
    // フィールドを乗っ取る――DirectoryMode自身のプロバイダが
    // searchAnnotation_対"usilic v1.5b"のrightWatermarkに対して
    // 行っているのと同じ置き換えで、単にここでは1つではなく2つの
    // フィールドにまたがっているだけ。
    std::string name = searchOriginLabel_.empty() ? path.filename().string() : searchOriginLabel_;
    // searchAnnotation_(2026-08-28の追加、setSearchAnnotation()自身の
    // コメント参照): searchOriginLabel_/Detail_(両方が何らかの形で
    // 設定されている場合、依然として完全に優先する――LOCATE/INFO
    // 自身の「これは本当にはファイル自身の名前についてのものでは
    // ない」という位置付けが優先される)より下の「3番目の」階層
    // ――それら2つとは異なり、これは`name`には手を付けず、プレーンな
    // 「含むディレクトリ」のtrailing2フォールバックを置き換える
    // だけ。DirectoryMode::searchAnnotation_の同一のname/trailing2
    // 分割を反映している。
    std::string trailing2 =
        ec ? "" : (searchOriginLabel_.empty() ? (searchAnnotation_.empty() ? directory : searchAnnotation_) : searchOriginDetail_);
    // Fileモードのtrailingフィールドは、legacyの端まで埋める
    // スタイル、ModeLineFields::fillTrailingToEdge自身のコメント
    // 参照。ここにはrightWatermarkは無い――Fileモードが決してそれを
    // 表示しない理由は自身のコメント参照。trailing2Kind=PathMiddle
    // は、LOCATE(searchOriginLabel_設定時)とsearchAnnotation_の
    // 両方に対して――これが他の全プロバイダの末尾のみの切り詰めに
    // 対する唯一の例外である理由はContentView::TrailingKind自身の
    // コメント参照。フォールバック(プレーンな含むディレクトリの
    // PATH)は代わりにPathを使い、利用可能な幅全体を使う。
    // searchAnnotation_(2026-08-28の修正、直接のユーザー報告+例:
    // "「- レガシーC実装...src/config.sh」...使えるだけ使う":
    // QueryEcho自身の固定16文字の末尾上限は、実際に利用可能な幅が
    // 必要とするよりもはるかにきつく、一致した行のテキストを切り
    // 詰めていた――"CLAUDE.md"のような短い名前は、このフィールドが
    // 実際に使うべき十分な余地を残す。PathMiddleは予算を認識する
    // (middleEllipsisTruncate()は、withTruncatedFields()自身の
    // パイプライン経由で、実際に「残っているもの」に合わせてサイズを
    // 決める)し、既に依頼されたまさにその"- レガシーC実装...
    // src/config.sh"の形に合わせてテキストの両端を保持している
    // ――そして下のsplitLayoutWhenCrowded(このプロバイダに対して
    // 既に無条件で有効)は、name+trailing2が一緒になって本当に
    // 両方収まらない場合に、「反対側」で自動的に発動するものである。
    ContentView::ModeLineFields fields{name,
                                        trailing,
                                        /*fillTrailingToEdge=*/true,
                                        /*rightWatermark=*/"",
                                        trailing2,
                                        /*trailingPlain=*/false,
                                        /*trailing2RightJustified=*/false,
                                        /*trailing2Kind=*/(searchOriginLabel_.empty() && searchAnnotation_.empty())
                                            ? ContentView::TrailingKind::Path
                                            : ContentView::TrailingKind::PathMiddle};
    // splitLayoutWhenCrowded(2026-08-27の追加、直接のユーザー報告/
    // 自身の以前のHISTORY.md:9608提案への呼び戻し。archiveContentView_
    // 自身のプロバイダが既に採用している――application.cppの自身の
    // コメント参照): 実際の、バイト数フィールドを押し出すほど長い
    // CJKファイル名は、以前はfitModeLineFields()自身の通常のall-or-
    // nothing階層を介してそれを丸ごと黙って落としていた。
    // DirectoryMode自身の`(ls)`一覧がまさにこの同一の修正を受けた
    // のと同じ種類のバグ。
    fields.splitLayoutWhenCrowded = true;
    return fields;
  });
}

std::vector<ContentView::Line> FileMode::readAndWrap(const std::string &path) const {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  std::ostringstream buffer;
  buffer << in.rdbuf();
  if (!in.good() && !in.eof()) return {};
  std::string content = buffer.str();
  // isHelpMarkupPagerFile()自身のコメント参照(HELP.txt/QUICK.txtの
  // <indentN>/<i>/<b>/<u>markupを、リテラルなタグのままではなく
  // ^Jh自身のkHelpTextJa/kHelpTextGuiJaと同じ規約で展開する)。
  if (isHelpMarkupPagerFile(path)) content = expandHelpMarkup(content);

  PipeBuffer pipe(encoding_, width_, 64L * 1024 * 1024);
  pipe.feed(content);
  pipe.flush();

  std::vector<ContentView::Line> lines;
  lines.reserve(pipe.lines().size());
  // styled.attributes(2026-08-05): PipeBuffer自身のオーバーストライク
  // 検出(nroffスタイルの太字/下線)は既にこれを生成している――以前は
  // ここで捨てられていた。FileModeがPipeBufferのStyledLinesを実際に
  // 画面へ届くものへ変換する、唯一の場所。
  for (const auto &styled : pipe.lines()) lines.push_back({styled.text, std::nullopt, std::nullopt, styled.attributes});
  return lines;
}

bool FileMode::loadFile(const std::string &path) {
  std::ifstream probe(path, std::ios::binary);
  if (!probe) return false;
  probe.close();

  // 「毎回の」読み込みでプレーンなページャーのデフォルトへリセット
  // する(2026-08-18): コンストラクタ自身のsetCursorHighlightEnabled
  // (false)は「最初の」読み込みより前の状態しかカバーしない。
  // Application::executeShowUsilicBanner()は、自身のloadFile()呼び出し
  // の直後にこれを再びONへ切り替える(^T画面のWORK/arc.N行は、
  // 選択可能であるために可視のハイライトを必要とする――実際の報告
  // =cqが、それ無しでは選ぶことが不可能であることを見つけた)ので、
  // このリセットが、そのフラグが後で読み込まれる「次の」通常の
  // ファイルへ漏れ出す(バナーからopenSelectedBannerEntry()経由で
  // 再度開かれるw.Nエントリ自身を含む、まさにこの同じloadFile()を
  // 通る)ことを防ぐものである。
  view_.setCursorHighlightEnabled(false);
  closeStream();  // 防御的措置: 通常の即時読み込みは、古びたストリームをぶら下げたままにしてはならない
  pagerMode_ = false;
  pagerReturnsToShell_ = false;  // 防御的措置、自身の宣言コメント参照。pagerMode_がfalseの間はいずれにせよ未使用
  view_.setUseFixedWidthPosition(false);  // loadStream()自身のコメント参照。他の全てのFileモードのエントリはプレーンなまま

  view_.setLines(readAndWrap(path));
  currentFile_ = path;
  // 新しい読み込みは古い検索を無効にする――setSearchOrigin()自身のコメント参照
  searchOriginLabel_.clear();
  searchOriginDetail_.clear();
  searchAnnotation_.clear();  // setSearchAnnotation()自身のコメント参照
  // K-46(^X^F、「FILE: Select file name」)自身のデータソース――
  // K-48自身のRecentDirectoryの対応物に対するDirectoryMode::
  // enterDirectory()の同一のpushRecent()呼び出し参照。
  history_.pushRecent(RingCategory::RecentFile, currentFile_);
  if (auto pos = history_.positionFor(PositionCategory::File, currentFile_)) {
    int idx = pos->row;
    int last = static_cast<int>(view_.lines().size()) - 1;
    if (idx < 0) idx = 0;
    if (idx > last) idx = (last < 0) ? 0 : last;
    view_.setCursorIndex(idx);
  } else {
    view_.setCursorIndex(0);
  }
  view_.redraw();
  // 現在の行に本物のカーソルを置く。プレーンなファイル内容には
  // それ以外にそれを示す行全体のハイライトが無いため
  // (ContentView::syncVisibleCursor()自身のコメント参照)――
  // 2026-08-04のユーザー依頼: 現在の行は、-c/-gの両方で目に見える
  // 形かつプログラムから知ることができる必要が依然としてある
  // (現在の行でエディタを起動すること、および将来のCOPYモードの
  // 行+列選択は、両方とも本物のカーソル位置に依存する)。
  view_.syncVisibleCursor();
  return true;
}

bool FileMode::reflow(int newWidth) {
  // 2026-08-30、実際のユーザー報告("フォントはOKだが文字数がおかしい
  // 、HELPはOKだけどね"): pagerMode_(loadStream()経由、CLI引数での
  // ファイル起動)の間は、これまでこの下のreadAndWrap()がview_のみを
  // 新しい幅で再構築し、streamPipe_(readMoreIntoPipe()/
  // ensureLoadedThrough()が実際に使い続ける、古い幅のまま生き残る
  // 別のバッファ)を一切更新していなかった。その結果、この直後に
  // ユーザーがスクロールしてensureLoadedThrough()が一度でも呼ばれる
  // と、古い幅で追加で読み込まれた行がview_.setLines()経由でここの
  // 正しい再折り返しを黙って上書きしてしまっていた。
  if (pagerMode_ && hasActiveStream()) {
    int oldLineCount = static_cast<int>(streamPipe_->lines().size());
    if (streamOwnsFd_ && !currentFile_.empty()) {
      // 本物の(CLI引数の)ファイルは、シーク可能なfdなので最初から
      // 読み直せる――loadStream()自身のコメント、
      // Application::startPagerFileList()自身の::open()呼び出し参照。
      ::close(streamFd_);
      streamFd_ = ::open(currentFile_.c_str(), O_RDONLY);
      streamAtEof_ = false;
      streamBytesRead_ = 0;
    }
    // streamOwnsFd_がfalseの場合(本物のstdinパイプ等)は巻き戻せない
    // ――streamFd_はそのまま、これ以降に新しく読む分だけ新しい幅で
    // 続きから再開する。既に表示済みだった内容が失われるのは、
    // フォント/GUIサイズ変更がライブなstdinページャーの最中に起きる
    // という稀なケースに限られる、既知の割り切り。
    width_ = newWidth;
    streamPipe_ = std::make_unique<PipeBuffer>(encoding_, width_, 256L * 1024 * 1024);
    if (streamFd_ >= 0) readMoreIntoPipe(std::max(oldLineCount, 1));
    int oldCursor = view_.cursorIndex();
    view_.setLines(convertPipeLines());
    int last = static_cast<int>(view_.lines().size()) - 1;
    if (oldCursor < 0) oldCursor = 0;
    if (oldCursor > last) oldCursor = (last < 0) ? 0 : last;
    view_.setCursorIndex(oldCursor);
    view_.redraw();
    view_.syncVisibleCursor();
    return true;
  }

  if (currentFile_.empty()) return false;
  width_ = newWidth;
  int oldCursor = view_.cursorIndex();

  view_.setLines(readAndWrap(currentFile_));
  int last = static_cast<int>(view_.lines().size()) - 1;
  if (oldCursor < 0) oldCursor = 0;
  if (oldCursor > last) oldCursor = (last < 0) ? 0 : last;
  view_.setCursorIndex(oldCursor);
  view_.redraw();
  view_.syncVisibleCursor();
  return true;
}

const std::string &FileMode::currentFile() const { return currentFile_; }

void FileMode::setSearchOrigin(const std::string &label, const std::string &detail) {
  searchOriginLabel_ = label;
  searchOriginDetail_ = detail;
}

void FileMode::setSearchAnnotation(const std::string &text) { searchAnnotation_ = text; }

void FileMode::setEncoding(Charset::Encoding encoding) { encoding_ = encoding; }

void FileMode::saveCursorPosition() {
  if (currentFile_.empty()) return;
  History::CursorPosition pos;
  pos.row = view_.cursorIndex();
  pos.totalRows = static_cast<int>(view_.lines().size());
  pos.top = 0;
  pos.lineNumber = -1;
  history_.setPosition(PositionCategory::File, currentFile_, pos);
}

void FileMode::closeStream() {
  if (streamFd_ >= 0 && streamOwnsFd_) ::close(streamFd_);
  streamFd_ = -1;
  streamOwnsFd_ = false;
  streamAtEof_ = false;
  streamPipe_.reset();
}

void FileMode::readMoreIntoPipe(int targetLineCount) {
  if (streamAtEof_ || streamFd_ < 0) return;
  // 意図的に「小さい」読み込みバッファ(2026-08-25、実際のPTY
  // キャプチャに対して調整済み): read()は1回の呼び出しで最大この
  // バイト数を返し、下のループは呼び出しの「間」でだけ
  // streamPipe_->lines().size()をチェックし、チャンクの途中では
  // チェックしない――短い行の内容(例えば`seq`自身の出力)に対する
  // 大きなバッファは、1回の一飲みで、要求されたtargetLineCountを
  // 意図していたよりもはるかに超過してしまう可能性がある(確認済み:
  // 8192バイトのバッファは、意図された約48行の初期ページに対して、
  // 最初の1回の呼び出しだけで約1859行を引き込んだ)。256バイトは、
  // 行の長さに関わらずその超過を小さく保つ。同じ合計バイト数に対する
  // より多くのread()呼び出しというコストを払って――このメソッドの
  // 実際の目的(一度に、legacy自身が観測した約23〜25行の初期読み込み
  // に合わせて、おおよそ画面1枚分より多い入力でブロックしない)に
  // 対しては無視できるコストである。
  // 2026-09-20追加、直接のユーザー報告("最終行へ来てもいないのに
  // （Exit）が出てこそ価値がある"、legacy実機での再現確認): loadStream()
  // 自身のO_NONBLOCK化のコメント参照。この下の分岐(EINTR以外の負の
  // 戻り値)は「本物のread失敗をEOFのように扱う」ものとして元々
  // 書かれていたが、実際にはstreamAtEof_を一切設定せずstreamFd_も
  // 閉じないまま単にループを抜けるだけだった――O_NONBLOCK化した今、
  // EAGAIN/EWOULDBLOCK(生きたSHELLパイプ/ソケットの向こう側がその
  // 瞬間まだ何も送っていない、"真のEOF"ではない)もこの同じ分岐へ
  // 素通しで落ちる。これはまさに欲しかった挙動そのもの――legacy自身の
  // (stdin)パイプの実機再現("EOFに出くわす時が必ず来るんだが最終
  // ページよりも手前が常だった")と同じ「一時的に何も届かなかった
  // だけ、streamFd_はまだ開いたまま、次回の呼び出しでまた読み直せる」
  // という意味論に、狙って変更することなく既に一致していた。
  char buf[256];
  while (static_cast<int>(streamPipe_->lines().size()) < targetLineCount) {
    ssize_t n = ::read(streamFd_, buf, sizeof(buf));
    if (n < 0) {
      if (errno == EINTR) continue;
      break;  // EAGAIN/EWOULDBLOCK(一時的に何も無い)も含め、ここで抜ける――すぐ上のコメント参照
    }
    if (n == 0) {
      streamAtEof_ = true;
      streamPipe_->flush();  // まだ保留中の末尾の部分行を確定する
      if (streamOwnsFd_) ::close(streamFd_);
      streamFd_ = -1;
      break;
    }
    streamPipe_->feed(std::string(buf, static_cast<std::size_t>(n)));
    streamBytesRead_ += static_cast<std::size_t>(n);  // 自身の宣言コメント参照: 確定した「実際に読み込まれたバイト数」の仕様
  }
}

std::vector<ContentView::Line> FileMode::convertPipeLines() const {
  std::vector<ContentView::Line> lines;
  lines.reserve(streamPipe_->lines().size());
  for (const auto &styled : streamPipe_->lines()) lines.push_back({styled.text, std::nullopt, std::nullopt, styled.attributes});
  return lines;
}

bool FileMode::loadStream(int fd, bool ownsFd, const std::string &displayName, int initialLineBudget) {
  closeStream();
  view_.setCursorHighlightEnabled(false);
  pagerMode_ = true;
  pagerReturnsToShell_ = false;  // 自身の宣言コメント参照。pollShellHandoff()はこの呼び出しの後にこれを切り替える
  // 2026-08-25(直接のユーザー依頼: "SHELL同様、行数を5桁とバイト
  // 数表示をよろしく")――他の全てのFileモードのエントリが使う
  // 可変幅のデフォルトの代わりに、同じ固定5桁ゼロパディングの
  // "NNNNN/NNNNN END"位置フォーマットのために、Shell自身の同一の
  // 設定(Application::startShell()自身のsetUseFixedWidthPosition
  // (true)呼び出し)を再利用する。
  view_.setUseFixedWidthPosition(true);
  streamFd_ = fd;
  streamOwnsFd_ = ownsFd;
  streamAtEof_ = false;
  lastLoadAttemptYieldedNothing_ = false;
  // 2026-09-20追加、readMoreIntoPipe()自身のO_NONBLOCK化のコメント
  // 参照(直接のユーザー報告+legacy実機での再現確認)――失敗しても
  // 無視して構わない(通常のブロッキングのfdのまま続行するだけ、
  // 生きたSHELLからの引き渡しの生きたパイプ/ソケット以外――通常の
  // ファイル/リダイレクトされたstdin等――ではどのみち意味の無い
  // フラグ)。
  ::fcntl(streamFd_, F_SETFL, ::fcntl(streamFd_, F_GETFL, 0) | O_NONBLOCK);
  streamBytesRead_ = 0;  // 自身の宣言コメント参照――新しいストリームはこれを0から再開する
  streamPipe_ = std::make_unique<PipeBuffer>(encoding_, width_, 256L * 1024 * 1024);
  currentFile_ = displayName;
  searchOriginLabel_.clear();
  searchOriginDetail_.clear();
  if (!displayName.empty()) history_.pushRecent(RingCategory::RecentFile, displayName);
  readMoreIntoPipe(std::max(initialLineBudget, 1));
  view_.setLines(convertPipeLines());
  // 2026-08-25のバグ修正(実際の報告): ContentView::setLines()は、
  // 以前のcursorIndex_を新しい内容の範囲内に「収める」だけである
  // ――それを0にリセットは「しない」(同じ内容のリフレッシュを
  // またいでカーソル位置を保存「したい」呼び出し元に対しては正しい、
  // 自身のコメント参照)――そのため2回目のloadStream()呼び出し
  // (例えば、同じ生きたSHELLプロンプトで再度入力された新しい
  // `cat x|usilic`)は、「最初の」セッション自身のナビゲーションが
  // 残していったcursorIndex_を何であれ黙って引き継いでいた。実際の
  // 再現: セッション1が119行のストリームでESC->(末尾へ)を押し、
  // cursorIndex_が118に着地した。セッション2自身のストリームは
  // わずか51行しか無かったので、その引き継がれた118は、0から始まる
  // 代わりに、新しい内容自身の最後の行である50まで切り詰められ、
  // 真新しい(stdin)セッションが既にその末尾までスクロールされた
  // 状態で開いてしまう("冒頭行から始まらない")。上のloadFile()は、
  // 自身の明示的なsetCursorIndex()呼び出し(履歴から復元、または0)
  // 経由で既にこれを正しく行っている――loadStream()にはその対応物が
  // 一度も無かった。全てのページャー起動(独立したCLI、またはSHELL
  // 引き渡しのパイプ)は無条件で新しい開始であり、CUI自身の慣習と、
  // (stdin)に特有に確立された直接の原則に合わせている:
  // "常時真っ新から始まる"――なのでこれは常に0であり、決して復元
  // されたHistory位置ではない(ページャーセッションはHistory経由の
  // カーソル位置の保存/復元を全く行わない――loadFile()自身の
  // currentFile_をキーとする検索を参照。currentFile_はCLIの使い
  // 捨てパスか、stdinに対して永続的に空かのどちらかであるため、
  // ここでは決して当てはまらない)。
  view_.setCursorIndex(0);
  return true;
}

bool FileMode::ensureLoadedThrough(int targetLineIndex) {
  // lastLoadAttemptYieldedNothing()自身の宣言コメント参照――「今回は
  // 何も試みなかった」場合との区別のため、実際に試みるまではfalseの
  // ままにしておく。
  lastLoadAttemptYieldedNothing_ = false;
  if (streamAtEof_ || streamFd_ < 0) return false;
  int before = static_cast<int>(streamPipe_->lines().size());
  if (targetLineIndex < before) return false;  // 既に十分読み込まれている(試みてすらいない)
  readMoreIntoPipe(targetLineIndex + 1);
  if (static_cast<int>(streamPipe_->lines().size()) == before) {
    lastLoadAttemptYieldedNothing_ = true;  // 実際に試みたが、この瞬間は何も届かなかった
    return false;
  }
  view_.setLines(convertPipeLines());
  return true;
}

}  // namespace fm
