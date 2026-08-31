#pragma once

// 2026-08-30、直接のユーザー依頼: HELP/HELP.jpのオンラインヘルプ本文を
// C++移植版の現状に合わせて全面的に書き直した上で、ソースへ直接埋め込む
// (「使用前に読まれて『実はすでにGUI』ネタバレを回避すべく」)。
// レガシー版自身のsrc/doc/HELP・HELP.jp(old/自身が今もmake installで
// 使う、historyバイト単位の重ね打ち形式)は意図的に一切変更していない
// ――このヘッダはC++移植版専用の、全く別の新しいヘルプ本文である。
//
// 太字/下線は<b>...</b>/<u>...</u>という軽量な独自マークアップで表現し、
// expandHelpMarkup()(help_text.cpp)が、PipeBuffer::wrapLine()が既に
// 理解する昔ながらの重ね打ちバイト列(X\bX=太字、_\bX=下線、1文字ごと)
// へ実行時に一度だけ変換する――ファイル自体には制御文字が一切無いので
// 素のテキストエディタで安全に編集できる。
//
// 各章見出しは行頭から半角スペース2つ+数字+ピリオドで始まる
// ("  N.  Title"/"  N.M.  Title")――Application::kHelpPattern
// ("^  [0-9]+\\."、application.cpp)がこの形をそのまま前提にして
// CutMode::split()経由で章ごとに分割するため、新しい見出しを足す時も
// この形を厳密に守ること。
//
// 2026-08-31、直接のユーザー依頼で全面刷新後、同日中に2度の訂正が入った
// (docs/HISTORY.md参照):
//   1回目「指示が伝わってない」: src/HELP.txt(ユーザー自身が旧HELP.jp
//     から重ね打ち装飾を剥ぎ取って用意した平文版)を「ベース」に、この
//     アシスタント自身の言葉でセクションを再構成・大幅加筆する形で
//     書き直していたが、これはユーザーの意図(「HELP.txtの編集と同時
//     進行で^J^Hを、legacyバイナリの実機画面=faに戻して欲しい」)を
//     取り違えていた。
//   2回目「『業務連絡版』の意味が全く伝わってない」: 1回目の訂正を
//     受けてsrc/HELP.txt(の平文+ユーザー自身が書き込む`X`/*X*マーク
//     アップ)を機械的に変換したものへ差し替えたが、これも取り違え
//     だった。src/HELP.txt自体は「ユーザーがこのアシスタントへ変更
//     内容を伝えるための、編集しやすい業務連絡用の下書き」に過ぎず、
//     実際にソースへ埋め込むべき一次資料はsrc/doc/HELP.jp(実際の
//     重ね打ちバイト列を持つ、legacyが今も表示するのと全く同じ
//     ファイル)である――ユーザー自身の言葉: 「HELP.jpをソースに
//     取り込んでもらう必要がある。そうすれば見た目は=faに戻る。
//     違いはHELP.jpをCUTしているわけではなく、ソースから展開している
//     点」。
//
// そのため今のkHelpTextJaは、次の手順で作られている:
//   1. src/doc/HELP.jpの生バイト列を、実際のPipeBuffer::wrapLine()
//      (このC++移植自身の、重ね打ちバイト列を解釈する本物のデコーダ
//      ――HELP.jp自身のA_us/A_us2/A_md/A_md2パターンを全て理解する)
//      へ通し、得られた各行のtext+AttributeRun(Bold/Underline)を、
//      隣接する同種の属性ランをまとめた上でこのファイル自身の
//      <b>...</b>/<u>...</u>タグへ変換した(1回限りの変換、scratchpad
//      の小さなC++プログラム経由――手作業の書き起こしではなく、
//      実際のデコーダの出力をそのまま使っているので、legacyが表示する
//      内容と取りこぼし無く一致する)。
//   2. 逆方向のexpandHelpMarkup()(help_text.cpp)で再度重ね打ちバイト列
//      へ戻した結果が、元のHELP.jpをwrapLine()へ直接通した結果と
//      「バイト単位で完全に一致する」ことを、同じくscratchpadの小さな
//      検証プログラムで確認済み(559行・0件の不一致)――この変換が
//      情報を一切失っていないことの裏付け。
//   3. 実際のC++移植で動作が変わり、legacyの説明をそのまま載せると
//      明確に誤りになる箇所「だけ」を、周辺の文体・装飾を保ったまま
//      最小限で修正した(全面的な書き直しはしていない):
//      - "control+t"/"USILIC 画面" → "control+i"/"INFO 画面"
//        (^Tは廃止済み、docs/CHANGES.md参照)。
//      - 2.11(CP/MV/RM/ARC): マークは`^M`ではなく`.`(または^Space)、
//        "^X^Ka"のような「with SHELL window」変種は2026-08-23に削除
//        済み(PendingCpMvRmOperation/kKeyTable自身のコメント参照)、
//        マーク後に移動する対は`,'/`.'ではなく`['/`]'(2026-08-23の
//        再配置)。
//      - 2.14(COPY): ^Spaceでの範囲設定/解除は、本会話での直接の
//        ユーザー依頼によりATOKとの衝突を避けるため^@(開始)/^[w
//        (確定)へ分離済み(copy_mode.cpp/application.cppの
//        feedCopyByte()自身のコメント参照)、確定内容はkillバッファ
//        経由で通常の^Yでも挿入できる。
// src/HELP.txt自体は変更していない――ユーザー自身がこの後も編集し、
// diffで新たな変更を示す「業務連絡用」の下書きであり続ける。ユーザー
// から新しい変更(内容・マークアップとも)が届いたら、それをsrc/HELP.txt
// 自身にではなく、このkHelpTextJa(HELP.jp由来の完全な装飾を持つ側)へ
// 同じ手順で反映すること。
// 以前(2026-08-30)の簡潔なクイックリファレンス版はkKeyReferenceTextJa
// という名前でこのファイルに残っており、新設のKEY(^H)画面の
// 「レファレンス表示」機能(application.cpp、kKeyTable自身のコメント
// 参照)がここから読む。英語版(kHelpTextEn)は今回のスコープ外につき、
// 以前と同じ簡潔な内容のまま変更していない。

#include <string>

namespace fm {

// PipeBuffer(Charset::Encoding)がAsciiでない場合に使う日本語版基本章、
// および常時ASCII/その他の場合に使う英語版基本章。src/doc/HELP.jp自身
// がこのkHelpTextJaの一次資料であることについては、このファイル冒頭の
// コメント参照。
constexpr const char *kHelpTextJa =
R"HELPTXT(
  <b>1.  Introduction</b>

  <b>control</b>+<b>g</b> (control キーと g キーを同時押下) で <b>HELP</b> の目次画面へ戻り、次に以下のキーでカーソルを移動し <b>return</b> キーを押せばその内容を参照できます。

     <b>control</b>+<b>p</b> : カーソルを１行上へ
     <b>control</b>+<b>n</b> : カーソルを１行下へ

  また `;'、`:' キーでそれぞれ一つ上、下の章に移動します。(主なキーの用法は <b>KEY</b> モード (<b>control</b>+<b>h</b> キーで表示) を参照して下さい)

  本文中の表記については以下の通りです。

     キー入力
        <b>^H</b>    : <b>control</b>+<b>h</b>
        <b>^J^H</b>  : <b>^J</b> の後 <b>^H</b>
        <b>^Jb</b>   : <b>^J</b> の後 <b>b</b>
        <b>^X^Ke</b> : <b>^X</b> の後 <b>^K</b> の後 <b>e</b>
        <b>return</b> : <b>^M</b>
        <b>esc</b>    : <b>^[</b>

     ファイル名
        最後が ``/'' で終っている <u>file</u> はディレクトリです (``~'' もディレクトリ名で `echo $HOME return' で表示されます)。またディレクトリおよびスペシャルファイル (通常は <u>/dev/</u> で使用) 以外の <u>file</u> を ``通常ファイル'' で表記しています。

     その他の文字列
        <u>usilic(1)</u> が出力するメッセージは "" で、またキーボードから入力する文字列で <b>^M</b> (<b>control</b>+<b>m</b> の簡略表記) 等以外については `'、それ以外の用語等については ``'' で括って表示しています。

  <u>usilic(1)</u> は下記のいずれかのモードで起動できます。(これらのモード名は <b>control</b>+<b>i</b> キーで表示される <b>INFO</b> 画面の "Startup mode:" で表示されます)

     <b>pager</b>
        パイプ (`ls -l|usilic return' 等) または <u>file</u> (通常ファイル) 引数 (`usilic file return') で起動した場合は <b>pager</b> として動作します。この時は <b>KEY</b> で表示されるモードのみ使用できます。

     <b>file manager</b>
        引数無し (`usilic return') かまたはディレクトリ名 (`usilic . return' 等) の場合は <b>file manager</b> で起動します。

        引数無しで起動した場合、終了時にアクセス履歴のリストが <u>~/.usilic/history</u> に保存されます。詳しくは <b>EXIT</b> の章を参照して下さい。

  <b>2.  Mode</b>

  <u>usilic</u>(1) は、画面最下行に下記のような <b>mode-line</b> を表示します。

     usilic.1 01/66 43% 2000 (groff) /home/user/sources/usilic-1.5b/doc/
     ~~~~~~~~ ~~~~~ ~~~ ~~~~ ~~~~~~~ ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
     [1]      [2]   [3] [4]  [5]     [6]

     <b>[1]</b> : ファイル名
     <b>[2]</b> : カレント行 / 総行
     <b>[3]</b> : カレントページ / 総ページ * 100
     <b>[4]</b> : 表示サイズ
     <b>[5]</b> : プログラム名
     <b>[6]</b> : 以前のモード

  以前のモード名またはファイル名は <b>mode-line</b> <b>[6]</b> に表示され、<b>^G</b> でそこへ戻ります。

  <b>2.1.  File-selector</b>

  [ <b>^J^K</b> ]

  <u>ls</u>(1) を起動して、カレントディレクトリの内容を表示します。カーソル移動後、その行位置のファイルは以下のキーで選択できます。

     <b>^M</b> (<b>return</b>)
        行頭が ``-'' ("-rxwr-xr-x ..." 等) の行は、それが通常ファイルである事を示し、その行位置でこのキーを押せば、ファイルの内容をバッファに読み込んで表示します。

        ``d'' ("drwxr-xr-x ..." 等) の時はディレクトリなので、そこへ移動します。特に <u>../</u> は親ディレクトリを表していて、例えばカレントディレクトリが <u>/home/user/</u> の時は <u>/home/</u> になります。 <u>./</u> を選択 (または <b>^J^K</b>) すると、カレントディレクトリ表示内容を更新します。

     <b>^O^M</b>
        このキーで通常ファイルを選択した時は、行単位入力で表示分まで読み込みます。後述の <b>MAN</b> および "(stdin)" 表示も、このモードになります。これらのモードでは、同じメモリ領域を使用するので、どれかのモードになると他の内容は消えます。

     <b>^[^M</b>
        0x7f および 0x20 未満の機能キャラクタを、反転表示 (0x0d の場合は反転の "M" で表示等)、0x80 以上のコードを反転の "*" で表示します。

  いずれも <b>^G</b> および <b>^J^J</b> で、それぞれ <b>File-selector</b> および以前の通常ファイルへ戻ります。また <b>^X^W</b> は <u>~/.usilic/w.usilic</u> にバッファの内容を保存します。

  下記の <b>suffix</b> を持つ通常ファイルを選択した場合は、<b>File</b> の章の <u>usilicrc</u> に定義されているプログラムを実行します。また特定の <b>suffix</b> を持たなくてもマニュアルページなどの ms マクロ形式で書かれている通常ファイルは <u>groff</u>(1) を起動して整形します。

     <b>.tar</b> <b>.tgz</b> <b>.lzh</b> <b>.zip</b> <b>.rpm</b> <b>.deb</b>
        バッファにファイルを読み込んで内容一覧を表示します。ファイルサイズが <u>usilicrc</u> の BUFFER で指定した値を超えた場合は、読み込みを中止して "<b>Not enough buffer</b>" のメッセージを表示します (<b>.rpm</b> パッケージの内容一覧については、バッファへの読み込みを行わずに <u>pipe</u>(2) を介して表示します)。

     <b>.gz</b> <b>.dvi</b>
        平文表示するために <u>gzip</u>(1) や <u>dvi2tty</u>(1) 等の他のプログラムを実行します。

     <b>.html</b> <b>.info</b>
        <u>lynx</u>(1)、<u>info</u>(1) 等のプログラムを実行します。(<u>usilic</u>(1) の画面上には内容を表示しません)

  <u>usilic</u>(1) 起動後は <b>^J^K</b> で、カレントディレクトリの <b>File-selector</b> になります。また <b>^J</b> の後 <b>0</b> ... <b>9</b> とした時は、カレントディレクトリ名の 0 ... 9 番目のスラッシュ ("/") より左の範囲で示されるディレクトリへ移ります。例えば

  <u>/home/user/src/usilic-1.5b/src/</u>

  での <b>^J2</b> は

  <u>/home/user/</u>

  になります (その際に <b>^J2</b> の代わりに <b>^O^K</b> とした時は親ディレクトリ

  <u>/home/user/src/usilic-1.5b/</u>

  へ移ります)。

  また画面最下行の <b>mode-line</b> 上での入力の場合、文字列の先頭がスラッシュの時は、そのファイル名上で対応するディレクトリへ移ります。例えば `<b>^Xf</b>/usr/lib/usilic/usilicrc <b>return</b>' で <u>usilicrc</u> を参照後 <b>^X^F</b> とタイプした後、

  <u>/usr/lib/usilic/usilicrc</u>

  のカーソル行で <b>^J2</b> とすると

  <u>/usr/lib/</u>

  へ移ります。

  <b>2.2.  KEY</b>

  [ <b>^H</b> ]

  <u>usilicrc</u> で定義したキーバインドおよび対応するモードを表示します ("(M)" は <u>usilicrc</u> の ``KEY'' の ``M-?'' に定義されている組み込みマクロキーです)。<b>^G</b> 以外のカーソル行で、そのまま <b>return</b> キーを押した場合には、<b>KEY</b> を呼び出したモードに戻ったのち、対応するキー定義を実行します (<b>^G</b> 行からは、以前のモードへ戻ります)。

  <b>2.3.  FGREP / EGREP</b>

  [ <b>^S</b> / <b>^J^S</b> ]

  <b>mode-line</b> 行で入力、または検索文字列リストから選択した文字列を <u>fgrep</u>(1) または <u>egrep</u>(1) により検索します (<u>fgrep</u>(1) では、入力された文字列を (正規表現を使用せずに) そのまま検索します)。検索結果はマッチした文字列行リストとして表示され、カーソル位置で <b>return</b> キーを押せばバッファ内の該当位置へ移動します。<u>f</u>(<u>e</u>)<u>grep</u>(1) 実行中に、どれかのキーが押された時は "<b>Interrupted</b>" のメッセージを表示して実行を中断します。

  <b>mode-line</b> 行では 0x20 未満の機能キャラクタを <b>^Q</b> で入力できます (0x08 は <b>^Q^H</b> 等)。また <b>^U</b> キーで新規に文字列を入力できます。その他の編集キーについては、後述 <b>Key</b> の章の <b>Command-line</b> を参照して下さい。

  <b>2.4.  CUT</b>

  [ <b>^Jc</b> or <b>^J^C</b> / <b>^Jb</b> or <b>^J^B</b> ]

  <b>mode-line</b> 行で入力、または検索文字列リストから選んだ文字列を含む行で、今のバッファ内容を分割し、目次リストを作成します。<b>File-selector</b> で <b>^Jb</b> または <b>^J^B</b> でプレーンテキストファイルを選択した場合は、内容をバッファに読み込む前に分割します。

  デフォルト設定 (<b>^J^C</b> または <b>^J^B</b> で表示) の検索文字列では、下記の正規表現を使用しています。(これ以外の指定については、<u>egrep</u>(1) のマニュアルページを参照して下さい)

     <b>[0-9]</b>
        0 から 9 までの任意の１文字

     <b>^</b>  行の先頭

     <b>$</b>  行の末尾

     <b>.</b>  任意の１文字

     <b>\.</b>  ``.'' 文字

     <b>*</b>  0 回以上の繰り返し

     <b>+</b>  1 回以上の繰り返し

  例えば <b>HELP</b> モードは <u>/usr/lib/usilic/HELP</u> のファイル内容を、文字列 "^  [0-9]<b>^H</b>[0-9]+\." の位置で分割していますが、<b>File-selector</b> 上では

     `<b>^Jb</b>^  [0-9]<b>^Q^H</b>[0-9]+\.<b>return</b>'

  で、また <b>File-selector</b> から <b>^M</b> でバッファへ読み込んだ後は

     `<b>^Jc</b>^  [0-9]+\.<b>return</b>'

  で参照できます。

  `<b>;</b>'、`<b>:</b>' で上下へ移動、目次リスト上で特定の文字列を検索後は、`<b>+</b>'、`<b>*</b>' で上下の特定行へ移動できます。(これらのマクロキーの定義内容については、<u>usilicrc</u> の ``P-?'' を参照して下さい)

  <b>^J^J</b> キーでは、戻り先の通常ファイルがこのモードで表示されていた場合、判別のため目次リスト表示の画面へ戻ります。

  <b>2.5.  DU</b>

  [ <b>^Jd</b> or <b>^J^D</b> ]

  <u>du</u>(1) を起動して、カレントディレクトリ以下の内容を表示します。<u>du</u>(1) の実行が終ると、メッセージが表示されるので <b>return</b> キーを押して下さい。

  <b>^J^D</b> とした場合は、再実行せずに <b>DU</b> 画面に戻ります。

  <b>2.6.  LOCATE</b>

  [ <b>^Jl</b> or <b>^J^L</b> ]

  <b>mode-line</b> 行で入力した文字列を <u>locate</u>(1) により検索します。例えば `.tgz <b>return</b>' とすれば、システムに含まれる <b>.tgz</b> という <b>suffix</b> を持つ全ファイル名を表示します。<b>^J^L</b> とした場合は、再実行せずに <b>LOCATE</b> 画面に戻ります。

  システムをインストールした直後等は、このモードは機能しません。その時はスーパーユーザーで <u>updatedb</u>(1) (通常は <u>cron</u>(8) により起動) かまたは <u>locate.updatedb</u>(8) を実行して下さい。

  <b>2.7.  MAN</b>

  [ <b>^Jm</b> or <b>^J^M</b> ]

  <b>mode-line</b> 行で入力、またはマニュアルページリストから選択した文字列について、<u>man</u>(1) を起動して、マニュアルページを表示します (例えば <u>cron</u>(8) のマニュアルページを参照したい時は `<b>^Jm</b> 8 cron <b>return</b>' で表示されます)。

  該当するマニュアルページが無い場合や、必要な環境が整備されていない時は "<b>No manual entry</b>" のメッセージを表示します。

  <b>2.8.  PROGRAM</b>

  [ <b>^Jp</b> or <b>^J^P</b> ]

  <u>usilic</u>(1) を一旦抜けて、<u>program</u> リスト (<u>usilicrc</u> で設定) から選択した <u>program</u> を実行します。<b>^Jp</b> キーで <b>mode-line</b> 行から新規に入力された <u>program</u> 名が、リストに載っていない場合は、後述の <b>SHELL</b> に画面を切り替えて、そこで実行します。

  <b>2.9.  FILE / DIR</b>

  [ <b>^Xf</b> or <b>^X^F</b> / <b>^Xd</b> or <b>^X^D</b> ]

  <b>mode-line</b> 行で入力、または今までにアクセスしたファイルリストから選択したファイルにアクセスします。(その際に <b>^R</b> とした時は、後述の <b>HISTORY</b> モードになります)

  <b>FILE</b> のファイルリストでは、使用したプログラムがあれば、通常ファイル名に "(プログラム名)" を付加して、また <b>CUT</b> で使用された文字列はダブルクォート (") で括って表示します。ただし <b>File-selector</b> で <b>^O^M</b> または <b>^[^M</b> で選択したファイルは <b>^J^J</b> キーや <b>FILE</b> リストでは表示されません。

  <b>2.10.  HISTORY / COMMAND</b>

  [ <b>^R</b> / <b>^J^R</b> ]

  <b>FILE</b>、<b>DIR</b>、<b>MAN</b>、<b>LOCATE</b>、<b>F</b>(<b>E</b>)<b>GREP</b>、<b>CUT</b> の各モードで <b>^R</b> キーを押した場合は、入力順に保存された文字列で <b>HISTORY</b> リストを表示します。

  <b>PROGRAM</b> および <b>SHELL</b> から入力された文字列は、<b>HISTORY</b> および <b>COMMAND</b> の文字列リストに保存されます。<b>COMMAND</b> リストについては、入力文字列について、ソート後、重複を省いて保存され、任意のモードから <b>^J^R</b> キーで参照できます。

  <b>2.11.  CP / MV / RM / ARC</b>

  [ <b>^Xc</b> / <b>^Xm</b> / <b>^Xr</b> / <b>^Xa</b> ]

  <b>File-selector</b> または <b>DU</b> モードで指定したファイルの複写、移動、削除、抽出を行います。

  <b>CP</b>、<b>MV</b>、<b>RM</b> では、`<b>.</b>' (または <b>^Space</b>) でファイルにマークし、<b>^X^M</b> で処理先ファイル (<b>RM</b> では `y') を入力すれば、それらの引数でそれぞれ <u>cp</u>(1)、<u>mv</u>(1)、<u>rm</u>(1) を実行します。<b>RM</b> では <b>FILE</b>、<b>DIR</b>、<b>MAN</b>、<b>EGREP</b>、<b>COMMAND</b> でも任意の行を削除します (<b>DU</b> では <b>RM</b> 等の実行後 <u>du</u>(1) の再実行はしないので、削除したファイルを選ぶとエラーになります)。

  <b>ARC</b> では <u>tar</u>(1)、<u>lha</u>(1)、<u>unzip</u>(1)、<u>rpm</u>(1)、<u>ar</u>(1) によりアーカイブの内容を抽出します。内容のわからないファイルはまず <b>^O^M</b> で確認してから抽出先を指定して下さい。例えば <b>File-selector</b> で <b>^O^M</b> で表示した <u>usilic-1.5b.tar.gz</u> の内容が下記のような場合、
  "-rw-r--r-- user/users     500 Jan  1 00:00 1998 usilic-1.5b/Makefile"
  "-rw-r--r-- user/users    2500 Jan  1 00:00 1998 usilic-1.5b/README"
  "........"

  "Destination ?" (処理先ファイル指定) に `/tmp <b>return</b>' とすれば <u>/tmp/usilic-1.5b/</u> というディレクトリに、全内容が抽出されます (その場合 ``/tmp'' がディレクトリでない時はエラーになります)。"Destination ?" 行では <b>^W</b> キーで、以前のディレクトリ名が表示されます。

  アーカイブの内容一覧を表示した画面上で、特定のファイルだけを `<b>.</b>' でマークしてから <b>^Xa</b> とした場合は、マークしたファイルだけが抽出されます。上の例で Makefile・README をマークした後、"Destination ?" に上のように指定すると

  <u>/tmp/usilic-1.5b/Makefile</u>
  <u>/tmp/usilic-1.5b/README</u>

  で抽出します。

  ファイル指定は <b>^X^Y</b>、<b>^X^N</b> でそれぞれ全指定、全解除できますが、行おうとしている処理がパーミッションに違反している場合は、ファイル指定できません (例えば一般ユーザーが、所有者の異なる読み出し専用のファイルを削除しようとした場合など)。またファイル指定後、処理が正常終了しなかった場合は、"<b>Exited abnormally</b>" のメッセージを表示します。

  `<b>[</b>' または `<b>]</b>' キーでカレント行にマーク後、前または次の行に移動します。また `<b><</b>' または `<b>></b>' で特定のファイルを選択できます。例えば <b>DU</b> 画面上で、末尾が ``~'' のファイル名 (通常はバックアップファイル) だけを削除したい場合、<b>RM</b> を起動し <b>EGREP</b> で `~$' を検索後 <b>return</b> でバッファ内の末尾が ``~'' の最初の行へカーソルを移動すれば、あとは `<b>></b>' キーで該当行を選択できます。

  なお以上の機能は <b>SHELL</b> プロンプトで他のプログラムが動作中の時は機能しません。

  <b>2.12.  SHELL</b>

  [ <b>^[s</b> or <b>^Xs</b> or <b>^J.</b> or <b>^J,</b> or <b>^Jw</b> or <b>^J^W</b> ]

  <b>^[s</b> を押すと、login-<u>shell</u> または <u>usilicrc</u> で定義した <u>shell</u> の仮想端末に切り替わります。(<b>^Xs</b> では <u>shell</u> を再起動します。仮想端末の動作がおかしくなった時は、このキーで初期化して下さい)

  <b>^J.</b> キーで <b>SHELL</b> プロンプトのカレントディレクトリを <b>File-selector</b> の位置に設定します。<b>^J,</b> では <u>usilicrc</u> の K-43 行で `' で指定されたコマンドを実行します。

  <b>^Jw</b> または <b>^J^W</b> の後 <b>return</b> キーで、それぞれ現在のファイル名 (<b>mode-line</b> <b>[1]</b>)、またはバッファ内容を表示します。このコマンドは <b>SHELL</b> プロンプトでは <b>^W</b> で再表示されます。

  プログラム実行中で、プロンプトからの入力ができない場合、<b>CP</b> や <b>PROGRAM</b> 等の、<b>SHELL</b> プロンプトを使用する機能および <b>EXIT</b> モードを実行しようとすると "<b>SHELL busy</b>" のメッセージを表示します。

  例えば <b>SHELL</b> プロンプトで `man bash <b>return</b>' 等した場合、<u>man</u>(1) の出力が <u>usilic</u>(1) にパイプ入力されているので、出力が済んでプロンプトが戻るまでは入力ができません。(この場合は、バッファ末尾への移動キー (`<b>^[></b>') で実行できるようになります)

  <b>SHELL</b> 以外のモードにいる時に <b>SHELL</b> プロンプトが戻った時は "<b>Exit</b>" のメッセージを表示します。(この文字列はモードが切り替わっても継続表示されるので、確認後は <b>^L</b> で消去してください)

  <b>SHELL</b> プロンプトでパイプまたは <u>file</u> 引数で <u>usilic</u>(1) を起動した場合は、それぞれファイル名 "(stdin)"、"file" で内容を表示します (例えば `usilic . <b>return</b>' とすれば、カレントディレクトリの <b>File-selector</b> になります。また上記の `man bash <b>return</b>' 後の出力は、``PAGER'' 環境変数により <u>usilic</u>(1) へのパイプ入力になるので "(stdin)" で表示されます)。この機能では、モード切り替えで使用する <u>socket</u>(2) の認証に <u>shell</u> のプロセス ID を使用しているので <b>SHELL</b> プロンプトで <u>su</u>(1) 等を実行した場合、そのプロンプトからはモードが切り替わりません。(<b>Note</b> の章の <u>socket</u>(2) 参照)

  <b>SHELL</b> モードは、複写等のファイル操作コマンドの実行、およびそのログの保存を行うために作成した仮想端末で、最小限の機能しかないので、エディタやページャ等の、端末設定を操作するプログラムは正常に動作しません。ただし、プログラム名が、<u>program</u> リストに載っている場合は、<b>PROGRAM</b> モードに画面を切り替えて実行されます。そうでないプログラムを起動してしまった時は、<b>^C</b> で実行を中断するか、または <b>^Z</b> でサスペンドしたのち、それらを <u>kill</u>(1) して下さい。

  <b>SHELL</b> プロンプトでコマンドを入力する際は <b>Key</b> の章の <b>Command-line</b> キーで文字列を編集した後 <u>shell</u> に渡しますが <u>ftp</u>(1) 等の対話型のプログラム上では <u>usilic</u>(1) は１文字単位で <u>shell</u> とデータをやり取りします。この時には <b>Key</b> の章の <b>Interaction</b> のキーが使用できます。

  実際は、行頭が "<b>usilic v1.5b</b>:..." (``PS1'' 環境変数の値) になっている行では <b>Command-line</b>、それ以外では <b>Interaction</b> の設定になるので <b>SHELL</b> プロンプトでプログラムを実行した場合、プロンプトが戻るまでは <b>Interaction</b> 以外のキーは使えません。(同モードの際、画面出力中に 0x00 のコードを検出した場合は、それ以降の 0x80 以上および 0x20 未満のコードを空白コード (0x20) で表示します)

  過去に実行したプログラムは <b>^R</b>、<b>^J^R</b> の他、該当プロンプト行までカーソルを移動後 <b>return</b> でも再実行できます。

  <b>2.13.  LIST</b>

  [ <b>^I</b> or <b>tab</b> ]

  <b>SHELL</b> プロンプト、または <b>mode-line</b> 行で文字列を入力中に <b>^I</b> キーを押すと、入力文字列を含むファイル名を表示します。その際ファイルがカレントディレクトリに複数存在する場合は、ビープ音で通知し、そのまま <b>^I</b> キーを押せば、それらのファイル名リストを表示します。

  例えば <u>tc.c</u> および <u>usilic.c</u> という２つのファイルがあるディレクトリの場合 `<b>^Xf</b>tc' とタイプした時点で <b>^I</b> キーを押すと、マッチするファイルが複数あるので、ビープ音が鳴ります。そこでさらに <b>^I</b> キーを押して <u>tc.c</u> と <u>usilic.c</u> のいずれかを選べば、ファイル名がカーソル位置に挿入されます。

  <b>SHELL</b>、<b>PROGRAM</b>、<b>MAN</b> では、入力行の先頭の文字列をプログラム名とみなして ``PATH'' 環境変数 (実行ファイルが置かれているディレクトリ名リスト) 上で検索しますが、文字列の最後がスペースの時は、プログラムへ渡すファイル名をカレントディレクトリから補完します。

  上の例なら `tc' の後の <b>^I</b> では ``PATH'' 上に <u>usilic</u>(1)、<u>tclsh</u>(1)、<u>tcsh</u>(1) の実行ファイルがある場合、`tc<b>^I^I</b>' でこれらが表示されます。一方 `tc <b>^I^I</b>' では "tc" を実行ファイルとみなして、それに渡すファイル名をカレントディレクトリから補完します。

  <b>FILE</b> または <b>DIR</b> では、これらの ``PATH'' 上の実行ファイルの検索は行いません。また <b>DIR</b> ではディレクトリだけが補完の対象になります。

  <b>2.14.  COPY</b>

  [ <b>^O^C</b> ]

  <b>mode-line</b> 以外の任意行内の文字列をコピーバッファへ保存します。<b>^@</b> で範囲の始点を設定し、<b>Command-line</b> キーの <b>^F</b>、<b>^B</b>、<b>^A</b>、<b>^E</b> および <b>^P</b>、<b>^N</b> キーでカーソル位置を移動して範囲を広げ、<b>^[w</b> で確定します。コピー範囲の文字列は反転表示されます。適当な位置で <b>^G</b> キーを押せば、確定せずにこのモードを抜けられます。<b>Command-line</b> では <b>^O^Y</b> でコピーバッファへ保存した文字列が、カーソル位置に挿入されます。確定した内容は削除内容と同じ領域(kill バッファ)にも保存されるので、通常の <b>^Y</b> でも挿入できます。

  <b>2.15.  EDIT</b>

  [ <b>^Je</b> ]

  ファイル名 (<b>mode-line</b> <b>[1]</b>) が通常ファイルで、プログラム名 (<b>mode-line</b> <b>[5]</b>) が空白の時は <u>usilicrc</u> に定義されているエディタを起動します。

  カレント行番号 N を ``+N'' オプションでエディタへ渡して起動するので、同オプションが使用できるエディタを設定しておくと便利です。デフォルトでは <u>vi</u>(1) に設定してあります。

  <b>2.16.  ERROR</b>

  [ <b>^X^Ke</b> ]

  プログラム実行中にエラーが発生した場合、特に子プロセスで起動したプログラムが異常終了した時は "<b>Invalid argument</b>" のメッセージを表示します。その際の、子プロセスでの標準エラー出力の内容は、この画面に表示されます。

  <b>2.17.  LOG</b>

  [ <b>^X^Kl</b> ]

  <b>SHELL</b> のログです。

  <b>2.18.  EXIT</b>

  [ <b>^X^C</b> or <b>^[^[</b> ]

  <u>usilic</u>(1) は今までアクセスしたファイルについて、参照時のカーソル位置等の情報を含む履歴リストを保持しています。

  引数無しで <b>file manager</b> で起動した場合は、<b>EXIT</b> でプログラムを終了する際に、それらのアクセス履歴リストを <u>~/.usilic/history</u> に保存し、次回の起動時にはこのファイルにより、履歴リストを復元します (保存が完了していなかった場合 (<u>~/.usilic/history</u> の末尾が特定コードでない場合) は、読み込みの際に <u>~/.usilic/history.old</u> (前回起動時のファイル) を使用します)。

  また <u>~/.usilic/history</u> の保存の際に <b>ERROR</b> および <b>LOG</b> の内容をそれぞれ <u>~/.usilic/error</u> および <u>~/.usilic/shell</u> に保存します。(ディレクトリ引数で起動した時や、<b>pager</b> で使用する場合は、以上のファイルにはアクセスしません)

  <b>SHELL</b> の章の "<b>SHELL busy</b>" のメッセージが表示された時は、<b>^X^C</b> または <b>^[^[</b> キーで <b>EXIT</b> を再実行して下さい。

  <b>3.  File</b>

  <u>usilic</u>(1) は起動時に <u>~/.usilic/usilicrc</u> を読み込んで環境設定を行います。(このファイルがない場合は <u>/usr/lib/usilic/usilicrc</u> から読み込みます。また <u>~/.usilic/usilicrc</u> の方が日付が古い場合も "<b>~/.usilic/usilicrc ignored.</b>" のメッセージを表示して <u>/usr/lib/usilic/usilicrc</u> から読み込みます)

  以下は <u>usilicrc</u> で定義されている内容です。

     ARCHIVE
        書式：<b>suffix0</b>   <b>suffix1</b>
        用例：.tar.gz   .tgz
        内容：

           <b>suffix0</b> を持つアーカイブファイルの内容を、下記の <b>suffix1</b> (ソースファイルの <u>str.c</u> で定義) の設定で表示。

           ARCHIVE (<u>str.c</u>)
              <u>tar</u>(1)、<u>lha</u>(1)、<u>unzip</u>(1)、<u>rpm</u>(1)、<u>ar</u>(1) の <u>program</u> を実行して <b>suffix1</b> を持つアーカイブファイルの内容一覧を表示。またアーカイブ内で <b>suffix1</b> を持つファイルは <u>program</u> を実行して平文表示。

              <b>suffix1</b>   <u>program</u>   (option)
              .tar      tar       tvf
              .tgz      gzip      -dc | tar tvf -
              .tar.bz2  bzip2     -dc | tar tvf -
              .lzh      lha
              .zip      unzip     -l
              .rpm      rpm       -qplv
              .deb      ar        tv

     FILE
        書式：<b>suffix</b> <u>program</u> (option)
        用例：.gz    gzip    -dc
        内容：

           <u>program</u> を実行して <b>suffix</b> を持つファイルの内容を平文表示。

     EXEC
        書式：<b>suffix</b> <u>program</u> (option)
        用例：.html  lynx
        内容：

           <b>suffix</b> を持つファイル引数で <u>program</u> を実行。

     GREP
        書式：<b>string</b>
        用例：\/\*usilic\*\/
        内容：

           <b>EGREP</b> または <b>CUT</b> で表示される <b>string</b> リスト。

     PROGRAM
        書式：<u>program</u>
        用例：dviout
        内容：

           <b>PROGRAM</b> で表示される <u>program</u> リスト。

     EDITOR
        書式：<u>editor</u>
        用例：vi
        内容：

           <b>EDIT</b> で起動される <u>editor</u>。

     BUFFER
        書式：<b>size</b>
        用例：2
        内容：

           通常ファイルに割り当てるバッファ <b>size</b> (MB)。

     CODE
        書式：<b>code0</b> <b>code1</b>
        用例：1     8     ...
        内容：

           ここでの設定以外の 0x20 未満の機能キャラクタが、上記の <b>suffix</b> を持たない通常ファイルに含まれていて、ファイルの漢字コード (<b>Note</b> の章の ``漢字コード'' 参照) が <b>jis</b> でない場合には、バイナリファイルとみなして <u>file</u>(1) を実行し、該当コードとファイルタイプを表示。

     SHELL
        書式：<u>shell</u>
        用例：tcsh
        内容：

           <u>shell</u> で <b>SHELL</b> モードを起動。(``SHELL'' 環境変数に使用されている値が ``bash'' および ``tcsh'' 以外の場合は、この設定 (<u>bash</u>(1) または <u>tcsh</u>(1) を指定可能) が必要)

     HISTORY
        書式：<b>line</b>
        用例：500
        内容：

           <b>HISTORY</b> リストへ保存する最大 <b>line</b>。(<b>mode-line</b> <b>[2]</b> の値が <b>line</b> に等しくなった後は、一番古い行から削除)

     WAIT
        書式：<b>wait</b>
        用例：10
        内容：

           組み込みまたはキーボードマクロキーを <b>wait</b> (10 msec) 間隔で実行。

     LS
        書式：<b>flag</b>
        用例：1
        内容：

           <u>ls</u>(1) 表示モード (起動後は <b>^Ol</b> で切替) の設定用フラグ。(セットされている時は、ファイル名のみ表示)

     SCROLL
        書式：<b>flag</b>
        用例：1
        内容：

           スクロールモード (起動後は <b>^Oh</b> で切替) の設定用フラグ。(セットされている時は、最下行より上の範囲でスクロール)

     CP
        書式：<u>cp</u> (option)
        用例：cp -rvi
        内容：

           <b>CP</b> モードの <u>cp</u>(1) 起動オプション。(デフォルト: cp -rvf)

     MV
        書式：<u>mv</u> (option)
        用例：mv -vi
        内容：

           <b>MV</b> モードの <u>mv</u>(1) 起動オプション。(デフォルト: mv -vf)

     RM
        書式：<u>rm</u> (option)
        用例：rm -rvi
        内容：

           <b>RM</b> モードの <u>rm</u>(1) 起動オプション。(デフォルト: rm -rvf)

     KEY
        内容：

           <b>KEY</b> で表示されるキーのカスタマイズ。

  <b>4.  Note</b>

     環境
        <u>usilic</u> は <b>file manager</b> で最初に起動する際 <u>~/.usilic/</u> および <u>/tmp/.usilic/</u> (ソースファイルの <u>config.sh</u> で定義) の作業ディレクトリを作成します。

        同一ユーザーが引数無しで <b>file manager</b> を複数起動しようとすると "Already running" のメッセージを出力して終了します。

        <u>usilic</u>(1) は今の端末が <b>SHELL</b> かどうかを判別するのに ``USILIC'' 環境変数を使用しています。特にこの変数が定義されている端末では <u>usilic</u>(1) は <b>file manager</b> で起動できません。

        <b>pager</b> で使用する場合は、端末で一旦 `export PAGER=usilic <b>return</b>' としておけば <u>dvi2tty</u>(1) や <u>man</u>(1) の出力を <u>usilic</u>(1) で参照できます。ただし <b>SHELL</b> では、この ``PAGER'' 環境変数の値は変更できません。

     モードスタック
        <u>usilic</u>(1) は新しいモードを実行するたびに、現在のモードをスタックに積みます。(<b>^G</b> キーではスタックの最上のモードへ戻ります)

        これらのスタックの内容は <b>File-selector</b> でディレクトリを指定するか、または <b>^J1</b> 等でディレクトリを移動するとクリアされます。またこの時は <b>mode-line</b> <b>[6]</b> の位置に "<b>usilic v1.5b</b>" の文字列を表示します。(このスタックの最下層では <b>^G</b> は一つ前のディレクトリに戻ります)

        <u>usilic</u>(1) を終了する時も <u>~/.usilic/history</u> への保存の前に、カレントファイル名 (<b>mode-line</b> <b>[1]</b>) が通常ファイル、またはディレクトリ、またはマニュアルページ、になるまでスタックの内容を解放します。

        <u>kterm</u>(1) 等で、画面サイズを変更してもスタックの保存内容は更新されません。そのため、<b>^G</b> キー後の画面では、継続行が正しく表示されない場合があります。

        <b>DU</b> 画面で、ディレクトリを選んだ時は、<b>File-selector</b> 画面になりますが、そこでさらにディレクトリを選ぶと、上記スタックがクリアされます。この場合は <b>^J^D</b> で <b>DU</b> 画面へ戻ります。

     漢字コード
        <b>sjis</b> (shift-jis) または <b>7bit-jis</b> の漢字コードを含むファイルは、内容をバッファに読み込んだ後 <b>euc</b> (内部コード) に変換します。それに伴い、<b>mode-line</b> <b>[4]</b> (表示サイズ) が実際のファイルサイズとは異なって表示される事があります。

        バッファ内 (内部コード変換前) の漢字コードの判別は、バッファの先頭から行い <b>jis</b> については、コードを検出した時点で終了しますが <b>euc</b> および <b>sjis</b> では、バッファの末尾までチェックした後、多く含まれている方のコードで判別します。

        <b>^O^M</b> でファイル表示した場合や <u>usilic</u>(1) を <b>pager</b> で起動した時は、入力行毎に、判別およびコード変換を行いますが、行内に１バイト半角カナが多用されている場合や、<b>quote</b> 系フォーマットで継続行を含むファイルでは、正常に判別できない場合があります。

        <b>^Oa</b> キーを押すと <b>ascii</b> モードになります。このモードでは、<b>mode-line</b> 以外の任意行内の 0x80 以上のコードを反転の "<??>" (?? は 16進数コード) で表示します。また <b>^[M</b> でベタ表示した場合と同様、内部コードの変換も行わないので例えば <b>^J^W</b> で作業ファイルへ保存したバッファの内容に、元のファイルの漢字コードを反映させたい場合 (例えば <b>sjis</b> コードを含む差分ファイルなど) に使用できます。

        内部コードの設定については、環境変数 ``USILICCHARSET'' に <b>euc</b> または <b>ascii</b> を指定しておけば、その値で起動します (デフォルトは、ソースファイルの <u>config.sh</u> の CHARSET で設定)。起動後も <b>^Oa</b> で切り替えできます。

     プロセス間通信

        <u>socket</u>(2)
           <b>SHELL</b> の章の <u>shell</u> プロンプトで、プログラムの出力を <u>usilic</u>(1) へリダイレクトした場合、その <u>usilic</u>(1) (<u>shell</u> の子プロセス) は <u>shell</u> のプロセス ID を含むファイル名の <u>socket</u>(2) を作成して、自分にリダイレクトされた内容を、そこへ書き込みます。一方 <u>shell</u> を起動した <u>usilic</u>(1) (<u>shell</u> の親プロセス) が、その <u>socket</u>(2) を見つけた場合、上記のリダイレクト内容を、ファイル名 "(stdin)" で、その <u>socket</u>(2) から読み込みます。<u>shell</u> のプロンプトから <u>file</u> 引数で <u>usilic</u>(1) を起動した場合は、<u>socket</u>(2) の代わりに同じ名前の通常ファイルを使用します。

        <u>pipe</u>(2)
           上記の <u>socket</u>(2) の使用以外では、通常ファイルの内容をバッファへ読み込む際に <u>fork</u>(2) した子プロセスとの間で、<u>pipe</u>(2) によるプロセス間通信を使用しています。

  <b>5.  Key</b>

  この章のキーはプログラム内に設定されています。

  <b>5.1.  Window</b>

  <b>^H</b>          : <b>KEY</b>
  <b>PageDown</b>    : 画面を１ページ下へ
  <b>PageUp</b>      : 画面を１ページ上へ
  <b>^V</b>          : 画面を１ページ下へ
  <b>^[v</b>         : 画面を１ページ上へ
  <b>Right-arrow</b> : 画面を１行下へ
  <b>Left-arrow</b>  : 画面を１行上へ
  <b>Down-arrow</b>  : カーソルを１行下へ
  <b>Up-arrow</b>    : カーソルを１行上へ
  <b>^Z</b>          : サスペンド
  <b>^[^[</b>        : 終了

  <b>5.2.  Command-line</b>

  <b>PageDown</b>       : 画面を１ページ下へ
  <b>PageUp</b>         : 画面を１ページ上へ
  <b>^V</b>             : 画面を１ページ下へ
  <b>^[v</b>            : 画面を１ページ上へ
  <b>Right-arrow</b>    : 画面を１行下へ
  <b>Left-arrow</b>     : 画面を１行上へ
  <b>Down-arrow</b>     : カーソルを１行下へ
  <b>Up-arrow</b>       : カーソルを１行上へ
  <b>back space</b>     : カーソル位置から左へ１文字削除
  <b>^H</b>             : カーソル位置から左へ１文字削除
  <b>^K</b>             : カーソル位置から表示行末まで削除
  <b>^U</b>             : カーソル位置から表示行頭まで削除
  <b>^Y</b>             : カーソル位置に削除内容を挿入
  <b>^D</b>             : カーソル位置の文字削除
  <b>^E</b>             : 表示行末へ移動
  <b>^A</b>             : 表示行頭へ移動
  <b>^B</b>             : 左へ１文字移動
  <b>^F</b>             : 右へ１文字移動
  <b>^L</b>             : 画面を再表示
  <b>^N</b>             : カーソルを１行下へ
  <b>^P</b>             : カーソルを１行上へ
  <b>^M</b>             : <b>return</b>
  <b>^Q</b>             : 機能キャラクタ入力
  <b>^G</b>             : 以前のモードへ復帰
  <b>^S</b>             : <b>FGREP</b>
  <b>^R</b>             : <b>HISTORY</b>
  <b>^C</b> (<b>SHELL</b>)     : 入力を中断
  <b>^W</b> (<b>SHELL</b>)     : <b>^J^W</b>、<b>^Jw</b> のコマンド再表示
  <b>^W</b> (<b>CP</b>/<b>MV</b>/<b>ARC</b>) : 以前のディレクトリ名を表示
  <b>^I</b>             : ファイル名補完
  <b>tab</b>            : ファイル名補完
  <b>^Z</b>             : サスペンド

  <b>5.3.  Interaction</b>

  <b>PageDown</b>    : 画面を１ページ下へ
  <b>PageUp</b>      : 画面を１ページ上へ
  <b>^V</b>          : 画面を１ページ下へ
  <b>^[v</b>         : 画面を１ページ上へ
  <b>Right-arrow</b> : 画面を１行下へ
  <b>Left-arrow</b>  : 画面を１行上へ
  <b>Down-arrow</b>  : カーソルを１行下へ
  <b>Up-arrow</b>    : カーソルを１行上へ
  <b>^C</b>          : 入力を中断
  <b>^D</b>          : 入力を終了
  <b>^G</b>          : <b>SHELL</b> を抜ける
  <b>^H</b>          : <b>back space</b>
  <b>^M</b>          : <b>return</b>
  <b>^Z</b>          : サスペンド
)HELPTXT";

// 2026-08-30時点の簡潔なクイックリファレンス版(以前のkHelpTextJa)。
// 2026-08-31、kHelpTextJaが詳細版に置き換わったのに伴いここへ改名して
// 保存 ―― 「試作版HELPも捨てがたい」というユーザー自身の言葉により、
// 新設のKEY(^H)画面の「レファレンス表示」機能(kKeyTable自身の
// コメント参照)が、各キーに対応する簡潔な解説として参照する。
constexpr const char *kKeyReferenceTextJa =
R"HELPTXT(
  1.  はじめに

  <u>control+g</u> でこの目次画面に戻ります。<u>control+p</u> / <u>control+n</u> でカーソルを上下に移動し、<u>return</u> で選択した章を開きます。<u>;</u> / <u>:</u> で前後の章へ直接移動できます。全キー一覧は <u>control+h</u> (KEY) で確認できます。

  本文中、^H は control+h、^J^H は ^J の後に ^H、return は ^M、esc は ^[ を表します。

  2.  モード一覧

  画面最下行の mode-line は [ファイル名] [現在行/総行] [ページ%] [サイズ] [プログラム名] [前のモード] の順で情報を表示します。^G で [前のモード] へ戻ります。

  2.1.  ファイル選択 (^J^K)

  カレントディレクトリの一覧を表示します。<u>return</u> で通常ファイルを開く、ディレクトリならそこへ移動します。<u>^O^M</u> は1行ずつ読み込む簡易ページャ、<u>^[^M</u> は制御コードもそのまま可視化して表示します。<u>^X^W</u> でバッファ内容を作業ファイルへ保存します。

  .tar / .tgz / .zip / .lzh / .rpm / .deb 等のアーカイブは自動的に内容一覧表示になります(ARC参照)。

  2.2.  KEY (^H)

  usilicrc で定義したキー割り当てと対応モードの一覧です。行を選んで <u>return</u> を押すと、その定義を呼び出したモードへ戻ってから実行します。

  2.3.  FGREP / EGREP (^S / ^J^S)

  文字列検索です。fgrep は入力文字列をそのまま検索、egrep は正規表現として検索します。結果一覧から <u>return</u> でバッファ内の該当箇所へ移動します。

  2.4.  CUT (^Jc / ^J^C / ^Jb / ^J^B)

  正規表現(または固定文字列)にマッチする行でバッファを分割し、目次のように一覧表示します。HELPのこの目次画面も内部的にはCUTの一種です。<u>;</u> / <u>:</u> で章の前後移動、目次一覧上でさらに検索した場合は <u>+</u> / <u>*</u> で該当行へ移動できます。

  2.5.  DU (^Jd / ^J^D)

  カレントディレクトリ以下の使用量を du(1) で集計表示します。^J^D で再実行せずDU画面へ戻ります。

  2.6.  LOCATE (^Jl / ^J^L)

  locate(1) によるファイル名検索です。事前に updatedb(1) が実行されている必要があります。

  2.7.  MAN (^Jm / ^J^M)

  man(1) を起動してマニュアルページを表示します。例: `^Jm 8 cron return`。

  2.8.  PROGRAM (^Jp / ^J^P)

  usilicrc の PROGRAM リストから選んだプログラムを実行します。リストに無い名前を入力した場合はSHELLへ切り替わります。

  2.9.  FILE / DIR (^Xf・^X^F / ^Xd・^X^D)

  アクセス履歴からファイル・ディレクトリを選んで移動します。<u>^R</u> でHISTORY一覧に切り替えられます。

  2.10.  HISTORY / COMMAND (^R / ^J^R)

  各モードで入力・選択した文字列の履歴です。COMMANDはSHELL/PROGRAMで実行したコマンドの重複無しリストで、どのモードからでも ^J^R で参照できます。

  2.11.  CP / MV / RM / ARC (^Xc・^Xm・^Xr・^Xa)

  ファイルの複写・移動・削除・アーカイブ展開です。<u>.</u> (または <u>^Space</u>) でマーク、<u>^X^M</u> で処理先(RMは`y`)を入力すると実行されます。<u>^X^Y</u> / <u>^X^N</u> で全マーク・全解除。ARCは展開先ディレクトリを指定するとアーカイブ全体を、個別ファイルを選べばそれだけを取り出せます。

  2.12.  SHELL (^[s / ^Xs / ^J. / ^J, / ^Jw / ^J^W)

  ログイン(または usilicrc 指定)シェルの仮想端末です。^Xs で再起動。^J. でSHELLのカレントディレクトリをFile-selectorに合わせ、^Jw / ^J^W でファイル名・バッファ内容をプロンプトへ挿入します。エディタ・ページャ等、端末制御を必要とするプログラムはSHELL内では正しく動作しません(EDIT/PROGRAM/MANモードをお使いください)。

  2.13.  ファイル名補完 (^I / tab)

  入力中の文字列を含むファイル名を補完します。候補が複数ある場合はビープ後、もう一度押すと一覧表示します。

  2.14.  COPY (^O^C)

  画面内の任意の文字列をコピーバッファへ保存します。^@ で範囲の始点を設定、Command-lineの移動キーでカーソルを動かし、^[w で確定。汎用エリア(killバッファ)へ保存されるので ^Y で挿入します。

  2.15.  EDIT (^Je)

  usilicrc 指定のエディタを、現在行を+N オプションで指定して起動します。

  2.16.  ERROR (^X^Ke)

  子プロセスの異常終了時のエラー出力です。

  2.17.  LOG (^X^Kl)

  SHELLの操作ログです。

  2.18.  EXIT (^X^C / ^[^[)

  終了時、引数無し起動であればアクセス履歴を ~/.usilic/history に保存し、次回起動時に復元します。ERROR/LOGの内容も ~/.usilic/error・~/.usilic/shell に保存されます。

  2.19.  INFO (^I)

  起動オプション・usilicrcのパス・ERROR/SHELLログ・作業ファイルの一覧を表示します。旧バージョンの ^T に相当します。

  3.  設定ファイル (usilicrc)

  起動時に ~/.usilic/usilicrc (無ければ共有ライブラリの usilicrc) を読み込みます。主な設定項目:

     ARCHIVE / FILE / EXEC   拡張子ごとに起動するプログラムの対応表
     GREP / PROGRAM          EGREP・CUT・PROGRAMで使う候補リスト
     EDITOR                  EDITで起動するエディタ
     CP / MV / RM            各モードで実行するコマンドのオプション
     HISTORY / BUFFER        履歴の保存件数・通常ファイル用バッファ容量
     KEY                     KEYモードで表示するキー割り当てのカスタマイズ

  4.  補足

  usilic(1) はモードを実行するたびにモードスタックへ積み、^Gで一段戻ります。ディレクトリを直接指定して移動するとスタックはクリアされます。

  文字コードは起動時に自動判別し、^Oa で ASCII / EUC / UTF-8 を切り替えられます。環境変数 USILICCHARSET でも指定できます。

  5.  キー一覧

  5.1.  Window

     ^H            KEY
     PageDown/^V   画面を1ページ下へ
     PageUp/^[v    画面を1ページ上へ
     ↓/↑          カーソルを1行上下へ
     ^Z            サスペンド
     ^[^[          終了

  5.2.  Command-line (mode-line への文字列入力)

     ^F/^B         カーソルを右/左へ
     ^A/^E         行頭/行末へ
     ^K/^U         カーソルから行末/行頭まで削除
     ^Y            削除内容を挿入
     ^D            カーソル位置の文字を削除
     ^L            画面を再描画
     ^S            FGREP
     ^R            HISTORY
     ^I/tab        ファイル名補完
     ^Q            制御文字を直接入力

  5.3.  Interaction (SHELLでの対話的プログラム実行中)

     ^C            入力を中断
     ^D            入力を終了
     ^G            SHELLを抜ける
     ^H            back space
)HELPTXT";

constexpr const char *kHelpTextEn =
R"HELPTXT(
  1.  Introduction

  Press <u>control+g</u> to return to this table of contents. Move the cursor with <u>control+p</u> / <u>control+n</u> and press <u>return</u> to open a section. <u>;</u> / <u>:</u> jump directly to the previous/next section. The full key list is under <u>control+h</u> (KEY).

  In this text, ^H means control+h, ^J^H means ^J followed by ^H, return means ^M, and esc means ^[.

  2.  Modes

  The mode-line at the bottom of the screen shows [name] [current/total lines] [page %] [size] [program] [previous mode], in that order. ^G returns to [previous mode].

  2.1.  File-selector (^J^K)

  Lists the current directory. <u>return</u> opens a regular file or enters a directory. <u>^O^M</u> loads a file a line at a time (a simple pager); <u>^[^M</u> shows control codes as visible glyphs instead of interpreting them. <u>^X^W</u> saves the current buffer to a work file.

  Archives (.tar, .tgz, .zip, .lzh, .rpm, .deb, ...) automatically open as a listing -- see ARC below.

  2.2.  KEY (^H)

  Lists every key binding defined in usilicrc together with the mode it triggers. Selecting a row and pressing <u>return</u> returns to the mode that invoked KEY and runs that binding.

  2.3.  FGREP / EGREP (^S / ^J^S)

  Searches the buffer. fgrep matches the input literally; egrep treats it as a regular expression. <u>return</u> on a result jumps to that line.

  2.4.  CUT (^Jc / ^J^C / ^Jb / ^J^B)

  Splits the buffer wherever a pattern (fixed string or regex) matches, and shows the matches as a table of contents -- this HELP screen is itself one CUT session. <u>;</u> / <u>:</u> move between sections; after searching within the table of contents, <u>+</u> / <u>*</u> jump to the matching row.

  2.5.  DU (^Jd / ^J^D)

  Runs du(1) on the current directory. ^J^D returns to the last result without re-running it.

  2.6.  LOCATE (^Jl / ^J^L)

  Searches file names via locate(1) (updatedb(1) must have run beforehand).

  2.7.  MAN (^Jm / ^J^M)

  Runs man(1). Example: `^Jm 8 cron return`.

  2.8.  PROGRAM (^Jp / ^J^P)

  Runs a program from usilicrc's PROGRAM list. A name not in the list falls through to SHELL instead.

  2.9.  FILE / DIR (^Xf/^X^F, ^Xd/^X^D)

  Jumps to a file or directory from your access history. <u>^R</u> switches to the HISTORY list.

  2.10.  HISTORY / COMMAND (^R / ^J^R)

  Per-mode input history. COMMAND is a deduplicated list of commands run from SHELL/PROGRAM, reachable from any mode via ^J^R.

  2.11.  CP / MV / RM / ARC (^Xc, ^Xm, ^Xr, ^Xa)

  Copy, move, delete, or extract. <u>return</u> marks an entry; <u>^X^M</u> asks for a destination (RM asks for `y`) and runs the command. <u>^X^Y</u> / <u>^X^N</u> mark/unmark everything. In ARC, giving a destination directory extracts the whole archive; marking individual entries first extracts only those.

  2.12.  SHELL (^[s, ^Xs, ^J., ^J,, ^Jw, ^J^W)

  A virtual terminal running your login (or usilicrc-configured) shell. ^Xs restarts it. ^J. sets the shell's current directory to match File-selector; ^Jw / ^J^W insert the current file name / buffer contents at the prompt. Editors, pagers, and other programs that need real terminal control do not work correctly inside SHELL -- use EDIT/PROGRAM/MAN instead.

  2.13.  Filename completion (^I / tab)

  Completes a filename containing what you've typed so far. If more than one file matches, it beeps; pressing it again lists the candidates.

  2.14.  COPY (^O^C)

  Saves any on-screen text into the copy buffer. ^space marks the start/end of the range, the usual Command-line motion keys move the cursor, ^G confirms. ^O^Y pastes it back.

  2.15.  EDIT (^Je)

  Launches the editor configured in usilicrc, passing the current line as a `+N` option.

  2.16.  ERROR (^X^Ke)

  Standard error captured from a failed child process.

  2.17.  LOG (^X^Kl)

  The SHELL session log.

  2.18.  EXIT (^X^C / ^[^[)

  On exit from a no-argument launch, your access history is saved to ~/.usilic/history (restored on the next launch), along with ERROR/LOG contents under ~/.usilic/error and ~/.usilic/shell.

  2.19.  INFO (^I)

  Shows boot options, the usilicrc path, ERROR/SHELL log paths, and any allocated work files. This replaces the older ^T screen.

  3.  Configuration (usilicrc)

  Read from ~/.usilic/usilicrc at startup (falling back to the shared usilicrc if that doesn't exist). Notable entries:

     ARCHIVE / FILE / EXEC   which program to run for which file suffix
     GREP / PROGRAM          candidate lists used by EGREP/CUT/PROGRAM
     EDITOR                  the editor EDIT launches
     CP / MV / RM            command-line options for each mode
     HISTORY / BUFFER        history size / buffer size for regular files
     KEY                     customizes the bindings shown in KEY

  4.  Notes

  Each mode you enter is pushed onto a mode stack; ^G pops one level. Jumping straight to a directory clears the stack.

  Character encoding is auto-detected at load time; ^Oa cycles ASCII / EUC / UTF-8. USILICCHARSET can set it explicitly.

  5.  Key Reference

  5.1.  Window

     ^H            KEY
     PageDown/^V   page down
     PageUp/^[v    page up
     down/up       cursor down/up
     ^Z            suspend
     ^[^[          quit

  5.2.  Command-line (typing into the mode-line)

     ^F/^B         cursor right/left
     ^A/^E         start/end of line
     ^K/^U         delete to end/start of line
     ^Y            paste deleted text
     ^D            delete char under cursor
     ^L            redraw screen
     ^S            FGREP
     ^R            HISTORY
     ^I/tab        filename completion
     ^Q            insert a literal control character

  5.3.  Interaction (an interactive program is running inside SHELL)

     ^C            interrupt
     ^D            end input
     ^G            leave SHELL
     ^H            backspace
)HELPTXT";


// Display::isGraphical()が真の間だけApplication::wrapHelpText()が
// 末尾に追加する、GUI専用の章。
constexpr const char *kHelpTextGuiJa =
R"HELPTXT(

  6.  GUI (-g / ダブルクリック起動)

  ここは GUI ウィンドウ表示中だけに現れる章です。

  6.1.  フォントサイズ・配色

  <u>Cmd +</u> / <u>Cmd -</u> でフォントサイズを変更、<u>Cmd 0</u> で起動時のサイズへ戻します。<u>Cmd K</u> で組み込みの配色(標準・緑地に黒・黒地にアンバー・白地に黒)を順に切り替えます。変更内容は次回起動時にも記憶されます。

  6.2.  フォントの指定

  起動時に <u>-f <フォントファイルのパス></u> を指定すると、そのフォントを使用します。一度指定すれば以後は記憶され、アイコンからのダブルクリック起動でも同じフォントで開きます。

  6.3.  マウス操作

  一覧・内容表示エリアをクリックしたままドラッグすると画面をスクロールします。右端のスクロールバーを直接ドラッグすると、その位置へ一気に移動します。一覧上の行は、クリックでカーソル移動、すでにカーソルがある行を再度クリックすると <u>return</u> と同じ選択動作になります。

  6.4.  前回の続きから再開

  引数無しで起動すると、前回終了時に通常ファイルまたはアーカイブの内容一覧を表示していた場合は同じ画面から再開します。該当のファイルが移動・削除されていた場合は、その在籍先ディレクトリ(無ければ実在する祖先ディレクトリ)で起動します。
)HELPTXT";

constexpr const char *kHelpTextGuiEn =
R"HELPTXT(

  6.  GUI (-g / launched from an icon)

  This section only appears while running in a GUI window.

  6.1.  Font size and colors

  <u>Cmd +</u> / <u>Cmd -</u> change the font size, <u>Cmd 0</u> resets it to the size the window opened with. <u>Cmd K</u> cycles the built-in color presets (default, green-on-black, amber-on-black, black-on-white). Changes are remembered for the next launch.

  6.2.  Choosing a font

  Pass <u>-f <path-to-a-font-file></u> at startup to use that font. Once given, it is remembered from then on -- including when the app is opened later by double-clicking its icon.

  6.3.  Mouse

  Click and drag inside a listing or content view to scroll it. Dragging the scrollbar on the right jumps straight to that position. Clicking a row moves the cursor there; clicking the already-selected row again acts like pressing <u>return</u>.

  6.4.  Resuming where you left off

  A no-argument launch resumes exactly where the previous session ended, if that was a regular file or a top-level archive listing. If that path no longer exists, it starts in whichever directory it used to live in (or the nearest surviving ancestor).
)HELPTXT";


// <b>X</b>/<u>X</u>を、PipeBuffer::wrapLine()が理解する昔ながらの
// 重ね打ちバイト列(1文字ごとのX\bX/_\bX)へ変換する。定義はhelp_text.cpp。
std::string expandHelpMarkup(const std::string &text);

}  // namespace fm
