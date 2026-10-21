# usilic.old(旧版)

2000 年の tcb 1.5b-1(リポジトリの初回インポート `47e2a1f`)に、**クラッシュ(segv・ハング・メモリの破壊)の修正だけ**を当てたものです。
名前を tcb から usilic に変えた以外、機能は当時のままです。
C++ 版の usilic(新)と並べて入れられるよう、名前と置き場所を `usilic.old` にしてあります。

当時の修正の経緯は、各コミットのメッセージ(`git log -- legacy/`)と、リポジトリの `docs/CHANGES.md` の項目 991 以降を参照してください。
機能を足した旧版は `old/` にあります(こちらとは別物です)。

## ビルドとインストール

```
cd legacy
make            # config.sh が src/Makefile・src/config.h を作り、src/usilic をビルド
make install    # ~/.local へ入れる
make clean
```

| もの | 場所 |
|------|------|
| 実行ファイル | `~/.local/bin/usilic.old` |
| 設定ファイル・HELP | `~/.local/lib/usilic.old/`(`usilic.oldrc`・`HELP`・`HELP.jp`) |
| man ページ | `~/.local/share/man/man1/usilic.old.1.gz` |
| データ | `~/.usilic.old/`(`history`・`error` 等。`usilic.oldrc` を置けば、`~/.local/lib/usilic.old/` のものより先に読む) |
| 作業用 | `/tmp/.usilic.old/` |
| 環境変数 | `USILIC_OLD`(SHELL の中か)・`USILIC_OLDCHARSET`(文字コード)・`USILIC_OLDVT` |

Linux(gcc)と macOS(clang、Homebrew は不要)でビルドできます。
画面の制御に ncurses を使います(Ubuntu では `sudo apt install libncurses-dev`)。

## 当てた修正

- conf_arc() の 64bit ポインタの切り詰め(ARCHIVE の配列)による segv。
- 環境変数 SHELL が無い時の NULL の参照による segv。
- ARC モードで、アーカイブの中のポインタを 32bit の整数に入れていたことによる segv(`PP.tell`、`read_arc()` の `FILE*`)。
- 重なった範囲の strcpy()(未定義動作)による起動時のハング・文字化け・クラッシュ(conf_grep()・get_str() ほか全 23 か所以上)。
- macOS の tar の一覧の読み取りの無限ループ。
- SHELL モードの文字列のバッファの溢れ、改行の無限ループ(GNOME Terminal 等の PROMPT_COMMAND)、`usilic <dir/file>` の segv、less 等のフリーズ。
- KEY モードの segv、execvp() に失敗した子の暴走、pipe() の呼び忘れ、fileno(NULL)。
- macOS で SHELL のキーを押すと全体が固まるのを、エラーで戻るようにした(下記)。

当てていないもの(クラッシュではないもの): UTF-8 への対応、表示の化け、macOS の tar の `._` ファイルの取り違え、CP/MV/RM のカーソル位置、ファイル名の空白の扱い等。

## 知っておくこと

- **文字コード**: 当時の既定は EUC です。今の UTF-8 の端末では日本語が化けます。
- **macOS**: SHELL モード(`^J.` 等)は使えません。
  macOS には古い BSD 式の擬似端末(`/dev/ptyp0` 等)のデバイスが残っていますが、実際には開けません。
  SHELL のキーは「Failed to open /dev/pty..」と出て戻ります。
- **macOS の tar**: macOS で作ったアーカイブ(`._` で始まる拡張属性の入れ物を含む)では、中のファイルを開くと別のものが出ることがあります。

## Linux で SHELL モードを使うには(カーネルのリメーク)

SHELL モードは、当時の BSD 式の擬似端末(`/dev/ptyp0`〜`/dev/ptysf` と `/dev/ttyp0`〜)を使います。
今の Linux のカーネル(Ubuntu 等)はこれを無効にしてビルドされているので、`CONFIG_LEGACY_PTYS` を有効にしたカーネルが必要です。
有効か無効かは次で分かります。

```
grep LEGACY_PTY /boot/config-$(uname -r)    # "# CONFIG_LEGACY_PTYS is not set" なら無効
ls /dev/ptyp0                               # あれば有効
```

### Ubuntu での手順の例

カーネルのビルドには数十 GB の空きと時間が要ります。
入れ替えたカーネルで起動しなくなった時に備え、GRUB のメニューから元のカーネルを選べることを確かめてから行ってください。

```
sudo apt install build-essential libncurses-dev bison flex libssl-dev libelf-dev dwarves bc rsync fakeroot
apt source linux-image-unsigned-$(uname -r)     # deb-src を有効にしておく(/etc/apt/sources.list.d/)
cd linux-*/
cp /boot/config-$(uname -r) .config
scripts/config --enable LEGACY_PTYS --set-val LEGACY_PTY_COUNT 256
scripts/config --disable SYSTEM_TRUSTED_KEYS --disable SYSTEM_REVOCATION_KEYS
make olddefconfig
make -j$(nproc) bindeb-pkg LOCALVERSION=-legacypty
sudo dpkg -i ../linux-image-*-legacypty*.deb ../linux-headers-*-legacypty*.deb
sudo reboot
```

起動したら `ls -l /dev/ptyp0 /dev/ttyp0` でデバイスがあるかを確かめます。
一般の利用者が読み書きできない時は、`sudo chmod 666 /dev/ptyp* /dev/ttyp*`(再起動で戻ります。常にそうするなら udev の規則を書きます)。

### WSL2

WSL2 は Microsoft のカーネル(https://github.com/microsoft/WSL2-Linux-Kernel)を使います。
同じく `CONFIG_LEGACY_PTYS=y` でビルドし、`%UserProfile%\.wslconfig` の `[wsl2]` に `kernel=C:\\path\\to\\bzImage` を書いて `wsl --shutdown` します。
