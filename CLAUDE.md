# tcb (テキストベースファイルマネジャー) 開発メモ

このファイルは Claude Code がセッション開始時に自動で読み込む。
claude.ai の別チャットで進めてきた作業の経緯をここに残しておくので、
Claude Code 側でも「初めまして」にならずに続きから対応できる。

## プロジェクト概要

- 作者(ユーザー)が26年前(1990年代)に書いた自作のテキストベースファイルマネジャー、`tcb`。
- Cソース、約16,000行(`src/`以下)。
- 当時のNetBSD/Linux(32bit)環境で動いていたが、64bit環境への移行後に不具合が出ていた。
- 開発は Linux + macOS の両対応を目指して進行中。Git導入済み(元々ほぼ未使用)。

## これまでの作業(git log 参照)

1. **`conf_arc()`のSegmentation fault**(最初の相談内容)
   - 原因: `s_P((u_int)ARCHIVE)` で `ARCHIVE`(`char**`)を32bitの`u_int`に
     キャストして後で復元するトリックが、64bit環境(ポインタ8byte)で
     上位32bitを切り捨てていた。
   - 修正: `s_P()`の引数型を`uintptr_t`に変更。合わせて`init_config()`内で
     コメントアウトされていた`conf_arc()`呼び出し2箇所を復活(以前の応急処置
     と思われる)。
   - ついでに見つかった別バグ: `getenv("SHELL")`がNULLの場合に`strcpy`で
     クラッシュ → NULLチェック追加。
   - `sub.c`の`ichrstr()`もポインタ差分を`(int)`キャストしていた
     (同種のUB) → `ptrdiff_t`に修正。

2. **macOS対応 / `/dev/pty`問題**
   - 原因: `open_pty()`が古いBSD方式の`/dev/ptyp0`〜`/dev/ptysf`を直接
     スキャンしていた。今のLinuxカーネルにもmacOSにも存在しない方式。
   - 修正: `posix_openpt()`/`grantpt()`/`unlockpt()`/`ptsname()`という
     POSIX標準API(Linux・macOS共通)に置き換え。Cygwin専用だった分岐も統一。
   - `open_pty()`失敗時に具体的なerrno理由を返す`pty_strerror()`を追加
     (元は理由不明のまま"Failed to open /dev/pty.."とだけ出ていた)。
   - `config.sh`: `uname`が`Darwin`の場合の分岐がそもそも無かったので追加。
     `USR_DIR=/usr/local`がデフォルト(Apple Siliconのbrewが`/opt/homebrew`
     の場合は`.config`の`CFLAGS`/`LDFLAGS`に`-I`/`-L`の追加が必要になる
     可能性あり、未検証)。
   - macOSビルドエラー「unknown type name 'u_char'」の原因: `u_char`/
     `u_int`/`u_long`が、Linux(glibc)では別ヘッダー経由でたまたま見えて
     いただけだった。`tcb.h`に`#include <sys/types.h>`を明示的に追加して解決。

3. **UTF-8対応(進行中、段階的に実施)**
   - tcbは元々ASCII/SJIS/EUC/JISのみ対応。UTF-8の概念がなかった。
   - 文字幅判定`count_k()`は「0x80以上のバイトで2byte固定トグル」という
     SJIS/EUC前提の実装だった。UTF-8は可変長(1〜4byte)なのでそのままでは動かない。
   - **Step 1(完了)**: `code.c`に`utf8_seqlen()`(バイト長判定)と
     `utf8_width()`(表示幅判定、CJK/結合文字等をカバー)を追加。`count_k()`を
     `vt_charset(0)==C_utf8`で分岐、UTF-8モードでは正しくマルチバイト境界を
     判定する。`TCBCHARSET=utf8`で切り替え可能。単体テスト16ケース全通過。
   - **Step 2(未着手)**: `code.c`の`init_code()`にあるファイル別の
     SJIS/EUC/JIS自動判定に、UTF-8の判定も追加する。これが無いと今のところ
     Step 1の実装は「宣言はできるが自動発動しない」状態。
   - **Step 3(未着手)**: `count_h()`/`count_i()`(桁位置⇔バイトオフセット
     変換)がまだ「1byte=1桁」前提。`utf8_width()`を組み込む必要あり。
   - **Step 4(未着手)**: SHELLモード自体の独自カーソル追跡(`shell.c`の
     `rv.col`)もUTF-8対応させる。
   - 関連要望: `nkf -w8 README.jp | tcb`でUTF-8ファイルの内容をそのまま
     正しく表示できるようにしたい → Step 2が前提。

4. **ARCモードのクラッシュ・フリーズ・カーソル位置ズレ**(Claude Code移行後、GitHub push済み)
   - シェルモードの改行ループ修正後の実地テストで発覚した4件。すべて
     「32bit時代はポインタ幅とu_intが一致していたが64bitで崩れた」系統の
     バグ(`conf_arc()`と同じクラス)。
   - **ARC一覧が100%CPUで固まる**: `alloc_head()`がGNU tarの
     "owner/group"(スラッシュ区切り)形式決め打ちで、macOSのBSD tarの
     verbose形式(スラッシュなし)を食べると無限ループ。フィールド数を
     数えるフォールバックを追加。
   - **ARCエントリ選択でSegmentation fault**: `PP.tell`が`u_int`型
     だったが、実際は`alloc_tar()`/`alloc_lha()`/`alloc_unzip()`が
     アーカイブバッファへの生ポインタを格納するのに使っていた。64bitで
     上位が切り詰められ、選択時の`memcpy`がガベージアドレスを読んで
     クラッシュ。`uintptr_t`に変更(関連する比較箇所もすべて追随)。
   - **2つ目以降のARCエントリで内容が化ける**: `alloc_tar()`の生バイト
     オフセット走査が「TOC一覧とtarヘッダは1:1対応する」前提だったが、
     macOSのtarが付与する隠しAppleDoubleエントリ(`._filename`、
     `tar tvf`には出ないが実データには存在)でズレる。ファイル名で
     再同期する`find_tar_header()`を追加、ただし512byte境界のみを
     見るようにして偽陽性(例: "file1.txt"が"._file1.txt"の部分文字列
     として誤マッチ)を回避。
   - **CP実行後、SHELLモードに戻って`tcb <dest>`再起動時にフリーズ**:
     `shell.c`の`write_shell()`で、SHELLプロンプト文字列から
     ディレクトリを取り出す`strcpy(dir, &cp[...])`が同じ`dir`バッファ内で
     コピー元・先が重なっていた(overlapping strcpy、UB)。macOSの
     `_FORTIFY_SOURCE`が検知して`__chk_fail_overlap()`内でスタックし
     フリーズ。`memmove()`に変更。
   - **CP/RM/MV/ARCモードに入る(または`^G`で戻る)たびにカーソルが
     先頭行にリセットされる**: `mode.c`の`load_ll()`がカーソル位置
     キャッシュ配列`hp->ll.a[]`(要素は`u_long`、8byte)のインデックスを
     `((u_int)lp - (u_int)&hp->ll.a[0]) / sizeof(u_int)`で計算していた。
     4byteの`sizeof(u_int)`で割っていたためインデックスが2倍になり
     (例: `p.dir`のインデックス2のはずが`p.man`のインデックス4に化ける)、
     ディレクトリの現在位置が正しいスロットに二度と保存されなかった。
     ポインタ演算(`lp - (LIST**)&hp->ll.a[0]`)に置き換えて修正。
   - すべてPTY(pyte)による自動化テストで再現・修正確認済み。

## アーキテクチャ上の発見(参考)

- `tc.c`はncursesの`WINDOW`/`addch`系を一切使わず、termcap(`tgetstr`/
  `tputs`/`tgoto`)の生の制御文字列を`putchar`で直接端末に流す方式。
- SHELLモード(`shell.c`)は子プロセスのpty出力をそのまま垂れ流す
  パススルー型ではなく、出力を1行ずつ自前の行バッファ(`alloc_sh()`、
  ファイル閲覧用バッファと同じ仕組み)に取り込み、プロンプト行を検出しつつ
  独自のカーソル位置(`rv.row`/`rv.col`)で再描画する、一般的な端末
  エミュレータとは異なる独自設計。

## 今後の大きな方向性

- ①クラッシュ系バグ修正(継続中。`-Wpointer-to-int-cast`警告が
  alloc.c/disp.c/fork.c/init.c/key.c/menu.c/mode.c/str.cにあと約20箇所
  残っており、`conf_arc()`と同種の64bit切り詰めバグの可能性がある。優先度
  をつけて順次対応予定)
  - 当面のゴールは「最後にデバッグが完了していた時点(20年以上前)の
    機能がすべて完全動作する」こと。④のオブジェクト指向版への着手は、
    これが達成できてから。
  - 未着手の細かい既知課題: `/tmp/.tcb/`配下に溜まる作業用一時ファイル
    (`e.*`/`l.*`/`w.*`)が自動的に掃除されない(ディスクを圧迫し得る、
    負荷ではなくゴミ掃除の話)。
- ②macOS完全対応(継続中)
- ③Git運用(導入済み、日常的に使えるよう学習中)
- ④学習目的でのC++/Python/Perl5移植(①②③が土台として固まってから着手予定。
  最終的には「オブジェクト指向版tcb」のお手本をClaudeが示す、という
  ゴールも希望されている)
  - 動機の具体例: ARCモードの入れ子対応。今の一覧はアーカイブの中に
    さらにアーカイブファイルがあっても、それを選んで再度ARCモードに
    入る(どこまで潜っても同様に動く)ことができない。現状のCソースは
    場当たり的な作りで、この手の再帰的な機能追加が難しいとユーザーは
    感じている → オブジェクト指向設計への移行で、こうした拡張を
    楽に実装できるようにしたい、というのが動機のひとつ。

## 進め方の合意事項

- 依頼は一度に詰め込みすぎず、段階的に区切って進める(以前まとめて依頼した際に
  制限に達したため)。
- 大きめの変更は必ずビルド確認・可能な範囲でのテストを行ってからコミットする。
- コミットメッセージには「なぜ」を詳しく書く(あとで読み返す用)。
