# usilic (C++版) ビルド手順

`src/cpp/`以下のC++移植版usilicを、何も入っていないまっさらな
macOS/Linuxマシンから実際にビルドできるようになるまでの手順。

「なぜこれが要るのか」を各所に書いてあるのは、パッケージの
過不足で無反応/謎のリンクエラーになったときに原因を追いやすく
するため(2026-08-24、Intel Mac実機での`SDL2_ttf`リンクエラー
[docs/CHANGES.md項目419]の調査を機に整備)。

**ブランチに注意(2026-10-05追記)**: `git clone`で取り出されるのは既定のブランチ`master`
(2026-08-29の版)で、それ以後の作業は`mpi-gui-fix`ブランチにある(まだ`master`へマージして
いない)。cloneした後、ビルドの前に必ず
```
git checkout mpi-gui-fix
git pull
```
とする(`git branch`で今のブランチを確かめられる)。2026-10-05、WSL2で`master`のままビルドした
ため古い版(^Hが効かず^TがTUTORIAL)を試していた、という実例があった。

## 必要なもの(共通)

- C++17対応コンパイラ(`clang++`または`g++`)と`make`
- `git`
- `ncurses`のヘッダ/ライブラリ(`terminal_display.cpp`が
  `setupterm`/`tputs`/`tgoto`等のterminfo APIを使用)
- `pkg-config`
- `SDL2`と`SDL2_ttf`のヘッダ/ライブラリ

`SDL2`/`SDL2_ttf`は本来GUIモード(`-g`、`GraphicalDisplay`)専用の
はずだが、`src/cpp/Makefile`は`src/`以下の`.cpp`を`make usilic`/
`make test`どちらでも無条件に全部ビルドする作りで、
`graphical_display.cpp`自身も`<SDL2/SDL.h>`/`<SDL2/SDL_ttf.h>`を
`#ifdef`無しで無条件includeしている。**つまりCUIしか使わない場合
でも、コンパイルを通すためだけにSDL2/SDL2_ttfのヘッダ/ライブラリが
必須になる**(GUIウィンドウを一度も開かない場合でも)。

## macOS

1. Xcode Command Line Tools(`clang++`/`make`/`git`、および
   `ncurses`のヘッダ/ライブラリもSDKに同梱):
   ```
   xcode-select --install
   ```
2. [Homebrew](https://brew.sh)(未導入の場合):
   ```
   /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
   ```
3. `pkg-config`(`pkgconf`パッケージが提供)、`SDL2`、`SDL2_ttf`:
   ```
   brew install pkgconf sdl2 sdl2_ttf
   ```
4. クローンしてビルド:
   ```
   git clone https://github.com/haa15030/usilic.git
   cd usilic/src/cpp
   make usilic     # build/usilic ができる
   make test       # 単体テスト一式(doctest)を実行
   ```
5. (任意)`/usr/local/bin/U`としてインストール(`INSTALL_NAME`が
   `usilic`ではなく`U`なのは、`src/install.sh`が入れる旧C版
   `/usr/local/bin/usilic`と衝突しないよう、日常的に試すための
   短い専用名としてユーザー自身が選んだもの):
   ```
   sudo make install
   ```

### Intel Mac(Sequoia)固有の注意点

Xcode 15以降のデフォルトリンカ(通称ld-prime)が、Homebrew製の
一部dylib構成(`sdl2_ttf`が`sdl2`ではなく`sdl2-compat`を参照する
構成など)で「ライブラリもリンクコマンドも正しいのにシンボルが
見つからない」という既知の非互換を起こすことがある
(docs/CHANGES.md項目419で実際に発生・特定)。対策として
`Makefile`のDarwin向け`LDLIBS`に`-Wl,-ld_classic`(旧リンカの
強制指定)を組み込み済みなので、**上記の手順通りに`make`すれば
追加の対応は不要**。万一同種のリンクエラーが再発した場合は、
`pkg-config --cflags --libs sdl2 SDL2_ttf`の出力・`file`/
`lipo -info`でのアーキテクチャ・`nm -gU`でのシンボルの実
エクスポート状況、の3点を順に確認するとよい(docs/CHANGES.md
項目419の調査ログに実例あり)。

### Apple Silicon + Homebrewが`/opt/homebrew`の場合

`/opt/homebrew/lib`はデフォルトのリンカ検索パスに含まれないが、
`pkg-config`経由でSDL2/SDL2_ttfの`-I`/`-L`は自動的に解決される
ため、追加の`CFLAGS`/`LDFLAGS`指定は不要(2026-07-16に検証済み、
CLAUDE.mdの経緯記録も参照)。

## Linux (Ubuntu/Debian系)

```
sudo apt-get update
sudo apt-get install -y build-essential libncurses-dev pkg-config \
    libsdl2-dev libsdl2-ttf-dev git

git clone https://github.com/haa15030/usilic.git
cd usilic/src/cpp
make usilic
make test
```

## Windows(WSL2)

2026-10-05追加(`.steering/20261005-windows-wsl2/`)。WSL2の中身は普通のUbuntuなので、上の
Linuxの手順でビルドする。違いは、外で開く・クリップボードがWindows側につながること。

1. **WSL2を入れる**(初回のみ): 管理者のPowerShellで`wsl --install`(既定でUbuntuが入る)→再起動→
   スタートメニューの「Ubuntu」を開いてユーザー名・パスワードを決める。
   Windows Terminalから「Ubuntu」のタブで使うのがお勧め。
2. **パッケージ**(Ubuntuの中で):
   ```
   sudo apt-get update
   sudo apt-get install -y build-essential libncurses-dev pkg-config \
       libsdl2-dev libsdl2-ttf-dev git fonts-noto-cjk unzip
   ```
   `fonts-noto-cjk`は`-g`の日本語の表示用、`unzip`はEPUBと.zip(ARC)用(WSLのUbuntuには最初は
   入っていない。どちらも無ければ最初の起動時の案内にも出る)。
3. **ビルドとインストール**: リポジトリは非公開なので、`gh auth login`(GitHub CLI、
   `sudo apt-get install gh`)かSSHの鍵を用意してから`git clone`し、
   ```
   cd usilic
   git checkout mpi-gui-fix   # 冒頭の「ブランチに注意」参照
   cd src/cpp
   make usilic
   make install          # ~/.local/bin/usilic、~/.local/lib/usilic/
   ```
   `~/.local/bin`がPATHに無ければ`~/.profile`に`export PATH="$HOME/.local/bin:$PATH"`を足す。
   リポジトリはWSLの中(`~/`の下)に置く。`/mnt/c/...`(Windows側のドライブ)の下はファイルの
   読み書きが遅く、パーミッションも正しく扱えない。
4. **-g(GUI)**: WSLg(Windows 11、またはWindows 10の新しい版のWSL)があれば`usilic -g`で
   Windowsのデスクトップにウィンドウが出る(X/Waylandの設定は要らない)。
5. **Windows側とのつながり**(usilicがWSLの中かを実行時に判定する、`src/cpp/src/platform.hpp`):
   - 動画・url2pdfで作ったPDF・EPUBのWebのリンク(Open)は、`explorer.exe`でWindowsの既定の
     アプリ(ブラウザ・動画プレーヤー等)で開く。WSLの中のファイルは`wslpath -w`で
     `\\wsl.localhost\Ubuntu\...`の形にして渡す。
   - COPY(`^[w`/`^[p`)とEPUBのWebのリンクのCopyは、`clip.exe`でWindowsのクリップボードへ入る
     (日本語はUTF-16に変換して渡す)。
   - Windows側のファイルは`/mnt/c/Users/<名前>/...`で見える。

## Windows(Git Bash / MSYS2)

2026-10-05追加(`.steering/20261005-windows-gitbash/`)。Git Bashの土台はMSYS2(Cygwin系)の実行環境
なので、MSYS2でビルドしたusilic.exeがGit Bashでも動く見込み。Git for Windowsにはコンパイラが
無いので、ビルドはMSYS2で行う。**-g(GUI)は使えない**(MSYS2のMSYS環境にSDL2が無いため。
SDL2が見つからないとMakefileが自動で`NO_GUI=1`になり、`src/graphical_display_stub.cpp`で
ビルドする)。LOCATEも使えない(locate/updatedbが無い)。

1. **MSYS2を入れる**: https://www.msys2.org/ のインストーラで`C:\msys64`へ。スタートメニューの
   **「MSYS2 MSYS」**の端末を開く(「MINGW64」「UCRT64」ではない。それらはWindowsネイティブの
   exeを作る環境で、usilicが使うforkや擬似端末が無い)。
2. **パッケージ**(MSYS2 MSYSの端末で):
   ```
   pacman -Syu                     # 初回は端末を閉じて開き直し、もう一度
   pacman -S --needed gcc make ncurses-devel pkgconf git unzip
   ```
3. **ビルド**:
   ```
   git clone ...(WSL2の節と同じく、非公開なので認証を用意する)
   cd usilic
   git checkout mpi-gui-fix   # 冒頭の「ブランチに注意」参照
   cd src/cpp
   make usilic                     # build/usilic.exe
   ```
4. **MSYS2の端末で使う**: `make install`(`~/.local/bin/usilic`、MSYS2のホームは
   `C:\msys64\home\<名前>`)。`~/.bashrc`に`export PATH="$HOME/.local/bin:$PATH"`。
5. **Git Bashで使う**: Git Bashのホームは`C:\Users\<名前>`(MSYS2からは`/c/Users/<名前>`)なので、
   ```
   make install PREFIX=/c/Users/<名前>/.local
   ```
   で入れ、Git Bashの`~/.bashrc`に`export PATH="$HOME/.local/bin:$PATH"`。
   libstdc++/libgccは静的にリンクしてある。それ以外に足りないDLLがあれば、MSYS2の端末で
   `ldd build/usilic.exe`を見て、`/usr/bin/msys-*.dll`のうちGit Bashの`/usr/bin`に無いもの
   (例えば`msys-ncursesw6.dll`)を`/c/Users/<名前>/.local/bin/`へコピーする。
   `msys-2.0.dll`はGit Bashのものを使う(コピーしない)。版の違いで動かなければ、4.の
   MSYS2の端末で使う。
6. **Windows側とのつながり**(`src/cpp/src/platform.hpp`のisMsys()): 動画・url2pdfのPDF・
   EPUBのWebのリンク(Open)は`explorer.exe`でWindowsの既定のアプリで開き(パスは`cygpath -w`)、
   COPYとEPUBのリンクのCopyは`/dev/clipboard`でWindowsのクリップボードへ入る。
7. **最初の起動時の案内**: MSYS2の端末(`pacman`がある)では、groff・texinfo・p7zipのうち
   無いものを`pacman -S --needed`で入れるコマンドを置く。Git Bashでは案内しない。

## Docker(`.devcontainer/`)

このリポジトリの`.devcontainer/Dockerfile`(Ubuntu 26.04ベース)
には上記Linux手順のパッケージが最初から含まれている
(2026-08-24、`libsdl2-dev`/`libsdl2-ttf-dev`/`pkg-config`を追加
[docs/CHANGES.md項目420] -- それ以前はSDL2関連が抜けており、
`make usilic`がヘッダ不足でコンパイルすら通らない状態だった)。
実際に`docker build`したイメージへ、ホストのbind mountを経由せず
リポジトリを`git clone`し直した上で`make usilic`/`make test`が
成功することを検証済み。

**注意(重要)**: ホストのMac上のリポジトリをこのDockerコンテナへ
bind mountして使う場合、コンテナ内で`make`/`make clean`を実行
すると、共有された`build/`ディレクトリにLinux版バイナリが
書き込まれ、Mac側の`build/usilic`(Mach-O)を壊す(あるいは
`rm -rf build`でMac側ごと削除する)。コンテナ内でのビルド確認は
リポジトリをコンテナ内の別パス(例: `/tmp/usilic`)へコピーまたは
`git clone`し直した上で行うこと。Mac側で確認済みのバイナリを
そのまま使い続けたい場合は、コンテナ内で`make`系コマンドを一切
実行しないこと。

## macOS: Finderからアイコンクリックで起動する(`.app`バンドル)

```
cd usilic/src/cpp
make app        # build/usilic.app ができる
open build/usilic.app   # 動作確認(実際にウィンドウが開く)
```

`usilic`は引数無し起動だとmacOSでは元々GUIモード(SDL2の独立
ウィンドウ、ターミナル不要)がデフォルトになる設計になっている
ため、`make usilic`が作る`build/usilic`をそのまま`.app`バンドルへ
包むだけで、Finderからのダブルクリック起動に対応できる
(2026-08-24、docs/CHANGES.md項目421)。GUI側の追加実装は不要
だった。

`build/usilic.app`をFinderで`/Applications`や`Dock`へドラッグ
すれば、以後は通常のアプリと同じ感覚でアイコンクリック起動できる。
ターミナルからの`U`起動(`-c`でCUIも選択可)は影響を受けない。

**署名について**: `make app`は末尾で`.app`バンドル全体を
ad-hoc署名し直す(`codesign --force --deep -s -`、単体実行
ファイル用の署名をバンドル構造にそのまま流用すると
`spctl -a`がrejectedになるため必須)。有償のDeveloper ID証明書
による署名・公証(notarization)は行っていないため、他のMacへ
コピーした場合や`com.apple.quarantine`属性が付いた場合は、
初回起動時にGatekeeperの確認(右クリック→「開く」)が必要になる
ことがある。ローカルでビルドしたものをそのまま使う分には
この確認は出ない(quarantine属性が付かないため)。

アイコンは未設定(Finderの汎用実行ファイルアイコンのまま)。
`.icns`を`src/cpp/packaging/`へ追加し`Info.plist`の
`CFBundleIconFile`を設定すれば、以後の`make app`で自動的に
反映される。

## ビルドターゲット早見表

| コマンド | 内容 |
|---|---|
| `make usilic` | `build/usilic`(実行可能バイナリ)を生成 |
| `make app` | `build/usilic.app`(Finderから起動できる`.app`バンドル、macOS用)を生成 |
| `make test` | `build/run_tests`を生成し、そのまま実行(doctest) |
| `make clean` | `build/`を削除 |
| `sudo make install` | `build/usilic`を`/usr/local/bin/U`へインストール |

`make`(ターゲット省略)は`Makefile`内で最初に書かれている`test`が
実行される(`usilic`ではない)ので注意。
