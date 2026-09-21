# 旧C実装とC++版の機能差の一覧(GUIを除く)

2026-09-21作成。`docs/ideas/BACKPORT.md`の「進め方1」(機能差の一覧化)の成果物。

## 根拠と限界

- **旧の全382関数の対応表**(`docs/legacy-cpp-map.md`、旧ソースのコメントから生成)。C++に対応が無い
  ものは「なし(理由)」と明記してある。
- **旧のキー表**: 最初のコミット(`47e2a1f:tcbrc`)のK-0〜K-88(86個)。
- **C++のキー処理**: `src/cpp/src/application.cpp`のキーのディスパッチ(caseのコメント)。
- **旧ソースの機能の有無**: `grep`で旧ソース(`old/src`)を確認した(確認したものは「確認済み」と書く)。
- **C++で追加された機能**: `docs/CHANGES.md`とgitログ(件名)から。差分そのものは読んでいないので、
  「移植の大きさ」は`docs/ideas/BACKPORT.md`と同じく粗い見積もりである。

## 1. 旧の382関数と、C++の対応

| | 関数数 |
|---|---|
| C++に対応がある | 323 |
| C++に対応が無い(なし) | 59 |

「なし」の59関数は、次の2種類に分かれる。

- **言語・構造の違いで不要なもの(29関数)**: メモリの解放(`free_*`等、C++はデストラクタとSTLが行う)、標準
  ライブラリで足りるもの(`utoa`、`ichrstr`等)、SHELLの2プロセス間の通信(`write_comm`等、C++は単一の
  ループ)、重ね打ち(`tobo`等、C++は属性で持つ)。
- **旧にあってC++に無い機能(30関数)**: 次の節。

## 2. 旧にあって、C++に無い機能(逆方向の差)

C++版にこの機能を足す(旧→C++)かどうかは、別の判断になる。

| 機能 | 旧の関数 | 内容 |
|---|---|---|
| usilicrcのKEY節(キー割り当ての変更) | `conf_key`、`conf_m`、`conf_f`、`conf_c`、`set_key`、`read_mark`、`print_key`、`save_ov`、`set_m`、`set_s`、`key_err`(11) | 旧はキー(K-?)、ファンクションキー(F-?)、マクロ(M-?)を設定で変えられた。C++は固定(KEYモードも2026-09-20に廃止) |
| MIMEヘッダの復号 | `decode_mime`、`decode_base64`、`decode_quote`、`b_code`、`q_code`、`set_func`、`conv_q`、`cat_mime`(8) | `=?ISO-2022-JP?B?...?=`のような、メールのヘッダを復号して表示・検索できた |
| fileコマンドによる型の表示 | `type_file`、`type_pipe`、`fork_file`(3) | バイナリのファイルの種類を、`file`で調べて表示した |
| 画面の内容をファイルへ書き出す | `m_w`、`send_pp`(2) | `^X^W`(K-17)で、バッファを`w.usilic`へ書き出した |
| SIGSEGVからの回収 | `init_clear`、`menu_clear`、`key_segv`、`sub_clear`(4) | クラッシュを検知して、状態を戻して続行した。C++は回収せず、範囲チェックとRAIIで防ぐ設計 |
| FILE履歴の`(プログラム名)`表記 | `cat_child`、`load_chp`(2) | 履歴の項目にプログラム名を付けた |

## 3. C++にあって、旧に無い機能(移植の候補)

「確認済み」は、旧ソースを`grep`して、対応するものが無いことを確かめたもの。大きさは、旧のCで
実装する場合の、新規・変更行数の目安(小=約100行、中=約500行、大=約1,500行以上)。

| # | 機能のまとまり | 旧の状況 | 旧で触る主な関数 | 大きさ |
|---|---|---|---|---|
| A | **キー体系の再編**(`^X`プレフィックスの廃止→`^O`/`^J`、新設の`^J^U`・`^Ju`・`^[u`・`^Ou`・`^J0〜9`等) | 旧のキーはK-0〜K-88の86個。`^X`系は25個。C++で`^X`に残るのは、caseのコメントから数えた範囲で8個(d、^D、^C、f、^F、(、)、e)。CP/MV/RM/ARCは`^O`へ、全選択/全解除は`^J y`/`^J n`へ移った | `read_m`、`input_key`、`edit_line`、`read_esc`、`set_km`、usilicrc | 大 |
| B | **履歴と`^R`の再設計**(カテゴリ別のリング、`^J^P`、USILIC履歴) | 旧は履歴が単一のHIST+COMMAND(`^J^R`)。USILIC履歴(確認済み: `USILIC_HISTORY`は旧に無い) | `read_list`、`write_list`、`save_history`、`is_str`、`is_list`、`init_str`、`init_list`、`m_list`、`load_ll` | 大 |
| C | **GREP**(その場ハイライト、インクリメンタルサーチ`^S`、`grep -F/-E`化) | インクリメンタルサーチは旧に無い(確認済み)。旧は自前のGREP | `fork_grep`、`fork_grep2`、`alloc_pp`、`init_grep`、`m_grep`、`put_str`、`stand_str` | 大 |
| D | **一覧の戻り値・注釈の統一と、モードラインの再設計**(DIR/FILE/MAN/DU/LOCATE/USILIC/CUT) | 旧のモードラインは固定の欄の並び | `disp_window`、`print_line`、`disp_prev`、`print_arg`、`m_bl`、`load_stack`、`save_stack`、`clear_stack` | 大 |
| E | **アーカイブの拡張**(rar、7z、xz、zst、入れ子、`^O^M`のtvf表示) | 旧の組み込みは7種類(tar、tgz、tar.bz2、lzh、zip、rpm、deb)。rar/7z/zst/xzは旧に無い(確認済み) | `conf`(ARCHIVE表)、`fork_arc`、`read_arc`、`init_arc`、`init_arc2`、`alloc_head`、`print_arc`、`is_chp`、`check_ext` | 中 |
| F | **LOCATEの拡張**(`updatedb`の実行、`^C`での中断、長い行の中央省略) | `updatedb`は旧に無い(確認済み) | `m_locate`、`init_ls`(T_locate)、`fork_bin` | 小〜中 |
| G | **PROGRAMの拡張**(選択中のファイルの自動挿入、自分自身の再帰起動、Quick Look) | Quick Lookは旧に無い(確認済み、macOS専用) | `m_prog`、`do_prog`、`make_list` | 小〜中 |
| H | **COPY/PASTEの拡張**(矩形選択、行番号へのジャンプ) | 旧にも`^O^C`(Copy string)/`^O^Y`(Paste)はある。矩形選択は旧に無い(確認済み) | `m_copy`、`make_map`、`disp_copy` | 中 |
| I | **SHELLの拡張**(約30項目: 引き渡し、PAGERの解釈、Tab補完の統一、`^R`など) | 旧のSHELLは、表示側と入力側の2プロセス構成。C++は単一のループ | `shell.c`の全体(25関数) | 大 |
| J | **プロセス・シグナル**(引数なしの多重起動の乗っ取り、非同期読み込みと`^G`での中断) | 旧は多重起動を警告して終了(`check_pl`、`check_ps`)。SIGTERM/SIGHUPのハンドラ(`key_term`)は旧にもある | `check_pl`、`check_ps`、`from_shell`、`read_buff`、`alloc_buff`、`save_signal` | 中 |
| K | **表示の細部**(タブの内部表現U+2060、禁則処理、ANSI属性の解釈) | 禁則処理、ANSI/SGRは旧に無い(確認済み) | `put_str`、`set_pp`、`alloc_pipe`、`count_h`、`count_i` | 中 |
| L | **DUの`ls -F`相当**(型マーカー) | 旧のDUは型マーカーを付けない | `fork_du`、`init_bin`(M_du)、`alloc_pp` | 小 |
| M | **文書**(TUTORIAL、HELPの全面改稿) | 旧の文書は`HELP`/`HELP.jp` | (コードではなく文書) | — |

### 合計の見積もり

- 大が5個(A、B、C、D、I)、中が4個(E、H、J、K)、小〜中が2個(F、G)、小が1個(L)。`BACKPORT.md`の
  見積もり(約1.2万〜2.5万行)の範囲に収まる見込み。
- 特に、A(キー)・B(履歴)・D(モードライン)は、旧の複数のファイルにまたがる。既存のコードを
  改造する必要があり、単純な追加では済まない。

## 4. 判断が要ること(BACKPORT.mdの「判断が要ること」の続き)

1. **キー体系(A)**: 旧のキー割り当てを保つか、C++に合わせるか。合わせると、旧版としての互換性が変わる。
2. **逆方向の差(2節)**: C++版へKEY節の設定、MIME復号などを足すか。旧の機能を残す意味で、
   C++の学習にもなる。
3. **順序**: 小さくて独立したもの(F、L、G)から、横断的なもの(A、B、D)を最後にするのが安全。
4. **検証**: 旧版(`U`)とC++版で、同じ操作をPTYで流して画面を比較する(`BACKPORT.md`の進め方2)。

## 参考

- `docs/legacy-cpp-map.md`: 旧の全関数とC++の対応。
- `old/tools/`: 関数の列挙・コメント検証・対応表の生成のスクリプト。
