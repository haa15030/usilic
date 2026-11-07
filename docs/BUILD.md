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
   入っている場合も、更新があれば入れる(`Label:`の後の名前を`-i`に渡す。下は2026-11-06のBig Surの例):
   ```
   softwareupdate --list
   ```
   ```
   sudo softwareupdate -i "Command Line Tools for Xcode-13.2"
   ```
   古いまま使うと、コンパイラ(clang)より新しいSDKが既定になっていることがあり、
   `make usilic`が`mac_cursor.mm`の中で止まる(下の「Command Line ToolsのclangとSDKの版の不一致」)。
2. [MacPorts](https://www.macports.org/install.php)(未導入の場合): そのMacのmacOSの版に合う`.pkg`を入れたら、
   **ターミナルを開き直す**(`/opt/local/bin`をPATHに足す`~/.zprofile`は、開いたままのウィンドウには反映されない)。
   2026-11-07から、macOSの版やCPU(Intel・Apple Silicon)によらずMacPortsに一本化した(ユーザー判断)。
   Homebrewは古いmacOS(Big Sur等)に対応しておらず、Intel Macには新しく入れられず(2026-11-06、インストーラが
   `Homebrew on macOS is only supported on Apple Silicon processors!`で止まる)、2027年9月以降はIntel Macでは
   Homebrew自体が動かなくなる予定(https://docs.brew.sh/Support-Tiers)。使えたり使えなかったりするより、
   どのMacでも同じ手順にする。最初の起動のインストール(groff等)もMacPortsで行う。
3. `pkg-config`、`SDL2`、`SDL2_ttf`:
   ```
   sudo port selfupdate
   ```
   ```
   sudo port install pkgconfig libsdl2 libsdl2_ttf
   ```
   既にHomebrewで`pkgconf sdl2 sdl2_ttf`を入れてあるMacは、そのままでもビルドできる(`pkg-config`が見つけた方を使う)。
4. 公開鍵の登録(リポジトリが非公開の間だけ必要)。鍵を作り(プロンプトに戻るまではEnterのみ)、
   公開鍵の1行をクリップボードへ入れる:
   ```
   ssh-keygen -t ed25519
   pbcopy < ~/.ssh/id_ed25519.pub
   ```
   ブラウザで`https://github.com/settings/keys`を開いてGitHubにサインインし、
   New SSH keyのKeyに貼って登録する(Titleは任意、Key typeはそのまま)。
   登録できたか確かめる(初回は`yes`、Hi ...! と出ればよい):
   ```
   ssh -T git@github.com
   ```
5. クローンしてビルド(`make usilic`で`build/usilic`ができ、`make test`で単体テスト一式(doctest)を実行する):
   ```
   mkdir -p ~/git
   cd ~/git
   git clone -b mpi-gui-fix git@github.com:haa15030/usilic.git
   cd usilic/src/cpp
   make usilic
   make test
   ```
   ※macOSのzshは、打ち込んだ行の`#`を注釈として扱わない(`interactivecomments`が既定で無効)。
   注釈付きの行を貼ると`zsh: no matches found`等になるので、このファイルの枠には注釈を書かない。
6. (任意)`/usr/local/bin/U`としてインストール(`INSTALL_NAME`が
   `usilic`ではなく`U`なのは、`src/install.sh`が入れる旧C版
   `/usr/local/bin/usilic`と衝突しないよう、日常的に試すための
   短い専用名としてユーザー自身が選んだもの):
   ```
   sudo make install
   ```

### Command Line ToolsのclangとSDKの版の不一致

Big Sur(mpr、2026-11-06)で、`make usilic`が次のエラーで止まった:
```
NSBundle.h:91:143: error: function does not return NSString
```
clangは12.0.5(Xcode 12.5の世代、合うSDKは11.3)なのに、既定のSDK(`xcrun --show-sdk-path`)は
`MacOSX12.1.sdk`だった。古いclangが12.1のSDKの`NS_FORMAT_ARGUMENT`を読めない。usilicのソースの問題ではない。
上の手順1.でCommand Line Toolsを更新すれば版が揃う。更新できない時は、
`ls /Library/Developer/CommandLineTools/SDKs/`でclangに合う版のSDKを探して指定する:
```
SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX11.3.sdk make usilic
```

### Intel Mac(Sequoia)固有の注意点

(Homebrewで入れたSDL2を使っていた頃の記録。MacPortsのSDL2でも`-ld_classic`はそのまま付く。)
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

## FreeBSD(2026-11-05、16.0-CURRENT・RTX 3050でインストールから通しで確認)

Makefileは GNU make の書き方なので、FreeBSD標準の`make`ではなく`gmake`を使う。コンパイラ(clang)とncursesはベースシステムにある。

順番は、インストーラ→パッケージ→GUI(X11・Xfce)→日本語入力→公開鍵の登録(非公開の間だけ)→ビルド。GUIを先にするのは、公開鍵の登録にFreeBSD上のfirefoxを使えるようにするため。

手順(1〜10)の表記の区別:

- <span style="color:#0097a7">設定項目名</span>
- **選択対象のハイライト表示**
- `自分で決めてよい値`
- *キー入力*
- [Enterで選択]
- 「表示文字列」

NVIDIAのGPUを使う例で書いている。AMD・Intelは「3. GPUのドライバ」の表を参照。

### 1. インストーラー

<span style="color:#0097a7">Welcome</span> -> [Install]

<span style="color:#0097a7">Keymap Selection</span> -> 「Japanese 106」を *Space* で選ぶ ->  
一番上の行 **>>> Continue with jp.kbd keymap** で [Select]  
(2行目の「->- Test default keymap」 は試し打ちだけ)

<span style="color:#0097a7">Set Hostname</span> -> `HOST` [OK]

<span style="color:#0097a7">Select Installation type</span> -> [Distribution Sets] (従来のtarball、portsとは無関係)

<span style="color:#0097a7">Distribution Select</span> -> 「src」を選ぶ -> [OK]

<span style="color:#0097a7">Network Installation</span> -> [OK]

<span style="color:#0097a7">Network Configuration</span> -> **rge0 RTL8125** (環境による) -> [Auto]

<span style="color:#0097a7">Partitioning</span> -> **Auto (ZFS) Guided Root-on-ZFS** -> [OK]

<span style="color:#0097a7">ZFS Configuration</span> -> 「zroot」 -> [OK] ->  
**>>> Install Proceed with Installation** [Select] ->  
**stripe Stripe - No Redundancy** [OK] ->  
**nda0 Patriot M.2 P300 128GB**（環境による）を選ぶ [OK] -> [YES]

<span style="color:#0097a7">Mirror Selection</span> -> **http://download.freebsd.org Main Site** -> [OK]

<span style="color:#0097a7">Fetching Distribution</span> ...

<span style="color:#0097a7">Archive Extraction</span> ...

<span style="color:#0097a7">Boot Configuration</span> -> [Yes]

<span style="color:#0097a7">Set root password</span> -> Password -> Repeat password -> [OK]

<span style="color:#0097a7">Time Zone Selector</span> -> **5  Asia** -> **20 Japan** -> [OK] ->  
<span style="color:#0097a7">Confirmation</span> -> [Yes]

<span style="color:#0097a7">Time & Date</span> -> [Set Date] (合ってれば [Skip]) ->  
[Set Time] (合ってれば [Skip])

<span style="color:#0097a7">System Configuration</span> -> [OK]

<span style="color:#0097a7">System Hardening</span> -> [OK]

<span style="color:#0097a7">FreeBSD firmware installation</span> ...

<span style="color:#0097a7">Add User Accounts</span> -> [Yes]

```screen
「FreeBSD Installer
========================
Add Users」
```

Username: `ABC` ->  
Full name: `DEF` ->  
Uid: (Leave empty for default): ->  
Login group [ABC]: ->  
Login group is ABC Invite ABC into other groups? [] : *wheel video operator*  
Login class [default]: ->  
Shell (sh csh tcsh nologin) [sh]: ->  
Home directory [/home/ABC] ->  
Home directory permissions (Leave empty for default): ->  
Enable ZFS encryption? (yes/no) [no] ->  
Use password-based authentication? [yes]: ->  
Use an empty password? (yes/no) [no]: ->  
Use a random password? (yes/no) [no]: ->  
Enter password: `GHI`  
Enter password again: `GHI`  
Lock out the account after creation? [no]: ->

```screen
「Username    : ABC
  Password    : *****
  Full Name   : DEF
  Uid         : 1001
  ZFS dataset : zroot/home/ABC
  Class       :
  Groups      : ABC wheel video operator
  Home        : /home/ABC
  Home Mode   :
  Shell       : /bin/sh
  Locked      : no」
```

OK? (yes/no) [yes]: ->  
adduser: INFO: Successfully added (ABC) to the user database.  
Add another user? (yes/no) [no]:

<span style="color:#0097a7">Final Configuration</span> ->  
**Finish Apply configuration and exit installer** -> [Select]

<span style="color:#0097a7">Manual Configuration</span> -> [No]

<span style="color:#0097a7">Complete</span> -> [Reboot]

※sshでログイン中にシステムを入れ直した場合は、ログインする側のマシンで *ssh-keygen -R \<IP\>* を実行する(ホスト鍵が変わるため)。

### 2.〜8.をスクリプトで進める(2026-11-07)

2.〜8.(9.の日本語入力の設定とusilicの起動の前まで)は、`tools/setup-freebsd.sh`で一度に実行できる。インストーラが終わって再起動し、rootでログインした所(sshでつないでもよい)で:

```key
pkg bootstrap -y && pkg install -y git
```

```key
su ABC -c 'git clone https://github.com/haa15030/usilic.git ~/git/usilic'
```

```key
sh ~ABC/git/usilic/tools/setup-freebsd.sh ABC
```

※`ABC`はインストーラで作ったユーザー名。GPUは`pciconf -l`で見分ける(NVIDIA・AMD・Intel)。違う時やAMDのHD 7000番台以前は、最後に`--gpu=nvidia`・`--gpu=amd`・`--gpu=radeon`・`--gpu=intel`を足す。コンソールのキーマップがjpならXのキーボードもjp106にし、`--swapcaps`を足すとCaps LockとCtrlを入れ替える。

※`kldload`がカーネルとの版のずれで失敗したら(下の注意点)、/usr/srcに合わせてportsからドライバをビルドし、`pkg lock`する。SDL2のportsからのビルドもあるので、時間がかかる。途中で止まったら、同じコマンドをもう一度実行すれば、済んだ所を飛ばして続きから進む。記録は`/var/log/usilic-setup.log`。

※最初の起動で入れるgroff・mg・texinfo・7-zip・w3mも、ここで入れる。

※終わったら *reboot* し、9.・10.へ進む。リポジトリが非公開の間は、*git clone* にGitHubへの鍵の登録が要る(Xfceがまだ無いので、注意点の「GUIを入れない時」と同じく、ユーザーで鍵を作って別のマシンのブラウザで登録し、`git@github.com:haa15030/usilic.git`から *git clone* する)。

以下は、スクリプトを使わずに手で進める場合の手順。

### 2. パッケージ

```screen
「FreeBSD/amd64 (HOST) (ttyv0)
 login: 」
```

※ここから先は、別のマシン(Mac等)からsshでつなぐと、この手順書のコマンドをブラウザからコピー&ペーストでき、打ち間違いを避けられる。作ったユーザーでログインして *ifconfig | grep broadcast* を実行し、「inet」の次の値(例: `192.168.1.23`)がこのマシンのIPアドレス。別のマシンの端末で *ssh `ABC`@\<IP\>* とつなぎ、以降はそこで続ける(sshdはインストーラのSystem Configurationで既定で有効。無効なら、このマシンのrootで *sysrc sshd_enable=YES; service sshd start*)。

```key
su -
```

*pkg bootstrap* -> *y*

```key
pkg update
```

*pkg install git gmake pkgconf sudo bash sdl2 sdl2_ttf xfce4-screenshooter-plugin firefox xdg-utils python3* -> *y*

※firefoxの最後に長いメッセージ(boost・gcc14・libjxl・firefoxの未対応機能等)が出るが、対応が要るものは無い。

*pkg install xorg xfce lightdm lightdm-gtk-greeter ja-font-ipa dejavu noto-jp fcitx5 fcitx5-configtool fcitx5-gtk3 fcitx5-qt6 ja-fcitx5-mozc3* -> *y*

```key
sysrc dbus_enable=YES lightdm_enable=YES
```

### 3. GPUのドライバ

| GPU | ドライバ | 起動時に読み込む | ファームウェア | 他に |
|---|---|---|---|---|
| NVIDIA | *pkg install nvidia-driver nvidia-xconfig* | *sysrc kld_list+=nvidia-modeset* | ドライバに同梱(インストーラには出ない) | *nvidia-xconfig* で`/etc/X11/xorg.conf`を作る |
| AMD(Radeon) | *pkg install drm-kmod* | *sysrc kld_list+=amdgpu*(HD 7000番台以前はradeonkms) | インストーラが自動で選ぶ(RX580ならgpu-firmware-amd-kmod-polaris10)。漏れたら *fwget* | Xorgの設定は不要(modesetting)。RX580で確認(2026-11-01) |
| Intel内蔵 | *pkg install drm-kmod* | *sysrc kld_list+=i915kms* | 同上(`gpu-firmware-intel-kmod-*`) | 同上。i5-7500(Kaby Lake)で確認(2026-11-07、下の※のビルドが要った) |

表の「ドライバ」と「起動時に読み込む」のコマンドを実行する(*sysrc* を忘れると再起動の後で画面が出ない)。

*kldload nvidia-modeset*(AMDはamdgpu、Intelはi915kms)を実行し、メッセージが何も出ないことを確かめる。

※AMD・Intelで *kldload* が失敗し、*dmesg | tail* に「depends on kernel - not available or version mismatch」と出たら、pkgのドライバが今のカーネルより新しいCURRENT向けにビルドされている(*sysctl -n kern.osreldate* がカーネル、*pkg info -d drm-kmod* の`drm-612-kmod-…1600030`等の末尾がドライバの版)。/usr/srcの版(*grep '^#define __FreeBSD_version' /usr/src/sys/sys/param.h*)がカーネルと同じなら、5.の *git clone* でportsを取ってから、*pkg info -d drm-kmod* に出たポートをビルドし直す(i5-7500では文字は細かくならなかったが、*ls /dev/dri* に`card0`があればよい)。

```key
cd /usr/ports/graphics/drm-612-kmod
```

```key
make install-missing-packages
```

```key
make BATCH=yes reinstall clean
```

```key
pkg lock -y drm-612-kmod
```

※ロックすると *pkg upgrade* でpkg版に戻らない。カーネルを更新した時は *pkg unlock -y drm-612-kmod* で解除し、ビルドし直す。

NVIDIAの場合は、続けて *nvidia-xconfig* を実行する。

### 4. sudoとキーボード

```key
echo '%wheel ALL=(ALL:ALL) ALL' > /usr/local/etc/sudoers.d/wheel
```

```key
chmod 440 /usr/local/etc/sudoers.d/wheel
```

```key
mkdir -p /usr/local/etc/X11/xorg.conf.d
```

```key
cat > /usr/local/etc/X11/xorg.conf.d/keyboard.conf <<'EOF'
Section "InputClass"
Identifier "keyboard"
MatchIsKeyboard "on"
Option "XkbModel" "jp106"
Option "XkbLayout" "jp"
Option "XkbOptions" "ctrl:swapcaps"
EndSection
EOF
```

※XkbOptionsの行はCaps LockとCtrlを入れ替える設定。要らなければ省く。

### 5. 日本語入力に対応したSDL2

パッケージのSDL2はfcitxに対応していないので、-gで日本語を入力できるようportsからビルドする。時間と数GBの空きが要る。

```key
git clone --depth 1 https://git.FreeBSD.org/ports.git /usr/ports
```

```key
echo 'devel_sdl20_SET+=DBUS FCITX' >> /etc/make.conf
```

```key
cd /usr/ports/devel/sdl20
```

```key
make install-missing-packages
```

```key
make BATCH=yes reinstall clean
```

*pkg query '%Ok=%Ov' sdl2 | grep -E 'DBUS|FCITX'*(DBUS=on、FCITX=on と出ることを確かめる)

```key
pkg lock -y sdl2
```

※ロックすると *pkg upgrade* でパッケージ版に戻らない。sdl2を更新する時は *pkg unlock -y sdl2* で解除し、この節の *cd* 以降をやり直す。

### 6. 日本語の環境を設定して再起動

rootからユーザーへ戻る。

```key
exit
```

```key
cat > ~/.xprofile <<'EOF'
export LANG=ja_JP.UTF-8
export GTK_IM_MODULE=fcitx
export QT_IM_MODULE=fcitx
export XMODIFIERS=@im=fcitx
export SDL_IM_MODULE=fcitx
EOF
```

```key
sudo reboot
```

### 7. Xfceの最初の設定と公開鍵の登録

xfceにログインし、画面最下行左から2番目の「\$\_」アイコン(Terminal)をクリックする。

NVIDIAの場合は、ウィンドウの合成表示を切る(有効のままだと画面が固まることがある)。

```key
xfconf-query -c xfwm4 -p /general/use_compositing -n -t bool -s false
```

鍵を作って、公開鍵を表示する。

*ssh-keygen -t ed25519* -> shプロンプトに戻るまでは *Enter* のみ

*cat ~/.ssh/id_ed25519.pub*(折り返し無しでコピーする)

firefoxで *https://github.com/settings/keys* へアクセスし、GitHubのアカウントでログインする。アカウントが無ければ作成する。

New SSH key をクリック

Title -> `任意`

Key type -> そのまま

Key -> 先ほどコピーした公開鍵をペースト

Authentication keys の欄に、Titleの公開鍵が登録されていることを確かめる。

※リポジトリが非公開の間だけ必要な手順。

### 8. usilicのビルド

Terminalで実行する。

*ssh -T git\@github.com* -> 初回は *yes*(Hi ...! と出ればよい)

```key
mkdir git
```

```key
cd git
```

```key
git clone -b mpi-gui-fix git@github.com:haa15030/usilic.git
```

```key
cd usilic/src/cpp
```

```key
gmake usilic && gmake install
```

```key
echo 'PATH=$HOME/.local/bin:$PATH; export PATH' >> ~/.profile
```

```key
. ~/.profile
```

```key
echo 'PATH=$HOME/.local/bin:$PATH; export PATH' >> ~/.bashrc
```

※~/.local/binは既定のPATHに無いので追加する。~/.profileはsshでつないだ時(ログインシェル)に読まれる。usilicのSHELL(機能限定SHELLも)はbashをログインシェルとしてではなく起動するので、~/.bashrcだけを読む(~/.bashrcに無いと、SHELLで`usilic .`等が見つからない。2026-11-07)。

※この時やログインの度に英語の小ネタが毎回違う内容で出るのは、既定の~/.profileの最後の fortune freebsd-tips(FreeBSDの使い方のヒントをランダムに1つ表示する)。エラーではない。要らなければその行の先頭に#。freebsd-tipsは英語だけで、日本語版は無い。

※bashをログインシェルにするなら *chsh -s /usr/local/bin/bash*。bashは~/.bash_profileがあれば~/.profileを読まないので、その時だけ次も実行する。

```key
echo 'PATH=$HOME/.local/bin:$PATH; export PATH' >> ~/.bash_profile
```

### 9. 日本語入力の設定

アプリケーション -> 設定 -> Fcitx5 設定

「入力メソッド」に Mozc があることを確かめる。無ければ追加して「適用」。一覧に出なければ「現在の言語のみ表示」のチェックを外す。 -> 「閉じる」

### 10. usilicの起動

アプリケーション -> アクセサリー -> usilic をクリックする。

※アクセサリーにusilicが無い時だけ、XfceのTerminalでパネルを再起動してから選ぶ(ログインした後で *gmake install* したusilicがメニューに出ないことがある。2026-11-07のi5-7500では再起動しなくても出ていた)。

```key
xfce4-panel -r
```

usilicが開いたら、「usilic-1.5b:~\$ sudo pkg install -y groff mg texinfo 7-zip w3m」で *Enter*

パスワードを入力する。

### 注意点

以下は上の表記の区別とは別で、`` ` `` はコマンドやファイル名を表す。

**インストールとパッケージ**

- `pkg install`の`firefox`の最後に長いメッセージ(boost・gcc14・libjxl・firefoxの未対応機能等)が出るが、対応が要るものは無い。
- `python3`はEPUBのリンクのEpub(narou2epub)・Pdf(url2pdf)に要る。そのほかの準備はHELPのnarou2epub・url2pdfの所。
- 最初の起動のインストールの案内で入らなかったものは、次の起動でもう一度だけ実行し、その後はモードラインに「見つかりません」と出すだけなので、手で入れる。
- `su`はwheelグループのユーザーだけ。インストーラで入れなかったユーザーは`pw groupmod wheel -m ユーザー名`。
- `noto-jp`は-cの表示に要る(無いと端末で豆腐になる字がある)。入れた後はログインし直す。

**GPUと画面**

- 再起動の前に`kldload`が何も言わずに戻ればカーネルと合っている(AMDはコンソールの文字が細かくなる。Intelはi5-7500では変わらなかった)。CURRENTで失敗するならportsからビルドする(3.の※、インストーラのDistribution Selectで`src`を入れておく)。2026-11-07、i5-7500ではpkgの`drm-612-kmod`が`1600030`向け、カーネルが`1600029`で読み込めず、Xが`/dev/dri/card0`を開けずに`Cannot run in framebuffer mode`で止まり、`login:`のままだった。
- このカーネルとドライバの版のずれは、GPUの種類によらない(AMDのamdgpuもIntelのi915kmsも同じ`drm-kmod`のパッケージで、ファームウェアの`gpu-firmware-*-kmod`、NVIDIAの`nvidia-driver`もカーネルのモジュール)。インストールした日のスナップショットとpkgの組み合わせで決まるので、RX580で一度動いても、別の日のスナップショットで入れ直した時や、カーネルはそのままで`pkg upgrade`した後の再起動で起きうる。3.の※のビルドの後で`pkg lock`しておけば、`pkg upgrade`でのずれは避けられる(ビルドしていなくても、動いているうちに`pkg lock -y drm-612-kmod`しておいてよい)。NVIDIAは`pkg lock -y nvidia-driver`。カーネルのモジュールが別のパッケージ(`nvidia-kmod`等、`pkg info -d nvidia-driver`で分かる)なら一緒にロックする(ドライバ本体とモジュールの版が違うと動かないので、片方だけにしない)。drm系はユーザーを`video`グループに入れる。
- ログインできず`login:`のままなら、`/var/log/lightdm/x-0.log`と`/var/log/Xorg.0.log`を見る。`keyboard.conf`の書き間違いや、`kld_list`の設定漏れでXが起動しない(2026-11-05に実際に両方)。
- NVIDIAでウィンドウの合成表示が有効だと、xfwm4が`glXBindTexImageEXT`で止まり、画面が固まることがある(RTX 3050・ドライバ595.112.07、2026-11-05。引き金は未特定)。マウスカーソルだけ動く状態になったら、sshから`pkill -9 xfwm4`で戻る。合成表示を切る`xfconf-query`はsshからでは届かないので、XfceのTerminalで実行する。
- 端末に重ねる-gはmacOSだけ(Linux・FreeBSDは重ねない、ユーザー判断2026-10-31)。

**日本語入力とSDL2**

- パッケージのSDL2は`DBUS`・`FCITX`が`off`で、-gでは窓は開いても日本語が入らない(`pkg query '%Ok=%Ov' sdl2`で分かる)。ビルドしても`off`のままなら、古い設定が残っているので`make rmconfig`と`make clean`をしてからやり直す。
- ビルドの前に`pkg lock`していると`make reinstall`が止まる。`pkg unlock -y sdl2`で解除する。

**日本語とキーボード**

- Xfceのメニュー等を日本語にする`LANG`は`~/.xprofile`に書く(lightdmの`/usr/local/etc/lightdm/Xsession`が読む、2026-11-01に確認)。`~/.login_conf`やログインクラスで設定するとコンソール(vt)のログインまで日本語になり、vtのフォントに漢字が無いので化ける。
- `keyboard.conf`を使わずXfceだけで設定するなら、設定→キーボード→レイアウトで「システムのデフォルトを使用」を外し、モデルを`Japanese 106-key`、レイアウトの追加で**▶を開かずに親の`Japanese`の行**を選ぶ(▶の下のKana・Macintosh・OADG 109A・Dvorakは標準以外の変種。Kanaを選ぶと`[`が`]`になる等、2026-11-01に実際にはまった)。ただし、これだとログイン画面はjp106にならない。
- Caps LockをCtrlにするだけなら`XkbOptions`を`ctrl:nocaps`にする。すぐ試すならXfceの端末で`setxkbmap -model jp106 -layout jp -option ctrl:swapcaps`(戻すのは`setxkbmap -option`)。Xfceのレイアウトで「システムのデフォルトを使用」を外していると、ログインの時にXfceが上書きして`XkbOptions`が消えることがあるので、`keyboard.conf`を使うならXfce側はデフォルトのまま。
- コンソール(vt)はXとは別で、`sysrc keymap=jp.capsctrl.kbd`して再起動(16.0-CURRENTの`/usr/share/vt/keymaps/`に`jp.kbd`と並んであることを2026-11-01に確認。名前のとおりCaps LockをCtrlにするもので、入れ替えではない)。

**公開鍵とPATH**

- `https://`のURLでcloneすると鍵は使われず、非公開の間はトークンを聞かれる(公開後は`https://github.com/haa15030/usilic.git`でよく、7の鍵の登録も要らない)。トークンの作り方は下の「ソースを手元で変更する」の4。
- GUIを入れない時は、別のマシンのブラウザで鍵を登録する。Macなら`ssh ユーザー名@<IP> cat .ssh/id_ed25519.pub | pbcopy`で1行がクリップボードに入る(FreeBSDのsshdが動いていなければ、rootで`sysrc sshd_enable=YES; service sshd start`)。GUIがあってもsshで作業中なら、鍵の作成・表示はsshで済み、登録もMacのブラウザでできる。

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
   `-g`の和文のゴシック体は、Windows側のメイリオ(`/mnt/c/Windows/Fonts/meiryo.ttc`)があればそれを
   使う(無ければNoto Sans CJK)。^Ofのフォント一覧にも`/mnt/c/Windows/Fonts`のフォントが出る。
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
8. **文字の大きさ**: Git BashもMSYS2の端末もmintty。usilic側ではなく端末で変える。
   その場では`Ctrl`+`+`/`-`(`Ctrl`+ホイール、`Ctrl`+`0`で戻す。閉じると元に戻る)。
   ずっと変えるには、ウィンドウを右クリック→Options→Text→Fontで選んでSave
   (`~/.minttyrc`に`FontHeight=14`のように入る)。Git Bash(`C:\Users\<名前>`)と
   MSYS2(`C:\msys64\home\<名前>`)はホームが別なので、それぞれで設定する。

## Windows(ネイティブ、MinGW-w64)

2026-10-15追加(`.steering/20261015-windows-native-gui/`)。POSIXの互換層(MSYS2の`msys-2.0.dll`)を使わない、
Windowsのふつうの実行ファイルで、2つある(2026-11-02、`.steering/20261102-windows-console-cui/`)。
`usilic.exe`はSDLの窓(-g、アイコンから起動してもコンソールの窓が出ない)、`usilic-c.exe`はコマンドプロンプト・
PowerShell・Windows Terminalの中のCUI(-gで窓も開ける)。CUIは**Windows 10 1809以降**(VT処理)、SHELLはConPTY。
親子のusilicの受け渡し(KEYの子から親のキー実行等)は使えない。(ls)の一覧はWindowsに`ls`が無いので、usilicの中で`ls -alF`と同じ形の行を作る
(`src/cpp/src/native_ls.cpp`)。外部コマンドは`CreateProcessW`で起動する(`src/cpp/src/child_process_win.cpp`)。
ファイル名はマニフェストの`activeCodePage`でUTF-8にしているので、**Windows 10 1903以降**が要る。

### Macからクロスビルドする

1. **ツール**: `sudo port install mingw-w64`(2026-11-07にMacPortsへ一本化。以前は`brew install mingw-w64`で作っていて、MacPortsでのクロスビルドは未確認)。
2. **SDL2・SDL2_ttf**: GitHubのlibsdl-orgのリリースから`SDL2-devel-<版>-mingw.tar.gz`・
   `SDL2_ttf-devel-<版>-mingw.tar.gz`を落とし、中の`x86_64-w64-mingw32/`の`include`・`lib`・`bin`を
   `~/.local/mingw/`へコピーする(別の場所なら`make`に`WIN_SDL_DIR=...`)。
3. **ビルド**:
   ```
   cd src/cpp
   make WINDOWS=1 -j8 usilic     # build-win/usilic.exe・usilic-c.exe(設定はwindows.mk)
   make WINDOWS=1 dist           # build-win/usilic/ と build-win/usilic-win64.zip
   ```
   `dist`は、strip済みの`usilic.exe`・`usilic-c.exe`・`SDL2.dll`・`SDL2_ttf.dll`と、`lib/usilic/`(usilicrc・HELP・
   HELP.notes、`~/.local/lib/usilic/HELP.epub`があればそれも)をまとめる。

### Windowsで使う

1. `usilic-win64.zip`を好きな所(例えば`C:\Users\<名前>\usilic\`)に展開する。
2. `usilic.exe`をダブルクリックするか、コマンドプロンプトから`usilic.exe [ディレクトリ|ファイル]`。
   引数が無ければカレントディレクトリ(ダブルクリックならexeの場所)の(ls)。端末の中で使うなら`usilic-c.exe`
   (`usilic.exe -c`はそれを案内して終わる)。^Zのサスペンド、CUIの^J^F等の-g専用の機能は使えない。
3. 設定・履歴は`%USERPROFILE%\.usilic\`(HOMEが無ければUSERPROFILEを使う)、HELP・既定のusilicrcは
   exeの隣の`lib\usilic\`。フォントは`C:\Windows\Fonts`のMSゴシック(無ければConsolas)、和文の
   ゴシック体はメイリオ、明朝体は游明朝。
4. 窓が出ない時はメッセージボックスで理由を出す(フォントが無い等)。`SDL2.dll`が無いとWindows自身が
   起動前に知らせる。

### Windows上でビルドする

MSYS2の**UCRT64**の端末で`pacman -S --needed mingw-w64-ucrt-x86_64-gcc make zip`、SDL2・SDL2_ttfは
上と同じ配布物を`WIN_SDL_DIR`に置き、`make WINDOWS=1 CXX=g++ WINDRES=windres STRIP=strip usilic`。

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

## ソースを手元で変更する(公開時の方針、2026-11-01)

**方針**: 公開後は、`src/cpp/tools/narou2epub.py`と`src/cpp/tools/narou2epub-assets/`(「小説家になろう」用の
スクリプト。NarouToEpub3を改変したもので、元のApache-2.0に従う)以外は、自由に変更してよい。
作者はあらゆる環境・使い方を十分にテストできていないので、不具合が出たら、cloneした手元のソースを
AI(Claude Code等)に見てもらって直す、という使い方を想定している。narou2epubを変更する場合は
Apache-2.0の条件(LICENSEを残す・変更した旨を記す。`narou2epub.py`の冒頭の変更点の一覧に追記する)に従う。

### 1. ソースを取ってくる(アカウント・鍵は不要)

公開リポジトリを読むだけなら、GitHubのアカウントもSSHの鍵も要らない。
```
git clone https://github.com/haa15030/usilic.git
cd usilic
git switch -c my-fix        # 自分の変更用のブランチ(元のブランチと分けておくと更新を取り込みやすい)
```
(冒頭の「ブランチに注意」も参照。)

### 2. AIに見てもらって直す

- cloneしたディレクトリで起動する(Claude Codeなら`claude`)。症状・再現の手順・OS(と`usilic -v`等の版)を伝える。
- 手がかりになるもの: このファイル(ビルド)、`docs/CHANGES.md`(機能ごと・モードごとの経緯。項目番号で引ける)、
  `src/HELP_.txt`等のHELPの原稿。
- **リポジトリ直下の`CLAUDE.md`と`.claude/`は作者の作業用の取り決め**で、Claude Codeが自動で読み込む。特に
  `.claude/settings.json`のStopフック(`.claude/hooks/history_sync.py`)は、会話を`docs/HISTORY.md`へ
  追記し続ける。要らなければ`.claude/settings.json`の`"hooks"`を消し、`CLAUDE.md`も自分の環境に合わせて
  書き換えてよい。
- 直したら、自分のブランチでビルドし直す(`make usilic`・`make install`。FreeBSDでは`gmake`)。

### 3. 作者側の更新を取り込む

```
git stash                   # コミットしていない変更があれば退避(コミット済みなら不要)
git fetch origin
git rebase origin/<元のブランチ>   # 自分の変更を最新の上に載せ直す。ぶつかったらAIに解消を頼んでもよい
git stash pop
```

### 4. 自分の変更をGitHubに置く・作者に送る(ここからアカウントが要る)

| やりたいこと | GitHubのアカウント | 鍵・認証 |
|---|---|---|
| 公開リポジトリを`https://`でcloneする・更新を取り込む | 不要 | 不要 |
| 自分の変更を自分のGitHubに置く(fork→push) | 必要 | SSHの鍵、または`gh auth login` |
| 作者に変更を送る(Pull Request)・不具合を報告する(Issue) | 必要 | (ブラウザでサインイン) |

- SSHの鍵はアカウントに登録するもので、`https://github.com/settings/keys`はサインインしないと開けない。
- 鍵は使うマシンで`ssh-keygen -t ed25519`で作り、`~/.ssh/id_ed25519.pub`の1行を上のページのNew SSH keyに貼る。
  そのマシンにまだブラウザが無ければ(FreeBSDを入れた直後等)、sshでつないだ別のマシンの端末でこの1行を
  コピーし、そちらのブラウザ(サインイン済み)で登録してもよい。
- `gh auth login`(GitHub CLI)なら鍵を作らずに済み、表示されたコードを別のマシンのブラウザで入力しても通る。
- **鍵とURLの組み合わせに注意**: SSHの鍵が使われるのは`git@github.com:haa15030/usilic.git`の形のURLだけ。
  `https://github.com/...`で非公開のリポジトリをclone(や自分のforkへpush)すると`Username for 'https://github.com':`
  と聞かれ、パスワードの欄には**GitHubのパスワードではなくトークン**を入れる(パスワードは受け付けない)。
  鍵を登録したなら、URLを`git@github.com:`の形にすればトークンは要らない(2026-11-01、FreeBSDで鍵を登録した後に
  `https://`でcloneしてこれに当たった。既にcloneしたものは`git remote set-url origin git@github.com:…`で切り替えられる)。
- **トークンの作り方**(`https://`のまま使う場合): GitHubにサインイン→右上のアイコン→Settings→
  (左の一番下)Developer settings→Personal access tokens→**Fine-grained tokens**→Generate new token。
  Token name(何でもよい)、Expiration(有効期限)、Repository accessは**Only select repositories**で`usilic`を選び、
  Permissions→Repository permissionsの**Contents**を`Read-only`(cloneだけ)か`Read and write`(pushもする)に。
  Generate tokenで表示される`github_pat_…`は**その時しか表示されない**のでコピーしておく。
  cloneの`Username:`に自分のアカウント名、`Password:`にこのトークンを貼る。
  毎回聞かれないようにするには`git config --global credential.helper store`(`~/.git-credentials`に平文で保存される。
  気になるなら`cache`で一定時間だけ覚えさせる)。`gh auth login`を済ませておけば、ghがこの役目も引き受ける。
- 手順: GitHubで作者のリポジトリをFork → `git remote add mine git@github.com:<自分>/usilic.git` →
  `git push mine my-fix` → GitHubのページからPull Request。
- 作者以外が1つのリポジトリだけを読む機械用には、リポジトリの所有者が登録するDeploy key(Settings→Deploy keys)
  もある(非公開の間に、作者が自分の別マシンへ取ってくる時等)。

## ビルドターゲット早見表

| コマンド | 内容 |
|---|---|
| `make usilic` | `build/usilic`(実行可能バイナリ)を生成 |
| `make app` | `build/usilic.app`(Finderから起動できる`.app`バンドル、macOS用)を生成 |
| `make test` | `build/run_tests`を生成し、そのまま実行(doctest) |
| `make clean` | `build/`を削除 |
| `make WINDOWS=1 usilic` | `build-win/usilic.exe`(Windowsのネイティブ、-gだけ。MinGW-w64、windows.mk) |
| `make WINDOWS=1 dist` | `build-win/usilic-win64.zip`(exe・SDLのDLL・HELP等) |
| `sudo make install` | `build/usilic`を`/usr/local/bin/U`へインストール |

`make`(ターゲット省略)は`Makefile`内で最初に書かれている`test`が
実行される(`usilic`ではない)ので注意。
