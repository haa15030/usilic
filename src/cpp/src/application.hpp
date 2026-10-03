#pragma once

#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "archive_mode.hpp"
#include "charset.hpp"
#include "configuration.hpp"
#include "content_view.hpp"
#include "cp_mv_rm_mode.hpp"
#include "cut_mode.hpp"
#include "epub_vertical.hpp"
#include "directory_mode.hpp"
#include "du_mode.hpp"
#include "display.hpp"
#include "edit_mode.hpp"
#include "exec_mode.hpp"
#include "file_mode.hpp"
#include "grep_mode.hpp"
#include "history.hpp"
#include "key_input.hpp"
#include "line_editor.hpp"
#include "copy_mode.hpp"
#include "list_browser_mode.hpp"
#include "locate_mode.hpp"
#include "man_mode.hpp"
#include "mode_stack.hpp"
#include "path_resolver.hpp"
#include "program_mode.hpp"
#include "shell.hpp"
#include "underline_mode.hpp"

namespace fm {

// アプリケーションレベルのコマンド、既に生のキー入力から解決済み
// ――get_m()の巨大なswitch(m)のcaseラベルが意味していたものを、
// それを生み出したバイト/エスケープシーケンスの認識(KeyInputの
// 仕事)や、この移植がまだ到達していない約50個のcaseラベルの
// どれも無しで。
enum class Command {
  None,
  MoveUp,
  MoveDown,
  // 「ウィンドウを1行上下へ動かす」(usilicrc自身のK-8/K-9の説明、
  // 'j'/'k'に紐付く――usilicrcが別に「カーソルを1行動かす」と説明
  // し、代わりに^N/^Pに紐付けているMoveUp/MoveDownと、正確にどう
  // 異なるかについてはContentView::scrollLineDown()自身のコメント
  // 参照)。
  ScrollLineDown,
  ScrollLineUp,
  PageUp,
  PageDown,
  Enter,
  // K-23(^O^M、M_fp、「行単位で読む」)/K-24(^[^M、M_code、「そのまま
  // 読む」)、いずれも(ls)専用(QUICK.txt自身の"<(ls)>"の注記参照)。
  // 2026-09-14追加、直接のユーザー依頼("（ls）での^O^M/^[^Mを実装
  // して")――openSelectedEntryNoWrap()/openSelectedEntryRaw()自身の
  // コメント参照。Directoryモード以外では、legacy自身のcr値が読み込み
  // ループの中では実質cr=0(素のEnter)と同一だった(alloc_buff()を
  // 追跡して確認済み)のに合わせ、単なるCommand::Enterへフォール
  // バックする。
  ReadFileNoWrap,
  ReadFileRaw,
  Back,
  // ';'/':' -- ユーザーによって直接確認済み(2026-08-03、最初は
  // これが何らかの「関連ファイル」ジャンプだという誤った推測が
  // あった後): これらは文字通りそれぞれBack+MoveDown+Enterと
  // Back+MoveUp+Enterの略記であり、それら自身の独立した機能では
  // ない。(dispatchByte()をインラインで展開するのではなく)それ
  // 自身のCommand値として保たれている。これにより、handleCommand()
  // の既存の「このCommandは実際に何をするか」という単一の場所が、
  // この3段階の合成を知る必要のある唯一の場所であり続ける――他の
  // 複合アクション(例えばStartFileInput/StartDirectoryInputが
  // startPathInputPrompt()を共有する)に対するこの移植の既存の
  // パターンに合わせている。2026-08-16: ';'ではなく':'から到達
  // 可能(理由についてはresolveCommand()自身のコメント参照――実際の
  // ユーザーによる訂正で、これら2つのどちらの文字がどちらへ対応
  // するかが入れ替わった。どちらのコマンド自身のBack+Move+Enter
  // の本体への変更でもない)。
  JumpNext,      // ':' : Back, MoveDown, Enter
  JumpPrevious,  // ';' : Back, MoveUp, Enter
  // (2026-09-09追加、2026-09-09廃棄): '{'/'}'(Command::MarkAndJumpNext/
  // MarkAndJumpPrevious、Enter+ToggleMark+Back+MoveDown/Upの合成)は
  // 「GREP結果一覧の最終行で連打すると進めなくなる」という設計上の
  // バグをユーザー自身が指摘し、GREP自体をin-place方式(このコメントの
  // すぐ下、StartIncrementalGrep/executeGrep()自身のコメント参照)へ
  // 作り直した
  // ことで、Directory起点では既存の'.'(ToggleMark)がその場でそのまま
  // 使えるようになったため、専用コマンドとしての存在意義が無くなり
  // 廃棄された。経緯の詳細は.steering/20260909-grep-inline-highlight/
  // requirements.md参照。
  // StartFgrep(2026-09-19追記): Enterで確定する一括のリテラル検索。
  // 2026-09-18に専用キー^O^Sを失い、2026-09-19には素の^S自体が
  // インクリメンタルサーチへ一本化された(すぐ下のStartIncrementalGrep
  // 自身のコメント参照)ため、今はどのキー割り当ても持たない内部/
  // テスト専用アーティファクトである――約40件の既存ユニットテストが
  // 「grep系プロンプトを開く」汎用トリガーとして参照しており、かつ
  // ^J^S(StartEgrep)が通るstartGrepPrompt()経路そのもののリテラル版
  // としてテストする価値があるので残してある。
  StartFgrep,
  StartEgrep,
  // (StartGrep: 2026-09-05追加、2026-09-19削除) プレーンなgrep
  // (基本正規表現BRE、GrepSyntax::Basic、当時の発動キーは素の^S)。
  // 2026-09-18に「BREの方がEREより易しいと勘違いしていた」という
  // ユーザー自身の気づきでGrepSyntax::Fixed(リテラル)へ格下げされ、
  // その時点で既にStartFgrepと完全に同じ挙動になっていた。さらに
  // 2026-09-19、^S自体がインクリメンタルサーチへ一本化されたことで
  // どのキーからも到達しなくなったため、重複したアーティファクトと
  // して削除した――Enter確定の一括リテラル検索を代表する内部/テスト
  // 専用アーティファクトはStartFgrepの方に一本化する(すぐ下の
  // StartIncrementalGrep自身のコメント参照)。
  // 2026-09-09追加(直接のユーザー提案、'{'/'}'廃棄の後継):
  // in-place GREPセッション中だけ、通常の^N/^P(MoveDown/MoveUp)の
  // 代わりにdispatchByte()が横取りして発行する――
  // jumpToGrepInPlaceMatch()自身の宣言コメント参照。
  GrepInPlaceNext,
  GrepInPlacePrevious,
  // 2026-09-10追加、直接のユーザー依頼("インクリメンタルサーチ...
  // いずれも可能ならやってみて...切り替えキーを新設すればいい"):
  // 当初は^S(当時のStartGrep、Enterで確定する一括検索)とは別の、
  // ESC i専用キーだった。startIncrementalGrepPrompt()自身のコメント
  // 参照――GrepPatternのプロンプトを開く点は一括検索と同じだが、
  // 以後の1文字ごとの入力で(Enterを待たずに)liveIncrementalGrepSearch()
  // がその場で検索し直し、in-placeハイライト+ジャンプを更新する。
  //
  // 2026-09-19改訂、直接のユーザー依頼("現状の^SでFGREP、^[iで
  // インクリメンタルサーチを廃止して、^Sでリテラルなインクリメンタル
  // サーチに一本化してほしい"): 発動キーが専用のESC i(^[i)からプレーン
  // な^S(K-31)へ移った。2026-09-18の統合(削除済みStartGrepの上の
  // コメント参照)で^Sと^[iはどちらも「GrepSyntax::Fixed(リテラル)で
  // 検索する」という全く同じ意味になり、違いは「Enterまで待つか、
  // 1文字ごとに検索するか」だけ――2つのキーを残す理由が無くなったので、
  // 後者(インクリメンタル)へ一本化した。これに伴い:
  //   - ^S(K-31)は、resolveCommand()・feedShellByte()の専用分岐・
  //     SHELL用エスケープハッチ^O/のいずれからもこのコマンドへ解決する
  //   - ^[i(ESC i)自体は廃止(dispatchLeaderByte()のescPending_の
  //     switchから削除、以後は未認識シーケンス)
  //   - Enterで確定する一括検索はキー空間には^J^S(StartEgrep、拡張
  //     正規表現)だけが残る(そのリテラル版はStartFgrep、キー割り当ての
  //     無い内部/テスト専用アーティファクト)
  //   - in-place方式の対象外モード(isGrepInPlaceEligibleMode()がfalse
  //     ――Grep自身/Cut(セグメント一覧)/Copy)では^Sは何もしなくなる
  //     (旧^Sはそこでも一括プロンプトを開けた)――in-place検索という
  //     機能自体がそれらの画面を想定していない以上、一本化の必然の
  //     帰結である。
  StartIncrementalGrep,
  // 2026-09-14再編、直接のユーザー依頼("CUTは一律grep仕様。Fixedは
  // 廃止。"+"^Jc/^J^Cがそれぞれfgrep/grep検索となっているのをいずれも
  // 「grepで^Jc（バッファ分割）/^J^C（ファイル分割。^Jb/^J^Bは廃止）」
  // でよろしく"): CUTは以前StartCutFixed(FGREP)/StartCutRegex(EGREP)の
  // 2方式を持っていたが、GrepSyntax::Basic(素のgrep、基本正規表現、
  // 他のトップレベルのGREP(^S)と同じ)一本化した――Fixed方式・EGREP
  // 方式のどちらも廃止(startCutPrompt()/startCutFilePrompt()自身の
  // コメント参照)。それに伴い^Jc/^J^Cの2キーも、「構文の選択」ではなく
  // 「対象がバッファかファイルか」を表す1組へ作り直した:
  // ^Jc(K-33、旧StartCutFixed) : CUT(バッファ分割)
  // ^J^C(K-34、旧StartCutRegex): CUT(ファイル分割、旧StartCutFileFixed/
  //                              StartCutFileRegexの後継――旧^Jb/^J^B
  //                              自体はキーごと廃止した)
  StartCut,
  StartCutFile,
  // ^J a: HELPとHELP.enの切替(helpPath_自身のコメント参照)。2026-09-14にTUTORIAL/TUTORIAL.enの
  // 切替として復活し、2026-10-02にTUTORIALがHELPへ改名されたのに合わせて改名。
  ToggleHelpLanguage,
  StartMan,
  StartManList,
  StartProgram,
  StartProgramList,
  StartLocate,
  LocateUpdateDb,
  StartShell,
  // '.' -- 実際のユーザー依頼、^X^M CP/MV/RMフロー(^Xc/^Xm/^Xrが
  // 操作を準備し、その後ユーザーが^X^Mの前に対象をマークする)に
  // 直接結び付いている: 元々は"「.」（現在行をマークしたのち下へ
  // (「,」は上へ)のマクロ）"(マークしてから前進/後退、この元々の
  // ペアについては下のMarkAndAdvance/MarkAndRetreat自身のコメント
  // 参照。今は'['/']'へ再配置されている)。2026-08-23
  // (docs/CHANGES.md項目415、Space-QuickLook実験自身の追加依頼):
  // 直接のユーザー依頼が、SpaceがCP/MV/RM/ARCの準備済み状態での
  // マーキングの仕事をやめた後(Command::QuickLook自身のコメント
  // 参照)、'.'自体を単純な移動無しのトグルへ再利用した("代わりに
  // '.'を選択/解除のトグル（行の移動は無し）としたい")――'.'と
  // ^Space(0x80)は今やまさにこの同じToggleMarkアクションに対する
  // 2つのキーである。
  ToggleMark,
  // '['/']'マクロ(2026-08-23に'.'/','から「再配置」された――なぜ
  // その2つが解放されたのかについてはToggleMark自身のコメント参照。
  // ユーザー自身の言葉: "代わりのマクロとしては暫定'['および']'に
  // してみて"): 現在のエントリのマークを(ToggleMarkと同様に)切り
  // 替え、その後カーソルを前進/後退させる。これにより、ユーザーは
  // ToggleMark+^Nを交互に使う代わりに、1つのキーを繰り返して一連の
  // エントリをマークできる。このペアが移植された元々の依頼: "コピー
  // したいディレクトリを選択しようにも[return]でそこへ移動してしまっ
  // てはどうにもならない".
  MarkAndAdvance,  // ']' -- Fileモードでページャーのファイル一覧が
                   // 有効な間はPagerNextFile自身のエイリアスでもある、
                   // 自身のコメント参照
  MarkAndRetreat,  // '[' -- 同様に、PagerPreviousFile自身のエイリアスでもある
  // 2026-08-25(直接のユーザー依頼、"旧版にない機能として...less同様
  // 'n''p''x''f'"): Fileモードの「ページャーファイル一覧」――
  // usilicがコマンドラインで起動された時の一連のパス(2つ以上の
  // 位置ファイル引数。startPagerFileList()自身のコメント参照)――
  // を移動する。ニーモニック: n/pは1ファイル前後へ移動する(less
  // 自身の:n/:pのように)。x/fは最初/最後のファイルへジャンプする
  // (xはless自身の:x「最初のファイルを調べる」に一致する。f――
  // 「final」――にはlessの等価物は無く、対称な対応物というユーザー
  // 自身の依頼による純粋なusilic限定の追加)。']'/'['(上の
  // MarkAndAdvance/MarkAndRetreat)は、ページャー一覧が有効な状態で
  // Fileモードにいる間、「同じ」次/前のアクションを兼ねる――自身の
  // handleCommand()の分岐参照――DirectoryMode自身のマーキングが
  // そのペアに対して既に確立した同じ上下の慣習に従っており、
  // (既に無関係なJumpNext/JumpPreviousの「Back+move+select」マクロ
  // に紐付いている、resolveCommand()自身のコメント参照)';'/':'を
  // 再利用するのではない。他のどこでも(ページャー一覧が有効でない、
  // またはファイルが2個未満)何もしない。'.'/[/]'がDirectory/
  // Archiveの外側では無効なのと同様。
  PagerNextFile,      // 'n'、そしてページャー一覧が有効な間は']'も
  PagerPreviousFile,  // 'p'、そしてページャー一覧が有効な間は'['も
  PagerFirstFile,     // 'x'
  PagerLastFile,      // 'f'
  // 2026-09-20新設、直接のユーザー依頼(編集中のTUTORIALへの追記
  // "(ls) では、通常ファイルを複数選択すると...^Ou ではそれらが順に
  // 画面表示され、; および : キーでファイルを移動します")。
  //
  // 2026-09-20再設計(直接のユーザー指摘、初回実装は撤回): 当初は
  // 上のPagerNextFile系(pagerFileList_/navigateToPagerFile()、CLIの
  // 複数位置引数起動用)をそのまま再利用し、';'/':'をdispatchByte()で
  // 横取りする形にしたが、ユーザーからの指摘で以下が判明した:
  //   - CLIの複数位置引数起動は元々「usilic内で完結させたい」という
  //     経緯(HISTORY.md:18159、^Jp usilicの前例)があり、それが実際
  //     には`FileMode::pagerMode_`(isPagerMode())という、'q'
  //     (QuitPager)がusilicプロセス全体を終了させる等、外部ページャー
  //     的な挙動を伴う仕組みだった――^Ouをそこへ相乗りさせるのは筋が
  //     悪い。
  //   - ';'/':'はCommand::JumpNext/JumpPrevious(Back+Move+Enterの
  //     マクロ)という「本来の機能」を持つのに、横取りしてしまうと
  //     それを隠してしまう。CUT(セグメント一覧)がこのマクロをその
  //     ままの意味で使えているのは、CUTが「専用の一覧モード」を
  //     持っているからであり、File自身がその一覧を代行しているから
  //     ではない。
  //   - 実際、素のDirectoryから開いたFileで';'/':'を試すと、
  //     Back+MoveDown/Up+Enterがディレクトリの「全エントリ」(マーク
  //     済みかどうかを問わない)に対して働くため、次のエントリが
  //     たまたまディレクトリだと「(ls)へ戻って終わり」に見える
  //     ――マークしたファイルだけを巡回する仕組みには最初からなって
  //     いなかった。
  // そこで、CUT/LOCATE/DUと同型の、独立した新モード
  // ApplicationMode::Underline(UnderlineMode、専用のunderlineView_)
  // を新設する。^OuはこのモードのEnter専用リストを表示するだけ
  // ――pagerFileList_・isPagerMode()には一切触れない。リストから
  // Enterで開いたFileはfileReturnMode_ = ApplicationMode::Underlineを
  // 設定し(Du/Locateと同じ慣習)、Backはこの一覧へ戻る
  // (returnFromUnderline())。この一覧「自体」がJumpNext/JumpPrevious
  // (Back+Move+Enter)の対象になるので、';'/':'は本来のCommand値
  // (JumpNext/JumpPrevious)のまま何も変更する必要がない――専用の
  // UnderlineViewNext/Previousのようなコマンドも、dispatchByte()での
  // 横取りも一切不要になった。詳細は
  // .steering/20260920-underline-mode-redesign/design.md参照。
  StartUnderlineView,
  // 2026-09-21新設(`^Ju`): StartUnderlineView(`^J^U`、一覧表示)を
  // 発動した上で、一覧のトップのエントリを`^M`で開いた後の画面へ
  // 直接遷移する(`^Jd`/`^J^D`、`^Jm`/`^J^M`が「実行・入力」と
  // 「一覧表示」に分かれているのと同じ形)。
  StartUsilicHistorySorted,  // `^J^U`
  StartUsilicHistory,        // `^Ju`
  StartUsilicToc,            // `^[u`
  StartCopy,
  StartMove,
  StartRemove,
  // K-71(^X^M、M_dest)は2026-09-08に削除された(直接のユーザー依頼、
  // "冗長分は全て削除で"): StartCopy/StartMove/StartRemoveは既に
  // それぞれ自分自身でstartDestinationPrompt()を直接呼ぶので、独立
  // したキー(^X^M)からこれへ到達する経路は、CP/MV/RMに対しては
  // 完全に冗長だった。ARCについても、^Oa(startArchivePrompt())が
  // 既に単独で(アーム状態を必要とせず)同じ宛先プロンプトを開くので
  // 同様に冗長――startDestinationPrompt()自身の(削除済みの)Archive
  // モード分岐のコメント参照。
  StartExit,
  StartDu,
  // ^J^D(K-38、旧「Redisplay window」): 2026-09-09、直接のユーザー
  // 依頼("^J^D : DU画面へ戻る。集計は行わず")によりStartDuから分離。
  // 以前は^Jd(StartDu)と全く同じコマンドへ解決していた(du(1)を
  // 毎回再実行し、既存の結果へ無条件でプロンプト経由の入力を要求
  // していた)――今は、既存のDU結果があればプロンプト無しで即座に
  // それへ戻るだけの、純粋な「再表示」。まだ一度もDUを実行して
  // いなければno-op。redisplayDu()自身のコメント参照。
  RedisplayDu,
  StartDirectoryHistory,
  // (StartCommandHistory: 2026-09-20削除、直接のユーザー依頼(^Rの
  // 再設計、"^J^Rを^J^Pに置き換える(^J^Rは廃棄)")。旧startListBrowser()
  // 自身の削除コメント参照(application.cpp)。)
  // K-49(素の^R、M_hist、" HISTORY"): リストブラウザの外側から
  // 「別の」カテゴリへジャンプする方法では「ない」(この移植の以前
  // のバージョンはまさにそれだと誤解し、StartDirectoryHistoryへ
  // エイリアスしていた――直接の2026-08-04のユーザーによる明確化と、
  // 純粋に文脈に依存するmenu.cのm_list()の読み直しによると誤り:
  // `is_name(ap->name, M_lfile) -> T_hfile`等)。「現在開いている」
  // ListBrowser(どのRingCategoryであれ――FILE/DIR/MAN/LOCATE/
  // FGREP/EGREP/COMMAND)を、その2つの表示モードの間で切り替える:
  // 通常(アルファベット順にソート、重複排除――重複排除は
  // History::pushRecent()自身の最初の出現優先ポリシーから既に無料
  // で得られていたが、ソートは得られていなかった)と「HISTORY」
  // (生の時系列アクセス順、最古が上、「最新は最下行」――ユーザーに
  // よって直接確認済み: "FILE（DIR）は今までアクセスしたファイル
  // （ディレクトリ）をソートしたのち重複を排除した一覧、HISTORYは
  // アクセスした順（最下行が最新）")。ListBrowserモードの外側では
  // 何もしない(falseを返す)。^Rが現在の文脈に当てはまらない時の
  // m_list()自身の`echo_mess(S_errf)`に合わせている――Application::
  // toggleListBrowserHistory()参照。
  ToggleHistoryView,
  StartEdit,
  StartHelp,
  // 2026-10-02追加、直接のユーザー依頼(.steering/20261002-help-key-usilicrc-reorg/):
  //   StartKey(^K、^J^K): KEYモード。HELPを「：」でCUTした一覧(冒頭の「--」は除く)。startKey()参照。
  //   KeyJumpToHelp(h、KEYの中だけ): 選んでいる行の、平文のHELPの同じ行へ。
  //   OpenPlainHelp(^J^Oh): 平文のHELP(helpPath_のファイルをそのまま)。
  //   OpenUserUsilicrc(^J^Ou): ~/.usilic/usilicrc。
  //   SaveMacro(^Xs、2026-10-03に^X^Sから): usilicrcの読み直し、記録したマクロの保存とマクロの一覧。saveMacroToUsilicrc()参照。
  StartKey,
  KeyJumpToHelp,
  OpenPlainHelp,
  OpenUserUsilicrc,
  SaveMacro,
  StartMacroRecord,
  StopMacroRecord,
  PlayMacro,
  // ToggleGuiMode(^Og)は2026-10-02に廃止(「実はすでにGUI」方針の打ち切り、-gが正式オプションに。
  // .steering/20261002-help-key-usilicrc-reorg/)。
  // K-3(^L、M_clear): アクティブなビューの完全な再描画を強制する。
  Redraw,
  // ^Z(M_susp)――legacy自身のkey.c(M_susp、`f_M != M_shell`の場合の
  // 分岐)からの移植、直接のユーザー依頼("^Zも実装して")。raw modeでは
  // カーネルの行編集(ISIG)を切っているため、Ctrl-Zを押しても通常の
  // ジョブコントロール(SIGTSTP)は自動発火しない――このコマンド自身が
  // 明示的にleaveRawMode()してから自分自身にSIGTSTPを送ることで代用
  // する(handleSuspend()自身のコメント参照)。legacy同様SHELLモード中
  // (子プロセスへの生バイト転送)は対象外――resolveCommand()経由では
  // dispatchByte()自身のShellモード早期分岐(feedShellByte())より後にしか
  // 届かないため、この制限は自然に成り立つ。同じ理由でプロンプト編集中
  // (promptKind_ != None)も対象外――legacy自身のf_M != M_shellという
  // 単純な条件よりは狭いスコープだが、素の閲覧中の誤操作からの復帰
  // (最も一般的な使い方)を素直にカバーする。
  Suspend,
  // K-12/K-13(ESC< / ESC>、M_head/M_tail): 最初/最後の行へジャンプ
  // する――ContentView::goToTop()/goToBottom()は既に存在していたが
  // 一度もキーに配線されていなかった(実際の報告: usilicrc自身の
  // KEYモードのヘルプ画面を、この移植のキーテーブルと1行ずつ比較
  // したところ、このような既に実装済みだが配線されていない/決して
  // 移植されていないキー割り当てをいくつも一度に発見した――
  // CLAUDE.md参照)。
  GoToTop,
  GoToBottom,
  // ^X^X(K-16、M_jump相当): ContentView::markJumpOrigin()自身の宣言
  // コメント参照(2026-09-25追加)。
  ExchangeJumpMark,
  // K-70('q'、M_quit): (StartExit/^X^Cとは異なり)y/nの確認無しで
  // 即座に終了する――usilicrcはこれをページャー専用("Exit usilic
  // (pager)")として文書化しているので、handleCommand()はDirectory
  // モードの外側でだけこれを尊重し、そのスコープに合わせている
  // (Directoryモードでの素の'q'は、これが追加される前と同様、
  // 何もしないままである)。
  QuitPager,
  // K-25(^Oh、M_hs): Display::smoothScrollEnabled()を切り替える
  // ――usilicrc自身のSCROLLセクション("When the flag is set, usilic
  // window will scroll within the range above bottom line"――まさに
  // moveUp()/moveDown()自身のコメントにあるように既にモードライン
  // 行を除外しているDisplayのハードウェアスクロール領域経路)から
  // の移植。Display::setSmoothScrollEnabled()/History自身の
  // smoothScrollEnabled_フィールドは既に存在していた(履歴ファイル
  // のUSILICトレーラー行からパース/そこへシリアライズされる)が、
  // どちらも一度もキーに配線されておらず、起動時にDisplayへ適用
  // されてもいなかった――両方のギャップがここで一緒に修正されて
  // いる。トグルキーは、それが切り替える永続化された値がそもそも
  // 実際にDisplayに到達しないなら意味を成さないため。
  ToggleScrollMode,
  // 2026-09-12追加、直接のユーザー依頼(項目⑫、"モードラインの表示/
  // 非表示切り替え...さらに言えば-g起動時の非表示モードでは、macOSの
  // Dockのようにモードラインを自動的に表示/非表示にする"): ^Ohを
  // ToggleScrollMode(すぐ上)からこちらへ付け替えた――旧割り当ての
  // 対象(Display::smoothScrollEnabled())は、単一行カーソル移動時に
  // 端末のハードウェアスクロール領域シフトを使うか完全redraw()するか
  // という、現代の端末では利用者から見えるべき機能差の無いレンダリング
  // 技法の選択に過ぎないと判明した(moveUp()/moveDown()自身の調査
  // より)一方、モードライン表示/非表示は実際に利用者の目に見える形で
  // 有用なため、より価値の高い機能へ^Ohを再割り当てする方が理にかなう
  // と判断した。ToggleScrollMode自体・Command本体・Display/History
  // 側の永続化機構はコードとして残したまま(どのキーからも到達
  // できないだけ)――usilicrcのSCROLLセクションで設定した起動時
  // デフォルトはそのまま尊重され続ける(handleCommand()自身の
  // ToggleScrollMode caseのコメント参照)。Display::modeLineVisible()/
  // setModeLineVisible()を切り替え、History::modeLineVisible_へ永続化
  // する。falseの間はApplication::layoutViews()のcontentHeight計算が
  // モードライン用の1行を予約しなくなり、ContentView::renderModeLine()
  // 自身がその値を見て描画を丸ごと抑制する――どちらも自身のコメント
  // 参照。GUIモード(-g)限定のDock風自動表示/非表示は、この永続化
  // された「希望」とは別に、main.cppのrunGuiPhase()がマウス位置に
  // 応じて一時的にDisplay側の実効状態だけを操作する(この「希望」
  // フィールド自体は変更しない)――両者を1つのbooleanに混ぜると
  // 手動トグルと自動再表示が互いに干渉してしまうため、意図的に
  // 分離してある(main.cpp自身のコメント参照)。
  ToggleModeLineVisibility,
  // K-45/K-47(^Xf / ^Xd、M_lfile2/M_ldir2――「Input file/directory
  // name」): パスを尋ねてそれを開き、PathResolverが実際にそれが
  // 何であると言うかに応じてFileまたはDirectoryモードに着地する。
  // (usilicrc自身の2つのK番号、そして今後おそらく異なる2つの
  // プロンプトラベルに合わせて)2つの別々のキー/コマンドだが、
  // 「このパスがどんな種類のものか」を解決することはどちらのキー
  // がプロンプトを開始したかに依存しないので、1つの共有された
  // 実行経路である――startPathInputPrompt()自身のコメント参照。
  // 「Select」の対応物(K-46/K-48、^X^F/^X^D、M_lfile/M_ldir)は、
  // 単なるテキストプロンプトではなく「一覧」ベースの補完ピッカー
  // であり、ここではカバーされていない――将来の課題、CLAUDE.md
  // 参照。
  StartFileInput,
  StartDirectoryInput,
  // K-26(^Oo、旧^Ol、M_ls): DirectoryMode::filenameOnlyMode()を切り替える
  // ――usilicrc自身のLSセクション("When the flag is set, file-
  // selector will display only filename")からの移植。Directory
  // モードだけにゲートされており、K-26自身の「file-selector」
  // スコープに合わせている(File/Man等の内容ビューでは意味を成さ
  // ない)。ToggleScrollMode/K-25と同じ方法で永続化される。2026-09-08
  // に^Olから^Ooへ改名された(直接のユーザー依頼、"^Ol⇒^Oo（←
  // ファイル名以外はomission）")――空いた^Olは2026-09-12、
  // ToggleLsReverse(すぐ下)へ新たに割り当てられた。
  ToggleLsMode,
  // 2026-09-12追加、直接のユーザー依頼("`-l`以外も変更できるように
  // したい...付加するオプションとして`^Ol`が`-r`トグル"): DirectoryMode
  // の`ls`呼び出しへ`-r`(逆順)を付加するかどうかを切り替える。
  // ToggleLsMode(^Oo)と同じくDirectoryモード限定。
  ToggleLsReverse,
  // 2026-09-12追加、同じユーザー依頼("`^O^L`が`-t`/`-S`/`-k`切り替え"):
  // DirectoryModeの`ls`呼び出しに付加するソートキーを、デフォルト
  // (名前順)→`-t`(更新日時順)→`-S`(サイズ順)→`-k`(拡張子順、macOS
  // 版lsには相当オプションが無いためDirectoryMode自身の自前実装、
  // その宣言コメント参照)→デフォルト、の順で巡回させる。`^O^L`
  // (0x0c)は元々Command::Redraw(「SHELL内から」用のエスケープハッチ)
  // に割り当て済みだったが、モードガードが無くDirectoryモードでも
  // 誤って発動していたため、Redraw側にShellモード限定のガードを
  // 追加した上で、Directoryモードでの`^O^L`をこちらへ回した
  // (dispatchLeaderByte()自身のコメント参照)。
  CycleLsSortKey,
  // K-72/K-73(^X^Y / ^X^N、CP/MV/RM/ARC向け「Mark all」/
  // 「Clear all」): ToggleMarkの一括版――^Spaceを1つずつ押す代わりに、
  // 1回のキー入力で現在の一覧の全エントリをマークするか、全マーク
  // を落とす。ToggleMark自体と同じDirectoryモード限定のスコープ。
  MarkAll,
  ClearAllMarks,
  // K-51/K-53/K-55/K-57(^X^Kc/^X^Km/^X^Kr/^X^Ka、"with SHELL
  // window")は2026-08-23に削除された(直接のユーザー依頼、CP/MV/
  // RM/ARCの実行が常に本物のSHELLセッション経由で実行されるよう
  // 統一された時点で――CP/MV/RMについてはexecuteCopy()自身のコメ
  // ント、ARCについてはexecuteArchiveExtract()自身のコメント参照
  // ――これらは、StartCopy/StartMove/StartRemove/
  // StartArchiveExtractと機能的に同一の重複になった)。
  // K-17(^X^W、M_w4): アクティブなビューの完全な内容をwFilePath_
  // へ書き込む(コンストラクタ自身のコメント参照)――menu.c自身の
  // `send_pp(hp->bp->pp, "a")`(追記モード)からの移植。
  SaveBufferToFile,
  // K-61/K-62(^J. / ^J,、M_chdir2/M_chdir): Shellモードへ切り替え
  // (まだ実行されていなければ起動する)、生成されたコマンドを即座に
  // 送信する――menu.c自身のm_w()(M_chdir2/M_chdirケース)からの
  // 移植: K-61には"cd '<directory>'"(legacy自身のlp->dirに合わせた
  // 「現在の」DirectoryModeのディレクトリ)、K-62には
  // usilicrc自身のK-62の説明が名付けるリテラルの"ls -l"。両方とも
  // 自動送信される(legacy自身の生成される文字列は\rで終わる)
  // ――下のK-63/K-64は意図的にそうしない。
  ShellChangeDirectory,
  ShellRunLsL,
  // K-63/K-64(^Jw / ^J^W、M_w/M_w2): Shellモードへ切り替え、
  // コマンド行を「事前に埋める」(送信は「しない」)――menu.c自身の
  // m_w()(M_w/M_w2ケース)からの移植。K-63は"echo <name>"(現在
  // 選択/開かれている項目の名前――legacyはまず一時ファイルに
  // それを書き込むが、それは単に、この移植のインメモリの
  // LineEditorには当てはまらない1990年代のシェルクオートの懸念を
  // 回避するためである。shellInsertName()自身のコメント参照)を
  // 挿入する。K-64は、実際にアクティブなビューの完全な内容をそこへ
  // (K-17/SaveBufferToFileと同じ対象ファイルへ)書き込んだ後、
  // "cat <wFilePath_>"を挿入する――K-63とは異なり、ここには本物の
  // ファイルを回避する方法が無い: `cat`は読むために1つ必要とする。
  ShellInsertName,
  ShellInsertBufferContent,
  // K-19(^O^K、M_m): 現在のディレクトリの親へジャンプする。
  // DirectoryMode::goToParent()自身のコメント参照。
  GoToParentDirectory,
  // K-18(^J^K、「Go to file-selector」): Fileモードから、「現在の」
  // ファイル自身の含むディレクトリの本物の`ls`一覧へ直接ジャンプ
  // する――Application::goToFileSelector()自身のコメント参照。
  // 直接のユーザー依頼("検索ファイル所在地へ降りるためのキー
  // アサインも実はあるので反映してほしい（^J^K）")、2026-08-14に
  // 追加。usilicrc自身のK-18行そのもの以上にはlegacyソースで検証
  // されていない(この移植には、カーソル位置決めの挙動を比較する
  // べきlegacyバイナリが無い、範囲についてはgoToFileSelector()
  // 自身のコメント参照)。
  GoToFileSelector,
  // ^J^J(2026-08-27、直接のユーザー依頼: "履歴直近ファイルへ戻る
  // ...^J^K同様どこでもキーなので反映よろしく"): RecentFileリング
  // の「最も最近の」エントリ(History::recentEntries(
  // RingCategory::RecentFile).back())を開く――^X^F自身の裏付けと
  // なるストア、自身のコメント参照――openSelectedListEntry()自身の
  // RecentFile分岐が既に確立したのとまさに同じアーカイブ優先、
  // ファイルをフォールバックとするロジックを再利用する。
  // (それ自身の対象を、利用可能などんなモード固有の「現在の
  // ファイル」からでも計算する、つまり何を開くべきかを知るためだけ
  // にモードごとの分岐が必要な)^J^Kとは異なり、これはhistory_を
  // 直接読み、全てのモードから意味を持つ――モードごとのゲートは
  // 全く無い、goToRecentFile()自身のコメント参照。動機となった
  // 報告: ^X^M/^X aがアーカイブ展開自身の移動先でDirectoryモードに
  // 着地し(startArchivePrompt()自身の2026-08-26のコメント参照)、
  // ユーザーがそこからさらに降りると、^Gは通常のディレクトリ
  // スタックだけをpopし(親ディレクトリへ戻る)、アーカイブ自体へは
  // 決して戻らない――しかしアーカイブは依然としてRecentFile自身の
  // 最も最近のエントリなので、^J^Jは、それ以来何個の素のディレク
  // トリに入場したかに関わらず、直接それに到達する。
  GoToRecentFile,
  // ^J0..^J9(legacyのM_cdリーダー、`menu.c`のm_dir()のデフォルト
  // ケース、T_0..T_9): まずlegacyで検証した(PTYで確認済み: 深く
  // ネストされたcwdから^J0/^J1/^J2は正しく"/"/"/Users/"/
  // "/Users/haa15030/"へchdirする)後の、直接のユーザー依頼
  // (2026-08-19/20、"^J?（?は数字）は単なる文字列上も含めたまさに
  // どこでも機能代表格なのでおよそ「/」がある場所では無条件に
  // （戻り値も不要）所定の（ls）へ移行して欲しい")。現在の画面に
  // 対して「有効な」どんなパス文字列であれ、左から(数字+1)番目の
  // '/'まで切り詰め、その`ls`を表示するDirectoryモードへ「直接」
  // ジャンプする――ユーザー自身の"無条件に(戻り値も不要)"という
  // 位置付け(覗き見ではなく、意図的な、後戻り無しの切り替え)に
  // 合わせて、「プレビューし、^Gで復元する」というステップは無い。
  // 各currentMode_が正確にどのモードごとの「パス文字列」へ対応する
  // か、そしてなぜこれがm_dir()自身の単一の`dir`ローカル変数(この
  // 移植にはそれに相当する単一の信頼できる情報源が無い)を再利用
  // するのではなく、goToFileSelector()/goToParentFromFileOrArchive()
  // が既に確立した「アクティブな寄り道モードが何であれそこから
  // パスを計算し、そこでenterDirectory()し、currentMode_を切り替
  // える」という形(同じ「K-18/K-19をlegacy自身のDirectory専用の
  // m_dir()を超えて一般化する」という前例)を直接モデルにしている
  // かについてはApplication::jumpToPathPrefix()自身のコメント参照。
  // pendingPathPrefixDigit_は、dispatchLeaderByte()自身の
  // leaderJPending_スイッチから、数字をjumpToPathPrefix()まで
  // 運ぶ(この移植ではCommandの値はペイロードを運ばない)。
  JumpToPathPrefix,
  // K-60(^Xs、M_hup): 現在のSHELLの子(もしあれば)を殺し、新しい
  // ものを起動する。Application::shellRestart()自身のコメント参照。
  RestartShell,
  // K-58(^Xa、ARC「Expand archive」)――K-57(^X^Ka、「with SHELL
  // window」)は2026-08-23に削除された。StartCopyInteractive自身の
  // 以前のコメント(今はClearAllMarks付近のこのenum自身のコメント)
  // 参照。Application::startArchivePrompt()自身のコメント参照。
  StartArchiveExtract,
  // K-67(^X^Ke、M_error、「ERROR」): errorLogPath_をFileモードで
  // 開く。K-68(^X^Kl、M_log、「LOG: SHELL output」): Shell::
  // scrollback()をlogFilePath_へスナップショットし、「それ」を
  // Fileモードで開く。Application::executeShowErrorLog()/
  // executeShowShellLog()自身のコメント参照。
  ShowErrorLog,
  ShowShellLog,
  // K-96(^J^Od、2026-09-27): ~/.usilic/data/をDIRで開く。K-97(^J^Ow):
  // ^[uの目次のWORK(^X^Wの作業ファイル、複数あり得る)を直接開く。
  // Application::executeOpenDataDirectory()/executeOpenWorkList()参照。
  OpenDataDirectory,
  OpenWorkList,
  // K-98(^Je)/K-99(^J^E、2026-09-28): EPUB: 入力/一覧。Application::openEpub()参照。
  StartEpubPrompt,
  StartEpubList,
  // K-100(^Oe): (ls)で選んでいる.epubをARCで開く(2026-09-29、縦書き/横書きの切り替え
  // から変更)。Application::openEpubAsArchive()参照。
  OpenEpubAsArchive,
  // ESC l("LOG"): Application::executeStartLog()自身のコメント参照。
  StartLog,
  // K-0(^T、M_usilic、「USILIC」): ステータス/バージョン情報画面を表示
  // する。Application::executeShowUsilicBanner()自身のコメント参照。
  ShowUsilicBanner,
  // StartTutorial(^T)は2026-10-02に廃止(TUTORIALはHELPへ改名、^Hで開く。
  // .steering/20261002-help-key-usilicrc-reorg/)。
  // K-27(^Oa)は2026-09-20に廃止された。2026-09-01に一度、直接の
  // ユーザー依頼("^Oaはデフォルト文字コード切り替えではなくてHELP/KEY
  // の日本語<->英語切り替えでいいと思う")によりCycleCharset(Ascii ->
  // Euc -> Utf8の3値巡回)からToggleHelpLanguage(HELP/KEYの日英切替)へ
  // 差し替えられていたが、直接のユーザー依頼("今のKEYをHELPに置き
  // 換え...正味の：カットによるHELPとしてほしい")によりHELP自体が
  // usilic.man(日本語専用、usilic -mで生成)のCUT-by-fileへ全面的に
  // 作り直され、日英切替という概念自体が意味を持たなくなったため、
  // このキー自体を(CycleCharsetへ戻すことも含めて)未割り当てのまま
  // にした――再割り当てが必要になれば別途検討する。
  // K-46(^X^F、「FILE: Select file name」): RingCategory::RecentFile
  // に対するListBrowserを開く。K-48(^X^D、「DIR: Select directory
  // name」)は「既存の」StartDirectoryHistoryを再利用するので、
  // それ自身のコマンドは不要。(訂正、2026-08-04: このコメントの
  // 以前のバージョンは、さらにStartDirectoryHistoryが「^R自身の
  // コマンド」だと主張していた――誤り。素の^RがどのRingCategoryが
  // 閲覧されているかと無関係である理由についてはToggleHistoryView
  // 自身のコメント参照。)
  StartFileHistory,
  // K-2(^H)は2026-09-20に廃止された――直接のユーザー依頼("今のKEY
  // をHELPに置き換え、KEYモードでは無くなる")により、旧KEY(QUICK.txt
  // "KEY"章のタブ分割によるキー一覧、CutOrigin::Key)は完全に退役し、
  // ^H自身がStartHelp(新HELP、下記)を指すようになった。かつての
  // Command::StartKey/startKey()/loadKeyText()/wrapKeyText()/keyText_/
  // CutOrigin::Keyは全て削除済み(git履歴には残る)。
  // 実験的(2026-08-23、単なるSpaceの意味を上乗せしてみるという
  // 直接のユーザー依頼――legacy/usilicrc自身のキーテーブルの一部
  // ではない)。最初の一切れ(docs/CHANGES.md項目414)はこれを
  // Directoryモードだけにスコープし、準備済みのCP/MV/RMを代わりに
  // 「選択を進める」(MarkAndAdvance)という合図として使っていた。
  // 直接の追加依頼("[space]によるクイックルックは採用でいいと思う
  // ...ただしハイライト表示での[space]は、やはりCP/MV/RM（とARC）
  // で混乱を招く")が、Quick Lookの半分は保ったがCP/MV/RMの意味の
  // 上乗せは完全に取り除いた――マーキングは代わりに'.'/ToggleMark
  // と'['/']'へ移動した(それら自身のコメント参照)――そしてスコープ
  // を「広げた」("LOCATE、FGREP等、ハイライト状態のレギュラー
  // ファイルならどこでも発動できる方針で"): Spaceは今や、PROGRAM
  // プロンプト無しに、カーソルの下にある実際のレギュラーファイル/
  // ディレクトリに対して直接`qlmanage -p`(macOSのQuick Look)を
  // 起動する。Finder自身の「ファイルを選択し、Spaceを押す」という
  // ジェスチャーに合わせている――Directoryモード、Locate結果、
  // そして「起源」がDirectoryモードだったGrep結果画面
  // (grepReturnMode_、そこでの各マッチした行は依然として本物の
  // ディレクトリエントリと1:1で対応するため――ファイル自身の内容に
  // 対するGrep実行にはそのような行ごとのファイルアイデンティティ
  // が無く、意図的にカバーされていない)。Archive/ArchiveContent
  // (仮想の、まだ展開されていないエントリ)、Du(ディレクトリの
  // 「サイズ」行、「レギュラーファイル」ではない)、そしてListBrowser
  // のRecentFile/RecentDirectory候補(原則として実際のパスだが、
  // Spaceは既にそこで意味のある形で紐付いている――「リテラルな
  // 空白から始めて候補の編集を開始する」――feedRegexListByte()
  // 自身のコメント参照)は全て意図的に除外されている――正確にどの
  // モード/各パスがどう解決されるかについてはApplication::
  // executeQuickLook()自身のコメント参照、そしてこの上書きが実際
  // にどこに存在するか(純粋なバイト->Commandの対応付けのままで
  // ある静的なresolveCommand()ではない)についてはdispatchByte()
  // 自身のコメント参照。
  QuickLook,
  // ^O^C(M_copy、「COPY」): 現在のコンテンツ領域をCopyModeへ凍結
  // する――startCopy()自身のコメント参照。より明白な「StartCopy」
  // ではなくEnterCopyModeと名付けられている。その名前は既に(上の
  // ^Xc、無関係なCP/MV/RMファイルコピー操作、legacy自身のM_cp)
  // 使われているため。
  EnterCopyMode,
  // ^O^Y(M_paste): アクティブなプロンプトのカーソルに、CopyModeの
  // 共有されたclipboardText()を挿入する――handleCommand()自身の
  // Pasteケース参照。
  PasteClipboard,
  // ^[P(新設、2026-09-08、直接のユーザー要望): DirectoryMode::
  // transientMarkedNames()(CP/MV/ARC完了後の一時マーキング)の内容を、
  // アクティブなプロンプトへフルパス+シェルクォート済みで挿入する。
  // PasteClipboardの「アプリ内クリップボード」とは別の、ディレクトリ
  // の一覧から直接読む経路――既存のCOPYモード専用^[p(小文字、
  // copyEscapePending_側)とも無関係。
  PasteTransientTargets
};

// 現在どの「*Mode」が画面上にあるか。legacyのnp->M(T_*モード定数)
// からの移植。この一切れが知っているモードだけに絞られている。
// CutとCutContentは1つのlegacyモード(M_cut)に対する2つの別々の
// 画面である: Cutはセグメントラベルの一覧を表示し、CutContentは
// 選択された1つのセグメントの実際の行を表示する――CutMode自体が
// なぜそれらを統合しないのかについてはクラスコメント参照。
enum class ApplicationMode {
  Directory,
  File,
  Grep,
  Cut,
  CutContent,
  Man,
  Locate,
  Archive,
  ArchiveContent,
  Shell,
  Du,
  // 2026-09-20新設、Command::StartUnderlineView自身の宣言コメント
  // 参照(`^Ou`)。
  Underline,
  ListBrowser,
  // COPY(M_copy/^O^C、copy_mode.hpp参照): code.cのmake_map()から
  // 移植された、他のどのモードからでも到達可能な凍結されたテキスト
  // 選択のオーバーレイ。このenumの中で意図的に最後にある:
  // ApplicationModeに対する既存の全ての網羅的なswitchはこの値より
  // 前に書かれており、`default:`へ落ちるので、ここでこれを追加しても、
  // それらの既存の挙動を黙って変えることはできない。
  Copy
};

// 2026-09-19追加、Application自身のmanAnchor_/locateAnchor_/duAnchor_
// 宣言コメント参照――MAN/LOCATE/DUの各セッションの恒久的な基点
// (nearestFileOrDirectoryAnchor()により解決される、File/Directoryの
// どちらか)が実際に指していた「中身」を、後で(GREPの結果からの
// 着地やDU自身のopenSelectedDuEntry()等、途中の寄り道がfileMode_/
// directoryMode_という「共有された」状態を無関係な内容へ上書きして
// しまった後でも)確実に復元できるよう凍結しておくためのスナップ
// ショット。Application自身のクラス本体より前、名前空間スコープに
// 置いてあるのは、ApplicationMode自体がそうであるのと同じ理由――
// Applicationクラス内の複数の箇所(ヘルパー関数の宣言とメンバー変数
// の宣言)がこれを、互いの相対的な出現順に関わらず参照する。
struct AnchorSnapshot {
  std::string path;
  // 基点がFileの場合だけ意味を持つ――fileReturnMode_自身の宣言コメ
  // ント参照。Directoryの場合は無視される(既定値のままでよい)。
  ApplicationMode fileReturnMode = ApplicationMode::Directory;
};

// 最上位のコントローラ: ModeStack(複数)と全ての*Modeインスタンスを
// 所有し、キー入力をCommandへ対応付け、各Commandを現在の*Modeへ
// ディスパッチする。menu.cのget_m()(キー入力からコマンドへの
// switch)+m_cr()(「Enterが押されたので正しいことをする」ルー
// ティング)+m_bl()(^G)+m_row()/m_page()(カーソル移動)+
// m_grep()/m_cut()(^S/^J^S/^Jc/^J^Cで検索/切り出しへ入場)+
// menu()/init_menu()(外側の実行ループと起動)からの移植――合わせて、
// このプロジェクト全体が1クラスずつ積み上げてきた、約3300行の
// mode.c/menu.cの「Mode」レイヤーである。これはその初期の一切れ
// (Directory + Fileのナビゲーション、加えてFGREP/EGREP検索とCUT)
// であり、意図的にまだここに無いものについては下の「意図的に除外」
// 参照。
//
// これは逐語的な移植ではない。legacyのget_m()/m_cr()/m_bl()/
// m_grep()/m_cut()は、十数個の共有される可変構造体
// (HEAP/ARRAY/FLAG/NUM/LINE/RV/PN/CHILD、このプロジェクトがなぜ
// それを前へ運ぶことを繰り返し選ばないのかについてはCLAUDE.mdと
// 既に移植済みの各*Modeクラス自身のヘッダー参照)にまたがる1つの
// 絡み合った状態機械である――ここでの等価な状態は、単に
// currentMode_(1つのenum)、いくつかの小さなプロンプト/リーダー
// キーの記帳用フィールド、そして所有される各ContentView自身の
// 既にカプセル化されたカーソル状態だけである。特に:
//   - legacyの単一の共有PPバッファ(DirectoryMode自身のヘッダーが
//     未解決のギャップとして印を付けた、12スロットのhp->base[]
//     プール)は、ここでは、Directory、File、Grep、Cut、CutContent
//     それぞれに、全て同じDisplayを包む、それ自身のContentViewを
//     与えることで解決されている。モードの切り替えは単に「あるビュー
//     でredraw()を呼ぶのを止め、別のビューでそれを呼び始める」
//     だけ――現在表示されていないビューは、そのlines/カーソル状態
//     をメモリ内に正確に以前のまま保ち、保存も復元も不要である。
//     これが、File/Grep/Cut/CutContentから、以前アクティブだった
//     ものへ戻る際にModeStackのpushが全く不要な理由である
//     (handleCommand()のBackケース参照): 以前のモード自身の状態は
//     決して離れなかった。
//   - ここでは1つではなく「2つの」別々のModeStackインスタンスが
//     所有されている: modeStack_(DirectoryMode自身のディレクトリ
//     からディレクトリへの降下、その最初の一切れで移植された)と
//     cutModeStack_(CutMode自身のネストされた再分割の記帳)。
//     両方とも「単なるModeStack」であるにも関わらず、それらは共有
//     されて「いない」――このクラスの設計中に、それらの間で1つの
//     インスタンスを共有することが本物のバグになることが確認され
//     た: CutMode::canGoBack()/goBack()は「スタックが空でないか」/
//     「popされたフレームがcutBufferを持つか」だけをチェックする
//     ので、ユーザーがCUTの途中にいる時、たまたま「共有された」
//     スタックの上にディレクトリナビゲーションのフレームが座って
//     いた場合、goBack()はそのディレクトリフレームを黙ってpopして
//     捨てながらfalseを返してしまう(frame->cutBufferがnull)
//     ――完全に無関係な寄り道のためにDirectoryMode自身の履歴を
//     破損させてしまう。重複はあるものの、各クラスにそれ自身の
//     ModeStackインスタンスを与えることで、既に出荷済みのどちらの
//     クラスにも触れることなく、配線層でこの相互汚染を完全に回避
//     する。
//   - legacyの`^J`は本物の2バイトのリーダーキー(^Jw、^Jc、^Jl、
//     ^Jm、^J^S、^J^C、...)であり、Enter/^Mの同義語ではない
//     ――このクラスのまさに最初の一切れが間違えた微妙な点である
//     (以前の改訂は、便宜上0x0aをEnterのケースへ折り込んでいた。
//     EGREPの^J^S紐付けが衝突を具体化した時点で修正された)。
//     resolveCommand()は純粋な単一バイトの関数のままである。2回の
//     feedByte()呼び出しにまたがる状態を持つリーダーバイトの蓄積は
//     feedByte()自体にあり、このクラスの中で、どのモードがアクティ
//     ブかに加えてキー入力の間で何かを覚えておかなければならない
//     唯一の部分である。
//   - legacyの検索パターンのテキスト入力(np->M ==
//     M_fgrep/M_egrep/M_cut/M_cut2の時のread_m()のklen/プロンプト
//     機構)は、ここでは、小さな明示的なプロンプト状態
//     (promptKind_)と、所有されるLineEditorによって表現されて
//     いる――feedByte()は、プロンプトが有効な間は生のバイトを
//     resolveCommand()ではなくエディタへルーティングし、^Mで
//     送信、^Gでキャンセルする。get_m()自身のread_m()対ディス
//     パッチという分割を、read_m()の手作りのklen/cnameテーブルを
//     必要とせずに反映している(LineEditorが既にテキスト編集を
//     所有しており、Applicationは、入力がCommandディスパッチを
//     通る代わりにいつそこへ行くかを決めるだけ)。GREPとCUTは1つの
//     promptKind_/LineEditorのペアを共有し、どのPromptKindが保留
//     中かと、(CUTに対しては)プロンプトが始まった時currentMode_
//     がFile(新規の分割)だったかCutContent(セグメントの再分割)
//     だったかによって区別される――startCutPrompt()/executeCut()
//     参照。MANの名前プロンプトは同じ機構を使うが、そのような
//     モードごとの分岐を全く必要としない: GREP/CUTとは異なり、
//     現在のビューの内容を全く読まない(m_man()はm_man()内の
//     *lp->arg = K_zを辿ることで確認できるように、現在の選択から
//     事前に埋めるのではなく、プロンプトを空へクリアする――名前は
//     常に新しく入力される)ので、grepReturnMode_/cutReturnMode_が
//     追跡されるのと同じ方法で、アクティブだったモードのどれから
//     でも開始でき、そこへ戻ることができる。
//   - PROGRAM(M_prog/M_prog2、^J^P/^Jp)は、上の他の全てのプロンプト
//     駆動のアクションとは異なり、ApplicationModeを全く切り替え
//     「ない」: mode.cのM_progケース(入力されたコマンド行が送信
//     された時点で実行されるm_cr()相当のもの)を辿ると、それは単に
//     フォアグラウンドでコマンドを実行し、既に表示されていた画面へ
//     戻ることが分かる――GREP/CUT/MANがそれぞれ1つを作るような、
//     新しい永続的な画面は無い。これはまた、ProgramModeが(この
//     クラスが所有する他の全ての*Modeとは異なり、子プロセスの周りで
//     生のtermioを実際に離脱/再入場するために)生きたKeyInputを
//     必要とすることを意味する――そのためApplicationは、独自に、
//     常に既に解決済みのバイトを受け取るのではなく、KeyInput&の
//     コンストラクタ引数を取る。その参照が、Applicationが自分の
//     ために構築できる使い捨てのものではなく、実際に本物のttyを
//     制御している「同じ」KeyInputインスタンスでなければならない
//     理由についてはProgramMode自身のクラスコメント参照。legacy
//     自身のプログラム名の検証(find_prog(): 入力された行の最初の
//     単語は、何らかのPROGRAMディレクティブ自身の最初の単語との
//     strncmp接頭辞一致でなければならない)は、Configuration::
//     programSeedCommands()に対してexecuteProgram()内で複製されて
//     いる。
//   - LOCATE(M_locate/M_locate2、^J^L/^Jl)は、プロンプト駆動の
//     アクションの中で最も単純なもの: MANと同様、現在のビューから
//     何も必要とせず常に空のプロンプトを始めるが、GREPと同様、
//     自身の結果画面「内から」の再入場を拒否「する」
//     (m_locate()自身のis_name(ap->name, M_locate)ガード)――
//     そのため、GREP/CUT/MANと同じ方法で戻りモードを追跡するが、
//     4つの中で唯一両方の性質を同時に持つものである。この一切れが
//     引き継が「ない」m_locate()の1つの部分については「意図的に
//     除外」参照。
//   - アーカイブ閲覧(T_tar/T_lha/T_unzip/T_rpm/T_deb)とEXEC
//     (T_exec、suffixで対応付けられた外部ビューア――例えば設定
//     されたPDFリーダー)は、上の全てのアクションとは異なり、
//     どちらもプロンプト駆動では「ない」――両方とも
//     DirectoryMode::openSelected()が既に「選択がディレクトリか」
//     を自動検出しているのと同じ方法で自動検出される:
//     openSelectedEntry()は、まず選択されたパスに対して
//     Configuration::isExec()をチェックする(child.cのis_child()を
//     辿ると、suffix自身の`f.exec`フラグがアーカイブsuffixテーブル
//     より前にチェックされ、EXECに優先順位を与えていることが分か
//     る――ただし実際にはConfiguration自身のsuffixEntries_が既に
//     これを問題無くしている: それは、各エントリがアーカイブ扱い
//     かexec扱いのどちらかであり、両方ではない、1つの統合された
//     最長suffix優先のテーブルなので、ArchiveMode::loadArchive()は
//     チェックの順序に関わらず、自然にexec対応付けられたsuffixで
//     成功できない――isExec()を先にチェックすることは、単に無駄な
//     loadArchive()の試みを避けるだけである)。そうであれば
//     ExecMode::run()を実行し、変更せずにDirectoryモードへ戻る
//     (do_exec()自身の挙動: ファイル全体を外部プログラムへ引き
//     渡す、新しい画面は無い、ArchiveよりむしろPROGRAM/EDITの形に
//     合わせている)。exec対応付けられて「いない」場合にだけ、
//     openSelectedEntry()は次にArchiveMode::loadArchive()を試み、
//     それも失敗した場合にだけFileMode::loadFile()へフォールバック
//     する(loadArchive()自体は「設定済みのアーカイブsuffixではない」
//     と「一覧表示コマンドが失敗した」を同一に報告する。どちらも
//     単純にfalse――見過ごされるのではなく印を付けられた実際の
//     忠実度のギャップ: legacyはおそらく、一覧表示コマンドが失敗した
//     本物のアーカイブsuffixに対して特定のエラーを表面化させる
//     だろうが、これは代わりにプレーンテキスト閲覧へ落ちる)。
//     アーカイブは、Cutのものと同じ形の2番目の画面になる: Archive
//     (エントリ一覧、ArchiveMode自身のContentView)とArchiveContent
//     (1つの展開されたエントリのバイト、プレーンなテキスト行として
//     表示される――FileModeが受け取るようなPipeBufferの文字コード/
//     オーバーストライク処理は無い。ArchiveMode::extractSelected()
//     は常に生のバイトを返すだけであるため)。CutContentと同様、
//     ArchiveContentを開くのにModeStackのpushは不要である
//     (ArchiveModeもそれ自身のスタックを全く所有しない)。
//     ArchiveContentからのBackはArchiveへ、ArchiveからのBackは
//     Directoryへ戻る。両方とも単純なトグルである。EXECは、この
//     一切れではopenSelectedEntry()(Directoryモードのenterキー)
//     にだけ配線されており、Archive検出が既に描いたのと同じ境界
//     である――DU/LOCATEの結果エントリは、それら自身の種類の
//     suffixディスパッチ無しに、直接FileMode::loadFile()経由で
//     開かれ、この追加によって変更されない(上のそれら自身の項目
//     参照)。
//   - SHELL(M_shell、^[s――ESCの後に's')は、上の全てのものの中で
//     構造的に特異である: 全て「promptEditor_へ入力し、送信し、
//     Command駆動の結果画面を得る」であるGREP/CUT/MAN/PROGRAM/
//     LOCATEとは異なり、Shellは既にそれ自身の「独自の」LineEditor
//     (commandLine())とContentView(scrollback())を所有しており、
//     それらを本物のptyに対して直接駆動する――ここでのApplication
//     の仕事は、ApplicationMode::Shellがアクティブな間、それ自体の
//     編集/表示ロジックを再実装するのではなく、バイトをShell自身の
//     APIへ純粋にルーティングすることだけである
//     (feedShellByte())(理由についてはShell自身のクラスコメント
//     参照: このプロジェクトは、Shell自体が移植された時点で既に
//     legacyの3プロセス/8パイプIPC設計を解消している)。一度開始
//     されると、pty駆動の子プロセスは、Back/^Gをまたいで意図的に
//     バックグラウンドで実行され続ける――Shellから離れることは、
//     Applicationがどのcontentviewを再描画しているかだけを変える
//     (shell_.scrollback()は、他の全てのモードの画面のように
//     Application所有のContentViewへコピーされるのではなく、
//     activeView()のShellケースから直接返される)。本物のターミナル
//     マルチプレクサのペインが、フォーカスされていない間も実行され
//     続けるのと同じである。この一切れでは、Application自体が破棄
//     される時のShell自身のデストラクタを超えて、実際にshellを
//     終了させる方法は無い(まだM_exitは無い、下参照)。
//     ^[sはDisplayの6個の認識されるエスケープシーケンスのどれでも
//     ないので、(特にESC接頭辞の端末capabilityシーケンスを消費して
//     マッチさせるために存在する)KeyInput::readEscapeSequence()は、
//     元々、何もマッチしなかった時に末尾の's'バイトを呼び出し元へ
//     返す定義された方法を持たなかった(実際の、以前から文書化
//     されていたギャップ)。このクラス自身のESCリーダー処理(^J
//     リーダー機構を反映している)は、(0x1bの後's')生のバイトを
//     直接与えられた場合は常に正しく動作していた。それがここでの
//     テスト方法である。ギャップはKeyInput側にあり、KeyInput::
//     lastEscapeBytes()(自身のクラスコメント参照)を追加すること
//     で解決された――本物の実行ループ(app/main.cpp)は今や、
//     lastEscapeBytes()が捕捉したものが何であれ、実際のキー入力が
//     到着するのとまさに同じ方法でESC+それをfeedByte()経由で送る
//     ので、^[sは本物の端末でも動作する。
//   - CP/MV/RM(^Xc/^Xm/^Xr――今回は^J/ESCと並ぶ「3つ目の」リーダー
//     キー、^X)は、DirectoryModeのマーキング
//     (toggleMark()/targetEntries()、自身のヘッダー参照)と既に
//     出荷済みのCpMvRmModeをそのまま再利用するので、この一切れの
//     唯一の本当に新しいコードはプロンプトの配管である: CP/MVは
//     移動先パスを尋ね(PromptKind::CopyDestination/
//     MoveDestination)、RMはリテラルな「y」の確認を尋ねる
//     (PromptKind::RemoveConfirm)――m_dest()/m_yn()のはるかに
//     手の込んだエントリごとの検証(チルダ/二重スラッシュの整理、
//     エントリごとに折り込まれるMustBeDirectory/
//     MissingIsWritableの移動先チェック、同じディスパッチに混ぜ
//     込まれるARCメンバー)に対する、意図的に単純化された代役で
//     ある。^Space(M_mark)はカーソルでのマークを切り替える
//     ――KeyInput自身の文書化された慣習は、リテラルなNUL/
//     Ctrl-Spaceをバイト0x80として報告し(自身のクラスコメント
//     参照)、それをresolveCommand()がマッチさせる。成功時、
//     ディレクトリ一覧は(現在のディレクトリに対する
//     DirectoryMode::enterDirectory())再読み込みされ、画面が
//     cp/mv/rmが実際に行ったことを反映する。マークは、
//     DirectoryMode自身の文書化された挙動に合わせて、その再読み込み
//     の副作用としてクリアされるので、ここで別途clearMarks()
//     呼び出しは不要である。
//   - 終了(M_exit、^X^C)は、RMの確認と「同じ」プロンプト機構を
//     再利用する: m_cr()のM_exitケースを辿ると、確認して終了する
//     ロジックが本当にこれだけ単純であることが分かる――
//     `if (*rvp->str == 'y') return -T_exit;`――m_exit()自体
//     (紛らわしいが、上のM_exitケースと同じものではなく関数)の
//     他の全ては、この移植のモードごとのContentViewがそもそも
//     一度も必要としなかった、legacyの共有バッファプールアーキ
//     テクチャのためのスタック巻き戻しの記帳である(上の他の全ての
//     「逐語的な移植ではない」という注記と同じ理由)。RMとは異なり、
//     StartExitはどのモードからでも到達可能(targetEntries()が空
//     というガードは無い)であり、成功時にファイルシステムに触れる
//     ことも何かを再読み込みすることも無い――将来の本物の実行
//     ループが気付いて止まるためのフラグ(wantsExit())を設定する
//     だけである。Applicationには依然としてそれ自身の実行ループが
//     無い(下参照)。この一切れは、単にそこへ抜け出す先を与える
//     だけである。
//   - DU(M_du/M_du2、^Jd/^J^D)は、GREP/CUT/MAN/LOCATEと同様プロン
//     プト駆動だが、新しく入力することを要求するのではなく、現在の
//     ディレクトリでプロンプトを「事前に埋める」(m_cr()の
//     `case T_du: strcpy(rvp->str, ap->du);`を辿る――空で始まる
//     上の全てのプロンプトとは異なる)。ユーザーは単に^Mを押して
//     デフォルトを受け入れるか、先に編集できる。Directory限定の
//     入場(m_du()自体は現在のモードでゲートしないが、この一切れ
//     はCP/MV/RMが行ったのと同じ方法でスコープを狭める)。他の全て
//     の結果画面とは異なり、DUのエントリでのEnterは実際に降りて
//     いく――エントリがディレクトリならDirectoryModeへ、ファイル
//     ならFileModeへ――DuMode自身のクラスコメントが明示的にその
//     引き渡しをMode/Applicationに任せているため
//     (「DUのエントリを開くことがDirectoryModeとFileModeのどちら
//     に着地するかの決定」)。^Jd/^J^Dの両方ともここでは同一に扱わ
//     れる(常に新しい`du`)。M_du2自身の「`du`を再実行せず既存の
//     DUバッファを再利用する」ショートカットは、LOCATEの類似の
//     ^Jlショートカットが延期されたのと同じ方法で延期されている。
//   - 履歴/リストブラウザ(m_list()、^R=M_hist/^J^R=M_cmd/
//     ^I=M_list)は、この移植全体の中で最も大きく単純化された
//     アクションであることが判明した: m_list()を辿ると、キーが
//     押された時どのプロンプト文脈がアクティブだったかに応じて、
//     9個の異なるHistory/Configurationに裏付けられたカテゴリ
//     (T_hfile/T_hdir/T_hman/T_hlocate/T_hfgrep/T_hegrep/T_list/
//     T_cmd/T_hist)から選んでいることが分かる――そうでなければ
//     この移植全体のApplicationレベルの設計が避けている文脈追跡
//     である。この一切れは、最も曖昧でなく、最も直接的に有用な
//     2つのケースだけを配線する。両方ともDirectoryモード限定:
//     ^RはHistory::RingCategory::RecentDirectoryを閲覧する(最新
//     優先)。^J^Rは::ShellCommandを閲覧する。ListBrowserMode
//     (既に薄い、汎用的な「文字列を表示し、選択を追跡する」クラス、
//     自身のヘッダー参照)は全く変更を必要としなかった――
//     Applicationは単に、recentEntries()を直接呼ぶために、それ
//     自身の`History &history_`参照(新規。Historyを必要とする
//     他の全ての*Modeは、代わりにそれ自身のコンストラクタ経由で
//     それを得る)を必要としただけである。この一覧を埋めることは、
//     どの単一の*Modeの仕事でもないため。RecentDirectoryエントリ
//     でのEnterはDirectoryModeへ降りていく(DUのディレクトリ
//     エントリと同じ形)。ShellCommandエントリでのEnterは、この
//     一切れでは何もしない(それでShellのコマンド行を事前に埋める
//     という明白な次のステップは、Shellが既に開始されていることを
//     必要とし、それが本物の状態管理上の疑問――開始されていない
//     場合はどうなるか――を加える。推測するのではなく意図的に
//     未解決のままにされている)。
//   - EDIT(M_edit、^Je――src/usilicrc自身のKEYテーブルに対して
//     確認済み: "F-1 e ... EDIT ... K-66"、^Jd/^Jl/^Jp/^Jm等が
//     既に使っているのと同じF-1は^Jリーダーを意味するという表記)
//     は、PROGRAMと同様、プロンプト駆動ではなく、ApplicationMode
//     を全く切り替えない――menu.cのM_editケースを辿ると、それは
//     既に画面上にあるものに対して即座に実行される
//     (m_edit()のdo_edit()呼び出し)ことが分かる。テキスト入力は
//     関与しない。この一切れでは正確に2つの文脈に絞られている:
//     Directory(openSelectedEntry()が既に使っているのと同じ
//     currentDirectory()+"/"+entry->nameというパス構築を使って、
//     ハイライトされたエントリを直接編集する。それがディレクトリ
//     なら拒否する――これを全くチェックせず、単にディレクトリ
//     パスをエディタへ渡してしまうdo_edit()に対する明示的な改善)
//     とFile(カーソル行で現在表示されているファイルを編集する)。
//     両方とも、(子エディタの周りで本物の生のtermioを離脱/再入場
//     するという)同じ理由で、PROGRAMが既に必要としているのと
//     「同じ」KeyInput&を使ってEditMode::edit()を呼ぶ。do_edit()
//     自身の行番号引数(`pnp->line`)は、より詳しく読むと、FILE
//     モードから呼ばれた時はファイル自身の行ごとの行番号フィール
//     ド(`rvp->pp->f.line`)だが、ディレクトリ一覧の文脈から呼ば
//     れた時は単にディレクトリ一覧「内の」カーソルの行であること
//     が分かる――つまりlegacyは、開かれている完全に別のファイル
//     内の行番号であるかのように、ディレクトリ一覧の行番号を渡し
//     ており、それは何も意味しない。この一切れは意図的にそれを
//     引き継が「ない」: Directoryモードの編集は常に1行目で開く。
//     Fileモードだけが、本物の行番号としてContentView::
//     cursorIndex()+1を使う。EditMode::edit()自身のbool戻り値
//     (ファイルの内容が実際に変わったか)は、その後FileModeの
//     表示されている内容を再読み込みするかどうかを決めるために
//     使われる(do_edit()自身のst_ctimeチェック、代わりに
//     last_write_time()を使って移植された――理由についてはEditMode
//     自身のクラスコメント参照)。Directoryモードの編集は、決して
//     再`ls`を引き起こさない(do_edit()自体も一覧表示バッファには
//     触れず、「次の」閲覧スロットだけに触れる)。
//   - HELP(M_help/T_help、^J^H――src/usilicrc自身のKEYテーブルに
//     対して確認済み: "F-1 C-h ... HELP ... K-1")は、menu.cの
//     init_bp()のM_help/T_helpに対するデフォルトケースの処理を
//     辿ると、そもそもそれ自身の表示機構では全く「ない」ことが
//     判明した: legacyは、固定の節見出し正規表現
//     ("^  [1-6][1-6]+\\.")付きで、ヘルプテキストの「パス」を
//     init_cut2()へそのまま渡し、np->M = T_cutを設定する――つまり
//     HELPは本当に「CUTの分割」(選ぶべき番号付き節の表)であり、
//     単にタイプされたものの代わりに固定のソースとパターンを持つ
//     だけである。これが意図的であり、迂回するための実装上の
//     ショートカットではないことをユーザーと直接確認済み: HELPが
//     CUT自体の生きた、手を動かしたデモンストレーションを兼ねる
//     ことは要点の一部である。そのためHELPは、新しいCutMode::
//     pushSplit()(splitSelected()自身のpushしてから分割するという
//     形から括り出された、自身のコメント参照)経由で、実際の
//     ユーザーのCUTセッションが使うのと「同じ」CutModeインスタンス
//     を再利用する――本物のCUTセッションがたまたま既にアクティブ
//     な場合(currentMode_が、本物の以前のsplit()を伴うCut/
//     CutContent)、pushSplit()はそれを、ネストされたsplitSelected()
//     の再分割がそうするのとまさに同じ方法でCutMode自身の
//     ModeStackに保存するので、HELPからの^Gは、Backが既にCutに
//     対してまずチェックするのと「同じ」canGoBack()/goBack()経路
//     経由で自然にそれを再開する。本物のCUTがアクティブでなかった
//     場合、startHelp()は代わりに直接split()を呼び(保存する価値の
//     あるものは無い)、cutReturnMode_をユーザーがいた場所に設定
//     する。本物のトップレベルのCUTが既に使っている同じ「Cutから
//     戻る先...」のアンカーである――HELPはどのモードからでも
//     到達可能で、legacy自身のinit_bp()のデフォルトケースが
//     M_help/T_helpに対してモード文脈のガードを持たないことに
//     合わせている。「これは本物のCUT」と「これはHELP」の区別は、
//     2つのプレーンなsplit()呼び出し箇所(startCutPrompt()、
//     startHelp()の保存しない分岐)でだけ更新される1つの新しい
//     フィールド(cutOrigin_、FileまたはHelp)で追跡される――
//     pushSplit()の呼び出し箇所はそれに触れない。pushされた階層は
//     構築上ネストされており、起源に関わらず既に下のリサイズの
//     再折り返しから除外されているため。
//   - HELP自身のテキストはloadHelpText(text)経由で読み込まれる
//     (Configuration::loadRcText()/History::loadHistoryText()の
//     「呼び出し元がファイルI/Oを行い、このクラスは単にテキストを
//     受け取る」というルールを反映している――Applicationは依然と
//     して自力での隠れたファイル読み込みを一切行わない)。そして
//     FileMode/ManMode自身のreadAndWrap()/runAndWrap()が使うのと
//     まさに同じCharset::Encoding/PipeBufferパイプライン
//     (wrapHelpText()、新しいプライベートヘルパー)を通して再度
//     折り返される――これが、このクラスが今`encoding_`を直接保存
//     する理由でもある(これより前は他のどのメンバーもそれを必要と
//     しなかった)。src/doc/HELP対HELP.jp(英語対、単なる再符号化
//     ではない本物の日本語訳――両方を読んで確認済み: HELP.jpの
//     生のバイトは実際のEUC-JPの日本語テキストである)は、
//     usilicrc/historyが既に描いている「呼び出し元が実際のパスを
//     解決する」という境界と同じで、main.cppが行う選択である――
//     Applicationは、渡されたテキストが何であれ、常にそれだけを
//     見る。
//   - 記録されたマクロ(p_km、^X(/^X)/^Xe――src/usilicrc自身の
//     「Begin/End/Execute macro definition」KEYエントリ参照)は、
//     本物の2つの異なるlegacyマクロ機構のうち「1つ」である。
//     もう1つ(p_bm、usilicrcのプリセットM-0..M-19マクロ、F-?/
//     K-?/M-?という、あらゆる物理キーをあらゆる内部コマンドへ
//     再紐付けできる、ユーザーが設定可能な完全なキー再割り当て
//     ディレクティブフォーマット経由で定義される)は、この移植が
//     支援するインフラを全く持たない、はるかに大きな、別の機能
//     であり(Applicationのコマンドディスパッチは依然として固定の、
//     ハードコードされたテーブルである)、ここでは意図的に試みら
//     れて「いない」――黙って落とすのではなく、ユーザーと明示的に
//     確認済み、CLAUDE.md参照。key.cのset_km()を辿ると、p_kmは、
//     呼び出しごとにread_m()自体が「返す」もの(通常のディスパッチ
//     中に解決されたM_*/T_*コマンド番号、またはテキストプロンプト
//     が有効な間のリテラルにタイプされた文字――read_m()は、それ
//     自身の現在のモードに応じて、両方を1つの戻り値チャネルへ統一
//     する)を記録することが分かる――解釈される前の生のキーボード
//     バイトでは「ない」。この移植の最も近い等価な単位は
//     「feedByte()に何のバイトが供給されたか」であり、read_m()
//     自身の戻り値より1層前だが、効果としては等価である: 同じ
//     バイトを再びfeedByte()経由で再生すると、決定的に同じリー
//     ダーの蓄積/プロンプトルーティング/ディスパッチの順序を再現
//     するので、バイト層での記録は、legacy自身の単一のint配列
//     a_km[]がread_m()の二重の戻り値の意味に黙って頼って取り
//     繕っている「このバイトは解決済みのコマンドだったのか
//     プロンプトのテキストだったのか」という別の処理を必要と
//     しない。これが必要とする唯一の注意点: 記録を開始/停止/再生
//     する^X( / ^X) / ^Xeのシーケンス自体は、記録「の中に」入って
//     はならない(legacy自身のset_km()も、M_ms/M_meをa_km[]へ
//     追加しない)――feedByte()は、leaderXシーケンスの先頭の^X
//     バイトを、2番目のバイトが解決するまでpendingLeaderXByte_に
//     保持しておき、「その後」解決自体がStartMacroRecord/
//     StopMacroRecord/PlayMacroでなかった場合にだけ両方のバイトを
//     一緒に確定する(^J/ESCリーダーシーケンスにはそのような保留
//     は不要である: マクロ制御は排他的に^Xの下に住むので、全ての
//     ^J/ESCバイトは、そのシーケンスがどう解決されるかを知らずに
//     即座に確定できる。2026-09-08、直接のユーザー依頼によるキー
//     再編で、この保留の対象自体が一時的に^Oへ付け替わったが、
//     2026-09-10、直接のユーザー依頼(既存の実装がいつの間にか
//     ^Oへ移っていたことに気づき、"^X(/^X)/^Xeへ戻す(^Oは廃止)"と
//     明示的に選択)により^Xへ差し戻された――
//     pendingLeaderXByte_自身のコメント参照)。playRecordedMacro()は、
//     記録された各
//     バイトを、feedByte()自体ではなく、プライベートな
//     dispatchByte()(feedByte()自身の以前の本体、今は名前を変更
//     されラップされている)経由で再生する。そのため、別の記録
//     セッション自身のバイトの内側からトリガーされたマクロが、
//     誤って自己記録されることは決して無い――set_km()自身の
//     M_mmの「無限ループ」の自己追加の癖(マクロが自分自身を呼ぶ
//     自動繰り返し機能、legacyのコメント自体がそれを普通ではない
//     と印を付けている)は意図的に複製されていない。この移植は
//     それをサポートしない。
//   - GUIモード(docs/ideas/IDEAS.mdの「猫の手」隠しモード、legacy
//     のusilicには全く存在しない――完全に新しい機能要望)は、ここには
//     正確に2つの表面を持ち、両方とも意図的にSDL2/GraphicalDisplay
//     の詳細については無知のままにされている――その具体的な
//     バックエンドという問題全体は、本物の実行ループ(app/main.cpp)
//     に属する。このクラスが既にTerminalDisplayの周りに描いている
//     のと同じ境界:
//       1. ^Og(新しいリーダー、^O――legacy自身のKEYテーブルの
//          F-3、この移植ではこれまで未使用)はGUIモードを切り替
//          える。ToggleGuiModeはcurrentMode_/どの*Modeにも全く
//          触れない。単に実行ループが
//          consumeGuiModeToggleRequest()経由でポーリングするフラグ
//          (guiModeToggleRequested_)を上げるだけである
//          (wantsExit()と同じ「直接のコールバックではなくフラグ」
//          という形)。そして実行ループは、「切り替え」が実際に何を
//          意味するか(どの具体的なDisplayバックエンドがアクティブ
//          か、GraphicalDisplay/ウィンドウが既に存在するか、実行
//          ループ自身の入力ソースを本物のttyバイトと変換された
//          SDLイベントの間で入れ替える)を完全に自力で決める。
//          暫定的な紐付けで、実際に試した後に見直すプレースホル
//          ダーとしてユーザーと確認済み(IDEAS.md自体は本物のキー
//          を覚えていなかった)。
//       2. handleMouseClick(row, column)――IDEAS.md自身の説明
//          ("click the already-highlighted file to view it")を、
//          通常のファイルマネージャの慣習と合わせて読む: 「別の」
//          行へのクリックは単にそれを選択する(そこへカーソルを
//          動かす、キーボードのUp/Downに合わせている)。「既に」
//          選択されている行へのクリックはそれを開く(Enterと同じ)。
//          最初はDirectoryモード限定で、IDEAS.md自身の例のシナリオ
//          (lsのファイルセレクタ画面)に合わせている。LOCATE結果は
//          2026-08-14にそれに加わった(直接のユーザー依頼、
//          handleMouseClick()自身のコメント参照)――File/Man/他の
//          内容ビューも合理的にクリックしてそこへスクロールする
//          ことを望むかもしれないが、まだ延期されている。行/列は
//          「表示」座標である(ContentView::lineIndexAtRow()自身の
//          座標空間、moveCursor()が使うのと同じもの)――実際の
//          ピクセルクリックを行/列のセルへ変換することは
//          GraphicalDisplayの仕事であり、Applicationのものでは
//          ない。右クリック(IDEAS.mdによればPROGRAM一覧のコンテキ
//          ストメニュー)と慣性の効く「猫の手」ドラッグの物理演算
//          自体は、両方ともこの最初の一切れに対して明示的にスコープ
//          外である(ユーザーと確認済み)――これは単なるクリックして
//          選択/開くだけを配線するので、その上により手の込んだ
//          部分を構築する前に、描画/操作の「感触」を評価できる。
//   - handleResize()(端末のリサイズ、legacyでのT_winch)は、
//     KeyInput/Commandディスパッチを全く経由せずにApplicationが
//     手を伸ばす唯一の状態である――menu.cのT_winchケースを辿ると、
//     legacyは、画面に「何が」あるかに応じて、リサイズを完全に
//     異なる方法で扱うことが分かる: ディレクトリ/リストバッファに
//     裏付けられたモード(T_dir、is_list()/is_str())は、単に次の
//     再描画時に完全な再一覧/再取得を強制するためにバッファを解放
//     するだけである(それらの内容はそもそも幅で折り返された段落
//     テキストではない――ls/tar/grep/locate/du/historyのエントリ
//     ごとに1行なので、再折り返しすべきものが何も無い)。アーカイブ
//     の一覧も同様にalloc_head()経由で再生成される。そして他の
//     全て(自由なテキスト閲覧のデフォルトケース)はalloc_winch()を
//     通る――現在読み込まれているPPリストから生のバイトを再構築し、
//     古い(幅が間違った)行折り返し済みバッファを捨て、初期読み
//     込み時に使われたのとまさに同じ折り返し関数を、「新しい」幅に
//     対して再実行する。この一切れは、その同じ3方向の分割を移植
//     しており、別々に渡されたパラメータを信頼するのではなく、
//     リサイズ後のrows()/columns()を読み戻すためにDisplay&
//     (display_、新しいメンバー――これより前は他のどの*Modeクラス
//     もDisplayへの直接アクセスを必要としなかった)を使う: 「全ての」
//     所有されるContentViewのビューポートがリサイズされる
//     (T_dir/is_list/アーカイブの「内容の再折り返しは不要、単に
//     幾何形状を更新するだけ」という扱いに合わせている――Directory/
//     Grep/Locate/Archive/Du/ListBrowserに対して正しい。それらの
//     内容はどれも段落のソースから幅で折り返されていないため)。
//     一方FileとManモード(実際にPipeBufferの幅折り返しの上に構築
//     されている2つのモード、legacyのalloc_winch()デフォルト
//     ケースに合わせている)は、プレーンなビューポートのリサイズ
//     に「加えて」ではなく「代わりに」FileMode::reflow()/
//     ManMode::reflow()が呼ばれる。reflow()は既にそれらの
//     ContentViewの行を完全に置き換えるため。どちらも逐語的な
//     alloc_winch()の移植ではない理由(この移植には存在しない
//     インメモリの生バイトキャッシュから再折り返しするのではなく
//     ファイルを再読み込み/`man`を再実行すること、そしてlegacyの
//     行インデックスベースのset_rv()の再計算ではなく制限による
//     「現在の」セッションのカーソルの保存)についてはそれら自身の
//     クラスコメント参照。
//     Cut/CutContentは、同じ「プレーンなリサイズの代わりに
//     reflow」という扱いを受けるが、それはcanGoBack()==falseの
//     場合「だけ」である(本物のトップレベルの分割であり、ネスト
//     された再分割ではない――ネストされた場合については下の
//     「意図的に除外」の項目参照): 再分割すべき新しいソース行は、
//     cutOrigin_==Fileの時はfileMode_.reflow()+fileView_.lines()
//     から、cutOrigin_==Helpの時はwrapHelpText()から来る。その後
//     cutMode_.split()が、それ自身の既に記憶されている
//     pattern()/cutSyntax_で再実行される――選択されたセグメントの
//     「インデックス」は(制限されて)保持され、CutContentに特有に、
//     openSelectedSegment()がその後再度呼ばれ、(望むべくは依然
//     として同じ)そのセグメントの新しく再折り返しされた内容で
//     cutContentView_を再度埋める。
//     Shellのptyは、currentMode_に関わらず無条件にリサイズされる
//     (Shell::onResize()、既に完全に構築済み――この一切れの唯一の
//     Shell関連の変更は実際にそれを呼ぶことである): 本物の端末の
//     リサイズは、たまたま可視のモードだけではなく、全モードが
//     共有する1つの物理的な端末の性質である。本物の端末マルチプ
//     レクサがバックグラウンドのペインもリサイズするのと同じで
//     ある。
//
// 意図的に除外(将来の課題、この一切れではない):
//   - m_list()の9個のカテゴリのうち残りの7個(T_hfile/T_hdir/
//     T_hman/T_hlocate/T_hfgrep/T_hegrep/T_list――Configurationの
//     シードに裏付けられたもの、Historyのリングではなく
//     GREP/PROGRAMパターン)と、M_list自身の素の^I/Tabの紐付け
//     (legacyでは文脈依存であり、ここではShellモードでTabは既に
//     別のことを意味する――feedShellByte()参照): それぞれが上の
//     ^R/^J^Rと同じ形の追加(カテゴリを選び、それを表示し、Enterが
//     何をするか決める)であり、この一切れをレビュー可能に保つため
//     延期されている。これらのリングにエントリを記録すること自体
//     (例えば全てのDirectoryMode::enterDirectory()呼び出しが
//     History::pushRecent()経由でRingCategory::RecentDirectoryへ
//     供給する)も、まだ配線されていない――History自体はそれを
//     完全に行える(pushRecent()、既にHistory自身のテストで行使
//     されている)が、この一切れはApplicationからそれを呼ばない
//     ので、何か他のものがそれを埋めるまでrecentEntries()は空の
//     ままになる(このセッション自身のテストは、閲覧側を独立に
//     行使するためpushRecent()を直接呼ぶ)。
//   - エラー/ログ画面(M_error/M_log): ヘルプ(下参照)とは異なり、
//     これらはそれ自身の専用の*Modeクラスをまだ全く持たない。
//     それらを配線することは、モードごとに繰り返される同じ形の
//     変更(Command/ApplicationModeを拡張し、feedByte()のリーダー
//     テーブルを拡張し、handleCommand()を拡張する)であり、各
//     一切れをレビュー可能に保つため延期されている。
//   - DuMode自身のchdir()を使わない設計は、測定中に実際にプロセス
//     の作業ディレクトリを変更するlegacyの挙動が複製されていない
//     ことを意味する(DuMode自身のクラスコメント参照)。ディレク
//     トリのDUエントリでのEnterは、代わりに絶対パス付きの
//     DirectoryMode::enterDirectory()を使う。この移植の恒久的な
//     「隠れたグローバル状態は無し」というルールと一貫している。
//   - CP/MV/RMに対する^X^Kc/^X^Km/^X^Kr同義語(get_m()が同じ3つの
//     アクションに対して受け入れる、2つ目の、より長いリーダー
//     シーケンス。効果が追跡されなかった追加の`fp->comm`フラグを
//     設定する)と^X^Ka/^Xa(M_arc/M_arc2、アーカイブ「作成」
//     アクション――下参照、アーカイブ閲覧とは異なる「ARC」):
//     この一切れでは^Xc/^Xm/^Xrだけが配線される。
//   - CpMvRmMode自身の既に文書化された除外(OSレベルでの対話的な
//     `-i`確認、アーカイブ内または`du`で測定されたサブディレクトリ
//     内に住むエントリ): この一切れによって変更されておらず、
//     それ自身のヘッダーが与える同じ理由で依然としてスコープ外
//     である。
//   - mode.cのM_progケースは、最初の単語が認識されるPROGRAM一覧
//     エントリで「ない」場合、入力された行が事前に埋められた状態
//     でSHELLモードの起動へフォールバックする(`m = -M_shell`)
//     ――executeProgram()は、SHELL自体が今は配線されているにも
//     関わらず、依然としてその引き渡しの代わりに単に失敗を報告
//     する。2つを繋ぐことはそれ自身の小さな将来の変更であり
//     (SHELL自身のコマンド行が入力されたテキストで事前に埋められ
//     る必要がある)、どちらの一切れにもまとめられていない。
//   - この一切れが未設定のままにするShell自身のさらなる接続:
//     setHistoryRecorder()/setDirectoryChangedCallback()/
//     setProgramLauncher()/setCompletionProvider()――全てオプ
//     ションで、全て延期されている(History統合、cwdがshellに追従
//     する挙動、SHELL内からのPROGRAM引き渡し、TAB補完候補は、
//     それぞれが独自の追加課題であり、動作するSHELLモードには
//     必要ない)。Shellモードにいる間の矢印Up/DownまたはPageUp/
//     PageDown経由のスクロールバック/コマンド行のページングも
//     同様に配線されていない(feedShellByte()はSpecialKeysの中
//     ではLeft/Rightのカーソル移動だけを扱う)。
//   - do_prog()の実行後の陳腐化チェック(閲覧中のファイルを再度
//     statする。プログラムの実行中にそれが変わっていれば強制的に
//     再読み込みする): これもここでは複製されていない――黙って
//     見過ごされるのではなく印を付けられた実際のギャップであり、
//     この一切れの最小限の「単に実行する」という挙動がそこまで
//     育つ必要が出るまで延期されている。
//   - LOCATE自身の^Jlショートカット(m_locate()のM_locate2ケース):
//     以前のlocate結果バッファが既に存在する場合、^Jlは、新しい
//     クエリを尋ねる代わりに、それを(プロンプトを完全にスキップ
//     して)直接再表示する――^J^Lはいずれにせよ常に新しく尋ねる。
//     この一切れは両方の紐付けを同一に扱う(常に新しいプロンプト)。
//     「検索を再実行せずに最後の結果セットを覚えて再表示する」
//     ショートカットは延期される。選択された結果に対して何をする
//     か(DirectoryModeまたはFileModeへの引き渡し)を決めることも、
//     LocateMode自身のクラスコメントに合わせて同様にスコープ外
//     である。この一切れではlocate結果でのEnterは何もしない
//     (GREPと同様)。
//   - GREP自身のさらなるアクション: マッチした行の元の文脈を開く/
//     ジャンプする、ネストされた検索(結果内での検索)、マッチした
//     部分文字列のハイライト、そしてパターンをHistoryの
//     hfgrep/hegrepリングへ記録すること――全て、GrepMode自身の
//     スコープの明示的な外側でもある(クラスコメント参照)。この
//     一切れではgrep結果でのEnterは何もしない。
//   - CUT自身の、ディレクトリ一覧から「まずファイルビューを開かず
//     に分割する」^Jb/^J^Bショートカットと、LOGモードの組み込みの
//     プロンプトマーカー分割: 両方とも、CutMode自身のスコープの
//     明示的な外側でもある(クラスコメント参照)――この一切れでは、
//     CUTはFileモード(新規の分割)またはCutContent(ネストされた
//     再分割)からだけ開始できる。切り出しパターンをHistoryへ記録
//     することも同様にスコープ外、GrepModeが既に描いているのと
//     同じ境界。
//   - m_exit()のスーパーユーザー安全チェック(`is_inter(NULL, -1,
//     0)`、su途中またはそれに類する状況での終了を防ぐ): 複製され
//     ていない、これについてのクラスコメント自身の注記参照。
//     StartExitは無条件に到達可能である。
//   - 本物の端末実行ループ(legacyのmenu()): 生きたKeyInputから
//     バイトを読み、エスケープシーケンスを翻訳し、終了までループ
//     する――今は抜け出す先(wantsExit())があるが、依然としてここ
//     では構築されていない。feedByte()/handleCommand()は、その
//     ようなループが呼ぶであろう、テスト済みの、端末に依存しない
//     基本要素である。
//   - ARC作成(^X^Ka/^Xa、M_arc/M_arc2、m_arc()): 上のアーカイブ
//     閲覧のものとは「異なる」、無関係な「ARC」――ここでのmenu.cの
//     m_arc()は、T_tar等のアーカイブ閲覧/表示ではなく、CP/MV/RMと
//     一緒にグループ化されたアーカイブ「作成」アクションである。
//     CP/MV/RMとは異なり、これにはまだ移植されたクラスが全く無い
//     ので、リーダーキーのスコープに関わらず配線されていない。
//   - ファイルジャンプ(M_jump、^X^X――カーソル位置と最後に
//     ^Spaceでマークされた位置の間を入れ替える): この一切れが
//     マップするコマンドの一部ではない。
//   - p_bm(usilicrcのプリセットM-0..M-19マクロ、そしてそれらが
//     依存する、F-?/K-?/M-?というユーザーが設定可能なキー再割り
//     当てディレクティブフォーマット全体): p_km(上の自身の節参照)
//     とは本当に別の、はるかに大きな機能である――この移植の
//     Commandディスパッチには、それを構築すべき設定可能なキー
//     紐付けのインフラがまだ全く無いことを踏まえ、p_kmと一緒に
//     試みるのではなく将来の一切れへ延期することをユーザーと明示
//     的に確認済み。
//   - ネストされたCut/CutContentの再分割は、リサイズ時に再折り
//     返し「されない」(このCut/CutContent全体を除外していた、
//     この項目の以前の、より広いバージョンから改訂された――理由に
//     ついてはCLAUDE.mdの履歴参照): 「トップレベルの」
//     Cut/CutContent(canGoBack()==false)は、実際のFileモード
//     起源のCUTとHELPの両方に対して、今はリサイズ時に再折り返し
//     «する»――「ネストされた」再分割(splitSelected()、
//     canGoBack()==true)だけが依然としてそうしない。ネストされた
//     階層を正しく再折り返しするには、現在の階層自身のsplit()を
//     単に再実行するだけでなく、本当のルートまでModeStackの連鎖
//     全体を遡って歩き、全ての中間の分割をゼロから再導出する必要
//     があるため――CutMode自身のクラスコメントは、既に「
//     splitSelected()の時点で親の分割がどう見えたか」を、各ネスト
//     階層での1回きりのスナップショットとして扱っており、この移植
//     はまだそれを更新するためにその連鎖を歩かない。ネストされた
//     再分割のContentViewのビューポートは依然としてリサイズされる。
//     その行の内容は、それ自身のsplit()/splitSelected()が最後に
//     実行された時にアクティブだった幅で折り返されたままである。
//   - Directory/File以外のどのモードからのEDITも(Grep/Cut/
//     CutContent/Man/Locate/Archive/ArchiveContent/Du/ListBrowser/
//     Shell): legacy自身の拒否リストは、アーカイブ/切り出し/分割の
//     閲覧文脈(T_tar/T_lha/T_unzip/T_rpm/T_deb/T_buff/T_cut/
//     T_split)だけを完全にブロックし、加えて合成された非ファイル
//     エントリ('('を含むGREP結果の「arg」、空/空白で始まる名前)に
//     対する内容ベースのガードを持つ――この一切れはさらに狭め、
//     正確にDirectory/Fileだけにし、より細かいlegacyのガードロジ
//     ック(そして例えばGREPマッチやLOCATE結果の背後のソース
//     ファイルを編集することが、どちらのモードも現在公開している
//     以上のパス解決の仕組み無しに意味を成すかどうか)を将来の
//     一切れへ延期している。
// 2026-09-14追加、直接のユーザー依頼("README改めTUTORIALとして...
// usilic内での発動は^Tで行う"): TUTORIAL自身の見出し(全角スペース+
// 章番号+ピリオド、1〜複数階層)にマッチするCUT用の正規表現パターン
// ――openRecentFileEntry()の合成エントリ分岐がfileMode_.loadFile()
// (オーバーストライク検出ON)経由で組み立てる、太字が既に1文字へ
// 収縮済みの「見た目通りの」文字列に対して一致する(application.cpp
// 自身の定義側コメント参照、\x08を含む生バイト版では一致しないと
// 判明した経緯も含む)。app/main.cpp(真っ新起動時、FILE履歴への
// 事前登録)とapplication.cpp(^T画面自身の「TUTORIAL」行、
// Application::openSelectedBannerEntry())の両方が、この同じバイト列を
// それぞれ独立に組み立てると、片方だけ直して他方を直し忘れる事故が
// 起きやすい――両者が正典として1箇所(application.cpp)だけで定義した
// この関数を呼ぶことで、その危険を排除する。

// parseCutFileHistoryEntry()/buildCutFileHistoryEntry()自身の定義側
// コメント参照(application.cpp): FILE履歴の合成CUTエントリ
// (「パス "パターン"」、QUICK.txt自身のC-j bの説明が定める表示形式)
// の双方向変換。tutorialCutPattern()と同じ理由(定義箇所を1つに
// 揃え、複数箇所での独自組み立てによる食い違い事故を防ぐ)で
// ここに宣言し、main.cpp(真っ新起動時の事前登録)からも呼べるように
// する。
std::optional<std::tuple<std::string, std::string, GrepSyntax>> parseCutFileHistoryEntry(
    const std::string &entry);
std::string buildCutFileHistoryEntry(const std::string &path, const std::string &pattern, GrepSyntax syntax);

class Application {
 public:
  // `keyInput`は、実際に本物のttyのraw/cookedモードを制御している
  // 同じインスタンスでなければならない(クラスコメント参照)――
  // Applicationは常にそれをProgramModeへ転送するだけで、自身では
  // 決してそれから読み取らない。`shellOptions`はShellの
  // argv/promptMark/programName/scrollbackBudgetの種になる。
  // rows/columns/encodingは、渡されたものが何であれ`display`/
  // `encoding`から上書きされるので、Shellセッションは、それらを
  // 2回指定する必要無く、常にこのApplication自身の幾何形状と
  // 文字コードに一致する。
  // homeDirectory: 所有されるPathResolver(K-45/K-47の^Xf/^Xdパス
  // プロンプト、StartFileInput自身のコメント参照)へ転送される
  // ――ここには隠れたgetenv("HOME")呼び出しは無く、PathResolver
  // 自身の(Configuration/Shell等に対して既に確立されている)
  // 「隠れたグローバル状態は無し」というルールに合わせている。
  // 呼び出し元(main())が実際の環境を読む。また、usilicrcPath_/
  // historyFilePath_/(そして、下の明示的なtempRootDirectoryが
  // 無ければ)error/shell/work/arcのフォールバックのルートでもある。
  // 全て`<homeDirectory>/.usilic`の下。
  // tempRootDirectory(2026-08-18の追加、tempRootDirectory_自身の
  // コメント参照): error/shellを特に何を根としてルートするか。
  // デフォルトは""(`<homeDirectory>/.usilic`へフォールバックする、
  // 2026-08-18より前のこのクラスの唯一の挙動)なので、複数インス
  // タンスの分離を気にしない既存の呼び出し元――全てのテスト
  // フィクスチャ――は更新の必要が無い。
  // workRootDirectory(2026-08-18の追加依頼、workRootDirectory_
  // 自身のコメント参照): work/arcを特に何を根としてルートするか
  // ――上のtempRootDirectoryとは「別」(直接のユーザー依頼: 起動
  // モードに関わらず、WORKは常にプロセス専用の
  // `/tmp/.usilic.<pid>`の下に住み終了時に片付けられるべきだが、
  // ERROR/SHELLは、引数起動された実行に対してだけそうすべきで、
  // それ以外では上のtempRootDirectoryが既にフォールバックする
  // 永続的な`<homeDirectory>/.usilic`のままであるべき)。
  // tempRootDirectoryとまさに同じ理由でデフォルトは""――
  // work/arcパスに対して既存のテストフィクスチャが既に想定して
  // いるのと同じ`<homeDirectory>/.usilic`へフォールバックする。
  // bootOptions(2026-08-18の追加): ^T自身の新しい「Boot options」
  // フィールド、そのまま。デフォルトは""(素の引数無し起動自身の
  // 空の表示に一致する、executeShowUsilicBanner()のコメント参照)。
  Application(Display &display, KeyInput &keyInput, History &history, Configuration &configuration,
              Charset::Encoding encoding, int width, ShellOptions shellOptions = ShellOptions{},
              std::string homeDirectory = "", std::string tempRootDirectory = "", std::string workRootDirectory = "",
              std::string bootOptions = "");

  // `directory`を一覧表示し(DirectoryMode::enterDirectory())、
  // それをアクティブなモードにする。init_menu()の初期ディレクト
  // リ読み込みからの移植。初期の`ls`が失敗すればfalse。
  //
  // consultHistoryはそのままenterDirectory()自身の引数へ転送され
  // る(自身のコメント参照): main()は、明示的なパス引数付きで起動
  // された場合は常にfalseを渡し、引数無しの起動(legacy自身の
  // 「最後のセッションを再開する」ケース)にだけtrueを渡す。
  bool start(const std::string &directory, bool consultHistory = true);

  // 2026-08-30、直接のユーザー報告("`open -a usilic`で起動すなわち
  // 引数無しなので前回の履歴を引き継がないと。通常ファイルで終えた
  // なら次回起動時も同ファイルから")、および同日の追加依頼
  // ("utf8.tarの画面（アーカイブ構成ファイル一覧）を開いていたら、
  // 再起動時も同じ画面でよろしく。仮にmv等でファイルが存在しなくても
  // 在籍していたディレクトリ（が無い場合は親）で起動して欲しい"):
  // start()自身の上のコメントが「legacy自身の『最後のセッションを
  // 再開する』ケース」と書いている通り、引数無し起動はlegacyの
  // init_menu()同様、前回終了時に開いていたもの(`lastOpenName`、
  // History::SessionState自身のコメント参照、currentResumableName()
  // 経由で書き込まれる)を復元すべきである――しかしそれがDirectory
  // モード(単なるディレクトリ一覧行のカーソル位置)にしか及んで
  // おらず、通常ファイル/トップレベルのアーカイブ一覧を見ていたまま
  // 終了した場合にそのモードへ戻る経路が一度も配線されていなかった。
  // legacy自身のinit_menu()(old/src/menu.c、`stat(ap->name, &st)`で
  // Directory/File判定)と同じ判定をここで行う: `lastOpenName`が実在
  // する通常ファイルなら、その親ディレクトリへ入場した上でArchive
  // (アーカイブとして開けた場合)またはFile(そうでない場合)モード
  // としてそれを開く(openSelectedEntry()自身のloadArchive()/
  // fileMode_.loadFile()呼び出しと同じ優先順位・パターン)。
  // `lastOpenName`自体が実在しない(mv/rm等)場合は、その在籍して
  // いたディレクトリ(既に無ければ最も近い実在する祖先)で起動する
  // ――home等へは逃げない。ネストしたアーカイブ内部やEXEC起動中の
  // パス等、それ以外での終了はこの対象外(currentResumableName()
  // 自身のコメント参照、ユーザー自身の要求の範囲を超えるため意図的に
  // 追わない)。
  // 2026-09-29追記: .epubはopenEpub()で開き直す(EPUBモード、縦横は本が決める)。
  // ARCで見ていた.epubは「パス/」(openEpubAsArchive()参照)。
  bool startResumingSession(const std::string &lastOpenName, const std::string &fallbackDirectory,
                             bool consultHistory = true);

  // 現在のモードに応じて、SessionState::lastOpenNameへ保存すべき単一の
  // パスを返す(main.cpp自身の終了時の呼び出し参照)。startResumingSession()
  // 自身のコメントで説明した通り、Fileモードとトップレベル(ネストして
  // いない)Archiveモードだけを扱い、それ以外は単にDirectoryMode::
  // currentDirectory()を返す。
  std::string currentResumableName() const;

  // 2026-08-25(直接のユーザー依頼): `paths`をDirectoryモードの
  // Enterキーフローを完全に迂回して直接Fileモードで開く――1つ以上
  // の素のFILE引数を与えられたCLI起動のため(main.cpp自身の
  // 「通常ファイル引数」ケース、以前は完全に未対応だった: 位置
  // 引数は全て、内容に関わらずディレクトリとして扱われていた)。
  // まず「先に」start()を呼ぶこと(main.cpp自身の慣習: 最初のパス
  // の含むディレクトリ。これにより、これらのファイルのどれかへの
  // Directoryモードのenterが既に残していくのと同じ形で、^G/Back
  // に、フォールバックすべき本物の、有用なDirectory一覧がある)
  // ――このメソッドは単にpaths.front()をFileモードへ読み込み、
  // pathsが2個以上のエントリを持つ場合、残りを移動可能な「ページ
  // ャーファイル一覧」として記録する(ナビゲーションキーについては
  // Command::PagerNextFile自身のコメント参照)。最初のファイルが
  // 読み込めなければfalse――start()自身の「初期の一覧表示/読み
  // 込みが失敗すればfalse」という慣習に合わせている。main.cppが
  // 同じ種類の起動エラーを表示して終了できるように。前提条件:
  // `paths`は空でない(空の一覧は、falseを返して報告すべき実行時
  // 条件ではなく、呼び出し元のバグである)。
  bool startPagerFileList(const std::vector<std::string> &paths);
  // 2026-08-25(直接のユーザー依頼: "STDINと同じなのでついでに
  // それもサポートして"): 上のstartPagerFileList()のパイプ入力版
  // ――位置引数が「無く」、標準入力がttyでない(main.cpp自身の
  // isatty()チェック)`usilic`。FileMode::loadStream()経由でfd 0
  // を直接読む(決して閉じられない――これがなぜキーボード入力と
  // 衝突しないのかについては自身のコメント参照)。表示名は無い
  // (legacyはパイプされたソースに対して空の名前フィールドを表示
  // する)。loadStream()自体がそうなる場合にだけfalse(実際には
  // 常にtrue。start()/startPagerFileList()自身の慣習との対称性
  // のためboolのまま)。
  bool startPagerStdin();

  // 2026-09-20追加、Command::StartUnderlineView自身の宣言コメント
  // 参照(`^Ou`): directoryMode_.targetEntries()(マーク済み、無ければ
  // カーソル選択中の1件)から通常ファイルだけを絶対パスで集める。
  // ディレクトリはTUTORIAL自身の記述("通常ファイルを複数選択すると
  // ...")通り対象外――collectTargetPaths()(CP/MV/RM用、ディレクトリも
  // 対象に含む)とは中身が異なるため、無理に共通化せず別のヘルパーと
  // して実装する。
  std::vector<std::string> collectTargetFilePaths() const;
  // currentMode_ != Directoryなら何もせずfalse。対象0件でもfalse。
  // underlineMode_.show()でApplicationMode::Underlineへ切り替える。
  bool startUnderlineFileView();
  // 2026-09-21追加: アクセス履歴(RingCategory::Usilic)をUSILIC一覧として
  // 表示する。sorted=trueはファイル/ディレクトリ/manの順に、各々ソート
  // 済み重複排除。falseは記録順(直近が一番下、カーソルもそこ)。
  enum class UsilicStyle { Chronological, Sorted, Toc };
  bool startUsilicHistory(UsilicStyle style);
  // 目次(MAN/DIR/FILE)で選んだ種別の一覧(ソート・重複排除)へ進む。
  bool openUsilicGroup(const std::string &title);
  // UnderlineMode一覧からEnterで選んだファイルを開く――
  // openSelectedDuEntry()/openSelectedLocateResult()と同型。
  bool openSelectedUnderlineEntry();
  // Fileモード(fileReturnMode_ == Underline)からBackで一覧へ戻る
  // ――returnToDuResults()と同型。
  bool returnToUnderlineResults();
  // UNDER一覧「自体」(currentMode_ == Underline)からBackでDirectoryへ
  // 戻る――returnFromDu()と同型。上のreturnToUnderlineResults()とは
  // 戻り先が異なるので混同しないこと。
  bool returnFromUnderline();
  // 2026-09-21追加、直接のユーザー依頼(一覧ありモードの仕様統一):
  // DU/LOCATE/USILICの一覧から選んだディレクトリへ移動した後、そこで
  // ディレクトリを移動(`^J^K`/`^J`+数字/`^O^K`/`../`等、`./`を除く)
  // したら、その一覧への戻り(注釈スタック=モードラインの戻り値)を
  // 破棄する。以後の^Gは通常のDirectoryとして振る舞う。
  void clearListReturnAnnotations();

  // 単一の、リーダー接頭辞の無いキー入力をCommandへ解決する。
  // get_m()のswitch(m)の移動/Enter/^G/^Sの一切れからの移植――
  // 正確なバイト/SpecialKeyの対応付けとそのlegacyの出所について
  // は.cpp内の表参照。`special`は、KeyInput::readEscapeSequence()
  // がESCバイトの後に返したもの、または通常のバイトに対しては
  // nullopt。^Jリーダーのシーケンス(例えばEGREP用の^J^S、CUT用の
  // ^Jc/^J^C)は認識「しない」――それらは2回の呼び出しにまたがる
  // 状態を必要とし、この純粋関数は意図的にそれを保持できない。
  // 本物のキーごとの入力にはfeedByte()を使うこと。それは両方を
  // 扱う。
  static Command resolveCommand(int byte, std::optional<KeyInput::SpecialKey> special);

  // 現在の*Modeが何であれ、それに対してcommandを実行する。現在の
  // 状態を踏まえてコマンドが何もしなかった場合(例えば戻る先が無い
  // Back、空の一覧でのEnter)はfalseを返す――同様にそのようなケース
  // でエラーにするのではなく黙って何もしないget_m()のハンドラを
  // 反映している。
  bool handleCommand(Command command);

  // このクラスをバイトごとに駆動するための本物のエントリポイント
  // (KeyInputに裏付けられた実行ループが呼ぶであろうもの): ^J
  // リーダーの蓄積とプロンプトのテキスト入力を処理し、他の全てに
  // 対してはresolveCommand()+handleCommand()へ落ちる。結果として
  // 起きたアクション(もしあれば)自体が何もしなかった場合にだけ
  // falseを返す。アクションがまだ保留中のまま、リーダー/プロンプト
  // 状態へ消費されたバイトはtrueを返す。マクロの記録がアクティブな
  // 間(クラスコメント自身のp_km節参照)、このバイトを
  // recordedMacro_へも追加する(またはpendingLeaderXByte_に保持
  // する)。
  bool feedByte(int byte, std::optional<KeyInput::SpecialKey> special);
  // usilicrcのKEY節の置き換えを通さずに、既定のキーとして本体へ渡す
  // (2026-09-25)。GUIの右クリック(=^G、「戻る」)のように、利用者が押した
  // キーではなくプログラムが意味を決めて合成するキー用。
  bool feedCanonicalByte(int byte);

  bool isRecordingMacro() const;
  std::size_t recordedMacroLength() const;

  // ApplicationMode::Shellが現在の間、バイトをルーティングする:
  // ^Gは離脱する(なぜpty自身のセッションが取り壊されないかについ
  // てはクラスコメント参照)、^Mはコマンド行を送信する、TabはShell
  // の補完フックを尋ねる(この一切れでは未設定なので事実上何も
  // しない)、^Cはptyへそのまま転送される、DEL/^Hはバックスペース
  // する、Left/Rightはコマンド行のカーソルを動かす、他の全ては
  // 通常のテキストとして挿入される。(feedByte()に黙って折り込む
  // のではなく)公開されているので、呼び出し元――またはテスト――は、
  // まずApplicationを本物の^[sシーケンス経由でShellモードにする
  // ことなく、Shellへの入力を駆動できる。
  bool feedShellByte(int byte, std::optional<KeyInput::SpecialKey> special);

  bool promptActive() const;
  // 自身の定義コメント(application.cpp)参照: promptActive()に
  // 加えて、RecentFile/RecentDirectoryの直接エントリ呼び出し状態
  // ――ホバー/たまたまの再描画を抑制する呼び出し元向けであり、
  // 汎用的な「これは本物の生きたテキストプロンプトか」というチェ
  // ック(それは依然としてpromptActive()単独である)ではない。
  bool redrawSensitivePromptOrRecallActive() const;
  const std::string &promptText() const;

  // 2026-08-14: 実際の報告("④の^[不具合は-gで起きる模様。^Jl後の
  // ^Gも受け付けない。-cでは問題なし")が、runLocateUpdateDb()自身の
  // 内部のpoll(STDIN_FILENO,...)ループを、このクラスがそうでなけ
  // れば常に尊重するまさにそのアーキテクチャ上の境界(全体を通じて
  // 「単一スレッド/同期的」であり続け、バックエンドに依存しないと
  // いうことについては自身のクラスコメント参照)まで辿った: その
  // ループは「1回の」Application呼び出しの「内側で」ブロックして
  // いたので、そのループが返るまで-g自身のSDLイベントポンプ
  // (app/main.cppのrunGuiPhase())は二度と実行されなかった――つまり
  // GUIウィンドウでの実際の^Gクリック/キー入力はSDLによってキュー
  // に入れられたが、その中で詰まっている間gui.pumpEvents()が二度と
  // 呼ばれないので、実際には処理されなかった。-c自身の端末実行
  // ループはたまたま依然として動いていた。そこではSTDIN_FILENOが
  // 「本物の」キーボードであるため、ブロックされた呼び出しの内側の
  // poll()は依然として直接^Gを見ることができた――しかしそれは意図
  // 的なものではなく偶然であり、このクラスがそうでなければ保証
  // している「バックエンドに依存しない」という性質そのものを壊し
  // ていた。runLocateUpdateDb()自体が、切り離されたupdatedbを起動
  // した直後に即座に返るよう再設計された(自身のコメント参照)
  // ――これら2つのメソッドは、代わりに「どちらの」実行ループ自身の
  // 既存の反復ごとのティック(バックエンドに関わらず両方とも既に
  // 頻繁にループする)にその待機を駆動させる。wantsExit()/
  // promptActive()自体が既に使っている「外部からチェックされる
  // フラグ」という同じ形。
  bool awaitingLocateUpdateDb() const;
  // 実行ループの反復ごとに1回、無条件に呼ぶこと(何も保留していな
  // ければ安価な何もしない呼び出し)――実行中のupdatedbに対して
  // ChildProcess::hasExited()をチェックし、それが完了したら、
  // startLocatePrompt()自体へ遷移する。^Gはここでは扱われ「ない」:
  // それは、feedByte()/dispatchByte()自身の新しいトップレベルの
  // awaitingLocateUpdateDb()ゲート(promptKind_よりも前にチェック
  // される)経由で、通常のバイトとして到着するので、両方の実行
  // ループは、他の全てのキー入力に対して既に行っているのとまさに
  // 同じ呼び出しを通じて、無料でそれを得る。
  void pollLocateUpdateDbCompletion();

  // 2026-09-19追加、直接のユーザー依頼("LOCATEと同じく^Gでduを中断し、
  // 構築中断時点のDUリストへ移動"): 上のawaitingLocateUpdateDb()/
  // pollLocateUpdateDbCompletion()と全く同じ「実行ループ自身の既存の
  // ティックによって駆動され、外部からチェックされるフラグ」という
  // 形――ただしLOCATEのupdatedb(ChildProcess::runDetached()、標準
  // 出力を/dev/nullへ捨てる、完了以外に意味のある「結果」を持たない)
  // とは異なり、`du`の標準出力は行ごとに意味のある構築途中の結果
  // なので、ChildProcess::startCapturing()/readAvailable()経由で
  // 捨てずに蓄積し続け、^Gによる中断時にはその時点までの分だけを
  // DuMode::loadUsageFromOutput()へ渡してDU一覧として表示する
  // (runDuOnCurrentFolder()自身のコメント参照)。
  bool awaitingDu() const;
  void pollDuCompletion();

  // 2026-09-20追加、直接のユーザー報告("debian-keyring_2015.03.04.
  // tar.xzを踏んだら反応が無くなった。LOCATE同様、^Gで読み込みを
  // 中断できるようにして"): awaitingDu()/pollDuCompletion()と全く
  // 同じ「実行ループの既存のティックで駆動される非同期ポーリング」
  // の形。ArchiveMode::loadArchive()自身は(.tar.xz/.tgz等の巨大な
  // アーカイブでは)展開+一覧コマンドの完了をブロッキングで待つため、
  // ^Gが割り込む余地が無かった――ChildProcess::startCapturing()
  // 経由で非同期に起動し直し、完了/^G中断のどちらでもその時点まで
  // 蓄積した出力をArchiveMode::finishLoadFromOutput()へ渡す
  // (startArchiveLoadAsync()/finishArchiveLoadAsync()自身の宣言
  // コメント参照)。
  bool awaitingArchiveLoad() const;
  void pollArchiveLoadCompletion();

  // 実行ループの反復ごとに1回、無条件に呼ぶこと(pendingCpMvRmShellRun_
  // が未設定なら安価な何もしない呼び出し)――すぐ上の
  // pollLocateUpdateDbCompletion()と同じ「実行ループ自身の既存の
  // ティックによって駆動され、外部からチェックされるフラグ」という
  // 形(理由については自身のコメント参照: 単一のApplication呼び
  // 出しの内側での無制限のブロッキング待機は、LOCATEのupdatedbに
  // 対して既に試され、拒否された。ここでも誤りである理由は同一
  // ――それは-g自身のGUIイベントポンプを飢えさせてしまう)。保留中
  // の間、shell_自身のスクロールバックは、currentMode()==Shellの
  // 時に両方の実行ループが既に行っているのと「同じ」watchShellPty
  // のポーリング経由で、反復ごとに無料でライブに更新され続ける
  // (executeCopy()自身のクラスコメント参照)――このメソッドは
  // 「完了」に気付く(shell_.promptGeneration()が進む)ことと、
  // その後Directoryモードへ着地することだけを扱う。
  void pollPendingCpMvRmShellRun();
  // pollPendingCpMvRmShellRun()と同じ形(自身のコメント参照)、
  // executeArchiveExtract()自身のshell_で注入された実行のため。
  void pollPendingArchiveExtractShellRun();

  // 2026-08-25(直接のユーザー依頼、usilic.c自身のfrom_main()/
  // from_shell()からの移植――子プロセス側についてはmain.cpp自身の
  // tryHandoffToParentShell()参照): Shellモードが生きている間、
  // この「shellの内側から」呼び出された本物のusilic(例えばプロン
  // プトに入力された素の「usilic .」、またはそれへのパイプ――
  // Shell::start()自身のsetenv("USILIC", ...)が、その子プロセスに
  // 独立したセッションを開く代わりにここへ引き渡すべきと検出させる
  // もの)が、/tmp/.usilic-shell/s.<uid>.<shell_.childPid()>に
  // ランデブーファイル/ソケットを残したかどうかをチェックする――
  // どちらの端も独立に計算する「同じ」パス(子はgetppid()経由、
  // こちら側はshell_.childPid()経由。事前の交渉は不要)。もし
  // あれば、それに応じてこのセッション自身の表示を切り替え(パス
  // に対してはDirectory、素のファイルに対しては通常の
  // fileMode_.loadFile()――通常のDirectory一覧のEnterに対して
  // openSelectedEntry()が行うのとまさに同じ呼び出し、自身の
  // 2026-08-25の訂正コメント参照――またはパイプされたソースに
  // 対しては、startPagerStdin()が使うのと同じ遅延読み込みの仕組み
  // であるFileMode::loadStream()経由のstdinページャーの読み込み)、
  // trueを返す。Shellモードがアクティブ/生きていない場合、または
  // まだランデブーが待っていない場合は単純に何もしない(false、
  // 何も触れられない)――pollPendingCpMvRmShellRun()自身のポーリン
  // グの慣習に合わせて、メインループの反復ごとに無条件に呼ぶのに
  // 十分安価である。
  bool pollShellHandoff();
  // 2026-09-17追加、直接のユーザー報告+提案(実機報告"D. が直ってない"、
  // 提案"実行後SHELLへ戻る案、戻らずに仕様だけ頂くってのはどうかな？
  // ^Jpusilic ...はlessのように現usilicから出ること無くusilic内で
  // 完結する。SHELLと違うのはPROGRAMを発動した場所へ戻る点"): ^Jp
  // (PROGRAM)経由で"usilic"が認識された場合、executeProgram()から
  // 通常のprogramMode_.run()(ChildProcess::runInteractive()、完全に
  // 同期ブロック)の代わりにこちらを呼ぶ。ChildProcess::
  // spawnInteractive()(pollShellHandoff()自身のコメントが説明する
  // getppid()側のランデブーパス計算と一致する、この子自身のpidを
  // 使う)で子を起動し、hasExited()でのポーリングの合間に
  // tryPollProgramHandoff()も呼ぶことで、pollShellHandoff()と全く
  // 同じランデブー機構(main.cpp自身のtryHandoffToParentShell())に
  // ProgramMode::run()自身の完全ブロッキングな設計のままでは参加
  // できなかった問題を解消する――USILIC環境変数を子のコマンドライン
  // 自体に付与する(Shell::start()自身のsetenv()と違い、この子は
  // usilic自身のfork/execであってShellの管理下のptyではないため)。
  bool runNestedUsilicProgram(const std::string &command);
  // runNestedUsilicProgram()自身の宣言コメント参照。pollShellHandoff()
  // の核心部分の複製――違いは(1)shell_.childPid()の代わりに引数の
  // pidを使う、(2)currentMode_ == Shellのゲートが無い(このポーリング
  // はProgramMode文脈でのみ呼ばれ、Shellとは無関係)、(3)戻り先が
  // 常にShell/Directory固定ではなく、呼び出し時にApplicationが
  // 実際にいたモード(capturedMode)になる――パイプされたstdinの
  // ケースだけ、FileMode::setPagerReturnsToProgram(true)でモード
  // ライン上「PROGRAM」と明示する(setPagerReturnsToShell()の
  // "SHELL"と同じ考え方)。プレーンなパス/ファイルのケースは
  // pollShellHandoff()自身と全く同じ(常にDirectory、legacy自身の
  // 挙動に合わせた既存の無条件の値であり、呼び出し元の文脈に
  // 依存しない)。
  bool tryPollProgramHandoff(pid_t pid, ApplicationMode capturedMode);
  // 2026-09-17追加、直接のユーザー報告("表示はOKだがキーが一切効か
  // ない、以前効いた^Cも効かなくなった")+runNestedUsilicProgram()
  // 自身のコメント参照: パイプされたstdinのハンドオフが成立した
  // 直後、子(sh経由の入れ子usilic)がまだソケットへ書き込み中(送信
  // バッファ満杯でブロックされたまま)かもしれない状態で、
  // runNestedUsilicProgram()自身がその完了を待たずに手放した場合に
  // 記録される、まだ回収(waitpid)していないpid。-1は「回収待ちの
  // 子は無い」。pollPagerExitNotice()自身の冒頭でメインループ毎回の
  // 反復のついでにopportunisticに回収し、ゾンビ化を防ぐ。
  pid_t pendingProgramChildPid_ = -1;
  // pollPagerExitNotice()自身の宣言コメント参照(2026-09-20追加)。
  // `suppressDraw`(2026-09-07追加、直接のユーザー報告"わかったのは
  // ブロックカーソル位置を画面上真ん中辺に変更すると、瞬時の表示も
  // 同じ位置（真ん中辺）になされる点。うっすら見える数字は相変わらず
  // （最下行の値）"): この関数が内部で呼ぶensurePagerStreamLoaded()
  // は、新しい内容を読み込めた瞬間にactiveView().redraw()+flush()を
  // 即座に行う(2026-09-20追加、自身のコメント参照――SHELLからの
  // PAGERハンドオフ直後に画面が更新されない不具合の修正)。この
  // メソッド自体は毎フレーム無条件に呼ばれる(main.cppのコメント
  // 参照)ため、GUIの慣性スクロール/ドラッグの真っ最中でも、背景の
  // 先読みが1行進むたびにこの通常のredraw()(cursorIndex_/
  // hoverIndex_基準のハイライト付き)がその場でSDLへ実際に提示されて
  // しまい、renderScrolled()の滑らかなピクセルスクロール描画の上に
  // 一瞬だけ全く別の内容(ハイライト位置・位置表示とも異なる)が
  // 挟まって見える――ちょうどこの症状の報告と一致する(ハイライトは
  // 常にredraw()自身のhoverIndex_優先ロジックに従うため、ユーザーが
  // マウスホバーで指定した位置にそのまま現れる)。呼び出し元がこの
  // フレーム自身のrenderScrolled()で確実に上書き提示することが
  // 分かっている間(main.cppの`dragging || coastLevel > 0 ||
  // pixelOffset != 0`と全く同じ条件)はtrueを渡し、内部で
  // ensurePagerStreamLoaded()の代わりにFileMode::ensureLoadedThrough()
  // を直接呼んで読み込みだけ行い、redraw()/flush()を省く――
  // preloadPagerAhead()が既にCommand::ScrollLineDown/PageDown向けに
  // 行っているのと同じ「読み込みと描画を分離する」パターン。デフォルト
  // false(CUIや、GUIでもスクロール中でない他の全ての呼び出しは
  // 影響を受けない、既存のCUI向け動作を保つ)。
  void pollPagerExitNotice(bool suppressDraw = false);
  // 2026-09-06追加、直接のユーザー報告("％の行数が97と98を行き来...
  // -g起動ではスクロールバーの（つまみ長を短くする）描き直しでも
  // モードライン描画が起きるみたい")――Command::ScrollLineDown/
  // PageDown自身のハンドラのコメント参照。main.cpp自身のGUIマウス
  // ホイール/慣性スクロール/スクロールバードラッグ(ContentView::
  // setTopIndex()/stepTopIndex()を直接操作するだけで、handleCommand()
  // を一切経由しない、それ自身のコメント参照)や、CUIのShift+Up/Down
  // コースト(runCoastLoop())も、キーボード駆動のScrollLineDown/
  // PageDownと全く同じ「まだ読み込んでいないtopIndex_へ動いてから
  // pollPagerExitNotice()の次回反復に読み込みを丸投げする」形になって
  // おり、同じ二重描画が起きる――この2つの公開メソッドで、それらの
  // 呼び出し元がキーボード側と同じパターン(移動の「前」にpreloadPagerAhead()、
  // 移動の「後」にsuppressPagerRepoll())を再利用できるようにする。
  // File以外のモード/生きたストリームが無い間はどちらも無害な無視。
  void preloadPagerAhead(int prospectiveTopIndex, bool bypassPacing = false);
  void suppressPagerRepoll(int actualNewTopIndex);

  // GUIのマウス範囲選択向け(2026-09-07追加、直接のユーザー要望:
  // "スクロールに関する画面ドラッグと同じマウス操作に関して、発動
  // し得る機能としては範囲指定がある。COPYモードの際にそれは可能
  // だろうか？")。main.cpp(runGuiPhase()のDragEvent消費)からだけ
  // 呼ばれ、いずれもcurrentMode_ != Copyの間は無害にno-opする――
  // 呼び出し元がモードを問わず同じDragEvent消費ループから毎回呼べる
  // ようにするため(スクロール用のcoastLevel/pixelOffset等を一切
  // 触れない、CopyMode自身のスナップショットは元々スクロール物理
  // (惰性/慣性)を必要としない静的な内容であるため)。`row`/`column`
  // はいずれもDisplay::MouseClick/DragEventと同じ表示座標系
  // (moveCursor()の座標系)。
  //
  // copyMouseDown(): 押下位置へキャレットを移動し、その場でマーク
  // アンカーを置く――ドラッグに発展せずそのままcopyMouseUp()が来た
  // 場合、CopyMode::confirmMark()自身のコメント通り空範囲は
  // クリップボードに触れないので、実質的には「クリックでキャレット
  // を置くだけ」の動作になる。
  void copyMouseDown(int row, int column);
  // copyMouseDrag(): 現在のドラッグ位置までマークを伸縮する。
  // CopyMode::placeCursorAt()自身のコメント参照――マーク開始行を
  // 一切またがない(legacy自身の一行限定選択モデル)。
  void copyMouseDrag(int row, int column);
  // copyMouseUp(): 確定する(空範囲なら何もしない)。
  void copyMouseUp();

  // 終了が確認された(StartExitのプロンプトが先頭'y'で送信された)
  // 時点でtrue――将来の本物の実行ループへの、feedByte()の呼び出しを
  // 止めて巻き戻すべきという合図。Application自体は決してこれを
  // 読まない。それが切り替わった時他に何も変わらない。
  bool wantsExit() const;

  // wantsExit()を直接trueに設定し、StartExitのy/n確認プロンプトを
  // 完全に迂回する。GUI起動されたセッション自身のより単純な終了
  // ジェスチャー(ESC ESC Enter、app/main.cppのrunGuiPhase()参照)
  // のために追加された――その文脈のために確認プロンプトの代わりに
  // 依頼されたもので、他所でのその置き換えではない(端末フェーズの
  // ^X^C+yフローは変更されていない)。
  void requestImmediateExit();

  // 「表示」行/列でのマウスクリック(この座標空間と現在の
  // Directoryモード限定のスコープについてはクラスコメント自身の
  // GUIモード節参照)。その行のエントリを選択するか、それが既に
  // 選択されていたものだった場合は――それを開く(Enterと同じ)。
  // Directoryモードの外側、またはクリックがどの本物の行の外側にも
  // 着地した場合はfalse。
  bool handleMouseClick(int row, int column);
  // KEYのhで飛んだ先の行のハイライトを消す(マウスに触れた時、main.cppから。2026-10-03)。
  void clearHelpJumpHighlight();
  // 2026-10-02追加、直接のユーザー報告("-gの^X^Fでスクロール停止時ハイライト行がマウスの動きに
  // 追随しないため、左クリックではファイルを選べない"): 直接入力の一覧(^X^F/^X^D等の履歴の
  // 呼び出し、^Iの補完一覧の閲覧中)か。この一覧はredrawSensitivePromptOrRecallActive()のため
  // 通常のホバー(ContentView::redraw())から外してあるので、ホバーはhoverDirectEntryList()で、
  // モードラインをrefreshDirectEntryModeLine()で描き直す形にする。
  bool directEntryListActive() const;
  // その一覧で、表示行`row`の項目を選ぶ。選び直した時true。
  bool hoverDirectEntryList(int row);

  // 端末のリサイズが検出された(本物の実行ループの合図、
  // KeyInput::consumeResizeSignal()がDisplay自身の幾何形状を既に
  // 更新した後――これは別々のパラメータからではなく、`display`
  // からrows()/columns()を読み戻すので、決して古びたサイズを
  // 見ることはできない)。menu.cのT_winch処理からの移植: 所有される
  // 全てのContentViewのビューポートとプロンプトフィールドを新しい
  // 幾何形状にリサイズし、File/Manモードのどちらかが現在アクティブ
  // であればその内容を新しい幅で再折り返しし(FileMode::reflow()/
  // ManMode::reflow())、「トップレベルの」Cut/CutContentの分割
  // (本物のCUTまたはHELP――クラスコメント自身のHELP節参照)に対し
  // ても同じことをする――Directory/Grep/Locate/Archive/Du/
  // ListBrowserの内容は、File/Man/トップレベルCutのものとは異なり、
  // 幅で折り返された段落テキストではない。legacy自身の
  // T_dir/is_list/is_strの再折り返ししない扱いに合わせているので、
  // それらの行はビューポートのリサイズを超えてはそのまま残される。
  // 「ネストされた」Cutの再分割もそのまま残される(クラスコメント
  // 参照)。新しいサイズは(今Shellが可視のモードでなくても)
  // 無条件にShellのptyへ伝播される(本物の端末のリサイズは、全
  // モードが共有する1つの物理的な端末に影響する。本物の端末マルチ
  // プレクサがバックグラウンドのペインもリサイズするのと同じ)。
  // そして現在画面上にあるものが何であれ再描画する。
  void handleResize();

  // 2026-09-12追加、項目⑫。当初はGUIモード限定のDock風モードライン
  // 自動表示/非表示専用だったが、そのDock機能自体は項目458で撤回
  // 済み――現在の呼び出し元は、`Command::ToggleModeLineVisibility`
  // 自身のhandleCommand()ハンドラ(^Oh、-c/-g共通、既にdisplay_/
  // history_双方を更新した「後」に呼ぶ)と、main.cpp起動時の
  // history.modeLineVisible()適用直後(display.setModeLineVisible()
  // 呼び出し側が既にdisplay_の実効状態を書き換えた「後」)の2箇所。
  // どちらも呼び出し側が既にdisplay_の状態を確定させてから呼ぶという
  // 前提は変わっていない。2026-09-13追加、項目461のさらなる修正
  // (TerminalDisplay::columns()自身のコメント参照――^Ohの表示/非表示
  // でcolumns()の値自体が変わるようになった)を受けて、単なる
  // layoutViews()+redraw()では幅に依存する内容(FileMode/ManModeの
  // 折り返し、DirectoryModeのls列レイアウト等)が古いままになるため、
  // handleResize()自身(本物の端末リサイズと全く同じ理由でdisplay_の
  // 現在のrows()/columns()を読み直して幅・高さ両方をリフローする)へ
  // 委譲するよう変更した。
  void relayoutForModeLineVisibilityChange();

  // Shell::onPtyReadable()がEOFを報告するたびに(子プロセスが、
  // 典型的には`exit`経由で、自力で終了した)メインループによって
  // 呼ばれる。定義自身のコメント参照。
  void handleShellChildExited();

  // HELP(^J^H)のテキストを供給する――Configuration::loadRcText()/
  // History::loadHistoryText()の「呼び出し元がファイルI/Oを行う」
  // というルールを反映している、クラスコメント自身のHELP節参照。
  // ^J^Hが何かをできるようになる前に呼ばれなければならない。
  // textが空の場合はfalse(HELPは利用不可能なまま、StartHelpは
  // 単に失敗する)。
  bool loadHelpText(const std::string &text);

  ApplicationMode currentMode() const;
  // 起動時に一度だけ決まる、ファイル内容の文字コード判別に使うヒント
  // (history.savedCharset().value_or(configuration.defaultCharset())、
  // main.cpp参照)。2026-09-01より前はK-27(^Oa)でセッション中にライブ
  // 更新できたが、その対話的な3値巡回はCommand::ToggleHelpLanguage
  // 自身のコメントの通り廃止された――今はこのメンバー自体、構築後は
  // 一切変更されない。main.cppがシャットダウン時にHistory::
  // setSavedCharset()へこれをそのまま書き戻しているのは、legacy自身の
  // historyファイルのトレーラー形式(このC++移植とlegacy usilicが
  // 同じファイルを共有し得る)との互換性のためにあえて残してある
  // ――もはやこの移植自身にとっては起動時に読んだのと同じ値を
  // そのまま書き戻すだけの無害な往復に過ぎない。
  Charset::Encoding encoding() const;

  // feedByte()に供給される「次の」バイトが、プレーンなトップ
  // レベルのナビゲーションコマンドとして解釈されるのではなく、
  // 保留中の複数キーシーケンス(ESC接頭辞、^Jリーダー)の一部として、
  // またはアクティブなプロンプト/入力ウィジェットへのリテラルな
  // テキストとして消費されるであろう時は常にtrue。runGuiPhase()の
  // w/s慣性スクロールトリガーの割り込み(CLAUDE.mdの2026-07-31の
  // 記録参照)のために追加された: 'w'/'s'は、これらの状態のどれで
  // あっても、スクロールのために乗っ取られるのではなく、
  // feedByte()へ手を付けられずに落ちなければならない――StartShell
  // のために「ESC s」を入力すること、またはFGREP/パスプロンプトへ
  // リテラルな「w」/「s」を入力すること。呼び出し箇所での
  // currentMode() != Shellのチェックと組み合わされる(タイピングは
  // 常にshell自身のコマンド行に到達しなければならない)。
  bool awaitingSequenceOrPromptByte() const;
  DirectoryMode &directoryMode();
  FileMode &fileMode();
  GrepMode &grepMode();
  CutMode &cutMode();
  ManMode &manMode();
  ProgramMode &programMode();
  LocateMode &locateMode();
  ArchiveMode &archiveMode();
  Shell &shell();
  CpMvRmMode &cpMvRmMode();
  DuMode &duMode();
  UnderlineMode &underlineMode();
  ListBrowserMode &listBrowserMode();
  EditMode &editMode();
  ExecMode &execMode();

  // 元々は2026-07-30に、単なるデモサポートとして追加された
  // (ドラッグスクロールの実現可能性サンプル、GraphicalDisplay自身
  // のクラスコメント参照)――プロトタイプの実行ループが、Command
  // ディスパッチを完全に迂回して、現在画面上にあるContentViewが
  // 何であれ、その完全な内容を直接描画/スクロールできるように、
  // それを公開する。2026-08-29に本物の、永続的なAPIへ昇格された:
  // startCopy()(自身のコメント参照)は、まさにこの同じ「現在画面
  // 上にあるものが何であれ、モードに依存しない」という性質を、
  // COPY自身のスナップショットのソースとして使う。これは本当に
  // 一級の用途であり、別のプロトタイプではない――ContentView::
  // lines()にこのアクセサを加えたものが、モードごとの統合を全く
  // 必要としない、モードに依存しないCOPYに対して十分であることが
  // 判明した(.steering/20260829-copy-mode/内のrequirements.md自身
  // のC++移植側の該当構造の調査結果参照)。
  ContentView &activeContentView();
  // EPUBの縦書き(Application::rebuildEpubVertical()参照): -gのホイール/トラック
  // パッドを列単位の横スクロールにするためにapp/main.cppから使う。
  bool epubVerticalScrollActive() const;
  bool scrollEpubVertical(int delta);  // 右端の列が実際に動いたらtrue
  // 2026-09-29追加、直接のユーザー依頼("左右方向の画面ドラッグを実装で"): 縦書きの本文で
  // 左右ドラッグを受け付けるか(app/main.cpp、GraphicalDisplay::setContentOffsetX()参照)。
  bool epubVerticalDragActive() const;
  // 2026-09-29追加、直接のユーザー依頼("スクロールバーは出ないの？"→B「右端の縦の
  // スクロールバーをそのまま使う」): 縦書きの本文の間、右端のスクロールバーに渡す
  // {右端の列, 列の数, 1画面の列の数}(上端が本の最初、下端が本の最後)。それ以外はnullopt。
  struct EpubScrollbar {
    int first, total, perScreen;
  };
  std::optional<EpubScrollbar> epubVerticalScrollbar() const;
  // スクロールバーのクリック/つまみのドラッグ: 本全体の`fraction`(0〜1)の所を右端に。
  void jumpEpubVerticalToFraction(double fraction);
  // 2026-09-25追加: 長い同期処理(^[^Mの「そのまま読む」)の途中で、
  // GUIウィンドウ側にキー入力が届いたかを問い合わせる関数。GUIでは
  // キーがSDL経由で届き起動元ターミナル(keyInput_)には来ないため。
  // main.cppのrunGuiPhase()が設定し、抜ける時にnullptrへ戻す。
  // openSelectedEntryRaw()自身のshouldAbortのコメント参照。
  void setInterruptPeeker(std::function<bool()> peeker);
  // 2026-09-25追加(main.cppのofferGroffInstallOnce()用): SHELLへ
  // 切り替え、`commandLine`を入力欄に置いた状態で(実行はせずに)待つ。
  // 2026-09-27拡張(直接のユーザー提案"brew install groffは2回必要なので、処理
  // が終わり、プロンプトが戻ったらインストール済みかどうか確認後、未インス
  // トールのアプリのみ再度brew実行"): `followUp`を渡すと、置いた行が実行されて
  // プロンプトが戻るたびに呼び、空でない文字列が返ればそれを再び入力欄に
  // 置く(最大`retries`回)。pollShellOfferFollowUp()参照。
  bool offerShellCommand(const std::string &commandLine, std::function<std::string()> followUp = nullptr,
                         int retries = 2);
  void pollShellOfferFollowUp();
  // 2026-09-25追加: GUIウィンドウへドロップされた/Finderから「この
  // アプリケーションで開く」で渡されたパスを開く(GraphicalDisplay::
  // consumeDroppedFiles()参照)。1つなら^Xfでパスを入力した時と同じ
  // executePathInput()(ディレクトリ→DIR、アーカイブ→ARC、他→FILE)、
  // 複数なら^Ouと同じUnderline一覧を作り、その最上行を開く。
  bool openDroppedPaths(const std::vector<std::string> &paths);

 private:
  // p_kmのための、1つの記録されたバイト(クラスコメント参照)――
  // feedByte()自身の以前の本体、名前を変更されdispatchByte()、が
  // 実際の仕事を行う。feedByte()自体は単に記録の記帳でそれを包む
  // だけである。
  struct RecordedStep {
    int byte;
    std::optional<KeyInput::SpecialKey> special;
  };
  bool dispatchByte(int byte, std::optional<KeyInput::SpecialKey> special);
  // GrepPattern/CutPattern自身の「Select string」ListBrowser
  // (startRegexPromptList()自身のコメント参照): 精選された一組の
  // バイトはリストナビゲーション専用のままであり、他の全ては、
  // ハイライトされた候補自身のテキストで事前に埋められた編集可能
  // なプロンプトへ遷移する――完全な設計については自身の定義コメン
  // ト参照。
  bool feedRegexListByte(int byte, std::optional<KeyInput::SpecialKey> special);
  // ApplicationMode::Copyがアクティブな間の、^O^C自身の小さなキー
  // 語彙――startCopy()自身のコメント参照。resolveCommand()の汎用
  // バイト->Commandテーブルを経由「しない」: そこでのこの移植自身
  // のLeft/Right矢印=Up/Downの慣習(自身のコメント参照)と^B/^F
  // 自身のページスクロールの紐付けは、両方とも本物の水平方向の
  // テキストカーソルより前からあるので、CopyModeはどちらかを流用
  // する代わりに完全に自己完結したディスパッチを得る。
  bool feedCopyByte(int byte, std::optional<KeyInput::SpecialKey> special);
  // ESC/^J/^X/^X^K/^Oのリーダーキー状態機械、feedShellByte()も
  // それに到達できるよう、dispatchByte()から括り出されている
  // ――自身のコメント参照(2026-08-02のユーザー報告: legacyは
  // ^J/^X/^Oのリーダーが、SHELLの内側から直接動作することを許す
  // (例えば^X^F/^X^C)。これは、この機能が存在する前は、この移植が
  // 生のshell入力として黙って飲み込んでいたものである)。
  bool dispatchLeaderByte(int byte, std::optional<KeyInput::SpecialKey> special);
  // 2026-09-08追加、直接のユーザー報告("usilic画面では98行だった行が
  // 97として表示されている"): dispatchLeaderByte()自身のgotoLine*
  // (^[N/^[gg N)から使う。`targetRawLine`は人間向けの1始まりの「生の」
  // 行番号(ContentView::Line::rawLineNumber自身のコメント参照)――
  // File modeでは幅超過による折り返しで1つの生の行が複数の表示行に
  // 分かれ得るので、`view`に既に読み込まれている範囲だけでは目的の
  // 生の行へまだ届いていないことがある。その場合はpreloadPagerAhead()
  // でさらに読み進めさせてから再チェックする(pagerが伸びなくなれば
  // ファイル末尾に達したとみなしクランプする)。rawLineNumberを持たない
  // モード(Directory等、そもそも折り返しの概念が無い)では、`view`の
  // 行インデックス自体が既に「行番号」なので素通しする。戻り値は
  // `view.setCursorIndex()`にそのまま渡せる0始まりの行インデックス。
  int resolveGotoLineRow(ContentView &view, int targetRawLine);
  // SHELLのscrollback_の行全体のハイライトに対する^P/^N自身の
  // プレーンなテキストカーソルの代替――自身の定義コメント参照。
  void moveDisplayCursorToShellHistoryLine();
  bool startMacroRecording();
  bool stopMacroRecording();
  bool playRecordedMacro();

  bool openSelectedEntry();
  // Command::ReadFileNoWrap/ReadFileRaw自身のコメント参照。
  bool openSelectedEntryNoWrap();
  bool openSelectedEntryRaw();
  bool openSelectedSegment();
  bool openSelectedArchiveEntry();
  // 入れ子のアーカイブサポート(docs/ideas/IDEAS.md自身の、C++移植
  // 全体のための元々の動機となった例: "アーカイブの中にさらに
  // アーカイブファイルがあっても、それを選んで再度ARCモードに入る
  // (どこまで潜っても同様に動く)ことができない"――legacy自体も
  // これを一度もサポートしたことが無い、menu.cのT_tar等の処理
  // 参照。それは常に移動先へ展開するか生のバイトを表示するだけで
  // ある。これはこの移植が追加する新機能であり、既存の挙動の移植
  // ではない)。ArchiveMode自体は、設計上、一度に1つのアーカイブ
  // だけを扱うクラスのままである(自身のクラスコメント参照)――
  // 「現在開いているアーカイブの連鎖」を調整することはApplication
  // の仕事であり、他の全てのモードの引き渡しに対して既に描かれた
  // のと同じ境界である。
  //
  // ArchiveMode::loadArchive()は本物のファイルパスを必要とする
  // (メモリバッファから読まないtar/unzip等をシェルアウトする)ので、
  // 入れ子のアーカイブに入場することは、まずその展開されたバイト
  // (既にArchiveMode::extractSelected()経由で利用可能、新しい展開
  // ロジックは不要)を本物の一時ファイルに実体化し、その後トップ
  // レベルのアーカイブが読み込まれたのとまさに同じ方法で「その」
  // パスを読み込むことを意味する。archiveParentStack_は各階層の
  // パスを記憶する(最も外側のアーカイブに対しては本物のファイル、
  // それより下の各階層に対してはこれらの一時ファイルの1つ)ので、
  // returnFromArchive()は、常にDirectoryへ直接戻るのではなく、
  // 一度に1階層ずつ登り返すことができる。
  bool descendToNestedArchive(const ArchiveEntry &entry);
  // `bytes`を`dir`の下の新しいファイル(呼び出しごとに新しい
  // ディレクトリ。異なるネスト階層で名前がたまたま同じ兄弟エントリ
  // の間でベース名の衝突を避ける)へ、entry.name自身のベース名に
  // ちなんで(programForSuffix()がそれでディスパッチするので、
  // suffixを保持したまま)書き込む。`dir`は呼び出し元自身の関心事
  // である――descendToNestedArchive()はallocateArcDirectory()の
  // 結果を渡し(2026-08-18: 以前は素の
  // `/tmp/usilic-arcXXXXXX`自体をmkdtemp()していたが、今は代わり
  // にarc.Nスロットを消費する、その関数自身のコメント参照)、
  // executeShowUsilicBanner()はtempRootDirectory_を直接渡す(それ
  // 自身の単一の固定ファイルはarc.Nの連番の一部では「ない」――
  // それはアーカイブ展開ではなく、そうしないとそれ自身の一覧内に
  // 再帰的に現れてしまう)。/tmp/.usilic自身の掃除されない作業ファイル
  // (CLAUDE.md)に対するこのプロジェクトの既に文書化され、既に
  // 受け入れられている許容と同様、再起動時/tmpクリーナーの掃除で
  // OSが片付けるに任される――archiveParentStack_にまだ乗っている
  // 親フレームが、同じセッション内で後でそれを再読み込みする必要
  // があるかもしれないので、積極的には削除されない。失敗時
  // (ディレクトリ作成/ofstreamの失敗)は空文字列。
  std::string materializeArchiveTempFile(const std::string &dir, const std::string &entryName,
                                          const std::vector<unsigned char> &bytes);
  // K-17(^X^W)/K-64(^J^W)の共有アロケータ: 新しい
  // `tempRootDirectory_ + "/w." + N`パスを返し、
  // nextWorkFileNumber_を進め、それをallocatedWorkFiles_(^T自身の
  // 「WORK」一覧)に記録する。ファイル自体は作成「しない」――呼び
  // 出し元は、それ自身の用途が必要とするモード(std::ios::
  // app/trunc)でそれを開く。
  std::string allocateWorkFilePath();
  // 入れ子アーカイブの降下自身のアロケータ(K-57/K-58自身の
  // descendToNestedArchive()向け、バナーの固定単一ファイル向けで
  // はない――materializeArchiveTempFile()自身のコメント参照): 新しい
  // `tempRootDirectory_ + "/arc." + N`ディレクトリパス(まだディスク
  // 上には作成されていない――materializeArchiveTempFile()自身の
  // 呼び出し元がそれを作成する)を返し、nextArchiveNumber_を進め、
  // それをallocatedArcDirs_(^T自身の「arc.N」一覧)に記録する。
  std::string allocateArcDirectory();
  bool openSelectedDuEntry();
  bool openSelectedLocateResult();
  bool goToFileSelector();
  bool goToParentFromFileOrArchive();
  bool jumpToPathPrefix();
  bool openSelectedListEntry();
  bool openSelectedListEntryImpl();  // openSelectedListEntry()の本体(^J^Eの縦書きの受け渡し)
  // openSelectedListEntry()自身のRecentFile分岐とgoToRecentFile()
  // (^J^J)で共有される――それら自身のコメント参照。
  bool openRecentFileEntry(const std::string &path);
  bool goToRecentFile();
  // Command::Back自身の、空のModeStackに対するフォールバック
  // ――自身の呼び出し箇所のコメントとこの関数自身の定義コメント
  // 参照。
  bool goToRecentDirectory();
  bool toggleMark();
  bool returnToDirectory();
  bool openSelectedGrepMatch();
  // in-place GREP(2026-09-09追加、design.md参照)。executeGrep()自身の
  // コメント参照: grepSyntax_==Basicかつ起点がDirectory/Fileの場合
  // だけ、この経路(currentMode_を一切変えない)へ分岐する。
  //
  // activateGrepInPlace(): grepMode_.matches()(既にexecuteGrep()内で
  // 検索済み)を読み、0件ならDirectoryMode::reportGrepNoMatchMessage()/
  // FileMode::reportGrepNoMatchMessage()(LOCATE自身の"Updating
  // database..."と同じ体裁、2026-09-10改訂)で一時的なメッセージを
  // 出してfalseを返す(currentMode_/セッション状態は一切変えない)。
  // 1件以上ならgrepInPlace*状態を設定し、最初のマッチへジャンプする。
  bool activateGrepInPlace();
  // deactivateGrepInPlace(): grepInPlaceActive_をfalseに戻し、
  // directoryMode_/fileMode_(grepInPlaceOriginMode_に応じて)の
  // setGrepInPlaceOverlay(false)を呼ぶ。セッションが元々非アクティブ
  // なら無害(DirectoryMode::clearEditError()と同じ「無条件に呼んでも
  // 安全」という慣習)。Application::handleCommand()冒頭から、
  // in-placeセッション中に^N/^P・マーク系以外のコマンドが来た時に
  // 呼ばれる。
  void deactivateGrepInPlace();
  // 2026-09-19追加、直接のユーザー報告(PTYハーネスでの再現確認、
  // Emacsとの比較実演: "本来マッチするのは*ひとつだけなので**の行は
  // ^Nを2回押さないと次行へ行かないはずが1回で行ってしまう")。
  // GrepMode::searchInProcess()自身のコメント参照: matches_(≒
  // grepInPlaceMatches_)は「行」単位(1件のGrepMatchが1行に対応し、
  // その行内の複数一致は同じGrepMatch::matchSpansへまとめて入る)
  // なので、それをそのまま^N/^Pのナビゲーション単位に使うと、同じ
  // 行内の複数マッチが1回のステップでまとめてスキップされてしまう
  // (Emacsは逆に一致した位置ごとに1歩ずつ進む)。GrepOccurrenceは、
  // 行単位のGrepMatchを「一致した個々の出現位置」まで展開した、
  // ナビゲーション専用のフラットな単位。
  struct GrepOccurrence {
    int sourceLineIndex = -1;
    std::pair<int, int> span{-1, -1};
  };
  // buildGrepInPlaceOccurrences(): GrepOccurrence自身の宣言コメント
  // 参照。行単位のGrepMatch群を、その中のmatchSpansまで展開した
  // フラットな出現位置リストへ変換する(matchSpansが空の行は
  // ContentView::setGrepMatchHighlight()自身のフォールバックと同じ
  // 理屈で、行全体を1件の出現として扱う)。
  std::vector<GrepOccurrence> buildGrepInPlaceOccurrences(const std::vector<GrepMatch> &matches) const;
  // jumpToGrepInPlaceMatch(): `delta`は+1(Next、^N)/-1(Previous、^P)。
  // grepInPlaceOccurrences_.size()(2026-09-19、行単位のgrepInPlaceMatches_
  // から出現単位のこちらへ変更――GrepOccurrence自身の宣言コメント
  // 参照)で循環(wrap)させてgrepInPlaceIndex_を更新し、
  // activeView().setCursorIndex()+centerOnCursor()+redraw()。File起点
  // では追加でsyncVisibleCursor()も呼ぶ(openSelectedGrepMatch()自身の
  // File分岐が同じ理由でこれを呼んでいるのと同じ、行全体のハイライト
  // を持たないFileモードでのキャレット表示のため)。ハイライトも
  // setGrepMatchHighlight()を呼び直し、現在位置(HighlightCurrent)を
  // 新しい出現へ移す。
  bool jumpToGrepInPlaceMatch(int delta);
  // 2026-09-10追加、直接のユーザー指摘("^N連打で最下行へ到達すると
  // 循環処理により最上行へと移動するのはいいとして、動きが今イチ
  // わかりにくい...EGREPと同じく行数情報を含ませて欲しい"):
  // activeView().setPositionTextOverride()を「マッチ番号/総マッチ数」
  // (1始まり)に差し替える――activateGrepInPlace()/jumpToGrepInPlaceMatch()
  // 自身のコメント参照。
  void updateGrepInPlacePositionText();
  // grepInPlaceMatchLabel()自身のコメント参照(application.cpp、
  // 2026-09-11追加)。
  std::string grepInPlaceMatchLabel() const;
  // 2026-09-10追加、直接のユーザー依頼(当初はESC iキー、2026-09-19に
  // プレーンな^Sへ一本化――design経緯はapplication.hpp自身の
  // Command::StartIncrementalGrep宣言コメント参照)。
  //
  // startIncrementalGrepPrompt(): startGrepPrompt(Fixed)とほぼ同じ
  // (grepSyntax_/grepReturnMode_の設定、GrepPatternプロンプトを開く)
  // だが、incrementalGrepActive_も立てる。起点は
  // isGrepInPlaceEligibleMode()が真のモードに限られる(in-place方式
  // 自体のスコープに合わせている――2026-09-11にDirectory/Fileから
  // MAN/LOCATE/DU/ARC/SHELL/CUT(内容)/LIST等へ拡張済み)。
  bool startIncrementalGrepPrompt();
  // liveIncrementalGrepSearch(): feedPromptByte()の薄いラッパー
  // (自身の宣言コメント参照)から、incrementalGrepActive_中に
  // promptEditor_のテキストが実際に変化するたびに呼ばれる。
  // GrepMode::searchInProcess()(サブプロセス無し)でパターンを
  // その場で検索し直し、成功すればactivateGrepInPlace()(既存の
  // ハイライト+ジャンプ+オーバーレイの仕組みをそのまま再利用)を
  // 呼ぶ。activateGrepInPlace()/deactivateGrepInPlace()はどちらも
  // 最後に(通常のモードラインを再描画する)target.redraw()を呼ぶ
  // ――生きたプロンプトの行(画面最終行、まさに同じ行)を上書きして
  // しまうため、その直後にdrawPromptLabel()+promptEditor_.redraw()
  // でプロンプト自身の表示を再度上に描き直す。
  void liveIncrementalGrepSearch();
  bool returnFromGrep();
  bool returnToGrepResults();
  bool returnFromCutContent();
  bool returnFromCut();
  bool returnToCutResults();
  // 2026-09-29追加、cutPositionKey_自身のコメント参照。ファイルCUTの
  // 一覧のカーソル位置を、「パス "パターン"」をキーに履歴(MODE章)へ
  // 保存/復元する。
  // contentCursor >= 0: 区間の中身(CutContent)を見ている時のそのカーソル行。
  // 終了時だけ渡し(saveResumableCutState())、CursorPosition::lineNumberへ
  // +1して入れる(0/-1は「中身は開いていない」)――次回起動の再開で使う。
  void saveCutFilePosition(int contentCursor = -1);
  void restoreCutFilePosition(const std::string &key);
  // 2026-10-01追加(同日訂正): ファイルCUTを開き直した時に開く区間(一覧の行)と、その中身の
  // カーソル行。restoreCutFilePosition()が保存から決める――区間の中身を見たまま離れていれば
  // その区間とその行、位置の記録が無い最初は最上行の区間(0, 0)、一覧のまま離れていれば無し
  // (一覧)。reopenLastCutContent()がその区間を開いてその行へ。
  std::optional<std::pair<int, int>> cutLastContent_;
  void reopenLastCutContent();
  // 2026-10-01追加、直接のユーザー報告("CUTで`;`のあと`:`で戻ってもカーソル位置が保存されていない"):
  // 区間の中身から一覧へ戻る時(returnFromCutContent()、;/:もこれを通る)の、区間(一覧の行)ごとの
  // 中身のカーソル行と画面の一番上の行。openSelectedSegment()が同じ区間を開く時に戻す。同じ区間の
  // 並び(CutMode::generation())の間だけ使う。
  std::map<int, std::pair<int, int>> cutSegmentViews_;
  int cutSegmentViewsGeneration_ = -1;
  // ^X^C等で終える直前に呼ぶ: ファイルCUTの一覧/区間の中身を見ていたら、
  // その位置を保存する(startResumingSession()が同じ画面を作り直す)。
  void saveResumableCutState();
  // 1回のキー/コマンドの処理でファイルCUTの一覧・区間の中身から別の画面へ
  // 移ったら(^G^G以外、例えば^J^Kで(ls)へ)、離れる前の位置を保存する――
  // ^J^J等で開き直すと区間の中身まで戻る(openRecentFileEntry())。
  struct CutLeaveSnapshot {
    ApplicationMode mode;
    std::string key;
    int contentCursor;
  };
  CutLeaveSnapshot cutLeaveSnapshot() const;
  void saveCutPositionIfLeft(const CutLeaveSnapshot &before);
  // ファイルCUT(^Cfとその履歴)の^Gの戻り先: (ls)かFILEから起動した
  // 場合は(ls)(FILEの中身はCUTしたファイルに差し替わっているため)、
  // それ以外(^T画面等)は起動元。
  static ApplicationMode cutFileReturnModeFor(ApplicationMode origin);
  bool returnFromMan();
  bool returnFromLocate();
  bool returnFromFileToLocate();
  bool returnFromArchiveContent();
  bool returnFromArchive();
  bool returnFromShell();
  bool returnFromDu();
  bool returnToDuResults();
  bool redisplayDu();
  bool returnFromListBrowser();
  // ^O^C(M_copy、「COPY」): activeContentView().lines()を
  // copyMode_へスナップショットし、ApplicationMode::Copyへ切り
  // 替える。なぜスナップショット状態自体がここではなくそこに住む
  // のかについてはCopyMode自身のクラスコメント参照。既にCopyモード
  // の場合はfalse(何もしない)。
  bool startCopy();
  // Copyモードからのどんな終了キーも(この移植自身の慣習: ^Gのみ。
  // 「小さな移動許可リストの外側のどんなキーでも、そのキーを再生
  // する」というlegacy自身のものとは異なる――この移植が、legacyの
  // 関数ごとの再生機構の代わりに、ここの他の全てのサブモードが
  // 既に使っている統一されたCommand::Backの慣習に従う理由について
  // はdesign.md自身の注記参照)。copyReturnMode_を復元し、Copy
  // モードにいる間一度も触れられなかったそれ自身のContentViewを
  // 再描画する。
  bool returnFromCopy();
  ContentView &activeView();
  // feedByte()/handleCommand()の実体(2026-09-25、stdinPagerActive()の
  // 前後比較で包むために分離――それ自身のコメント参照)。
  bool feedByteImpl(int byte, std::optional<KeyInput::SpecialKey> special);
  // usilicrcのKEY節(2026-09-25): feedByte()が置き換えた後の本体の入口と、
  // 置き換えを効かせる範囲の判定、照合の途中状態。
  bool feedUntranslatedByte(int byte, std::optional<KeyInput::SpecialKey> special);
  KeyBindings::Context keyBindingContext() const;
  KeyBindings::Pending keyBindingPending_;
  bool handleCommandImpl(Command command);
  bool stdinPagerActive() const;
  void discardStdinPager(bool returnsToShell);
  int stdinPagerCheckDepth_ = 0;
  std::function<bool()> interruptPeeker_;  // setInterruptPeeker()参照
  bool keyInterruptRequested();  // それ自身のコメント(application.cpp)参照
  ContentView &viewForMode(ApplicationMode mode);
  void layoutViews();
  std::vector<std::string> collectTargetPaths() const;
  // (ls)以外の一覧の選択(2026-09-27、list_marks.hpp参照)。activeListMarks()は
  // 現在のモードが選択できる一覧ならその選択の集合と行数を、そうでなければ
  // nullptrを返す。listTargetPaths()は選択(無ければハイライト行)のうち実在
  // するものの絶対パス(末尾の'/'は除く)。
  ListMarks *activeListMarks(int *size);
  std::vector<std::string> listTargetPaths() const;
  std::string cpMvRmSourceDirectory() const;
  // Fileモードにいる間のCommand::PagerNextFile/PagerPreviousFile/
  // PagerFirstFile/PagerLastFileと'['/']'で共有される(Command
  // enum自身のコメント参照)――pagerFileList_[newIndex]を読み込み、
  // pagerFileIndex_を更新する。newIndexが範囲外、既に現在の
  // インデックス、またはそのインデックスのファイルが読み込みに
  // 失敗した場合はfalse(純粋に何もしない、現在のファイルには
  // 手を付けない)――一覧の端でのless自身の":n"/":p"
  // ("No next/previous file")に合わせている、循環はしない。
  bool navigateToPagerFile(int newIndex);
  // (startListBrowser(): 2026-09-20削除、application.cpp自身の削除
  // コメント参照。)
  // ToggleHistoryView自身のコメント参照。
  bool toggleListBrowserHistory();
  std::vector<std::string> sortedUniqueRecentEntries(RingCategory category) const;
  std::vector<std::string> egrepListItems() const;  // EGREPの一覧(2026-09-27)
  int egrepListCursorIndex(const std::vector<std::string> &items) const;
  // isGrepInPlaceEligibleMode()自身のコメント参照(application.cpp、
  // 2026-09-11追加)。
  bool isGrepInPlaceEligibleMode(ApplicationMode mode) const;
  bool startGrepPrompt(GrepSyntax syntax);
  bool startCutPrompt(GrepSyntax syntax);
  bool startCutFilePrompt(GrepSyntax syntax);
  bool startManPrompt();
  bool startManList();
  bool startProgramPrompt();
  bool startProgramList();
  bool startLocatePrompt();
  bool runLocateUpdateDb();
  // runLocateUpdateDb()が起動した切り離された`updatedb`がまだ実行
  // 中の間は>0(awaitingLocateUpdateDb()自身のコメント参照)、それ
  // 以外は-1。もはやKeyInputとは全く関係が無い――以前のpoll()
  // ベースのバージョン自身のkeyInput_メンバーは、それと一緒に無く
  // なっている。
  pid_t locateUpdateDbPid_ = -1;
  // 2026-09-19追加、awaitingDu()/pollDuCompletion()自身の宣言コメント
  // 参照。上のlocateUpdateDbPid_と同じ役割(実行中かどうかのフラグ)
  // だが、`du`は捨てずに読める標準出力を持つので、追加でduOutputFd_
  // (ChildProcess::startCapturing()自身のfd、O_NONBLOCK)と
  // duPendingOutput_(そこから今まで蓄積した分)、duPendingPath_
  // (loadUsageFromOutput()へ最終的に渡す対象パス、runDuOnCurrentFolder()
  // が計算済みのfolder)を保持する。
  pid_t duPid_ = -1;
  int duOutputFd_ = -1;
  std::string duPendingOutput_;
  std::string duPendingPath_;

  // awaitingArchiveLoad()自身の宣言コメント参照。duPid_/duOutputFd_/
  // duPendingOutput_/duPendingPath_と全く同じ役割。
  pid_t archiveLoadPid_ = -1;
  int archiveLoadOutputFd_ = -1;
  std::string archiveLoadPendingOutput_;
  std::string archiveLoadPendingPath_;
  // 完了/中断時、openSelectedEntry()自身の既存の後始末(archiveParentStack_
  // のクリア+RecentFileへのpushRecent())を再現するために覚えておく
  // 必要がある――runDuOnCurrentFolder()相当のこの起動元はopenSelectedEntry()
  // 1箇所だけなので、DU/LOCATEのような戻り先スタックの計算は不要。
  bool startArchiveLoadAsync(const std::string &path);
  // 2026-09-16追加、直接のユーザー依頼("debian-keyring...tar.xzを^Mし
  // た場合の^Gによる中断を^O^M/^[^Mへも適用してほしい"):
  // openSelectedEntryNoWrap()自身のARCHIVE分岐(^O^M、対話的Archive
  // Modeではなく選択不可のプレーンなtvfページャー表示)向けの、
  // startArchiveLoadAsync()と全く同じ非同期起動――ただし完了後の着地
  // 先がArchiveMode(finishArchiveLoadAsync()の既定の分岐)ではなく
  // FileMode自身のpager表示になるため、archiveLoadForPager_で分岐する。
  // 新しい非同期の仕組み一式(pid/fd/バッファ/^Gゲート)をまるごと複製
  // するのではなく、既存のarchiveLoadPid_一式・awaitingArchiveLoad()・
  // pollArchiveLoadCompletion()・dispatchByte()自身の^Gゲートをそのまま
  // 共有し、finishArchiveLoadAsync()内の着地先だけを条件分岐させる。
  bool startArchivePagerLoadAsync(const std::string &path, const std::string &directory);
  bool archiveLoadForPager_ = false;
  std::string archiveLoadPendingDirectory_;
  std::string archiveLoadPendingBadge_;
  void finishArchiveLoadAsync();
  bool startShell();
  // startShell()とK-61/K-62/K-63/K-64で共有される: startShell()
  // 自身の「既にShellモード」という拒否無しに、Shellモードを画面
  // に表示する(必要ならshellプロセスを起動する)――素のESC-s/^Xsの
  // 入場とは異なり、これら4つのキーは、Shellが既にアクティブで
  // あったかどうかに関わらず動作することを意図している(legacy
  // 自身のm_w()の呼び出し箇所にも、そのようなモードガードは無く、
  // 別のis_inter()のbusyチェックだけがある)。
  bool ensureShellActive();
  // 2026-09-06追加、直接のユーザー報告("最初のman ls用のパイプが
  // 解放されていない...（stdin）を抜けたら同プロセスをkillしてパイプを
  // 解放して欲しい"): SHELLの生きたPAGERハンドオフセッション
  // (fileMode_.pagerReturnsToShell())をShellへ戻る形で離れる3箇所
  // (Command::Back自身のFileモード分岐、Command::QuitPager、
  // ensureShellActive())全てで全く同一だった「currentMode_ = Shell;
  // scrollback().redraw(); redrawPromptRow();」に、FileMode::closeStream()
  // の呼び出しを足して1箇所へ集約した――呼び出し元自身のコメント
  // 参照: 元々の"(stdin)の存在は画面を離れた時点で消失でいい"という
  // 設計はUI上のcurrentMode_の切り替えだけを指しており、その裏の
  // OS側のfd(SHELL側のPAGERハンドオフの受信ソケット)はどの箇所でも
  // 一度も閉じられていなかった――閉じないままだと、その先で待って
  // いる子プロセス(man|usilic --usilic-pager、tryHandoffToParentShell()
  // 自身のコメント参照)がbashの前景ジョブとして永遠にブロックされた
  // ままになり、SHELLプロンプトが戻ってきたように見えても実はまだ
  // その古いパイプラインの中で、次に打ち込んだコマンド(2回目のman ls
  // 等)が新しいプロンプトとしては一切解釈されない――ユーザー自身の
  // 報告通り「何も起きない」。streamFd_(こちらの読み込み端)を閉じる
  // ことで、書き込み端(子プロセス)は次のwrite()でEPIPE/SIGPIPEを
  // 受けて自然に終了し、man/groff自身もそれで芋づる式にSIGPIPEを
  // 受けて終了する――標準的なUnixのパイプの後始末そのもの。
  bool returnPagerToShell();
  // shellReturnName_のための、`mode`に一致するどの*Mode自身の
  // 「現在の名前」アクセサでも、そのスナップショット(自身のコメン
  // ト参照)。既にそのようなアクセサを持ち「かつ」Shellへの妥当な
  // 入口であるモード(Directory/File/Man/Archive/ArchiveContent/
  // Du)だけがカバーされている。他の全て(Grep/Cut/CutContent/
  // Locate/ListBrowser――それ自身の単一の「名前」文字列を現在
  // 公開しているものは1つも無い)は、推測するのではなくtrailing
  // フィールドを単に省略する""へフォールバックする。
  std::string previousModeName(ApplicationMode mode) const;
  // 2026-09-19追加、直接のユーザー方針の転換("MANを使ってみて思うのは、
  // 通常ファイルを閲覧中にMANを発動した場合、そこへ戻る方が自然な気
  // がする。読書でも注釈参照後は元の場所へ戻るように"): MAN/LOCATE/DU
  // (HELP.jp自身の言う「通常ファイルかディレクトリ」棲み分け――
  // HELP/KEY/SHELLの「全モード」棲み分けとは区別される、直接のユー
  // ザーの整理)が新規に発動された時の戻り先を決める。currentMode_
  // (発動時点でたまたまいたモード)をそのまま使うのではない――
  // GREP/CUT/LOCATE/DU/KEY等、それ自体「通常ファイルでもディレクトリ
  // でもない」画面から発動される場合があり得るため、その発動元自体の
  // さらに発動元を辿り、最初に見つかる通常File/Directoryまで遡る
  // (ユーザー自身の比喩: 注釈の中でさらに別の注釈を参照していたら、
  // 両方閉じて元の本文まで戻る――1段ずつではなく、通常File/Directory
  // に行き着くまで飛び越す)。Archive/ArchiveContentは専用の
  // archiveParentStack_機構(*ReturnMode_のような単純な変数ではない)
  // を持つが、returnFromArchive()自身の確立された挙動により最終的に
  // 常にDirectoryへ行き着くので、ここではその終着点を直接返す。
  // 循環を踏む可能性は無いはずだが(各*ReturnMode_は常に「より外側」
  // を指す)、万一の保険として上限付きのループにしてある。
  ApplicationMode nearestFileOrDirectoryAnchor() const;
  // anchorSnapshotFor()/restoreAnchorContent()自身のapplication.cppのコメント
  // 参照――manAnchor_/locateAnchor_/duAnchor_(AnchorSnapshot、この
  // ファイル冒頭の名前空間スコープの宣言参照)を凍結/復元するための
  // 対の小さなヘルパー。
  AnchorSnapshot anchorSnapshotFor(ApplicationMode anchor) const;
  void restoreAnchorContent(ApplicationMode anchor, const AnchorSnapshot &snapshot);
  // "CUT:<現在のセグメント自身のラベル>"(先頭の空白は取り除か
  // れる)、例えば"CUT:1.  Introduction"――previousModeName()自身
  // の裸の「CUT」(その関数は描画すべきインスタンスごとの状態を
  // 持たず、単なるモードのenumだけである)よりも、CutContentに
  // 対するより具体的な戻り先の説明。cutContentView_自身の確立
  // された「CUT:<ラベル>」モードライン慣習(自身のプロバイダの
  // コメント参照)を反映している――ここで(2026-08-24)
  // grepView_自身の戻り先フィールドと、drawPromptLabel()自身の
  // プロンプト時の「destinationDirectory」フィールドの両方に
  // よって共有される。両方とも、以前は代わりに裸の、より具体的で
  // ないpreviousModeName(CutContent)へフォールバックしていた。
  std::string cutContentReturnLabel() const;
  // 「一覧」ベースの履歴ピッカー自身のモードラインのtrailingフィー
  // ルドに、各RingCategoryに対して表示されるラベル――
  // listBrowserView_.setModeLineFieldsProvider()自身のコメント
  // (コンストラクタ)参照。
  std::string ringCategoryLabel(RingCategory category) const;
  // K-60(^Xs、M_hup): 現在のSHELLの子(もしあれば、
  // Shell::terminate()のSIGHUP+wait経由)を殺し、新しいものを起動
  // し、Shellモードに入場する。menu.c自身のM_hupケースからの移植:
  // legacyのis_inter(F12,...)はshellとそのIPC中継プロセスの両方に
  // SIGHUPを送り両方が死ぬまでブロックする。これは、この移植の
  // より単純な単一プロセスのpty設計(なぜここに中継プロセスが無い
  // かについてはShell自身のクラスコメント参照)では、
  // Shell::terminate()が既にそれと等価なことを行う。legacy自身の
  // is_inter(NULL,...)ガードと同様、引き渡された対話的なPROGRAM
  // がまだ実行中の間(Shell::busy())は拒否される――現在それを
  // 制御している`less`のようなものの足元でptyを再起動すると、
  // その子を取り残してしまう。
  bool shellRestart();
  bool saveBufferToFile();
  bool shellChangeDirectory();
  bool shellRunLsL();
  bool shellInsertName();
  bool shellInsertBufferContent();
  // K-67(^X^Ke): errorLogPath_をFileモードで開く。プロンプト駆動
  // ではない――Directoryモード自身のアーカイブ自動検出のような即時
  // アクション。まだファイルが存在しない(今回のセッションで何も
  // ログされていない)、または開けない場合はfalse。
  bool executeShowErrorLog();
  bool executeOpenDataDirectory();  // K-96
  bool openExternallyIfApplicable(const std::string &path);
  static const std::set<std::string> &quickLookSupportedTypes();
  bool openListEntrySpecial(bool raw);  // ^O^M/^[^Mの一覧版(2026-09-27)
  bool runQuickLook(const std::vector<std::string> &paths);
  bool openEpubSegmentImage(const CutSegment &segment);  // 自身のコメント(application.cpp)参照
  bool openEpubLineImage(const ContentView &view);       // 同上
  bool openEpubImageIn(const std::string &text);          // 同上
  bool openEpub(const std::string &path);
  bool openEpubQuietly(const std::string &path);  // openEpub()の本体(画面への出力を止めた中で)  // EPUBモード、自身のコメント(application.cpp)参照
  // 縦書き(rebuildEpubVertical()自身のコメント参照)
  bool epubVerticalActive() const;
  // 2026-09-29追加、直接のユーザー依頼(EPUBの段階3、"扉...や奥付(横書き)等、章毎の
  // 縦横使い分け"): 縦書きの本(epubBookVertical())の中の、横書きの範囲
  // (EpubLayoutHints::horizontal、奥付等)。その範囲を読んでいる間(epubHorizontalSection_)は
  // 横書きのfileView_で見せ、epubVerticalActive()はfalse。範囲の外へ出たら
  // (leaveEpubHorizontalIfOutside()、キー/コマンドの処理の後)縦書きへ戻る。
  bool epubBookVertical() const;
  std::optional<std::pair<int, int>> epubHorizontalRangeAt(int rawLine) const;
  void enterEpubHorizontal(const std::pair<int, int> &range, int rawLine);
  bool maybeEnterEpubHorizontal(int fromRaw, bool forward);
  // 縦書きの1列の送り/戻り(rebuildEpubVertical()の空の列・扉のページ参照)。返り値は
  // {右端の列(-1なら今のまま), 列カーソル}。
  std::pair<int, int> epubVerticalStep(int cursor, bool forward, bool wholeImagePages = false) const;
  // 列cが空の列/扉のページなら、その後の(forward)/前の本文の列、または扉の書名の列へ
  // 寄せ、右端を画面の区切りに揃える(ページ送り・飛び先用)。
  void snapEpubVerticalToPage(bool forward);
  // 右端の列を1列ずつdelta列ずらす(ホイール・ドラッグ・1列スクロール)。1列ずつの規則は
  // epubVerticalStep()と同じ(空の列を飛ばして章の頭を右端に、扉はページごと)。描き直し
  // はしない。右端の列が動いたらtrue。
  bool stepEpubVerticalFirst(int delta);
  bool selectAdjacentEpubVerticalLink(int delta);
  // 横書きのEPUBの本文(横書きの本・縦書きの本の奥付等)のリンク(下線の並び)。2026-09-30、
  // 直接のユーザー依頼("奥付でも↑↓でリンクを選べるようにしてよ")。
  struct EpubHorizontalLink {
    int index = 0;  // fileView_の行
    int start = 0, end = 0;  // バイト
    int raw = 0;
    int ordinal = 0;  // 行(元の行)の中での番号(EpubLayoutHints::lineLinks)
  };
  std::vector<EpubHorizontalLink> epubHorizontalVisibleLinks() const;
  std::vector<EpubHorizontalLink> epubHorizontalLinks(int from, int to) const;
  bool epubHorizontalLinksActive() const;
  bool selectAdjacentEpubHorizontalLink(int delta);
  bool activateEpubHorizontalLink(const EpubHorizontalLink &link);
  bool activateSelectedEpubHorizontalLink();
  bool clickEpubHorizontalLink(int index, int column);
  bool openEpubReturningTo(const std::string &path, ApplicationMode returnMode);
  void syncEpubHorizontalPresentation();
  void applyEpubChapterLabels();
  ContentView &grepViewForMode(ApplicationMode mode);
  std::vector<ContentView *> grepModeLineViews(ApplicationMode mode);
  bool redrawEpubVerticalForGrep(ContentView &target);
  std::pair<int, int> epubVerticalShownRange(int first) const;
  // 縦書きの窓(右端の列first)に見える画像ページの置き場所(2026-10-03、rebuildEpubVertical()参照)。
  std::vector<Display::ContentImage> epubVerticalImagesFor(int first, int shownFrom, int shownUntil) const;
  std::optional<std::string> epubImageBytes(const std::string &key) const;  // 画像のプロバイダ(コンストラクタ参照)
  std::map<std::string, std::pair<int, int>> epubImageSizes_;  // keyごとの画像の幅・高さ(0,0は読めない)
  // 縦書きの列の端(deltaの向き)の先(前)に横書きの範囲があればそこへ入る(描き直す)。
  bool enterEpubHorizontalAtEdge(int delta);
  bool enterEpubHorizontalAtBookEnd();  // ^[<で本の末尾の横書きの範囲(奥付等)の最下行へ
  bool leaveEpubHorizontalIfOutside();  // 縦書きへ戻ったらtrue

 public:
  // 2026-09-29追加、直接のユーザー報告("奥付...ホイールを回すと縦方向の慣性スクロールに
  // なる。逆方向へスクロールを継続させると、奥付の前の章以前も横書きのまま...jかkを押すと
  // 縦書きへ切り替わる"): GUIの毎フレームでも横書きの範囲から出たかを確かめる(慣性・
  // ホイール・ドラッグはキー/コマンドを通らない)。縦書きへ戻ったらtrue(呼び出し元は上下の
  // 慣性を止める)。
  bool syncEpubHorizontalSection() { return leaveEpubHorizontalIfOutside(); }
  // 縦書きの本の中の横書きの範囲(奥付等)を見ている間true(app/main.cppが慣性を縦横の間で
  // 引き継ぐのに使う)。
  bool epubHorizontalSectionActive() const { return epubHorizontalSection_.has_value(); }
  // 上下の慣性で横書きの範囲から前へ出て左右の慣性に引き継ぐ時(app/main.cpp): 前の章の
  // 最後の列(今の列カーソル)を画面の左端に置き、そこから右へ遡れるようにする。
  void alignEpubVerticalCursorToLeftEdge();

 private:
  std::optional<std::pair<int, int>> epubHorizontalSection_;
  int fileRawLineAtCursor() const;
  void rebuildEpubVertical(int rawLine);
  void showEpubVerticalRaw(int rawLine);
  void setEpubVerticalFirst(int first);
  void setEpubVerticalView(int first, int cursor, std::optional<int> link = std::nullopt);
  int epubVerticalRows() const;
  int epubVerticalCurrentRaw() const;
  void syncEpubVerticalToFile();
  std::optional<bool> handleEpubVerticalCommand(Command command);
  // K-100(^Oe、2026-09-29): .epubをARCで開く。FILE履歴には「パス/」で積み、
  // バッジは"(unzip)"(EPUBモードで開いた「パス」は"(epub)")――履歴の一覧から
  // どちらのモードで開いたか分かるように(直接のユーザー依頼)。
  bool openEpubAsArchive(const std::string &path);
  bool openSelectedEpubAsArchive();
  bool clickEpubVertical(int row, int column);
  struct EpubVerticalTarget {
    std::optional<int> link;  // リンク先の元の行番号
    std::string imageLine;    // "[image: ...]"の行(linkが無い時)
    std::string url;          // http(s)のリンク(2026-09-30、openEpubWebLink())
  };
  bool openEpubWebLink(const std::string &url);
  bool openEpubWebLinkAtCursor();
  std::optional<EpubVerticalTarget> epubVerticalColumnTarget(long column) const;
  bool activateEpubVerticalColumn(long column);
  bool epubVertical_ = false;
  std::vector<EpubVerticalColumn> epubVerticalCols_;  // 組んだ列の並び(右から左へ読む順)
  // 章(扉のページを含む)の区切りの列(rebuildEpubVertical()のpadToPage()の位置、昇順)。
  // 2026-09-30追加(setEpubVerticalView()参照): 画面には右端の列の章だけを描く。
  std::vector<int> epubVerticalBoundaries_;
  // 列カーソルの行の選んでいるリンクの番号(EpubVerticalCell::link、-1で無し)と、その行
  // (setEpubVerticalView()参照)。
  int epubVerticalLink_ = -1;
  int epubVerticalLinkRaw_ = -1;
  std::optional<EpubHorizontalLink> epubHorizontalLink_;  // 横書きの選んでいるリンク
  // ^X^Xの戻り先の元の行(縦書きの本で、横書きの範囲との間をリンクで行き来した時。縦書きの列の
  // 戻り先はepubVerticalMark_)。2026-09-30。
  std::optional<int> epubJumpRaw_;
  // 横書きのリンクから飛ぶ前の、選んでいたリンクと画面の先頭の行(^X^Xでそのまま戻す、
  // restoreEpubJumpOrigin())。2026-09-30。
  struct EpubJumpOrigin {
    EpubHorizontalLink link;
    int top = 0;
  };
  std::optional<EpubJumpOrigin> epubJumpOrigin_;
  bool restoreEpubJumpOrigin();
  std::string epubVerticalPath_;  // epubBookVertical()参照
  int epubVerticalFirst_ = 0;                         // 右端に置いている列
  int epubVerticalCursor_ = 0;                        // 列カーソル(setEpubVerticalView()参照)
  std::optional<int> epubVerticalMark_;               // ^X^Xの行き来先の列
  // 2026-09-29追加、直接のユーザー依頼("EPUBで縦書きになったらCUT一覧も縦書きに
  // して欲しい"): 縦書きの間の章の一覧。章の見出し1つを論理行1つ(rawLineNumber =
  // 章の番号+1)として本文と同じ組版で列に並べ、選んでいる章(cutView_のカーソル)の
  // 最初の列を反転表示する。cutView_は裏で持ち続け、[Enter]・^G等はそのまま働く。
  bool epubTocVerticalActive() const;
  void refreshEpubTocVertical(bool keepFirst = false);
  void captureEpubVerticalEdges(ContentView &view, const std::vector<EpubVerticalColumn> &cols, int first,
                                int highlight, int shownFrom = 0, int shownUntil = 1 << 30, int highlightLink = -1);
  void selectEpubTocVertical(int segment);
  std::optional<bool> handleEpubTocVerticalCommand(Command command);
  bool clickEpubTocVertical(int row, int column);
  std::vector<EpubVerticalColumn> epubTocCols_;
  int epubTocFirst_ = 0;  // 右端に置いている列
  bool openEpubTocEntry(const CutSegment &segment);       // 同上
  bool returnToEpubToc();                                 // 同上
  bool startEpubPrompt();                                 // K-98(^Je)
  bool startEpubList();                                   // K-99(^J^E)
  std::string listBrowserCategoryLabel() const;           // startEpubList()参照
  bool epubPathPending_ = false;  // ^Jeのパス入力中(PathInputの投入をopenEpub()へ回す)
  bool epubListPending_ = false;  // ^J^E: 次のstartPromptHistoryRecall()をEPUBだけにする
  bool executeOpenWorkList();       // K-97
  // K-68(^X^Kl): Shell::scrollback()の「現在の」行をlogFilePath_
  // へ(以前のスナップショットを切り詰めて)書き込み、その後その
  // ファイルをFileモードで開く。これはlegacy自身の
  // write(sfd,...)(shell.cのread_line()参照)のような、Shellのpty
  // 出力の継続的な複写ではなく、要求に応じたスナップショットで
  // ある――それを構築することは、それ以来画面外へスクロールした
  // かクリアされた出力を見るという見返りのために、ログファイルの
  // fdをShellのバイトごとの出力処理に配管することを意味するが、
  // このより単純なスナップショットは既に「shellがちょうど何を
  // 印字したか」というよくあるケースに対してそれを届けている。
  // Shellが一度も起動されていない(スナップショットすべきものが
  // 無い)、またはファイルが書き込み/開けない場合はfalse。
  bool executeShowShellLog();
  // executeShowShellLog()/executeStartLog()で共有される――自身の
  // 定義コメント(application.cpp)参照。
  bool refreshShellLogFile();
 public:
  // 今のSHELLの内容をlogFilePath_(~/.usilic/shell)へ、前のセッションの分を残したまま書く
  // (2026-10-03、定義のコメント参照)。終了時にmain()からも呼ぶ。
  void saveShellLog();

 private:
  // saveShellLog()が最後に書いた、このSHELLセッションの分。次はファイル中のこれを置き換える。
  std::string shellLogBlock_;
  // ESC l("LOG"): 自身の定義コメント(application.cpp)参照。
  bool executeStartLog();
  // K-0(^T): 短いステータス/バージョン情報画面を構築する――
  // バージョン、usilicrc/historyファイルのパス(str.cのprint_usilic()
  // が「同じ」2つのフィールド、「Startup file」/「History file」を
  // 表示する)、user@host、そしてERROR/LOGのパス(K-67/K-68)――
  // そしてそれをFileモード経由で開く。
  // legacy自身のprint_usilic()より意図的に狭い: 「Startup mode」
  // (ファイルマネージャ対ページャー)と「Command line」(呼び出し
  // 元のargv)は両方とも省略されている――この移植にはまだページャー
  // 専用の起動経路が無く(main.cppは引数に関わらず常に
  // Application::start()をDirectoryモードへ呼ぶ)、Applicationは
  // 現在それ自身のargvを全く受け取らないので、推測された/ハード
  // コードされたプレースホルダーではなく、どちらのフィールドに
  // 対しても報告すべき本物のものが無い。materializeArchiveTempFile()
  // 自身のパターンで、使い捨てのmkdtemp()一時ファイルへ書き込まれ
  // る。FileMode::loadFile()は本物のパスを必要とし、このテキストは
  // ERROR/LOGのような他の自然なディスク上の住処を持たないため。
  bool executeShowUsilicBanner();
  // Command::Suspend(^Z)自身のコメント参照。
  bool handleSuspend();
  // ^Tバナー自身の合成された裏付けファイルは
  // "<homeDirectory>/.usilic/usilic"の下に住む――tempRootDirectory_
  // では「ない」(2026-08-18の修正、実際の報告: ERROR/SHELL/WORK/
  // ARCとは異なり、バナーはバックグラウンドで/継続的に書き込まれ
  // ることが決して無いので、それは項目338のPIDベースの再編成が
  // 修正していた複数インスタンスの衝突の一部では実際には全く無
  // かった。それでもtempRootDirectory_を無理に通すと、単にそれ
  // 自身のモードラインに、紛らわしく戻り先のように見えるフィール
  // ドとして「/tmp/.usilic.<pid>/」を漏らすだけだった)。専用の
  // メンバーではなく、usilicrcPath_自身のディレクトリを共有する。
  std::string bannerFilePath() const;
  // ~/.usilic/data(旧saved、2026-09-27改名、直接のユーザー依頼: ^J^Odの
  // 'd'に揃えるため)。^X^Wの名前付き保存先・^Oaの既定の展開先・^J^Odが
  // 開くディレクトリ。無ければ作成する。
  std::string dataDirectory() const;
  // 2026-09-25追加、直接のユーザー報告("Startup file:のusilicrcは
  // -c起動でも無反応"): INFOの「Startup file」行に出す/開くパス。
  // usilicrcPath_(~/.usilic/usilicrc)は~/.usilic自体の所在(saved/や
  // バナーの置き場所)を求めるためにも使われるので書き換えず、起動時に
  // main.cppが実際に読むのと同じ順(~/.usilic/usilicrc →
  // ~/.local/lib/usilic/usilicrc → /usr/local/lib/usilic/usilicrc)で
  // 最初に実在するものを返す。どれも無ければusilicrcPath_。
  std::string startupFilePath() const;
  // K-0/^T自身のWORK/ARC候補一覧の選択(2026-08-18): 元の仕様自身の
  // モックアップは、WORK/arc.N行に
  // "[hit highlighted string][returns to the usilic screen]"と注釈
  // を付けていた――設計については自身の定義コメント参照。
  bool openSelectedBannerEntry();
  bool startCopyPrompt();
  bool startMovePrompt();
  bool startRemovePrompt();
  // K-71(^X^M、M_dest): startCopyPrompt()/startMovePrompt()/
  // startRemovePrompt()のうち最も最近準備した操作に対してプロンプ
  // トを表示する――PendingCpMvRmOperation自身のコメント参照。
  bool startDestinationPrompt();
  // 操作が(^X^Mより前に)準備されている間の^G――自身の定義コメント
  // 参照。
  bool cancelPendingCpMvRm();
  bool startExitPrompt();
  bool runDuOnCurrentFolder();
  // awaitingDu()自身の宣言コメント参照――完了/^G中断のどちらでも
  // 共有される仕上げ処理。
  void finishDuAsync();
  // K-58(^Xa、M_arc2、「ARC: Expand archive」)――K-57(^X^Ka、
  // "with SHELL window")は2026-08-23に削除された、kKeyTable自身の
  // コメント参照。「Destination ?」プロンプト(HELP.jp 2.11)を
  // 開始する。2つの入口、両方ともHELP.jp自身のテキストによれば
  // 有効:
  //   - Directoryモードから、選択されたエントリ自体が認識される
  //     アーカイブsuffixでなければならない(openSelectedEntry()が
  //     トップレベルで既に使っているのと同じprogramForSuffix()
  //     チェック)――「アーカイブ全体」が展開される。
  //   - Archiveモード(既にアーカイブの目次を閲覧中、トップレベル
  //     でもネストされていても)から、targetEntries()が空でない
  //     必要がある(マークされたエントリ、または何もマークされて
  //     いなければ現在の選択――ArchiveMode::toggleMark()自身の
  //     コメント参照)――それらの特定のエントリだけが展開される。
  // 他の全てのモードから、または上のどちらの条件も成り立たない
  // (例えばDirectoryモードで非アーカイブファイルが選択されている)
  // 場合はfalse(拒否)。
  bool startArchivePrompt();
  // `kind`(Tar/Zip/SevenZipに対応――Lha自身の展開構文は
  // extractSelected()が使う`lha p`の標準出力への展開コマンドとは
  // 異なり、Rpm/Debには既にlocateContent()サポートが全く無い、
  // ArchiveMode自身のクラスコメント参照。これらのフォーマットへ
  // 拡張することは将来の課題であり、ここで推測されてはいない)の
  // ための展開argvを構築する。`members`が空は「アーカイブ全体」を
  // 意味する。空でなければ「これらのエントリだけ」を意味する
  // (tar/unzip/7zはいずれも、まさにこのために末尾のメンバー名の
  // 引数を受け付ける――members中の1件がディレクトリエントリ
  // (isDirectory)自身の名前であっても、3ツールとも自動的にその
  // 配下を再帰的に展開する。2026-09-04、ユーザーからの直接の質問
  // 「.7zの0バイトの構成員(ディレクトリ)だけをそれ単体で展開
  // できるか」を受け、実際の`7z`バイナリ(Homebrew、p7zip 17.05)
  // で`7z x archive.7z -o<dest> -y <dirname>`を直接検証して確認
  // 済み――ネストしたサブディレクトリを含め、配下のファイルが
  // 全て正しいパスで展開された)。サポートされていないkindに
  // 対しては空の結果。
  //
  // 7zのmembersはescapeArchiveMemberPattern()を通さない
  // (tar/unzipとは異なる、下のSevenZip分岐自身のコメント参照)。
  std::vector<std::string> buildArchiveExtractArgv(const std::string &archivePath, ArchiveKind kind,
                                                    const std::vector<std::string> &members,
                                                    const std::string &destination) const;
  // 実際のアーカイブエントリ名の中のtar/unzip自身のglobメタ文字
  // (`\*?[]{}`)をバックスラッシュエスケープし、それがシェルスタイル
  // のglobパターンとして(誤って)パースされるのではなく、リテラル
  // なメンバー名としてマッチされるようにする――これが修正する実際
  // の報告についてはbuildArchiveExtractArgv()自身の呼び出し箇所の
  // コメント参照。
  static std::string escapeArchiveMemberPattern(const std::string &name);
  bool startPathInputPrompt(bool isDirectory);
  // K-65(^I、M_list、「LIST: Complete file name」): `partial`
  // (`partial`自体が絶対でも~始まりでもない場合は`baseDirectory`
  // に対して解決される)が暗示するディレクトリの下にある、名前が
  // 残りの接頭辞で始まるファイルシステムエントリを一覧表示する。
  // 返される各候補は、マッチした接尾辞だけではなく、完全な補完
  // (partial自身のディレクトリ部分+マッチした名前)である――
  // Shell::completionProvider_自身の契約(宣言コメント参照)に
  // 合わせている: 呼び出し元は、候補がちょうど1つの時にだけ行動
  // し、既に入力された部分を過ぎた部分を繋ぎ込む。ディレクトリは
  // 末尾に'/'を得る(この移植の確立されたls -Fの慣習、例えば
  // DirectoryEntry::name)ので、単一の曖昧でないディレクトリの
  // マッチは、次のパスセグメントのために即座に再度Tab補完できる。
  // Shell自身のTab補完(コンストラクタで配線される)とFILE/DIRの
  // PathInputプロンプト自身のfeedPromptByte()でのTab処理で共有
  // される――この最初の一切れでは、意図的に他の全てのプロンプトの
  // 種類(MAN/PROGRAM/LOCATE/GREPの名前はそもそもファイルシステム
  // パスではない)には配線されていない。一般的なパターンについては
  // クラスコメント自身の範囲の注記参照。
  // 2026-09-18追加、HELP.txt 2.13章「LIST」の実装(directoriesOnly):
  // legacyのmake_list()自身のf_dir==2(M_ldir/T_arc、ディレクトリのみ
  // 補完)に対応――PathInput(pathInputIsDirectory_==true)/
  // ArchiveDestination(展開先は常にディレクトリ)で使う。既定false
  // (既存の全呼び出し元は無変更のまま)。
  std::vector<std::string> completeFilesystemPath(const std::string &partial,
                                                    const std::string &baseDirectory,
                                                    bool directoriesOnly = false) const;
  // 2026-09-18追加、HELP.txt 2.13章「LIST」の実装(legacy make_list()
  // 自身のf_prog分岐、old/src/key.c参照): $PATH環境変数の各ディレクト
  // リを走査し、`partial`から始まる名前で、通常ファイルかつ所有者
  // 実行可能なエントリだけを集める。legacyと異なりソート済み・重複
  // 排除する(design.md参照、異なる$PATHディレクトリの同名実行ファイル
  // を1回だけ表示する判断)。SHELL(Shell::onTab()、Provider注入経由)/
  // PROGRAM/MANプロンプト(feedPromptByteInner()、shouldSearchPath()が
  // trueの間)で使う。
  std::vector<std::string> pathProgramCandidates(const std::string &partial) const;
  bool feedPromptByte(int byte);
  // feedPromptByte()自身の宣言に対する薄いラッパー実装本体
  // (2026-09-10追加、liveIncrementalGrepSearch()自身のコメント参照):
  // 元々の全ロジックはここへ移した――feedPromptByte()自体は、これを
  // 呼んだ前後でpromptEditor_.text()を比較し、incrementalGrepActive_
  // 中に実際に変化していればliveIncrementalGrepSearch()を呼ぶだけの
  // 薄い皮になっている。
  bool feedPromptByteInner(int byte);
  // 2026-09-26追加(emacs_tutorial.txtの単語編集キー): ESCの直後の1バイトが
  // f/b/d/DEL/^Hなら、editorの単語移動/単語killとして処理してescPending_を
  // 下ろし、trueを返す。それ以外は何もせずfalse(通常のリーダー処理へ)。
  // feedPromptByteInner()とfeedShellByte()で共有する。
  bool applyWordEditEscape(LineEditor &editor, int byte);
  bool submitPrompt();
  void cancelPrompt();
  // 2026-09-10追加、submitPrompt()自身のコメント参照(直接のユーザー
  // 報告: "^[i.→space→^Mで意味不明"): ESC iのEnter確定専用の経路。
  bool finalizeIncrementalGrep(const std::string &pattern);
  bool executeGrep(const std::string &pattern);
  bool executeCut(const std::string &pattern);
  bool executeMan(const std::string &name);
  bool executeProgram(const std::string &command);
  // executeProgram()とsetProgramLauncher()自身のラムダ(コンストラ
  // クタ)で共有される――それら自身の呼び出し箇所のコメント参照。
  bool isRecognizedProgram(const std::string &firstWord) const;
  static std::string cdWrap(const std::string &dir, const std::string &command);
  // Command::QuickLook――その列挙子自身のコメント参照。programMode_
  // 経由で、directoryMode_.selectedEntry()に対して直接
  // `qlmanage -p '<path>'`を実行する(これは単一のハードコードされた
  // コマンドであり、ユーザーが入力したものではないので、PROGRAM
  // プロンプト/isRecognizedProgram()の許可リストを完全に迂回する)。
  // Directoryモードの外側、またはカーソルの下にエントリが無い場合は
  // false(何もしない)を返す。
  bool executeQuickLook();
  bool executeLocate(const std::string &query);
  // executeCopy()/executeMove()で共有される――自身の定義コメント
  // 参照。
  std::string resolveCpMvDestination(const std::string &destination) const;
  bool executeCopy(const std::string &destination);
  bool executeMove(const std::string &destination);
  bool executeRemove(const std::string &confirmation);
  // executeMove()/executeRemove()で共有される――自身の定義コメント
  // (docs/HISTORY.md:1840)参照。
  bool checkTargetsWritable(const std::vector<std::string> &targets);
  // executeCopy()/executeMove()/executeRemove()の共有される末尾
  // ――executeCopy()自身のコメント参照。
  // `targetBasenames`(2026-09-08追加): CP/MVがコピー/移動した対象の
  // ベースネーム一覧――pollPendingCpMvRmShellRun()が宛先ディレクトリ
  // への着地に成功した時、DirectoryMode::setTransientMarkedNames()へ
  // そのまま渡す(directory_mode.hpp自身のコメント参照)。RM
  // (executeRemove())は空のまま渡す(削除後に「対象」を表示する意味が
  // ないため、requirements.mdのスコープ外)。
  bool startCpMvRmShellRun(const std::vector<std::string> &argv, const std::string &destination,
                            const std::string &sourceDirectory,
                            std::vector<std::string> targetBasenames = {});
  bool executeExit(const std::string &confirmation);
  bool executeArchiveExtract(const std::string &destination);
  bool executePathInput(const std::string &path);
  bool executeEdit();
  bool startHelp();
  bool startKey();  // Command::StartKey参照
  bool saveMacroToUsilicrc();  // Command::SaveMacro参照
  // ~/.usilic/usilicrcのKEY・MACROを読み直してその場で効かせる(^X^Sの保存後、EDITでusilicrcを
  // 直した後)。HELPのキー表記も読み直す。
  void reloadUsilicrcKeys();
  bool executeSelectedKeyLine();  // KEYの^M: 選んでいる行のキーを、KEYを開く前の画面で実行する
  bool jumpFromKeyToHelp();       // Command::KeyJumpToHelp参照
  bool openPlainHelp(std::optional<int> rawLine = std::nullopt);
public:
  // HELP(helpPath_のファイル)を読み直す(起動時・^Ja)。読めなければfalse(helpText_はそのまま)。
  bool reloadHelpText();
private:
  std::vector<StyledLine> wrapHelpText(int width) const;

  Display &display_;  // handleResize()自身のrows()/columns()の参照――現在他のどのメンバーもこれを必要としない
  // Command::Suspend(^Z)自身のコメント参照。他の*Mode(ExecMode等)は
  // 既にそれぞれ自分自身のKeyInput&を持っているが、Application自身は
  // これまで直接には保持していなかった――このコマンドだけがApplication
  // 自身のレベルでleaveRawMode()/reenterRawMode()を必要とするため、
  // ここに追加した。
  KeyInput &keyInput_;
  PathResolver pathResolver_;  // ^Xf/^Xdパスプロンプト、コンストラクタ自身のhomeDirectoryコメント参照
  // 2026-08-18の追加(スクリーンショット=cm、2つの同時実行される
  // 引数起動されたインスタンスが、以前はhomeDirectoryの下の1つの
  // 固定パスを共有していたので互いのERROR/SHELL/WORK/ARCファイル
  // を壊し合っていたという実際の報告): error/shell/work/arcファイル
  // が割り当てられるルートディレクトリ。引数起動(複数インスタンス
  // 対応)の実行に対しては`/tmp/.usilic.<pid>`、素の引数無し
  // 「最後のセッションを再開」実行に対しては`<homeDirectory>/.usilic`
  // (legacy自身の単一インスタンスの前提は依然としてそこで成り立つ)
  // ――これが正確にどう選ばれ、ディレクトリが作成されるかについては
  // main()自身のコメント参照。下のusilicrcPath_/historyFilePath_は、
  // 意図的にここには根を持たない――それら自身のコメント参照。
  std::string tempRootDirectory_;
  // 2026-08-18の追加依頼: 上のtempRootDirectory_とは「別」――すぐ上
  // の2026-08-18の再編成への直接のユーザーによる訂正。その再編成は
  // error/shell/work/arcを一緒に根付けたが、素の引数無し起動自身の
  // 「最後のセッションを再開」ERROR/SHELLログは再起動をまたいで
  // 永続化されることを意図している(下のusilicrcPath_/
  // historyFilePath_が既に免除された理由と同じ)一方、WORK
  // (^X^W/K-64自身のw.N作業バッファ)は、起動モードに関わらず本当に
  // セッションごとの使い捨ての内容であり、どちらにせよ終了時に
  // 片付けられるべきである。上のtempRootDirectory_は、それ自身の
  // 既存のerror/shell専用の意味(引数起動された実行に対してだけ
  // PIDスコープ)を保つ。これは、特にwork/arcに対しては常に
  // `/tmp/.usilic.<pid>`である(main()自身の終了時の片付け、自身の
  // コメント参照、はそれが無条件であることに依存している)――
  // allocateWorkFilePath()/allocateArcDirectory()自身が
  // tempRootDirectory_の代わりにこれを使っていることを参照。
  std::string workRootDirectory_;
  // ^T自身の新しい「Boot options」フィールド(executeShowUsilicBanner()
  // 自身のコメント参照)――このプロセスが起動された時の元々の
  // argv[1..]そのもの(フラグとディレクトリ引数、元の順序で、~は
  // 展開済み)、または素の引数無し起動に対しては空。main()(argvが
  // 利用可能な唯一の場所)で一度計算され、単にここへ運ばれる。
  std::string bootOptions_;
  // K-17(^X^W)/K-64(^J^W)の共有される「現在の」ファイルへの書き込み
  // バッファの対象――割り当てポリシーについてはallocateWorkFilePath()
  // 自身のコメント参照(^X^Wはこれを再利用し、^J^Wは常にそれを進め
  // る)。最初の割り当てまでは空。
  std::string currentWorkFilePath_;
  int nextWorkFileNumber_ = 0;  // 最後に割り当てられたw.N接尾辞。0=まだ無し
  // このセッションで割り当てられた全てのw.Nパス、最古優先――^T自身
  // の「WORK」フィールドは、現在のものだけでなく、それら全てを一覧
  // 表示する(executeShowUsilicBanner()自身のコメント参照)。
  std::vector<std::string> allocatedWorkFiles_;
  int nextArchiveNumber_ = 0;  // 最後に割り当てられたarc.N接尾辞。0=まだ無し
  // このセッションで割り当てられた全てのarc.Nディレクトリ(入れ子
  // アーカイブの降下のみ、materializeArchiveTempFile()自身のコメン
  // ト参照――バナー自身の単一の固定一時ファイルではない)、最古優先
  // ――^T自身の「arc.N」一覧のエントリ。
  std::vector<std::string> allocatedArcDirs_;
  // K-67(^X^Ke、M_error、「ERROR」): CpMvRmModeが非対話的な子プロ
  // セスの標準エラーをルーティングする先(コンストラクタ自身の
  // コメントとCpMvRmMode自身のerrorLogPath_引数参照)。str.cの
  // S_errorパス(`~/.usilic/error`)からの移植で、この移植が元々そう
  // していたような常に`~/.usilic/error`ではなく、tempRootDirectory_
  // (2026-08-18、自身のコメント参照)の下に根付けられている。
  std::string errorLogPath_;
  // K-68(^X^Kl、M_log、「LOG: SHELL output」): legacy自身の
  // write(sfd,...)のような、Shellのpty出力の継続的な複写では「
  // ない」(この単純化についてはshellLog()自身のコメント参照)
  // ――K-68が実際に押された時に要求に応じて書き出される、
  // Shell::scrollback()の現在の行のスナップショット。str.cの
  // S_logパス(`~/.usilic/shell`)からの移植で、tempRootDirectory_
  // (2026-08-18、自身のコメント参照)の下に根付けられている。
  std::string logFilePath_;
  // K-0自身のバナーテキスト(executeShowUsilicBanner()自身のコメント
  // 参照)は、Startup fileをそのまま報告する――str.cのprint_usilic()は
  // 同じフィールドを表示する。引数起動(複数インスタンス)の実行
  // でさえ、意図的に依然として`<homeDirectory>/.usilic`に根付けら
  // れている: usilicrcは読み取り専用の設定であり、historyFilePath_
  // は本当にセッションをまたいで蓄積するリングであり、どちらに
  // とってもPIDごとのディレクトリは意味を成さない――tempRootDirectory_
  // 自身のコメントと、この再設計全体が応えている2026-08-17の報告
  // ("^Tで表示されるファイルはusilicrc以外使えない"――^Tが表示
  // する全てのもののうち、usilicrcが共有し続けても問題無い唯一の
  // 例外である)参照。
  std::string usilicrcPath_;
  std::string historyFilePath_;
  // 2026-09-14追加、直接のユーザー依頼("README改めTUTORIALとして...
  // usilic内での発動は^Tで行う"): executeShowUsilicBanner()/
  // openSelectedBannerEntry()自身のコメント参照。usilicrcPath_とは
  // 異なり(~/.usilic/usilicrc、ユーザー自身のホームディレクトリ配下)、
  // これはインストール先($PREFIX/lib/usilic/TUTORIAL、main.cpp自身の
  // 真っ新起動時のFILE履歴事前登録と全く同じパス構築)を指す――
  // ホームディレクトリはこの2つの意味で重なるだけで、本質的には
  // 別の概念である。
  // 2026-10-02変更(.steering/20261002-help-key-usilicrc-reorg/): TUTORIALはHELPへ改名。
  // `~/.local/lib/usilic/HELP`、無ければ`/usr/local/lib/usilic/HELP`(コンストラクタ参照)。^Hは
  // これを^I(タブ)でCUTし(startHelp())、^KはKEY(「：」でCUT、startKey())。^JaでHELP.enと切り替える。
  std::string helpPath_;
  // 2026-09-14追加、直接のユーザー依頼("モード名はTUTORIALだが読み込む
  // ファイルをTUTORIALとTUTORIAL.enとで切り替えを行う^Jaを復活して
  // 欲しい"、旧HELPの日英切替^Oa/^Jaの後継、HISTORY.md:13854): 切替
  // 前の(常に日本語版の)パスをここに固定で保持しておき、
  // handleCommand(Command::ToggleTutorialLanguage)がtutorialPath_
  // 自身をこれと(これ+".en")の間でトグルする――tutorialPath_を直接
  // 書き換えるだけなので、^T画面のTUTORIAL行表示・そこからの再生
  // (openSelectedBannerEntry())は無改造のまま追随する。
  std::string helpBasePath_;
  Charset::Encoding encoding_;  // wrapHelpText()自身のPipeBuffer構築――現在他のどのメンバーもこれを必要としない
  Configuration &configuration_;  // (KEY・MACROはreloadUsilicrcKeys()で読み直す) executeProgram()自身のPROGRAM一覧の参照、上のfind_prog()の注記参照
  History &history_;  // sortedUniqueRecentEntries()/startPromptHistoryRecall()自身のrecentEntries()の参照(2026-09-20: startListBrowser()削除に伴い参照元を更新)
  ModeStack modeStack_;
  ModeStack cutModeStack_;
  ContentView directoryView_;
  ContentView fileView_;
  ContentView grepView_;
  ContentView cutView_;
  ContentView cutContentView_;
  ContentView manView_;
  ContentView locateView_;
  ContentView archiveView_;
  ContentView archiveContentView_;
  ContentView duView_;
  ContentView underlineView_;
  // startUnderlineFileView()発動時のカレントディレクトリ(ディレクトリを
  // 選んで移動した後も、returnFromUnderline()が元の位置へ戻れるように覚える)。
  std::string underlineOriginDirectory_;
  ContentView listBrowserView_;
  // ^O^C(COPY): CopyMode自身の凍結されたスナップショットの裏付け
  // となるビュー――startCopy()自身のコメント参照。
  ContentView copyView_;
  ContentView epubVerticalView_;  // EPUBの縦書き(rebuildEpubVertical()参照)
  ContentView epubTocVerticalView_;  // 縦書きの章の一覧(refreshEpubTocVertical()参照)
  // feedCopyByte()自身のコメント参照: ApplicationMode::Copyの間だけ
  // 有効な、"^[w"/"^[p"(confirmMark()、2026-09-07に'p'追加)用の
  // 1バイト先読み状態。resolveCommand()側のescPending_とは意図的に
  // 別物――feedCopyByte()はそもそもresolveCommand()のテーブルを
  // 一切経由しない独立した語彙であるため。
  bool copyEscapePending_ = false;
  // feedCopyByte()自身のコメント参照(2026-09-07追加、直接のユーザー
  // 要望"①emacsには矩形範囲指定の機能があるのであれを移植して
  // ほしい...emacs同様^X␣で発動"): copyEscapePending_と全く同じ
  // 理由の1バイト先読み――^Xに続く2バイト目(SPC)が来るまで、
  // 矩形マークの発動を確定できない。
  bool copyLeaderXPending_ = false;
  // 2026-10-01追加、直接のユーザー報告("^J^Cでカレントディレクトリへ移動のつもりの^O^C(COPY)では
  // ^J^Cと打っても移動しない。^J^Cは全権モードなので対応よろしく"): COPYの中の^Jリーダー。
  // ^J^CでCOPYを抜けて(ls)のカレントディレクトリへ(Command::GoToFileSelector)。
  // 同日拡張、直接のユーザー依頼("^Jが入口・移動だとすれば全ての^J"): ^Jの2打目でCOPYを抜け、
  // ^Jとその2打目を通常どおりdispatchByte()へ渡す(^J^GだけはCOPYのまま取り消し)。
  bool copyLeaderJPending_ = false;
  // feedCopyByte()自身のコメント参照(2026-09-08追加、直接のユーザー
  // 要望"未サポートでもある^[N（Nは行番号）で当該行へジャンプする
  // 機能をサポートして欲しい。emacsではalt＋ggNなのでそちらも
  // よろしく"): ^[に続く2バイト目が'g'だった場合、Emacs自身の
  // goto-line(M-g g)のプレフィックスとして扱い、次の'g'が来るまでの
  // 1バイト先読み状態。
  bool copyGotoLineGPending_ = false;
  // ^[N(直接、数字が続く)または^[gg(上のcopyGotoLineGPending_経由)の
  // いずれかで入る、行番号の桁を蓄積中の状態。数字が続く限り
  // copyGotoLineDigits_へ積み、改行(Enter)で確定してCopyMode::
  // gotoLine()を呼ぶ――それ以外のバイトはこの入力全体を破棄して
  // キャンセルする(このモードの「^G以外はほぼ全て吸収する」という
  // 既存の単純さに合わせ、訂正/バックスペースは意図的にサポートしない)。
  bool copyGotoLineDigitsPending_ = false;
  std::string copyGotoLineDigits_;
  LineEditor promptEditor_;
  DirectoryMode directoryMode_;
  FileMode fileMode_;
  GrepMode grepMode_;
  CutMode cutMode_;
  ManMode manMode_;
  ProgramMode programMode_;
  LocateMode locateMode_;
  ArchiveMode archiveMode_;
  // 入れ子アーカイブの降下――descendToNestedArchive()自身のコメント
  // 参照。archiveParentStack_[i]は、ネスト階層iまで登り返すために
  // 再読み込みすべきパスである(最も外側のアーカイブに対しては本物
  // のファイル、それより下ではmaterializeArchiveTempFile()自身が
  // 新しくmkdtemp()した一時ファイルの1つ)。
  std::vector<std::string> archiveParentStack_;
  Shell shell_;
  CpMvRmMode cpMvRmMode_;
  DuMode duMode_;
  UnderlineMode underlineMode_;
  ListBrowserMode listBrowserMode_;
  EditMode editMode_;
  ExecMode execMode_;
  CopyMode copyMode_;

  ApplicationMode currentMode_ = ApplicationMode::Directory;
  ApplicationMode grepReturnMode_ = ApplicationMode::Directory;
  // 2026-09-11追加、直接のユーザー報告("usilic -c（引数無し）で起動→
  // ^X^D→^O^S：モード名FGREPのはずなのにGREPかつハイライトの戻り値
  // 空白"、"あとは戻り値だけ。現状空白なのでFILE/DIRではそれぞれ
  // FILE/DIRとしてくれればいい"): grepReturnMode_が
  // ApplicationMode::ListBrowser(^X^F/^X^D自身の候補一覧を「まだ
  // 閲覧中」に^O^S/^J^Sを起動した場合)になる時、previousModeName()に
  // ListBrowserのケースが無いため戻り値欄が空白になっていた。EGREP
  // (GrepSyntax::Regex)は`startRegexPromptList()`→
  // `startPromptHistoryRecall()`が`promptHistoryContext_`/
  // `listBrowserCategory_`をEGREP自身の候補一覧向けに上書きしてしまう
  // ため、後から「元々RecentFile/RecentDirectoryのどちらを閲覧して
  // いたか」を復元できない(FGREPは`activatePrompt()`だけを呼ぶため
  // 上書きされず、たまたま`promptHistoryContext_`から復元できていた
  // ――FGREP/EGREPで挙動が非対称になっていた根本原因)。`startGrepPrompt()`
  // が`grepReturnMode_`を設定する「まさにその時点」(上書きされる前)で
  // ここへ退避しておく。
  RingCategory grepReturnListCategory_ = RingCategory::RecentDirectory;
  // in-place GREPセッション(activateGrepInPlace()/deactivateGrepInPlace()
  // /jumpToGrepInPlaceMatch()自身の宣言コメント参照、2026-09-09追加、
  // .steering/20260909-grep-inline-highlight/design.md)。
  // `currentMode_`は起点(Directory/File)のまま一切変えない――このセット
  // は「今、一時的にin-place GREPの最中である」ことだけを表す、
  // `currentMode_`とは独立した状態。
  bool grepInPlaceActive_ = false;
  std::vector<GrepMatch> grepInPlaceMatches_;
  // GrepOccurrence自身の宣言コメント(このヘッダ内、buildGrepInPlace
  // Occurrences()宣言の直前)参照――^N/^Pのナビゲーション単位を「行」
  // から「一致した個々の出現位置」へ細分化したフラットなリスト。
  // 全マッチを見渡すハイライト自体(grepInPlaceMatches_→
  // toHighlightMatches())は引き続き行単位のままでよい(そちらは
  // 変更不要)。
  std::vector<GrepOccurrence> grepInPlaceOccurrences_;
  int grepInPlaceIndex_ = 0;
  ApplicationMode grepInPlaceOriginMode_ = ApplicationMode::Directory;
  // 2026-09-19追加、直接のユーザー報告(TUTORIAL執筆中に発覚、"EGREPの
  // モードからは離脱したうえで前のモードに戻るわけだからカーソル位置
  // だけ残留するわけにはいかない。SHELL時のカーソル位置へ戻らないと"):
  // activateGrepInPlace()自身がShell起点の場合に呼ぶshell_.
  // beginHistoryBrowsing()は、検索開始前のShell自身の状態(生きた
  // プロンプトだったか、既に手動で履歴閲覧中だったか)を一切保存
  // しないため、deactivateGrepInPlace()側もそれを正確に戻せなかった
  // (検索前は生きたプロンプトだったのに、検索後もマッチ行にカーソルが
  // 残留し、もう1回^Gを押して初めて生きたプロンプトへ戻る、という
  // 余分な1段が必要だった)。activateGrepInPlace()自身の「最初の」
  // activation(grepInPlaceActive_がまだfalseだった時点)でだけこの2つを
  // 退避し、deactivateGrepInPlace()で正確に復元する――ユーザー提案の
  // 「モードスタック」の1フレーム分の状態退避に相当する。Shell以外の
  // 起点では未使用(Shell特有のhistoryBrowsing_概念に対応するものが
  // 他の起点には無いため)。
  bool grepInPlaceShellWasBrowsing_ = false;
  int grepInPlaceShellPriorCursorIndex_ = 0;
  // startIncrementalGrepPrompt()/liveIncrementalGrepSearch()自身の
  // 宣言コメント参照(2026-09-10追加)。GrepPatternプロンプトが編集中の
  // 間だけtrueで、submitPrompt()/cancelPrompt()の両方の入口で
  // 無条件にfalseへ戻す。
  bool incrementalGrepActive_ = false;
  ApplicationMode cutReturnMode_ = ApplicationMode::Directory;
  // 2026-09-19三訂正、直接のユーザー提案("フラグはなるべく使いたく
  // ないんで、何かを上書きするのではなく...旧版のようにスタックへの
  // 積み降ろしでは対応できない？"): 単一の値(manReturnMode_)+それを
  // 上書きするための独立したフラグ(manReturnsToGrepResults_)という
  // 直前の設計を、素朴なLIFOスタックへ差し替える――legacyのSTACK/GREP
  // 構造体からの移植であるModeStack(mode_stack.hpp、CutMode自身の
  // modeStack_/cutModeStack_が既に使っている)と同じ発想だが、あちら
  // ほど重くはない: ModeStackFrameはCutMode自身が抱える「バッファの
  // 中身そのものを積み降ろしする」問題(1つのcutView_/cutMode_しか
  // 無いので、ネストする度に本物のコンテンツを退避/復元しなければ
  // ならない)を解くための型だが、MAN/LOCATEにはその問題が無い――
  // manView_/locateView_はそれぞれ専用の永続ContentViewを持っており
  // (このクラス自身の冒頭のコメント、"ModeStackのpushが全く不要な
  // 理由"の節参照)、モード切り替えは単にどのビューをredraw()する
  // かを変えるだけで済む。ここで本当に必要なのは、「次の^Gはどこへ
  // 行くか」という「モード識別子だけ」のLIFOであり、ModeStackFrame
  // 一式(name/directory/cutBuffer/view等)は要らない――
  // std::vector<ApplicationMode>で十分。
  //
  // 不変条件: 常に「先頭(front)」= nearestFileOrDirectoryAnchor()に
  // より解決された、この一連のMANセッションの恒久的な基点
  // (File/Directory、それ自体はもう再帰する必要が無い終端値)。
  // その後ろに積まれるのは、GREPの結果からマッチへ着地するたびに
  // 積まれる中間の寄り道(現状はApplicationMode::Grepのみ、複数回
  // ネストしても構わない――例えばMAN内のマッチへ着地→そこから
  // 「さらに」GREPし直して「別の」マッチへ着地、のような入れ子も
  // 正しく1段ずつ戻れる)。startManPrompt()/startManList()自身の
  // 「まだManにいない時だけ」ガードで新規に1要素へリセットされ、
  // openSelectedGrepMatch()自身のMan/Locate分岐がGrepを1つpush_back()
  // し、returnFromMan()/returnFromLocate()が1つpop_back()する
  // (front以外に何も残っていなければ、それが「本来の」基点)。
  //
  // 2026-09-19七訂正、直接のユーザー報告("^G連打で①③行き来ループ")
  // で判明した、上のコメントの当初の主張("MAN/LOCATEにはCutMode同種
  // の「バッファの中身そのものを積み降ろしする」問題が無い")の誤り:
  // 基点がFile/Directoryの場合、このスタック自体はモード識別子だけ
  // しか覚えておらず、実際の内容は「fileMode_/directoryMode_が今
  // 何を保持しているか」に頼っている――しかしDU自身のopenSelectedDuEntry()
  // (GREPの結果からDU内のマッチへ着地した「後」、その場でさらに
  // Enterを押して「別の」ファイルを直接開く、この一連の遷移とは
  // 別個の既存の機能)が、まさにこの共有されたfileMode_/directoryMode_
  // を、途中で全く無関係な内容へ上書きしてしまう――基点まで遡って
  // 戻った時には、もう当初の内容ではなくなっている。結果、
  // returnFromDu()がFile側へ「戻った」つもりでも、fileMode_自体は
  // 依然として後から開いた別のファイルを指したままで、かつその
  // fileReturnMode_もDuを指したままなので、Du⇄Fileの間を永遠に
  // 往復してDirectoryへ決して抜けられなくなっていた。
  // manAnchor_/locateAnchor_/duAnchor_(すぐ下)は、この
  // 基点が実際に解決された「その瞬間」のfileMode_.currentFile()/
  // directoryMode_.currentDirectory()を凍結して覚えておき、
  // returnFromMan()/returnFromLocate()/returnFromDu()が最後の1要素
  // (=基点)まで戻ってきた時に、途中で何が起きていようと、その凍結
  // されたパスでfileMode_/directoryMode_を明示的に再読み込みする
  // (ModeStack自身のfloorフレームがまさに「これより上には何も無い」
  // 底を保持しているのと同じ発想の「底を凍結して復元する」安全策)。
  // AnchorSnapshot自身の宣言コメント参照(名前空間スコープ、この
  // クラスの本体より前)。pathだけでなくfileReturnMode(基点がFileの
  // 時だけ意味を持つ)も併せて凍結する――さもないと、基点のFile自体は
  // 正しく復元できても、その*File自身の*^Gの戻り先(fileReturnMode_、
  // 全く別個の既存の仕組み)が、途中の無関係な寄り道(例えば
  // openSelectedDuEntry()が別のファイルを開いた際に上書きしたもの)
  // を指したまま生き残ってしまい、File⇄Man/Locate/Duの間で永遠に
  // ループしてしまう(2026-09-19七訂正、直接のユーザー報告
  // "^G連打で①③行き来ループ"で発覚――パスだけの復元ではこの
  // ループを止められなかった)。
  std::vector<ApplicationMode> manReturnStack_ = {ApplicationMode::Directory};
  AnchorSnapshot manAnchor_;
  std::vector<ApplicationMode> locateReturnStack_ = {ApplicationMode::Directory};
  AnchorSnapshot locateAnchor_;
  // 2026-09-19再訂正、直接のユーザー依頼("DUの修正と、戻り値の仕組み
  // が無いようなのでMAN/LOCATEと同じ仕様にしてほしい"): 以前は
  // duReturnMode_という単一の値のみで、GREPの結果からマッチへ着地
  // する経路(openSelectedGrepMatch()、MAN/LOCATEは既に対応済み)が
  // 実装されておらず、DUだけ「戻り値の仕組みが無い」状態だった。
  // MAN/LOCATE自身のmanReturnStack_/locateReturnStack_と全く同じ
  // LIFOスタックに揃える――理由はそちらの宣言コメント参照。
  std::vector<ApplicationMode> duReturnStack_ = {ApplicationMode::Directory};
  AnchorSnapshot duAnchor_;
  // 2026-08-14: grepReturnMode_/cutReturnMode_等(プロンプトが戻る
  // べき先を追跡する)とは異なり、これは「FILEモード自身の」^G
  // (Command::Back)がどこへ行くべきかを追跡する――直接のユーザー
  // による訂正("戻り値は通常ファイル...なので^G→(ls)はおかしい。
  // LOCATEの検索結果一覧へ戻るわけだし")が、
  // openSelectedLocateResult()のFile分岐が、LOCATE経由で到達した
  // ファイルがファイルシステムのどこにでもあり得て、検索前に
  // アクティブだったディレクトリとは無関係であるにも関わらず、
  // 常にreturnToDirectory()のハードコードされた
  // ApplicationMode::Directoryへ落ちていたことを発見した。
  // FileMode::loadFile()自体にそれをクリアさせるのではなく(FileMode
  // はこのApplicationレベルのフィールドが存在することを全く知ら
  // ない。FileMode::setSearchOrigin()が既に尊重しているのと同じ
  // 境界)、他の全てのloadFile()の後にFileモードが続く呼び出し
  // 箇所(それぞれの「自身の宣言コメント参照」というマーカー参照)
  // で明示的にDirectoryへリセットされる。
  ApplicationMode fileReturnMode_ = ApplicationMode::Directory;
  // 2026-08-28の追加、直接のユーザー報告("CLAUDE.mdで^Iしてる
  // 以上CLAUDE.mdに戻らないと"): executeShowUsilicBanner()は常に
  // fileReturnMode_ = Directoryをハードコードする(上の自身の宣言
  // コメント参照)ので、バナーからの^Gは常にreturnToDirectory()へ
  // 落ち、それは(特にバナー自身の合成されたファイルに対しては)、
  // ^Iが押された時実際に閲覧されていた「本物の」ファイル(もし
  // あれば)を再読み込みするのではなく、directoryMode_に手を付けず
  // 残す。空は「^IがDirectoryモードから直接呼び出された(または
  // この旅がバナー自身のWORK/arc.Nの子の1つからバナーへ戻るホップ
  // である、executeShowUsilicBanner()自身のコメント参照)――^Gは既存
  // のDirectory復帰の挙動を保つ」を意味する。空でなければ「^Iは
  // この本物のファイルを実際に閲覧している間に押された――^Gは
  // 代わりにそれを再読み込みしてそれへ戻るべき」を意味する。^Gが
  // 実際にそれを使う瞬間に消費される(クリアされる)、自身の
  // Command::Back分岐参照。
  std::string infoPreviousFilePath_;
  // startPagerFileList()自身の「ページャーファイル一覧」――usilicが
  // コマンドラインで起動された時の一連のパス全体(2個以上の位置
  // ファイル引数)。そのような起動が一度も起きていなければ空
  // (Directory自身のEnterキー経由の全ての通常のFileモードの
  // エントリ)。その場合Command::PagerNextFile等は全て何もしない
  // ――それら自身の宣言コメント参照。pagerFileIndex_は、
  // startPagerFileList()の直後はpaths.front()自身のインデックス
  // (常に0)であり、navigateToPagerFile()がそれを動かすにつれ、
  // 現在読み込まれているエントリを追跡する。
  std::vector<std::string> pagerFileList_;
  int pagerFileIndex_ = 0;
  ApplicationMode shellReturnMode_ = ApplicationMode::Directory;
  // 前のモード自身の「名前」フィールド(例えばDirectoryMode::
  // currentDirectory()、FileMode::currentFile())、Shellモードに
  // 入場する瞬間にスナップショットされる――previousModeName()
  // 自身のコメント参照。legacyのdo_shell(prev, ...)からの移植:
  // menu.c自身のM_shellケース(ESC-s)は、`ap->name`(全モード自身の
  // mode-line[1]の「name」フィールドが構築される元になる「同じ」
  // 文字列、disp_window()参照)をローカルな`name`バッファへコピー
  // し、それをそのまま`prev`としてdo_shell()へ渡す。それを
  // shell.cは、mode-line[6]の「前モード」のtrailingフィールドと
  // して、disp_window(..., T_shell, name="SHELL", prev, ...)まで
  // ずっと運ぶ。実際のユーザー報告(2026-08-01)が、これが「あなた
  // が来た画面を識別していた名前が何であれ」として読めることを
  // 確認した――Directoryモードから入場した時は現在のディレクトリ
  // だが、Fileモードから入場した時はファイル名であり、この移植が
  // 最初に想定していたように常に「現在のディレクトリ」ではない。
  std::string shellReturnName_;
  // startProgramPrompt()(^Jp)が、ハイライト/開かれているファイル
  // 自身の裸のファイル名でプロンプトを事前に埋める時に設定される
  // (自身のコメント参照): ファイルの含むディレクトリで、
  // executeProgram()がコマンドを実行する前にそこへ`cd`できる
  // ようにする――usilic自体は決してchdir()しない(理由については
  // executePathInput()自身のコメント参照)ので、プログラム名と
  // 並んで入力された裸のファイル名(例えば「less CLAUDE.md」)は、
  // そうしないと、ユーザーが実際に閲覧している場所ではなく、その
  // プロセスがたまたま起動したディレクトリに対して解決されて
  // しまう。startProgramPrompt()がファイルの文脈を見つけなかった
  // 場合は空(既存の挙動へフォールバックする: 空のプロンプト、
  // `cd`接頭辞無し)。送信ごとに正確に一度、executeProgram()に
  // よって消費される(クリアされる)。
  std::string programPromptDirectory_;
  ApplicationMode listBrowserReturnMode_ = ApplicationMode::Directory;
  RingCategory listBrowserCategory_ = RingCategory::RecentDirectory;
  // ToggleHistoryView自身のコメント参照。新しいstartListBrowser()
  // の入場のたびにfalseへリセットされ、toggleListBrowserHistory()
  // によって切り替えられる。
  bool listBrowserChronological_ = false;
  ApplicationMode copyReturnMode_ = ApplicationMode::Directory;
  GrepSyntax grepSyntax_ = GrepSyntax::Fixed;
  GrepSyntax cutSyntax_ = GrepSyntax::Fixed;
  // startCutFilePrompt()とCutPatternプロンプトの送信(K-35/K-36の
  // 「通常ファイル」バリアント)の間は空でない: 現在表示されている
  // バッファの代わりに読み込んで分割すべきパス。消費されるとすぐ
  // executeCut()によってクリアされるので、まさに次のCUT(どちらの
  // バリアントであれ)が誤って古びたパスを再利用しない。
  std::string cutFileSourcePath_;
  // 2026-09-29追加、直接のユーザー報告("docs/HISTORY.mdを"^<"付きで
  // 開いた場合...カーソル位置が保存されず、FILEで再び "^<" 付きを
  // 開いても冒頭行からになる"): 今表示しているファイルCUTの一覧の
  // 「パス "パターン"」(buildCutFileHistoryEntry())。ファイルCUT以外
  // では空。
  std::string cutPositionKey_;
  // 2026-09-20追加、直接のユーザー依頼("C-j bだが、QUICK.txt記載の
  // ようなFILE履歴にしてほしい"): startCutFilePrompt()(C-j b/C-j C-b、
  // 「指定文字列でファイルを分割」)は元々Directory一覧で選択済みの
  // ファイルに対してしか発動できなかった――HELPの新設計(usilic.man
  // をCUT-by-fileで開く)がこれをどのモードからでも「ファイル名を
  // 入力/履歴から選ぶ」形で起動できる必要が出たため拡張した。値が
  // 設定されている間は、startCutFilePrompt()がPromptKind::PathInput
  // (K-45/K-47と全く同じ、履歴補完/Tab補完込みの共有UI)を起動して
  // いる最中であることを示し、その投入(executePromptSubmission()
  // 自身のPathInput分岐参照)を、通常のexecutePathInput()(ファイルを
  // そのまま開く)ではなくcutFileSourcePath_を設定してCutPattern
  // プロンプトへ引き継ぐ経路へ振り向ける。cancelPrompt()で無条件に
  // クリアされる(このフローの外では常にnullopt)。
  std::optional<GrepSyntax> cutFilePendingSyntax_;
  bool shellStarted_ = false;
  // pollPagerExitNotice()自身のコメント参照(2026-09-05追加、GUIの
  // マウスホイール/慣性スクロールでの"止まった"報告への対応)――
  // 「ビューアの現在位置(topIndex)が最後にここで記録した位置から
  // 実際に動いたかどうか」をエッジトリガーで見るための記憶。毎回の
  // 反復で無条件に現在のtopIndexへ更新される(自身のコメント参照)。
  // 初期値-1は「まだ一度も記録していない」ことを表す専用の番兵――
  // 2026-09-06追記: 以前はこれを「どのtopIndexとも一致しないので
  // 最初の呼び出しは必ず『動いた』扱いになる」という設計に使って
  // いたが、これが新しいpagerReturnsToShell()セッションの開始直後
  // (topIndex_はまだ0のまま動いていない)にも無条件に「動いた」と
  // 誤判定し、stillCatchingUpFirstScreen相当の先読みに加えて余分な
  // 2画面分の先読みを重ねて発火させてしまうバグの原因だった(直接の
  // ユーザー報告"開いた直後「00001/00024」のはずだが「00001/00050」
  // になっている")。番兵が-1のままの間は「動いた」と判定しない
  // よう改めた。
  int lastPagerPollTopIndex_ = -1;
  // 2026-09-20更新、直接のユーザー依頼("今のKEYをHELPに置き換え...
  // 正味の：カットによるHELPとしてほしい"): helpText_はもはや
  // loadHelpText(expandHelpMarkup(kHelpTextJa/En))経由の埋め込み
  // テキストではなく、~/.local/lib/usilic/usilic.man(usilic -m
  // QUICK.txtで生成する、legacy自身のHELP.jpと同じnroff重ね打ち
  // バイト列形式のファイル、markup_to_roff.hpp参照)の生の内容を
  // 保持する(main.cpp参照)。startHelp()自身のCUTパターンも、
  // 番号付き見出し(kHelpPattern、正規表現)から全角「：」の固定
  // 文字列検索へ切り替えた(2026-09-15、さらにタブ文字へ切り替え
  // 済み――kHelpPattern自身の定義コメント参照)。日英切替(旧
  // ToggleHelpLanguage/^Oa)・GUI専用章(旧loadGuiHelpText()/
  // guiHelpText_)は、この作り直しに伴い完全に削除した。
  std::string helpText_;
  // 現在どのトップレベルのCut/CutContentの分割がアクティブか
  // ――!cutMode_.canGoBack()の場合にだけ意味を持つ(そしてhandleResize()
  // にだけ参照される)。pushSplit()(ネストされた)階層はこれに
  // 触れない、クラスコメント自身のHELP節参照。
  // Log: ESC l、executeStartLog()自身のコメント参照。Man/Locate/Du
  // (2026-09-15追加、直接のユーザー方針"CUT、F/EGREPは[MAN、LOCATE、
  // DU等に]関与（検索）できる"): Directory起点(directoryView_.lines()
  // を分割する)と全く同じ発想で、manView_/locateView_/duView_.lines()
  // を分割する――executeCut()自身の新設の分岐参照。
  enum class CutOrigin { File, Help, Directory, Log, Man, Locate, Du, Epub, Key };  // Epub: openEpub()参照、Key: startKey()参照
  CutOrigin cutOrigin_ = CutOrigin::File;

  bool recordingMacro_ = false;
  // ^X^S(saveMacroToUsilicrc()参照): 直前に^Xeでマクロを実行した(^X^S以外のキーで下ろす)、
  // 実行キーの1文字を待っている、保存するK番号の並び。
  bool macroJustPlayed_ = false;
  bool executingKeyLine_ = false;
  // 2026-10-02追加、直接のユーザー報告("KEYではカーソル位置が保存されない"、"hを押してHELPへ移動したあとの
  // 対象位置がわかりにくい。一行に渡るハイライトでよろしく（任意のキーで消える）"): KEYの一覧で最後に
  // いた行(startKey()が戻す)、hで飛んだ先の行をハイライトしている間(clearTransientNotice()が消す)。
  int keyCursorIndex_ = 0;
  bool helpJumpHighlight_ = false;
  void saveKeyCursor();  // executeSelectedKeyLine()がキーを流している間(startKey()参照)
  // ^J^W・^Jwが最後にSHELLのコマンド行へ入れたコマンド(SHELLの^Wで出し直す、2026-10-03)。
  std::string lastShellInsertCommand_;
  bool isHelpCommandLineKey(int rawLine) const;  // executeSelectedKeyLine()参照
  // ^X^Sのマクロ一覧(2026-10-03、startMacroList()参照)。一覧の行ごとのM番号(0は「--」の行)と、
  // 閉じた時に戻る画面。
  std::vector<int> macroListNumbers_;
  ApplicationMode macroListReturnMode_ = ApplicationMode::Directory;
  bool startMacroList(bool selectMarker);
  bool closeMacroList();
  bool executeMacroEdit(const std::string &text);
  static std::string macroItemsFromBytes(const std::vector<int> &bytes, bool *failed = nullptr);
  static std::string macroNotation(const std::string &items);
  static std::optional<std::string> macroItemsFromNotation(const std::string &notation);
  std::vector<RecordedStep> recordedMacro_;
  // leaderXシーケンスの先頭の^Xバイトを、2番目のバイトが解決する
  // まで保持しておくので、StartMacroRecord/StopMacroRecord/
  // PlayMacroの解決を、recordedMacro_から完全に(両方のバイトを
  // 一度に)除外できる――クラスコメント参照。2026-09-08、直接の
  // ユーザー依頼によるキー再編で^X(/^X)/^Xeが^O(/^O)/^Oeへ一時的に
  // 移行し、この保留のロジック自体も^O用へ付け替えたことがあったが、
  // 2026-09-10、直接のユーザー依頼("次はマクロ（^X(...）を実装
  // して欲しい"――既に実装済みで単に^Oへ移っていただけと判明した後、
  // "^X(/^X)/^Xeへ戻す(^Oは廃止)"と明示的に選択)により、この保留の
  // ロジックごと^X用へ差し戻した。^Oの他の全てのコマンド(c/m/r/a等、
  // 2026-09-08の同じ再編で移った分)はそのまま^Oに残る――今回戻る
  // のはマクロ制御の3つ(^X(/^X)/^Xe)だけである。
  std::optional<RecordedStep> pendingLeaderXByte_;

  bool leaderJPending_ = false;  // ^Jを見た、2バイトのコマンドを完成させるバイトを待っている
  // ^Jの後にdispatchLeaderByte()自身のleaderJPending_スイッチが
  // ちょうど見た数字(Command::JumpToPathPrefix自身のペイロード
  // ――この移植ではCommandの値はそれ自身のデータを運ばないので、
  // これは、リーダー数字コマンドがその引数をhandleCommand()の
  // switchへ渡す確立された方法である: Command::JumpToPathPrefix
  // 自身のコメント参照)。それを設定したバイトのすぐ後の、単一の
  // handleCommand()呼び出しに対してだけ意味を持つ。
  int pendingPathPrefixDigit_ = 0;
  bool escPending_ = false;      // ESC(0x1b)を見た、^[sを完成させるバイトを待っている――クラスコメント参照
  // 2026-09-08追加、直接のユーザー要望("^[Nはおよそ行数あり画面なら
  // どこでも機能でもあるのでよろしく"): dispatchLeaderByte()自身の
  // escPending_ブロック、feedCopyByte()自身のcopyGotoLineGPending_/
  // copyGotoLineDigitsPending_と全く同じ理屈の、COPYモード以外の
  // 全モード共通版――CopyMode専用のcopyGotoLine*とは別物(こちらは
  // Application::activeContentView()へ直接作用する、モード非依存の
  // 汎用ジャンプ)。
  bool gotoLineGPending_ = false;
  bool gotoLineDigitsPending_ = false;
  std::string gotoLineDigits_;
  // 2026-09-08追加、直接のユーザー要望("「現在行/総行 ??%」エリア幅の
  // まま非ハイライト入力エリアとして使用して欲しい。実際の見た目は
  // 旧を参照"): old/src/disp.cのprint_line()(linep==F2の分岐)は、
  // 元々そこにあった位置フィールドの幅(`k = tlen`)ぶんだけ最初に
  // 空白でクリアしてから、入力された桁をその先頭から順に上書きして
  // いく――つまり入力欄は常に元の表示と「同じ幅」を占め続ける。この
  // 幅を、桁入力が始まる瞬間(ContentView::naturalPositionText()、
  // まだ上書きされていない自然な位置テキスト)に記録しておき、
  // 桁を表示するたびにその幅までスペースでパディングする。
  int gotoLineFieldWidth_ = 0;
  bool leaderXPending_ = false;  // ^Xを見た、^Xd/^X^D/^Xf/^X^F/^X^C/^X(/^X)/^Xeを完成させるバイトを待っている(2026-09-08のキー再編で他は全て^O/^Jへ移行したが、DIR/FILE/EXITは同日中に、マクロ制御(^X(/^X)/^Xe)は2026-09-10にそれぞれ^Xへ差し戻された)
  // ^J^O(usilicrcのERROR/LOG、2026-09-08新設のネストしたサブ
  // リーダー): ^Jの後に^O自体を見た、^J^Oe/^J^Olを完成させる
  // バイトを待っている――旧^X^K(F-2)がERROR/LOGの2つだけを残して
  // 他は全て削除された後を引き継ぐ形。
  bool leaderJOPending_ = false;
  bool leaderCPending_ = false;  // ^Cを見た(2026-09-25、^Cb/^Cf。dispatchByte()自身のコメント参照)
  bool leaderOPending_ = false;  // ^Oを見た、2バイトのコマンドを完成させるバイトを待っている(2026-09-08のキー再編で^Xの大半の役割を引き継いだ)
  bool wantsExit_ = false;
  // 2026-09-25追加、次のキーで消える一時メッセージ(showTransientNotice()
  // 参照)を今表示しているビュー。nullptrなら表示していない。
  ContentView *transientNoticeView_ = nullptr;
  bool sortLabelNoticeShown_ = false;
  bool archiveLoadAborted_ = false;  // ^Gでアーカイブの読み込みを中断した(finishArchiveLoadAsync()参照)  // ^O^Lのソートキーのラベル表示中(CycleLsSortKey参照)
  // 一時メッセージを`view`のモードラインの位置欄(GREPの"No String"と同じ
  // 場所、非ハイライト)に出す。どのキーでも(feedUntranslatedByte()の冒頭、
  // あるいはhandleCommand()の冒頭で)消え、そのキー自体は通常どおり働く。
  // 使い道: toggleMark()のディレクトリ複数選択の拒否、BUFFERを超えるファイル
  // の^Mを^O^Mへ切り替えた時の"Not enough buffer"。
  void showTransientNotice(ContentView &view, const std::string &text);
  void clearTransientNotice();
  void showBufferLimitNoticeIfHit();  // それ自身のコメント(application.cpp)参照
  bool capAsyncOutput(std::string &output, pid_t pid);  // それ自身のコメント(application.cpp)参照
  void capBufferContent(std::vector<unsigned char> &content);  // 同上
  bool handleMouseClickImpl(int row, int column);

  enum class PromptKind {
    None,
    GrepPattern,
    CutPattern,
    ManName,
    ProgramCommand,
    LocateQuery,
    CopyDestination,
    MoveDestination,
    RemoveConfirm,
    ExitConfirm,
    // DuPathは2026-09-09に削除された(直接のユーザー依頼、"DUの仕様
    // としては常時カレントで...文字列入力エリア無しでよろしく")――
    // Application::runDuOnCurrentFolder()自身のコメント参照。
    PathInput,  // K-45/K-47の共有されるプロンプトの種類、Command::StartFileInput自身のコメント参照
    ArchiveDestination,  // K-57/K-58自身のDestinationプロンプト、startArchivePrompt()自身のコメント参照
    // K-46/K-48(^X^F/^X^D、「Select file/directory name」):
    // 2026-08-17の追加――なぜこの2つがそもそもここに追加される必要
    // があったのかについてはstartListBrowser()自身のコメント参照
    // (実際の報告+legacyのkey.cの読み込みが、T_hfile/T_hdirが
    // T_hfgrep/T_hegrep/T_hmanとまさに同じf_enter=1「モードライン
    // で文字列を入力する」グループを共有していること、つまり履歴
    // 一覧はそれらと同じ生きた編集可能なコンボボックスであり、この
    // 移植が元々構築したような純粋な閲覧ピッカーではないことを
    // 発見した)。
    RecentFile,
    RecentDirectory,
    // 2026-09-19追加、直接のユーザー依頼("`^Jl`と同じく`集計中`な意味の
    // メッセージを出して欲しい"): DuPath(すぐ上のコメント参照)とは
    // 違い、これは本物のプロンプト(promptKind_/promptActive())では
    // ない――runLocateUpdateDb()がPromptKind::LocateQuery+staticMessage
    // 経由で"Updating database..."を描くのと全く同じ仕組みを、
    // `du`自身の(現状ブロッキングな)呼び出しの直前に一度だけ
    // drawPromptLabel(PromptKind::Du, "", "Disk usage...")として呼ぶ
    // ためだけに存在する――promptKind_へは一切代入されない。
    Du,
    // 2026-09-20追加、startArchiveLoadAsync()自身のコメント参照。
    // Du同様、promptKind_へは一切代入されないdrawPromptLabel()専用の
    // 静的メッセージ("Loading archive...")のためだけに存在する。
    ArchiveLoad,
    // 2026-10-01追加、openEpub()自身のコメント参照。ArchiveLoadと同じく、drawPromptLabel()専用の
    // 静的メッセージ("Reading...")のためだけに存在する。
    EpubLoad,
    // 2026-10-02追加、2026-10-03変更(startMacroList()参照): ^X^Sのマクロ一覧のプロンプト("MACRO Key:")。
    // 「キー キーの並び」を編集し、^Mでusilicrcへ書く。
    MacroEdit,
    // 2026-09-18追加、HELP.txt 2.13章「LIST」の実装、実機報告
    // (スクリーンショット=gv、legacyの参照画面: "プロンプトエリアは
    // 編集可能な非ハイライト表示が正解")を受けての追加。Du/ArchiveLoad
    // と全く同じ理由でpromptKind_へは一切代入されない
    // ――refreshDirectEntryModeLine()自身のtabCompletionContext_分岐が
    // drawPromptLabel(PromptKind::TabCompletionList, positionText)を
    // 呼んで"LIST"ラベルを描くためだけに存在する(promptLabelText()
    // 自身のコメント参照)。
    TabCompletionList,
    // 2026-09-18追加(フォローアップ5、直接のユーザー提案: "SHELL自体を
    // スタックへ積んだ上でLISTルーチンへ移行し、文字列編集はLIST内で
    // 行った上で(当然一覧も無傷)編集が完了した時点でスタックからSHELL
    // を戻して文字列を渡す"): 上のDu/ArchiveLoad/TabCompletionListとは
    // 違い、これは「本物の」promptKind_であり、実際にpromptEditor_へ
    // 代入される。SHELLはGrepPattern/CutPattern/ManName/RecentFile/
    // RecentDirectoryと違って専用のPromptKindを持たず、feedShellByte()
    // /shell_.commandLine()という別個のウィジェットへ直接バイトを再送り
    // していたため、行の所有権(一覧の内容行とSHELL自身のcommandLineRow()
    // /モードライン行)が2系統に分かれ、実機報告のたびに(パス補完/
    // ソート/生きた編集/ページ送り/戻り値バッジ/一覧の消失、と5ラウンド
    // 連続で)取りこぼしが起き続けた。ShellCommandは、SHELLのTab補完中の
    // 編集を、GrepPattern等と全く同じpromptEditor_ベースのコンボボック
    // スへ完全に統合する――編集中は一覧の内容行に一切触れず(list
    // BrowserView_のredraw()以降何も再描画しない、activatePrompt()自身
    // が最下行のモードライン1行だけを書く)、Enter/^Gで初めて確定した
    // 文字列をshell_.commandLine()へ書き戻す(executePromptSubmission()/
    // cancelPrompt()自身のShellCommandの分岐参照)。
    ShellCommand
  };
  // startCopyPrompt()/startMovePrompt()/startRemovePrompt()の一覧版(2026-09-27)。
  bool startListCpMvRmPrompt(PromptKind kind);
  // ^Xc/^Xm/^Xr(K-52/K-54/K-56)とその^X^K「with SHELL window」の
  // 兄弟(K-51/K-53/K-55)は、以前は移動先/確認プロンプトを即座に
  // 有効化していた――実際のユーザー報告(=ecのスクリーンショット:
  // ファイルがまだ何もマークされていないうちから、^Xcが押された
  // 瞬間に空の「CP」テキスト入力ボックスが現れる)が、代わりに
  // legacy自身の2段階のM_cp/M_mv/M_rm -> M_dest(^X^M、K-71)フロー
  // を求めた: ^Xc/^Xm/^Xrは今や、「どの」操作が準備されているか
  // だけを覚えており(この構造体)、ユーザーは以前と全く同じように
  // 通常の`ls`スタイルの一覧でファイルをマークし(^Space/^X^Y/
  // ^X^N、変更なし――legacyとは異なり、マーキングはこの移植では
  // 既に「今はCPモードにいる」という別の閲覧状態から切り離されて
  // いたので、その部分のために新しいモードは不要だった)、
  // startDestinationPrompt()(^X^M)が、実際にプロンプトを表示する
  // ものである。`directory`は、無関係なディレクトリへのナビゲート
  // を古びた準備状態が生き残ることを防ぐ(マーク自体は既に
  // DirectoryMode::enterDirectory()でクリアされるが、targetEntries()
  // は、何もマークされていない時「カーソルの下にあるもの」へ
  // フォールバックする――ユーザーが決して意図しなかったディレクト
  // リでそれに対して黙って行動することは、特にRMにとって本物の
  // 落とし穴になる)。startDestinationPrompt()はこれをチェックし、
  // 古びた準備状態を使うのではなく捨てる。
  struct PendingCpMvRmOperation {
    PromptKind kind;
    std::string directory;
  };
  std::optional<PendingCpMvRmOperation> pendingCpMvRm_;
  // executeCopy()/executeMove()/executeRemove()(2026-08-22の再設計、
  // それら自身のコメント参照): cp/mv/rmコマンド行がshell_の本物の、
  // 永続的なSHELLセッションへ注入され送信された後、これは、それが
  // 完了した時点で何をすべきかを覚えている――
  // shell_.promptGeneration()がpromptGenerationBeforeを超えて進む
  // ことで検出される(pollPendingCpMvRmShellRun()参照)。
  // `destination`はRMに対しては空(移動先という概念が無い)。
  // `sourceDirectory`は常に操作が起動されたディレクトリであり、
  // `destination`が本物の、閲覧可能なディレクトリへ解決しない
  // 場合のフォールバックの着地場所である(またはRMに対しては、
  // それが「唯一の」着地場所である)。
  struct PendingCpMvRmShellRun {
    int promptGenerationBefore;
    std::string destination;
    std::string sourceDirectory;
    // startCpMvRmShellRun()自身のtargetBasenames引数のコメント参照
    // (2026-09-08追加)。
    std::vector<std::string> targetBasenames;
    // 発動元(2026-09-27、cpMvRmOrigin_自身のコメント参照)。
    ApplicationMode origin = ApplicationMode::Directory;
  };
  // offerShellCommand()のfollowUp(2026-09-27)。`offered`は置いた行、
  // `generation`はその時点のshell_.promptGeneration()。
  struct ShellOffer {
    std::function<std::string()> followUp;
    std::string offered;
    int generation;
    int retries;
  };
  std::optional<ShellOffer> shellOffer_;
  std::optional<PendingCpMvRmShellRun> pendingCpMvRmShellRun_;
  // CP/MV/RMを(ls)以外の一覧(LOCATE/DU/^X^F・^X^D/USILIC)から発動した場合
  // (2026-09-27、直接のユーザー依頼、list_marks.hpp参照)の発動元と、発動時に
  // 確定した対象の絶対パス。Directoryなら従来どおりdirectoryMode_から
  // 集める(collectTargetPaths())。
  ApplicationMode cpMvRmOrigin_ = ApplicationMode::Directory;
  std::vector<std::string> cpMvRmListTargets_;
  // executeArchiveExtract()自身のPendingCpMvRmShellRunのバージョン
  // (2026-08-23、CP/MV/RMのものと同じ再設計、executeCopy()自身の
  // コメント参照)。2026-08-26の追加(直接のユーザー仕様): 今は
  // CP/MV自身のPendingCpMvRmShellRunと同様、成功時に移動先ディレ
  // クトリに着地する「ようになった」――`destination`/`archivePath`
  // は、pollPendingArchiveExtractShellRun()がdescendTo()とそこで
  // 設定する「ARC: <archivePath>」注釈のために必要とするもの。
  // `returnMode`は依然として、本当に失敗した展開に対するフォール
  // バックである(自身のコメント参照)。
  struct PendingArchiveExtractShellRun {
    int promptGenerationBefore;
    ApplicationMode returnMode;
    std::string destination;
    std::string archivePath;
    // 2026-09-09修正、直接のユーザー報告("ARC展開先を~/.usilic/saved/
    // utf8/（あるいは^Oa^M）とした場合、同ディレクトリがすでに存在
    // していると[一時マークの]アンダーラインが付かない"): 当初
    // (2026-09-08、requirements.md「設計判断B」)は展開前後のディレ
    // クトリ一覧の差分で「新規に現れた名前」を判定していたが、この
    // 方式は「以前にも同じ場所へ展開したことがある」場合(2回目以降の
    // 再展開、まさに~/.usilic/saved/配下でよく起こる)に根本的に
    // 機能しない――対象の名前が「以前から既にそこにあった」ため、
    // 差分上は「新規」に見えない。この場合、展開が実際に何を作った
    // かに関わらず一時マークが一切付かなかった。
    // 代わりに、展開コマンドを組み立てる時点で分かっている「アーカ
    // イブ自身のメンバー一覧(または部分展開ならmembers)のトップ
    // レベルの名前」を直接ここへ保存しておく――ディレクトリの実際の
    // 前後比較には一切頼らないので、既存/新規を問わず常に正しく
    // 「このアーカイブが宛先に作るはずの名前」を反映する
    // (executeArchiveExtract()自身のコメント参照)。
    std::vector<std::string> topLevelNames;
  };
  std::optional<PendingArchiveExtractShellRun> pendingArchiveExtractShellRun_;
  // submitPrompt()(生きたまま入力されたテキスト)と
  // openSelectedListEntry()自身のプロンプト呼び戻し分岐(代わりに
  // ^Rの履歴一覧から選ばれたテキスト)で共有される――
  // startPromptHistoryRecall()自身のコメント参照。両方の経路が
  // switchを複製するのではなくPromptKindに対して同一にディスパッチ
  // するよう括り出されている。
  bool executePromptSubmission(PromptKind kind, const std::string &text);
  // サポートされているプロンプト(MAN/LOCATE/FGREP/EGREPの名前/
  // クエリ/パターン)がアクティブな「間」にK-49(^R)が押された:
  // 完全な設計については自身の定義コメント参照。まだそれ自身の
  // 履歴を持たないプロンプトの種類に対してはfalse(バイトは黙って
  // 飲み込まれ、リテラルなテキストとしては挿入されない)――^Rが
  // 当てはまらない文脈に対するm_list()自身のecho_mess(S_errf)に
  // 合わせている、ToggleHistoryView自身のListBrowser外での何も
  // しない挙動と同じ精神。
  bool startPromptHistoryRecall(bool directEntry = false);
  // ^RがArchive/ArchiveContentの中で(2026-08-20の追加、自身の
  // 呼び出し箇所のコメント参照): RecentFileリングを閲覧している
  // 間だが、同じリングへの通常の^X^F/^Rを介したFile/Directoryから
  // の経路ではなく、特に^R経由でArchive/ArchiveContentから到達
  // した場合にtrue――startPromptHistoryRecall()(最初の一覧)と
  // toggleListBrowserHistory()(既に閲覧中の間の^Rによる時系列
  // トグル)の両方がアイテムを表示する前にこれをチェックするので、
  // 一覧がどう再構築されようとアーカイブフィルタは適用され続ける。
  bool archiveHistoryFilterActive() const;
  // K-32/K-34(^J^S EGREP、^J^C CUTの正規表現バリアント): 空の
  // プロンプトのステップを完全にスキップして「直接」候補パターン
  // 一覧を開く――理由についてはstartGrepPrompt()/startCutPrompt()
  // 自身のコメント参照(単なるusilicrcの文言のニュアンスではなく、
  // 実際のlegacy-usilicの挙動に対して確認済み)、そしてそれが
  // startPromptHistoryRecall()をどう丸ごと再利用するかについては
  // startRegexPromptList()自身の定義コメント参照。
  bool startRegexPromptList(PromptKind kind);
  void selectListBrowserItem(const std::string &item);
  // 生きたSHELLプロンプトでK-49(^R)が押された(^R設計の統合ポイント
  // 3――CLAUDE.mdの項目162/163参照): ^J^R自身の「COMMAND」ピッカーが
  // 既に使っているのと同じ基礎データである、ソートされた
  // RecentEntries(ShellCommand)のListBrowserを開く。意図的に
  // promptHistoryRecallKind_を再利用するのではなく別のフラグ
  // (shellHistoryRecallActive_)にしている(SHELLはPromptKindでは
  // ない――それはそれ自身のfeedShellByte()入力経路を持つそれ自身の
  // ApplicationModeである)、またはRingCategory::ShellCommandに
  // 対する^J^Rの既存のopenSelectedListEntry()処理に折り込むのでも
  // ない(それは、それ自身の「この一切れでは」テストに従って、
  // Enterで意図的に何もしないままである――これは、たまたま同じ
  // リングを共有する「別の」機能である)。ユーザーと直接確認済み
  // (2026-08-05): エントリを選択することは、それをレビュー/編集の
  // ためコマンド行へ「挿入する」だけであり、自動実行は「しない」
  // ――副作用の無いアクションであるMAN/LOCATE/FGREP自身の呼び戻し
  // とは異なり、選ばれたSHELLコマンドは破壊的である可能性がある
  // (rm、mv、...)ので、先にそれを見る機会無しに古いものを黙って
  // 再実行することは、単なるUXの癖ではなく、本物の安全性の退行に
  // なるだろう。
  bool startShellHistoryRecall();
  // 2026-09-18追加、HELP.txt 2.13章「LIST」の実装、
  // TabCompletionContext自身の宣言コメント参照。startShellHistory
  // Recall()と全く同じ形(currentMode_を保存→ListBrowserMode::show()
  // →currentMode_=ListBrowser)だが、戻り先がSHELL固定ではなく
  // 呼び出し時のcurrentMode_(SHELLの生きたコマンドライン、または
  // promptEditor_の下敷きのモードのどちらでもあり得る)である点が
  // 異なる。
  bool showTabCompletionList(const std::vector<std::string> &candidates, const std::string &partial,
                              const std::string &prefixBeforeWord);
  // openSelectedListEntry()(^M、selected=true)/returnFromListBrowser()
  // (^G、selected=false)の両方の「先頭」(promptHistoryContext_等の
  // 既存の分岐より前)から呼ばれる共有ヘルパー。tabCompletionContext_
  // が無ければ何もせずfalseを返す(呼び出し元はそのまま既存の分岐へ
  // 進む)。
  bool returnFromTabCompletionList(bool selected);
  // 全てのstartXxxPrompt()で共有される末尾処理: promptKind_を設定
  // し、その種類自身のラベルをモードラインの行に描き
  // (promptLabelText()自身のコメント参照)、initialTextで
  // promptEditor_を開始する。実際のユーザー報告(2026-08-02)が、
  // どのプロンプトも、どこにも、それが何を尋ねているか(FGREP?
  // EXIT y/n? DUパス?)の可視の指標を全く表示していないことを
  // 発見した――以前は全てのstartXxxPrompt()が、それ自身の
  // promptKind_=/setText()/redraw()の3つ組を、ラベル無しで行って
  // いた。このプロジェクト全体を通じて、legacy「自身の」
  // mode-line[1]の「name」フィールドが黙って移植されないままに
  // なっていたのに合わせている。
  // positionText(2026-08-12の修正、デフォルトは""/表示されない):
  // 直接のユーザーによる訂正――"[bs]→「1/10 TOP」が消滅"(既存の
  // 候補をそのまま編集へバックスペースすると、候補一覧がちょうど
  // 閲覧されている間ずっと可視だった、自身の「N個の候補のうち
  // どれか」という位置を失う)。feedRegexListByte()自身の編集への
  // 遷移が、drawPromptLabel()がそれを表示し続けられるよう、ここへ
  // ModeLine::format(cursorIndex+1, count)を渡す。他の全ての
  // activatePrompt()の呼び出し箇所(プレーンなFGREP/MAN/LOCATE/
  // .../ArchiveDestinationのプロンプト、どれも番号付きの一覧から
  // 発生しない)はこれをデフォルトのままにする。
  // cursorAtStart(2026-08-17の追加、PROGRAM自身のIDEAS.md:218の
  // 修正): trueの場合、setText()自身のデフォルト(末尾)ではなく、
  // カーソルを`initialText`のまさに「先頭」に位置決めする――
  // startProgramPrompt()自身の「CLAUDE.md」の事前入力のため。
  // そこではカーソルが「最初の」文字に座る必要がある。プログラム
  // 名("less ")を入力すると自然に前に追加され、「less CLAUDE.md」
  // が形成されるように。既存の全ての呼び出し箇所はこれをデフォル
  // ト(false、変更されていない末尾のカーソル配置)のままにする。
  // selectAllOnSeed: 2026-09-11追加、直接のユーザー報告(docs/CHANGES.md
  // 項目439参照)。trueの場合、事前入力したinitialTextを「全選択」
  // 状態にする(LineEditor::selectAll()自身のコメント参照) ――
  // 続く最初の編集操作がinitialTextを丸ごと消してから新しい入力に
  // 置き換わる。startLocatePrompt()の「前回クエリを事前入力」の
  // ためだけに新設した。既存の全ての呼び出し箇所はこれをデフォルト
  // (false、変更されていない従来の追記的編集)のままにする。
  void activatePrompt(PromptKind kind, const std::string &initialText, const std::string &positionText = "",
                       bool cursorAtStart = false, bool selectAllOnSeed = false);
  // 各PromptKindに対して表示されるラベルテキスト――通常のケース
  // に対してはlegacy自身のs_P(P_mode)[...]のモード名文字列
  // (disp_window()のmode-line[1]の「name」フィールド、例えば
  // " EXIT"、" FGREP")からの移植、「加えて」M_exit自身のdisp.cの
  // 特別なケース(sprintf(line, "%s (y/n) ?  ",
  // s_S(S_errg)))が、legacyの別々のname+positionフィールドの分割
  // を複製するのではなく、ここで「1つの」結合された文字列に折り
  // 込まれている――この移植には、promptEditor_が既に行の残りを
  // 所有している時点で「(y/n) ?」を入れる等価な2番目のフィールド
  // が無く、単一の結合されたラベルは、はるかに少ない仕組みで
  // 同じことを伝える。
  std::string promptLabelText(PromptKind kind) const;
  // promptLabelText(kind)をモードラインの行自身の左端に反転表示で
  // 描き、その後promptEditor_自身のフィールドをその直後から始まる
  // よう狭める(activatePrompt()自身のコメント参照)。将来のプロン
  // プト中のリサイズの修正(ここでは試みられていない、この移植
  // 自身の既知の制限の注記参照)が再呼び出しすべき明白な単一の
  // 場所を持てるよう、単にactivatePrompt()から分離されている。
  //
  // staticMessage(2026-08-13、デフォルト""/未使用): 直接のユーザー
  // 依頼――LOCATE自身のupdatedb進捗表示("Updating database...")
  // は、ブロッキングなChildProcess呼び出しが実行されている「間」
  // (promptEditor_は全く関与せず、編集可能なものは何も無い)、
  // まさにこの同じ「ラベルの直後」の位置に、下線付きで現れる必要
  // があった。これの最初のバージョンは、runLocateUpdateDb()の中に
  // 直接、この関数のラベル描画ロジックの独自のコピーを手作りして
  // いた――実際の追加報告("モードライン共有ルーチンはできない？")
  // が、それがまさに項目291自身のモードライン監査が他所でちょうど
  // 排除し終えたばかりの類の1回限りの重複であることを指摘した。
  // 空でない場合、これはstaticMessageを、promptEditor_自身の
  // フィールドがそうでなければ始まるであろうのと「同じ」列
  // (labelWidth)に下線付きで描き、promptEditor_.setField()/
  // trailingの予約ロジックを完全にスキップする(ここには余地を
  // 確保すべき編集可能なフィールドも移動先ディレクトリのtrailing
  // も無い――これは本物のプロンプトではなく、プロンプト行自身の
  // ラベル描画を再利用する一時的なステータス行に過ぎない)。
  void drawPromptLabel(PromptKind kind, const std::string &positionText = "", const std::string &staticMessage = "");
  // 2026-09-06追加、直接のユーザー報告("旧では^J.でSHELL busyと出る")
  // ――shell_.busy()自身の宣言コメント参照(is_inter()からの移植、
  // ようやくPROGRAM起動以外のケースもカバーした)。K-60/K-61/K-62/
  // K-63/K-64が全て同一の"if (shell_.busy()) return false;"を持って
  // いたので、legacy自身のecho_mess(S_errf)("SHELL busy")に相当する
  // 可視の通知をここへ集約する――shell_.busy()がtrueならベル付きで
  // 通知を出してtrueを返す(呼び出し元はそのままreturn falseする)。
  bool refuseIfShellBusy();
  bool switchToShellIfBusy();  // それ自身のコメント(application.cpp)参照
  void showShellBusyNotice();
  // `bypassPacing`(2026-09-06追加): pollPagerExitNotice()自身の
  // コメント参照――呼び出し元が「まだ最初の1画面を埋めている最中」
  // だと分かっている時にだけtrueを渡し、FileMode::ensureLoadedThrough()
  // 自身のペーシングを一時的に迂回する。
  void ensurePagerStreamLoaded(int targetLineIndex, bool bypassPacing = false);
  // K-46/K-48(^X^F/^X^D)自身の読み取り専用(まだ編集していない)
  // 候補一覧: 2026-08-18の修正、実際の報告+参照スクリーンショット
  // (=cw)――ハイライトされた候補は、最初のバイトが入力された時点
  // だけでなく、「最初から」プレーンな、ハイライトされていない
  // 編集可能なフィールドのように見える必要がある(drawPromptLabel()
  // 自体経由で、本物の編集が始まった瞬間にそれが切り替わる先に
  // 合わせている)。drawPromptLabel()自体(同じレイアウト、同じ
  // 自動折り返し安全な埋め)に加えて、現在選択されている候補の
  // テキストをそのフィールドに表示する非編集状態のpromptEditor_
  // の再描画を再利用する――選択されているものを変え得る
  // listBrowserView_の全ての描画の後(startPromptHistoryRecall()
  // 自身の最初の表示、閲覧中のMoveUp()/MoveDown())に呼ばれ、他の
  // 全てのPromptKind/モードに対しては何もしない。
  void refreshDirectEntryModeLine();
  PromptKind promptKind_ = PromptKind::None;
  // activatePrompt()自身のコメント参照。
  bool promptCandidateListActive_ = false;
  // 2026-08-28の再設計(docs/HISTORY.md、"9個のバラバラなメンバ変数
  // に分散しており...直したつもりの箇所が別の箇所の前提を壊す"
  // ――このまさに種類のバグの直接のユーザーによる診断で、同日の
  // 複数の報告にまたがって反響した): 以前は4つの別々のメンバー
  // (promptHistoryRecallKind_/Text_/ReturnMode_/IsDirectEntry_)で
  // あり、異なる呼び出し箇所によって独立に設定/リセットされて
  // いたものが、「このセッションはそもそも開いているのか」と
  // 「それは何を覚えているのか」が決してずれ得ないよう、1つの
  // 構造体に統合されている――今回のセッションで繰り返し当たった
  // まさにその失敗モード(あるコードパスが、4つのうち「一部だけ」
  // をクリア/設定し、残りを古びたままにする)。^R起動のプロンプト
  // 履歴ListBrowserが開いている間だけ設定される
  // (startPromptHistoryRecall()自身のコメント参照): どのプロンプト
  // を再開すべきか、^Rが押される前にそこに既に何が入力されていたか
  // (Back/^Gで逐語的に復元される――^Rは黙って部分的な入力を捨てる
  // べきではない)、そしてプロンプトの下でどのApplicationModeが
  // アクティブだったか(プロンプトは、それ自身のモードを所有する
  // のではなく「現在の」モードに重ねられるので、これは、
  // listBrowserReturnMode_が素の^X^F/^X^Dのケースに対してそうする
  // のと同じ方法で、ListBrowserの合間をまたいで保存/復元されなけ
  // ればならない)。呼び戻しセッションの外側ではnullopt――この
  // 1つのoptionalのhas_value()が、今や「このセッションは開いて
  // いるか」に対する唯一の権威であり、4つの別々にチェックされる
  // 条件を置き換える。
  struct PromptHistoryContext {
    PromptKind kind;
    ApplicationMode returnMode = ApplicationMode::Directory;
    std::string textBeforeRecall;
    // このListBrowserの合間が、過去に入力されたパターンへの本物の
    // ^R呼び戻しではなく、startRegexPromptList()(^J^S/^J^C自身の
    // 「候補一覧へ直接開く」入場、自身のコメント参照)経由で到達
    // された場合にだけtrue――legacyのmode.cは、これらを本当に
    // 異なる画面として区別している(^Rに対してはT_hfgrep/
    // T_hegrep、直接入場に対しては素のM_fgrep/M_locate/M_cut2):
    // 本物の^R呼び戻しは常に固定のリテラルな「HISTORY」という名前
    // を表示し(なぜそれがそこで正しいのかについては
    // startPromptHistoryRecall()自身の2026-08-05のコメント参照)、
    // 最新のエントリ(下端)から始まる。直接入場は実際のモード名
    // (例えば「CUT」)に加えて右側にハイライトされた移動先ディレク
    // トリを表示し、「上端の」エントリから始まる(実際の報告+
    // legacyソースの読み込み、2026-08-11: T_hfgrep/T_hegrepは
    // 明示的に`row = nrow - 1`を設定するが、M_fgrep/M_locate/
    // M_cut2は単に`i = -2`を設定し、上端をデフォルトにする)。
    bool isDirectEntry = false;
    bool epubOnly = false;  // ^J^E(Application::startEpubList())
  };
  std::optional<PromptHistoryContext> promptHistoryContext_;
  // 2026-09-18追加、HELP.txt 2.13章「LIST」の実装(design.md参照)。
  // TAB補完の候補が曖昧なまま2回目のTABが来た時、ListBrowserMode
  // 経由で一覧を見せ、選択結果を呼び出し元(SHELLの生きたコマンド
  // ライン、またはpromptEditor_)へ返すための最小限の文脈。
  // promptHistoryContext_(「過去の1件を丸ごと呼び戻す」)とは目的が
  // 異なる(「今まさに入力中の単語を補完する」)ため、あえて別の
  // optionalとして独立させている――兼用すると、PromptKindを持たない
  // SHELL(生きたコマンドライン、promptKind_を一切経由しない)を表現
  // できない。
  struct TabCompletionContext {
    // 呼び出し時のcurrentMode_。プロンプトはcurrentMode_を書き換え
    // ない(promptKind_/promptEditor_が下敷きのモードの「上に」重なる
    // だけ)という既存の設計そのものにより、これがApplicationMode::
    // Shellならshell_.commandLine()、それ以外ならpromptEditor_が
    // 挿入先だと判別できる――isShellPrompt相当の別フィールドは不要。
    ApplicationMode returnMode;
    // 2026-09-18追加(フォローアップ8、直接のユーザー報告「SHELLなら
    // SHELL、FILEならFILEから一歩も出ずに補完しているように見える」):
    // 一覧を閲覧中の間、モードラインのラベルにこの値(SHELL/PROGRAM/
    // MAN/FILE/DIR等、宛先の実際のPromptKind)をそのまま使う――以前は
    // 閲覧中だけ汎用の`PromptKind::TabCompletionList`("LIST")を挟み、
    // 編集開始の瞬間に初めてこの値へ切り替えていたため、ラベルが
    // "LIST"→"SHELL"のように見た目上変化していた(=^X^F^Rの「HISTORY」
    // が閲覧中も編集中も同じラベルを保つのとは対照的だった)。
    PromptKind kind;
    // 一覧表示時点での補完対象語――選択項目からこれを差し引いた
    // 残りだけをpaste()する(既存のLCP補完と同じ「残りの接尾辞だけ
    // 追記する」流儀に揃えている、置換ではない)。
    std::string partial;
    // 2026-09-18追加、実機報告(③「^Nで非ハイライトにはなるものの
    // 編集不能」)を受けて追加: 補完対象語の「前」にあったテキスト
    // (例えば"less "、SHELLなら"cat "――バッファ全体からpartialを
    // 引いた残り)。一覧の閲覧中にタイプ/DELが来て「編集を開始する」
    // 遷移(feedTabCompletionListByte()自身のコメント参照、
    // feedRegexListByte()と同じ考え方)へ入る時、これ+選択中の候補で
    // 完全な行を再構成する――候補一覧はpartialだけを補完する機能
    // なので、その前にあった単語(コマンド名等)を消してはならない。
    std::string prefixBeforeWord;
  };
  std::optional<TabCompletionContext> tabCompletionContext_;
  // 2026-09-18追加、実機報告(③)を受けて新設。dispatchByte()自身の
  // 冒頭付近、promptKind_/currentMode_==Shellの通常分岐より「前」で
  // 呼ばれる――tabCompletionContext_が有効な間だけの専用の小さな
  // キー語彙(feedRegexListByte()自身のクラスコメント参照、全く同じ
  // 考え方の複製): ^N/^P/Enter/^G/^Lは一覧のナビゲートのみ、他の
  // 全てのバイト(普通の文字、DEL/^H)は「編集を開始する」遷移――
  // prefixBeforeWord+現在ハイライト中の候補で、SHELLの生きたコマンド
  // ライン(returnMode==Shell)またはpromptEditor_(それ以外)の
  // どちらかへ実際に書き戻し、その後このバイト自身をfeedShellByte()/
  // feedPromptByte()へ再送りして編集を続行させる。feedRegexListByte()
  // と違い、SHELLという「PromptKindを持たない」戻り先にも対応する
  // 必要があるため、promptHistoryContext_ベースの既存関数を拡張する
  // のではなく専用に新設した(design.md参照)。
  bool feedTabCompletionListByte(int byte, std::optional<KeyInput::SpecialKey> special);
  // ensureShellActive()/returnFromShell()(2026-08-24の修正、実際の
  // 100%再現する報告: "（ls）のCLAUDE.md上で^J^B^J^W^G"――^J^Bの
  // 直後は正しく「CUT」とラベル付けされていた候補一覧が、この
  // 往復の後は「EGREP」と誤ってラベル付けされて戻ってくる):
  // ^J^W(shellInsertBufferContent()、ensureShellActive()の複数ある
  // 呼び出し元の1つ)は、promptHistoryContext_のセッション(例えば
  // ^J^B自身の正規表現CUT候補一覧、直接入場)がまだ本当に開いている
  // 「間」でも押され得る――放棄されるのではなく、単に「Shellを
  // 素早く覗いて、^G経由ですぐ戻ってくる」だけである。
  // ensureShellActive()は入場時に無条件にpromptHistoryContext_を
  // クリアする(そもそもなぜそのクリアが存在するのかについては
  // 自身の長いコメント参照――後の、無関係な^R ShellCommandセッション
  // に古びたフラグが生き残るという、実際の、別のバグについて)。
  // これは元々のそのシナリオに対しては正しいが、「現在の」セッ
  // ション自身のアイデンティティも捨ててしまう:
  // listBrowserView_自身のModeLineFieldsProviderは、
  // promptHistoryContext_が設定されている時だけ本物の「CUT」という
  // 名前を表示する(なぜringCategoryLabel()単独ではCUTの正規表現
  // バリアントをEGREPと区別できないのかについては自身のコメント
  // 参照――それらはRingCategory::RecentEgrepPatternを共有する)ので、
  // 空のpromptHistoryContext_は、実際にどちらが開いていたかに
  // 関わらず、^Gの帰り旅を、そのリングカテゴリのみのラベル
  // 「EGREP」へフォールバックさせてしまう。これは、Shellへの寄り道
  // の間、生きているものの「一時停止された」コピーを保持し
  // (ensureShellActive()自身の保存とreturnFromShell()自身の復元
  // 参照)、その間に他に何も起きなかった場合の素の^Gでの帰りに
  // 何を再開すべきかを失うことなく、生きているものが依然として
  // 安全に消去される(元々のバグを修正する)ことを可能にする。
  // 意図的に完全なセッションのスナップショットでは「ない」
  // (listBrowserCategory_自体はこれによって手を付けられない)
  // ――Shellの「内側の」ネストされた寄り道であって、それ自体が
  // 最終的な^Gの前に別のListBrowserセッション(例えばShellCommand
  // 履歴に対する^R)を開くものは、これがカバーしない、既知の、
  // より狭い残存ギャップである。
  std::optional<PromptHistoryContext> suspendedPromptHistoryContext_;
  // すぐ上の3つのフィールドとは異なる、より単純なケース: それらの
  // 3つはListBrowserの「候補一覧」のシナリオ
  // (promptHistoryRecallKind_)向けである。これらの2つはpromptKind_
  // 自体――「直接」アクティブな、まだ入力中の編集可能なプロンプト
  // (例えば素の^SからのGrepPattern、パターンの途中)――向けであり、
  // リーダーコマンド(^J^W、shellInsertBufferContent())が、
  // プロンプト自身の通常の終了地点(submitPrompt()/cancelPrompt()/
  // startPromptHistoryRecall()――promptKind_が消去される、既存の
  // 唯一の3つの場所)を迂回して、そこから直接Shellへジャンプする。
  // 2026-08-24の修正、実際の報告:
  // dispatchByte()自身のトップレベルのルーティングは、
  // `currentMode_ == Shell`「より前に」`promptKind_ !=
  // PromptKind::None`をチェックする(順序については自身のコメント
  // 参照)――Shellへジャンプした後promptKind_をGrepPatternに詰まった
  // ままにすることは、その後に生きたSHELLプロンプトへ入力される
  // 全てのバイト(まさに次のキー入力から)が、feedShellByte()の
  // 代わりにfeedPromptByte()経由でルーティングされ続け、黙って
  // 古びたFGREPパターンを再編集/再送信することを意味していた
  // ――ユーザーが実際に見ていたshellコマンドを実行する代わりに
  // 検索を再実行する(executeGrep()は無条件にcurrentMode_を上書き
  // し、Shellから完全にスナップして抜け出す)素のEnterを含めて
  // ("^J^WはHELPに戻っているようでいて...実はSHELLにいるらしい")。
  // (単に捨てられるのではなく)一時停止されるので、素の^Gで戻ると、
  // すぐ上のpromptHistoryRecallKind_自身の確立されたパターンに
  // 合わせて、ユーザーが残していったまさにその場所での編集が再開
  // される。
  std::optional<PromptKind> suspendedActivePromptKind_;
  std::string suspendedActivePromptText_;
  // 2026-08-11: 実際の報告+直接のユーザーによる明確化――"検索への
  // 入口がCUTなら出るときもCUTから出るはず"(検索自身の入口が
  // 候補一覧だったなら、Backもそこを通って出るべき)、つまり正しい
  // LIFOスタック: Directory -> 候補一覧 -> 結果、そして^Gは、
  // Directoryへ直接ではなく、一度に「1階層ずつ」popする。
  // promptHistoryRecallIsDirectEntry_自体は、feedRegexListByte()/
  // openSelectedListEntry()がそれを消費した瞬間(検索が実際に実行
  // されるずっと前)にクリアされるので、その値は「まず」ここで
  // 捕捉され、成功時にexecuteGrep()/executeCut()を通して運ばれ、
  // それらがそれを
  // grepReturnsToCandidateList_/cutReturnsToCandidateList_へ移す
  // ――returnFromGrep()/returnFromCut()によって、Backが
  // grepReturnMode_/cutReturnMode_へ直接戻る代わりに候補一覧を
  // (再びstartRegexPromptList()経由で)再度開くべきかどうかを決める
  // ために参照される(そして一度きりでクリアされる)。2回目の往復
  // (その再度開かれた一覧からパターンを再編集し、再度送信する)は、
  // フラグを新たに再度設定するので、これは自然に、ユーザーが行う
  // だけの数の検索/編集の周期を連鎖させる――Directoryへ到達する
  // 唯一の方法は、候補一覧自体から明示的にキャンセルすること
  // (returnFromListBrowser()自身の無関係な、既存の^Gキャンセル
  // 経路)である。
  bool pendingSearchFromDirectEntryList_ = false;
  // 2026-09-21追加、直接のユーザー依頼(MAN一覧の仕様統一): ^J^Mの
  // ソート済み一覧から選んで開いたManは、^Gでその一覧へ戻る(Grepの
  // grepReturnsToCandidateList_と同型)。ディレクトリ移動でクリアする。
  bool manReturnsToCandidateList_ = false;
  // USILIC一覧(アクセス履歴)から開いたManは、^GでそのUSILIC一覧へ戻る。
  bool manReturnsToUnderline_ = false;
  // USILIC一覧の階層: None=マーク一覧/時系列、Toc=MAN/DIR/FILE目次、Group=種別ごとの一覧。
  enum class UsilicLevel { None, Toc, Group };
  UsilicLevel usilicLevel_ = UsilicLevel::None;
  int usilicGroupKind_ = 0;  // 目次で最後に選んだ種別(usilicTocTitles()の添字: 0=MAN、1=DIR、2=FILE、3=PROGRAM、4=WORK)
  // ^Ou(閲覧)を(ls)以外の一覧(LOCATE/DU/^X^F・^X^D/USILIC)から発動した時の
  // 戻り先(2026-09-27、直接のユーザー依頼"（ls）で可能なファイル操作には
  // 「閲覧」（^Ou）もあるのでよろしく")。^Gで発動元の一覧へ戻す。USILICから
  // 発動した場合は、その一覧の中身・階層・カーソルも戻す。
  struct UnderlineReturn {
    ApplicationMode mode;
    std::vector<std::string> entries;
    UsilicLevel level;
    int groupKind;
    int cursor;
  };
  std::optional<UnderlineReturn> underlineReturn_;
  // DIR一覧(^X^D)から選んで移動する前のディレクトリ。注釈Dirの^Gで
  // 元の位置へ戻した上で一覧を再表示する。
  std::string dirListOriginDirectory_;
  // 上の一覧で選んだ項目(^Gで一覧へ戻る時、カーソルをそこへ復元する)。
  std::string dirListSelectedItem_;
  // LOCATE一覧から選んだディレクトリへ移動する前のディレクトリ(^Gで一覧へ戻る時に復元)。
  std::string locateDirOriginDirectory_;
  // FILE一覧(^X^F)から開いたファイルの^G復帰用(DIR一覧と同型)。
  bool fileReturnsToFileList_ = false;
  std::string fileListOriginDirectory_;
  std::string fileListSelectedItem_;
  std::string manListSelectedItem_;  // MAN一覧から選んだページ(^G復帰時のカーソル復元用)
  bool grepReturnsToCandidateList_ = false;
  bool cutReturnsToCandidateList_ = false;
  // returnToCutResults()/returnFromCut()(2026-08-24の修正、実際の
  // 報告: "^G連打→戻り値「CUT: ^##」との行き来ループ→（ls）へ戻ら
  // ず"――^Gを連打すると、注釈付きのDirectory一覧とCut結果画面の
  // 間で永遠にループし、決してプレーンな(ls)には落ち着かない)。
  // CUT自身のクラスコメントは、なぜreturnFromCut()が、GREP自身の
  // 2関数分割(openSelectedGrepMatch()は、本当に「新しい」マッチの
  // 時に常に一度それを設定する。returnFromGrep()は、その関数へ
  // 到達することが「本当に後退する」ことを意味するので常にそれを
  // 「クリアする」、自身のコメント参照)とは異なり、常にCutモード
  // を離れ「かつ」「CUT: pattern」注釈を(再)書き込む単一の関数で
  // あるかを説明している――しかしその理由は、新しい検索の後のCUT
  // 「自身の」最初の終了だけをカバーする。それは、
  // returnToCutResults()(注釈自身のクリックバックの対象)経由で
  // 結果に「再入場」し、その後この同じreturnFromCut()を通して
  // 「再び」離れる、「2回目の」^Gをカバーしない――それは無条件に
  // 毎回同一の注釈を再書き込みするので、Directoryでの次の^Gは
  // 永遠に再度入場する。returnToCutResults()によって、再開された
  // 結果画面を表示する直前に設定される。returnFromCut()によって、
  // この特定の「再開されたセッションを離れる」旅では、注釈を書き
  // 換えるのではなく「クリアする」ために参照される(そして一度
  // きりでクリアされる)。returnFromGrep()自身の挙動を正確に反映
  // している。
  bool cutResultsResumed_ = false;
  // startShellHistoryRecall()自身のコメント参照。
  bool shellHistoryRecallActive_ = false;
  // 2026-09-18追加、HELP.txt 2.13章「LIST」の実装。Shell::tabArmed_と
  // 全く同じ役割(design.md参照)だが、こちらはpromptEditor_側
  // (feedPromptByteInner()のbyte==0x09分岐)専用――Shell::tabArmed_は
  // Shellクラス自身が持つので二重に持つ必要はない。feedPromptByte()
  // 自身の冒頭で、TAB以外の任意のバイトによりfalseへ戻る。
  bool promptTabArmed_ = false;
  std::string shellHistoryRecallText_;
  // 2つのキー(K-45 ^Xf対K-47 ^Xd)のどちらが現在のPathInput
  // プロンプトを開始したか――なぜ「実行」は統一されたまま
  // (PathResolverが、入力されたパス自体からファイルかディレクトリ
  // かを決める)である一方、モードラインの「ラベル」は依然として、
  // 実際に押されたキーがどちらであれ、legacy自身の別々の
  // " FILE"/" DIR"というテキストに一致する必要があるのかについては
  // startPathInputPrompt()自身のコメント参照(legacyに対する実際の
  // 並べたスクリーンショット比較、2026-08-03が、この移植が、
  // プロンプトを開始したキーに関わらず常に「FILE/DIR」を表示して
  // いることを発見した)。
  bool pathInputIsDirectory_ = false;
};

}  // namespace fm
