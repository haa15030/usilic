# 旧C実装 → C++版 対応表

`old/tools/gen_map.py`が、旧ソース(`old/src/*.c`)の各関数の直前のコメントから自動生成する。
手で直さず、旧ソースのコメントを直してから再生成すること(`check_legacy_comments.py`で検証できる)。

- 旧関数の位置: `grep -n "^関数名" old/src/*.c`。行番号は書かない(すぐずれるため)。
- 「なし(…)」は、C++版に対応するものが無いことを表す(理由つき)。
- 関数数: 382

## 目次

- [alloc.c](#allocc) (26関数)
- [child.c](#childc) (10関数)
- [code.c](#codec) (30関数)
- [disp.c](#dispc) (7関数)
- [fork.c](#forkc) (17関数)
- [init.c](#initc) (14関数)
- [key.c](#keyc) (29関数)
- [menu.c](#menuc) (30関数)
- [mode.c](#modec) (28関数)
- [scroll.c](#scrollc) (9関数)
- [shell.c](#shellc) (25関数)
- [str.c](#strc) (34関数)
- [sub.c](#subc) (63関数)
- [tc.c](#tcc) (22関数)
- [usilic.c](#usilicc) (8関数)
- [vt.c](#vtc) (30関数)
- [C++からの逆引き](#c-からの逆引き)

## alloc.c

| 旧関数 | 機能 | C++の対応先 |
|---|---|---|
| `save_attr` | 行の中の、nroff形式の重ね打ち(太字`X\bX`、下線`_\bX`)を見つけて、属性列(Alloc.attr)へ属性の開始・終了位置を登録し、重ね打ちの文字は読み飛ばす。 | PipeBuffer::wrapLine (src/cpp/src/pipe_buffer.cpp) |
| `set_attr` | 文字列strの制御文字(`^X`と表示するもの)や、GREPの強調範囲を調べて、属性列(ATTR)へ登録し、属性の個数を返す。 | ContentView::renderLine (src/cpp/src/content_view.cpp); PipeBuffer::wrapLine (src/cpp/src/pipe_buffer.cpp) |
| `esc_pipe` | パイプの読み込みを中断する(ESC)。行(pp)と入力を閉じ、標準入力でなければSIGPIPEを送り、エラーを記録する。 | FileMode::closeStream (src/cpp/src/file_mode.cpp) |
| `linein` | ファイルから1行(最大N_buff/2バイト)を読み、必要ならJIS等の文字コードを変換して、buffへ入れる。許可されていない制御文字があれば、バイナリと判定する。 | PipeBuffer::feed (src/cpp/src/pipe_buffer.cpp) |
| `alloc_pipe` | パイプまたは標準入力から少しずつ読み込み、行(PP)の連結リストを組み立てる。画面幅での折り返し、タブ、重ね打ちの属性、漢字境界、文字コードの変換を行う。initの値で、初期化(-2)・追加読み込み・終了を切り替える。 | PipeBuffer::feed; PipeBuffer::wrapLine (src/cpp/src/pipe_buffer.cpp); FileMode::loadStream (src/cpp/src/file_mode.cpp) |
| `alloc_l` | 二分木のLIST(履歴や候補一覧)を、左から順に(再帰で)行(PP)の連結リストへ変換する。 | ListBrowserMode::show (src/cpp/src/list_browser_mode.cpp) |
| `alloc_list` | LIST(二分木)から行(PP)の連結リストを作り、行数を返す(alloc_lの窓口)。 | ListBrowserMode::show (src/cpp/src/list_browser_mode.cpp) |
| `alloc_str` | STR(連結リストに保存された文字列群)から、行(PP)の連結リストを作り、行数を返す。 | ListBrowserMode::show (src/cpp/src/list_browser_mode.cpp); History (src/cpp/src/history.cpp) |
| `alloc_pp` | `ls -l`や`grep -n`等の出力(buff)を、モード(f_M)に合わせて行(PP)の連結リストへ編集する。 lsの名前の開始桁、grepの行番号、KEY一覧の区切りなどを、行ごとに決める。 | DirectoryMode::parseLsLine (src/cpp/src/directory_mode.cpp); GrepMode::search (src/cpp/src/grep_mode.cpp); ContentView::setLines (src/cpp/src/content_view.cpp) |
| `alloc_head` | アーカイブ(tar/lha/unzip/rpm/deb)の一覧(tvf等の出力)から、各エントリの行(PP)を作り、内容が置かれた位置(tell)と大きさ(split)の見出しを準備する。 | ArchiveReader (src/cpp/src/archive_reader.cpp); ArchiveMode::finishLoadFromOutput (src/cpp/src/archive_mode.cpp) |
| `find_tar_header` | tarの本体から、名前nameで始まるヘッダを探す。ヘッダは512バイト境界に揃っているので、その位置だけを調べる(macOSの`._name`のような、一覧に出ない隠れたエントリと取り違えないため)。 | ArchiveReader (src/cpp/src/archive_reader.cpp) |
| `alloc_tar` | tarアーカイブの各エントリについて、内容の位置(tell)と大きさ(split)を、ヘッダを辿って決める。 | ArchiveReader (src/cpp/src/archive_reader.cpp) |
| `alloc_lha` | lhaアーカイブの各エントリについて、内容の位置と大きさを決める。 | ArchiveReader (src/cpp/src/archive_reader.cpp) |
| `alloc_unzip` | zipアーカイブの各エントリについて、内容の位置と大きさを決める。 | ArchiveReader (src/cpp/src/archive_reader.cpp) |
| `alloc_arg` | コマンド行の文字列を、空白と引用符(`'`、`"`)で区切って、argvの配列を作る。 | ExecMode::run (src/cpp/src/exec_mode.cpp) |
| `free_arg` | alloc_argが作ったargvの配列を解放する。 | なし(C++はstd::vectorの自動解放で足りる) |
| `alloc_split` | ファイル(name)のtell位置からsizeバイトを読み込んで返す(メモリが足りなければ半分にして再試行)。 | CutMode::segmentContent (src/cpp/src/cut_mode.cpp) |
| `alloc_buff` | ファイル記述子fdから全データをバッファへ読み込む(大きさが不明でもよい)。読み込みは中断でき、 f_chkbinに応じて文字コードの判定・変換も行う。 | ChildProcess::run (src/cpp/src/child_process.cpp); FileMode::loadFile (src/cpp/src/file_mode.cpp) |
| `alloc_sh` | SHELLの出力(str)を、行(PP)として連結リストへ積み上げる(入力途中の行を仮想行として扱う)。 | Shell::processOutputBytes (src/cpp/src/shell.cpp) |
| `make_bp` | エラーメッセージ用のバッファ(bp)を作る。メッセージを画面に表示し、nameが有ればエラー記録 (save_error)にも書く。 | DirectoryMode::reportError (src/cpp/src/directory_mode.cpp); ArchiveMode::reportError (src/cpp/src/archive_mode.cpp) |
| `set_pp` | バッファ(cs)を、画面幅で折り返しながら行(PP)の連結リストにする。タブ、制御コード、漢字の境界、バイナリ表示(f_code)を扱う。csがNULLのときは、折り返し幅(bsize)だけを設定する。 | PipeBuffer::feed; PipeBuffer::wrapLine (src/cpp/src/pipe_buffer.cpp) |
| `set_ppp` | ウィンドウのリサイズ後に、古い行ポインタを調整済みのものへ差し替えるため、差し替え先の変数のアドレスを覚えておく(f_setが真のとき)/差し替える(偽のとき)。 | なし(C++は行を配列で持つ。リサイズ時はFileMode::reflowが再折り返しをする) |
| `alloc_winch` | ウィンドウのリサイズ後に、バッファの行を新しい幅で折り返し直して、行ポインタを付け替える。 | FileMode::reflow (src/cpp/src/file_mode.cpp) |
| `free_bp` | バッファ(BASE)とその行のリストを解放し、サイズと行数を0にする。 | なし(C++はデストラクタ/std::vectorの自動解放で足りる) |
| `is_intr` | SIGINTのハンドラ。中断フラグ(Alloc.f_intr)を立て、以後のSIGINTを無視する。 | なし(C++は、読み込みの合間に^Gを確認して中断する) |
| `seek_intr` | パイプの出力を、末尾(またはSIGINTによる中断)まで読み進める。読み込み中はSIGINTで中断できる。 | FileMode::ensureLoadedThrough (src/cpp/src/file_mode.cpp) |

## child.c

| 旧関数 | 機能 | C++の対応先 |
|---|---|---|
| `save_child` | usilicrcの1行(`suffix 'プログラム' 引数... [\| 後段のプログラム]`)を解析して、外部プログラムの定義(CHILD)を連結リストへ追加する。ARCHIVE/EXEC/EDITOR/SHELL/PROGRAMの各設定で共用し (f_childで区別)、`'...'`・`"..."`の引用と`#`のコメントを解釈する。パスにプログラムが無ければ、エラーを記録して捨てる。chpがF10のときは、エラー一覧の解放だけを行う。 | Configuration::Configuration; Configuration::loadRcText (src/cpp/src/configuration.cpp) |
| `check_ext` | ファイル名の末尾(suffix)から、対応する外部プログラム(CHILD)を探す(最長一致)。見つからなければ、ファイルの先頭N_man(0x1000)バイトを調べて、manページ形式ならgroffのCHILDを返す。 | Configuration::programForSuffix (src/cpp/src/configuration.cpp); FileMode::loadAndTransformContent (src/cpp/src/file_mode.cpp) |
| `init_child` | 組み込みのプログラム定義(s_P(P_child))を読み込んで、外部プログラムのリスト(CHILD)を初期化する。ファイルマネジャーかどうかで、読み始める位置が違う。 | Configuration::Configuration (src/cpp/src/configuration.cpp) |
| `get_child` | 外部プログラムのリスト(CHILD)を、suffix・プログラム名(bin)・次の要素のいずれかで検索する。 name指定があれば、見つかった要素の引数の末尾へファイル名を設定する。全引数がNULLのときは、先頭を登録/取得する。 | Configuration::programForSuffix; Configuration::commandFor (src/cpp/src/configuration.cpp) |
| `cat_child` | FILE一覧の表示用に、パス名の末尾へ` (プログラム名)`を付ける。ディレクトリや空白始まりの行には付けない。 | なし(C++のFILE履歴は実パスだけを持ち、プログラムはConfiguration::programForSuffixで都度引く) |
| `free_chp` | 外部プログラム1件(CHILD)と、その引数・suffix・パイプ後段を解放する。 | なし(C++はデストラクタとstd::vectorの自動解放で足りる) |
| `free_child` | 外部プログラムのリスト(CHILD)全体を解放する。 | なし(C++はデストラクタとstd::vectorの自動解放で足りる) |
| `is_child` | ファイルを開くための外部プログラム(CHILD)から、そのファイルを表示するモード (T_tar/T_lha/T_unzip/T_rpm/T_deb/T_gz/T_roff/T_exec/T_bin)を決めて返す。ファイル名をプログラムの引数へ設定する。 | ArchiveKind (src/cpp/src/configuration.hpp); Application::openSelectedEntry (src/cpp/src/application.cpp) |
| `load_chp` | FILE一覧の`ファイル名 (プログラム名)`という行から、括弧内のプログラム名を取り出して、外部プログラム (CHILD)を引き当てる。 | なし(cat_childと対。C++のFILE履歴はプログラム名を持たない) |
| `is_chp` | 外部プログラムが「展開して一覧を出す」種類か判定し、種別の番号を返す(deb=9、rpm=8、tar=5、 lha=4、unzip=3、bzip2=2、パイプ後段付きは6/7)。argを渡した場合は、その名前が後段に使われているかを調べる。 | ArchiveKind (src/cpp/src/configuration.hpp) |

## code.c

| 旧関数 | 機能 | C++の対応先 |
|---|---|---|
| `is_man` | 行の先頭が、roffのマクロ(`."`、`.B`、`.I`、`.aa`〜`.zz`、`.Aa`〜`.ZZ`)で始まるかを見て、manページ形式のテキストかどうかを判定する。 | FileMode::loadAndTransformContent (src/cpp/src/file_mode.cpp) |
| `type_pipe` | メモリ上のバッファの型を、fileコマンドで調べて文字列で返す(バイナリの表示用)。 | なし(C++はfileコマンドを使わない) |
| `type_file` | ファイルの型を、fileコマンドで調べて文字列で返す(バイナリの表示用)。 | なし(C++はfileコマンドを使わない) |
| `check_type` | バッファの文字コードを判定して(init_code)、JIS/BASE64/QUOTEならEUCへ、必要ならEUCから UTF-8へ変換して返す。バイナリなら変換せず、コードC_binを返す。f_chkbinが0のときは判定しない。 | Charset::detect; Charset::jis_to_euc; Charset::sjis_to_euc; Charset::euc_to_utf8 (src/cpp/src/charset.cpp); FileMode::loadAndTransformContent (src/cpp/src/file_mode.cpp) |
| `b_code` | BASE64の1文字を、6ビットの値へ変換する。 | なし(C++はMIMEヘッダ(=?...?=)の復号を移植していない) |
| `q_code` | quoted-printableの16進の1文字(0-9、A-F、a-f)を、数値へ変換する。 | なし(C++はMIMEヘッダ(=?...?=)の復号を移植していない) |
| `decode_base64` | BASE64でエンコードされた文字列(mime〜end)を、元のバイト列(code)へ復号する。 | なし(C++はMIMEヘッダ(=?...?=)の復号を移植していない) |
| `decode_quote` | quoted-printable(`=XX`と`_`=空白)でエンコードされた文字列を、元のバイト列へ復号する。 | なし(C++はMIMEヘッダ(=?...?=)の復号を移植していない) |
| `set_func` | MIMEエンコード語`=?文字集合?B/Q?...?=`の文字集合(ISO-2022-JP、Shift_JIS、EUC-JP)と方式(B/Q)を読み取り、使う変換関数と復号関数を選ぶ。 | なし(C++はMIMEヘッダ(=?...?=)の復号を移植していない) |
| `conv_q` | quoted-printableで復号したバイト列が、JIS・SJIS・EUCのどれかを判定して、EUCへ変換する。 | なし(C++はMIMEヘッダ(=?...?=)の復号を移植していない) |
| `conv_jis` | ISO-2022-JP(JIS)をEUCへ変換する。ESC列(`ESC $ B`、`ESC ( B`等)で状態を切り替えながら、 2バイト文字の最上位ビットを立てる。MIME(buff_i<0)の場合は状態を保持する。 | Charset::jis_to_euc (src/cpp/src/charset.cpp) |
| `decode_mime` | `=?...?=`のMIMEエンコード語を見つけて復号し、EUCへ変換して、出力(euc)へ置き換える。 | なし(C++はMIMEヘッダ(=?...?=)の復号を移植していない) |
| `conv_sjis` | Shift_JISをEUCへ変換する(半角カナは`0x8e`+1バイトの形にする)。MIME(buff_i<0)の場合や、長さの指定(len<0)による文字列の終端まで、を扱い分ける。 | Charset::sjis_to_euc (src/cpp/src/charset.cpp) |
| `conv_euc` | EUC文字列を、制御文字(CODE設定で許可されたものを除く)を落としながらeucへ複写する。MIME (`=?`)の復号も、ここで行う。 | PipeBuffer::feed (src/cpp/src/pipe_buffer.cpp) |
| `jistoeuc` | JISのバッファを、EUCへ変換する(conv_jisの窓口)。 | Charset::jis_to_euc (src/cpp/src/charset.cpp) |
| `sjistoeuc` | SJISのバッファを、新しく確保した領域へEUCとして変換し、元の領域を解放して置き換える。確保に失敗したらエラーを記録する。 | Charset::sjis_to_euc (src/cpp/src/charset.cpp) |
| `euctoeuc` | EUCのバッファを、新しく確保した領域へ複写し(制御文字を落とす、MIMEを復号する)、元の領域を解放して置き換える。 | PipeBuffer::feed (src/cpp/src/pipe_buffer.cpp) |
| `uni_to_utf8` | Unicodeのコードポイントを、UTF-8のバイト列(1〜3バイト)へ変換して、書き込んだ長さを返す。 | uni_to_utf8 (src/cpp/src/charset.cpp) |
| `euc_to_utf8_core` | EUC-JP(半角カナ`0x8e`、JIS X 0208の2バイト)を、変換表(euc_uni.h)でUnicodeに直して、 UTF-8へ変換する。 | Charset::euc_to_utf8 (src/cpp/src/charset.cpp) |
| `euctoutf8` | EUCの領域(*eucp)を、UTF-8へ変換して新しい領域へ置き換える(呼び出し元が領域を所有する場合用)。 | Charset::euc_to_utf8 (src/cpp/src/charset.cpp) |
| `euctoutf8_buf` | 固定サイズの共有バッファ(alloc.cのlinein)の中で、EUCをUTF-8へ変換する。入力がN_buff/2バイトまでなら、最大でも1.5倍にしかならず、必ずバッファに収まる。 | Charset::euc_to_utf8 (src/cpp/src/charset.cpp) |
| `utf8_seqlen` | UTF-8の先頭バイトから、その文字のバイト長(1〜4)を返す。 | Charset::sequence_length (src/cpp/src/charset.cpp); Utf8::sequence_length (src/cpp/src/utf8.cpp) |
| `utf8_width` | UTF-8の1文字(lenバイト)の表示幅(0、1、2カラム)を返す。幅0の結合文字と、幅2の全角・絵文字の範囲表を持つ。 | Charset::display_width (src/cpp/src/charset.cpp); Utf8::display_width (src/cpp/src/utf8.cpp) |
| `count_k` | バイト位置ciが、マルチバイト文字(漢字)の途中にあるかを判定する(途中なら1)。UTF-8ではシーケンス長で、EUCでは0x80以上の連続で数える。 | Charset::splits_character (src/cpp/src/charset.cpp) |
| `count_h` | 文字列の先頭からciバイト目までの表示桁数を数える。タブは8桁ごとの位置へ進める。 | PipeBuffer::wrapLine (src/cpp/src/pipe_buffer.cpp) |
| `count_i` | 表示桁colに対応する、文字列のバイト位置を返す(count_hの逆引き)。 | PipeBuffer::wrapLine (src/cpp/src/pipe_buffer.cpp) |
| `is_utf8` | バッファが、整形式のUTF-8か(マルチバイトの先頭バイトの後に正しい数の継続バイトが続き、少なくとも1つのマルチバイト文字がある)を判定する。素のASCIIは、UTF-8とは扱わない。 | Charset::detect (src/cpp/src/charset.cpp) |
| `init_code` | バッファの文字コード(ASCII/EUC/SJIS/JIS/UTF-8/BASE64/QUOTE/バイナリ)を自動判定する。 UTF-8端末で整形式のUTF-8なら、他の判定より先に決める。制御文字はCODE設定(S_code)で許可されたものだけを許容し、それ以外があればバイナリとする。 | Charset::detect (src/cpp/src/charset.cpp) |
| `make_map` | COPYモードのために、画面の各行を、桁ごとに文字位置を引ける対応表(map)へ展開する。タブや全角文字を考慮して、選択範囲の桁と文字を対応させる。 | CopyMode::loadSnapshot (src/cpp/src/copy_mode.cpp) |
| `toeuc` | SJISの文字列を、一時領域を使って、その場でEUCへ変換する(行末の改行を保つ)。 | Charset::sjis_to_euc (src/cpp/src/charset.cpp) |

## disp.c

| 旧関数 | 機能 | C++の対応先 |
|---|---|---|
| `tabtospc` | nroff形式の下線(`_\b文字`)と太字(`文字\b文字`)の重ね打ちを読み飛ばし、タブを空白に変換して、最大256バイトをstrへコピーする(モードラインに出す文字列の下ごしらえ)。 | PipeBuffer::wrapLine (src/cpp/src/pipe_buffer.cpp) |
| `put_str` | 1行(pp)を画面へ描く。属性列(ATTR)に従って太字・下線を切り替え、制御コードは反転の `^X`や`<XX>`で見せる。f_markが真のときは反転表示にする。ウィンドウ右端でのバックスペースの扱いと、UTF-8では継続バイト0xa0を番兵と誤認しない処理を含む。 | ContentView::renderLine (src/cpp/src/content_view.cpp) |
| `disp_pp` | 画面の本文部分(最下行のモードラインを除く全行)を、ppから順に描き直す。行が尽きた残りは空行にする。 | ContentView::redraw (src/cpp/src/content_view.cpp) |
| `disp_window` | 最下行のモードラインを組み立てて描く。左から、モード名(反転)、現在行/総行数と%、引数(バイト数や`(ls)`等)、直前のモード(戻り先)、右端のプログラム名。幅が足りなければ名前や戻り先の先頭を`...`で切り詰める。文字列を入力させるモード(f_enter)では入力欄を確保する。最後に必要なら本文も描き直す(disp_pp)。 | ContentView::renderModeLine; ContentView::fitModeLineFields; ContentView::ModeLineFields (src/cpp/src/content_view.cpp) |
| `print_line` | モードラインの「現在行/総行数 %」の文字列を作る(linepへ返すか、直接描く)。linepがF2のときは、行番号のジャンプ先を数字で入力させて値を返す(^Gで取消し)。ps0がF2のときは`Mark set`を表示する。 | ContentView::renderModeLine (src/cpp/src/content_view.cpp); Application::resolveGotoLineRow (src/cpp/src/application.cpp) |
| `disp_prev` | モードライン右側の「直前のモード(戻り先)」欄を描く。長ければ先頭を`...`で切り詰め、短ければ空白で埋める。dirがNULLのときは描かずに、整形した文字列をprevへ返す。 | ContentView::ModeLineFields (src/cpp/src/content_view.cpp); ContentView::fitModeLineFields (src/cpp/src/content_view.cpp) |
| `disp_copy` | COPYモードの画面を描く。本文の各行を出し、最下行にモード名・位置・戻り先の3欄を反転で並べる。 | CopyMode (src/cpp/src/copy_mode.cpp) |

## fork.c

| 旧関数 | 機能 | C++の対応先 |
|---|---|---|
| `w_arg` | 特定のsuffix(.dvi等)を必要とする外部プログラム(dvi2tty等)のために、作業ファイル名 `w.<プログラム名>.<元のファイル名>`を作って返す。 | ExecMode::run (src/cpp/src/exec_mode.cpp) |
| `read_buff` | ファイル記述子fdから全データをバッファへ読み込む(alloc_buff)。読み込み中は`echo_mess`で表示する。f_chkbinで、バイナリ判定の有無を切り替える。 | ChildProcess::run (src/cpp/src/child_process.cpp); FileMode::loadFile (src/cpp/src/file_mode.cpp) |
| `fork_key` | KEYモード(キー一覧)の内容を、子プロセスにconf_keyで書かせ、パイプ越しにバッファへ読み込む。 | Application::startHelp (src/cpp/src/application.cpp) |
| `fork_bin` | 外部プログラム(chp)を子プロセスで実行し、その出力(標準出力)をバッファへ読み込む(ls、locate、 groff等)。ディレクトリ一覧(T_dir)ではLC_ALL=Cを強制する(月名がマルチバイトだと、ファイル名の開始桁がずれるため)。 | ChildProcess::run (src/cpp/src/child_process.cpp); DirectoryMode::refresh (src/cpp/src/directory_mode.cpp) |
| `fork_exec` | バッファの内容を作業ファイル(w_arg)へ書き出し、EXEC設定の外部プログラムで開く(do_exec)。終わったら作業ファイルを消す。 | ExecMode::run (src/cpp/src/exec_mode.cpp) |
| `fork_chp` | バッファを作業ファイルへ書き出し、外部プログラム(解凍やgroff等。パイプの後段があれば連鎖)を通した結果を、新しいバッファとして返す。 | ChildProcess::runPipeline (src/cpp/src/child_process.cpp); FileMode::loadAndTransformContent (src/cpp/src/file_mode.cpp) |
| `fork_arc` | アーカイブの一覧を取得する(tar/lha/unzip/rpm/deb)。展開プログラムを子プロセスで実行して、その出力を読み込む。tgzやdebは、パイプの後段(gzip、ar等)をつないで実行する。 | ArchiveMode::loadArchive (src/cpp/src/archive_mode.cpp); ArchiveReader (src/cpp/src/archive_reader.cpp) |
| `fork_split` | CUTのセグメント(pp〜)の行を、子プロセスからパイプへ書き出し、読み戻してバッファにする。制御コードは`^X`の形に直す。 | CutMode::segmentContent (src/cpp/src/cut_mode.cpp) |
| `sgets` | メモリ上のバッファtpから、改行までの1行(最大len)をbuffへ複写して、進めたtpを返す。 | なし(C++は標準ライブラリ(std::getline等)で足りる) |
| `cat_mime` | MIME(`=?...?=`)でエンコードされ、複数行に分かれたヘッダ行を連結して、検索できる形にする。 | なし(C++のGREPは外部のgrepへ委ねる) |
| `fork_grep` | GREP/FGREPを子プロセスのパイプで実行する。検索元(pp)の行を子から書き出して、検索に掛け、マッチした行をバッファへ読み込む(制御コードは`K_a0`+文字で表す)。 | GrepMode::search (src/cpp/src/grep_mode.cpp) |
| `fork_grep2` | ファイルまたはバッファを直接検索する版のGREP。文字コード(EUC/SJIS/JIS)を判定して、検索文字列と照合できる形に直してから検索する。 | GrepMode::search (src/cpp/src/grep_mode.cpp) |
| `fork_man` | manコマンドを子プロセスで実行して、その出力の読み取り口(FILE*)を返す。PAGER=catに、LANGを文字コードに合わせて設定する。`変数=値 ページ名`の形の環境変数の前置に対応する。 | ManMode::loadPage (src/cpp/src/man_mode.cpp) |
| `fork_pipe` | 展開が必要なファイル(gzip/bzip2/tar系/deb等。種別はf_chp)を、外部プログラムの連鎖を子プロセスで実行し、その出力を読む口(FILE*)を返す。 | ChildProcess::runPipeline (src/cpp/src/child_process.cpp) |
| `fork_file` | fileコマンドでファイルの種類を調べ、その出力の1行を返す(最初の1回だけコマンドを引き当てる)。 | なし(C++はfileコマンドを使わない) |
| `fork_du` | duを子プロセスで実行して、その結果を読む口(fd)を返す。実行中を示す目印のファイルを作り、完了すると別の目印を作って知らせる(前回の実行が終わっていなければ-1を返す)。 | DuMode::loadUsage; DuMode::commandArgvFor (src/cpp/src/du_mode.cpp) |
| `read_arc` | アーカイブの1項目(name)の内容を、展開プログラムで取り出してバッファにする(tar/deb/unzip/rpm等で手順が違う)。サイズはsizepへ返す。 | ArchiveMode::extractSelected (src/cpp/src/archive_mode.cpp); ArchiveReader (src/cpp/src/archive_reader.cpp) |

## init.c

| 旧関数 | 機能 | C++の対応先 |
|---|---|---|
| `init_clear` | SIGSEGVのハンドラ。直前のシグナル処理を元に戻し、目印(Init.abort)を立てて、longjmpで init_*の入口へ戻る(クラッシュしても、その画面をエラー表示にして続行するため)。 | なし(C++版はSIGSEGVを回収しない。範囲チェックとRAIIで防ぐ設計) |
| `init_man` | manページ(name)をfork_manで取得し、1画面分ではなくページ全体を先に読み込んで、バッファ(bp)へ載せる。バイト数も合計しておく(モードラインに出すため)。無ければ`No manual entry`。 | ManMode::loadPage (src/cpp/src/man_mode.cpp) |
| `init_pipe` | パイプ入力(標準入力、SHELLからの引き渡し、パイプ系のT_pipe*)のページャを初期化する。モードラインを描き、最初の1画面分まで読み込む。読み込み中は子プロセスがキー入力を見張り、何かキーが押されたら中断のシグナルを送る。 | FileMode::loadStream (src/cpp/src/file_mode.cpp); Application::startPagerStdin (src/cpp/src/application.cpp) |
| `init_cut` | CUT(バッファの分割)。検索文字列strで、分割元(cut_bp)の行を検索して(fork_grep)、結果をbpへ載せる。SEGV時はエラー表示に切り替えて戻る。 | CutMode::split (src/cpp/src/cut_mode.cpp) |
| `init_cut2` | CUT(ファイルを直接分割)。検索文字列strで、バッファ・ファイルを検索して(fork_grep2)、結果を bpへ載せる。 | CutMode::split (src/cpp/src/cut_mode.cpp) |
| `init_arc` | アーカイブの1項目(pp)の内容を取り出す(read_arc、または記憶済みの領域から複写)。必要なら解凍や groff変換(fork_chp)を続けて掛け、結果をbpへ載せる。EXEC対象なら外部プログラムへ渡して終わる(F9を返す)。 | ArchiveMode::extractSelected (src/cpp/src/archive_mode.cpp); FileMode::loadAndTransformContent (src/cpp/src/file_mode.cpp) |
| `init_arc2` | アーカイブの1項目を、展開プログラム(tar/gzip/bzip2/unzip/rpm/deb等)のパイプラインを子プロセスで組んで流し込み、その出力を読むFILE*を返す。 | ChildProcess::runPipeline (src/cpp/src/child_process.cpp); ArchiveMode::extractSelected (src/cpp/src/archive_mode.cpp) |
| `init_grep` | GREP(検索)。検索文字列strで、対象(pp)を検索して(fork_grep)、結果をbpへ載せる。 | GrepMode::search (src/cpp/src/grep_mode.cpp) |
| `init_ls` | ディレクトリ一覧(T_dir: `ls -alF`を実行し、以前のsplit(マーク)を引き継ぐ)、またはlocateの結果(T_locate)を作る。パスがディレクトリでなければ、カレントディレクトリに置き換える。 | DirectoryMode::refresh; DirectoryMode::enterDirectory (src/cpp/src/directory_mode.cpp); LocateMode::search (src/cpp/src/locate_mode.cpp) |
| `init_split` | CUTで分割した1セグメントの内容(fork_split)を取り出して、bpへ載せる。文字コード判定(check_type)と改行位置の決定(set_pp)も行う。 | CutMode::segmentContent (src/cpp/src/cut_mode.cpp) |
| `init_split2` | init_splitと同じく、CUTの1セグメントの内容を、記憶済みのバッファ、または元ファイルの範囲から取り出してbpへ載せる。 | CutMode::segmentContent (src/cpp/src/cut_mode.cpp) |
| `init_split3` | CUTのセグメント(ps〜pe)を、外部プログラムへ流し込むためのパイプを作り、その読み側を返す。 | CutMode::segmentContent (src/cpp/src/cut_mode.cpp) |
| `init_split4` | init_split3と同じく、CUTのセグメントを、ファイルまたはバッファの範囲から書き出すパイプを作り、その読み側を返す。 | CutMode::segmentContent (src/cpp/src/cut_mode.cpp) |
| `init_bin` | 汎用のバッファ初期化。モード(f_M)に応じて、KEY一覧(fork_key)、DUの結果、ERROR/LOG・ファイルの読み込み(read_buff、文字コード判定check_type)、アーカイブ一覧(T_tar等)、解凍・groff変換 (T_bin/T_gz/T_roff)を、bpへ作る。 | FileMode::loadFile (src/cpp/src/file_mode.cpp); ArchiveMode::loadArchive (src/cpp/src/archive_mode.cpp); DuMode::loadUsage (src/cpp/src/du_mode.cpp); Application::startHelp (src/cpp/src/application.cpp) |

## key.c

| 旧関数 | 機能 | C++の対応先 |
|---|---|---|
| `is_esc` | 端末が送るエスケープ列(esc)が、キーの表(ks: カーソルキー等)のどれと一致するかを調べて、その番号を返す(見つからなければ0)。 | KeyInput::readEscapeSequence (src/cpp/src/key_input.cpp) |
| `set_name` | 補完の候補(sp0からの連結リスト)の、共通する先頭部分を求めて、入力済みの文字列(name)を、そこまで伸ばす。 | Application::completeFilesystemPath (src/cpp/src/application.cpp) |
| `key_getch` | キー入力を1文字返す関数(GETCHの実体の1つ)。f_keyが2/3のときは、再生する文字列(cva)を設定する (キーボードマクロの再生)。それ以外は、設定済みの文字列から1文字ずつ返し、尽きたら-1を返す。 | KeyInput::playRawMacro; KeyInput::readByte (src/cpp/src/key_input.cpp) |
| `key_getch_play` | key_getchの再生側の窓口。GETCHの本物の形(引数なし)に合わせて、引数に2でも3でもない値を渡す (以前は、引数レジスタに残った値が偶然2/3と一致するとクラッシュした)。 | KeyInput::playRawMacro (src/cpp/src/key_input.cpp) |
| `set_km` | キーボードマクロ。押されたキーを、配列(a_km)へ記録する(M_ms=記録開始、M_me=記録終了、M_mm= 再生、-1=先頭を返す)。記録が満杯になっても終了する。 | Application::startMacroRecording; Application::stopMacroRecording (src/cpp/src/application.cpp) |
| `key_ttou` | SIGTTOU(バックグラウンドから端末へ出力しようとした)のハンドラ。ハンドラを登録し直し、プロセスIDを表示し、自分(またはプロセスグループ)をSIGTSTPで停止する。 | KeyInput::installSignalHandlers (src/cpp/src/key_input.cpp) |
| `key_cont` | SIGCONT(停止からの再開)のハンドラ。ハンドラを登録し直し、プロセスIDを表示して、端末の状態を rawモードに設定し直す(SIGTTOUを確認するため)。 | KeyInput::installSignalHandlers (src/cpp/src/key_input.cpp) |
| `key_winch` | SIGWINCH(ウィンドウの大きさの変更)のハンドラ。端末の大きさを取得し直し、SHELLが動いていれば通知し、winchの旗を立てる。引数が負のときは、SHELLのプロセスIDを覚えるだけ。 | KeyInput::consumeResizeSignal (src/cpp/src/key_input.cpp) |
| `key_segv` | SIGSEGVのハンドラ。エラーを記録して、終了処理(exit_usilic)をしてから、終了する。 | なし(C++はSIGSEGVを回収しない。範囲チェックとRAIIで防ぐ設計) |
| `key_term` | SIGTERM/SIGHUPのハンドラ。起動した子プロセスをkillし、一時ファイルを消して、終了の旗(Key.f.term)を立てる。 | main (src/cpp/app/main.cpp) |
| `stand_str` | カーソル行の文字列を、反転表示で描く(rvp0あり)/通常表示に戻す(rvp0なし)。幅に収まる範囲に切り詰め、制御コードの属性も付ける。SHELL等では何もしない。 | ContentView::renderLine (src/cpp/src/content_view.cpp) |
| `kton` | キー(k)が、キーの表(key_c)の何番目にあるかを返す(無ければ0)。 | Application::resolveCommand (src/cpp/src/application.cpp) |
| `key_eeol` | 入力欄の(row, col0)から長さlen分を空白で埋めて、カーソルをその位置へ戻す(欄の消去)。 | LineEditor::redraw (src/cpp/src/line_editor.cpp) |
| `char_bytes_before` | バイト位置(bytepos)で終わる文字のバイト数を返す(UTF-8ではシーケンス長、EUCでは2、他は1)。入力欄でカーソルを1文字戻すのに使う。 | LineEditor::charBytesBefore (src/cpp/src/line_editor.cpp) |
| `b2col` | バイトオフセット(bytepos)を、画面上の桁数へ変換する(shift以降の文字の幅の合計)。UTF-8では、文字の幅(0〜2桁)を数える。 | LineEditor::redraw (src/cpp/src/line_editor.cpp) |
| `utf8_total_width` | UTF-8の文字列全体の表示幅(桁数)を返す。 | LineEditor::redraw (src/cpp/src/line_editor.cpp) |
| `utf8_right_shift` | 入力欄(len桁)に右寄せで収める場合の、読み始めるバイト位置を返す。必ず文字の先頭に着地する。 | LineEditor::redraw (src/cpp/src/line_editor.cpp) |
| `need_scroll` | 入力欄の内容が、幅(len桁)に収まらないか調べる。収まらなければ、スクロール量(*shiftp)とカーソル位置(*colp)を設定して1を返す。収まっていれば0を返す(バイト数ではなく桁数で比較する)。 | LineEditor::redraw (src/cpp/src/line_editor.cpp) |
| `input_key` | 最下行の入力欄で、文字列を入力させる(1行の編集)。キーを読み、編集(edit_line)、履歴、補完 (make_list)、確定(改行)・取消(^G)を処理して、入力結果を返す。 | LineEditor (src/cpp/src/line_editor.cpp); Application::activatePrompt (src/cpp/src/application.cpp) |
| `read_m` | キー入力を読んで、コマンド(M_*)へ変換して返す中核の関数。エスケープ列(^[...)、カーソルキー、 `^J`/`^X`/`^O`のような先頭キー、キーボードマクロ(M_ms/M_me/M_mm)、入力欄への切り替えなどを扱う。モード(f_M)ごとに、使えるキーの表(key_m)を切り替える。 | Application::dispatchByte; Application::resolveCommand; Application::dispatchLeaderByte (src/cpp/src/application.cpp) |
| `load_signal` | 全てのシグナルを、既定の動作(SIG_DFL)に戻す(子プロセスの起動時に使う)。 | ChildProcess (src/cpp/src/child_process.cpp) |
| `save_signal` | シグナルのハンドラを登録する: SIGWINCH、SIGTTOU、SIGCONT、SIGSEGV、SIGTERM/SIGHUP等。起動形態 (f_usilic)に応じて、対象が変わる。 | KeyInput::installSignalHandlers (src/cpp/src/key_input.cpp); main (src/cpp/app/main.cpp) |
| `find_name` | ディレクトリ(str)の中から、入力済みの名前(name)で始まるファイル名を集めて、補完の候補リストへ追加する。ディレクトリには`/`を付ける。f_findで、実行可能ファイルだけ/ディレクトリだけに絞る。 | Application::completeFilesystemPath (src/cpp/src/application.cpp) |
| `make_cmd` | 補完の候補(STRの連結リスト)を、一覧表示用のLISTへ移す。 | Application::completeFilesystemPath (src/cpp/src/application.cpp) |
| `esc_shell` | 補完したファイル名の中の、シェルの特殊文字(空白、`[]{}()<>\|&;'"$*?!~#\`)の前へ、バックスラッシュを付ける(SHELLのコマンドラインへ入れる場合のみ)。 | shellQuoteArg (src/cpp/src/application.cpp) |
| `make_list` | Tab補完の候補一覧を作る。入力済みの文字列(buff)から、ディレクトリと名前の部分を切り分けて、 find_nameで候補を集め、共通部分まで補完する。PROGRAM/LFILE/LDIR/SHELL等、モード(f_M)ごとに対象が違う。 | Application::completeFilesystemPath (src/cpp/src/application.cpp); Shell::onTab (src/cpp/src/shell.cpp) |
| `add_buff` | 入力欄のバッファへ、1文字(m)を挿入する。UTF-8では、複数バイトの文字を組み立ててから入れ、桁数を計算して、スクロールが必要なら位置(shift)を更新する。 | LineEditor::insertByte (src/cpp/src/line_editor.cpp) |
| `edit_line` | 入力欄の編集。カーソル移動(^F/^B/^A/^E)、削除(^D/^H/DEL/^K/^U)、貼り付け(^Y)、次の文字をそのまま入れる(^Q)等を処理する。back/yankに、消した文字を保存する。 | LineEditor (src/cpp/src/line_editor.cpp) |
| `read_esc` | エスケープ列を読み、カーソルキーや、`^[` に続くキーを、対応するコマンド番号へ変換する。 | KeyInput::readEscapeSequence (src/cpp/src/key_input.cpp) |

## menu.c

| 旧関数 | 機能 | C++の対応先 |
|---|---|---|
| `menu_clear` | SIGSEGVのハンドラ。ハンドラを登録し直し、モードをT_skip(状態を巻き戻す)にして、longjmpで menu()のループへ戻る(クラッシュしても状態を戻して続行するため)。 | なし(C++版はSIGSEGVを回収しない。範囲チェックとRAIIで防ぐ設計) |
| `clear_cut` | CUTの状態(cutp)の名前を消し、確保していたバッファを解放する。 | CutMode (src/cpp/src/cut_mode.cpp) |
| `m_copy` | COPYモードへ移る。現在の状態をスタックへ保存し、直前のモードの名前を戻り先にして、COPYの名前へ切り替える。 | Application::startCopy (src/cpp/src/application.cpp) |
| `m_num` | 数字で指定された行(N_numを引いた値)へ移動する。パイプ入力のときは、その行まで読み進める。 | Application::resolveGotoLineRow (src/cpp/src/application.cpp) |
| `m_dir` | DIR(ディレクトリ名の入力・選択)の処理。一覧の行が`/`で始まっていればそれを、無ければカレントディレクトリを、移動先の候補にする。 | DirectoryMode::enterDirectory (src/cpp/src/directory_mode.cpp); Application::startRegexPromptList (src/cpp/src/application.cpp) |
| `m_row` | カーソルを1行動かす(move_row)。表示の総行数を更新して、モードラインを描き直す。 | ContentView::moveUp; ContentView::moveDown (src/cpp/src/content_view.cpp) |
| `m_page` | ページ単位の移動をする(move_page)。総行数を更新して、モードラインを描き直す。 | ContentView::pageUp; ContentView::pageDown (src/cpp/src/content_view.cpp) |
| `m_w` | 画面の内容などを、作業ファイル(S_w)へ書き出す(M_w等)。 | なし(C++には、画面の内容をファイルへ書き出す機能が無い) |
| `m_arc` | CP/MV/RM/ARC(展開)を始める前の確認。必要なプログラム(cp/mv/rm)がインストールされているか、 ARCなら対応しているアーカイブ(tar/lha/unzip/rpm/deb)かを調べて、だめならエラーを出す。 | Application::startCopyPrompt; Application::startMovePrompt; Application::startRemovePrompt; Application::startArchivePrompt (src/cpp/src/application.cpp) |
| `m_exit` | EXITの処理。モードのスタックを遡って(load_stack)最初のモードまで戻し、終了の確認(y/n)に進む。 | Application::startExitPrompt (src/cpp/src/application.cpp) |
| `m_locate` | LOCATEの入口。locateがインストールされているか確認し、LOCATE中なら拒否する。M_locate2は、前回の結果の画面へ戻る。 | Application::startLocatePrompt (src/cpp/src/application.cpp) |
| `m_grep` | FGREP/EGREPの入口。プログラム(fgrep/egrep)の有無を確認し、履歴やCUTの一覧の上からの起動も扱う。 | Application::startGrepPrompt (src/cpp/src/application.cpp) |
| `m_cut` | CUTの入口。既にCUT中なら拒否する。パイプ入力のときは、末尾まで読み進めてから始める。 | Application::startCutPrompt (src/cpp/src/application.cpp) |
| `m_man` | MANの入口。manがインストールされているか確認し、モードのスタックを整理して、MANモードへ移る。 | Application::startManPrompt (src/cpp/src/application.cpp) |
| `m_help` | KEY(キー一覧)/HELPの入口。現在の状態をスタックへ保存し、直前のモードの名前を戻り先にして、 KEYの名前へ切り替える。 | Application::startHelp (src/cpp/src/application.cpp) |
| `m_prog` | PROGRAM(外部プログラムの実行)の入口。SHELLが対話中なら拒否する。FILE/DIRの選択(M_lfile2 等)からの起動も扱う。 | Application::startProgramPrompt (src/cpp/src/application.cpp) |
| `m_list` | LIST(履歴の一覧、^R)の入口。現在のモード(FILE/DIR/MAN/LOCATE/FGREP等)に応じて、対応する履歴(T_hfile/T_hdir/T_hman等)へ切り替える。 | Application::startPromptHistoryRecall (src/cpp/src/application.cpp) |
| `m_edit` | エディタを起動し(do_edit)、戻った後に、表示用のバッファを作り直す。 | Application::executeEdit (src/cpp/src/application.cpp) |
| `m_shell` | SHELLモードの入口と復帰の処理。SHELLの起動・再開、直前のモードへの戻り、標準入力(パイプ)の引き渡しなどを行う。 | Application::startShell (src/cpp/src/application.cpp) |
| `m_du` | DUの処理。結果を作る/再表示する(M_du2)。パイプの読み込みを止め、スタックを整理して、DUのモードへ移る。 | Application::runDuOnCurrentFolder; Application::redisplayDu (src/cpp/src/application.cpp) |
| `m_yn` | y/nによる複数の選択(M_yes/M_no)。CP/MV/RM/ARC等の対象一覧の全ての行に、印を付ける/外す。 | Application::markAll (src/cpp/src/application.cpp) |
| `m_dest` | CP/MV/ARCの宛先の決定。宛先の入力・選択を受け、対象の一覧を作って、実行へ進める。 | Application::startCopyPrompt; Application::startArchivePrompt (src/cpp/src/application.cpp) |
| `m_pipe` | ^O^M(行単位で読む)の準備。ファイルを、直接ではなく、パイプ越しに読ませる(展開が必要ならそのプログラム経由)。 | FileMode::loadFileAsPipe (src/cpp/src/file_mode.cpp); Application::openSelectedEntryNoWrap (src/cpp/src/application.cpp) |
| `m_cr` | Enter(^M、^O^M、ESC ^M等)の処理。モードごとに、選択した行を開く。ディレクトリなら降り、ファイルなら開き、アーカイブなら一覧を作り、EGREP/CUT/DU/LOCATEの結果なら、その行へ移る。 | Application::handleCommand; Application::openSelectedEntry (src/cpp/src/application.cpp) |
| `m_clear` | 画面を再描画する。表示を有効にし、中断の表示を消して、本文とモードラインを描き直す。 | Application::handleCommand (src/cpp/src/application.cpp) |
| `m_bl` | 戻る(^G)の処理。現在のモード(np->Mb)ごとに、前のモードへ戻る。入力欄の取消し、検索結果からの戻りなども、ここで扱う。 | Application::handleCommand (src/cpp/src/application.cpp) |
| `get_m` | 1つのコマンドを取得して実行する。キーを読み(read_m)、コマンドの種類ごとに、上のm_*関数へ振り分ける。 | Application::dispatchByte; Application::handleCommand (src/cpp/src/application.cpp) |
| `init_menu` | menu()の初期化。起動形態(ディレクトリ・ファイル・パイプ)に応じて、最初のモード、履歴、カーソル位置を決める。 | Application::start (src/cpp/src/application.cpp); main (src/cpp/app/main.cpp) |
| `init_bp` | モード(np->M)に応じて、表示用のバッファ(bp)を作る。文字列リスト、履歴リスト、`ls`の結果、ファイル、アーカイブ等。 | FileMode::loadFile (src/cpp/src/file_mode.cpp); DirectoryMode::enterDirectory (src/cpp/src/directory_mode.cpp); ListBrowserMode::show (src/cpp/src/list_browser_mode.cpp) |
| `menu` | メインループ。初期化(init_menu)の後、モードごとの画面(bp)を作り(init_bp)、コマンドを取得・実行 (get_m)し続ける。SIGSEGVからの回収(setjmp)も、ここで行う。base[]は、モードごとの表示領域 (0=KEY/FILE/DIR/LOG/PROGRAM/MAN、1=DIR、2=DU、3=EGREP、…)。 | Application::start; Application::run (src/cpp/src/application.cpp); main (src/cpp/app/main.cpp) |

## mode.c

| 旧関数 | 機能 | C++の対応先 |
|---|---|---|
| `echo_mess` | メッセージ番号f_messに応じて、最下行にメッセージ(`Hit any key`、`Interrupted`等)を表示する。負の値ではBEL(ベル)を鳴らさない。 | Application::drawPromptLabel (src/cpp/src/application.cpp) |
| `free_h` | 履歴等の管理領域(HEAP)を解放する(終了処理)。hp0が渡されたときは、それを覚えるだけで解放しない。 | History (src/cpp/src/history.cpp) |
| `skip_spc` | `ls -alF`の1行から、ファイル名より前の部分(権限・所有者・サイズ・日付)を読み飛ばして、名前だけにする。lsの型マーカー(`*=\|@`)や、`-> リンク先`も処理する。f_spcで動作が違う。 | DirectoryMode::parseLsLine (src/cpp/src/directory_mode.cpp) |
| `init_rv` | 画面の位置(PN)を範囲内に補正して、ページ先頭・カーソル行のポインタ(RV)を設定する。DUのときは、 DUの一覧モードへ切り替える。 | ContentView::setCursorIndex (src/cpp/src/content_view.cpp); History::positionFor (src/cpp/src/history.cpp) |
| `set_rv` | init_rvの後、記憶しておいた行(np->tell、pnp->line)へカーソルを合わせる。 | ContentView::setCursorIndex (src/cpp/src/content_view.cpp); History::positionFor (src/cpp/src/history.cpp) |
| `load_ll` | FILE/DIR/MAN/LOCATE/PROGRAM等の履歴のリスト(LL)を、モードf_Mごとに読み込み、候補一覧として表示できるよう整える。 | Application::startPromptHistoryRecall (src/cpp/src/application.cpp); History::recentEntries (src/cpp/src/history.cpp) |
| `mode` | モード遷移の中核。現在のモード(np->Mb)ごとに、選択された行を開く(Enter)処理を分岐する: EGREP/FGREPの結果、ディレクトリ(T_dirは、ディレクトリなら降りる・ファイルなら開く)、アーカイブ、 CUT、DU等。モードのスタック(hp->sp)への積み降ろしと、カレントディレクトリの更新も行う。 | Application::handleCommand; Application::openSelectedEntry (src/cpp/src/application.cpp); ModeStack (src/cpp/src/mode_stack.hpp) |
| `clear_stack` | モードのスタック(hp->sp)を空にして、スタックに積まれていたGREP・CUTの領域等を解放する。 f_clearの値で、消す範囲が変わる。 | ModeStack (src/cpp/src/mode_stack.hpp); DirectoryMode::resetNavigationToFloor (src/cpp/src/directory_mode.cpp) |
| `set_cut` | CUTの状態(cutp)を設定する。メッセージmessから検索文字列と名前を取り出し、ディレクトリ(dir)と合わせて、分割の対象を決める。 | Application::executeCut (src/cpp/src/application.cpp); CutMode::split (src/cpp/src/cut_mode.cpp) |
| `check_dir` | ディレクトリを変えるときの共通処理。スタックを空にし、カレントディレクトリと直前のディレクトリを入れ替えてchdirし、ディレクトリの履歴(dirp)へ記録する。 | DirectoryMode::enterDirectory (src/cpp/src/directory_mode.cpp) |
| `fmark` | モードf_Mで、複数選択(マーク)に使う値(行のsplitに入れる印)を返す。CP/MV/RM/ARC等が対象で、 EXITでは常に1。 | DirectoryMode::targetEntries (src/cpp/src/directory_mode.cpp) |
| `nmark` | 行のリスト(pp)の中で、マーク(fmarkの値)が付いた行の数を数える。 | DirectoryMode::targetEntries (src/cpp/src/directory_mode.cpp) |
| `send_pp` | 行のリスト(pp)の内容を、ファイルへ書き出す。fmodeが"a"なら書き出し用ファイル(S_write)へ追記、それ以外は作業ファイル(S_w)へ書く。 | なし(C++には、画面の内容をファイルへ書き出す機能が無い) |
| `do_rm` | 履歴のリスト(LL)から、選ばれた項目(ssp)を削除する(FILE/DIR等の履歴から消す操作)。 | History (src/cpp/src/history.cpp) |
| `get_hup` | usilic終了後に、子プロセスをkillするためのプロセスIDを、覚える(正の値)・忘れる(負の値)・返す(0)。 | ProgramMode::run (src/cpp/src/program_mode.cpp) |
| `mode_cont` | SIGCONT(停止からの再開)のハンドラ。ハンドラを登録し直し、プロセスIDを表示し直す。 | なし(C++はSIGCONTを、このために特別扱いしない) |
| `do_prog` | 外部プログラム(PROGRAM)を実行する。実行したコマンドを、コマンド履歴(cmdp)と履歴(histp)へ保存し、子プロセスとして起動して、f_waitなら終了を待つ。 | ProgramMode::run (src/cpp/src/program_mode.cpp); Application::executeProgram (src/cpp/src/application.cpp) |
| `save_ll` | 履歴のリスト(LL)へ、項目(ssp/full)を保存する。n_llの値(FILE/DIR/MAN等)で、どのリストへ入れるかが決まる。 | History::pushRecent (src/cpp/src/history.cpp) |
| `do_edit` | エディタを起動する(行番号付き)。ファイルの種類(f_M)に応じて、アーカイブ内のファイル等は一時ファイルへ取り出してから編集する。終了を待ち、SIGCONTも扱う。 | EditMode::edit (src/cpp/src/edit_mode.cpp) |
| `do_exec` | EXECの設定(chp)にしたがって、外部プログラム(IINA等)を、ファイル(name)を渡して起動する。引数を組み立て、子プロセスとして実行し、終了を待つ。 | ExecMode::run (src/cpp/src/exec_mode.cpp) |
| `clear_base` | BASEの内容(buff)を解放して、サイズを0にする。 | なし(C++はstd::stringなどの自動解放で足りる) |
| `free_pp` | 行(PP)の連結リストを、先頭から順に解放する(行の文字列、属性を含む)。 | なし(C++はstd::vectorの自動解放で足りる) |
| `print_arc` | アーカイブの選択行(cp)から、展開コマンドの前半を、fpへ書き出す。種類(is_chp)ごとに違う: tar系、 lha、unzip、rpm(rpm2cpio \| cpio)、deb(ar \| gzip \| tar)等。戻り値でコマンドの形を知らせる。 | ArchiveMode::extractSelected (src/cpp/src/archive_mode.cpp); ChildProcess::runPipeline (src/cpp/src/child_process.cpp) |
| `command` | CP/MV/RM/ARC(展開)のコマンドを組み立てて実行する。対象のファイルを、シェルスクリプトとして子プロセスへパイプで渡し(`cd ディレクトリ`、`cp/mv/rm ...`、`\|\|`での失敗検知)、結果を返す。 | CpMvRmMode::copyTo; CpMvRmMode::moveTo; CpMvRmMode::remove; CpMvRmMode::buildArgv (src/cpp/src/cp_mv_rm_mode.cpp) |
| `is_write` | RM/MVの対象(name)が操作できるかを調べる。rootなら常に可。ディレクトリの確認(f_dir)もできる。シンボリックリンクは可、権限が無ければ-EACCES、存在しなければ-ENOENTを返す。 | CpMvRmMode::isWritable (src/cpp/src/cp_mv_rm_mode.cpp) |
| `set_cutchp` | CUT一覧の行(arg)の`(プログラム名)`から、対応する外部プログラム(CHILD)を引き当てる。 | Configuration::programForSuffix (src/cpp/src/configuration.cpp) |
| `print_intr` | 中断(SIGINT)やエラーの記録を、最下行に表示する。savef8のときは、Exitの表示を消す。エラーの記録(save_error)の内容も出す。 | Application::drawPromptLabel (src/cpp/src/application.cpp) |
| `ins_q` | 文字列中の制御文字の前へ、引用符(`^Q`=K_q)を差し込む(入力欄に制御文字をそのまま入れるため)。 | LineEditor (src/cpp/src/line_editor.cpp) |

## scroll.c

| 旧関数 | 機能 | C++の対応先 |
|---|---|---|
| `rv_str` | カーソル行(rvp->pp)の文字列のうち、名前の開始桁(rvp->col)以降だけを rvp->strへ取り出す(選択中のエントリ名の取得)。SHELLの仮想行では何もしない。 | DirectoryMode::selectedEntry (src/cpp/src/directory_mode.cpp) |
| `set_top` | ページ先頭行(pp)が、行のリストの先頭から数えて何行目か(0始まり)を返す。 | ContentView::topIndex (src/cpp/src/content_view.cpp) |
| `move_page` | ページ単位の移動。M_back/M_kback/M_headは前のページ・先頭へ、M_forw/M_kforw/M_tailは次のページ・末尾へ移す。パイプ入力は必要なだけ追加で読み込み(alloc_pipe)、移動後に画面を更新する(端末にステータス行(hs、vt_mode 'H')が有れば端末のスクロールで 1行ずつ、無ければ全体を再描画)。位置情報(pnp)を更新し、端末の入力バッファを捨てる (tcflush)。 | ContentView::pageUp; ContentView::pageDown; ContentView::goToTop; ContentView::goToBottom (src/cpp/src/content_view.cpp) |
| `move_row` | 行単位の移動。M_up/M_kupは1行上、M_dn/M_kdnは1行下へカーソルを動かす。ページの端では画面をスクロールする(パイプ入力は必要に応じて追加で読み込む)。M_prev/M_next系は、一度ページの端へ寄せてから同じ動作をする。 | ContentView::moveUp; ContentView::moveDown; ContentView::scrollLineUp; ContentView::scrollLineDown (src/cpp/src/content_view.cpp) |
| `sr_row` | 画面を1行分下へずらして(端末のsrコマンド)、空いた先頭行にppを描く。ウィンドウ表示が無効(vt_mode 'D'が偽)のときは何もしない。 | TerminalDisplay::scrollReverse (src/cpp/src/terminal_display.cpp) |
| `sf_row` | 画面を1行分上へずらして(端末のsfコマンド)、空いた最下行にppを描く。ウィンドウ表示が無効(vt_mode 'D'が偽)のときは何もしない。端末にステータス行(hs、vt_mode 'H')が有るかどうかで、カーソル位置と描く順序が変わる。 | TerminalDisplay::scrollForward (src/cpp/src/terminal_display.cpp) |
| `init_col` | モード(f_M)ごとに、カーソルが指す名前の開始桁の初期値を返す。lsの出力を持つモード(T_dir、アーカイブ各種)はget_col()で求め、一覧系は0、CP/MV/RM等は-1(桁の概念なし)。 | DirectoryEntry::nameStartColumn (src/cpp/src/directory_mode.hpp); ContentView::Line (src/cpp/src/content_view.hpp) |
| `init_row` | 記憶していた位置(tell=行番号、1始まり)から、ページ先頭のPP(*ppp)とカーソル行(pnp->row)を復元する。総行数が1画面に収まらないときは、記憶した行が画面内に入るようtopを決める。 | History::positionFor (src/cpp/src/history.cpp); ContentView::setCursorIndex (src/cpp/src/content_view.cpp) |
| `get_col` | `ls -alF`の1行から、ファイル名が始まる桁(0始まり)を返す。月の略称(Jan〜Dec)を目印に、その13バイト後ろをファイル名の開始とする(ファイル名に空白があっても壊れない)。目印が無い行(アーカイブ一覧等)だけ、旧来の「最後の空白」で代用する。 | DirectoryMode::parseLsLine (src/cpp/src/directory_mode.cpp) |

## shell.c

| 旧関数 | 機能 | C++の対応先 |
|---|---|---|
| `write_shell` | SHELLの表示側プロセスのSIGALRMハンドラ。Shell.f.initで用途が変わる(0=通知の処理、1=パイプの設定、 2=状態変数の公開、3=停止)。通知では、シェルのプロンプト行から現在のディレクトリを取り出してchdirし、カーソルの位置(行・桁)を受け取り、入力できる桁数を親へ返す。 | Shell::handlePromptDetected; Shell::onPtyReadable (src/cpp/src/shell.cpp) |
| `input_shell` | SHELLモードの入力側のメイン処理。キー入力を読み、行編集(read_line)、画面のスクロール (shell_row/shell_page)、履歴の呼び出しや補完、他のモードへの移行などを処理して、必要なコマンドを表示側へ送る(write_comm)。 | Application::feedShellByte (src/cpp/src/application.cpp); Shell::submitLine (src/cpp/src/shell.cpp); LineEditor (src/cpp/src/line_editor.cpp) |
| `write_comm` | 表示側と入力側の間の通信。コマンドm(と、あれば文字列comm)をパイプ(pfd)へ書き、SIGALRMで相手プロセス(spid)を起こす。commがNULLのときは、mを負にして「文字列なし」を伝える。 | なし(C++は単一プロセスなので、プロセス間の通信を持たない) |
| `read_comm` | write_commで送られたコマンドを、パイプ(pfd)から読む。正の値なら文字列commが続き、負の値は「文字列なし」の印(絶対値を返す)。 | なし(C++は単一プロセスなので、プロセス間の通信を持たない) |
| `hide_ps` | 行のリストの末尾にある仮想行(プロンプト行)を、リストから一時的に外して返す(画面のスクロール等の間、プロンプトを含めないため)。 | Shell::redrawPromptRow (src/cpp/src/shell.cpp) |
| `disp_shell` | SHELL画面を、モードライン付きで描く。プロンプト行があればコマンドラインも描き、表示が無効なら有効に戻す。 | Shell::redrawPromptRow (src/cpp/src/shell.cpp); ContentView::renderModeLine (src/cpp/src/content_view.cpp) |
| `read_string` | バッファcpから改行までを文字列strへ複写して、進めた位置を返す(通信で受け取った文字列の分解)。 | なし(C++は標準ライブラリで足りる。プロセス間通信が無い) |
| `shell_row` | SHELL画面で、カーソルを1行動かす(move_rowを呼ぶ)。仮想行(プロンプト)を一時的に外して、動かした後に戻す。 | ContentView::moveUp; ContentView::moveDown (src/cpp/src/content_view.cpp) |
| `shell_page` | SHELL画面で、ページ単位の移動をする(move_pageを呼ぶ)。プロンプト行を一時的に外して、動かした後に戻す。 | ContentView::pageUp; ContentView::pageDown (src/cpp/src/content_view.cpp) |
| `next_line` | SHELL画面へ新しい行を1行追加して(alloc_sh)、カーソル位置を進める。画面がいっぱいなら、先頭を1行送ってスクロールする。 | Shell::appendBodyByte; Shell::flushPendingLine (src/cpp/src/shell.cpp) |
| `read_line` | SHELLのコマンドライン入力を、1バイト受け取って処理する。文字の挿入・削除、UTF-8の組み立て、 ESC列や属性の解釈、改行を扱い、改行なら行が確定したことを返す。 | LineEditor (src/cpp/src/line_editor.cpp); Shell::submitLine (src/cpp/src/shell.cpp) |
| `print_prompt` | SHELLのプロンプトと、入力途中の文字列を再描画する。 | Shell::redrawPromptRow (src/cpp/src/shell.cpp) |
| `set_pos` | 画面の現在位置(先頭・末尾・途中)と前回の位置(pos)から、次のスクロール指令(M_head/M_tail/T_not/ M_cmd)を決める。 | Application::feedShellByte (src/cpp/src/application.cpp) |
| `shell_hup` | SIGALRM等を受けたとき、SHELLの終了処理(output_shellをf_shell=-1で呼ぶ)を行う。 | Shell::terminate (src/cpp/src/shell.cpp) |
| `shell_pipe` | SIGPIPEを受けても終了しないよう、ハンドラを登録し直す。 | なし(C++は、SIGPIPEを無視する設定で足りる) |
| `print_shell` | ウィンドウのリサイズ後に、SHELLの行を新しい幅で作り直す(alloc_winchを使う)。 | Shell::onResize (src/cpp/src/shell.cpp) |
| `read_shell` | SHELLの入力側プロセスのSIGALRMハンドラ。表示側から届いたコマンド(T_in/T_out/T_off/T_disp/M_mark/ M_jump/行番号の指定など)をread_commで読み、対話中かの応答、表示の切り替え・再描画、マーク、指定した行へのジャンプを行う。 | Application::feedShellByte (src/cpp/src/application.cpp); Shell::onResize (src/cpp/src/shell.cpp) |
| `add_line` | read_lineの結果が改行(またはエラー)なら、新しい行を追加する(next_line)。 | Shell::appendBodyByte (src/cpp/src/shell.cpp) |
| `output_shell` | SHELLの出力側のメインループ。擬似端末から読み込み、プロンプトを検出し、ESC列を解釈して、画面に行を積み、エラー出力を別に扱う。f_shellの値で、開始・通常・終了(-1)を切り替える。 | Shell::onPtyReadable; Shell::processOutputBytes (src/cpp/src/shell.cpp) |
| `from_main` | 別のusilicプロセス(SHELLの中で起動された、引数付きの起動やパイプ入力)から、ソケットまたはファイルで渡された内容(ファイル・ディレクトリ・DU・パイプ)を受け取って開く。 | Application::pollShellHandoff (src/cpp/src/application.cpp); tryHandoffToParentShell (src/cpp/app/main.cpp) |
| `wait_comm` | 表示側からの返事(T_in/T_out等)を、パイプ(pfd)で待って読む。返事に応じて表示を有効にしたり、位置(pos)を更新したりする。 | なし(C++は単一プロセスなので、プロセス間の通信を持たない) |
| `make_shell` | SHELL用のパイプ4組(8本)を作り、子プロセスをforkして、表示側と入力側を組み立てる。SIGALRMのハンドラも設定する。 | Shell::start (src/cpp/src/shell.cpp) |
| `start_shell` | SHELLの起動・状態取得・終了を行う。擬似端末を開き(open_pty)、シェルをforkして起動する。 shell_pidが負ならシェルのPIDを返し、disp_pidを渡せば表示側のPIDを登録する。 | Shell::start; Shell::alive; Shell::childPid; Shell::terminate (src/cpp/src/shell.cpp) |
| `do_shell` | SHELLモードの入口と全体の制御。初回に表示用の長さを計算し、シェルが生きていなければ起動して、 SHELLの入出力の処理へ入る。 | Application::ensureShellActive (src/cpp/src/application.cpp); Shell::start (src/cpp/src/shell.cpp) |
| `is_inter` | SHELLが対話中(入力待ち)かどうかを、表示側へ問い合わせて判定する。F12を渡すと、シェルへSIGHUPを送って、終了するまで待つ。 | Shell::foregroundJobRunning; Shell::busy (src/cpp/src/shell.cpp) |

## str.c

| 旧関数 | 機能 | C++の対応先 |
|---|---|---|
| `r_S` | 文字列の表(Str.S)の、番号n_Sの1件を解放する。 | なし(C++はstd::stringの自動解放で足りる) |
| `s_S` | 文字列の表(Str.S)から、番号n_Sの文字列(メッセージ、パス、コマンド等)を返す。 | Configuration::commandFor (src/cpp/src/configuration.cpp) |
| `s_P` | 組み込みの文字列の配列(外部プログラムの定義child[]、モード名、ラベル等)を、番号で返す。 | Configuration::Configuration (src/cpp/src/configuration.cpp) |
| `get_prog` | コマンド行の文字列から、最初の単語(空白まで)を取り出す。 | Configuration::loadRcText (src/cpp/src/configuration.cpp) |
| `print_usilic` | INFO(バージョンと設定の要約)の画面を表示する。C++版のINFO画面のフィールド構成に合わせてある。 | Application::executeShowUsilicBanner (src/cpp/src/application.cpp) |
| `get_cp` | 設定の行から、`#`と改行以降を取り除き、先頭の空白を飛ばした位置を返す(空ならNULL)。 | Configuration::loadRcText (src/cpp/src/configuration.cpp) |
| `get_val` | 設定の値の行を、コメント・空行を飛ばして読む。 | Configuration::loadRcText (src/cpp/src/configuration.cpp) |
| `conf_arc` | usilicrcのARCHIVEの設定(`.tar tar tvf`のような、suffixと展開コマンド)を読み込む。 | Configuration::loadRcText (src/cpp/src/configuration.cpp) |
| `conf_bin` | 外部プログラムの設定(EXEC、EDITOR、SHELL、PROGRAM等。種類はf_child)を読み込む。 | Configuration::loadRcText (src/cpp/src/configuration.cpp) |
| `conf_grep` | usilicrcのGREPの設定(検索文字列の候補)を読み込む。 | Configuration::loadRcText (src/cpp/src/configuration.cpp) |
| `conf_prog` | usilicrcのPROGRAMの設定(外部プログラムの候補)を読み込む。 | Configuration::loadRcText (src/cpp/src/configuration.cpp) |
| `conf_bsize` | usilicrcのBUFFERの設定(読み込みバッファの大きさ、1〜100)を読み込む。範囲外ならエラー。 | Configuration::loadRcText (src/cpp/src/configuration.cpp) |
| `conf_code` | usilicrcのCODEの設定(バイナリ判定で許可する制御文字)を読み込む。 | Configuration::loadRcText (src/cpp/src/configuration.cpp) |
| `conf_history` | usilicrcのHISTORYの設定(履歴のファイル)を読み込む。 | Configuration::loadRcText (src/cpp/src/configuration.cpp) |
| `conf_wait` | usilicrcのWAITの設定(表示の待ち時間、既定100000マイクロ秒)を読み込む/返す。 | Configuration::loadRcText (src/cpp/src/configuration.cpp) |
| `conf_scroll` | usilicrcのSCROLLの設定を読み込む/返す。 | Configuration::loadRcText (src/cpp/src/configuration.cpp) |
| `env_charset` | 環境変数(<プログラム名>CHARSET)が、明示的に設定されたかを返す。明示された場合は、前回のセッションで保存された文字コードより優先する。 | main (src/cpp/app/main.cpp); Configuration::setEnvironmentCharset (src/cpp/src/configuration.cpp) |
| `conf_ls` | usilicrcのLSの設定(lsのオプション)を読み込む/返す。 | Configuration::loadRcText (src/cpp/src/configuration.cpp) |
| `get_str` | usilicrcを1行(最大N_char)読む。`P-?`の行の扱いを含む。 | Configuration::loadRcText (src/cpp/src/configuration.cpp) |
| `key_err` | usilicrcのKEYの設定の書式エラーを記録して、終了する。 | なし(C++は、usilicrcのKEY節(キーの割り当ての変更)を解釈しない。キーは固定で、KEYモード自体も2026-09-20に廃止) |
| `save_ov` | キーの番号から、表示用の名前(`^A`、`DEL`等)の文字列を作る。 | なし(C++は、usilicrcのKEY節(キーの割り当ての変更)を解釈しない。キーは固定で、KEYモード自体も2026-09-20に廃止) |
| `set_m` | KEYの設定の中の、数字(番号)を読み取る。 | なし(C++は、usilicrcのKEY節(キーの割り当ての変更)を解釈しない。キーは固定で、KEYモード自体も2026-09-20に廃止) |
| `set_s` | KEYの設定の中の`"..."`の文字列を読み取り、`\cX`等の記法を制御文字へ変換する。 | なし(C++は、usilicrcのKEY節(キーの割り当ての変更)を解釈しない。キーは固定で、KEYモード自体も2026-09-20に廃止) |
| `conf` | 設定全体の初期化。組み込みの既定値(`ls -alFN`、`xvf`等)、ARCHIVEの表、エラーメッセージの表を文字列の表(Str.S)へ設定し、作業ディレクトリ、HOME、LANG、usilicrcの場所を決める。 | Configuration::Configuration (src/cpp/src/configuration.cpp); main (src/cpp/app/main.cpp) |
| `free_S` | 文字列の表(Str.S)の全体を解放する。 | なし(C++はstd::stringの自動解放で足りる) |
| `print_key` | KEYモードの一覧(キーとコマンドの対応)を出力する。 | なし(C++は、usilicrcのKEY節(キーの割り当ての変更)を解釈しない。キーは固定で、KEYモード自体も2026-09-20に廃止) |
| `set_key` | キーの表(key_m、key_c)を初期化する。 | なし(C++は、usilicrcのKEY節(キーの割り当ての変更)を解釈しない。キーは固定で、KEYモード自体も2026-09-20に廃止) |
| `read_mark` | usilicrcのKEYの節の中の見出し(`M-?`、`F-?`、`K-?`)まで読み進める。無ければ書式エラー。 | なし(C++は、usilicrcのKEY節(キーの割り当ての変更)を解釈しない。キーは固定で、KEYモード自体も2026-09-20に廃止) |
| `conf_m` | usilicrcのKEYの節の`M-?`(キーボードマクロの定義)を読み込む。 | なし(C++は、usilicrcのKEY節(キーの割り当ての変更)を解釈しない。キーは固定で、KEYモード自体も2026-09-20に廃止) |
| `conf_f` | usilicrcのKEYの節の`F-?`(ファンクションキー等の割り当て)を読み込む。 | なし(C++は、usilicrcのKEY節(キーの割り当ての変更)を解釈しない。キーは固定で、KEYモード自体も2026-09-20に廃止) |
| `conf_c` | usilicrcのKEYの節の`K-?`(キーとコマンドの割り当て)を読み込む。 | なし(C++は、usilicrcのKEY節(キーの割り当ての変更)を解釈しない。キーは固定で、KEYモード自体も2026-09-20に廃止) |
| `conf_key` | usilicrcのKEYの節(M-?、F-?、K-?)の全体を読み込む。一覧の出力(print_key)にも使う。 | なし(C++は、usilicrcのKEY節(キーの割り当ての変更)を解釈しない。キーは固定で、KEYモード自体も2026-09-20に廃止) |
| `init_config` | usilicrcの全体(ARCHIVE、EXEC、GREP、PROGRAM、EDITOR、BUFFER、CODE、SHELL、HISTORY、WAIT、 SCROLL、LS、CP/MV/RM、KEY)を読み込んで、各設定へ反映する。 | Configuration::loadRcText (src/cpp/src/configuration.cpp) |
| `set_usilicrc` | usilicrcの場所を決めて返す。`~/.<プログラム名>/<プログラム名>rc`(ホーム側)があればそれを使う。ただし、LIB側の同名ファイルの方が新しい(ctime)と判定されたときは、*errepへ1を入れてLIB側を返す。ホーム側が無ければLIB側を返し、そのLIB側も無ければ*errepへ-1を入れる。 | main (src/cpp/app/main.cpp); Configuration::loadRcText (src/cpp/src/configuration.cpp) |

## sub.c

| 旧関数 | 機能 | C++の対応先 |
|---|---|---|
| `sub_clear` | SIGSEGVのハンドラ。中断の旗(Sub.abort)を立てて、longjmpで呼び出し元へ戻る(履歴ファイルの読み込み中のクラッシュ回収)。 | なし(C++版はSIGSEGVを回収しない) |
| `sspc` | sscanfで読めるよう、文字列中の制御文字と空白を、印(`K_o`+文字、`K_a`)へ変換する(履歴ファイル用)。 | History::serialize (src/cpp/src/history.cpp) |
| `rspc` | sspcの逆。印(`K_o`+文字、`K_a`)を、元の制御文字と空白に戻す(履歴ファイルの読み込み用)。 | History::loadHistoryText (src/cpp/src/history.cpp) |
| `print_list` | LIST(二分木)を、左・自分・右の順(=名前の順)に、1行ずつファイルへ書き出す。 | History::serialize (src/cpp/src/history.cpp) |
| `read_str` | 履歴ファイルから、モードmの履歴(STR)を読み込む。見出し行(`K_l`+モード名)から次の見出しまで。 | History::loadHistoryText (src/cpp/src/history.cpp) |
| `write_str` | 履歴(STR)を、順序を保ったまま履歴ファイル(~/.<プログラム名>/history)へ書き出す。 | History::serialize (src/cpp/src/history.cpp) |
| `delete_str` | 履歴(STR)から、名前が一致する項目を削除する(名前をNULLにする)。 | History (src/cpp/src/history.cpp) |
| `get_par` | ディレクトリ名(dir)を、親ディレクトリの名前に置き換える(`/`ならそのまま)。 | DirectoryMode::goToParent (src/cpp/src/directory_mode.cpp) |
| `get_time` | 現在の時刻を、asctimeの形式の文字列にして返す(履歴ファイルの先頭用)。 | currentTimestampText (src/cpp/app/main.cpp) |
| `find_prog` | PROGRAMの候補リスト(prog)の中に、buffのコマンド名と一致するものがあるかを、二分木の順に探す。 | Shell::onTab (src/cpp/src/shell.cpp) |
| `load_grep` | GREPの状態を積んだ連結リストから、先頭の1つを取り除いて(解放)、その1つ前を返す。 | ModeStack (src/cpp/src/mode_stack.cpp) |
| `save_grep` | GREPの状態を、連結リストへ新しく1つ積んで返す。 | ModeStack (src/cpp/src/mode_stack.cpp) |
| `find_grep` | GREPの状態の連結リストの中に、表示領域(bp)が対応するものがあるかを探す。 | ModeStack (src/cpp/src/mode_stack.cpp) |
| `delete_name` | ファイル(fp)に書かれた名前を1行ずつ読み、それらを履歴(STR)から削除して、LISTを返す。 | History (src/cpp/src/history.cpp) |
| `save_hist` | 履歴(STR)の末尾へ、名前と位置(PN)を追加する(再帰で末尾まで辿る)。 | History::pushRecent (src/cpp/src/history.cpp) |
| `find_hist` | 履歴(STR)の末尾の項目が、名前と一致するかを調べ、個数をcountpへ返す。 | History::pushRecent (src/cpp/src/history.cpp) |
| `save_history` | 履歴(STR)へ、名前を追加する。直前と同じなら追加しない。保存する個数の上限も管理する。 | History::pushRecent; History::retentionCount (src/cpp/src/history.cpp) |
| `free_history` | 履歴(STR)のうち、保存する個数(n_hist)を超えた古いものを、先頭から取り除く。 | History::pushRecent (src/cpp/src/history.cpp) |
| `find_list` | LIST(二分木)の中に、名前があるかを探す(見つかれば*ipを1にする)。 | History::containsName (src/cpp/src/history.cpp) |
| `save_str` | STR(連結リスト)へ、名前と位置(PN)を追加する。既にある名前は追加しない。 | History::pushRecent (src/cpp/src/history.cpp) |
| `read_stack` | モードのスタックを辿って、表示領域(f_read=0)または名前(f_read=1)が一致するものを探す。 | ModeStack (src/cpp/src/mode_stack.cpp) |
| `load_stack` | モードのスタックの先頭を降ろして(戻る)、その時の状態(LINE、モード、名前、表示領域)を復元する。 | ModeStack::pop (src/cpp/src/mode_stack.cpp) |
| `save_stack` | 現在のモードの状態(直前のモード、名前、ディレクトリ、表示領域等)を、モードのスタックへ積む。 | ModeStack::push (src/cpp/src/mode_stack.cpp) |
| `save_stack_dir` | カレントディレクトリを、モードのスタックの底として積む。 | ModeStack::pushFloor (src/cpp/src/mode_stack.cpp) |
| `get_name` | パス名(pathname)を、最後の`/`より後(ファイル名だけ)に置き換える。 | PathResolver (src/cpp/src/path_resolver.cpp) |
| `get_dir` | パス名(pathname)を、最後の`/`まで(ディレクトリ部分だけ)にする。`/`が無い/ルートなら0を返す。 | PathResolver (src/cpp/src/path_resolver.cpp) |
| `erase_dot` | パス名から、`./`(カレント)と`../`(親)を整理して取り除く。 | PathResolver::resolve (src/cpp/src/path_resolver.cpp) |
| `read_list` | 履歴ファイル(~/.<プログラム名>/history)を読み込み、FILE/DIR/MAN/LOCATE/GREP/PROGRAM等の履歴(LL/SS)と、位置の記憶を復元する。 | History::loadHistoryText (src/cpp/src/history.cpp) |
| `write_list` | 履歴(LL/SS)を、履歴ファイルへ書き出す。書く前に、古いファイルを`.old`へ退避する。 | History::serialize (src/cpp/src/history.cpp) |
| `free_list` | LIST(二分木)の全体を解放する。 | なし(C++はデストラクタとstd::vectorの自動解放で足りる) |
| `free_str` | STR(連結リスト)の全体を解放する。 | なし(C++はデストラクタとstd::vectorの自動解放で足りる) |
| `init_str` | 履歴(STR)から、モード(f_M)に応じた表示用のバッファ(bp)を作る(履歴の一覧画面)。 | ListBrowserMode::show (src/cpp/src/list_browser_mode.cpp) |
| `init_list` | LIST(二分木)から、モード(f_M)に応じた表示用のバッファ(bp)を作る(候補・履歴の一覧画面)。 | ListBrowserMode::show (src/cpp/src/list_browser_mode.cpp) |
| `load_list` | LIST(二分木)から名前を探して、記憶していた位置(PN)を返す(見つかれば1)。 | History::positionFor (src/cpp/src/history.cpp) |
| `save_list` | LIST(二分木)へ、名前と位置(PN)を、名前の順(ソート順)に追加する。 | History::setPosition; History::rememberName (src/cpp/src/history.cpp) |
| `utoa` | unsigned intを、10進の文字列へ変換して返す。 | なし(C++はstd::to_stringで足りる) |
| `tobo` | 文字列を、太字の重ね打ち(`X^HX`)へ変換する。 | なし(C++は重ね打ちにせず、AttributeRunで太字を持つ) |
| `toul` | 文字列を、下線の重ね打ち(`_^HX`)へ変換する。 | なし(C++は重ね打ちにせず、AttributeRunで下線を持つ) |
| `tome` | 文字列から、重ね打ち(`X^H..`)の部分を取り除き、改行までを複写して返す。 | なし(C++は重ね打ちにせず、AttributeRunで属性を持つ) |
| `ichrstr` | 文字列(str0)の中で、文字cまたは文字列strが最初に現れる位置(1始まり)を返す(無ければ0)。 | なし(C++はstd::string::findで足りる) |
| `print_arg` | モードラインの引数欄(バイト数と`(プログラム名)`)を組み立てる。 | ContentView::ModeLineFields (src/cpp/src/content_view.cpp) |
| `dup_error` | 子プロセスの標準エラー出力を、エラーファイル(S_error)へ付け替える。 | ChildProcess (src/cpp/src/child_process.cpp) |
| `save_error` | 書式付きのエラーメッセージを、エラーファイル(S_error)へ追記する(ERROR画面で見られる)。 | ChildProcess (src/cpp/src/child_process.cpp) |
| `save_stderr` | 端末を元に戻した後に、標準エラーへ出す内容を、ファイル(S_stderr)へ保存しておく。 | main (src/cpp/app/main.cpp) |
| `save_intr` | 中断・エラーの番号を、ファイル(S_intr)へ書く(別のプロセスへ知らせるため)。 | なし(C++は単一のプロセスなので、状態を変数で持つ) |
| `load_intr` | save_intrが書いた、中断・エラーの番号を読む(無ければ0)。 | なし(C++は単一のプロセスなので、状態を変数で持つ) |
| `is_inst` | PATH(コロン区切り)の中に、プログラム(prog)があるかを探す(インストールの確認)。 | なし(C++は、実行の失敗で不在を知る。PATHの探索はTab補完(Shell::onTab)だけが行う) |
| `is_access` | ファイル名(name)にアクセスできるかを調べ、できなければエラーメッセージ用のバッファ(bp)を作って-1を返す。 | DirectoryMode::reportError (src/cpp/src/directory_mode.cpp) |
| `is_str` | モード(f_M)に対応する履歴(STR)の番号と、表示領域の番号(*n_bp)を返す(履歴の一覧画面の種類)。 | RingCategory (src/cpp/src/history.hpp) |
| `is_list` | モード(f_M)に対応する候補・履歴(LIST)の番号と、表示領域の番号(*n_bp)を返す。 | RingCategory (src/cpp/src/history.hpp) |
| `pty_strerror` | 擬似端末を開けなかった理由の文字列を返す(記録が無ければ`unknown reason`)。 | Shell::start (src/cpp/src/shell.cpp) |
| `open_pty` | 擬似端末を開く。POSIX(Unix98)のAPIで、スレーブのパス(Linuxの`/dev/pts/N`、macOSの`/dev/ttysNNN`)を strへ返す。f_openで、開くか、名前だけかを切り替える。 | Shell::start (src/cpp/src/shell.cpp) |
| `mtoa` | 整数(モード番号)のバイト順を入れ替えて、文字列にする(モードのスタックの名前の照合用)。 | なし(C++はモードを列挙型で持つ) |
| `print_buff` | バッファを、制御文字を反転表示にしながら描く(入力欄など)。 | ContentView::renderLine (src/cpp/src/content_view.cpp) |
| `print_ctrl` | 文字列の制御文字を、接頭文字ccと`X@`の形(`^A`など)に直す。 | ContentView::renderLine (src/cpp/src/content_view.cpp) |
| `unlink_file` | 作業ファイル(w、mess、enter、intr、pid、error等)を削除する(終了処理)。 | main (src/cpp/app/main.cpp) |
| `print_pid` | プロセスIDを、pidファイル(S_pid)へ書く。 | main (src/cpp/app/main.cpp) |
| `strint` | 整数の表(tab、0で終わり)の中にuがあれば、その値を返す(無ければ0)。 | なし(C++は標準ライブラリで足りる) |
| `is_comm` | モード(f_M)が、CP/MV/RM/ARC等の、複数選択して実行するモード(f_comm=0)か、その入力・確認の段階 (f_comm=1)かを判定して、対応するモード番号を返す。 | Application::startCopyPrompt; Application::startMovePrompt; Application::startRemovePrompt; Application::startArchivePrompt (src/cpp/src/application.cpp) |
| `check_chp` | 設定に、外部プログラム(s_i)が登録されているかを確認し、なければ`Not installed`のエラーを出してNULLを返す。 | Configuration::commandFor (src/cpp/src/configuration.cpp) |
| `print_stderr` | 保存しておいた内容(save_stderr)を、標準エラーへ出す(端末を戻した後)。err0を渡すと、内容を設定する。 | main (src/cpp/app/main.cpp) |
| `is_ls` | モード(f_M)が、ls形式の一覧(DIR、DU、FILE/DIRの選択、履歴等)かどうかを返す。 | DirectoryEntry (src/cpp/src/directory_mode.hpp) |
| `set_help` | HELPファイルの名前(文字コードに応じて、英語版・日本語版)を選んで覚える。cpがF13なら解放する。 | Application::startHelp (src/cpp/src/application.cpp); expandHelpMarkup (src/cpp/src/help_text.cpp) |

## tc.c

| 旧関数 | 機能 | C++の対応先 |
|---|---|---|
| `outc` | tputsに渡す1文字出力の関数。ウィンドウ表示が有効(vt_mode 'D')のときだけputcharする。 | TerminalDisplay::putChar (src/cpp/src/terminal_display.cpp) |
| `tc_putchar` | 1文字を端末へ出す。BEL(^G)だけは、表示が無効でも鳴らす。 | TerminalDisplay::putChar (src/cpp/src/terminal_display.cpp) |
| `tc_flush` | 標準出力をflushする(表示が有効のとき)。 | TerminalDisplay::flush (src/cpp/src/terminal_display.cpp) |
| `tc_endstand` | 反転表示(standout)を終える(terminfoのse)。 | TerminalDisplay::setAttribute (src/cpp/src/terminal_display.cpp) |
| `tc_stand` | 反転表示(standout)を始める(terminfoのso)。 | TerminalDisplay::setAttribute (src/cpp/src/terminal_display.cpp) |
| `tc_move` | カーソルを(row, col)へ移動する(terminfoのcm)。 | TerminalDisplay::moveCursor (src/cpp/src/terminal_display.cpp) |
| `tc_row` | 端末の行数を記憶する/返す(iが0でなければ設定)。 | TerminalDisplay::rows (src/cpp/src/terminal_display.cpp) |
| `tc_col` | 端末の桁数を記憶する/返す(iが0でなければ設定)。 | TerminalDisplay::columns (src/cpp/src/terminal_display.cpp) |
| `tc_clear` | 画面を消去する(terminfoのcl)。 | TerminalDisplay::clearScreen (src/cpp/src/terminal_display.cpp) |
| `tc_eeol` | カーソルから行末までを消去する(terminfoのce)。 | TerminalDisplay::clearToEndOfLine (src/cpp/src/terminal_display.cpp) |
| `tc_left` | カーソルを1桁左へ動かす(terminfoのle)。 | TerminalDisplay::cursorLeft (src/cpp/src/terminal_display.cpp) |
| `tc_sf` | 端末のスクロール領域を1行、上へずらす(terminfoのsf)。 | TerminalDisplay::scrollForward (src/cpp/src/terminal_display.cpp) |
| `tc_sr` | 端末のスクロール領域を1行、下へずらす(terminfoのsr)。 | TerminalDisplay::scrollReverse (src/cpp/src/terminal_display.cpp) |
| `tc_rc` | 保存しておいたカーソル位置へ戻す(terminfoのrc)。 | TerminalDisplay::restoreCursor (src/cpp/src/terminal_display.cpp) |
| `tc_sc` | カーソル位置を保存する(terminfoのsc)。 | TerminalDisplay::saveCursor (src/cpp/src/terminal_display.cpp) |
| `tc_cs` | スクロール領域(m行〜n行)を設定する(terminfoのcs)。 | TerminalDisplay::applyScrollRegion (src/cpp/src/terminal_display.cpp) |
| `tc_seta` | 文字の属性(太字・下線・反転など)をterminfoの対応するコマンドで設定する。下線が無い端末では反転で代用する。 | TerminalDisplay::setAttribute (src/cpp/src/terminal_display.cpp) |
| `tc_geta` | 属性に対応する端末の制御文字列(エスケープ)を返す(下線が無ければ反転のもの)。 | TerminalDisplay::attributeEscape (src/cpp/src/terminal_display.cpp) |
| `tc_ks` | カーソルキー(上下左右、PageUp/Down)が端末から送る文字列の表を返す。 | TerminalDisplay::keySequence (src/cpp/src/terminal_display.cpp) |
| `tc_mode` | 端末の状態フラグの取得・設定。'D'=ウィンドウを表示する、'W'=SHELLでのSIGWINCH、 'H'=ステータス行(hs)を持つ。小文字で設定、負の値(-'d'等)で解除。0のときは初期化(ti)を出す。 | TerminalDisplay::setOutputEnabled; TerminalDisplay::setModeLineVisible (src/cpp/src/terminal_display.cpp) |
| `tc_cursor` | カーソルの表示・非表示に合わせて、ウィンドウサイズ変更のキー処理(key_winch)を切り替える。 | TerminalDisplay::hideVisibleCursor; TerminalDisplay::setVisibleCursor (src/cpp/src/terminal_display.cpp) |
| `tc_load` | terminfoを読み込み(setupterm)、端末の各機能(cm、cl、ce、so、se、sf、sr、cs、キー等)を取り出して、端末操作の関数表(VT)を組み立てて返す。TERMの上書きにも対応する。 | TerminalDisplay::fromEnvironment (src/cpp/src/terminal_display.cpp) |

## usilic.c

| 旧関数 | 機能 | C++の対応先 |
|---|---|---|
| `usage` | 使い方(プログラム名のみ、`[directory]`付き、`[file] [< file]`付き)とバージョン行を標準エラーへ出す。 | main (src/cpp/app/main.cpp) |
| `main_pipe` | SHELLからのパイプ引き渡し用のソケットを閉じ、その一時ファイルを消して終了する (SIGPIPEのハンドラ)。 | tryHandoffToParentShell (src/cpp/app/main.cpp) |
| `set_path` | 起動引数のパス(name)を、カレントディレクトリ(dir)を基にフルパス化し、ディレクトリなら末尾へ`/`を補って(T_dir)、ファイルなら(T_file)を返す。`.`・`..`を整理してdirへも書く。 | PathResolver (src/cpp/src/path_resolver.cpp) |
| `from_shell` | SHELLモードの中から起動されたU(引数付き、または標準入力のパイプ)の入力を、親のSHELLの持つUへ引き渡して終了する。引数が無いパイプ入力はUNIXドメインソケットで、ファイル・ディレクトリ・`-u`/`-U`(DU)はファイルへ書いて渡す。 | tryHandoffToParentShell (src/cpp/app/main.cpp) |
| `check_ps` | psとgrepを子プロセスで実行して、同じ端末(-N)で既に動いているか(f_tty)、または指定したプロセスが実際に動作中か(pid)を調べる。動いていれば、メッセージを出して1を返す。 | main (src/cpp/app/main.cpp) |
| `check_pl` | 引数なしの多重起動を防ぐ。~/.<プログラム名>/pidに記録されたプロセスが生きていれば(SIGCONTを送り、ファイルの更新時刻で応答を確認して)、警告を出して終了する。応答が無ければpidファイルを消す。端末指定(f_tty)のときはcheck_psへ回す。 | main (src/cpp/app/main.cpp) |
| `main` | 起動処理全体。作業ディレクトリ(config.shが決めるTMP、既定は/tmp/.<プログラム名>)の確認、環境変数(ENV_PROG、USILIC_LIVE_SHELL)による SHELL内起動の判定、コマンドライン引数の解釈(-p/-l/-u/-U/-h/-?、ディレクトリ・ファイル)、二重起動の確認、設定(conf)・端末(vt_set/vt_init)・シグナルの初期化を行い、menu()のループへ入る。終了時にexit_usilic()で後始末をする。 | main (src/cpp/app/main.cpp) |
| `exit_usilic` | 終了処理。SEGVのハンドラを戻し、SHELLを終了して(ファイルマネージャのとき)ログとエラー一覧をw.N(作業ファイル)へ改名して残し、一時ファイルを消し、端末を元に戻して、確保したメモリと履歴を解放する。 | main (src/cpp/app/main.cpp) |

## vt.c

| 旧関数 | 機能 | C++の対応先 |
|---|---|---|
| `vt_printf` | 書式付きで端末へ出力する(ウィンドウ表示が有効(vt_mode 'D')のときだけ)。VTの関数表のprintfを呼ぶ。 | Display::write (src/cpp/src/display.hpp) |
| `vt_putchar` | 1文字を端末へ出す。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。 | Display::putChar (src/cpp/src/display.hpp) |
| `vt_flush` | 出力をflushする。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。 | Display::flush (src/cpp/src/display.hpp) |
| `vt_endstand` | 反転表示を終える。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。 | Display::setAttribute (src/cpp/src/display.hpp) |
| `vt_stand` | 反転表示を始める。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。 | Display::setAttribute (src/cpp/src/display.hpp) |
| `vt_move` | カーソルを(row, col)へ動かす。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。 | Display::moveCursor (src/cpp/src/display.hpp) |
| `vt_row` | 端末の行数を設定する/返す(引数が0でなければ設定)。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。 | Display::rows (src/cpp/src/display.hpp) |
| `vt_col` | 端末の桁数を設定する/返す(引数が0でなければ設定)。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。 | Display::columns (src/cpp/src/display.hpp) |
| `vt_clear` | 画面を消去する。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。 | Display::clearScreen (src/cpp/src/display.hpp) |
| `vt_eeol` | カーソルから行末までを消去する。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。 | Display::clearToEndOfLine (src/cpp/src/display.hpp) |
| `vt_left` | カーソルを1桁左へ動かす。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。 | Display::cursorLeft (src/cpp/src/display.hpp) |
| `vt_sf` | スクロール領域を1行上へずらす。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。 | Display::scrollForward (src/cpp/src/display.hpp) |
| `vt_sr` | スクロール領域を1行下へずらす。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。 | Display::scrollReverse (src/cpp/src/display.hpp) |
| `vt_rc` | 保存したカーソル位置へ戻す。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。 | Display::restoreCursor (src/cpp/src/display.hpp) |
| `vt_sc` | カーソル位置を保存する。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。 | Display::saveCursor (src/cpp/src/display.hpp) |
| `vt_cs` | スクロール領域を設定する。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。 | TerminalDisplay::applyScrollRegion (src/cpp/src/terminal_display.cpp) |
| `vt_seta` | 文字の属性(太字・下線など)を設定する。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。 | Display::setAttribute (src/cpp/src/display.hpp) |
| `vt_geta` | 属性に対応する端末の制御文字列を返す。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。 | Display::attributeEscape (src/cpp/src/display.hpp) |
| `vt_ks` | カーソルキー等が送る文字列の表を返す。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。 | Display::keySequence (src/cpp/src/display.hpp) |
| `vt_mode` | 端末の状態フラグ('D'=表示有効、'W'=SHELLのSIGWINCH、'H'=ステータス行有り)を取得・設定する。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。 | Display::outputEnabled; Display::setOutputEnabled; Display::modeLineVisible (src/cpp/src/display.hpp) |
| `vt_cursor` | カーソルの表示・非表示を切り替える。vt_*は、端末の実装(VT構造体の関数表)を呼ぶ薄い窓口。 | Display::setVisibleCursor; Display::hideVisibleCursor (src/cpp/src/display.hpp) |
| `vt_set` | 起動形態(f_usilic)を覚え、端末の実装(tc_load)を読み込む。引数0なら、覚えた起動形態だけを返す。 | TerminalDisplay::fromEnvironment (src/cpp/src/terminal_display.cpp) |
| `get_winsize` | 端末のウィンドウの行数・桁数を、/dev/ttyへのioctl(TIOCGWINSZ)で取得する。取得できなければ環境変数(LINES/COLUMNS)などで代用する。 | TerminalDisplay::fromEnvironment (src/cpp/src/terminal_display.cpp) |
| `set_winsize` | 端末のウィンドウの大きさを、ioctl(TIOCSWINSZ)で設定する。 | Shell::start (src/cpp/src/shell.cpp) |
| `vt_getch` | 端末(/dev/tty)から1バイト読み込んで返す。0バイト(Ctrl+Space)は`K_at`として返す。 | KeyInput::readByte (src/cpp/src/key_input.cpp) |
| `vt_charset` | 端末の文字コード(ASCII/EUC/UTF-8)を設定する/返す(引数が0でなければ設定)。 | Charset::Encoding (src/cpp/src/charset.hpp) |
| `vt_winsize` | 端末の大きさを取得して、行数・桁数を記憶し、環境変数LINES/COLUMNSへ反映する。ステータス行が無い端末では、スクロール領域も設定し直す。 | TerminalDisplay::setSize (src/cpp/src/terminal_display.cpp) |
| `vt_setwin` | 行数・桁数を指定して設定し(SIGWINCH後など)、必要ならスクロール領域も設定し直す。 | TerminalDisplay::setSize (src/cpp/src/terminal_display.cpp) |
| `vt_tfd` | 端末の入力に使うファイル記述子を、設定する/返す/閉じる。 | KeyInput::fd (src/cpp/src/key_input.cpp) |
| `vt_init` | 端末を、キー入力用のrawモード(ICANON/ECHO/ISIG等を無効)へ切り替える/元へ戻す。initの値で動作が違う: 1=初期化(/dev/ttyを開き、termiosの元の状態を保存して、rawモードにする)、 2/-9=rawモードへ再設定(2はスクロール領域も設定し直す)、-1/-2=元のモードへ戻す(-1はさらに端末を閉じる)、3/-3=キー待ちの時間(VTIME)を短く/なしにする。 | KeyInput::open; KeyInput::close; KeyInput::leaveRawMode; KeyInput::reenterRawMode (src/cpp/src/key_input.cpp) |

## C++からの逆引き

C++のファイルごとに、対応する旧関数を挙げる(そのファイルを参照しているもの)。

### `src/cpp/app/main.cpp`

`key.c:key_term`, `key.c:save_signal`, `menu.c:init_menu`, `menu.c:menu`, `shell.c:from_main`, `str.c:env_charset`, `str.c:conf`, `str.c:set_usilicrc`, `sub.c:get_time`, `sub.c:save_stderr`, `sub.c:unlink_file`, `sub.c:print_pid`, `sub.c:print_stderr`, `usilic.c:usage`, `usilic.c:main_pipe`, `usilic.c:from_shell`, `usilic.c:check_ps`, `usilic.c:check_pl`, `usilic.c:main`, `usilic.c:exit_usilic`

### `src/cpp/src/application.cpp`

`child.c:is_child`, `disp.c:print_line`, `fork.c:fork_key`, `init.c:init_pipe`, `init.c:init_bin`, `key.c:set_name`, `key.c:set_km`, `key.c:kton`, `key.c:input_key`, `key.c:read_m`, `key.c:find_name`, `key.c:make_cmd`, `key.c:esc_shell`, `key.c:make_list`, `menu.c:m_copy`, `menu.c:m_num`, `menu.c:m_dir`, `menu.c:m_arc`, `menu.c:m_exit`, `menu.c:m_locate`, `menu.c:m_grep`, `menu.c:m_cut`, `menu.c:m_man`, `menu.c:m_help`, `menu.c:m_prog`, `menu.c:m_list`, `menu.c:m_edit`, `menu.c:m_shell`, `menu.c:m_du`, `menu.c:m_yn`, `menu.c:m_dest`, `menu.c:m_pipe`, `menu.c:m_cr`, `menu.c:m_clear`, `menu.c:m_bl`, `menu.c:get_m`, `menu.c:init_menu`, `menu.c:menu`, `mode.c:echo_mess`, `mode.c:load_ll`, `mode.c:mode`, `mode.c:set_cut`, `mode.c:do_prog`, `mode.c:print_intr`, `shell.c:input_shell`, `shell.c:set_pos`, `shell.c:read_shell`, `shell.c:from_main`, `shell.c:do_shell`, `str.c:print_usilic`, `sub.c:is_comm`, `sub.c:set_help`

### `src/cpp/src/archive_mode.cpp`

`alloc.c:alloc_head`, `alloc.c:make_bp`, `fork.c:fork_arc`, `fork.c:read_arc`, `init.c:init_arc`, `init.c:init_arc2`, `init.c:init_bin`, `mode.c:print_arc`

### `src/cpp/src/archive_reader.cpp`

`alloc.c:alloc_head`, `alloc.c:find_tar_header`, `alloc.c:alloc_tar`, `alloc.c:alloc_lha`, `alloc.c:alloc_unzip`, `fork.c:fork_arc`, `fork.c:read_arc`

### `src/cpp/src/charset.cpp`

`code.c:check_type`, `code.c:conv_jis`, `code.c:conv_sjis`, `code.c:jistoeuc`, `code.c:sjistoeuc`, `code.c:uni_to_utf8`, `code.c:euc_to_utf8_core`, `code.c:euctoutf8`, `code.c:euctoutf8_buf`, `code.c:utf8_seqlen`, `code.c:utf8_width`, `code.c:count_k`, `code.c:is_utf8`, `code.c:init_code`, `code.c:toeuc`

### `src/cpp/src/charset.hpp`

`vt.c:vt_charset`

### `src/cpp/src/child_process.cpp`

`alloc.c:alloc_buff`, `fork.c:read_buff`, `fork.c:fork_bin`, `fork.c:fork_chp`, `fork.c:fork_pipe`, `init.c:init_arc2`, `key.c:load_signal`, `mode.c:print_arc`, `sub.c:dup_error`, `sub.c:save_error`

### `src/cpp/src/configuration.cpp`

`child.c:save_child`, `child.c:check_ext`, `child.c:init_child`, `child.c:get_child`, `mode.c:set_cutchp`, `str.c:s_S`, `str.c:s_P`, `str.c:get_prog`, `str.c:get_cp`, `str.c:get_val`, `str.c:conf_arc`, `str.c:conf_bin`, `str.c:conf_grep`, `str.c:conf_prog`, `str.c:conf_bsize`, `str.c:conf_code`, `str.c:conf_history`, `str.c:conf_wait`, `str.c:conf_scroll`, `str.c:env_charset`, `str.c:conf_ls`, `str.c:get_str`, `str.c:conf`, `str.c:init_config`, `str.c:set_usilicrc`, `sub.c:check_chp`

### `src/cpp/src/configuration.hpp`

`child.c:is_child`, `child.c:is_chp`

### `src/cpp/src/content_view.cpp`

`alloc.c:set_attr`, `alloc.c:alloc_pp`, `disp.c:put_str`, `disp.c:disp_pp`, `disp.c:disp_window`, `disp.c:print_line`, `disp.c:disp_prev`, `key.c:stand_str`, `menu.c:m_row`, `menu.c:m_page`, `mode.c:init_rv`, `mode.c:set_rv`, `scroll.c:set_top`, `scroll.c:move_page`, `scroll.c:move_row`, `scroll.c:init_row`, `shell.c:disp_shell`, `shell.c:shell_row`, `shell.c:shell_page`, `sub.c:print_arg`, `sub.c:print_buff`, `sub.c:print_ctrl`

### `src/cpp/src/content_view.hpp`

`scroll.c:init_col`

### `src/cpp/src/copy_mode.cpp`

`code.c:make_map`, `disp.c:disp_copy`

### `src/cpp/src/cp_mv_rm_mode.cpp`

`mode.c:command`, `mode.c:is_write`

### `src/cpp/src/cut_mode.cpp`

`alloc.c:alloc_split`, `fork.c:fork_split`, `init.c:init_cut`, `init.c:init_cut2`, `init.c:init_split`, `init.c:init_split2`, `init.c:init_split3`, `init.c:init_split4`, `menu.c:clear_cut`, `mode.c:set_cut`

### `src/cpp/src/directory_mode.cpp`

`alloc.c:alloc_pp`, `alloc.c:make_bp`, `fork.c:fork_bin`, `init.c:init_ls`, `menu.c:m_dir`, `menu.c:init_bp`, `mode.c:skip_spc`, `mode.c:clear_stack`, `mode.c:check_dir`, `mode.c:fmark`, `mode.c:nmark`, `scroll.c:rv_str`, `scroll.c:get_col`, `sub.c:get_par`, `sub.c:is_access`

### `src/cpp/src/directory_mode.hpp`

`scroll.c:init_col`, `sub.c:is_ls`

### `src/cpp/src/display.hpp`

`vt.c:vt_printf`, `vt.c:vt_putchar`, `vt.c:vt_flush`, `vt.c:vt_endstand`, `vt.c:vt_stand`, `vt.c:vt_move`, `vt.c:vt_row`, `vt.c:vt_col`, `vt.c:vt_clear`, `vt.c:vt_eeol`, `vt.c:vt_left`, `vt.c:vt_sf`, `vt.c:vt_sr`, `vt.c:vt_rc`, `vt.c:vt_sc`, `vt.c:vt_seta`, `vt.c:vt_geta`, `vt.c:vt_ks`, `vt.c:vt_mode`, `vt.c:vt_cursor`

### `src/cpp/src/du_mode.cpp`

`fork.c:fork_du`, `init.c:init_bin`

### `src/cpp/src/edit_mode.cpp`

`mode.c:do_edit`

### `src/cpp/src/exec_mode.cpp`

`alloc.c:alloc_arg`, `fork.c:w_arg`, `fork.c:fork_exec`, `mode.c:do_exec`

### `src/cpp/src/file_mode.cpp`

`alloc.c:esc_pipe`, `alloc.c:alloc_pipe`, `alloc.c:alloc_buff`, `alloc.c:alloc_winch`, `alloc.c:seek_intr`, `child.c:check_ext`, `code.c:is_man`, `code.c:check_type`, `fork.c:read_buff`, `fork.c:fork_chp`, `init.c:init_pipe`, `init.c:init_arc`, `init.c:init_bin`, `menu.c:m_pipe`, `menu.c:init_bp`

### `src/cpp/src/grep_mode.cpp`

`alloc.c:alloc_pp`, `fork.c:fork_grep`, `fork.c:fork_grep2`, `init.c:init_grep`

### `src/cpp/src/help_text.cpp`

`sub.c:set_help`

### `src/cpp/src/history.cpp`

`alloc.c:alloc_str`, `mode.c:free_h`, `mode.c:init_rv`, `mode.c:set_rv`, `mode.c:load_ll`, `mode.c:do_rm`, `mode.c:save_ll`, `scroll.c:init_row`, `sub.c:sspc`, `sub.c:rspc`, `sub.c:print_list`, `sub.c:read_str`, `sub.c:write_str`, `sub.c:delete_str`, `sub.c:delete_name`, `sub.c:save_hist`, `sub.c:find_hist`, `sub.c:save_history`, `sub.c:free_history`, `sub.c:find_list`, `sub.c:save_str`, `sub.c:read_list`, `sub.c:write_list`, `sub.c:load_list`, `sub.c:save_list`

### `src/cpp/src/history.hpp`

`sub.c:is_str`, `sub.c:is_list`

### `src/cpp/src/key_input.cpp`

`key.c:is_esc`, `key.c:key_getch`, `key.c:key_getch_play`, `key.c:key_ttou`, `key.c:key_cont`, `key.c:key_winch`, `key.c:save_signal`, `key.c:read_esc`, `vt.c:vt_getch`, `vt.c:vt_tfd`, `vt.c:vt_init`

### `src/cpp/src/line_editor.cpp`

`key.c:key_eeol`, `key.c:char_bytes_before`, `key.c:b2col`, `key.c:utf8_total_width`, `key.c:utf8_right_shift`, `key.c:need_scroll`, `key.c:input_key`, `key.c:add_buff`, `key.c:edit_line`, `mode.c:ins_q`, `shell.c:input_shell`, `shell.c:read_line`

### `src/cpp/src/list_browser_mode.cpp`

`alloc.c:alloc_l`, `alloc.c:alloc_list`, `alloc.c:alloc_str`, `menu.c:init_bp`, `sub.c:init_str`, `sub.c:init_list`

### `src/cpp/src/locate_mode.cpp`

`init.c:init_ls`

### `src/cpp/src/man_mode.cpp`

`fork.c:fork_man`, `init.c:init_man`

### `src/cpp/src/mode_stack.cpp`

`sub.c:load_grep`, `sub.c:save_grep`, `sub.c:find_grep`, `sub.c:read_stack`, `sub.c:load_stack`, `sub.c:save_stack`, `sub.c:save_stack_dir`

### `src/cpp/src/mode_stack.hpp`

`mode.c:mode`, `mode.c:clear_stack`

### `src/cpp/src/path_resolver.cpp`

`sub.c:get_name`, `sub.c:get_dir`, `sub.c:erase_dot`, `usilic.c:set_path`

### `src/cpp/src/pipe_buffer.cpp`

`alloc.c:save_attr`, `alloc.c:set_attr`, `alloc.c:linein`, `alloc.c:alloc_pipe`, `alloc.c:set_pp`, `code.c:conv_euc`, `code.c:euctoeuc`, `code.c:count_h`, `code.c:count_i`, `disp.c:tabtospc`

### `src/cpp/src/program_mode.cpp`

`mode.c:get_hup`, `mode.c:do_prog`

### `src/cpp/src/shell.cpp`

`alloc.c:alloc_sh`, `key.c:make_list`, `shell.c:write_shell`, `shell.c:input_shell`, `shell.c:hide_ps`, `shell.c:disp_shell`, `shell.c:next_line`, `shell.c:read_line`, `shell.c:print_prompt`, `shell.c:shell_hup`, `shell.c:print_shell`, `shell.c:read_shell`, `shell.c:add_line`, `shell.c:output_shell`, `shell.c:make_shell`, `shell.c:start_shell`, `shell.c:do_shell`, `shell.c:is_inter`, `sub.c:find_prog`, `sub.c:pty_strerror`, `sub.c:open_pty`, `vt.c:set_winsize`

### `src/cpp/src/terminal_display.cpp`

`scroll.c:sr_row`, `scroll.c:sf_row`, `tc.c:outc`, `tc.c:tc_putchar`, `tc.c:tc_flush`, `tc.c:tc_endstand`, `tc.c:tc_stand`, `tc.c:tc_move`, `tc.c:tc_row`, `tc.c:tc_col`, `tc.c:tc_clear`, `tc.c:tc_eeol`, `tc.c:tc_left`, `tc.c:tc_sf`, `tc.c:tc_sr`, `tc.c:tc_rc`, `tc.c:tc_sc`, `tc.c:tc_cs`, `tc.c:tc_seta`, `tc.c:tc_geta`, `tc.c:tc_ks`, `tc.c:tc_mode`, `tc.c:tc_cursor`, `tc.c:tc_load`, `vt.c:vt_cs`, `vt.c:vt_set`, `vt.c:get_winsize`, `vt.c:vt_winsize`, `vt.c:vt_setwin`

### `src/cpp/src/utf8.cpp`

`code.c:utf8_seqlen`, `code.c:utf8_width`

