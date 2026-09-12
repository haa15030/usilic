#include "directory_mode.hpp"

#include <algorithm>
#include <optional>
#include <sstream>

#include "child_process.hpp"

namespace fm {

namespace {
// application.cpp自身のstripLsFTypeMarker()と同じ(小さいので複製、
// 2026-09-09追加)――`ls -alF`がDirectoryEntry::nameの末尾に付与する
// 型マーカー('/'/'*'/'@'/'='/'|')を、実在パス/アーカイブメンバー名
// との比較の瞬間だけ剥がす。isMarked()自身のコメント参照。
std::string stripLsFTypeMarker(const std::string &name) {
  if (!name.empty() && std::string("/*@=|").find(name.back()) != std::string::npos) return name.substr(0, name.size() - 1);
  return name;
}
}  // namespace

DirectoryMode::DirectoryMode(ContentView &view, ModeStack &modeStack, History &history,
                              const Configuration &configuration)
    : view_(view), modeStack_(modeStack), history_(history), configuration_(configuration) {
  // マークされた行は、カーソルが外れていてもStandoutで描画される
  // ――これが修正する実際の報告についてはContentView::
  // setMarkedPredicate()自身のコメント参照(マーキングは以前、視覚的
  // フィードバックが全く無かった)。
  view_.setMarkedPredicate([this](int index) { return isMarked(index); });
  // 初期状態(CP/MV/RM準備前): マークはUnderlineで描画され、カーソル行
  // 自身の通常のStandoutとは区別される――これが修正する実際の報告に
  // ついてはContentView::setMarkedRowUsesUnderline()自身のコメント
  // 参照('['で一連のエントリをマークすると、カーソルは実際にマーク
  // された最後の行から1行過ぎたところに留まり、その留まった行は、
  // 単に同じStandoutの箱を共有していたという理由だけで「これも
  // マークされている」ように見えてしまっていた)。Application::
  // startCopyPrompt()/startMovePrompt()/startRemovePrompt()は、準備
  // された時点でこれをfalseへ切り替える(setCursorRowAlsoHighlighted()
  // もfalseへ)。「その」画面に対するlegacy自身の参照スクリーン
  // ショットに合わせるため。Directoryへの復帰経路は両方をこの初期の
  // 組に戻す。
  view_.setMarkedRowUsesUnderline(true);
  // モードラインフィールド、disp_window()自身のT_dir/file-selector
  // ケース向けフィールドレイアウト(name0[i]=='/' && *prev0==K_zが
  // 絶対パス全体をそのまま保つ、そのコメント参照)からの移植: 左側に
  // 現在のディレクトリの完全なパス、右側に一覧表示コマンド(例:
  // "(ls)")――選択されたエントリの名前では「ない」。これは、実際の
  // CUIに合わせるよう修正される前の、この移植独自の考案された設計
  // だった。右端の「usilic v1.5b」透かし(rightWatermark、
  // ModeLineFields自身のコメント参照)は、legacyがこの画面で特に
  // 表示する「もう一つ」のもの――実際のCUIスクリーンショットで確認
  // 済み、かつC言語のソースにおいてprev0がT_dirに対して本当に「前の
  // モードのテキストが無い」番兵であること(実際の内容でそのスロット
  // を埋めるFileモードとは異なる)による。「1.5b」はold/src/
  // version.h自身のバージョンタグを反映している――これは同じ
  // プロジェクト、同じバージョンの時代であり、単に実装言語が違う
  // だけなので、ここで別の番号を考案する理由は無い。ここで一度だけ
  // 登録され、enterDirectory()を呼ぶたびに再設定はされない。
  // currentDirectory_/configuration_への生きたコールバックであり、
  // スナップショットではないため。
  // 2026-08-15の追加: `trailing`が、"(ls)"プログラム表示から一覧
  // 自体の合計バイト数へ変更された。他の全モード自身のtrailing
  // フィールドが既に使っている「同じ」慣習に合わせている
  // (contentByteCount()/ContentView::totalByteCount()、自身のコメント
  // 参照)――直接のユーザー依頼("全モードのモードラインに「画面TOP
  // からENDまでのバイト数」を追加してほしい")が、特にDirectoryを
  // まだこれが欠けているモードの1つとして挙げていた。trailing/
  // trailing2/rightWatermarkの「3つ全て」がここで既に使われていた
  // (バイト数は、明白な行き先の無い4つ目の概念になってしまう)ので、
  // "(ls)"は代わりに今trailing2に住んでいる。searchAnnotation_が
  // 設定されていない時だけ表示されるフォールバックとして――
  // 検索注釈(LOCATE/GREP/CUT検索が実際にどのモードから来たか)は、
  // ほぼ常に単に「(ls)」であるプログラム名よりも状況に応じて有用
  // である。これは判断であり、legacyソースで検証されたものではない
  // (この移植には、バイト数を表示するDirectory画面と比較すべき
  // legacyバイナリが無い)。実際に意図されたものと一致しなければ
  // 修正するよう印を付けてある。
  view_.setModeLineFieldsProvider([this](int) -> ContentView::ModeLineFields {
    const auto &ls = configuration_.commandFor(ConfiguredRole::Ls);
    std::string program = ls.argv.empty() ? "" : ls.argv.front();
    std::string programBadge = program.empty() ? "" : "(" + program + ")";
    std::string trailing2 = searchAnnotation_.empty() ? programBadge : searchAnnotation_;
    // fillTrailingToEdgeは、trailing2自身の空かどうか(「(ls)」が
    // フォールバックとして埋まるようになった今では常に非空である)
    // ではなく、特にsearchAnnotation_に紐付けたままにしてある――
    // 実際の退行として、「(ls)」自体が行の右端まで丸ごとパディング
    // されてしまい、renderModeLine()自身のdrawnLen追跡がrightWatermark
    // に到達する時点で既に列数「そのもの」になってしまい、その関数
    // 自身の`drawnLen + watermark.size() <= cols`チェックによって
    // 「usilic v1.5b」が黙って落とされていた――まさにこのラウンドが
    // 修正すべきだった=bd/=be報告の「usilic v1.5b」側の半分であり、
    // 保存すべきものではなかった。
    ContentView::ModeLineFields fields{currentDirectory_, std::to_string(view_.totalByteCount()),
                                        /*fillTrailingToEdge=*/!searchAnnotation_.empty(), "usilic v1.5b", trailing2};
    // reportEditError()/clearEditError()自身のコメント参照――読み取り
    // 時にここではクリアされ「ない」。実際のユーザー報告
    // (2026-08-17、GUIモードのみ: "-cはOKだが-gだといつもの一瞬出て
    // 消える症状")によると、ここでクリアする(このコードの以前の
    // バージョン)と、-g特有で、これがほぼ即座に消えてしまった:
    // -gの実行ループは、単なるキー入力だけで正当化されるよりも
    // はるかに頻繁にredraw()を呼ぶ(マウスホバー追跡、約60Hzの
    // アイドルループ、...)ので、一度読んでクリアするフラグは、
    // メッセージが実際に画面上に表示される前に、それら「たまたまの」
    // 読み取りの1つに消費されてしまった――そこで何もキー入力への
    // 直接の反応以外でredraw()を呼ばない-cでは、これは決して問題に
    // ならない。代わりに、Application::handleCommand()の先頭で、
    // ディスパッチされる全てのコマンドに対して1回呼ばれる
    // clearEditError()によってクリアされる――これにより、この
    // メッセージは、legacy自身のecho_mess()とまさに同じ方法で、
    // 何度たまたま再描画されても持続し、ユーザーが何か他のことを
    // した時にだけ実際に消える。
    //
    // `name`/`nameUnderlined`だけが上書きされる――以前のバージョンは
    // 代わりに、ここで新しい、ほとんどデフォルト構築されたままの
    // ModeLineFieldsを返しており、それはエラーメッセージ自身の表示
    // 期間中、(上で計算された)バイト数/"(ls)"/"usilic v1.5b"の各
    // フィールドを黙って失っていた――実際の報告+スクリーンショット
    // (=cg/=ch)が、前後のモードラインを比較して、それらのフィールド
    // が目に見えて欠けていることを示した。ユーザーが実際に置き換え
    // るよう頼んだ「1つだけの」フィールド("左端モード名...だけを
    // 丸々消してメッセージを表示させたら？")だけが今変わる。他の
    // 全ては下でそのまま普通に描画され続ける。
    if (showEditError_) {
      // (末尾に空白を付けて、メッセージ自身と一緒に下線を引かれ)
      // それ自身の自然な長さのままにしておくのではなく、それが
      // 置き換えるディレクトリ名の幅までパディングされる――後続の
      // 報告により、短い置換文字列がそれ以降の全フィールド(位置/
      // バイト数/trailingの各バッジ)をその差分だけ左へずらして
      // しまうことが分かった。それらのフィールド自身の「内容」は
      // その時点で既に正しく保持されていたにも関わらず(上の長い
      // コメントブロック参照)。currentDirectory_がメッセージ自身
      // より短い場合(例: "/")に対して保護されており、その場合は
      // パディングは追加されず、ずれは避けられない。
      std::string message = editErrorMessage_;
      if (message.size() < fields.name.size()) message.resize(fields.name.size(), ' ');
      fields.name = message;
      fields.nameUnderlined = true;
    } else if (view_.grepInPlaceActive()) {
      // in-place GREPセッション中(setGrepInPlaceOverlay()自身の宣言
      // コメント参照): GREP結果一覧(grepView_自身のモードラインプロ
      // バイダ、application.cpp)がname="GREP"/badge="(grep)"を表示
      // するのとまさに同じ見た目を、Directory自身のモードラインへ
      // 一時的に流用する。showEditError_と同時に真になることは無い
      // (マッチ0件時はactivateGrepInPlace()がセッションを開始せず、
      // 代わりにreportGrepNoMatchMessage()を呼ぶだけ)ので、優先順位
      // 自体に実質的な意味は無い。trailing3(2026-09-10追加、直接の
      // ユーザー指摘: "モード名が消える以上、検索対象情報も無くなる。
      // ...起点名を戻り値として欲しい"): nameが検索元のディレクトリ
      // パスを乗っ取ってしまうので、grepView_自身のtrailing3(戻り先
      // ラベル)慣習と同じ場所へ、その消えたパス自体を退避する。
      fields.name = view_.grepInPlaceLabel();
      fields.trailing2 = view_.grepInPlaceProgramBadge();
      fields.trailing3 = currentDirectory_;
      fields.fillTrailingToEdge = true;
    } else if (view_.grepNoMatchActive()) {
      // reportGrepNoMatchMessage()自身の宣言コメント参照。
      // 2026-09-10再修正、直接のユーザー指摘("No StringもGREPでは無く
      // 行数の隣に表示"): 以前はメッセージ自体をtrailing2に置き、
      // position/trailingを空にすることで見た目上"GREP"の直後に
      // 来るようにしていた。しかしgrepInPlaceActive_側(すぐ上の分岐)
      // に「マッチN/M」という行数情報を追加した今、成功時と失敗時で
      // メッセージの置き場所が別の仕組み(position vs trailing2)に
      // 分かれているのは筋が悪い――両者とも同じposition枠を使う方が
      // 一貫している(reportGrepNoMatchMessage()自身がここでは無く
      // setPositionTextOverride(message)側でメッセージ本体を設定
      // する)。trailing2は成功時と同じ"(grep)"バッジのまま、trailing
      // (バイト数)も通常通り計算済みの値のまま残す。
      fields.name = view_.grepInPlaceLabel();
      fields.trailing2 = view_.grepInPlaceProgramBadge();
      fields.trailing3 = currentDirectory_;
    } else if (!pendingOperationLabel_.empty()) {
      // ディレクトリパスの代わりに"CP"/"MV"/"RM"、プレーンな
      // Standout(renderModeLine()自身の`name`向けデフォルト――上の
      // showEditError_のステータスメッセージ慣習のために予約されて
      // いるnameUnderlinedでは「ない」。これはエラーではなく「モード」
      // 表示である)――2026-08-21の追加、実際のユーザー報告
      // ("^Xc,m,r等反応無し")、同日の^X^Mフロー変更後
      // (Application::startCopyPrompt()自身のコメント参照): 準備は
      // 以前は完全に無音だった(^X^Mまでプロンプトが表示されなかった)
      // ので、実際には登録されていたキー入力が、登録されなかった
      // ものと見分けが付かなかった。Application::startCopyPrompt()/
      // startMovePrompt()/startRemovePrompt()がsetPendingOperationLabel()
      // 経由で設定し、startDestinationPrompt()(^X^M)が準備を消費
      // (成功「または」失敗/陳腐化)した時にclearPendingOperationLabel()
      // によってクリアされる――そしてここでも、enterDirectory()
      // 自身によって。他所へナビゲートしたことで残った古びた準備
      // 状態が、もはや実際には準備されていない操作のラベルを表示し
      // 続けることがないように(その準備状態自体もそれを生き残らない
      // 理由についてはPendingCpMvRmOperation自身のコメント参照)。
      //
      // 裸の、パディングされていない"CP"/"MV"/"RM"――2026-08-22の
      // 修正、実際のユーザーによる本物の参照スクリーンショット(=ed)
      // との直接比較: legacy自身の準備済みモードラインは、フィールド
      // を自然な幅でそのまま連結する(短い"CP"ラベルは、単にそれ以降
      // のフィールドを、準備されていないレイアウトより早い位置から
      // 始めさせるだけ)。これは、それが一時的に置き換える「周囲の」
      // 準備されていないレイアウトとの整列を意図的に保つ
      // showEditError_自身のパディング(ここには当てはまらない、別の
      // より狭い目的)とは異なる。
      fields.name = pendingOperationLabel_;
      // 切り詰められていない「完全な」ディレクトリパス――2026-08-21の
      // 追加(直接のユーザー報告、"=eiは（ls）からなので戻り値は
      // ディレクトリ名では？")、2026-08-22に修正(=ed/=eeとの別の
      // 直接比較: legacyはパスを、drawPromptLabel「自身」の他所での
      // 移動先パス慣習が行うような短い末尾への「...」切り詰めではなく、
      // 行自身の右端まで空白で右パディングして全体を表示する)。下の
      // fillTrailingToEdgeは、これを端までパディングし「かつ」
      // (それ自身の確立された挙動通り、renderModeLine()自身のコメント
      // 参照)スペース不足のためrightWatermarkを押し出す――これも
      // たまたまの副作用として頼るのではなく、ここで明示的にクリア
      // される。
      fields.trailing2 = currentDirectory_;
      fields.fillTrailingToEdge = true;
      fields.rightWatermark = "";
    }
    // arcExtractSourceAnnotation_(2026-08-26、自身の宣言コメント
    // 参照): 上の2つの分岐(showEditError_/pendingOperationLabel_)
    // からは独立している(これらとこれが同時に起こることは決して
    // 無い――これは、成功したアーカイブ展開がここへ着地した直後に
    // だけ設定され、その時点ではそれらのどちらも有効ではない)――
    // trailing2は通常の"(ls)"バッジのままで、trailing3(と、その
    // ためのスペースを作るためのfillTrailingToEdge/rightWatermark)
    // だけが変わる。ArchiveMode自身の同一のtrailing2+trailing3の
    // バッジ/パスの組み合わせに合わせている。
    if (!arcExtractSourceAnnotation_.empty()) {
      fields.trailing3 = "ARC: " + arcExtractSourceAnnotation_;
      fields.fillTrailingToEdge = true;
      fields.rightWatermark = "";
    }
    // splitLayoutWhenCrowded(2026-08-27の追加、直接のユーザー報告/
    // 自身の以前のHISTORY.md:9608提案への呼び戻し。これは
    // archiveContentView_自身のプロバイダが既に採用している――
    // application.cppの自身のコメント参照): バイト数フィールドを
    // 押し出すほど長い実際のCJKファイル名は、ArchiveModeのcontent
    // viewが既にこのまさにその場合に対して受けている「位置+バイト数
    // を中央に置き、残りのスペースをnameとtrailing2で分割する」処理
    // とは異なり、fitModeLineFields()自身の通常のall-or-nothing階層
    // を介してここで黙って消えていた。DirectoryMode自身の`(ls)`一覧
    // は同一の混雑に遭遇し得る(アーカイブエントリのものだけでなく、
    // 実際のファイル/ディレクトリの「名前」は任意に長くなり得る)
    // ので、今は同じ処理を受ける。
    fields.splitLayoutWhenCrowded = true;
    return fields;
  });
}

bool DirectoryMode::enterDirectory(const std::string &path, bool consultHistory) {
  ProgramCommand ls = configuration_.commandFor(ConfiguredRole::Ls);
  if (ls.argv.empty()) return false;

  // 末尾スラッシュの正規化。currentDirectory_自身がそれを受け取る
  // この関数の末尾だけではなく「ここで」(`ls`のargvが構築される前に)
  // 行う――2026-08-22の修正、実際の報告+スクリーンショット(=en):
  // BSDの`ls`(macOS)は、末尾スラッシュ「無し」のディレクトリへの
  // シンボリックリンク引数を、常にデリファレンスするGNU/Linuxの`ls`
  // とは異なり、(`ls -d`と同様に)「その内容を一覧表示する」ではなく
  // 「シンボリックリンク自体を記述する」として扱う――`/tmp`自体が、
  // macOSではまさにそのようなシンボリックリンク(-> `/private/tmp`)
  // である。executeCopy()/executeMove()自身の移動先ナビゲーション
  // (enterDirectory(destination))は、ユーザーがプロンプトに実際に
  // 入力したものをそのまま渡すが、それが既に「/」で終わっている
  // 理由は無かった――「/tmp」と入力し、その後そこへ着地すると、
  // `/tmp`自身のシンボリックリンク行("1/1 END")だけが表示され、
  // その実際の内容は表示されなかった。
  std::string normalizedPath = path;
  if (!normalizedPath.empty() && normalizedPath.back() != '/') normalizedPath += '/';

  std::vector<std::string> argv = ls.argv;
  // 2026-09-12追加、直接のユーザー依頼(toggleReverseSort()/
  // cycleSortKey()自身のコメント参照): Kind(拡張子順)だけはこの
  // モード自身が後段で並べ替える自前実装なので、本物の`ls`へは何も
  // 渡さない(Default=フラグ無し、実際にはKindの並べ替え前の`ls`
  // 自身の名前順出力が入力になる)。
  if (sortKey_ == LsSortKey::Time) argv.push_back("-t");
  else if (sortKey_ == LsSortKey::Size) argv.push_back("-S");
  if (reverseSort_) argv.push_back("-r");
  argv.push_back(normalizedPath);

  ChildProcess::Options options;
  // fork_bin()のLC_ALL=C強制からの、まさにこの理由での移植:
  // parseLsLine()の月名アンカーは英語の略称しか認識しない(自身の
  // コメント参照、scroll.cのget_col()からの移植)。これが無いと、
  // 英語以外のロケールの`ls`は最後の空白によるヒューリスティックへ
  // フォールバックし、空白を含むファイル名を誤って分割してしまう。
  options.forceCLocale = true;
  ChildProcess::Result result = ChildProcess::run(argv, options);
  if (!result.ok) return false;

  std::vector<DirectoryEntry> entries;
  std::vector<ContentView::Line> lines;
  std::istringstream stream(result.output);
  std::string line;
  while (std::getline(stream, line)) {
    if (line.empty()) continue;
    if (line.rfind("total ", 0) == 0) continue;  // `ls -l`のブロック数合計行、エントリではない
    DirectoryEntry entry = parseLsLine(line);
    ContentView::Line contentLine;
    contentLine.text = filenameOnlyMode_ ? entry.name : line;
    if (!filenameOnlyMode_) contentLine.highlightStartColumn = entry.nameStartColumn;
    lines.push_back(std::move(contentLine));
    entries.push_back(std::move(entry));
  }

  if (sortKey_ == LsSortKey::Kind) {
    // cycleSortKey()自身のコメント参照: macOS/GNU lsのどちらにも
    // 拡張子順オプションが無いため自前で並べ替える。"./"/"../"は
    // ナビゲーション用の特殊エントリなので、常に(ls自身が出力した
    // 元の並び順のまま)先頭に固定し、反転(-r)の対象にもしない。
    std::vector<std::size_t> order(entries.size());
    for (std::size_t i = 0; i < order.size(); i++) order[i] = i;
    auto isNav = [&](std::size_t idx) { return entries[idx].name == "./" || entries[idx].name == "../"; };
    auto extensionOf = [](const std::string &name) {
      std::string stripped = stripLsFTypeMarker(name);
      std::size_t dot = stripped.find_last_of('.');
      return (dot == std::string::npos || dot == 0) ? std::string() : stripped.substr(dot + 1);
    };
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
      bool aNav = isNav(a), bNav = isNav(b);
      if (aNav != bNav) return aNav;
      if (aNav && bNav) return false;  // どちらもナビゲーション用: stable_sort自身が元の順序を保つ
      return extensionOf(entries[a].name) < extensionOf(entries[b].name);
    });
    if (reverseSort_) {
      auto firstNonNav = std::find_if(order.begin(), order.end(), [&](std::size_t idx) { return !isNav(idx); });
      std::reverse(firstNonNav, order.end());
    }
    std::vector<DirectoryEntry> reorderedEntries;
    std::vector<ContentView::Line> reorderedLines;
    reorderedEntries.reserve(entries.size());
    reorderedLines.reserve(lines.size());
    for (std::size_t idx : order) {
      reorderedEntries.push_back(std::move(entries[idx]));
      reorderedLines.push_back(std::move(lines[idx]));
    }
    entries = std::move(reorderedEntries);
    lines = std::move(reorderedLines);
  }

  // 末尾スラッシュの正規化のみ(正規化「ではない」――シンボリック
  // リンクの解決も、相対から絶対への変換も無い): CUIモードライン
  // 自身のディレクトリ表示慣習に合わせている(CLAUDE.mdの実際の
  // スクリーンショット比較参照)。normalizedPath(上で、`ls`のargvが
  // 構築される前に計算済み)を再利用し、同じ正規化を2回導出しない。
  currentDirectory_ = normalizedPath;
  searchAnnotation_.clear();  // 新しい`ls`は古い検索を無効にする――setSearchAnnotation()自身のコメント参照
  entries_ = std::move(entries);
  markedIndices_.clear();  // マークはそれが行われた一覧にスコープされる。新しい`ls`はそれらを無効にする
  // setTransientMarkedNames()自身のコメント参照(2026-09-08追加)――
  // 上のmarkedIndices_と同じ理屈: 一時マークもそれが行われた一覧に
  // スコープされる。
  transientMarkedNames_.clear();
  // CP/MV/RMの準備状態も、それが行われたディレクトリにスコープされて
  // いる(Application::PendingCpMvRmOperation自身のコメント参照)――
  // ここでも(Application自身の状態だけでなく)「ラベル」をクリア
  // することで、新しい`ls`(例えば別のディレクトリへのナビゲート)が
  // 既に陳腐化させた準備状態に対して、モードラインが"CP"等を表示し
  // 続けないようにする。
  clearPendingOperationLabel();
  // setArcExtractSourceAnnotation()自身のコメント参照: 新しい`ls`
  // (成功したアーカイブ展開がちょうど着地したディレクトリからの
  // 実際の離脱ナビゲート)はこれを無効にする。すぐ上のCP/MV/RM
  // 準備状態ラベルと同じ寿命。
  arcExtractSourceAnnotation_.clear();
  // cancelPendingCpMvRm()/startDestinationPrompt()が自身の準備解除
  // 経路で行うのと同じ復元――ここでクリアされる古びた準備状態
  // (別のディレクトリへのナビゲート)は、カーソル行自身のハイライト
  // を抑制されたままにしてはならない。2026-08-27:
  // setMarkedRowUsesUnderline(true)が、まさに同じ理由でこれと並んで
  // 追加された――startCopyPrompt()等自身の移動先プロンプトが有効な
  // フェーズは両方を一緒に抑制する(自身のコメント参照)。そして
  // ここへの「成功した」CP/MV/RMの着地(Application::
  // pollPendingCpMvRmShellRun()からのdescendTo()/enterDirectory()経由)
  // は、取り消し経路が既に明示的に行っているのと同様に、それを復元
  // しなければならない経路の1つである――これらの経路はいずれにせよ
  // 全てenterDirectory()に収束するので、代わりにここで一元化されて
  // いる。
  view_.setCursorRowAlsoHighlighted(true);
  view_.setMarkedRowUsesUnderline(true);
  view_.setLines(std::move(lines));

  // K-48(^X^D、「DIR: Select directory name」)自身のデータソース
  // ――正常に一覧表示された全てのディレクトリは「アクセス済み」と
  // 数える(HELP.jp 2.9自身の広い表現)。History::pushRecent()の既存の
  // 「最初の出現優先/上限付きサイズ」挙動(自身のコメント参照)に
  // 合わせている――ここで追加の記帳は不要。
  history_.pushRecent(RingCategory::RecentDirectory, currentDirectory_);

  auto pos = consultHistory ? history_.positionFor(PositionCategory::Directory, currentDirectory_) : std::nullopt;
  if (pos) {
    int idx = pos->row;
    if (idx < 0) idx = 0;
    if (idx >= static_cast<int>(entries_.size())) idx = static_cast<int>(entries_.size()) - 1;
    view_.setCursorIndex(idx < 0 ? 0 : idx);
  } else {
    view_.setCursorIndex(0);
  }
  view_.redraw();
  // COLUMNディレクティブ(applyColumnAutoDisplay()自身のコメント参照)
  // ――ここが「新しい一覧を読んだ直後」のちょうどそのタイミングで
  // あり、カーソル位置が既に確定した後に呼ぶ(比較対象がカーソル行の
  // nameStartColumnであるため)。
  applyColumnAutoDisplay();
  return true;
}

void DirectoryMode::setSearchAnnotation(const std::string &text) { searchAnnotation_ = text; }

void DirectoryMode::reportEditError() { reportError("Invalid mode"); }

void DirectoryMode::reportError(const std::string &message) {
  editErrorMessage_ = message;
  showEditError_ = true;
}

void DirectoryMode::clearEditError() { showEditError_ = false; }

void DirectoryMode::setPendingOperationLabel(const std::string &label) { pendingOperationLabel_ = label; }

void DirectoryMode::clearPendingOperationLabel() { pendingOperationLabel_.clear(); }

void DirectoryMode::setArcExtractSourceAnnotation(const std::string &archivePath) {
  arcExtractSourceAnnotation_ = archivePath;
}

const std::string &DirectoryMode::currentDirectory() const { return currentDirectory_; }
const std::vector<DirectoryEntry> &DirectoryMode::entries() const { return entries_; }

const DirectoryEntry *DirectoryMode::selectedEntry() const {
  int idx = view_.cursorIndex();
  if (idx < 0 || idx >= static_cast<int>(entries_.size())) return nullptr;
  return &entries_[idx];
}

std::string DirectoryMode::parentDirectory(const std::string &dir) {
  if (dir.empty() || dir == "/") return "/";
  std::string trimmed = dir;
  if (trimmed.back() == '/') trimmed.pop_back();
  std::size_t slash = trimmed.find_last_of('/');
  if (slash == std::string::npos) return "/";
  return trimmed.substr(0, slash + 1);
}

bool DirectoryMode::descendTo(const std::string &target) {
  saveCurrentCursorPosition();
  // 2026-08-27の修正、実際の報告+再現("①→'/tmp'[return]→「/tmp/
  // （ls）」②→^G→「~/.usilic/（ls）」→^G→「/tmp/（ls）」にならない
  // （~/git/usilic/src/（ls）」"): ^J^K自身のresetNavigationToFloor()
  // は、ModeStack::pushFloor()経由で永続的な2方向のトグルペアを
  // 確立する(goBack()自身のfloor-pop処理は、1回きりのpopではなく、
  // 毎回入れ替えて再度pushする――自身のコメント参照)。ここへの着地
  // (CP/MV/アーカイブ展開、または他のdescendTo()呼び出し元)は、
  // 以前は「常に」、floorを含む既にスタックにあったものの「上に」、
  // プレーンなフレームをpushしていた――「通常の」複数段階の降下
  // (巻き戻すべき場所の本物のスタック)に対しては問題ないが、floorが
  // 既に有効な場合は誤り: そのプレーンなフレームをpopする(最初の
  // ^G)と、降下が始まった場所へ正しく戻るが、「2回目の」^Gは
  // その下に座っている「古びた」floorをpopしてしまい、`target`
  // (ちょうど到達した場所)とのトグルを続ける代わりに、元の^J^Kの
  // ジャンプが記録した、ずっと無関係になったpreviousDirectoryへ
  // どこであれテレポートしてしまう。ここでもgoBack()自身のfloor
  // 入れ替えを反映している: floorから降下することは、それを積み
  // 重ねるのではなく、新しいもの(target <-> ここ)に置き換える。
  // これにより、トグルは最も最近訪れた2つの実際の場所に固定された
  // ままになる。
  if (const ModeStackFrame *top = modeStack_.top(); top && modeStack_.isFloor(*top)) {
    std::string previous = currentDirectory_;
    if (!enterDirectory(target)) return false;
    modeStack_.pushFloor(target, previous, nullptr, 0);
    return true;
  }
  ModeStackFrame frame;
  frame.directory = currentDirectory_;
  frame.name = currentDirectory_;
  modeStack_.push(std::move(frame));

  if (!enterDirectory(target)) {
    // 降下できなかった(権限拒否、削除との競合、...): pushを取り消し
    // その場に留まる。legacy自身のaccess(p, R_OK)失敗時にT_dirに
    // 留まる挙動に合わせている。
    modeStack_.pop();
    return false;
  }
  return true;
}

bool DirectoryMode::openSelected() {
  const DirectoryEntry *entry = selectedEntry();
  if (!entry || !entry->isDirectory) return false;
  // "./"(2026-08-23の修正、直接のユーザー報告): 以前は完全な
  // 何もしない状態だった。legacy自身の等価物(そしてこの移植自身の
  // ^J^K/GoToFileSelectorは、既にDirectoryモード「内」にいる時、
  // 既に同じ方法で修正済み――Application::goToFileSelector()自身の
  // コメント参照)は、既に表示中のどこかへ「着地」する場合でも
  // 再一覧表示を兼ねる――ここで`ls`を再実行することは、まさに、
  // (例えばCPでファイルをその場で上書きした直後の)古びたサイズ/
  // mtime列を、離れて戻ることなくリフレッシュする方法である。
  // これが(enterDirectory()を直接呼ぶのではなく)refresh()を呼ぶ
  // 理由については自身のコメント参照――同日の後続の修正: 保存済み
  // 履歴位置を参照すると、ユーザーが現在いる場所に留める代わりに
  // カーソルが古びた行へスナップしてしまった。
  if (entry->name == "./") return refresh();

  std::string target;
  if (entry->name == "../") {
    target = parentDirectory(currentDirectory_);
  } else {
    target = currentDirectory_;
    if (!target.empty() && target.back() != '/') target += '/';
    target += entry->name;
  }
  return descendTo(target);
}

bool DirectoryMode::refresh() {
  const DirectoryEntry *before = selectedEntry();
  std::string selectedName = before ? before->name : std::string();
  if (!enterDirectory(currentDirectory_, /*consultHistory=*/false)) return false;
  if (!selectedName.empty()) {
    for (std::size_t i = 0; i < entries_.size(); i++) {
      if (entries_[i].name == selectedName) {
        view_.setCursorIndex(static_cast<int>(i));
        view_.redraw();
        break;
      }
    }
  }
  return true;
}

bool DirectoryMode::goToParent() {
  std::string target = parentDirectory(currentDirectory_);
  if (target == currentDirectory_) return false;  // 既にルート("/")にいる、legacy自身のそこでの何もしない挙動に合わせている
  return descendTo(target);
}

bool DirectoryMode::goBack() {
  if (modeStack_.empty()) return false;
  saveCurrentCursorPosition();
  auto frame = modeStack_.pop();
  if (!frame) return false;
  if (modeStack_.isFloor(*frame)) {
    if (frame->previousDirectory.empty()) return false;
    std::string target = frame->previousDirectory;
    if (!enterDirectory(target)) return false;
    // directory/previousDirectoryを入れ替えて再度push――理由は
    // この関数自身の宣言コメント参照(1回きりのpopではなく本当の
    // トグル)。
    modeStack_.pushFloor(target, frame->directory, nullptr, 0);
    return true;
  }
  return enterDirectory(frame->directory);
}

bool DirectoryMode::canGoBack() const { return !modeStack_.empty(); }

void DirectoryMode::resetNavigationToFloor(const std::string &previousDirectory) {
  modeStack_.pushFloor(currentDirectory_, previousDirectory, nullptr, 0);
}

void DirectoryMode::saveCurrentCursorPosition() {
  if (currentDirectory_.empty()) return;
  History::CursorPosition pos;
  pos.row = view_.cursorIndex();
  pos.totalRows = static_cast<int>(entries_.size());
  pos.top = 0;  // ContentViewは復元時にcursorIndexから可視ビューポートを再計算する。ここでは重要ではない
  pos.lineNumber = -1;
  history_.setPosition(PositionCategory::Directory, currentDirectory_, pos);
}

// scroll.cのget_col()からの移植: `ls -alF`出力の日付フィールド
// (最近のファイルは「Mon DD HH:MM 」、古いものは「Mon DD  YYYY 」)
// は、月の略称からファイル名の開始位置まで常にちょうど13バイトで
// あり、月の略称は(上のforceCLocaleがあれば)信頼できる、ロケールに
// 依存しないアンカーである――行の最後の空白を単純に取るのとは
// 異なり、それはファイル名自体に空白が含まれた瞬間に壊れる(例えば
// 「2024-09-14 12.13.08.mp4」のようなFinder/QuickTimeの書き出し)。
// 認識可能な日付フィールドが見つからない場合は最後の空白へ
// フォールバックする(get_col()自身のフォールバックに合わせている、
// 例えば`ls -alF`の形をしていない行に対して)。
DirectoryEntry DirectoryMode::parseLsLine(const std::string &line) const {
  static const char *kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

  std::string searchable = line;
  std::size_t arrow = searchable.find(" -> ");  // シンボリックリンクの参照先――名前の一部ではない
  if (arrow != std::string::npos) searchable.erase(arrow);

  std::size_t nameStart = std::string::npos;
  for (const char *month : kMonths) {
    std::size_t pos = 0;
    while ((pos = searchable.find(month, pos)) != std::string::npos) {
      bool leftBoundaryOk = (pos == 0 || searchable[pos - 1] == ' ');
      bool rightBoundaryOk = (pos + 3 < searchable.size() && searchable[pos + 3] == ' ');
      if (leftBoundaryOk && rightBoundaryOk && searchable.size() > pos + 13) {
        std::size_t candidate = pos + 13;
        if (nameStart == std::string::npos || candidate < nameStart) nameStart = candidate;
      }
      pos += 1;
    }
  }
  if (nameStart == std::string::npos) {
    std::size_t lastSpace = searchable.find_last_of(' ');
    nameStart = (lastSpace == std::string::npos) ? 0 : lastSpace + 1;
  }

  DirectoryEntry entry;
  entry.lsLine = line;
  // searchable(矢印除去済み)を使う。生の`line`ではない――シンボリック
  // リンクの「 -> target」部分は、lsLine(生の表示行)には属するが、
  // (openSelectedEntry()自身の`path += entry->name`など、本物の
  // ファイルシステムパスを構築するために使われる)`name`には属さない。
  // これはここでの元の`line.substr()`では全く考慮されていなかった。
  if (nameStart < searchable.size()) entry.name = searchable.substr(nameStart);
  entry.isDirectory = !line.empty() && line[0] == 'd';
  // 2026-08-17の修正: `ls -alF`自身の末尾タイプ表示文字
  // ('*'実行可能、'@'シンボリックリンク、'='ソケット、'%'ホワイト
  // アウト、'|' FIFO――ディレクトリの'/'は下で別途扱われる)は、以前
  // `name`にそのまま埋め込まれたままになっており、これらのタイプの
  // いずれかを持つ非ディレクトリエントリから構築された全てのパスを
  // 黙って壊していた。ディレクトリ自身の末尾'/'は無害である
  // (opendir()/stat()は"dir"と"dir/"を同一に扱い、openSelected()は
  // 実際にそれが残っていることに「依存している」――自身の
  // `entry->name == "./"`チェック参照――なのでディレクトリはここで
  // 意図的に手を付けられずに残される)。実際の再現(^J^Lで検索 →
  // LOCATE結果を開く → ^J^K、実行可能ファイルだらけのディレクトリへ
  // 着地する――本物の.app/Contentsバンドルがまさにそう見える。この
  // 移植のどの合成テストフィクスチャもchmod +xすることは無い)で、
  // そのようなエントリの「どれ」に対してもEnterが黙って何もしない
  // ことが判明した: 構築されたパス(".../LicensingSettings.xml*")は
  // ディスク上に存在しないので、fileMode_.loadFile()は失敗し、
  // openSelectedEntry()は目に見える変化なくfalseを返していた――
  // まさに報告された「^J^Kを使うと以後反応しなくなる」という症状で
  // あり、以前はより広範な状態破損として誤診断されていた。
  if (!entry.isDirectory && !entry.name.empty()) {
    char last = entry.name.back();
    if (last == '*' || last == '@' || last == '=' || last == '%' || last == '|') entry.name.pop_back();
  }
  entry.nameStartColumn = static_cast<int>(nameStart);
  return entry;
}

// "./"/"../"は決してマーク可能ではない――2026-08-22の修正、実際の
// ユーザー報告("CP:「./」「../」は選択から除外してほしい"): CP/MV/RM
// 自身の対象は、マークされているもの(または、何もマークされて
// いなければカーソル自身のエントリ――下のtargetEntries()自身の
// コメント参照)から直接来る。"cp -R ." / "rm -r .."は、まさに
// このガードが「構造的に」不可能にすることを目的としている類の
// 事故であり、単に「起こりにくい」だけではない。
bool DirectoryMode::isSelfOrParentEntry(int index) const {
  if (index < 0 || index >= static_cast<int>(entries_.size())) return false;
  return entries_[index].name == "./" || entries_[index].name == "../";
}

void DirectoryMode::toggleMark(int index) {
  if (index < 0 || index >= static_cast<int>(entries_.size())) return;
  if (isSelfOrParentEntry(index)) return;
  auto it = markedIndices_.find(index);
  if (it != markedIndices_.end())
    markedIndices_.erase(it);
  else
    markedIndices_.insert(index);
}

bool DirectoryMode::isMarked(int index) const {
  if (markedIndices_.count(index) > 0) return true;
  // setTransientMarkedNames()自身のコメント参照。2026-09-09追記、
  // 直接のユーザー要望("見た目だけで実際は選択済み状態になっていない
  // らしくPROGRAMもCP/MV/RMも反応しない...実際の選択としても使い
  // たい"): 当初(2026-09-08)はmarkedCount()/targetEntries()が
  // markedIndices_だけを見る設計だったが、ユーザーの明示的な選択
  // (AskUserQuestion「統合する」)により、この一時マークもPROGRAM/
  // CP/MV/RMの実際の対象として扱うよう変更した――markedCount()/
  // targetEntries()は今やこのisMarked()自身を経由するので、以下の
  // 判定はその両方に反映される。^Lによる消去(clearTransientMarks()、
  // markedIndices_には触れない)との非破壊性は変わらず保たれている。
  if (index < 0 || index >= static_cast<int>(entries_.size())) return false;
  // stripLsFTypeMarker()自身のコメント参照(2026-09-09追加)――
  // transientMarkedNames_に入る名前の出所(CP/MVのベースネーム、ARC
  // のアーカイブメンバー由来のトップレベル名)はどちらも型マーカー
  // を持たないので、比較する側(entries_[index].name、`ls -F`由来で
  // ディレクトリなら末尾に'/'等を持つ)から剥がして揃える。
  return transientMarkedNames_.count(stripLsFTypeMarker(entries_[index].name)) > 0;
}

std::size_t DirectoryMode::markedCount() const {
  std::size_t count = 0;
  for (int i = 0; i < static_cast<int>(entries_.size()); i++)
    if (isMarked(i)) count++;
  return count;
}

void DirectoryMode::clearMarks() { markedIndices_.clear(); }

void DirectoryMode::markAll() {
  markedIndices_.clear();
  for (int i = 0; i < static_cast<int>(entries_.size()); i++) {
    if (isSelfOrParentEntry(i)) continue;
    markedIndices_.insert(i);
  }
}

void DirectoryMode::setTransientMarkedNames(std::vector<std::string> names) {
  transientMarkedNames_ = std::set<std::string>(names.begin(), names.end());
}

void DirectoryMode::clearTransientMarks() { transientMarkedNames_.clear(); }

std::vector<std::string> DirectoryMode::transientMarkedNames() const {
  std::vector<std::string> result;
  // isMarked()自身の2026-09-09追記のコメント参照(型マーカーの剥がし)。
  for (const auto &entry : entries_)
    if (transientMarkedNames_.count(stripLsFTypeMarker(entry.name)) > 0) result.push_back(entry.name);
  return result;
}

const std::vector<DirectoryEntry> &DirectoryMode::currentEntries() const { return entries_; }

std::vector<DirectoryEntry> DirectoryMode::targetEntries() const {
  std::vector<DirectoryEntry> result;
  // isMarked()自身の2026-09-09追記のコメント参照――markedIndices_
  // (明示的な選択)とtransientMarkedNames_(CP/MV/ARC完了後の一時
  // マーキング)の両方を、entries_の並び順(昇順)でまとめて返す。
  // toggleMark()/markAll()自体が"./"/"../"を決してmarkedIndices_へ
  // 入れず、ARC/CP/MVの一時マーキングも同じく".を含まないので、
  // ここで改めて除外する必要はない。
  for (int i = 0; i < static_cast<int>(entries_.size()); i++)
    if (isMarked(i)) result.push_back(entries_[i]);
  if (!result.empty()) return result;
  // 上のtoggleMark()/markAll()と同じ"./"/"../"除外を、カーソル
  // フォールバックの場合(何も明示的にマークされていない)に対して
  // 行う――markedIndices_自体はもうどちらも含み得ないが、カーソルは
  // 依然として単純にどちらかの上に留まっていることがあり得る。
  if (const DirectoryEntry *entry = selectedEntry()) {
    if (entry->name != "./" && entry->name != "../") result.push_back(*entry);
  }
  return result;
}

void DirectoryMode::setFilenameOnlyMode(bool enabled) {
  filenameOnlyMode_ = enabled;
  std::vector<ContentView::Line> lines;
  lines.reserve(entries_.size());
  for (const auto &entry : entries_) {
    ContentView::Line line;
    line.text = enabled ? entry.name : entry.lsLine;
    if (!enabled) line.highlightStartColumn = entry.nameStartColumn;
    lines.push_back(std::move(line));
  }
  view_.setLines(std::move(lines));
  view_.redraw();
}

bool DirectoryMode::filenameOnlyMode() const { return filenameOnlyMode_; }

void DirectoryMode::applyColumnAutoDisplay() {
  if (entries_.empty()) return;
  const DirectoryEntry *cursor = selectedEntry();
  int nameStartColumn = cursor ? cursor->nameStartColumn : entries_.front().nameStartColumn;
  int remaining = view_.columns() - nameStartColumn;
  bool shouldBeNameOnly = remaining < configuration_.columnThreshold();
  if (shouldBeNameOnly != filenameOnlyMode_) setFilenameOnlyMode(shouldBeNameOnly);
}

void DirectoryMode::toggleReverseSort() {
  reverseSort_ = !reverseSort_;
  refresh();
}

bool DirectoryMode::reverseSort() const { return reverseSort_; }

void DirectoryMode::cycleSortKey() {
  switch (sortKey_) {
    case LsSortKey::Default:
      sortKey_ = LsSortKey::Time;
      break;
    case LsSortKey::Time:
      sortKey_ = LsSortKey::Size;
      break;
    case LsSortKey::Size:
      sortKey_ = LsSortKey::Kind;
      break;
    case LsSortKey::Kind:
      sortKey_ = LsSortKey::Default;
      break;
  }
  refresh();
}

DirectoryMode::LsSortKey DirectoryMode::sortKey() const { return sortKey_; }

}  // namespace fm
