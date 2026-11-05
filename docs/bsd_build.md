# FreeBSDへのインストールとusilicのビルド

表記の区別:

- **太字**: 画面に出ている文字(クリック・選択する項目)
- <span style="color:#c0392b">色付き</span>: 任意(自分で決めてよい値)
- *斜体*: キー入力

(太字は画面の文字の確認が済んでいないため、まだ付けていない。)

NVIDIAのGPUを使う例で書いている。AMD・Intelは「3. GPUのドライバ」の表を参照。

## 1. インストーラー

Welcome -> Install

Keymap Selection -> Japanese 106 を *Space* で選ぶ -> 一番上の行 Continue with jp.kbd keymap で *Enter*(2行目のTestは試し打ちだけ)

Select Installation type -> Distribution Sets(従来のtarball、portsとは無関係)

Distribution Select -> src を選ぶ -> OK

Set Hostname -> *<span style="color:#c0392b">ahmbsd</span>*

Network Installation -> OK

Network Configuration -> rge0 RTL8125(環境による)-> Auto

Partitioning -> Auto (ZFS) -> OK

ZFS Configuration -> zroot -> OK

\>>> Install Proceed with Installation -> Select

stripe Stripe - No Redundancy -> OK

ZFS Configuration -> nda0 Patriot M.2 P300 128GB(環境による)-> OK -> YES

Mirror Selection -> http://download.freebsd.org -> OK

Fetching Distribution -> Archive Extraction -> Boot Configuration -> OK

Set root password -> Password -> Repeat password -> OK

Time Zone Selector -> Asia -> Japan -> Confirmation -> Yes

Time & Date -> Set Date (Skip) -> Set Time (Skip)

System Configuration -> OK

System Hardening -> OK

FreeBSD firmware installation -> そのまま進める

Add User Accounts -> Yes

Username: *<span style="color:#c0392b">usilic</span>*

Full name: *<span style="color:#c0392b">Yu Silic</span>*

Uid: -> Login group: -> *Enter* のみ

Login group is usilic. Invite usilic into other groups? [] : *wheel video operator*

Login class -> Shell -> Home directory -> Home directory permissions -> Enable ZFS encryption? -> Use password-based authentication? -> Use an empty password? -> Use a random password? -> *Enter* のみ

Enter password: *<span style="color:#c0392b">usilic</span>*

Enter password again: *<span style="color:#c0392b">usilic</span>*

Lock out the account after creation? -> OK? -> Add another user? -> Final configuration -> Finish -> *Enter* のみ

Select Manual Configuration -> No

Complete -> Reboot

※sshでログイン中にシステムを入れ直した場合は、ログインする側のマシンで *ssh-keygen -R \<IP\>* を実行する(ホスト鍵が変わるため)。

## 2. パッケージ

ログインしてrootになる。

*su -*

*pkg bootstrap* -> *y*

*pkg update*

*pkg install git gmake pkgconf sudo bash sdl2 sdl2_ttf xfce4-screenshooter-plugin firefox xdg-utils python3* -> *y*

※firefoxの最後に長いメッセージ(boost・gcc14・libjxl・firefoxの未対応機能等)が出るが、対応が要るものは無い。

*pkg install xorg xfce lightdm lightdm-gtk-greeter ja-font-ipa dejavu noto-jp fcitx5 fcitx5-configtool fcitx5-gtk3 fcitx5-qt6 ja-fcitx5-mozc3* -> *y*

*sysrc dbus_enable=YES lightdm_enable=YES*

## 3. GPUのドライバ

| GPU | ドライバ | 起動時に読み込む | 他に |
|---|---|---|---|
| NVIDIA | *pkg install nvidia-driver nvidia-xconfig* | *sysrc kld_list+=nvidia-modeset* | *nvidia-xconfig* |
| AMD(Radeon) | *pkg install drm-kmod* | *sysrc kld_list+=amdgpu*(HD 7000番台以前はradeonkms) | |
| Intel内蔵 | *pkg install drm-kmod* | *sysrc kld_list+=i915kms* | |

表の「ドライバ」と「起動時に読み込む」のコマンドを実行する(*sysrc* を忘れると再起動の後で画面が出ない)。

*kldload nvidia-modeset*(AMDはamdgpu、Intelはi915kms)を実行し、メッセージが何も出ないことを確かめる。

NVIDIAの場合は、続けて *nvidia-xconfig* を実行する。

## 4. sudoとキーボード

*echo '%wheel ALL=(ALL:ALL) ALL' \> /usr/local/etc/sudoers.d/wheel*

*chmod 440 /usr/local/etc/sudoers.d/wheel*

*mkdir -p /usr/local/etc/X11/xorg.conf.d*

*cat \> /usr/local/etc/X11/xorg.conf.d/keyboard.conf \<\<'EOF'*  
*Section "InputClass"*  
*Identifier "keyboard"*  
*MatchIsKeyboard "on"*  
*Option "XkbModel" "jp106"*  
*Option "XkbLayout" "jp"*  
*Option "XkbOptions" "ctrl:swapcaps"*  
*EndSection*  
*EOF*

※XkbOptionsの行はCaps LockとCtrlを入れ替える設定。要らなければ省く。

## 5. 日本語入力に対応したSDL2

パッケージのSDL2はfcitxに対応していないので、-gで日本語を入力できるようportsからビルドする。時間と数GBの空きが要る。

*git clone --depth 1 https://git.FreeBSD.org/ports.git /usr/ports*

*echo 'devel_sdl20_SET+=DBUS FCITX' \>\> /etc/make.conf*

*cd /usr/ports/devel/sdl20*

*make install-missing-packages*

*make BATCH=yes reinstall clean*

*pkg query '%Ok=%Ov' sdl2 | grep -E 'DBUS|FCITX'*(DBUS=on、FCITX=on と出ることを確かめる)

*pkg lock -y sdl2*

※ロックすると *pkg upgrade* でパッケージ版に戻らない。sdl2を更新する時は *pkg unlock -y sdl2* で解除し、この節の *cd* 以降をやり直す。

## 6. 日本語の環境を設定して再起動

rootからユーザーへ戻る。

*exit*

*cat \> ~/.xprofile \<\<'EOF'*  
*export LANG=ja_JP.UTF-8*  
*export GTK_IM_MODULE=fcitx*  
*export QT_IM_MODULE=fcitx*  
*export XMODIFIERS=@im=fcitx*  
*export SDL_IM_MODULE=fcitx*  
*EOF*

*sudo reboot*

## 7. Xfceの最初の設定と公開鍵の登録

xfceにログインし、画面最下行左から2番目の「\$\_」アイコン(Terminal)をクリックする。

NVIDIAの場合は、ウィンドウの合成表示を切る(有効のままだと画面が固まることがある)。

*xfconf-query -c xfwm4 -p /general/use_compositing -n -t bool -s false*

鍵を作って、公開鍵を表示する。

*ssh-keygen -t ed25519* -> *Enter* のみ

*cat ~/.ssh/id_ed25519.pub*(折り返し無しでコピーする)

firefoxで *https://github.com/settings/keys* へアクセスし、GitHubのアカウントでログインする。アカウントが無ければ作成する。

New SSH key をクリック

Title -> *<span style="color:#c0392b">適当な名前</span>*

Key type -> そのまま

Key -> 先ほどコピーした公開鍵をペースト

Authentication keys の欄に、Titleの公開鍵が登録されていることを確かめる。

※リポジトリが非公開の間だけ必要な手順。

## 8. usilicのビルド

Terminalで実行する。

*ssh -T git\@github.com* -> 初回は *yes*(Hi ...! と出ればよい)

*mkdir git*

*cd git*

*git clone -b mpi-gui-fix git\@github.com:haa15030/usilic.git*

*cd usilic/src/cpp*

*gmake usilic && gmake install*

*echo 'PATH=\$HOME/.local/bin:\$PATH; export PATH' \>\> ~/.profile*

*. ~/.profile*

※~/.local/binは既定のPATHに無いので追加する(sshでつなぎ直しても読まれる)。

※この時やログインの度に英語の小ネタが毎回違う内容で出るのは、既定の~/.profileの最後の fortune freebsd-tips(FreeBSDの使い方のヒントをランダムに1つ表示する)。エラーではない。要らなければその行の先頭に#。freebsd-tipsは英語だけで、日本語版は無い。

※bashをログインシェルにするなら *chsh -s /usr/local/bin/bash*。bashは~/.bash_profileがあれば~/.profileを読まないので、その時だけ次も実行する。

*echo 'PATH=\$HOME/.local/bin:\$PATH; export PATH' \>\> ~/.bash_profile*

## 9. 日本語入力の設定

アプリケーション -> 設定 -> Fcitx5 設定

入力メソッドに Mozc があることを確かめる。無ければ追加する。一覧に出なければ「現在の言語のみ表示」のチェックを外す。

切り替えキーから Ctrl+Space を外す(usilicの選択/非選択のキーと被るため)。切り替えは 半角／全角 のキーで行う。

## 10. usilicの起動

XfceのTerminalで、パネルを再起動する(ログインした後で *gmake install* したusilicをメニューに出すため)。

*xfce4-panel -r*

アプリケーション -> アクセサリー -> usilic をクリックする。

「usilic-1.5b:~\$ sudo pkg install -y groff mg texinfo 7-zip w3m」で *Enter*

パスワードを入力する。
