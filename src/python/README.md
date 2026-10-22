# usilic(Python3のCUI版、学習用の移植)

C++版(`src/cpp/`)のCUI(`usilic -c`)を、Python3の標準ライブラリだけで写したもの。GUI(-g)・Windowsは対象外
(WindowsでSHELLを使うならC++版)。計画と進み具合は`.steering/20261022-python-cui-port/`。

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

SHELL・検索・ARC・EPUB・MAN・CP/MV/RM・履歴・usilicrc・KEY/HELPは、次の段から。

## C++版との対応

モジュールとクラスはC++版と同じ名前(`content_view.py`のContentView ⇔ `content_view.cpp`)。メソッドは
snake_caseにしただけで語は同じ(`ensureLoadedThrough` ⇔ `ensure_loaded_through`)。各関数の説明に、写した
C++の関数名を書いてある。

C++版と違う所: ページャーの読み込み方。C++版は画面が要る所の1行先まで少しずつ読むので、ページ送りの直後は
画面の先頭が少し遅れる(`^V`で79行目の時に先頭41行目)。Python版は2画面先まで読むので遅れない。
