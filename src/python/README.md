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

この版に無い物: マクロ・COPY・EDIT・TOOL・USILIC(^[u等)・PART・INFO・ブックマーク・レジスタ・^U、EPUBの
縦書き・Webのリンク(^Ow)。

## C++版との対応

モジュールとクラスはC++版と同じ名前(`content_view.py`のContentView ⇔ `content_view.cpp`)。メソッドは
snake_caseにしただけで語は同じ(`ensureLoadedThrough` ⇔ `ensure_loaded_through`)。各関数の説明に、写した
C++の関数名を書いてある。

C++版と違う所: ページャーの読み込み方。C++版は画面が要る所の1行先まで少しずつ読むので、ページ送りの直後は
画面の先頭が少し遅れる(`^V`で79行目の時に先頭41行目)。Python版は2画面先まで読むので遅れない。
