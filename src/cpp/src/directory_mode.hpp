#pragma once

#include <set>
#include <string>
#include <vector>

#include "configuration.hpp"
#include "content_view.hpp"
#include "history.hpp"
#include "mode_stack.hpp"

namespace fm {

// ディレクトリ一覧の1エントリ、`ls -alF`出力の1行からパースされた
// もの。
struct DirectoryEntry {
  std::string name;    // ファイル名のみ(ls -Fの末尾"/"/"*"/"@"は保持、legacyと同じ)
  std::string lsLine;   // 表示用の、完全に整形されたls行
  bool isDirectory = false;
  // lsLine内で`name`が始まるバイトオフセット――scroll.cの
  // get_col()/init_col()(T_dirケース)からの移植で、legacyでは
  // カーソル行自身の反転表示ハイライトが列0からではなくここから
  // 始まる(stand_str()の`&rvp->pp->cs[rvp->col]`参照)。parseLsLine()
  // が`name`自身と一緒に計算するので、ContentView::Lineの
  // highlightStartColumnを、再導出せずに設定できる。
  int nameStartColumn = 0;
};

// ディレクトリ閲覧モード: 他の全ての寄り道(ファイル閲覧、アーカイブ
// 閲覧、GREP、CUT、...)が戻ってくる「本拠地」画面。これは、legacyが
// 「Mode」と呼ぶもの(mode.c/menu.cのmode()/menu()/get_m()、この移植
// 全体の中で圧倒的に最大かつ最も絡み合った部分――2ファイル合わせて
// 約3300行、約170個のモード定数を巡る巨大なswitch)の、意図的に
// 狭い最初の一切れである。ここで移植されているのはT_dir(ディレクトリ
// 閲覧)と^G「戻る」動作だけ。他の全モード(ファイル/アーカイブ/
// GREP/CUT/MAN/PROGRAM/SHELL連携/CP-MV-RM/...)は将来の課題であり、
// このクラスが既に正しく組み合わせて見せている、同じ既に移植済みの
// 構成要素の上に積み重ねられる: ContentView(描画)、ModeStack
// (ナビゲーション履歴)、History(カーソル位置の記憶)、Configuration
// (`ls`のコマンドライン)、ChildProcess(それを実行する)。
//
// これはmode()のT_dirケースやmenu.cのm_bl()/m_dir()の逐語的な移植
// ではない。それらの関数は、このプロジェクトが繰り返し逐語的には
// 引き継がないと選択してきたまさにその種のlegacy設計である:
// m_bl()だけでも、共有される可変バッファ(ap->name/lp->dir/lp->old)
// 経由でCUT/T_grep/T_man/T_pipe4/T_pipe5/T_dir/T_du/M_shellの各分岐
// を一度に扱う、約120行の1関数である。なぜならmode()とm_bl()が
// 合わさって、全モードの「戻る」挙動が通されなければならない
// 「唯一の」場所だったからだ。今ではModeStack/History/ContentView
// がそれぞれ、以前はそれらの共有バッファに詰め込まれていたものの
// 自分自身の断片を所有しているので、戻ることは単に: カーソル位置を
// 保存し、フレームをpopし、そのディレクトリを再度一覧表示し、その
// カーソル位置を復元する――共有バッファのやりくりは不要。
//
// 意図的に除外(将来の課題、この段階では対象外):
//   - 通常ファイルを開くこと(openSelected()はファイルに対しては
//     何もしない――ファイル閲覧はまだ移植されていない)。
//   - mode()のswitchにある他の全て: GREP、CUT、MAN、PROGRAM、
//     アーカイブ閲覧、SHELLへの入場、DU、LOCATE、履歴/リスト
//     ブラウザ、ヘルプ/エラー/ログ画面、終了確認。選択マーキング
//     (下のtoggleMark()/targetEntries())は「対象内」である。CP/MV/RM
//     (別のCpMvRmModeクラス)が、対象ファイル一覧をどこから取得する
//     か必要としているため――ただし実際にマークされたエントリに
//     対してcp/mv/rmを実行するのはCpMvRmModeの仕事であり、この
//     クラスの仕事ではない。
//   - ModeStackの「floor」概念(pushFloor()/isFloor()): モードが1つ
//     しか実装されていない状態では、floorフレームが「スタックが単に
//     空である」ことと有用に区別すべきものがまだ何も無い――この
//     段階の目的では、空のModeStackは既に「戻る先が無い」ことを
//     意味する。2つ目のモードが存在するようになったら再検討する。
//   - 12スロットの再利用可能バッファプール(legacyのHEAP.base[N_base]):
//     動いているContentViewが1つだけの状態では不要。ファイル閲覧、
//     または2つ目の並行バッファが何であれ移植された時点で解決すべき
//     本当のギャップ(このクラスの設計時に印を付けてあり、静かに
//     見過ごされているわけではない)。
class DirectoryMode {
 public:
  DirectoryMode(ContentView &view, ModeStack &modeStack, History &history, const Configuration &configuration);

  // `path`を一覧表示し(ChildProcess + Configuration::commandFor(Ls)経由、
  // LC_ALL=Cを強制する――理由はparseLsLine()のコメント参照)、自身が
  // 所有するContentViewに表示する。consultHistoryがtrue(デフォルト
  // ――既存の呼び出し元は全て: openSelected()、goBack()、^Or
  // リロード)の場合、このパスちょうどに対する記憶済みカーソル位置が
  // Historyにあればそれを復元する。そうでなければ、保存済み位置が
  // あるかどうかに関わらず常に先頭から始める。`ls`の実行に失敗した
  // 場合(存在しない/読めないディレクトリ、`ls`コマンド未設定)はfalse。
  //
  // consultHistory=falseは、正確に1つの呼び出し元のために存在する:
  // 明示的なパス引数(「.」を含む)付きで起動された時のmain()自身の
  // 最初のapp.start()。History自身のクラスコメントは、これが
  // カーソル位置を「複数回の実行をまたいで」永続化すると記している
  // ――legacy自身の、引数なし起動の再開機能である――が、それは
  // 特に引数「なし」起動の機能であって、明示的なパスを入力することは
  // 「今このディレクトリを見せろ」という意味であり「前回のセッションを
  // 再開しろ」という意味ではなく、同じパスへの以前の無関係な訪問で
  // 保存された位置を黙って引っ張ってくるべきではない(実際の報告:
  // 「.」で起動すると、先頭ではなく古びたリスト途中の行に着地して
  // しまい、この特定の起動が履歴の再生であるようなものは何も無かった
  // ので混乱した)。
  bool enterDirectory(const std::string &path, bool consultHistory = true);

  const std::string &currentDirectory() const;
  const std::vector<DirectoryEntry> &entries() const;

  // 現在カーソルの下にあるエントリ。一覧が空の場合はnullptr。
  const DirectoryEntry *selectedEntry() const;

  // 選択されたエントリに対するEnter/M_cr: それがディレクトリなら、
  // 現在のカーソル位置を保存し、戻ってくる先を記憶するModeStack
  // フレームをpushし、そこへ降りていく。legacyのT_dirケースと同じ
  // 方法で".."/"."を解決する(ディレクトリの文字列操作によるもので
  // あって、ファイルシステムが本物の".."エントリを持っていることを
  // 信頼するのではない――例えばルートディレクトリで重要になる)。
  // 選択がレギュラーファイルの場合、または一覧が空の場合はfalse
  // (何もしない)を返す。
  bool openSelected();

  // その場でCURRENTディレクトリに対して`ls`を再実行する――ナビゲー
  // ションではなく(ModeStackフレームなし、履歴参照なし)、単なる
  // リフレッシュ。openSelected()自身の「./」ケースとApplication::
  // goToFileSelector()自身の、既にそこにいるDirectoryケース
  // (2026-08-23の追加、両方とも同じ実際のユーザー報告からの移植)の
  // ため。カーソルは、リフレッシュ前に選択されていたものと「同じ名前」
  // を持つエントリへ復元される(その名前が(このリフレッシュを引き
  // 起こした何らかの外部変化によりリネーム/削除されて)存在しなければ
  // 行0にフォールバック)――直接のユーザーからの訂正: ここで保存済み
  // 履歴位置を参照する(enterDirectory()自身のデフォルト)と、ユーザーが
  // 現在いる場所にそのまま留めておく代わりに、以前の何らかの無関係な
  // 訪問からの古びた行へカーソルが飛んでしまった
  // ("現在行位置が更新されない...^P^P^P...^J^K（マイナカード表.
  // pngに戻る）"――3行離れた後リフレッシュすると、そのまま留まる
  // 代わりに古い位置へスナップして戻ってしまう)。
  bool refresh();

  // K-19(^O^K、M_m): currentDirectory()の親へ直接ジャンプする。
  // openSelected()が「../」エントリに対して行うのと同じ方法でModeStack
  // フレームをpushする――これはまさにそれと同じナビゲーションであり、
  // 先にカーソルを「../」に動かすことなく到達できるというだけの違い。
  // m_dir()自身のM_mケースからの移植: ルート("/")では、legacy自身の
  // パス切り詰めロジックは何もしない(取り除くべき先頭の1個の'/'より
  // 前には何も無い)。これは、無意味に「/」へ再入場する代わりに拒否
  // (false)することでそれを反映している。
  bool goToParent();

  // openSelected()/goToParent()で共有される(^Gでナビゲート可能な降下、
  // 同じディレクトリの単なる再読み込みでは「ない」): 現在のカーソル
  // 位置を保存し、戻ってくる先を記憶するModeStackフレームをpushし、
  // `target`へ入場する――失敗した場合はpushを取り消す。公開された
  // 理由(2026-08-23の修正、実際の報告: "CPで/tmpへのコピーを終えた後
  // の（ls）では^Gで/tmpから変わらない")は、Application自身のCP/MV/RM
  // 完了後の移動先への着地(pollPendingCpMvRmShellRun())が、
  // enterDirectory()を直接呼ぶのではなく、通常のナビゲーションと
  // 「同じ」履歴をpushする経路を通るようにするため――enterDirectory()
  // は新しい一覧を読み込むが(設計上、その自身のコメント参照)
  // modeStack_自体には決して触れないので、^Gにpopして戻る先が無い
  // ままになってしまう。
  bool descendTo(const std::string &target);

  // ^G: 最も新しいModeStackフレーム(もしあれば)をpopしてそこへ戻り、
  // Historyからそのカーソル位置を復元する。スタックが空(戻る先が
  // 無い)場合はfalseを返す。popされたFLOORフレーム
  // (resetNavigationToFloor()自身のコメント参照)は特別に扱われる:
  // 自身のdirectoryではなくpreviousDirectoryへ行き、その2つを入れ
  // 替えたfloorを再度pushする。これにより、^Gを繰り返し押すことが
  // 1回のpopで尽きてしまうのではなく、本当に行ったり来たりを切り
  // 替えるようになる。
  bool goBack();

  bool canGoBack() const;

  // K-48(^X^D、「DIR: Select directory name」――履歴/最近使った
  // ものからのピッカーであり、通常のEnterによるサブディレクトリへの
  // 降下では「ない」): ^G自身のナビゲーションスタックを、
  // 「今まさに入場した」ディレクトリ(呼び出し時点のcurrentDirectory_)
  // を根とする新しい「floor」へリセットする。これにより、ピッカーが
  // 呼び出される前に積み上がっていた複数段階の降下チェーンが、その後の
  // ^Gの挙動に漏れ出すことがない。enterDirectory()がcurrentDirectory_を
  // 新しく選ばれたディレクトリへ既に更新した「後」に呼ぶこと。legacyの
  // save_stack_dir()の番兵(ModeStack::kFloorMarker)からの移植――
  // pushFloor()/isFloor()はModeStackに既に存在していたが何もそれらを
  // 呼んでいなかった。この移植の他の場所でも繰り返し見られる、
  // 「存在するが配線されていない」という同じパターン。2026-08-13に、
  // 実運用での検証待ちの、明示的に「暫定」なユーザー指定の^Gポリシー
  // (保証された最終設計ではない)に基づいて追加: "「選択後の画面」
  // 以降のDIR...については履歴を廃棄して真っ新から始める...^Gの連打も
  // DIRならカレントおよび以前のディレクトリを行き来".
  void resetNavigationToFloor(const std::string &previousDirectory);

  // -- 選択マーキング(CP/MV/RM用、^Omスタイル) --
  // mode.cのfmark()/nmark()からの移植――legacyはPPリスト自体の
  // その場でエントリをマークしていた(pp->split == -f_mark)。ここでの
  // マークは代わりにentries_へのインデックスであり、「現在の」一覧に
  // スコープされる(enterDirectory()がそれを再読み込みするたびに
  // クリアされる。マークが何らかの形で新しい`ls`をまたいで生き残る
  // のではなく、「一覧を見て、複数のエントリをマークし、それらに
  // 対して操作する」という、1つのビュー内での通常のファイル
  // マネージャのワークフローに合わせている)。
  void toggleMark(int index);
  bool isMarked(int index) const;
  std::size_t markedCount() const;
  void clearMarks();
  // K-72(^X^Y、「Mark all」): 現在の一覧の全エントリをマークする。
  // 「./」/「../」の除外は無い――toggleMark()自身にも無いので、これは
  // 1エントリずつマークすることと一貫性を保っている。clearMarks()
  // 自体が既にK-73(「Clear all」)をカバーしている。
  void markAll();

  // -- 一時マーキング(CP/MV/ARC完了後の対象表示用、^[P用) --
  // 上のmarkedIndices_(CP/MV/RM「着手前」の複数選択)とは意図的に
  // 別の状態: 2026-09-08、直接のユーザー要望("CP/MV/ARCモードで複写/
  // 移動/展開先ディレクトリへ移動したあと、対象ファイルおよび
  // ディレクトリを^[Pへも渡せるようアンダーライン表示して欲しい。
  // ^Lで消去")。名前ベース(インデックスではない)なのは、一覧の並び
  // 順・インデックスが再読み込みごとに変わり得るため――特にARCの
  // 「展開前後の差分」判定では、そもそも「以前のインデックス」という
  // ものが存在しない。isMarked()がmarkedIndices_との論理和として
  // これも見るので、既存のsetMarkedRowUsesUnderline(true)描画経路に
  // そのまま合流する。markedCount()/targetEntries()はmarkedIndices_を
  // 直接見るので、この一時マーキングはCP/MV/RM操作対象の判定には
  // 一切影響しない。
  void setTransientMarkedNames(std::vector<std::string> names);
  void clearTransientMarks();
  // 現在のentries_に実在するものだけを返す(一覧再読み込み後に古い
  // 名前が残っていても、存在しないものは黙って除外する)。
  std::vector<std::string> transientMarkedNames() const;

  // 現在の一覧をそのまま返す(ARC展開後の「新規に現れた名前」の差分
  // 計算で、Application側が展開前に保存しておいた名前集合と比較する
  // ために使う)。
  const std::vector<DirectoryEntry> &currentEntries() const;

  // 一覧順のマーク済みエントリ。または――何も明示的にマークされて
  // いなければ――単に現在選択されている1エントリ(明示的な複数選択
  // なしのアクションが「カーソルの下にあるもの」に対して働くという、
  // よくあるファイルマネージャの慣習に合わせている)。マークが無く
  // 有効な選択も無い場合は空。
  std::vector<DirectoryEntry> targetEntries() const;

  // K-26(^Ol、usilicrc自身のLSセクション: 「When the flag is set,
  // file-selector will display only filename」): 各表示行を、完全な
  // `ls -alF`行と裸のファイル名だけの間で切り替える。`ls`を
  // 再実行「しない」――entries_は既にDirectoryEntry::lsLineと::name
  // の両方を独立して保持しているので、これは既にパース済みの
  // エントリからview_の各行を再構築するだけの、純粋な表示層の作業
  // である(選択/マーク/カーソル位置は全て手を付けられない。
  // entries_自体が決して変わらないため)。ToggleScrollMode/K-25と
  // 同じ方法で実行間をまたいで永続化される――Application自身の
  // 処理を参照。
  void setFilenameOnlyMode(bool enabled);
  bool filenameOnlyMode() const;

  // ユーザーがこの一覧に対するFGREP/EGREP/CUT検索から戻ってきた時に
  // Application::openSelectedGrepMatch()/returnFromCut()によって
  // 設定される――モードラインのtrailing2フィールド("FGREP:
  // pattern"/"CUT: pattern")として表示され、mode.c自身のT_grepケース
  // (`sprintf(lp->prev, "%s: %s", ...)`、「直前にどの検索から来たかを
  // 覚えている」という注釈で、実際のCUIスクリーンショットで確認済み)
  // からの移植。legacy自身のT_dirケースにおけるis_prev(prev,
  // M_fgrep/M_egrep)チェックは、ここでCUTへ一般化された同じ発想で
  // ある。CUTも同じ「prev」スロットへ戻ってくることがあるため。
  // enterDirectory()によってクリアされる(新しい`ls`は、この注釈が
  // 参照していた古い一覧を生み出した検索が何であれ無効にする)。
  // カーソルを動かすたびには「クリアされない」――これはlegacy自身の
  // T_dir処理と一致しており、本物のディレクトリ変更でのみこれを
  // クリアする。意図的に汎用的(名前上「grep専用」ではない): どの
  // 「種類」の検索がそれを設定したかはApplicationが別に追跡している
  // (自身のlastDirectorySearchKind_参照)ので、Backは正しい結果画面へ
  // ルーティングできる――このクラスはテキストを表示することだけを
  // 所有し、それが何を意味するかは所有しない。
  void setSearchAnnotation(const std::string &text);
  bool hasSearchAnnotation() const { return !searchAnnotation_.empty(); }

  // in-place GREPセッション(Application::activateGrepInPlace()/
  // deactivateGrepInPlace()、詳細は.steering/20260909-grep-inline-
  // highlight/design.md参照)が有効な間、モードラインの`name`スロット
  // を"GREP"に、プログラムバッジ(通常は"(ls)")を"(grep)"に、一時的に
  // 差し替える。setSearchAnnotation()とは別の状態――こちらは検索が
  // 「終わった後」に戻ってきた画面への一回限りの注釈だが、こちらは
  // 検索の「最中」であることを示す、セッションの生死そのものを表す
  // フラグである。
  void setGrepInPlaceOverlay(bool active);

  // 2026-09-10追加、直接のユーザー指摘("とにかくハイライト表示は
  // 出てくれないと...何にヒットしたかもわからない"): in-place GREP
  // セッション中、マッチした「全ての」行を(現在のマッチだけでなく)
  // 同時にBold(太字)表示する――カーソル行のStandout/dired式マークの
  // Underlineとは独立した3つ目のチャネルなので、3つとも共存・区別
  // できる。ls一覧の行は(他のモードのFileMode::attributesとは異なり)
  // 元々オーバーストライク由来の属性を一切持たない――確認済み、
  // content_view.hpp自身のLine::attributesのコメント参照――ので、
  // クリア時に「元に戻す」べきスナップショットを別途保持する必要が
  // 無く、単に空へ戻すだけでよい。`indices`は`entries_`と同じ
  // インデックス空間(GrepMatch::sourceLineIndex)。
  void setGrepMatchHighlight(const std::set<int> &indices);
  void clearGrepMatchHighlight();

  // "Invalid mode"(legacy mode.cのdo_edit()、S_errvからの逐語移植
  // ――str.cの文字列テーブル参照)を、現在のディレクトリパスの
  // 代わりに、モードラインの`name`スロットに下線付きで、次に
  // clearEditError()が呼ばれるまで表示する――直接のユーザー依頼から
  // の移植: ディレクトリエントリ(ファイルではなく)に対して^Je(EDIT)
  // を押した場合、黙って何もしない(Application::executeEdit()は以前
  // フィードバック一切なしで単にfalseを返していた)代わりにこの
  // 「適切な」エラーをそこに表示すべき。executeEdit()から呼ばれる。
  // 下のreportError()の薄い便利ラッパー(それ自身の元々の、そして
  // しばらくの間唯一の、使用例)。
  void reportEditError();

  // reportEditError()と同じ、下線付きモードライン`name`スロットでの
  // 表示だが、呼び出し元が任意に指定できるメッセージ付き――
  // CpMvRmMode::isWritable()の失敗(executeMove()/executeRemove()、
  // Application自身のコメント参照)のために2026-08-21に一般化された:
  // 直接のユーザー依頼(docs/HISTORY.md:1840、"chown実行後のRM、MVでは
  // ファイルが選べない...その旨何らかのメッセージ表示があれば親切")
  // が、権限拒否されたRM/MVの対象が、legacyの黙ったマーク時ビープの
  // 代わりに自身を説明することを求めた。docs/HISTORY.md:8912が既に
  // ^Jeの「Invalid mode」に対して確立したのとまさに同じ表示を再利用
  // しており、2つ目の異なる挙動をするステータスメッセージ機構を
  // 新たに発明するのではない。
  void reportError(const std::string &message);

  // in-place GREPが0件のマッチも見つけられなかった時、モードラインへ
  // 一時的に表示するメッセージ(2026-09-09追加、2026-09-10改訂)――
  // 直接のユーザー報告(=fv、drawPromptLabel()自身の同日の修正コメント
  // 参照)を受け、LOCATE自身の"Updating database..."/"Disk usage..."
  // と同じ体裁(短いモード名のラベル+メッセージ+起点の3フィールド)に
  // 合わせた: `name`="GREP"、`trailing2`にこのメッセージ、`trailing3`
  // に現在のディレクトリ(検索元の起点、"戻り値")を表示する――
  // reportError()とは意図的に別のstate(showEditError_)にしてある:
  // reportError()は"Invalid mode"のような実際のエラーで`name`を丸ごと
  // 乗っ取る一方、こちらは検索結果が単に0件だっただけで、起点情報
  // (currentDirectory_)を隠す理由が無いため。
  void reportGrepNoMatchMessage(const std::string &message);
  void clearGrepNoMatchMessage();

  // reportEditError()/reportError()が設定したものをクリアし、次の
  // 再描画時にモードラインの`name`スロットを現在のディレクトリパスへ
  // 戻す。Application::handleCommand()の先頭で、ディスパッチされる
  // 全てのコマンドに対して1回、無条件に呼ばれる(モードラインの
  // プロバイダ自体から読み取り時にではない――理由は自身のコメント
  // 参照: -gの描画ループは、本物のキー入力だけよりもはるかに頻繁に
  // redraw()を呼ぶため、このフラグの読み取って消すバージョンはそこで
  // ほぼ即座に消えてしまった。実際の2026-08-17の報告。また
  // dispatchByte()からでもない。実際の再描画のきっかけ――および
  // 一部の呼び出し元/テスト――の一部は、それを経由せず直接
  // handleCommand()へ到達するため)。現在何も表示していなければ
  // 何もしない。
  void clearEditError();

  // `label`("CP"/"MV"/"RM")を、モードラインの`name`スロットに、
  // ディレクトリパスに代わってプレーンなStandoutで表示する――完全な
  // 2026-08-21の根拠についてはモードラインプロバイダ自身のコメント
  // (directory_mode.cpp)を参照。Application::startCopyPrompt()/
  // startMovePrompt()/startRemovePrompt()が操作を準備する時に呼ばれる。
  void setPendingOperationLabel(const std::string &label);
  // setPendingOperationLabel()が設定したものをクリアする。
  // Application::startDestinationPrompt()が準備を消費した時
  // (実際にプロンプトを表示するかどうかに関わらず)、および
  // enterDirectory()自身(自身のmarkedIndices_.clear()と並んで)
  // によって呼ばれる。これにより、他所へナビゲートしたことで残った
  // 古びた準備状態が、もはや実際には準備されていない操作のラベルを
  // 表示し続けることがないようにする。
  void clearPendingOperationLabel();

  // "ARC: <archivePath>"を、ハイライトされたtrailing3フィールドとして
  // 表示する(2026-08-26、^X^M/^X aのアーカイブ展開成功後にここへ着地
  // した際の直接のユーザー依頼/仕様、Application::
  // pollPendingArchiveExtractShellRun()自身のコメント参照)――純粋に
  // 情報提供のみ(「このディレクトリの新しく展開された内容がどこから
  // 来たか」)。setPendingOperationLabel()自身の準備状態と同じ方法で
  // クリアされる: enterDirectory()自身によって。ユーザーが他所へ
  // ナビゲートしたら残り続けないように。
  void setArcExtractSourceAnnotation(const std::string &archivePath);
  // 2026-08-27の追加、直接のユーザー提案("ディレクトリ（ls）の
  // 戻り値に「ARC:」等モードへの帰還があるときは^J^Jルーチンへ移行
  // じゃまずいの？"): Application::handleCommand(Command::Back)が、
  // この注釈を、すぐ上のhasSearchAnnotation()を既にチェックしている
  // のと「同じ」方法でチェックできるようにする(その前例は、汎用の
  // ModeStackベースgoBack()の代わりにBackをreturnToGrepResults()/
  // returnToCutResults()へルーティングする――これは、代わりに
  // goToRecentFile()/^J^Jへルーティングするという点を除いて同一の
  // 形である。ModeStackフレームは「Archiveモードへ戻る」を全く表現
  // できないため、pollPendingArchiveExtractShellRun()自身のコメント
  // 参照)。
  bool hasArcExtractSourceAnnotation() const { return !arcExtractSourceAnnotation_.empty(); }
  // 2026-08-27の追加、直接のユーザー報告+自身が提案した修正
  // ("①のあと「ＢＳ世界の〜」を閲覧したりutf8/へ降りたりした時点で
  // ARC履歴は抹消し"): サブディレクトリへ降りることは既に
  // enterDirectory()へ到達する(これは自力でこの注釈をクリアする、
  // setArcExtractSourceAnnotation()自身のコメント参照)が、ファイルを
  // 「開く」ことは決してそうならない――Application::
  // returnToDirectory()自身の「ファイルの本物の親ディレクトリを
  // 辿る」修正は、ファイルが「現在表示中の」ディレクトリから開かれた
  // 場合(ここでのよくあるケース)に丁度何もしないので、そうしないと
  // この注釈はファイルの閲覧を無期限に生き残ってしまう。
  // Application::openSelectedEntry()は、選択されたもの(ファイル/EXEC
  // /アーカイブいずれも)を開くために離れる直前に、この理由で明示的に
  // これを呼ぶ。
  void clearArcExtractSourceAnnotation() { arcExtractSourceAnnotation_.clear(); }

  // カーソル自身の行を、currentDirectory_に対してHistory
  // (PositionCategory::Directory)へ記録する。descendTo()/goBack()が
  // ナビゲートして離れる前に自力で既に行っているのと同じ記帳――
  // 公開された理由(2026-08-27、実際の報告: "^J^K→カーソル位置が
  // 「usilicrc」じゃない")は、Application::openSelectedEntry()の
  // ため。これはFile/Archive/Execモードへ、それらのどちらも経由
  // せずに(ディレクトリからディレクトリへのナビゲーションではない
  // ため)切り替わるので、ファイルを開く直前にユーザーが選択していた
  // 行は実際には決して保存されていなかった――後でこの同じ
  // ディレクトリへのgoToFileSelector()/素のenterDirectory()には、
  // 復元すべきものとして、古びた、場合によってはずっと以前に保存
  // された位置しか無かった。
  void saveCurrentCursorPosition();

 private:
  DirectoryEntry parseLsLine(const std::string &line) const;
  // "./"/"../"は決して有効なCP/MV/RMの対象ではない――toggleMark()
  // 自身のコメント参照。
  bool isSelfOrParentEntry(int index) const;
  static std::string parentDirectory(const std::string &dir);

  ContentView &view_;
  ModeStack &modeStack_;
  History &history_;
  const Configuration &configuration_;

  std::string currentDirectory_;
  std::vector<DirectoryEntry> entries_;
  std::set<int> markedIndices_;
  // setTransientMarkedNames()自身のコメント参照。
  std::set<std::string> transientMarkedNames_;
  bool filenameOnlyMode_ = false;
  std::string searchAnnotation_;
  bool showEditError_ = false;
  std::string editErrorMessage_ = "Invalid mode";
  std::string pendingOperationLabel_;
  std::string arcExtractSourceAnnotation_;
  // in-place GREPセッション(Application::activateGrepInPlace()/
  // deactivateGrepInPlace())中だけtrueになる――モードラインの
  // 主要フィールドを一時的に"GREP"/"(grep)"へ差し替える
  // (setGrepInPlaceOverlay()自身の宣言コメント参照)。
  bool grepInPlaceActive_ = false;
  // reportGrepNoMatchMessage()自身の宣言コメント参照。
  bool grepNoMatchActive_ = false;
  std::string grepNoMatchMessage_;
};

}  // namespace fm
