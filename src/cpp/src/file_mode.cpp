#include "file_mode.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <system_error>

#include "charset.hpp"
#include "child_process.hpp"
#include "help_text.hpp"
#include "markdown_to_help_markup.hpp"
#include "pipe_buffer.hpp"
#include "utf8.hpp"

namespace fm {

// readAndWrap()自身のコメント参照(K-23、^O^M、長い行を折り返さない)。
// loadFileAsPipe()もこれを共有する――ファイル名だけ違う「読み込み
// 経路」であって、折り返し方針自体はloadFileNoWrap()と揃えたいため。
constexpr int kNoWrapWidth = 1 << 20;

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

// 2026-09-22追加、直接のユーザー依頼("せっかく.mdフォーマットで書かれているので
// usilicに文字装飾機能を実装してほしい"): 拡張子(大小無視)で判定する
// ――isHelpMarkupPagerFile()自身のコメントと同じ理由(内容を見て自動判定すると、
// たまたま`*`や`#`を含むプレーンテキストを誤検出しかねない)で、名前(ここでは
// 拡張子)だけを見る。
bool isMarkdownFile(const std::string &path) {
  std::string ext = std::filesystem::path(path).extension().string();
  for (char &c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return ext == ".md" || ext == ".markdown";
}

// old/src/code.cのis_man()から移植: `man[0]`が'.'で、続く1〜2文字が
// troffマクロ名らしく見え(".\\\""コメント、".B "/".I "、または英字1〜2
// 文字+空白/改行)、その位置が実際に行頭にある場合に真。legacy自身の
// check_ext()(old/src/child.c)は、これを先頭N_man(0x1000)バイトの中で
// 改行ごとに1回だけ試す――拡張子テーブルで一致しなかった通常ファイル
// に対する最後の判定手段として。
bool looksLikeRoffDocument(const std::string &content) {
  constexpr std::size_t kScanLimit = 0x1000;
  std::size_t limit = std::min(content.size(), kScanLimit);
  std::size_t pos = 0;
  while (pos < limit) {
    if (pos + 3 < content.size() && content[pos] == '.') {
      char c1 = content[pos + 1], c2 = content[pos + 2], c3 = content[pos + 3];
      if (c1 == '\\' && c2 == '"') return true;
      if ((c1 == 'B' || c1 == 'I') && c2 == ' ') return true;
      bool c1Upper = c1 >= 'A' && c1 <= 'Z';
      bool c1Lower = c1 >= 'a' && c1 <= 'z';
      if (c1Upper || c1Lower) {
        bool c2Lower = c2 >= 'a' && c2 <= 'z';
        bool c2Upper = c1Upper && c2 >= 'A' && c2 <= 'Z';
        if ((c2Lower || c2Upper) && (c3 == ' ' || c3 == '\n')) return true;
      }
    }
    std::size_t nl = content.find('\n', pos);
    if (nl == std::string::npos) break;
    pos = nl + 1;
  }
  return false;
}

FileMode::FileMode(ContentView &view, History &history, const Configuration &configuration,
                    Charset::Encoding encoding, int width)
    : view_(view), history_(history), configuration_(configuration), encoding_(encoding), width_(width) {
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
      // 2026-09-15追加、直接のユーザー指摘("^O^Mによる(stdin)は
      // （ls）から発動しているので戻り値も（ls）でないと（現状非
      // ハイライトのusilic v1.5b）")、同日中に単純化("usilic.1を
      // ^O^Mで開いたときの戻り値は、^Mで開いたときと同じでいい"):
      // 一度はpagerReturnsToShell_の"SHELL"に倣い専用の"(ls)"trailing3
      // バッジを追加したが、ユーザー自身の判断でこれは過剰と分かった
      // ――K-23の戻り値表示は、通常のloadFile()(K-22)が最初から
      // 「rightWatermark/trailing3のどちらも一切表示しない」のと
      // 全く同じにすればよい(下の非pager分岐参照、"Fileモードが
      // 決してそれを表示しない"のがまさにこの単純な既定)。ただし
      // rightWatermarkの「独立起動フォールバック」("usilic v1.5b")
      // だけは、それとは別に依然として抑制する必要がある――
      // pagerReturnsToDirectory_の間、戻り先が無い独立起動ではない
      // ことに変わりはないため。
      bool hasReturnTarget = pagerReturnsToShell_ || pagerReturnsToDirectory_ || pagerReturnsToProgram_;
      ContentView::ModeLineFields fields;
      // 2026-09-21追加、直接のユーザー指摘("モードライン不良...正しく
      // は「tcb-1.5b.linux.tgz」「(tar)」「/Users/haa15030/」"):
      // setPagerListingDirectory()自身の宣言コメント参照――ARCHIVE
      // 分岐はpushWithNameOverride(kind, entry->name, "(tar)"等)を
      // 併用するので、トップフレームがnameOverride付きならそちらを
      // nameに使う(nameOverride無しの他の全ての(stdin)ケースでは、
      // これまで通りcurrentFile_――常に空文字列)。plain push()の
      // フレーム(旧searchAnnotation_相当)はこの分岐では一切読まない
      // ――旧実装もここではsearchAnnotation_を参照していなかった。
      const auto *originFrame = annotations_.top();
      bool hasOrigin = originFrame && originFrame->nameOverride;
      fields.name = hasOrigin ? *originFrame->nameOverride : currentFile_;
      fields.trailing = std::to_string(bytes);
      fields.rightWatermark = hasReturnTarget ? "" : "usilic v1.5b";
      fields.trailing2 = hasOrigin ? originFrame->text : "";
      // 2026-09-17追加、直接のユーザー指摘("戻り値がPROGRAMなら
      // PROGRAMへ戻らないと。なんだけど、PROGRAMは^J^P(program一覧)
      // でも発動するので話がややこしくなる。そこで戻り値は
      // ~/git/usilic/でいいかなと"): 以前はpagerReturnsToProgram_の
      // 間、固定文字列"PROGRAM"を表示していたが、この語自体が
      // usilicrc設定一覧(^J^P、startProgramList())と紛らわしく、
      // しかも実際の^Gの戻り先は「PROGRAM画面」という独立した何かでは
      // なく、Application::tryPollProgramHandoff()自身のコメント通り
      // capturedModeが指す実際の場所(典型的にはディレクトリ)――
      // ARCHIVE再設計(①)/K-23(⑥)と同じ理屈で、ラベルではなく
      // pagerListingDirectory_(tryPollProgramHandoff()が
      // capturedMode==Directoryの時に設定する)をそのまま出す。
      fields.trailing3 = pagerReturnsToShell_ ? "SHELL" : pagerListingDirectory_;
      // 2026-08-25の修正(HISTORY.md:9462を引用した直接のユーザー
      // 報告、"基本的に右端まで埋まっていないモードラインは全部
      // 不良"): trailing3は「既に」fillTrailingToEdgeサポートを
      // 持っている(自身の宣言コメント参照、以前ArchiveMode向けに
      // 追加された)――ここでは単に一度も有効にされていなかった
      // ので、"SHELL"は他の全モード自身の最後のフィールドのように
      // パディングされる代わりに、行の右端の手前で止まっていた。
      // hasReturnTargetに特有にスコープされている(無条件ではない):
      // 独立起動のケースはtrailing3を全く持たず、代わりに
      // rightWatermark自身の別のmoveCursor()による端への配置に
      // 依存しており、それは既に自力で端に到達している――そこでも
      // これを有効にすると、この報告の対象では決してなく、望まれた
      // ものとして確認もされていない、バイト数と"usilic v1.5b"の
      // 間のプレーンなギャップを新たに反転表示で埋めてしまうことに
      // なる。pagerReturnsToDirectory_はtrailing3を持たない(上参照)
      // ので、これはpagerReturnsToShell_だけにスコープしたまま。
      // 2026-09-21追加: pagerListingDirectory_(ARCHIVE分岐の所属
      // ディレクトリ)が非空の場合も同様にtrailing3を端まで埋める
      // ――ArchiveMode自身のtrailing3(currentArchive_、フルパス)が
      // fillTrailingToEdgeなのと同じ理屈。
      fields.fillTrailingToEdge = pagerReturnsToShell_ || !pagerListingDirectory_.empty();
      // 2026-09-11追加、直接のユーザー報告("FILE/DIRでも^O^S/^J^S
      // 同様^S/^[iをサポートして欲しい"): pagerMode_(CLIから直接
      // `usilic <file>`/パイプされたstdinで開いたファイル、この関数
      // 自身の宣言コメント参照)は、下の「通常にloadFile()された
      // ファイル」の分岐にある同じgrepInPlaceActive()/
      // grepNoMatchActive()チェックに一度も到達できず、無条件に
      // ここでreturnしていたため、in-place GREP自体は実際には機能
      // していた(ハイライト・マッチ数は正しく設定される)にも
      // 関わらず、モードラインには決して反映されなかった――
      // ^O^S(FGREP)/^J^S(EGREP)は専用の一覧画面(ApplicationMode::
      // Grep)へ切り替わるため、この関数自体を経由せずこの欠落の
      // 影響を受けなかった。DirectoryMode/この関数下方の分岐と同じ
      // 判定をここにも複製する。
      if (view_.grepInPlaceActive()) {
        fields.name = view_.grepInPlaceLabel();
        fields.trailing2 = view_.grepInPlaceProgramBadge();
        fields.trailing3 = currentFile_;
        fields.fillTrailingToEdge = true;
      } else if (view_.grepNoMatchActive()) {
        fields.name = view_.grepInPlaceLabel();
        fields.trailing2 = view_.grepInPlaceProgramBadge();
        fields.trailing3 = currentFile_;
      }
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
    // annotations_のトップフレーム(旧searchOriginLabel_/Detail_+
    // searchAnnotation_、design.md「FileModeの統合」参照)は、
    // nameOverride付きなら常にname/trailing2フィールドを乗っ取り
    // (LOCATE/INFO等)、nameOverride無しのplain pushならtrailing2
    // だけを乗っ取る(GREP等、ファイル自身の名前"CLAUDE.md"等はそのまま)。
    // どちらの種類のフレームも無ければ、name=ファイル名、trailing2=
    // 含むディレクトリという素のフォールバックになる。以前は2つの
    // 独立フィールド+優先順位チェックで表現されていたが、スタックの
    // トップが常に「今表示すべき唯一の理由」を表すため、この単純化が
    // 成り立つ。
    const auto *originFrame = annotations_.top();
    std::string name = (originFrame && originFrame->nameOverride) ? *originFrame->nameOverride : path.filename().string();
    std::string trailing2 = ec ? "" : (originFrame ? originFrame->text : directory);
    // Fileモードのtrailingフィールドは、legacyの端まで埋める
    // スタイル、ModeLineFields::fillTrailingToEdge自身のコメント
    // 参照。ここにはrightWatermarkは無い――Fileモードが決してそれを
    // 表示しない理由は自身のコメント参照。trailing2Kind=PathMiddle
    // は、annotations_に何らかのフレームがある間――これが他の全
    // プロバイダの末尾のみの切り詰めに対する唯一の例外である理由は
    // ContentView::TrailingKind自身のコメント参照。フォールバック
    // (プレーンな含むディレクトリのPATH)は代わりにPathを使い、
    // 利用可能な幅全体を使う。
    // (2026-08-28の修正、直接のユーザー報告+例: "「- レガシーC実装...
    // src/config.sh」...使えるだけ使う": QueryEcho自身の固定16文字の
    // 末尾上限は、実際に利用可能な幅が必要とするよりもはるかにきつく、
    // 一致した行のテキストを切り詰めていた――"CLAUDE.md"のような
    // 短い名前は、このフィールドが実際に使うべき十分な余地を残す。
    // PathMiddleは予算を認識する(middleEllipsisTruncate()は、
    // withTruncatedFields()自身のパイプライン経由で、実際に
    // 「残っているもの」に合わせてサイズを決める)し、既に依頼された
    // まさにその"- レガシーC実装...src/config.sh"の形に合わせて
    // テキストの両端を保持している――そして下のsplitLayoutWhenCrowded
    // (このプロバイダに対して既に無条件で有効)は、name+trailing2が
    // 一緒になって本当に両方収まらない場合に、「反対側」で自動的に
    // 発動するものである。
    ContentView::ModeLineFields fields{name,
                                        trailing,
                                        /*fillTrailingToEdge=*/true,
                                        /*rightWatermark=*/"",
                                        trailing2,
                                        /*trailingPlain=*/false,
                                        /*trailing2RightJustified=*/false,
                                        /*trailing2Kind=*/originFrame ? ContentView::TrailingKind::PathMiddle
                                                                       : ContentView::TrailingKind::Path};
    // splitLayoutWhenCrowded(2026-08-27の追加、直接のユーザー報告/
    // 自身の以前のHISTORY.md:9608提案への呼び戻し。archiveContentView_
    // 自身のプロバイダが既に採用している――application.cppの自身の
    // コメント参照): 実際の、バイト数フィールドを押し出すほど長い
    // CJKファイル名は、以前はfitModeLineFields()自身の通常のall-or-
    // nothing階層を介してそれを丸ごと黙って落としていた。
    // DirectoryMode自身の`(ls)`一覧がまさにこの同一の修正を受けた
    // のと同じ種類のバグ。
    fields.splitLayoutWhenCrowded = true;
    // DirectoryMode::setGrepInPlaceOverlay()/reportGrepNoMatchMessage()
    // 自身のコメント参照(2026-09-09追加、2026-09-10改訂): pagerMode_
    // のファイルについては上で既にreturn済みのため、ここに到達するのは
    // 通常にloadFile()されたファイルだけ。trailing3にcurrentFile_
    // (このファイル自身のフルパス、通常はname/trailing2が示していた
    // 情報)を退避する――直接のユーザー指摘=fv参照。
    if (view_.grepInPlaceActive()) {
      fields.name = view_.grepInPlaceLabel();
      fields.trailing2 = view_.grepInPlaceProgramBadge();
      fields.trailing3 = currentFile_;
      fields.fillTrailingToEdge = true;
    } else if (view_.grepNoMatchActive()) {
      // DirectoryMode自身の同じ分岐のコメント参照(2026-09-10再修正、
      // 直接のユーザー指摘: "No StringもGREPでは無く行数の隣に表示")。
      fields.name = view_.grepInPlaceLabel();
      fields.trailing2 = view_.grepInPlaceProgramBadge();
      fields.trailing3 = currentFile_;
    }
    return fields;
  });
}

// 2026-09-15追加、直接のユーザー指摘("^[^Mは「as is」（msマクロ）に
// なってない"): 以前はここ(旧readAndWrap()の先頭)がrawControlCodesMode_
// を一切見ておらず、loadFile()/loadFileNoWrap()/loadFileRaw()いずれも
// 同じ変換済み内容(HELP-markup展開、またはroff/msマクロ文書のgroff
// 整形)を表示していた。old/src/code.cのcheck_type()を辿ると、legacy
// のM_code(^[^M、fp->cr=0)はcheck_ext()自体を呼ばない――つまりHELP-
// markup展開/groff整形どちらの変換ステップにも一切進まない、という
// のが"そのまま(as is)読む"の実体だった。rawControlCodesMode_が真の
// 間はこの変換を丸ごとスキップし、生バイトをそのままcontentとして
// 返すようにした。
// 2026-09-25追加、直接のユーザー依頼(usilicrcのCODEが読み込まれる
// だけで使われていなかった件、"実装して"): legacy code.cのcheck_type()/
// init_code()/type_file()の移植。usilicrcのCODEが空(code0が0、または
// CODEセクション自体が無い)ならこの判定はしない。そうでなければ、
// 先頭N_buff(0x10000)バイトをCharset::detect()へCODEの許可リスト付きで
// 渡し(UTF-8端末で正しいUTF-8ならテキスト扱い、が他より優先)、バイナリ
// と判定されたら、内容をlegacyと同じ「(^X) <file(1)の結果>」の太字1行
// (^Xは最初に見つかった許可されていない制御文字)へ差し替え、ERROR
// ログにも"file: <パス>: <種類>."を記録する。legacyのK_h(\b)まわりの
// 追加判定(\bの直後がNUL等)は移植していない。
// binaryTypeText()の判定部分だけ(file(1)の実行やERRORログへの記録という
// 副作用が無い)。loadFileAsPipe()の「整形が要らなければ元のファイルから直接
// 流す」判定でも使う(2026-09-25)。
bool FileMode::looksBinary(const std::string &content) const {
  const std::string &allowed = configuration_.allowedControlBytes();
  if (allowed.empty() || content.empty()) return false;
  int window = static_cast<int>(std::min<std::size_t>(content.size(), 0x10000));
  const auto *bytes = reinterpret_cast<const unsigned char *>(content.data());
  return Charset::detect(bytes, window, encoding_, allowed.c_str()) == Charset::Encoding::Binary;
}

std::optional<std::string> FileMode::binaryTypeText(const std::string &path, const std::string &content) const {
  if (!looksBinary(content)) return std::nullopt;
  const std::string &allowed = configuration_.allowedControlBytes();
  int window = static_cast<int>(std::min<std::size_t>(content.size(), 0x10000));
  const auto *bytes = reinterpret_cast<const unsigned char *>(content.data());
  unsigned char offending = 0;
  for (int i = 0; i < window; i++) {
    unsigned char c = bytes[i];
    if (c == 0 || (c < 0x20 && c != '\n' && allowed.find(static_cast<char>(c)) == std::string::npos)) {
      offending = c;
      break;
    }
  }
  std::string type;
  ChildProcess::Result result = ChildProcess::run({"file", "-b", path});
  if (result.ok) {
    type = result.output.substr(0, result.output.find('\n'));
  } else {
    type = "cannot execute `file'";
  }
  std::string plain = std::string("(^") + static_cast<char>('@' + offending) + ") " + type;
  if (errorLogPath_) {
    std::ofstream log(*errorLogPath_, std::ios::app);
    log << "file: " << path << ": " << plain << ".\n";
  }
  // PipeBufferの重ね打ち検出(X\bX→太字)でlegacyのtobo()の太字を表す。
  std::string bold;
  for (std::size_t i = 0; i < plain.size();) {
    int len = std::max(1, Utf8::sequence_length(static_cast<unsigned char>(plain[i])));
    std::string glyph = plain.substr(i, len);
    bold += glyph + "\b" + glyph;
    i += len;
  }
  return bold + "\n";
}

std::string FileMode::loadAndTransformContent(const std::string &path) const {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  std::ostringstream buffer;
  buffer << in.rdbuf();
  if (!in.good() && !in.eof()) return {};
  std::string content = buffer.str();
  if (rawControlCodesMode_) return content;
  if (auto typeText = binaryTypeText(path, content)) return *typeText;
  // isHelpMarkupPagerFile()自身のコメント参照(HELP.txt/QUICK.txtの
  // <indentN>/<i>/<b>/<u>markupを、リテラルなタグのままではなく
  // ^Jh自身のkHelpTextJa/kHelpTextGuiJaと同じ規約で展開する)。
  if (isHelpMarkupPagerFile(path)) {
    content = expandHelpMarkup(content);
  } else if (isMarkdownFile(path)) {
    // markdownToHelpMarkup()自身のコメント参照: Markdownを<b>/<u>/<i>/<indentN>タグへ
    // 変換してから、既存のexpandHelpMarkup()でHELP.txt/QUICK.txtと同じ制御バイトへ
    // 展開する(二段構成)。
    content = expandHelpMarkup(markdownToHelpMarkup(content));
  } else if (looksLikeRoffDocument(content)) {
    // 2026-09-20追加、直接のユーザー依頼("usilic -m src/QUICK.txt > .../
    // usilic.man...HELPは同ファイルを「C-j b」でCUTする")、HELP.jp
    // 自身の文書化された挙動("特定のsuffixを持たなくてもマニュアル
    // ページなどのmsマクロ形式で書かれている通常ファイルはgroff(1)を
    // 起動して整形します")の初移植。ConfiguredRole::Groffは
    // Configuration側に以前から定義済みだったが、この一切れまで
    // どこからも一度も呼ばれていなかった。groff自体が使えない/失敗
    // した場合は、legacy自身のfork_bin()が失敗時に生バイトへ
    // フォールバックするのに倣い、contentを未加工のまま表示する
    // (この通常ファイル閲覧という文脈で、失敗時に画面を完全に
    // 諦めさせるほどの理由は無い)。
    ProgramCommand groff = configuration_.commandFor(ConfiguredRole::Groff);
    if (!groff.argv.empty()) {
      ChildProcessOptions options;
      options.input = &content;
      ChildProcess::Result result = ChildProcess::run(groff.argv, options);
      if (result.ok) content = result.output;
    }
  }
  return content;
}

std::vector<ContentView::Line> FileMode::readAndWrap(const std::string &path,
                                                       const std::function<bool()> &shouldAbort) const {
  std::string content = loadAndTransformContent(path);

  // 2026-09-14追加、直接のユーザー依頼("（ls）での^O^M/^[^Mを実装
  // して"): loadFileNoWrap()自身のコメント参照――行の折り返しを行わ
  // ない代わりに、PipeBufferへ実質無限の幅を渡す。長い行はContentView::
  // renderLine()自身の既存のmaxCol打ち切り(そちら自身のコメント参照、
  // 元々ELLIPSISロジックのために存在する)にそのまま任せる――水平
  // スクロール自体は実装しない(less -Sと同じ「画面幅で切り詰める」
  // 挙動、水平スクロールバー等の新規UIは無い)。
  int effectiveWidth = noWrapMode_ ? kNoWrapWidth : width_;
  PipeBuffer pipe(encoding_, effectiveWidth, 64L * 1024 * 1024, /*detectOverstrike=*/!rawControlCodesMode_);
  // 2026-09-25追加、直接のユーザー質問("^Mは中断できないの？"): 時間が
  // かかるのは読み込みそのものより、この画面用の行への組み立て(折り返し/
  // 装飾の解釈)なので、1MB程度ずつ(行の区切りで)渡し、その合間に
  // shouldAbort()(Application::keyInterruptRequested()、どれかのキー)を
  // 見る。中断したらそこまでの行だけを表示する。
  constexpr std::size_t kChunkBytes = 1024 * 1024;
  std::size_t pos = 0;
  while (pos < content.size()) {
    std::size_t end = std::min(content.size(), pos + kChunkBytes);
    if (end < content.size()) {
      std::size_t newline = content.find('\n', end);
      end = (newline == std::string::npos) ? content.size() : newline + 1;
    }
    pipe.feed(content.substr(pos, end - pos));
    pos = end;
    if (pos < content.size() && shouldAbort && shouldAbort()) break;
  }
  pipe.flush();

  std::vector<ContentView::Line> lines;
  lines.reserve(pipe.lines().size());
  // styled.attributes(2026-08-05): PipeBuffer自身のオーバーストライク
  // 検出(nroffスタイルの太字/下線)は既にこれを生成している――以前は
  // ここで捨てられていた。FileModeがPipeBufferのStyledLinesを実際に
  // 画面へ届くものへ変換する、唯一の場所。
  for (const auto &styled : pipe.lines()) {
    ContentView::Line line{styled.text, std::nullopt, std::nullopt, styled.attributes};
    // StyledLine::rawLineNumber自身のコメント参照(2026-09-08追加、
    // 直接のユーザー報告"usilic画面では98行だった行が97として表示
    // されている")――上の位置指定初期化では素通りしてしまう
    // (ellipsisEligible/neverHighlightedの後にある)ので、個別に代入。
    line.rawLineNumber = styled.rawLineNumber;
    lines.push_back(std::move(line));
  }
  return lines;
}

// loadFileRaw()自身の宣言コメント参照。readAndWrap()と違い、ファイル
// 全体を一度にloadAndTransformContent()で読んでからPipeBuffer::feed()
// する代わりに、kChunkBytes単位で読み進めながら都度feed()し、各
// チャンクの後にshouldAbort()を呼ぶ。呼び出し元は常にrawControlCodesMode_
// が真の場合だけこれを使う(loadFileImpl()自身の分岐参照)ので、
// loadAndTransformContent()自身が行うHELP-markup展開/roff整形は
// そもそも関係が無く、ここでは複製していない――生バイトをそのまま
// PipeBufferへ渡す。
std::vector<ContentView::Line> FileMode::readAndWrapChunked(const std::string &path,
                                                              const std::function<bool()> &shouldAbort) const {
  std::vector<ContentView::Line> lines;
  std::ifstream in(path, std::ios::binary);
  if (!in) return lines;

  int effectiveWidth = noWrapMode_ ? kNoWrapWidth : width_;
  PipeBuffer pipe(encoding_, effectiveWidth, 64L * 1024 * 1024, /*detectOverstrike=*/false);
  // 2026-09-16、直接のユーザー提案("使用頻度は確かに低いので...速度
  // は度外視でいいのではないか"): 8MBという大きさ自体に技術的な根拠は
  // 無く、「^Gの反応までの遅延(チャンクの実処理時間+shouldAbort()自身の
  // 最大0.1秒のポーリング待ち)」と「チャンク数を減らして後者の合計を
  // 抑える」という2つの相反する要求の、深く検証していない妥協点。
  constexpr std::size_t kChunkBytes = 8 * 1024 * 1024;
  std::string chunk(kChunkBytes, '\0');
  while (in) {
    in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    std::streamsize n = in.gcount();
    if (n <= 0) break;
    pipe.feed(chunk.substr(0, static_cast<std::size_t>(n)));
    if (shouldAbort()) break;
  }
  pipe.flush();

  lines.reserve(pipe.lines().size());
  for (const auto &styled : pipe.lines()) {
    ContentView::Line line{styled.text, std::nullopt, std::nullopt, styled.attributes};
    line.rawLineNumber = styled.rawLineNumber;
    lines.push_back(std::move(line));
  }
  return lines;
}

bool FileMode::loadFile(const std::string &path, const std::function<bool()> &shouldAbort) {
  noWrapMode_ = false;
  rawControlCodesMode_ = false;
  return loadFileImpl(path, shouldAbort);
}

bool FileMode::loadFileNoWrap(const std::string &path) {
  noWrapMode_ = true;
  rawControlCodesMode_ = false;
  return loadFileImpl(path);
}

bool FileMode::loadFileRaw(const std::string &path, const std::function<bool()> &shouldAbort) {
  noWrapMode_ = false;
  rawControlCodesMode_ = true;
  return loadFileImpl(path, shouldAbort);
}

// loadFileAsPipe()自身の宣言コメント(file_mode.hpp)参照。
bool FileMode::loadFileAsPipe(const std::string &path) {
  std::ifstream probe(path, std::ios::binary);
  if (!probe) return false;
  // 2026-09-25追加(BUFFERを超えるファイルを^Mから^O^Mへ切り替えるようにした
  // のに伴い、直接のユーザー依頼): 以前は常にloadAndTransformContent()で
  // ファイル全体をメモリへ読み、一時ファイルへ書き直してから流していた
  // (数十〜数百MBのファイルではそれ自体が重い)。整形(HELP用マークアップ/
  // Markdown/roff/バイナリの種類表示)が要らないファイルは、元のファイルを
  // そのまま流す――先頭の64KBだけで判定できる(looksLikeRoffDocument()は
  // 先頭4KB、バイナリ判定は先頭64KBしか見ない)。
  std::string head(0x10000, '\0');
  probe.read(head.data(), static_cast<std::streamsize>(head.size()));
  head.resize(static_cast<std::size_t>(probe.gcount()));
  probe.close();

  rawControlCodesMode_ = false;
  if (!isHelpMarkupPagerFile(path) && !isMarkdownFile(path) && !looksLikeRoffDocument(head) && !looksBinary(head)) {
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd >= 0) {
      loadStream(fd, /*ownsFd=*/true, "", 1, /*noWrap=*/false);
      annotations_.pushWithNameOverride(FileAnnotationKind::Search, "", "(stdin)");
      setPagerReturnsToDirectory(true);
      return true;
    }
  }
  std::string content = loadAndTransformContent(path);
  return loadContentAsPipe(content);
}

// loadContentAsPipe()自身の宣言コメント(file_mode.hpp)参照――
// loadFileAsPipe()自身のmkstemp()以降と元々全く同じ内容だったものを
// 2026-09-21に抽出した(そちら自身は上でloadAndTransformContent()の
// 結果を渡すだけの薄いラッパーになった)。
bool FileMode::loadContentAsPipe(const std::string &content) {
  char tempPath[] = "/tmp/usilic-pipe-XXXXXX";
  int fd = ::mkstemp(tempPath);
  if (fd < 0) return false;
  ::unlink(tempPath);  // POSIXでは開いたままのfdは生き続ける――ディスク上に何も残さない
  std::size_t written = 0;
  while (written < content.size()) {
    ssize_t n = ::write(fd, content.data() + written, content.size() - written);
    if (n < 0) {
      if (errno == EINTR) continue;
      ::close(fd);
      return false;
    }
    written += static_cast<std::size_t>(n);
  }
  if (::lseek(fd, 0, SEEK_SET) < 0) {
    ::close(fd);
    return false;
  }

  // 2026-09-21、直接のユーザー判断(旧版との比較、実機スクリーンショット): 旧版の^O^M(M_fp、
  // fp->cr=2)は、m_pipe()経由でファイルをパイプ越しに読ませるだけで、行の折り返しは止めない
  // (81桁の行は"…</date"と">"の2行に折り返して全文字が見える)。2026-09-14には「画面幅で切り詰める
  // (less -S相当)」としていたが、行末の文字が欠けるため旧版に合わせて折り返す。
  loadStream(fd, /*ownsFd=*/true, "", 1, /*noWrap=*/false);
  // "(stdin)"――loadStream()自身のクラスコメント/実際のSTDINパイプ
  // 起動が使うのと全く同じラベル付け。displayName/labelを空のまま
  // にするのは、その本物のケースと同じくcurrentFile_を空のままにし
  // (History::pushRecent()も発火しない)、カーソル位置もHistory経由
  // では保存/復元しない――"常時真っ新から始まる"というloadStream()
  // 自身のコメントが述べる原則に、このパイプ越しの疑似セッションも
  // 素直に従う(実ファイルの閲覧履歴・カーソル位置は、K-22の通常の
  // loadFile()セッション側だけが引き続き担う)。
  annotations_.pushWithNameOverride(FileAnnotationKind::Search, "", "(stdin)");
  // 2026-09-15追加、直接のユーザー指摘("^O^MはSTDINなページャー仕様
  // へ完全移行していて^Gで（ls）へ戻れない"): setPagerReturnsToDirectory()
  // 自身の宣言コメント参照――本物のSTDINパイプ起動と違い、K-23は
  // Directory一覧からの選択という正真正銘の「前のモード」を持つ。
  setPagerReturnsToDirectory(true);
  return true;
}

bool FileMode::loadFileImpl(const std::string &path, const std::function<bool()> &shouldAbort) {
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
  pagerReturnsToDirectory_ = false;  // 同上
  pagerReturnsToProgram_ = false;    // 同上
  view_.setUseFixedWidthPosition(false);  // loadStream()自身のコメント参照。他の全てのFileモードのエントリはプレーンなまま
  view_.setContentMayGrow(false);  // setContentMayGrow()自身の宣言コメント参照。この非ストリーミング読み込みは常に一括で確定済み

  // 2026-09-16追加、loadFileRaw()自身の宣言コメント参照。
  view_.setLines(rawControlCodesMode_ && shouldAbort ? readAndWrapChunked(path, shouldAbort) : readAndWrap(path, shouldAbort));
  currentFile_ = path;
  // 新しい読み込みは古い検索を無効にする――AnnotationStack自身のコメント参照
  annotations_.clear();
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
    // ContentView::Line::rawLineNumber自身のコメント参照(2026-09-08
    // 追加、直接のユーザー報告"-gのときだけは97ではなくて103の位置に
    // ブロックカーソルがある...この症状には手を付けてないってこと？"
    // ――幅が変わって再折り返しされると、折り返しの入り方自体が変わり
    // 得るため、以前と「同じ表示行インデックス」がもはや同じ生の行を
    // 指すとは限らない。折り返し前に、カーソルが実際にいた生の行番号を
    // 覚えておき、再折り返し後にその番号へ翻訳し直す――
    // Application::resolveGotoLineRow()と同じ理屈だが、reflow()自身は
    // 再読み込みを既に済ませた後の同期的なコードなので、pager先読みの
    // リトライは不要な単純な線形探索で足りる)。
    int oldCursor = view_.cursorIndex();
    std::optional<int> oldRawLine = rawLineNumberAt(oldCursor);
    if (streamFd_ >= 0) readMoreIntoPipe(std::max(oldLineCount, 1));
    view_.setLines(convertPipeLines());
    view_.setCursorIndex(translateCursorAfterReflow(oldCursor, oldRawLine));
    view_.redraw();
    view_.syncVisibleCursor();
    // 2026-09-20追加、"-gで今度は103ではなく100に来る"追跡用の一時
    // 診断ログ(既存のUSILICGUIDEBUG慣習に合わせる、graphical_display.cpp
    // 各所参照)――-gでは実機再現しか手段が無いため、次回の再現時に
    // これを見ればreflow()自身が何回・どの幅で呼ばれ、生行番号の翻訳が
    // 何を返したかが分かる。
    if (const char *dbg = std::getenv("USILICGUIDEBUG")) {
      FILE *f = std::fopen(dbg, "a");
      if (f) {
        std::fprintf(f, "FileMode::reflow(pager) newWidth=%d oldCursor=%d oldRawLine=%d newCursor=%d newRawLine=%d\n",
                     newWidth, oldCursor, oldRawLine.value_or(-1), view_.cursorIndex(),
                     rawLineNumberAt(view_.cursorIndex()).value_or(-1));
        std::fclose(f);
      }
    }
    return true;
  }

  if (currentFile_.empty()) return false;
  width_ = newWidth;
  int oldCursor = view_.cursorIndex();
  std::optional<int> oldRawLine = rawLineNumberAt(oldCursor);

  view_.setLines(readAndWrap(currentFile_));
  view_.setCursorIndex(translateCursorAfterReflow(oldCursor, oldRawLine));
  view_.redraw();
  view_.syncVisibleCursor();
  if (const char *dbg = std::getenv("USILICGUIDEBUG")) {
    FILE *f = std::fopen(dbg, "a");
    if (f) {
      std::fprintf(f, "FileMode::reflow(plain) newWidth=%d oldCursor=%d oldRawLine=%d newCursor=%d newRawLine=%d\n",
                   newWidth, oldCursor, oldRawLine.value_or(-1), view_.cursorIndex(),
                   rawLineNumberAt(view_.cursorIndex()).value_or(-1));
      std::fclose(f);
    }
  }
  return true;
}

// reflow()自身の同日コメント参照。`beforeCursor`は再折り返し「前」の
// 表示行インデックス、返り値はそこにあった生の行(オプション、無ければ
// beforeCursor自身をそのままクランプするだけの、以前と同じ挙動)を
// 再折り返し「後」の内容の中で探し直した、対応する表示行インデックス。
std::optional<int> FileMode::rawLineNumberAt(int index) const {
  const auto &lines = view_.lines();
  if (index < 0 || index >= static_cast<int>(lines.size())) return std::nullopt;
  return lines[index].rawLineNumber;
}

int FileMode::translateCursorAfterReflow(int beforeCursor, std::optional<int> beforeRawLine) const {
  const auto &lines = view_.lines();
  int last = static_cast<int>(lines.size()) - 1;
  if (last < 0) return 0;
  if (beforeRawLine) {
    for (int i = 0; i <= last; i++) {
      if (lines[i].rawLineNumber && *lines[i].rawLineNumber >= *beforeRawLine) return i;
    }
  }
  return std::clamp(beforeCursor, 0, last);
}

const std::string &FileMode::currentFile() const { return currentFile_; }

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
  pendingReadAhead_.clear();
}

bool FileMode::readMoreIntoPipe(int targetLineCount) {
  if (streamAtEof_ || streamFd_ < 0) return false;
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
  // 2026-09-06追加、直接のユーザー報告("総行数が１行単位で進まない
  // ...旧同様１バイトずつ読み込んで改行が来たら１増やす")――上の
  // 256バイトという固定値自身、それより前のコメントの通り「超過を
  // 画面1枚分程度に抑える」ための妥協であり、超過そのものを無くしては
  // いなかった――ensureLoadedThrough()自身の2026-09-06追加分のコメント
  // 参照(SHELLの生きたPAGERハンドオフに限り「あと1行」だけを要求する
  // ペーシング)が実際に効くためには、その「あと1行」の要求1回につき
  // 高々1行分しか読み進めてはならない――256バイト読めば大抵それだけで
  // 数十行分をまとめて掴んでしまい、ペーシングそのものが無意味になる。
  // 呼び出し元が要求している差分(targetLineCount-現在の行数)が1以下
  // (=「あと1行だけ」というensureLoadedThrough()自身のペーシング済み
  // 要求)の間だけ1バイト単位に切り替え、legacy自身のlinein()
  // (old/src/alloc.c)と同じ粒度で改行1つ分ずつ進む――それより大きな
  // 差分は256バイトずつ効率よく読み進めるが、2026-09-06追加、直接の
  // ユーザー報告("80x24ターミナルだと開いた直後「00001/00024」の
  // はずだが「00001/00050」になっている")を経て、このチャンクサイズ
  // はループの「毎回」、残り差分に合わせて計算し直す(以前はループの
  // 外で1回だけ、最初の差分を基に決めていた)――固定256バイトのまま
  // だと、要求が「画面1枚分(例えば24行)」程度の中規模な場合でも、
  // 残りわずか数行の所で256バイト分をまとめて読んでしまい、行の長さ
  // 次第で数十行分も超過することがあった(実機PTYで確認: man bashの
  // 最初の1ページ読み込みで24行を要求したのに52行読み込まれていた)。
  // 残り差分に応じて縮めることで、目標に近づくほど読み込み単位も
  // 小さくなり、最終的な超過を高々1行程度に抑える――GoToBottom自身の
  // sentinel値のような巨大な差分に対しては引き続き256バイトの一括
  // 読み込みのまま、効率を保つ。
  // 2026-09-06追加、直接のユーザー報告("特に-g起動ではENDページになる
  // までガタガタなスクロールで滑らかさがない...１行単位の読み込みに
  // 出来ない？"): 上の「あと1行の間は1バイト単位」という精度は
  // pendingReadAhead_自身の宣言コメントの通り引き続き必要だが、それを
  // 1バイトごとのread()システムコールの連打で実現する必要は無い――
  // 1回のread()でまとめて読み、その中の「最初の改行の直後」までだけを
  // streamPipe_へ供給し、それを超えて読めてしまった分は
  // pendingReadAhead_へ一時的に留め置いて次回の呼び出しで真っ先に消費
  // する。これにより、想定される典型的な行の長さの範囲では、1行につき
  // 高々1回のread()で済むようになる(以前は行の長さと同じ回数の1バイト
  // read()が必要だった)。
  char buf[256];
  while (static_cast<int>(streamPipe_->lines().size()) < targetLineCount) {
    if (!pendingReadAhead_.empty()) {
      std::size_t nl = pendingReadAhead_.find('\n');
      if (nl == std::string::npos) {
        streamPipe_->feed(pendingReadAhead_);
        pendingReadAhead_.clear();
        continue;  // まだ改行を含んでいなかった――行数はまだ増えていないので読み続ける
      }
      streamPipe_->feed(pendingReadAhead_.substr(0, nl + 1));
      pendingReadAhead_.erase(0, nl + 1);
      continue;  // ループ条件(streamPipe_->lines().size())を再評価する
    }
    int remaining = targetLineCount - static_cast<int>(streamPipe_->lines().size());
    // 2026-09-06再訂正、直接のユーザー報告("さっきのちらつきの件、
    // １バイト読みオーバーヘッドが原因じゃなかったんで試しに１バイト
    // 読みに戻してみてほしい")――上のコメント(「あと1行の間もチャンク
    // サイズをsizeof(buf)へ広げてよい」という判断)が誤りだったと
    // ユーザー自身の実機検証で判明した。原因の切り分けのため、実験的に
    // 元の1バイト単位へ戻す――pendingReadAhead_自身の仕組み(すぐ上の
    // ループ先頭)は無害なまま残しておく(chunkSize=1では改行を跨いで
    // 読み過ぎることが無いので、単に一度もデータを溜めないだけになる)。
    std::size_t chunkSize = (remaining <= 1) ? 1 : std::min<std::size_t>(256, static_cast<std::size_t>(remaining) * 2);
    ssize_t n = ::read(streamFd_, buf, chunkSize);
    if (n < 0) {
      if (errno == EINTR) continue;
      return true;  // EAGAIN/EWOULDBLOCK(一時的に何も無い)も含め、ここで抜ける――すぐ上のコメント参照。まさに求めていた「一瞬の停止」の信号
    }
    if (n == 0) {
      streamAtEof_ = true;
      view_.setContentMayGrow(false);  // setContentMayGrow()自身の宣言コメント参照。ここが本物のEOFの唯一の確定箇所
      streamPipe_->flush();  // まだ保留中の末尾の部分行を確定する
      if (streamOwnsFd_) ::close(streamFd_);
      streamFd_ = -1;
      return false;  // 本物のEOF――「一瞬の停止」ではない
    }
    streamBytesRead_ += static_cast<std::size_t>(n);  // 自身の宣言コメント参照: 確定した「実際に読み込まれたバイト数」の仕様(streamFd_から引き出せた量、pendingReadAhead_への一時的な留め置きの有無に関わらず)
    if (remaining <= 1) {
      // ペーシングが効いている間だけ、最初の改行の直後までしか
      // streamPipe_へ渡さない――それを超える分はpendingReadAhead_へ。
      std::string chunk(buf, static_cast<std::size_t>(n));
      std::size_t nl = chunk.find('\n');
      if (nl == std::string::npos) {
        streamPipe_->feed(chunk);
      } else {
        streamPipe_->feed(chunk.substr(0, nl + 1));
        pendingReadAhead_ = chunk.substr(nl + 1);
      }
    } else {
      streamPipe_->feed(std::string(buf, static_cast<std::size_t>(n)));
    }
  }
  return false;  // ループ条件を満たした――頼んだ行数に到達済み、停止ではない
}

std::vector<ContentView::Line> FileMode::convertPipeLines() const {
  std::vector<ContentView::Line> lines;
  lines.reserve(streamPipe_->lines().size());
  for (const auto &styled : streamPipe_->lines()) {
    // convertPipeLines()(上、非ストリーミング経路)自身のコメント参照
    // (2026-09-08追加、StyledLine::rawLineNumber)。
    ContentView::Line line{styled.text, std::nullopt, std::nullopt, styled.attributes};
    line.rawLineNumber = styled.rawLineNumber;
    lines.push_back(std::move(line));
  }
  return lines;
}

bool FileMode::loadStream(int fd, bool ownsFd, const std::string &displayName, int initialLineBudget,
                           bool noWrap) {
  closeStream();
  view_.setCursorHighlightEnabled(false);
  noWrapMode_ = noWrap;  // 自身の宣言コメント参照(file_mode.hpp) -- 前回のセッションからの取り残しを防ぐ明示的な上書き
  pagerMode_ = true;
  pagerReturnsToShell_ = false;  // 自身の宣言コメント参照。pollShellHandoff()はこの呼び出しの後にこれを切り替える
  pagerReturnsToDirectory_ = false;  // 同上。loadFileAsPipe()がこの呼び出しの後にこれを切り替える
  pagerReturnsToProgram_ = false;    // 同上。tryPollProgramHandoff()はこの呼び出しの後にこれを切り替える
  pagerListingDirectory_.clear();  // 同上。Application::openSelectedEntryNoWrap()のARCHIVE分岐/tryPollProgramHandoff()がこの呼び出しの後にこれを設定する(2026-09-17追加分)
  // 2026-08-25(直接のユーザー依頼: "SHELL同様、行数を5桁とバイト
  // 数表示をよろしく")――他の全てのFileモードのエントリが使う
  // 可変幅のデフォルトの代わりに、同じ固定5桁ゼロパディングの
  // "NNNNN/NNNNN END"位置フォーマットのために、Shell自身の同一の
  // 設定(Application::startShell()自身のsetUseFixedWidthPosition
  // (true)呼び出し)を再利用する。
  view_.setUseFixedWidthPosition(true);
  streamFd_ = fd;
  streamOwnsFd_ = ownsFd;
  streamAtEof_ = false;  // pagerExitEncountered()自身の宣言コメント参照、これ自身が既にsticky
  view_.setContentMayGrow(true);  // setContentMayGrow()自身の宣言コメント参照。本物のEOFに達するまでtrueのまま
  lastLoadAttemptYieldedNothing_ = false;
  // 2026-09-20追加、readMoreIntoPipe()自身のO_NONBLOCK化のコメント
  // 参照(直接のユーザー報告+legacy実機での再現確認)――失敗しても
  // 無視して構わない(通常のブロッキングのfdのまま続行するだけ、
  // 生きたSHELLからの引き渡しの生きたパイプ/ソケット以外――通常の
  // ファイル/リダイレクトされたstdin等――ではどのみち意味の無い
  // フラグ)。
  ::fcntl(streamFd_, F_SETFL, ::fcntl(streamFd_, F_GETFL, 0) | O_NONBLOCK);
  streamBytesRead_ = 0;  // 自身の宣言コメント参照――新しいストリームはこれを0から再開する
  // 2026-09-15追加、loadFileAsPipe()自身のコメント参照: noWrapMode_は
  // 本来loadFileImpl()/readAndWrap()専用だったが、loadFileAsPipe()が
  // 「先に」これをセットしてからloadStream()を呼ぶ経路を追加した
  // ため、ここもreadAndWrap()自身のkNoWrapWidthと同じ幅を使うよう
  // 揃える必要がある――そうしないとloadFileAsPipe()の折り返し方針
  // (loadFileNoWrap()と同じ「長い行を折り返さない」)がこの構築時点
  // で無視され、通常幅で折り返されてしまう。実際のSTDINパイプ/SHELL
  // 引き渡しの既存呼び出し元はnoWrapMode_を一切セットしない
  // (常にfalseのまま)ので、この変更はそれらの既存の挙動を変えない。
  streamPipe_ = std::make_unique<PipeBuffer>(encoding_, noWrapMode_ ? kNoWrapWidth : width_, 256L * 1024 * 1024);
  currentFile_ = displayName;
  annotations_.clear();
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

bool FileMode::ensureLoadedThrough(int targetLineIndex, bool bypassPacing) {
  // lastLoadAttemptYieldedNothing()自身の宣言コメント参照――「今回は
  // 何も試みなかった」場合との区別のため、実際に試みるまではfalseの
  // ままにしておく。
  lastLoadAttemptYieldedNothing_ = false;
  if (streamAtEof_ || streamFd_ < 0) return false;
  int before = static_cast<int>(streamPipe_->lines().size());
  if (targetLineIndex < before) return false;  // 既に十分読み込まれている(試みてすらいない)
  // 2026-09-06再訂正(3回目)、直接のユーザー報告(「80x24のターミナル
  // で旧との違いを比較すると」で始まる、実機PTYでの旧との詳細な
  // 逐一比較): 前回のここの修正は「画面に今見えている範囲がまだ
  // 埋まっていない間"だけ"あと1行に絞り、埋め終えたら元通り一括で
  // 読み込む」という設計だったが、これはlegacy自身の実機を新たに
  // 見比べた結果、根本的に誤りだったと判明した――legacy自身は
  // 「最初の1画面を埋め終えた後」も、通常のスクロール(`j`等)の
  // たびに先読み分がわずか1〜2行しか伸びない(直接のユーザー報告
  // 「②`j`により１行進むと「00024/00026」（先読み分は２行だけ）」
  // 「③`j`連打によりカレント行/総行は１行ずつ進行」)――「最初の
  // 画面さえ埋まればあとは２画面分まとめて読んでよい」という前回の
  // 前提そのものが、実機のこの逐次比較で否定された。
  // 2026-09-06さらに訂正(6回目、直接のユーザー報告
  // "`man ls|usilic -c`→「00001/00050」"「SHELLプロンプトで
  // `man ls^M`後の最初の画面が瞬時に出ない」を受け、legacy自身の
  // 実機`U`で`man ls`を改めて逐一比較): 上の「常にあと1行」への
  // 一般化自体が誤りだった――legacyの実機は、SHELL経由の生きた
  // PAGERハンドオフ((stdin))でも、まだ画面が一度も埋まっていない
  // 「最初の1画面」は(既にデータが届いていれば)0.3秒足らずでほぼ
  // 瞬時に埋め切り(`man ls\r`直後の反復サンプリングで確認)、その後の
  // 通常のスクロール(`j`連打)でだけ先読み分がちょうど1行ずつしか
  // 伸びない――「最初の画面もあと1行ずつ」ではなく「最初の画面だけは
  // 別枠、以後は1行ずつ」という2段構えが正しい実態だった。加えて、
  // このペーシング自体、SHELL経由の生きたハンドオフ(pagerReturnsToShell_)
  // に限らず、`usilic -c`への直接パイプ/`$PAGER=usilic -c`経由の
  // 起動でも同じくlegacyは「現在+1」を守る(`man ls|usilic -c`の
  // `j`一回で「00024/00050」ではなく「00024/00025」を期待する直接の
  // ユーザー報告)ため、pagerReturnsToShell_に絞らず全てのストリームへ
  // 適用するよう一般化した。呼び出し元(pollPagerExitNotice()自身の
  // コメント参照)が、今回の要求が「まだ最初の1画面を埋めている最中」
  // だと分かっている時にだけbypassPacing=trueを渡し、このペーシングを
  // 一時的に迂回する。
  // Command::GoToBottom自身の巨大なsentinel値(INT_MAX-1、呼び出し元
  // のコメント参照: `less`のGコマンド同様、本当のEOFまでブロックして
  // 読み切る意図)も同じくこのペーシングの対象外とする――そちらは
  // 「今すぐ全部」が意図そのものであり、1行ずつ間延びさせては壊れる。
  bool isDrainToRealEofRequest = targetLineIndex >= std::numeric_limits<int>::max() - 1;
  bool applyPacing = !isDrainToRealEofRequest && !bypassPacing;
  int effectiveTarget = applyPacing ? before : targetLineIndex;
  // readMoreIntoPipe()自身のコメント参照: 要求された差分(effectiveTarget
  // -before)が小さい間は1バイト単位まで読み込みの粒度自体も切り詰め、
  // legacy自身のlinein()(old/src/alloc.c)と同じ粒度で改行1つ分ずつ
  // 進む――256バイト固定のままだと、上の「あと1行」という要求1回に
  // つき256バイト分(数十行相当)を一気に掴んでしまい、このペーシング
  // 自体が無意味になる。
  bool stalled = readMoreIntoPipe(effectiveTarget + 1);
  int after = static_cast<int>(streamPipe_->lines().size());
  // 2026-09-06再訂正(5回目、直接のユーザー報告「実際の内容(250行)を
  // ほぼ全部捨てて早期にソケットを閉じてしまうバグが旧にあるのなら、
  // なぜ最後まで読めるの？」): この間、「本物のEOFだけをトリガーに
  // すべき」という訂正を一度試したが、これは「ソケットが閉じても、
  // 既に送り込まれた未読データは消えない」という事実を見落として
  // いた――legacy自身の実機の(Exit)は「本当にもう二度と来ない」
  // ではなく、その狭い先読みマージン(current+2程度)への「今回は
  // たまたま届かなかった」という、その場限りの一時的な取りこぼしで
  // あり、それが繰り返し起きているだけだった(操作を続ければ本当に
  // 最終行まで到達できる)。そのため一瞬のEAGAIN(stalled)も本物の
  // EOF(streamAtEof_)も、区別せず等しく「今回は届かなかった」として
  // 扱う――lastLoadAttemptYieldedNothing()自身のgetter(streamAtEof_を
  // 無条件でORしている、その宣言コメント参照)が、いつ/どの経路で
  // EOFに当たったかに関わらず正しく反映する。
  if (stalled) lastLoadAttemptYieldedNothing_ = true;
  if (after == before) return false;  // 本当に1行も増えなかった――描画すべき新しい内容自体が無い
  view_.setLines(convertPipeLines());
  return true;
}

}  // namespace fm
