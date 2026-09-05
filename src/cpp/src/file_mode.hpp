#pragma once

#include <memory>
#include <string>
#include <vector>

#include "charset.hpp"
#include "content_view.hpp"
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

class FileMode {
 public:
  FileMode(ContentView &view, History &history, Charset::Encoding encoding, int width);

  // pathの全内容を読み、PipeBuffer経由で変換/折り返しし、表示する。
  // このパスに対して記憶済みのカーソル位置がHistoryにあればそれを
  // 復元する。ファイルが読めない場合(存在しない、権限が無い、等)は
  // false。
  bool loadFile(const std::string &path);

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
  bool reflow(int newWidth);

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

  // モードラインの名前フィールド(通常は素のファイル名)と、その
  // trailing2フィールド(通常はファイルが属するディレクトリ、
  // コンストラクタ自身のコメント参照)の「両方」を上書きする――
  // Application::openSelectedLocateResult()のFile分岐のために追加:
  // LOCATE経由で到達したファイルは、ちょうど今閲覧していたディレクト
  // リの近くにあるとは限らないため、そこでは素の所属ディレクトリ
  // 表示より、実際にそれを見つけた検索の方が有用である(実際の
  // ユーザー報告: 「旧版と違うので注意」――legacy自身のこのケースの
  // 「戻り値」フィールド)。`label`は名前になる(例えば"LOCATE"、
  // その検索自身の結果画面名と一致させる――ユーザーからの直接の
  // 訂正、スクリーンショット=aw: 「『LOCATE: .txt』はモード名では
  // なく戻り値、モード名は『LOCATE』で」により、検索自身のラベルは
  // detailへの接頭辞としてではなく、ここでは「名前」スロットに属する
  // ことが明確になった)。`detail`はtrailing2へそのまま入る
  // (Finder風の中間省略で収まるよう切り詰め――ContentView::
  // withTruncatedFields()自身のコメント参照――ユーザーからの直接の
  // 依頼と一致: 「戻り値も検索一覧同様『/Applications/Adobe
  // Acrob...source/.../HKSCS.txt』形式で」――LOCATE自身の結果一覧の
  // 行が既に使っているのと「同じ」切り詰めの慣習。ファイルの完全な
  // 絶対パスは、まさにその慣習が存在する対象である「長い共通接頭辞、
  // 異なる末尾」というテキストそのものであるため)。全てのloadFile()
  // 呼び出しの先頭で自動的にクリアされる(自身のコメント参照)。
  void setSearchOrigin(const std::string &label, const std::string &detail);

  // 2026-08-28追加、実際のユーザー報告(「これでは^Gで（ls）に戻って
  // しまう。検索結果一覧へ戻るのが筋なので戻り値は『EGREP: ^-』で
  // あるべき」): DirectoryMode::setSearchAnnotation()を正確に踏襲
  // している(同じ名前、同じ「直接この内容を検索したのではなく、
  // GREPの一致に対するEnter経由で『ここへ』着地した」という意味)
  // ――上のsetSearchOrigin()(名前とtrailing2の「両方」を、
  // LOCATE/INFOの「これは本当にはファイル自身の名前についての話では
  // ない」という枠組みのために置き換える)とは異なり、これは
  // trailing2「だけ」に触れる: ファイル自身の名前(例えば
  // "CLAUDE.md")はそのまま正確に残り、戻り先フィールドだけが変わる。
  // DirectoryMode自身のname/searchAnnotation_が既に保っているのと
  // 同じ分離。
  void setSearchAnnotation(const std::string &text);
  bool hasSearchAnnotation() const { return !searchAnnotation_.empty(); }

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
  // ものを渡す)。
  bool loadStream(int fd, bool ownsFd, const std::string &displayName, int initialLineBudget);
  // `targetLineIndex`が現在読み込み済みの末尾以上で、かつストリームが
  // まだEOFに達していなければ、まだ開いているストリームからさらに
  // 読み込む――Applicationが、スクロール/移動コマンドをビューへ委譲
  // する直前に、hasActiveStream()がtrueである間「だけ」呼ぶ(他の
  // 全てのFile mode入口――上のloadFile()――にはストリーミングする
  // ものが無いので、そちらでは常に何もしない)。EOFに達した後、
  // またはそもそもストリームがアクティブでない場合はfalse(純粋に
  // 何もしない)。
  bool ensureLoadedThrough(int targetLineIndex);
  // 2026-09-20追加、直接のユーザー報告("最終行へ来てもいないのに
  // （Exit）が出てこそ価値がある"、legacy実機での再現確認: "(stdin)
  // は1ページ分ずつ読み込みながら表示するうちEOFに出くわす時が必ず
  // 来るんだが旧版では最終ページよりも手前が常だった")――
  // readMoreIntoPipe()自身のO_NONBLOCK化のコメント参照。
  // ensureLoadedThrough()の直前の呼び出しが、既に十分読み込み済み
  // だったため何も試みなかった(false)のか、実際に読もうと試みたが
  // その瞬間には何も新しく届かなかった(false、しかし再試行の余地は
  // ある――streamAtEof_はまだfalseのまま)のかを、呼び出し元
  // (Application::handleCommand()、showPagerExitNotice()自身の
  // コメント参照)が区別できるようにする。直後の1回の
  // ensureLoadedThrough()呼び出しについてだけ意味を持つ。
  bool lastLoadAttemptYieldedNothing() const { return lastLoadAttemptYieldedNothing_; }
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

 private:
  std::vector<ContentView::Line> readAndWrap(const std::string &path) const;
  // loadStream()/ensureLoadedThrough()が共有する: streamFd_から
  // 有限サイズのチャンクで読み込み、それぞれをstreamPipe_へ供給
  // する。streamPipe_->lines()がtargetLineCountに達するか、EOFに
  // 当たる(これはstreamPipe_をflush()もし、まだ保留中の末尾の
  // 途中の行を確定させ、streamOwnsFd_ならstreamFd_を閉じる)まで
  // 続ける。
  void readMoreIntoPipe(int targetLineCount);
  std::vector<ContentView::Line> convertPipeLines() const;
  // 以前のストリーム状態を全てリセットする(所有しているfdを閉じる)
  // ――loadStream()の先頭(2つ目のCLI起動されたファイルが1つ目を
  // 置き換える場合、例えばCommand::PagerNextFile)と、loadFile()の
  // 先頭(防御的に: 通常の即時読み込みが古いストリームを宙ぶらりんの
  // まま残すことは決して無いようにする)で呼ばれる。
  void closeStream();

  ContentView &view_;
  History &history_;
  Charset::Encoding encoding_;
  int width_;
  std::string currentFile_;
  std::string searchOriginLabel_;
  std::string searchOriginDetail_;
  // setSearchAnnotation()自身のコメント参照。新しいloadFile()のたび
  // にクリアされる(新しいファイル/再読み込みは、「前の」ファイルへ
  // 着地させた検索が何であれ、それを無効にする)。同じ関数内ですぐ
  // 上にあるsearchOriginLabel_/Detail_自身のクリアと同じ。
  std::string searchAnnotation_;
  // pagerMode_(2026-08-25): loadStream()が設定し、loadFile()が
  // クリアする――ページャ起動されたファイルのモードラインが、
  // 通常のloadFile()で開かれたファイルとは異なり、サイズ/ディレクト
  // リのtrailingフィールド無しで完全パス(標準入力の場合は何も無し)
  // を表示する理由についてはコンストラクタ自身のModeLineFieldsProvider
  // のコメント参照。
  bool pagerMode_ = false;
  bool pagerReturnsToShell_ = false;
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
};

}  // namespace fm
