# usilic(Python3のCUI版、学習用の移植)

C++版(`src/cpp/`)のCUI(`usilic -c`)を、Python3の標準ライブラリだけで写したもの。GUI(-g)・Windowsは対象外
(WindowsでSHELLを使うならC++版)。計画と進み具合は`.steering/20261022-python-cui-port/`(第2段・第3段は
`-2/`・`-3/`)。

## 使い方

```
python3 src/python/usilic.py [directory]   # (ls)
python3 src/python/usilic.py file ...      # ページャー(n/p/x/fで切り替え、qで終了)
cmd | python3 src/python/usilic.py         # 標準入力のページャー
```

テスト: `cd src/python && python3 -m unittest discover -s tests -t .`

## 第1段(2026-10-22)で使えるもの

- ページャー: j k ^N ^P ␣ ^V ^F b ^B ^[v ^[< ^[> ^[N(N行目へ、^[100^M) ^X^X ^L n p x f q
- (ls): ^M(ディレクトリへ・ファイルを開く) ^G ^J^B ^J^C ^]
- ^X^C・^[^[(終了の確認、「y」が入った状態で^M)、PREFIXの「C-x-」等(1秒後)
- UTF-8の表示幅(East Asian Width)での折り返しと行頭禁則、日本語のメッセージ(LANGがja)

## 第2段(2026-10-22)で足したもの

- 履歴: C++版と同じ`~/.usilic/history`(ディレクトリで起動した時だけ)。(ls)・ファイルの位置、FILE・DIR・
  コマンド・FGREP/EGREPの履歴。形式と保存の手順(ロック、最新を読み直して自分の変更だけを重ねる)はC++版と同じ
- SHELL: `^[s`で疑似端末のbashへ、`^G`で戻る。コマンド行の^A ^E ^B ^F ^H ^D ^K ^U ^Y、`^C`。bashは
  readline無し(`--noediting`)で動かす(C++版はreadlineのまま、エコーを見分けて取り除いている)
- FGREP/EGREP: `^S`(打つたびに探す、打っている間の`^S`で次へ)・`^J^S`(拡張正規表現)。一致した所に色を
  付け、`^N`/`^P`で一致の間を移り、`^M`・`^G`でやめる

## 第3段(2026-10-22)で足したもの

- キー: C++版と同じK-番号の割り当て(`key_bindings.py`)。usilicrcのKEY節で置き換えられる。接頭キーは
  ^X・^J・^O・^C・^J^O(PREFIXの表示は「C-o-」等)
- usilicrc: ~/.usilic/usilicrc(無ければ~/.local/lib/usilic/usilicrc)のARCHIVE・FILTER・LAUNCH・GREP・BUFFER・
  CODE・LINE・COPY・MOVE・DELETE・KEY。GUIの節・MACRO・URL等は読み飛ばす
- FILE(^Xf・^X^F)・DIR(^Xd・^X^D)・^J^J・^JN。プロンプトの^Rで入力順の一覧、一覧の^Rでソート済みと切り換え
  (EGREPはGREPの例が先頭)、一覧で字を打つとプロンプトへ。^Iでパスの補完
- 選択: (ls)の`.`・^SPC・^Oy・^On(下線)。CP/MV/RM: ^Oc・^Om(「転送先？」、空なら~/.usilic/data/、^Wで
  以前のディレクトリ)・^Or(「削除しますか？ (y/n)」)。SHELLで動かし、成功すれば行き先の(ls)へ
- ARC: (ls)の^Mで内容一覧、^Mで中のfile(中のアーカイブはさらに一覧)、^Oaで展開、^J^A。tar系・zip系は
  標準ライブラリ、lha・rpm・deb・rar・7zはC++版と同じコマンド
- EPUB: (ls)の^Mで本(横書き)、^Gで目次、`;`・`:`で前/次の章、^Je・^J^E・^Oe(ARCとして)
- MAN(^Jm・^J^M、太字・下線)・DU(^Jd・^J^D、^Gで中断)・LOCATE(^J^L・^Jl、macOSは~/.usilic/locate.dbも)
- HELP(^H・^J^H、HELP.epubをEPUBで)・KEY(^K・^J^K、^Mでその行のキーを実行、hで平文のHELPの同じ行へ)
- FILTER(.gz等をgzip -dcで)・LAUNCH(画面を抜けて起動)・CODE(バイナリはfile(1)の結果)・BUFFER

## 第4段(2026-10-22)で足したもの

- PART(^Jp・^J^P)、一致した行の一覧(^O^S・^O^J^S、^Mでその行へ、; :で前/次)
- TOOL(^Jt・^J^T): PROGRAMの物は画面を抜けて、`usilic`は外のページャー、他はSHELLで
- USILIC(^[u・^J^U・^Ju)、作業ファイル(^X^W、/tmp/.usilic/python-PID/、終了で消える)、外のページャー(^Ou)、
  (ls)の␣(クイックルック、macOS)
- MACRO: usilicrcのMACRO節のキー(; : [ ] e l)、^X( ^X) ^Xe、^Xs(^Xeの直後は記録をMACRO節へ、それ以外は
  usilicrcを読み直してMACROの一覧)
- COPY(^O^C、^@/^␣・^X␣で範囲、^[wでクリップボード、^[tでTOOLへ)、プロンプトの^O^Y
- EDIT(^[e、+N付き、終わったら読み直す)、INFO(^I・^J^I)、^J^Ou・^J^Oe・^J^Os・^J^Od・^J^Ow・^[l
- マーク(^␣・^U^␣・^X^X、最大16個)、^U(回数)、^Xz、レジスタ(^Xr␣c・^Xrjc)、ブックマーク(^Xrm・^Xrb・
  ^Xrl、~/.usilic/bookmarks)
- (ls)の^Oo・^Ol・^O^L・%・*、SHELLの^J.・^J,・^Jw・^J^W・^W・^O/・^O^F・^O^B、^Of・^Oj
- EPUB: 本文のリンク(^Mで注釈へ、^Gで戻る、Webのリンクは^Owのモードで)、^Os(章ごと/連続)、^O^E、^Jeの
  作品番号・作品のURL(narou2epub)

## 第5段(2026-10-23)で足したもの

- EPUBの縦書き(`epub_vertical.py`): 本のOPFが`page-progression-direction="rtl"`なら縦書き(C++版と同じく本が
  決める)。1行=1列(全角2桁+行間1桁)を右から左へ。句読点・括弧等は縦書きの字形、半角の英数2字までは縦中横、
  行頭・行末の禁則、字下げ(CSSの1em=1マス)、リンクは列の右の行間の「|」(傍線)
- 縦書きの移動: ←/^N/↓で1列先(左)へ、→/^P/↑で1列戻る(リンクが2つ以上の段落では次/前のリンク)、j/kで右端の
  列を1列ずつ、␣/^Vで1画面先、^[vで1画面戻る、^[<で末尾・^[>で先頭(横書きと逆)、^␣・^U^␣・^X^Xで記録の
  列と行き来、^Mで選んでいるリンクへ(^Gで戻る)。^Osの章ごとでは、章の頭が画面の右端から始まる。FGREP/EGREP・
  目次・^O^S等は横書きの側で動き、着いた所を縦書きの右端に置く(一致の字は縦書きでも色付き)
- 縦書きのCOPY(^O^C): 縦書きのまま字を範囲指定する。^N/←で次の列(左)、^P/→で前の列、^F/↓・^B/↑でマス、
  ^A/^Eで列の頭/末、^V/^[vで1画面。^@で範囲を始め、もう一度で閉じると「Record?(y/n)」で、yで
  ~/.usilic/epub-marks.txtへ記録(栞、C++版と同じ「本.epub:章のパス:位置: 文字列」)。^[wでクリップボード、
  ^[tでTOOLへ、^Gでやめる(C++版の縦書きのCOPYはマウスだけなので、キーの割り当てはこの版の物)
- ^O^E: 本文からは今の本の記録を、記録した字を強調した本文の行の一覧に(^Mでその行へ)。他の画面からは全部の本の
  記録(^Mでその本を開き、記録の所へ)

- EPUBの組版の指定(C++版のEpubLayoutHintsと同じ判定): 本文のファイルが読み込むCSSの単純なセレクタから、
  縦横(writing-mode)・text-align・行頭側の余白(縦書きの本はpadding-top)・text-indent・display:none・font-weightを
  要素ごとに決める
  - 縦書きの本の中の横書きの範囲(奥付・付録等): 横書きで見せ、範囲の外の行は空に。縦書きから送って入る時は
    範囲の最初の行が画面の下から現れ、送り返すと縦書きへ戻る(j/kは範囲が画面に見えているか、↑↓・ページ送りは
    カーソルの行で決める)。^[<は本の末尾の奥付の最下行へ。目次・リンクで範囲の中へ飛んでも横書きに
  - 扉(横書きの中の縦書きの箱)と冒頭の書名・著者名(表紙が先頭ならその後): 1ページの中央に置き、1列の送りで
    ページごと越える。著者名等(text-align: right)は列の下端に寄せる
  - 中央寄せ(text-align: center・区切り線)は横書きで行の中央に。display:noneの見出しは出さない
  - 注釈の区画(epub:typeがfootnote等、または本文と双方向のリンクで結ばれたブロック): リンクで飛ぶとその注釈
    だけの画面に、^Gや注釈の中のリンクで元の所へ
  - 目次の項目が指す所に字が無ければ目次の名前の行に(空の見出し+画像だけの段落は「表紙 [image: …]」の1行に)

- 章の一覧はC++版と同じく、本文の章の行(見出しと、目次が指す所の最初の字の行)の字。隠した見出し(display:none)の
  ある章はその字、最初の章より前に行があれば先頭に「--」

## 第6段(2026-10-23)で足したもの

- マウス(C++版の-cと同じ、SHELL以外で追う): ホイールは1刻み1行(縦書きは1列)、^Odで向きを逆に。右クリックは^G。
  左クリックで行を選び、選んでいる行のクリックで開く((ls)・ARC・DU・LOCATE・一覧・章の一覧・PART・OCCUR・INFO)。
  縦書きの本文は列を選び、リンクの字のクリックでリンクへ。横書きのEPUBの本文もリンクの字のクリックでリンクへ。
  ポインタを動かすとその行にカーソル
- ドラッグで画面を送り(縦書きは左右)、勢いよく離すと慣性。慣性中のクリックは止めるだけ、矢印で速さ・向き。
  ←→(縦書きの本文は↓↑)でも慣性を始める。段の速さはusilicrcのSPEED・ARROW
- COPYはドラッグで範囲(横書きは押し直すと次の範囲、縦書きは離すと「Record?(y/n)」)。ファイルをドロップすると
  開く(いくつもならUSILICの一覧)
- 縦書きの本の章の一覧・検索結果の一覧(^O^S・^O^Eの記録)も縦書き(1項目1列、^N・j・←で次、^[>で先頭・^[<で末尾)

- .md・.markdownは文字装飾付き(C++版と同じく、見出し・強調・コード・リンク・引用・リスト・表・フェンス)

## 第6段(2026-10-30)で足したもの(C++版の10/24〜10/30の分、.steering/20261030-python-cui-catchup/)

- HELP: HELP.1(英語はHELP_en.1)から~/.usilic/HELP.epub(HELP.en.epub)を作って開く(`help_to_epub.py`、C++と同じ
  EPUB)。^OjはHELP(EPUB・平文)・KEYをその場で切り換える。ページャーで使えるキーはHELPの行頭がタブの表から
- 起動オプション: `-h -c -g -H -K -P -s -d -f -z -w -b -l`と大文字(usilicrcへ書く)、`-e`(HELP.1→EPUB)・`-n`・`-m`。
  ~/.usilic/usilicrcが無ければ写し、インストールされた方が新しければ控えを取って置き換える
- 子のウィンドウ: ^T^H・-H(HELP)、^T^K・-K(KEY、^Mは親で実行、hはHELPの子のその行へ)。^Ou・^Jtのusilicも、
  端末の新しいウィンドウ(Terminal.app・iTerm2・gnome-terminal等)で開く(開けなければ前面で)
- 最初の起動(~/.usilic/historyが無い): ページャーのHELP、機能限定SHELL(プロンプトは「USILIC」)。足りない
  プログラムのインストールの案内(`install_offer.py`、~/.usilic/install-offered)
- ^]はどこでも^J^C、^J^Wの保存の名前・SHELL busy、COPYの中のモードキー、^J^Jの行き来を履歴に積まない、
  引数の小さいファイルの位置の形、EPUBの自前の扉をやめる、<u>FONT</u>[6]の下線のリンク、慣性中の;/:

EPUBの余白: ^[の後の矢印で辺を選び、↑で増やす・↓で減らす(横書きは←→の左右の桁、縦書きは↑↓の上下の行、C++版と同じくhistoryに残す)。

この版に無い物: GUI(-g)の物、窓の行き来(^Tn・^Tp)。縦書きの本の連続スクロールで横書きの範囲(付録)に出入りする
所の細かい動き(C++の項目1085・1089・1090)。

## C++版との対応

モジュールとクラスはC++版と同じ名前(`content_view.py`のContentView ⇔ `content_view.cpp`)。メソッドは
snake_caseにしただけで語は同じ(`ensureLoadedThrough` ⇔ `ensure_loaded_through`)。各関数の説明に、写した
C++の関数名を書いてある。

C++版と違う所: ページャーの読み込み方。C++版は画面が要る所の1行先まで少しずつ読むので、ページ送りの直後は
画面の先頭が少し遅れる(`^V`で79行目の時に先頭41行目)。Python版は2画面先まで読むので遅れない。

## RESCUE(2026-11-23、Python版だけ)

- `^J^R`(K-156): /opt/U.sh(rescue-USB・RAMDISKの道具、src/U_proto.sh)のオプションの一覧。-a・-d・-f・-nは
  番号の一覧から選び、引数の要るもの(-s name等)はモードラインで打つ。U.shがその後に聞くこと(再起動するか等)も
  モードラインで聞く。sudoのパスワードはモードラインで(*で出す)。U.shは別のセッションで動かし、出力
  (/tmp/usilic-rescue.log)をSHELLの`tail -f`で見る(usilicを終えてもU.shは止まらない)。Linuxだけ

## U.shのPython版(2026-11-23、`ush.py`・`usilic/ush/`)

- 旧C版(old/)とC++版の関係と同じく、U.sh(旧シェル版、src/U_proto.sh)の機能をオプションごとに移す。移したのは
  `-a`・`-f`・`-n`・`-v`(`usilic/ush/__init__.py`のPORTED)。他は今まで通り/opt/U.sh。/opt/BASE.uの状態にはpython3が無い
  (-uの最初の一歩)ので、U.shはそのために残る
- `sudo python3 src/python/ush.py -f`(U.shと同じ引数、`--list`・`--ans=`・`--batch`も)。RESCUE(^J^R)は、移した
  オプションをこちらで動かす
- 外への働きかけは`usilic/ush/system.py`のSystemに集めてあり、テスト(tests/test_ush.py)は偽の区画・マウント
  (FakeSystem)で手順を確かめる(実機に触れない)
