# --- 基本設定
# DEBIAN区画（SSD 等のルート）を RAMDISK でマウントする /mnt/$DEBIAN と、STRAGE のイメージ等の置き場所（STRAGE/img.$DEBIAN）の名前。
# Ubuntu は ubuntu（Debian と Ubuntu で同じ STRAGE を使っても、?GB-名前.img 等を取り違えない。Debian の土台で Ubuntu の
# イメージを展開すると、画面が真っ暗なままだった）。LiveUSB稼働想定なら「vbox」非推奨
readonly DEBIAN=`grep -qx 'ID=ubuntu' /etc/os-release 2>/dev/null && echo ubuntu || echo debian`
readonly USR=`awk -F: '$3 >= 1000 && $3 < 60000 && $6 ~ "^/home/" {print $1; exit}' /etc/passwd` # インストーラで作ったユーザー（/home の最初の一般ユーザー）
SH=bash	# U.sh デフォルト実行シェル

# --- make_usb（-m）で作るUSBのルート区画の大きさ（GiB）
# 16GBのUSBメモリ（実際は14.4〜14.9GiB）に収まるよう14。USBが小さければ、残り全部をルートにしてSTRAGE区画は作らない
readonly ROOT_GB=14

# --- -u（Debian、NONE-KERNEL）で RESCUE_GB のカーネルに続けて作るカーネル（GB）。STRAGEが要る
# 4GB-rescue.img と usilic のビルド環境より先に作り、RAM-*+??gb-base.gz が収まるかを見る。作らないなら ""
readonly EXTRA_GB=8

# --- -u で tools/setup-debian.sh に渡すオプション（rescue-USBは色々な機械で使うので、GPUのドライバは入れない）
readonly SETUP_OPT="--gpu=none"

# --- -u で usilic をビルドした後に外す、ビルドにだけ要るパッケージ（4GBのRAMDISKに収めるため）
# usilicが使うライブラリ（libSDL2等）は残す。rescue上でもビルドするなら ""
readonly RESCUE_DEV_PURGE="build-essential pkg-config libncurses-dev libsdl2-dev libsdl2-ttf-dev"

# --- -s・-u のイメージ（?GB-*.img）に入れないもの（/ からの相対、* 可）
# usilic は ~/.local に入っていて、rescue ではビルドしないのでリポジトリ（.git とビルドの中間ファイルで1GB近く）は要らない
readonly IMG_EXCLUDE="home/*/git/usilic home/*/.cache"
readonly RAMDISK_MARGIN_KB=50000 # RAMDISKに収まるかの確認で残す余裕（起動してから / に書かれる分）

# --- データ保存（-s）の作業エリア設定
# 下記$WORK_AREAが｢opt｣の時は作業エリアとして$STRAGE/opt（デフォルトはtmpfs）を使用する他
# RAMDISK稼働時'. U'[Enter]で表示の値が『/dev/shm』＜『img-size』の時は自動的に｢optモード｣となる
# ただし容量間近まで肥大化したシステムでの｢-s｣は、表示の値に関わらずエラーとなるケースがあるため
# ｢-｣（'. U -s . -'[Enter]）で一時的に｢optモード｣へ設定可能
#
# readonly WORK_AREA=opt
#
readonly WORK_AREA=tmpfs

# --- RAMDISK稼働時のtmpfs割り当てディレクトリ
#readonly SHM_LIST=""
readonly SHM_LIST="home tmp var"

# --- 4GB-rescue版パッケージリスト（任意）
# -u は tools/setup-debian.sh（xorg・xfce4・fcitx5-mozc・日本語フォント・firefox-esr・mpv・usilicのビルド等）と
# これを入れた状態を 4GB-rescue.img として保存する（setup-debian.sh が入れるものは書かない。RAMDISKの空きを残すため最小限に）
RESCUE_LIST="gparted gdisk"
# 推奨パッケージ（Recommends）を入れずに入れるもの。emacs は推奨で mailutils・exim4 も入り、GTK版と一緒だと
# 4GBのRAMDISKに収まらなかった。emacs-nox を --no-install-recommends で入れると約190MB（Debian 13で収まった）
RESCUE_LIST_NOREC="emacs-nox"

# --- -d（8GB等、4GBより大きなRAMDISKの ?GB-base 上）で選んだデスクトップと一緒に入れる日本語環境
# デスクトップの一覧は、Debian では tasksel の *-desktop、Ubuntu では下の「Ubuntu用の設定」の DESKTOP_LIST
JP_LIST="task-japanese-desktop"

# --- usilic が使うツール（usilic の最初の起動で sudo apt install を案内するもの、app/main.cpp の offerGroffInstallOnce()）
# デスクトップのあるホストへ usilic を入れる時（-r・-d）に、無ければ入れておく（~/.usilic が前のホストから残っていると
# usilic は「最初の起動」にならず、案内せずに「見つからない」と知らせるだけになる）
USILIC_TOOLS="groff mg info p7zip-full w3m unzip fonts-noto-cjk python3"

# --- CtrlとCaps Lockの入れ替え（~/.xprofile、Xにログインした時）
# rescue-USBは色々なキーボードで使うので、入れ替えるかはログインの時につながっているキーボードで決める。
# 名前（/proc/bus/input/devices の N: Name=）がこれに合うキーボード（最初からAの左がCtrl）がつながっていれば入れ替えない。
# ~/.xprofile に何も書かないなら ""
readonly NOSWAP_KBD="HHKB|Happy Hacking"

# --- xGB-rescue（オプション）
# 例）4GB-rescue＋カーネルビルド環境
#
RESCUE_LIST_X="$RESCUE_LIST fakeroot libncurses5-dev"
readonly BUILD_LIST="build-dep linux"
#
#RESCUE_LIST_X=""
#readonly BUILD_LIST=""

# --- -k・-u で作るカーネルから外すもの（scripts/config --disable、親を外せばその下も外れる）
# 配布のカーネルの設定は今では使われないものまで全部入りで、/lib/modules（RAM-*-base.gz に入る）が大きい（Ubuntu 576MB）。
# PCカード・ISDN・ATM 等の古いもの、無線LAN（firmware も外している）、テレビ・カメラ（MEDIA_SUPPORT）、ノートパソコン・
# 組み込みの音声（SND_SOC。デスクトップの HD Audio は SND_HDA で残る）、センサー（IIO）、サーバーの NIC・HBA・InfiniBand、
# staging、テスト用。外さないなら ""
readonly KERNEL_DISABLE="PCCARD ISDN ATM FDDI HIPPI ARCNET HAMRADIO CAN WAN IEEE802154 NFC FIREWIRE PARPORT
    WLAN MEDIA_SUPPORT SND_SOC SOUNDWIRE IIO INPUT_JOYSTICK INPUT_TABLET INPUT_TOUCHSCREEN
    INFINIBAND NET_VENDOR_MELLANOX NET_VENDOR_CHELSIO NET_VENDOR_QLOGIC NET_VENDOR_CAVIUM NET_VENDOR_HUAWEI
    NET_VENDOR_PENSANDO NET_VENDOR_NETRONOME NET_DSA SCSI_QLA_FC SCSI_LPFC SCSI_BFA SCSI_QEDF FCOE
    STAGING GREYBUS SURFACE_PLATFORMS CHROME_PLATFORMS USB_GADGET KUNIT RUNTIME_TESTING_MENU"
# 足すもの（scripts/config --enable）。LEGACY_PTYS: 旧C版（old/）の SHELL は BSD 形式の擬似端末（/dev/ptyXX）を使う
readonly KERNEL_ENABLE="LEGACY_PTYS"

# --- /etc/hosts への追加分（オプション）
#
readonly BAK_HOST="192.168.3.1	router"

# --- STRAGE用ファイル共有設定（/etc/fstab）
#
# 例) Windowsユーザのホームディレクトリ等
#readonly SMBFS="//192.168.3.90/c /mnt/cifs cifs rw,noauto,user=usilic,pass=U 0 1"
#
readonly SMBFS=""
#

#readonly MNTS=""
readonly MNTS="ubuntu"
#
# --- RAMDISK の上で /opt/U.sh を書き換えたら、$MNT_DEBIAN/opt/U.sh へも写すこと（写さないと再起動で消える）
# （以前の差分の記録（U.diff・「Patch now?」）は廃止。書き換えた直後だと記録されないことがあり、more の表示も扱いにくかった）

# マニュアル　第3章　注意点③
#
# DST_STRAGE_FS=ntfs
# DST_STRAGE_FS=exfat
# DST_STRAGE_FS=vfat
DST_STRAGE_FS=ext4

# --- $USRのホームディレクトリへ保存
# 例）システム起動後、各種設定ファイルの内容は下記が優先（ログイン後はU.shの実行で反映）
DOT_FILES() {
    if [ -f /tmp/dot_files.$TMP_LABEL ]; then
	return
    fi
    # ~/.bash_aliases　キーボードの日本語配列化
    cat <<EOF > /home/$USR/.bash_aliases
alias jp='
    if [ "\$DISPLAY" ]; then
        if [ ! "\`dpkg -l | grep x11-xkb-utils\`" ]; then
            sudo apt install -y x11-xkb-utils
        fi
        setxkbmap jp > /dev/null 2>/dev/null
    fi
'
jp
EOF

    # ~/.emacs.d/init.el　emacsの背景および文字色
    mkdir -p /home/$USR/.emacs.d
    cat <<EOF > /home/$USR/.emacs.d/init.el
(if window-system
    (progn
      (set-face-background 'default "#123a33") ;; 標準の背景色
      (set-face-foreground 'default "#ffffff")   ;; 標準の文字色
      (set-face-background 'mode-line "#557777")   ;; モードライン背景
      (set-face-foreground 'mode-line "#dddddd")   ;; モードライン
      (set-face-background 'mode-line-inactive "#556060") ;; モードライン背景
      (set-face-foreground 'mode-line-inactive "#aaaaaa") ;; モードライン2
      (set-face-background 'scroll-bar "#d6dabf")  ;; スクロールバー
      (set-face-foreground 'scroll-bar "#c6caaf")  ;; スクロールバー
      (set-face-background 'tooltip "ghostwhite")  ;; ツールチップ背景
      (set-face-foreground 'tooltip "black")       ;; ツールチップ
      (set-face-background 'tool-bar "#d6dabf")    ;; ツールバー
      (set-face-background 'menu "#d6dabf")        ;; メニュー
      (set-face-background 'fringe "#d0d4bf")      ;; fringe
      (set-face-foreground 'fringe "#90947f")      ;; fringe
      (set-cursor-color "#dddddd")
      )
  (progn
    (set-face-background 'default "#000000")
    (set-face-foreground 'default "#ffffff")
    )
  )
EOF
    mkdir -p /root/.emacs.d
    ln -sf /home/$USR/.emacs.d/init.el /root/.emacs.d
    chown -R $USR:$USR /home/$USR/.emacs.d # ディレクトリごと（rootのままだとemacsがeln-cache等を書けない）

    # ~/.xprofile　CtrlとCaps Lockの入れ替え（NOSWAP_KBD）。前に書いた分（# >>> U.sh 〜 # <<< U.sh）は書き直す
    if [ -f /home/$USR/.xprofile ]; then
        sed -i /home/$USR/.xprofile -e '/^# >>> U.sh swapcaps/,/^# <<< U.sh swapcaps/d'
    fi
    if [ "$NOSWAP_KBD" ]; then
        cat <<EOF >> /home/$USR/.xprofile
# >>> U.sh swapcaps（U.sh の NOSWAP_KBD に合うキーボードがつながっていれば入れ替えない。/etc/default/keyboard の分も外す）
if grep -qiE '^N: Name=".*($NOSWAP_KBD)' /proc/bus/input/devices 2>/dev/null; then
    _o=\$(setxkbmap -query | sed -n 's/^options: *//p' | tr ',' '\n' | grep -vx 'ctrl:swapcaps' | paste -sd, -)
    setxkbmap -option '' \${_o:+-option \$_o}
else
    setxkbmap -query | grep -q 'ctrl:swapcaps' || setxkbmap -option ctrl:swapcaps
fi
# <<< U.sh swapcaps
EOF
        chown $USR:$USR /home/$USR/.xprofile
    fi
    # ~/.config/kxkbrc　KDE の Wayland のセッションでは ~/.xprofile の setxkbmap が効かない（Zutty 等の X11 のアプリだけ入れ替わる）ので、
    # KDE のキーボードの設定にも書く（[Layout] の Options。入れ替えるかは、この時（起動の時）につながっているキーボードで決める）
    if [ "$NOSWAP_KBD" ]; then
        _kx=/home/$USR/.config/kxkbrc
        mkdir -p /home/$USR/.config
        _kopt=`sed -n '/^\[Layout\]/,/^\[/s/^Options=//p' $_kx 2>/dev/null | tr ',' '\n' | grep -vx 'ctrl:swapcaps' | grep -v '^$'`
        if ! grep -qiE "^N: Name=\".*($NOSWAP_KBD)" /proc/bus/input/devices 2>/dev/null; then
            _kopt="$_kopt
ctrl:swapcaps"
        fi
        _kopt=`echo "$_kopt" | grep -v '^$' | paste -sd, -`
        touch $_kx
        awk -v o="$_kopt" '
            /^\[/ {sec = ($0 == "[Layout]")}
            sec && /^(Options|ResetOldOptions)=/ {next}
            {print}
            $0 == "[Layout]" {print "Options=" o; print "ResetOldOptions=true"; done = 1}
            END {if (!done) {print "[Layout]"; print "Options=" o; print "ResetOldOptions=true"}}' $_kx > /tmp/kxkbrc.$TMP_LABEL
        cat /tmp/kxkbrc.$TMP_LABEL > $_kx
        rm -f /tmp/kxkbrc.$TMP_LABEL
        chown $USR:$USR $_kx
    fi
    # /etc/dconf/db/local.d/00-U-swapcaps　GNOME（Wayland のセッション）も ~/.xprofile・kxkbrc を読まない。
    # gsettings の org.gnome.desktop.input-sources xkb-options の、システム全体の既定として書く（ユーザーが自分で設定していればそちらが優先）。
    # 入れ替えるかは kxkbrc と同じく、この時（起動の時）につながっているキーボードで決める
    if [ "$NOSWAP_KBD" ] && command -v dconf > /dev/null 2>&1; then
        mkdir -p /etc/dconf/profile /etc/dconf/db/local.d
        if [ ! -f /etc/dconf/profile/user ]; then
            printf 'user-db:user\nsystem-db:local\n' > /etc/dconf/profile/user
        elif ! grep -qx 'system-db:local' /etc/dconf/profile/user; then
            echo 'system-db:local' >> /etc/dconf/profile/user
        fi
        if grep -qiE "^N: Name=\".*($NOSWAP_KBD)" /proc/bus/input/devices 2>/dev/null; then
            rm -f /etc/dconf/db/local.d/00-U-swapcaps
        else
            printf "[org/gnome/desktop/input-sources]\nxkb-options=['ctrl:swapcaps']\n" > /etc/dconf/db/local.d/00-U-swapcaps
        fi
        dconf update
    fi

    # ~/.config/autostart/light-locker.desktop　light-locker（画面ロック）を起動しない（/etc/xdg/autostart の同名ファイルを打ち消す）
    mkdir -p /home/$USR/.config/autostart
    cat <<EOF > /home/$USR/.config/autostart/light-locker.desktop
[Desktop Entry]
Type=Application
Name=Screen Locker
Exec=light-locker
Hidden=true
EOF
    cat <<EOF > /home/$USR/.config/autostart/xfce4-terminal.desktop
[Desktop Entry]
Encoding=UTF-8
Version=0.9.4
Type=Application
Name=xfce4-terminal
Comment=
Exec=/usr/bin/xfce4-terminal --geometry 125x29+0+0
OnlyShowIn=XFCE;
RunHook=0
StartupNotify=false
Terminal=false
Hidden=false
EOF
    # ~/.config/autostart/polkit-mate-authentication-agent-1.desktop　LXDE では lxpolkit と MATE の認証エージェントの両方が起動し、
    # 「An authentication agent already exists for the given subject」と出た。MATE が無いホスト（LXDE 等）では MATE の方を起動しない
    _pk=/home/$USR/.config/autostart/polkit-mate-authentication-agent-1.desktop
    if [ -f /etc/xdg/autostart/lxpolkit.desktop ] && [ -f /etc/xdg/autostart/polkit-mate-authentication-agent-1.desktop ] && \
       [ ! -f /usr/share/xsessions/mate.desktop ]; then
        printf '[Desktop Entry]\n# U.sh: lxpolkit と重なるので起動しない\nType=Application\nName=MATE Authentication Agent\nExec=true\nHidden=true\n' > $_pk
    elif grep -q '^# U.sh:' $_pk 2>/dev/null; then
        rm -f $_pk
    fi
    chown -R $USR:$USR /home/$USR/.config
    touch /tmp/dot_files.$TMP_LABEL
}

# --- 他のOSが起動しなくなった場合のレスキューUSBとしての設定
# 例）個別のSSDに｢masOC Mojave｣｢masOS Sonoma｣｢Windows 11｣がインストール済みの場合
# blkid等でそれぞれのEFI区画デバイス名(/dev/nvme0n1p1, /dev/nvme1n1p1, /dev/nvme2n1p1 ...)を
# 確認後、あらかじめ下記の設定が必要（EFIパーティションがp1でない時は要読み替え）
# 'sudo fatlabel /dev/nvme0n1p1 SONOMA'[Enter]
# 'sudo fatlabel /dev/nvme1n1p1 MOJAVE'[Enter]
# 'sudo fatlabel /dev/nvme2n1p1 WIN11'[Enter]
# GRUBをUEFIで起動した時（.efi を chainload できる時）に、そのラベルの区画がある項目だけ出る。
# 項目に savedefault は付けない（付けると GRUB_DEFAULT=saved で次から既定がその項目になり、時間切れでまた OpenCore・Windows へ行く）。
# 選んだOSが機械の起動順（UEFIの BootOrder）の先頭を自分に書き換える時は、そちらで止める:
#   Windows: 管理者のコマンドプロンプトで 'bcdedit /set {fwbootmgr} displayorder {bootmgr} /addlast'
#   OpenCore: config.plist の UEFI → Quirks → RequestBootVarRouting を true（macOS が書く起動順を OpenCore の中だけに留める）
#
readonly GRUB_CUSTOM="
if [ \"\$grub_platform\" = \"efi\" ]; then
	insmod part_gpt
	insmod part_msdos
	insmod fat
	if search --no-floppy --set=u_esp --label SONOMA; then
		menuentry 'macOS Sonoma' {
			search --no-floppy --set=root --label SONOMA
			chainloader /EFI/Boot/BOOTx64.efi
		}
	fi
	if search --no-floppy --set=u_esp --label MOJAVE; then
		menuentry 'macOS Mojave' {
			search --no-floppy --set=root --label MOJAVE
			chainloader /EFI/Boot/BOOTx64.efi
		}
	fi
	if search --no-floppy --set=u_esp --label WIN11; then
		menuentry 'Windows 11' {
			search --no-floppy --set=root --label WIN11
			chainloader /EFI/Microsoft/Boot/bootmgfw.efi
		}
	fi
fi
"
#
#readonly GRUB_CUSTOM=""


# set -ux;
#
if [ ! "`ls /tmp/log.?????? 2>/dev/null`" ]; then
    mktemp /tmp/log.XXXXXX >/dev/null
fi
readonly TMP_LABEL=`ls -t /tmp/log.?????? | head -n1 | sed -e 's@^/tmp/log\.@@'` # 2つ以上あれば新しい方
chmod 644 /tmp/log.$TMP_LABEL
readonly MNT_DEBIAN=/mnt/$DEBIAN
readonly U_SH=`readlink -f "$0"` # 今動いている U.sh（オプションがあるかを見る。/opt/U.sh とは限らない）
readonly OPT_BUILD=/opt/build # カーネル.deb は build/の親ディレクトリに作成される
readonly MEM_TOTAL_KB=`grep MemTotal /proc/meminfo | awk -F: '{print $2}' | awk '{print $1}'`
readonly MEM_TOTAL_GB=$((MEM_TOTAL_KB/1000/1000))
readonly TMPFS_KB=$((MEM_TOTAL_KB/2))
readonly NETWORK_MANAGER_CONF=/etc/NetworkManager/NetworkManager.conf
readonly NUMERATOR=11 # $NUMERATOR * ｢ルートファイルシステムサイズ｣ / 10 -> ｢同システム.img｣サイズ
readonly DISPLAY_MANAGER_LIST="lightdm sddm gdm3 wdm xdm"
readonly UNAME_R=`uname -r`
readonly ON_WSL=`echo $UNAME_R | fgrep 'WSL'`
readonly ON_RAMDISK=`fgrep -e 'remount-ro' /etc/fstab | fgrep /dev/ram`
readonly ON_LINE=`ip a | grep 'inet ' | grep global`
readonly ON_UBUNTU=`cat /etc/issue|grep -v 'Debian GNU/Linux'`
readonly ON_TORAM=`[ "$(findmnt -no FSTYPE / 2>/dev/null)" = tmpfs ] && echo y` # ルートの区画をtmpfsへコピーして起動している（Ubuntu、-u で設定）
# ディストリビューションのカーネルでSSD等の区画から起動している（Debian: 〜-amd64、Ubuntu: 〜-generic。TORAM は除く）
if [ ! "$ON_TORAM" ]; then
    readonly ON_NONE=`echo $UNAME_R | grep -E -e 'amd64$' -e '-generic$'`
else
    readonly ON_NONE=""
fi
# 通常カーネル稼働時に $MNT_DEBIAN が既にマウント済みなら、symlink 化も mount/umount もせずそのまま使う
if [ ! "$ON_RAMDISK" ] && mountpoint -q $MNT_DEBIAN 2>/dev/null; then
    readonly KEEP_MNT_DEBIAN=y
else
    readonly KEEP_MNT_DEBIAN=""
fi
_fstab_uuid() { # /etc/fstab で $1 にマウントする区画のUUID（UUID=〜 と /dev/disk/by-uuid/〜 のどちらでも）
    awk -v m="$1" '$1 !~ /^#/ && $2 == m {print $1}' /etc/fstab | head -n1 | sed -e 's/^UUID=//' -e 's@^/dev/disk/by-uuid/@@'
}
if [ "$ON_UBUNTU" ]; then
    readonly BUILD=BUILD_UBUNTU
    readonly BUILD_IMG_GB=7 # kernel-7.3.0 では 6GB では不足
    readonly RESCUE_GB=8 # Ubuntu の土台は 4GB に収まらない（-u は ${RESCUE_GB}GB-base で ${RESCUE_GB}GB-rescue を起動する。EXTRA_GB は作らない）
    # linux-source-* の版。配布のカーネル（7.3.0-11-generic）ならその名前から取れるが、自作のカーネル（7.3.1+8gb）では
    # 名前の版（7.3.1、Makefile の SUBLEVEL）とパッケージの版（linux-source-7.3.0）がずれる（2026-11-25、8GB-rescue の
    # -k 16 で /usr/src/linux-source-7.3.1 が無いと止まった）ので、linux-source が依存するパッケージの名前から取る
    _kernel_version=`LC_ALL=C apt-cache depends linux-source 2>/dev/null | sed -n 's/.*: linux-source-\([0-9.]*\)$/\1/p' | head -n1`
    readonly KERNEL_VERSION=${_kernel_version:-`echo $UNAME_R | sed -e 's/[-+].*//'`} # 7.0.0-xx-generic / 7.0.0+8gb -> 7.0.0
    if [ "$ON_RAMDISK" ]; then
        readonly RAMDISK_GB=`echo $UNAME_R | awk -F'+' '{print $2}' | sed -e s/gb//` # カーネル名から抽出
    else
        readonly RAMDISK_GB=$RESCUE_GB
    fi
    readonly TEMPLATE_MB=3000
    if [ "$ON_RAMDISK" ] || [ "$ON_TORAM" ]; then
        readonly LINUX_CONFIG=config-$UNAME_R
        if [ "$ON_TORAM" ]; then
            DEBIAN_ROOT_UUID=`_fstab_uuid /` # fstab はそのまま（区画は tmpfs の下に隠れている）
        else
            DEBIAN_ROOT_UUID=`_fstab_uuid $MNT_DEBIAN`
        fi
    else
        readonly LINUX_CONFIG=`ls /boot/config*generic 2>/tmp/error.$TMP_LABEL|tail -n1|sed -e 's/\/boot\///'`
        if [ ! "$LINUX_CONFIG" ]; then
            cat /tmp/error.$TMP_LABEL
            exit
        fi
        DEBIAN_ROOT_UUID=`findmnt -no UUID /`
    fi
    readonly PURGE_LIST=""
else
    readonly BUILD=BUILD_DEBIAN
    readonly BUILD_IMG_GB=7 # Debian 13（6.12）では 4GB・5GB では不足
    readonly RESCUE_GB=4
    case "`. /etc/os-release; echo $VERSION_ID`" in # linux-source-* の版
        12) readonly KERNEL_VERSION=6.1 ;;
        13) readonly KERNEL_VERSION=6.12 ;;
        *) readonly KERNEL_VERSION=`echo $UNAME_R | sed -e 's/[-+].*//' | awk -F. '{print $1"."$2}'` ;;
    esac
    readonly TEMPLATE_MB=2000
    if [ "$ON_RAMDISK" ]; then # RAMDISK上
        DEBIAN_ROOT_UUID=`_fstab_uuid $MNT_DEBIAN`
        readonly RAMDISK_GB=`echo $UNAME_R | awk -F'+' '{print $2}' | sed -e s/gb//` # カーネル名から抽出
    else # DEBIAN上（WSLを含む） #_４)　DEBIANのルートファイルシステムUUIDを記録
        DEBIAN_ROOT_UUID=`findmnt -no UUID /`
        if [ "$ON_NONE" ] || [ "$ON_WSL" ]; then
            readonly RAMDISK_GB=$RESCUE_GB #_13)　最初に作成するカーネルのRAMDISKサイズを4GBに設定
        else
            readonly RAMDISK_GB=`echo $UNAME_R | awk -F'+' '{print $2}' | sed -e s/gb//` # それ以外はカーネル名から抽出
        fi
    fi
    readonly LINUX_CONFIG=linux-config-$KERNEL_VERSION
    readonly PURGE_LIST="texlive-base texlive-binaries texlive-latex-base texlive-latex-extra texlive-latex-recommended texlive-pictures dvipng"
    readonly CONFIG_NONE=config.amd64_none_amd64 # DebianデフォルトカーネルのCONFIG設定(/usr/src/$LINUX_SOURCE/.config)
fi
if [ "$DEBIAN_ROOT_UUID" ]; then
    readonly DEBIAN_ROOT_PART=`blkid -U $DEBIAN_ROOT_UUID`
else
    readonly DEBIAN_ROOT_PART=""
fi
readonly BUILD_IMG_KB=$((BUILD_IMG_GB*1000*1000)) # buildディレクトリイメージサイズ
readonly RESCUE_KB=$((RESCUE_GB*1000*1000))
readonly TEMPLATE_KB=$((TEMPLATE_MB*1000))
readonly LINUX_SOURCE=linux-source-$KERNEL_VERSION
readonly TEMPLATE_BASE=TEMPLATE-$KERNEL_VERSION-base
readonly RAM_DIR_LIST="boot dev media opt proc run sys usr/lib/modules tmp mnt/$DEBIAN" # $MNT_DEBIAN はNG
# arch-install-scripts（arch-chroot）・gdisk（sgdisk）・dosfstools（mkfs.vfat）・grub-*-bin（BIOSとUEFIの両方のGRUB）は -m で使う
# git: -u が usilic のリポジトリを取るのに使う（BASE に無いと、記録へ戻すたびに外れて入れ直していた）。RAMDISK でも使える
BASE_LIST="firmware-realtek firmware-misc-nonfree firmware-amd-graphics firmware-nvidia-gsp arch-install-scripts gdisk dosfstools grub-pc-bin grub-efi-amd64-bin git"
readonly PAGER=more # _page で usilic・less が無い時だけ（more は戻れない）
# NVIDIA の純正ドライバのカーネル側（dkms が作ったカーネル向けにモジュールを組み立てる。GSP の firmware と nouveau を止める設定も入る）。
# NVIDIA の GPU がある時だけ、-u が BASE に入れる。画面の描画のライブラリ（libnvidia-gl 等、約900MB）は入れない（要るなら DESKTOP_LIST に）。
# "" なら nouveau のまま。NVIDIA_NOUVEAU_FW は、その時に外す nouveau 用の firmware。
# NVIDIA_DESKTOP は、-d がデスクトップと一緒に入れる描画の側（GNOME は nvidia の GBM・EGL が無いとログイン画面が出ない）。
# 版を変える（NVIDIA_LIST を変える）と、-u が BASE.u の前の版の nvidia-*・libnvidia-* を外して入れ直す
NVIDIA_LIST=""
NVIDIA_NOUVEAU_FW=""
NVIDIA_DESKTOP=""
# -u が ${RESCUE_GB}GB-rescue に、カーネルを組み立てる環境（BUILD_LIST の build-dep linux と、-k が使う fakeroot・libncurses-dev・
# openssl）も入れておく（rescue の上でネット無しでも -k できる。ソース（linux-source）は -k の時に取る）。Debian の 4GB-rescue は
# 収まらないので ""
RESCUE_KERNEL_DEV=""

# --- Ubuntu（ON_UBUNTU）用の設定 ------------------------------------
# Ubuntu でも -u は Debian と同じく ${RESCUE_GB}GB（8GB）の RAMDISK 用カーネル・RAM-*-base.gz・${RESCUE_GB}GB-rescue.img を作る。
# あわせて「ルートの区画を tmpfs へコピーして起動する」項目（TORAM、dracut の時）も作る
if [ "$ON_UBUNTU" ]; then
    readonly STD_LIST="ubuntu-standard"
    readonly NO_SNAP=y # snapd を入れず Firefox は Mozilla 公式 apt リポジトリの deb 版を使う（snap を使うなら ""）
    # mv・cp・ls 等を GNU 版（coreutils-from-gnu）に。Ubuntu 25.10 からの既定は Rust で書き直した uutils（coreutils-from-uutils）で、
    # 端のケースが GNU と違う（「mv /home/.」を断らず /home/home ができた）。U.sh は GNU の動きを前提に書いてある。uutils のままなら ""
    readonly GNU_COREUTILS=y
    readonly NETPLAN_NM=/etc/netplan/90-U-network-manager.yaml
    # Z390UD・RTX3050（GA107）で、nouveau では DP の音が鳴らなかった（Ubuntu 26.10 の live、Debian 13 の 6.12 でも。純正なら鳴った）。
    # 580（ubuntu-drivers の recommended）は 7.3.1+8gb の GNOME で「Failed to apply atomic modeset」の後 nvidia-modeset が
    # 止まり、画面が真っ暗・キーボードも効かなかった（ssh は可）ので、新しい 615 に
    NVIDIA_VER=615
    NVIDIA_LIST="nvidia-dkms-$NVIDIA_VER-open"
    NVIDIA_DESKTOP="nvidia-driver-$NVIDIA_VER-open"
    RESCUE_KERNEL_DEV=y # 8GB-rescue は 4.3GB 程度で空きがある（4GB-rescue は作れない）
    NVIDIA_NOUVEAU_FW="linux-firmware-nvidia-graphics" # nouveau 用の GSP（約106MB。純正は nvidia-firmware-580 の方を使う）
    RESCUE_LIST=`echo " $RESCUE_LIST " | sed -e 's/ firefox-esr / firefox /'`
    if [ "$RESCUE_LIST_X" ]; then
        RESCUE_LIST_X="$RESCUE_LIST fakeroot libncurses-dev"
    fi
    # -d のデスクトップ一覧（メタパッケージ:名前。名前は ?GB-名前.img になる）。パッケージ名は 'apt-cache search -- -desktop$' 等で確認
    # 「,」で包みを足せる（DESKTOP_APT_OPT で推奨を入れないので、メタだけだと端末等が入らない。ubuntu-desktop-minimal は
    # GNOME Shell だけで、端末（Ubuntu 25.10 からは ptyxis）・ファイルマネージャ（nautilus）・テキストエディタ・音（PipeWire、
    # pipewire-audio は pipewire-pulse・wireplumber・pipewire-alsa をまとめたもの）は推奨だった）。
    # fonts-ubuntu: 端末（ptyxis）の既定の Ubuntu Sans Mono。無いと monospace が Noto Sans Mono CJK JP になり、字間が広がった
    # 先頭が「+」なら、その項目だけ推奨も入れる（DESKTOP_APT_OPT を使わない）。GNOME-Live は、インストーラの「お試し」と
    # 同じ構成（ubuntu-desktop-minimal と推奨）
    readonly DESKTOP_LIST="ubuntu-desktop-minimal,ptyxis,nautilus,gnome-text-editor,pipewire-audio,fonts-ubuntu:GNOME +ubuntu-desktop-minimal:GNOME-Live xubuntu-core:Xfce kubuntu-desktop:KDE-Plasma lubuntu-desktop:LXQt ubuntu-mate-core:MATE"
    readonly DESKTOP_APT_OPT="--no-install-recommends" # 推奨パッケージも入れるなら ""
    JP_LIST="language-pack-ja language-pack-gnome-ja fcitx5-mozc fonts-noto-cjk"
    BASE_LIST="arch-install-scripts gdisk dosfstools grub-pc-bin grub-efi-amd64-bin git" # 必要なら "linux-firmware" も（1GB前後あるので RAMDISK 容量に注意）
    # -u で BASE.u を記録する前に外すもの（Ubuntu Server が最初から入れる、RAMDISK では要らないもの。RAM-*-base.gz が
    # Debian の倍以上（1.4GB）になった）。クラウド・コンテナ・snap、自動の更新・報告、SAN、開発・解析用、カーネルのメタ
    # （linux-generic: firmware を選んで外すため。U.sh はカーネルを自分で作る）、携帯端末・サーバーの NIC 用の firmware。
    # 「*」も使える。入っていないものは飛ばす
    readonly UBUNTU_PURGE_LIST="cloud-init cloud-init-base cloud-guest-utils lxd-installer snapd squashfs-tools
        unattended-upgrades needrestart ubuntu-release-upgrader-core apport apport-core-dump-handler packagekit
        software-properties-common upki multipath-tools open-iscsi bcache-tools thermald
        linux-perf linux-tools-* makedumpfile manpages-dev ssh-import-id linux-generic
        linux-firmware-qualcomm-misc linux-firmware-qualcomm-graphics linux-firmware-mellanox-spectrum
        linux-firmware-netronome linux-firmware-qlogic linux-firmware-marvell-prestera
        linux-firmware-intel-wireless linux-firmware-qualcomm-wireless linux-firmware-mediatek
        linux-firmware-broadcom-wireless linux-firmware-marvell-wireless firmware-sof-signed"
    # （上の最後の2行: 無線 LAN と、Intel のノートパソコンの音声（SOF）の firmware。Debian の BASE_LIST も入れていない。
    #   rescue なので外す（約250MB）。ノートパソコンで無線 LAN を使うなら、この2行から消す）
    # thermald: Intel のノートパソコンの温度の管理（ACPI の DPTF を読む）。Z390UD では起動に5回失敗し続けた（温度の保護は
    #   ハードウェアがする）
fi
readonly RESCUE_LIST RESCUE_LIST_NOREC RESCUE_LIST_X BASE_LIST JP_LIST NVIDIA_LIST NVIDIA_NOUVEAU_FW NVIDIA_DESKTOP RESCUE_KERNEL_DEV

_on_line() { # 現在のネット接続状態（readonly の $ON_LINE は起動時の値のまま）
    ip a | grep 'inet ' | grep global
}

_net_restart() {
    if [ "$ON_UBUNTU" ]; then
	echo "netplan apply"
	netplan apply
    else
	systemctl restart networking.service
    fi
    sleep 3
}

_apt_sources_ubuntu() { # Ubuntu: deb822 形式で universe/multiverse と deb-src を有効化
    local _src=/etc/apt/sources.list.d/ubuntu.sources _uri _suite

    if [ ! -f $_src ]; then # debootstrap 直後は 1行形式の sources.list のみ
	_uri=`grep '^deb ' /etc/apt/sources.list 2>/dev/null | head -n1 | awk '{print $2}'`
	_suite=`grep '^deb ' /etc/apt/sources.list 2>/dev/null | head -n1 | awk '{print $3}'`
	if [ ! "$_uri" ]; then
	    _uri=http://archive.ubuntu.com/ubuntu
	fi
	if [ ! "$_suite" ]; then
	    _suite=`. /etc/os-release; echo $VERSION_CODENAME`
	fi
	echo "create $_src ($_suite)"
	cat <<EOF > $_src
Types: deb deb-src
URIs: $_uri
Suites: $_suite $_suite-updates $_suite-backports
Components: main restricted universe multiverse
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg

Types: deb deb-src
URIs: http://security.ubuntu.com/ubuntu
Suites: $_suite-security
Components: main restricted universe multiverse
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg
EOF
	if [ -f /etc/apt/sources.list ]; then
	    echo "mv /etc/apt/sources.list /etc/apt/sources.list.U-bak"
	    mv /etc/apt/sources.list /etc/apt/sources.list.U-bak
	fi
	apt update -y
    elif ! grep -q '^Types:.*deb-src' $_src || ! grep -q '^Components:.*universe' $_src; then
	echo "sed -i $_src (deb-src, universe multiverse)"
	sed -i $_src -e 's/^Types: deb$/Types: deb deb-src/' -e 's/^Components:.*/Components: main restricted universe multiverse/'
	apt update -y
    fi
}

_no_snap_ubuntu() { # snapd を禁止し、Firefox を Mozilla 公式 apt リポジトリから入れる設定
    if [ ! "$NO_SNAP" ] || [ -f /etc/apt/preferences.d/U-nosnap ]; then
	return
    fi
    echo "create /etc/apt/preferences.d/U-nosnap, U-mozilla"
    cat <<EOF > /etc/apt/preferences.d/U-nosnap
Package: snapd
Pin: release a=*
Pin-Priority: -10
EOF
    _is_inst wget +
    install -d -m 0755 /etc/apt/keyrings
    wget -q https://packages.mozilla.org/apt/repo-signing-key.gpg -O /etc/apt/keyrings/packages.mozilla.org.asc
    echo "deb [signed-by=/etc/apt/keyrings/packages.mozilla.org.asc] https://packages.mozilla.org/apt mozilla main" > /etc/apt/sources.list.d/mozilla.list
    cat <<EOF > /etc/apt/preferences.d/U-mozilla
Package: *
Pin: origin packages.mozilla.org
Pin-Priority: 1000
EOF
    apt update -y
}

_install_standard() { # 標準システムユーティリティをインストール
    if [ "$ON_UBUNTU" ]; then
	_apt_sources_ubuntu
	_no_snap_ubuntu
	echo "apt-get install -y $STD_LIST"
	apt-get install -y $STD_LIST
    else
	tasksel --task-packages standard 2>/dev/null | xargs apt install -y
    fi
}

_netplan_nm() { # Ubuntu: netplan のレンダラを NetworkManager に（on）／networkd に戻す（off）
    if [ ! -d /etc/netplan ] || [ ! "`command -v netplan`" ]; then
	return
    fi
    # デスクトップの包み（ubuntu-desktop-minimal 等）が /usr/lib/netplan/00-network-manager-all.yaml（renderer: NetworkManager）
    # を入れるのに、NetworkManager は推奨なので DESKTOP_APT_OPT（--no-install-recommends）では入らない。そのままだと netplan が
    # networkd 用の設定を書かず、誰も DHCP をしない（8GB-GNOME で enp5s0 にアドレスが付かず、ssh もできなかった）
    if [ "$1" = "on" ] && [ ! -f "$NETWORK_MANAGER_CONF" ] && [ "`netplan get 2>/dev/null | grep "renderer: NetworkManager"`" ]; then
	echo "apt-get install -y network-manager (netplan: renderer: NetworkManager)"
	apt-get install -y network-manager
    fi
    if [ "$1" = "on" ] && [ -f "$NETWORK_MANAGER_CONF" ]; then
	echo "create $NETPLAN_NM (renderer: NetworkManager)"
	printf 'network:\n  version: 2\n  renderer: NetworkManager\n' > $NETPLAN_NM
	chmod 600 $NETPLAN_NM
	systemctl disable systemd-networkd.service systemd-networkd-wait-online.service 2>/dev/null
	systemctl enable NetworkManager.service 2>/dev/null
    elif [ "$1" = "off" ] && [ -f $NETPLAN_NM ] && [ ! -f "$NETWORK_MANAGER_CONF" ]; then
	echo "rm $NETPLAN_NM"
	rm -f $NETPLAN_NM
	systemctl enable systemd-networkd.service 2>/dev/null
    fi
    netplan generate # 反映は次回起動時（インストール中の接続は切らない）
}

_desktop_ubuntu() { # Ubuntu: デスクトップ導入後の設定（on）／BASE へ戻した後の後始末（off）
    local _dm

    if [ "$1" = "on" ]; then
	_netplan_nm on
	if [ ! -L /etc/systemd/system/display-manager.service ]; then
	    for _dm in lightdm sddm gdm gdm3
	    do
		if [ "`systemctl list-unit-files --no-legend $_dm.service 2>/dev/null`" ]; then
		    echo "systemctl enable --force $_dm"
		    systemctl enable --force $_dm
		    break
		fi
	    done
	fi
	if [ "$NVIDIA_DESKTOP" ] && _nvidia_gpu && [ "`_is_inst ${NVIDIA_LIST%% *}`" ]; then # nvidia の描画の側（GBM・EGL・Xorg 用等）
	    echo "apt-get install -y --no-install-recommends $NVIDIA_DESKTOP"
	    apt-get install -y --no-install-recommends $NVIDIA_DESKTOP
	fi
	echo "systemctl set-default graphical.target"
	systemctl set-default graphical.target
    else
	_netplan_nm off
	echo "systemctl set-default multi-user.target"
	systemctl set-default multi-user.target
    fi
}


_tmpfs_size() {
    local _used_tmpfs _used_devshm _sh
    local _df_line _tmpfs_free _shm _total_tmpfs=0
    local _used_total=$1 

    _sh=`grep ^SH= /opt/U.opt | awk -FSH= '{print $2}'`
    df | \
	while read _line
	do
	    if [ "$_used_total" ]; then
		if [ "$SHM_LIST" ] && [ "$MEM_TOTAL_GB" -ge "$RESCUE_GB" ]; then
		    for _shm in $SHM_LIST
		    do
			if [ "`echo $_line | awk '{print $6}'`" = "/${_shm}" ]; then
		    	    _used_tmpfs=`echo $_line | awk '{print $3}'`
			    _used_total=$((_used_total+_used_tmpfs))
			    echo $_used_total
			fi
		    done
		else
		    echo $_used_total
		fi
	    else
		if [ "`echo $_line | grep /dev/shm$`" ]; then
		    _tmpfs_free=`echo $_line | grep /dev/shm$ | awk '{print $2}'`
		fi
		_used_tmpfs=`echo $_line | grep ^tmpfs | awk '{print $3}'`
		_used_devshm=`echo $_line | grep ^/dev/shm | awk '{print $3}'`
		if [ "$_used_tmpfs" ]; then
		    _tmpfs_free=$((_tmpfs_free-_used_tmpfs))
		elif [ "$_used_devshm" ]; then
		    _tmpfs_free=$((_tmpfs_free-_used_devshm))
		fi
		echo $_tmpfs_free
	    fi
	done
}

_ask() { # read -p の代わり。$1 変数名、$2 問い、$3 --list・--ans= で答えが尽きた時の答え（既定は空＝選ばない・終える）
    # 前から使う時（usilic の RESCUE）: --ans=答え を問いの順に。答えが無ければ（--list だけなら一覧を出して）$3 で進む
    local _v

    if [ "$U_BATCH" ]; then
        if [ "$U_ANS" ]; then
            _v=${U_ANS%%|*}
            U_ANS=${U_ANS#*|}
        else
            _v=$3
        fi
        echo "$2$_v" > $U_TTY # （_list_parts は ` ` の中で呼ばれるので、標準出力へは出さない）
    else
        read -p "$2" _v < /dev/tty
    fi
    eval "$1=\$_v"
}

_page() { # $1 を、戻って読めるページャーで（usilic-py → ユーザーの usilic -c → less → more）
    # more は戻れず、-v で STRAGE の ls の後の df まで送ると終わってしまい、ls へ戻れなかった。usilic は q で終える
    # usilic は U.sh と同じ root で（SHELL も root。旧 tcb と同じく /root/.usilic ができてかまわない。前は /root に作らないよう
    # runuser -u $USR で一般ユーザーにしていた）。HOME は root のもの（/root）に。標準エラー出力は捨てる（最初の起動で HELP を
    # 組む groff の警告（cannot select font 'JPM'）が端末に出ないように）
    chmod 644 $1
    # usilic-py（Python 版）はページャーでなく、/mnt の (ls) の上に FILE で開く（USILIC_OPEN）。^G で /mnt へ出て、そのまま /mnt/番号 へ行ける
    if [ -x /usr/local/bin/usilic-py ]; then
        env HOME=/root USILIC_OPEN=$1 /usr/local/bin/usilic-py /mnt 2>/dev/null
    elif [ -x /home/$USR/.local/bin/usilic ]; then
        env HOME=/root /home/$USR/.local/bin/usilic -c $1 2>/dev/null
    elif command -v less > /dev/null 2>&1; then
        less $1
    else
        $PAGER $1
    fi
}

_die() { # ログ（/tmp/log.*、$1 があれば足す）を出して U.sh を終える（main の最後と同じ出し方）
    if [ "$1" ]; then
	echo "$1" >> /tmp/log.$TMP_LABEL
    fi
    cd /opt
    grep -v '^ : ' /tmp/log.$TMP_LABEL | sed '/^$/d' > /tmp/tmp.$TMP_LABEL
    _save_log /tmp/tmp.$TMP_LABEL
    exit 1
}

_save_log() { # ログ $1 を出して /opt/U.log_日付 へ足す。RAMDISK なら DEBIAN区画の /opt/U.log_日付 へも（再起動で消えないように）
    local _log=U.log_`date "+%F"` _mounted=""

    if [ ! -s $1 ]; then
        return
    fi
    cat $1 | tee -a /opt/$_log
    if [ "$ON_RAMDISK" ]; then
        if mountpoint -q $MNT_DEBIAN; then
            _mounted=y
        elif ! _fs_try $LINENO mount $MNT_DEBIAN > /dev/null 2>&1; then
            return
        fi
        cat $1 >> $MNT_DEBIAN/opt/$_log
        if [ ! "$_mounted" ]; then
            umount $MNT_DEBIAN
        fi
    fi
}

_is_mounted() { # $1（マウントポイントか区画のデバイス名）がマウントされていれば真（/etc/mtab の文字列の一部ではなく、ちょうど同じもの）
    if [ -b "$1" ]; then
	awk -v d="$1" '$1 == d {f = 1} END {exit !f}' /proc/mounts
    else
	mountpoint -q "$1" 2>/dev/null
    fi
}

FS_TRY=""
_fs_try() { # _fs と同じだが、マウントできなくても U.sh を終えずに 1 を返す（起動の時の -r、一覧を作る時の仮のマウント等）
    local _r

    FS_TRY=y
    _fs "$@"
    _r=$?
    FS_TRY=""
    return $_r
}

_fs() { # mount・umount（$1 が数字なら行番号）。マウントできなければ、メッセージをログへ書いて U.sh を終える
    local _i _op _lineno _args="$1 $2 $3 $4 $5 $6 $7 $8 $9"
    local _mount_point=`echo $_args | awk '{print $NF}'`

    cd /opt
    if [ ! "$_mount_point" ] || [ "$_mount_point" = "/opt/" ] || [ -L "$_mount_point" ]; then
	return 0
    fi
    if [ "$KEEP_MNT_DEBIAN" ] && [ "$_mount_point" = "$MNT_DEBIAN" ]; then
	return 0
    fi
    if [ "`echo $1 | grep -E '^[0-9]+$'`" ]; then
	_lineno="${1}: "
	_args=`echo $_args | sed -e "s/$1 //"`
	_op=$2
    else
	_lineno=""
	_op=$1
    fi
    cat /dev/null > /tmp/tmp.$TMP_LABEL
    if [ "$_op" = "mount" ]; then
	# 前にマウントしたもの（ループのイメージ等）が残っていれば外してから（外せない＝使用中なら、そのまま使う）
	if [ -d $_mount_point ]; then
	    umount $_mount_point 2>/dev/null
	else
	    mkdir -p $_mount_point > /tmp/tmp.$TMP_LABEL 2>&1
	fi
    fi
    for _i in `seq 1 2`
    do
	if [ -s /tmp/tmp.$TMP_LABEL ]; then
	    echo "$_lineno`cat /tmp/tmp.$TMP_LABEL`"
	    break
	fi
	if ( [ "$_op" = "mount" ] && _is_mounted $_mount_point ) || ( [ "$_op" = "umount" ] && ! _is_mounted $_mount_point ); then
	    break
	fi
	$_args > /tmp/tmp.$TMP_LABEL 2>&1
	sleep 1
    done
    cd - > /dev/null
    if [ "$_op" = "mount" ] && ! _is_mounted $_mount_point; then
	echo "${_lineno}$_args: failed. `head -n1 /tmp/tmp.$TMP_LABEL`" >> /tmp/log.$TMP_LABEL
	if [ "$FS_TRY" ]; then
	    return 1
	fi
	_die
    fi
    return 0
}

_is_inst() { # $1（パッケージ名、完全一致）が入っていれば「ii  $1」を出力。$2が「+」なら、無い時だけ入れる
    if dpkg-query -W -f='${Status}' "$1" 2>/dev/null | grep -q 'ok installed$'; then
        if [ ! "$2" ]; then
            echo "ii  $1"
        fi
    elif [ "$2" = "+" ]; then
        echo "apt-get install -y $1"
        apt-get install -y $1
    fi
}

_unmount_part() {
    local _partition _PART=$1

    cd /opt
    fgrep $_PART /etc/mtab | awk '{print $1}' | \
	while read _partition
	do
	    _fs $LINENO umount $_partition
	done
}

_dd_build_img() {
    # dd で 0 を書かず、中身の無いファイルにする（mkfs.ext4 が要る所だけ書く。USB等の遅いSTRAGEで数分短くなる）
    mkdir -p $strage_opt
    echo "truncate -s $((BUILD_IMG_KB*1000)) $strage_opt/$BUILD.img"
    truncate -s $((BUILD_IMG_KB*1000)) $strage_opt/$BUILD.img
    echo "mkfs.ext4 -q $strage_opt/$BUILD.img"
    mkfs.ext4 -q $strage_opt/$BUILD.img
}

_mount_build_img() { # ビルド用イメージ（$BUILD.img）はSTRAGEにだけ置く（DEBIAN区画には置かない、圧縮した控えも作らない）
    local _build_img_kb

    _fs $LINENO mount $mnt_strage
    if [ -f $strage_opt/$BUILD.img ]; then
	_build_img_kb=$((`ls -l $strage_opt/$BUILD.img | awk '{print $5}'`/1000))
    else
	_build_img_kb=0
    fi
    if [ "$_build_img_kb" -lt "$BUILD_IMG_KB" ]; then # 無いか、BUILD_IMG_GB より小さい（壊れた、またはBUILD_IMG_GBを増やした）
	rm -f $strage_opt/$BUILD.img
	_dd_build_img
    fi
    _fs $LINENO umount $OPT_BUILD
    echo "mount -o loop $strage_opt/$BUILD.img $OPT_BUILD"
    _fs $LINENO mount -o loop $strage_opt/$BUILD.img $OPT_BUILD
}

# --- -u の区切りごとの経過時間（/opt/U_update.log、マニュアルの目安用。動きには関わらない）
LAP_START=""
LAP_PREV=""
_hms() { # $1 秒 -> h:mm:ss
    printf '%d:%02d:%02d' $(($1/3600)) $(($1%3600/60)) $(($1%60))
}

_lap_start() { # $1 オプション。機械の素性を見出しにして測り始める
    LAP_START=`date +%s`
    LAP_PREV=$LAP_START
    {
	echo
	echo "=== U.sh $1 `date '+%F %T'` `hostname` (`grep -m1 'model name' /proc/cpuinfo | sed -e 's/.*: //'`, `nproc` threads, ${MEM_TOTAL_GB}GB, BUILD_IMG_GB=$BUILD_IMG_GB, STRAGE=`findmnt -no SOURCE,FSTYPE --target $mnt_strage 2>/dev/null | head -n1`) ==="
	echo "time      +lap     total    step"
    } >> /opt/U_update.log
}

_lap() { # $1 区切りの名前（_lap_start の後だけ記録）
    local _now

    if [ ! "$LAP_START" ]; then
	return
    fi
    _now=`date +%s`
    printf '%s  %s  %s  %s\n' "`date +%T`" "`_hms $((_now-LAP_PREV))`" "`_hms $((_now-LAP_START))`" "$1" | tee -a /opt/U_update.log
    LAP_PREV=$_now
}

_echo_error() {
    if [ "$2" ]; then
	echo $1 : $2
    else
	echo $1
    fi
    
}

_compile() {
    local _new_kb _new_config _new_version _src_tmpfs_size
    local _new_gb _build_img_kb _skip_config
    local _TMPFS_OPT="-o size=${TMPFS_KB}K"

    if [ "$2" = "." ]; then
	_new_kb=$((RAMDISK_GB*1000*1000))
	_new_config=config-$UNAME_R
	_new_version="+${RAMDISK_GB}gb" #_20)　カーネル名からRAMDISKサイズを取得するための変数$_new_version（｢+4gb｣）を設定
    elif [ "$2" = "-" ]; then
	_new_kb=""
	_new_config=`dpkg -l | grep linux-image | awk '{print $2}' | grep "$KERNEL_VERSION" | grep amd64 | sort -r | sed -e 's/linux\-image/config/' | head -n1`
	if [ "$3" ]; then
	    _new_version=-${3}
	elif [ "$ON_UBUNTU" ]; then
	    _new_version=-generic
	else
	    _new_version=-amd64
	fi
    elif expr "$2" : "[0-9]*$" > /dev/null; then
	_new_gb=$2
	_new_kb=$((_new_gb*1000*1000))
	_new_config=""
	_new_version="+${_new_gb}gb" #_20)　カーネル名からRAMDISKサイズを取得するための変数$_new_version（｢+4gb｣）を設定
    else
	echo "$1: Please type '. U -k (.)(-)(??)'[Enter] : $LINENO" >> /tmp/log.$TMP_LABEL
	return
    fi
    _skip_config=$3
    _fs $LINENO mount $MNT_DEBIAN
    _mount_build_img
    apt update
    apt $BUILD_LIST -y 2>/tmp/error.$TMP_LABEL
    #_19)　buildイメージ位置（の相違による新規コンパイルを回避）を固定化するためloopデバイスとしてマウント
    if [ ! -e /usr/src/$LINUX_SOURCE ] || [ ! "`_is_inst make`" ]; then
	apt update
	apt install -y fakeroot libncurses-dev
	if [ -s /tmp/error.$TMP_LABEL ] && [ "$PURGE_LIST" ]; then
	    echo "$1: `cat /tmp/error.$TMP_LABEL` : $LINENO" | tee -a /tmp/log.$TMP_LABEL
	    dpkg --purge $PURGE_LIST
	fi
	if [ ! -e /usr/src/$LINUX_SOURCE ]; then
	    echo "cd /usr"
	    cd /usr
	    echo "umount /usr/src"
	    _fs $LINENO umount /usr/src
	    if [ "$ON_UBUNTU" ]; then
		if [ "$MEM_TOTAL_GB" -ge 8 ]; then
		    echo "mount -t tmpfs -o size=2200000K /dev/shm /usr/src"
		    _fs $LINENO mount -t tmpfs -o size=2200000K /dev/shm /usr/src
		fi
		if [ "`dpkg -l | grep linux-source 2>/dev/null`" ]; then
		    echo "dpkg --purge linux-source linux-source-$KERNEL_VERSION"
		    dpkg --purge linux-source linux-source-$KERNEL_VERSION
		fi
		echo "apt install -y linux-source"
		apt install -y linux-source linux-source
		echo "cd /usr/src"
		cd /usr/src
		mv $LINUX_SOURCE/$LINUX_SOURCE.tar.bz2 ..
		echo "Extracting $LINUX_SOURCE.tar.bz2 ..."
		tar xfj ../$LINUX_SOURCE.tar.bz2
		rm ../$LINUX_SOURCE.tar.bz2
	    elif [ "$ON_RAMDISK" ] || [ "$ON_WSL" ] || ( [ "$ON_NONE" ] && [ "$MEM_TOTAL_GB" -ge 8 ] ); then
		echo "mount -t tmpfs -o size=2200000K /dev/shm /usr/src" # Debian 13（6.12）では 1600000K では不足
		_fs $LINENO mount -t tmpfs -o size=2200000K /dev/shm /usr/src
		if [ "`dpkg -l | grep linux-source 2>/dev/null`" ]; then
		    echo "dpkg --purge linux-source linux-source-$KERNEL_VERSION linux-config-$KERNEL_VERSION"
		    dpkg --purge linux-source linux-source-$KERNEL_VERSION linux-config-$KERNEL_VERSION
		fi
		echo "apt install -y linux-source"
		apt install -y linux-source
		echo "cd /usr/src"
		cd /usr/src
		echo "Extracting $LINUX_SOURCE.tar.xz ..."
		xzcat $LINUX_SOURCE.tar.xz | tar xf -
		xzcat $LINUX_CONFIG/$CONFIG_NONE.xz > CONFIG_NONE
		rm $LINUX_SOURCE.tar.xz $LINUX_CONFIG/$CONFIG_NONE.xz
	    fi
	fi
	df
	_lap "$LINUX_SOURCE -> /usr/src"
    fi
    echo "cd /usr/src/$LINUX_SOURCE"
    if ! cd /usr/src/$LINUX_SOURCE; then # 展開に失敗（版の食い違い等）。make 等を続けても全部失敗するだけなので止める（2026-11-25）
	echo "$1: /usr/src/$LINUX_SOURCE: No such directory : $LINENO" | tee -a /tmp/log.$TMP_LABEL
	return 1
    fi
    if [ ! -f .config ]; then
	if [ "$_new_config" ]; then
	    echo "cp $MNT_DEBIAN/boot/$_new_config .config"
	    cp $MNT_DEBIAN/boot/$_new_config .config
	else
	    if [ "$ON_UBUNTU" ]; then
		echo "cp $MNT_DEBIAN/boot/$LINUX_CONFIG .config"
		cp $MNT_DEBIAN/boot/$LINUX_CONFIG .config
	    else
		echo "cp ../CONFIG_NONE .config"
		cp ../CONFIG_NONE .config
	    fi
	    sed -i .config \
		-e "s/CONFIG_DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT=y/# CONFIG_DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT is not set/" \
		-e s/CONFIG_BLK_DEV_RAM=m/CONFIG_BLK_DEV_RAM=y/ \
		-e s/CONFIG_EXT4_FS=m/CONFIG_EXT4_FS=y/
	fi
	if [ "$_new_kb" ]; then
	    sed -i .config -e s/`grep CONFIG_BLK_DEV_RAM_SIZE= .config`/CONFIG_BLK_DEV_RAM_SIZE=$_new_kb/
	else
	    sed -i .config \
		-e "s/CONFIG_DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT=y/# CONFIG_DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT is not set/"
	fi
	if [ "$ON_UBUNTU" ]; then
	    sed -i .config \
	    -e "s/CONFIG_INPUT_EVBUG=m/CONFIG_INPUT_EVBUG=n/" \
	    -e "s/debian\/canonical\-certs\.pem//" \
	    -e "s/debian\/canonical\-revoked\-certs\.pem//" \
	    -e "s/CONFIG_DEBUG_INFO_BTF=y/CONFIG_DEBUG_INFO_BTF=n/" \
	    -e "s/CONFIG_DEBUG_INFO_DWARF5=y/CONFIG_DEBUG_INFO_DWARF5=n/"
	fi
	if [ "$_skip_config" = "/" ]; then #_21)　BASE環境構築中、新規config設定への差分（make oldconfig）および個別設定（make menuconfig）はスキップ
	    echo 'make oldconfig'
	    make oldconfig <<EOF




EOF
	else
	    if [ "$2" != "." ]; then
		echo "make oldconfig"
		make oldconfig
	    fi
	    echo "make menuconfig"
	    make menuconfig
	fi
	echo "cp .config .."
	cp .config ..
	echo "make mrproper"
	make mrproper
	echo "cp ../.config ."
	cp ../.config .
    fi
    echo "ln -sf $OPT_BUILD /usr/src"
    ln -sf $OPT_BUILD /usr/src
    echo "mv -fv .config ../build"
    mv -fv .config ../build
    # デバッグ情報排除指定（choice: NONE / TOOLCHAIN_DEFAULT / DWARF4 / DWARF5 を全部切って NONE を明示）
    echo "scripts/config --file ../build/.config --disable DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT --disable DEBUG_INFO_DWARF4 --disable DEBUG_INFO_DWARF5 --enable DEBUG_INFO_NONE"
    scripts/config --file ../build/.config --disable DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT --disable DEBUG_INFO_DWARF4 --disable DEBUG_INFO_DWARF5 --enable DEBUG_INFO_NONE
    # モジュールを1つずつ署名しない（MODULE_SIG_ALL、署名は1つ数百バイト）。MODULE_SIG（署名の確かめ）は残し、
    # 署名の無いモジュールも読み込む（MODULE_SIG_FORCE は元から無効。読み込むとカーネルが「tainted」になるだけ）
    echo "scripts/config --file ../build/.config --disable MODULE_SIG_ALL"
    scripts/config --file ../build/.config --disable MODULE_SIG_ALL
    if [ "$KERNEL_DISABLE" ]; then # 今では使われないもの等を外す（/lib/modules を小さく）
        echo "scripts/config --file ../build/.config --disable" $KERNEL_DISABLE
        scripts/config --file ../build/.config `for _c in $KERNEL_DISABLE; do printf -- '--disable %s ' $_c; done`
    fi
    if [ "$KERNEL_ENABLE" ]; then
        echo "scripts/config --file ../build/.config --enable" $KERNEL_ENABLE
        scripts/config --file ../build/.config `for _c in $KERNEL_ENABLE; do printf -- '--enable %s ' $_c; done`
    fi
    if [ "$ON_UBUNTU" ]; then # Ubuntu の署名鍵（debian/canonical-*.pem）はソースに含まれないので常に空にする
	echo "scripts/config --file ../build/.config --set-str SYSTEM_TRUSTED_KEYS '' --set-str SYSTEM_REVOCATION_KEYS ''"
	scripts/config --file ../build/.config --set-str SYSTEM_TRUSTED_KEYS "" --set-str SYSTEM_REVOCATION_KEYS ""
    fi
    echo "make O=../build/ olddefconfig"
    make O=../build/ olddefconfig
    echo "make -j9 O=../build/ LOCALVERSION=$_new_version"
    make -j9 O=../build/ LOCALVERSION=$_new_version
    echo "make modules -j9 O=../build/ LOCALVERSION=$_new_version"
    make modules -j9 O=../build/ LOCALVERSION=$_new_version
    # INSTALL_MOD_STRIP=1: モジュールのシンボルは外す（RAM-*-base.gz を小さく）。DPKG_FLAGS=-d: ビルドに要るものの確かめを
    # しない（Ubuntu で GNU 版の coreutils に入れ替えた後、dpkg-checkbuilddeps が「unmet build dependencies:
    # coreutils-from-uutils」で止まった。必須の包みの一覧が uutils の時のままらしい。要るものは BUILD_LIST の build-dep で入れてある）
    echo "make bindeb-pkg INSTALL_MOD_STRIP=1 DPKG_FLAGS=-d O=../build/ LOCALVERSION=$_new_version"
    make bindeb-pkg INSTALL_MOD_STRIP=1 DPKG_FLAGS=-d O=../build/ LOCALVERSION=$_new_version
    _lap "linux-image-*$_new_version .deb"
    if [ ! -L $MNT_DEBIAN ]; then
	echo "mv /opt/*.deb /opt/linux-upstream* $MNT_DEBIAN/opt"
	mv /opt/*.deb /opt/linux-upstream* $MNT_DEBIAN/opt
    fi
    _fs $LINENO umount $OPT_BUILD
    _fs $LINENO umount $MNT_DEBIAN
}

_correct_uuid() { # ファイル$1の中の、区画$2のUUIDを区画$3のUUIDへ。$4（EFI区画）があれば$1をfstabとして/boot/efiの行を作り直す
    local _uuid_1=`blkid -c /dev/null -s UUID -o value $2`
    local _uuid_2=`blkid -c /dev/null -s UUID -o value $3`

    if [ ! "$_uuid_1" ] || [ ! "$_uuid_2" ]; then
        echo "_correct_uuid: $2 $3: UUID not found. : $LINENO" | tee -a /tmp/log.$TMP_LABEL
        return 1
    fi
    echo "sed $1 -e s/$_uuid_1/$_uuid_2/g"
    sed $1 -e "s/$_uuid_1/$_uuid_2/g" > /tmp/uuid.$TMP_LABEL
    if [ "$4" ]; then
        grep -ve /boot/efi -ve /mnt/cifs -ve /mnt/ext4 -ve /mnt/ntfs -ve /mnt/exfat /tmp/uuid.$TMP_LABEL > $1
        echo "UUID=`blkid -c /dev/null -s UUID -o value $4` /boot/efi vfat umask=0077 0 1" >> $1
    else
        cat /tmp/uuid.$TMP_LABEL > $1 # mv だと40_customの実行権が消える
    fi
}

_ram_entry() { # RAM-$1-base の起動項目を40_customへ（u_root= は rc.local が $MNT_DEBIAN の区画として使う）
    # Ubuntu の GRUB（2.14、UEFI）は、読み込むファイルを TPM に記録するために initrd を丸ごともう一度読み込むので、
    # RAM-*-base.gz（600MB 前後）で「out of memory」になった（Debian の GRUB では読めた）。UEFI の時は tpm を外す
    local _tpm=""

    if [ "$ON_UBUNTU" ]; then
        _tpm='	if [ "$grub_platform" = efi ]; then rmmod tpm; fi'
    fi
    if [ "`fgrep "'RAM-$1-base'" /etc/grub.d/40_custom`" ]; then
        if [ "$_tpm" ] && [ ! "`grep -A1 "^menuentry 'RAM-$1-base'" /etc/grub.d/40_custom | grep 'rmmod tpm'`" ]; then
            echo "rmmod tpm -> RAM-$1-base (/etc/grub.d/40_custom)" # 前に作った項目にも足す
            awk -v h="menuentry 'RAM-$1-base' {" -v t="$_tpm" '{print} $0 == h {print t}' /etc/grub.d/40_custom > /tmp/40.$TMP_LABEL
            cat /tmp/40.$TMP_LABEL > /etc/grub.d/40_custom # mv だと40_customの実行権が消える
        fi
        return
    fi
    cat <<EOF >> /etc/grub.d/40_custom
menuentry 'RAM-$1-base' {
$_tpm
	insmod part_gpt
	insmod part_msdos
	insmod ext2
	search --no-floppy --fs-uuid --set=root $DEBIAN_ROOT_UUID
	echo	'Loading base:vmlinuz-$1 from $DEBIAN_ROOT_UUID ...'
	linux	/boot/vmlinuz-$1 root=/dev/ram0 ro u_root=$DEBIAN_ROOT_UUID
	echo	'Loading initial ramdisk ...'
	initrd	/opt/RAM-$1-base.gz
	savedefault
}
EOF
}

_update_base() {
    local _new_kb _new_gb=$1
    local _new_base _new_module _new_kernel

    cd /opt
    _new_kernel=`ls -t /opt | grep linux-image | grep +${_new_gb}gb | head -n1`
    _new_module=`ls -t /lib/modules | grep +${_new_gb}gb | head -n1`
    _new_base=RAM-$_new_module-base

    if [ -f "/opt/$_new_base.gz" ] && [ ! "`ls -t /opt/$_new_base.gz /opt/$_new_kernel | head -n1 | grep '^/opt/linux'`" ]; then
        _ram_entry $_new_module # カーネルより新しいbaseがあれば作り直さない
        return
    fi
    echo "umount /tmp/shm0.$TMP_LABEL"
    _fs $LINENO umount /tmp/shm0.$TMP_LABEL
    echo "mount -t tmpfs -o size=${TEMPLATE_KB}K /dev/shm /tmp/shm0.$TMP_LABEL"
    _fs $LINENO mount -t tmpfs -o size=${TEMPLATE_KB}K /dev/shm /tmp/shm0.$TMP_LABEL
    echo "zcat /opt/$TEMPLATE_BASE.gz > /tmp/shm0.$TMP_LABEL/IMG1.img"
    zcat /opt/$TEMPLATE_BASE.gz > /tmp/shm0.$TMP_LABEL/IMG1.img
    echo "umount /tmp/loop1.$TMP_LABEL"
    _fs $LINENO umount /tmp/loop1.$TMP_LABEL
    echo "mount -o loop /tmp/shm0.$TMP_LABEL/IMG1.img /tmp/loop1.$TMP_LABEL"
    _fs $LINENO mount -o loop /tmp/shm0.$TMP_LABEL/IMG1.img /tmp/loop1.$TMP_LABEL
    _nvidia_module $_new_module
    # このカーネルのモジュールとヘッダだけを写す（名前で指すので、他のものを /tmp へ逃がさなくてよい）
    echo "tar cf - /lib/modules/$_new_module /usr/src/linux-headers-$_new_module 2>/dev/null| tar xf - -C /tmp/loop1.$TMP_LABEL"
    tar cf - /lib/modules/$_new_module /usr/src/linux-headers-$_new_module 2>/dev/null| tar xf - -C /tmp/loop1.$TMP_LABEL
    _new_kb=$((_new_gb*1000*1000))
    mkdir -p $strage_opt
    echo "truncate -s $((_new_kb*1000)) $strage_opt/IMG0.img"
    rm -f $strage_opt/IMG0.img
    truncate -s $((_new_kb*1000)) $strage_opt/IMG0.img
    echo "mkfs.ext4 -q $strage_opt/IMG0.img"
    mkfs.ext4 -q $strage_opt/IMG0.img
    echo "mount -o loop $strage_opt/IMG0.img /tmp/loop0.$TMP_LABEL"
    _fs $LINENO mount -o loop $strage_opt/IMG0.img /tmp/loop0.$TMP_LABEL
    echo "cd /tmp/loop1.$TMP_LABEL"
    cd /tmp/loop1.$TMP_LABEL
    echo "tar cf - * | tar xfp - -C /tmp/loop0.$TMP_LABEL"
    tar cf - * | tar xfp - -C /tmp/loop0.$TMP_LABEL
    echo "${_new_gb}GB-base > /tmp/loop0.$TMP_LABEL/etc/hostname"
    echo "${_new_gb}GB-base" > /tmp/loop0.$TMP_LABEL/etc/hostname
    sed -i /tmp/loop0.$TMP_LABEL/etc/hosts -e "s/$DEBIAN/${_new_gb}GB-base/"
    echo "cd /opt"
    cd /opt
    echo "umount /tmp/loop0.$TMP_LABEL /tmp/loop1.$TMP_LABEL /tmp/shm0.$TMP_LABEL"
    _fs $LINENO umount /tmp/loop0.$TMP_LABEL
    _fs $LINENO umount /tmp/loop1.$TMP_LABEL
    _fs $LINENO umount /tmp/shm0.$TMP_LABEL
    # gzip でなく xz で（GRUB は initrd を丸ごとメモリに読むので、小さいほどいい。Ubuntu の 1.13GB は UEFI で「Out of memory」、
    # BIOS で「the size of initrd is bigger than addr_max」になった）。カーネルは中身の形（xz）を見て展開する（RD_XZ）ので、
    # 名前は .gz のまま（GRUB の項目等を変えずに済む）。カーネルの xz は CRC64 を確かめられないので --check=crc32
    _is_inst xz-utils +
    echo "xz -T0 --check=crc32 -c $strage_opt/IMG0.img > /opt/$_new_base.gz"
    xz -T0 --check=crc32 -c $strage_opt/IMG0.img > /opt/$_new_base.gz
    _lap "$_new_base.gz"
    echo "rm -f $strage_opt/IMG0.img"
    rm -f $strage_opt/IMG0.img
    df /
    ls -l /opt/RAM-*
    _ram_entry $_new_module
}

_output_u() {
    local _u="." _diff _pkgs _pkg _try _prev
    local _bar="dpkg --get-selections | \
       grep -ve \
       deinstall$ -ve \
       efibootmgr -ve \
       grub -ve \
       grub-efi-amd64 -ve \
       grub-efi-amd64-bin -ve \
       grub-efi-amd64-signed -ve \
       grub-pc -ve \
       grub-pc-bin -ve \
       grub2-common -ve \
       linux-headers -ve \
       linux-image -ve \
       linux-modules -ve \
       mokutil -ve \
       os-prober -ve \
       shim-helpers-amd64-signed -ve \
       shim-signed -ve \
       shim-signed-common -ve \
       shim-unsigned -ve \
       linux-headers -ve \
       linux-image -ve \
       ^python3 -ve \
       ^libpython3 | \
       sed -e 's/\tinstall//' -e 's/\tpurge//'"
    local _to_u="grep -v \
       ^+++ | \
       grep ^+ | \
       cut -c2-"
    
    _ARG="$1 $2 $3 $4 $5 $6 $7 $8 $9"
    _lineno=`echo $_ARG | awk '{print $NF}'`
    _lineno=`echo $_lineno | grep -E '^[0-9]+$'` # egrep は Debian 13 で「obsolescent」と出る
    if [ ! "$1" ] || [ "$1" = "-p" ] || [ "$1" = "-i" ] || ( [ "$1" = "." ] && ( [ "$2" = "-p" ] || [ "$2" = "-i" ] ) ); then
	echo "Please type '. U -o . or filename'[Enter] : $_lineno" >> /tmp/log.$TMP_LABEL
	return
    elif [ "$2" != "$_lineno" ]; then
	if [ "$2" ] && [ ! -f /opt/$1 ]; then
	    echo "-o : /opt/$1: not exist. : $LINENO" >> /tmp/log.$TMP_LABEL
	    return
	fi
    fi
    echo $_bar | sh > /tmp/current.$TMP_LABEL
    if [ "$2" = "-p" ]; then
	_diff="diff -ru /opt/$1 /tmp/current.$TMP_LABEL"
	if [ "`$_diff`" ]; then
	    # dpkg --purge は依存関係を解かない。残す側（$1 にあるもの）が要しているもの（Depends や、mail-transport-agent の
	    # ような仮想パッケージ）は「dependency problems」で外さずに残す。外すもの同士の順番の都合で残ったものは、
	    # もう一度やれば外れるので10回まで（減らなくなったらやめる。依存が python3-pil→libraqm0→libharfbuzz0b→libgraphite2-3 のように深いと3回では残った）。それでも残ったものは、dpkg の理由（〜 depends on 〜）と一緒にログへ
	    # （記録に載せないもの（_bar の grep -v、shim-signed-common 等）が要しているものも残る。例: Debian 13 の openssl）
	    _try=0
	    _pkgs=`echo "$_diff | $_to_u | tr '\n' ' '" | sh`
	    _pkgs=`echo $_pkgs`
	    _prev=""
	    while [ "$_pkgs" ] && [ "$_pkgs" != "$_prev" ] && [ "$_try" -lt 10 ]
	    do
		echo "dpkg --purge $_pkgs"
		LC_ALL=C dpkg --purge $_pkgs 2>&1 | tee /tmp/purge.$TMP_LABEL # ログの理由を英語のまま grep するため
		echo $_bar | sh > /tmp/current.$TMP_LABEL
		_prev=$_pkgs
		_pkgs=`echo "$_diff | $_to_u | tr '\n' ' '" | sh`
		_pkgs=`echo $_pkgs`
		_try=$((_try+1))
	    done
	    if [ "$_pkgs" ]; then
		echo "-o $1 -p: not purged: $_pkgs : $_lineno" >> /tmp/log.$TMP_LABEL
		grep -e ' depends on ' -e 'error processing' /tmp/purge.$TMP_LABEL | sort -u | head -n20 >> /tmp/log.$TMP_LABEL
	    fi
	    _u="current.u"
	fi
    elif [ "$2" = "-i" ]; then
	_diff="diff -ru /tmp/current.$TMP_LABEL /opt/$1"
	if [ "`$_diff`" ]; then
	    _pkgs=`echo "$_diff | $_to_u | tr '\n' ' '" | sh`
	    # 入れられないもの（インストール候補が無い: 別の版の記録にしか無い、snapd のように Pin で止めている等）は
	    # 先に除く。1つでもあると apt-get が全体で止まり、1つずつ入れ直すことになって何百回もかかる
	    _ok=`LC_ALL=C apt-cache policy $_pkgs 2>/dev/null | awk '/^[^ ]/ {sub(/:$/, ""); p=$0} /^  Candidate:/ && $2 != "(none)" {print p}'`
	    if [ "$_ok" ]; then
		_none=""
		for _pkg in $_pkgs
		do
		    echo "$_ok" | grep -qx -e "$_pkg" -e "$_pkg:amd64" || _none="$_none $_pkg"
		done
		if [ "$_none" ]; then
		    echo "-o $1 -i: no candidate:$_none : $_lineno" >> /tmp/log.$TMP_LABEL
		    echo "no candidate:$_none"
		fi
		_pkgs=`echo $_ok`
	    fi
	    # 候補はあっても依存を満たせないもの（thunderbird のような snap へ移る移行用 deb は snapd を要する）も除く。
	    # apt-get -s（試すだけ）の「 名前 : Depends: 〜」の名前を外して、満たせるまで（記録に無いものが原因なら諦めて1つずつへ）
	    _try=0
	    while [ "$_pkgs" ] && [ "$_try" -lt 10 ]
	    do
		_bad=`LC_ALL=C apt-get install -s -y $_pkgs 2>&1 | awk '/^ [^ ]+ : / {print $1}' | sort -u`
		_left=""
		for _pkg in $_pkgs
		do
		    echo "$_bad" | grep -qx -e "$_pkg" || _left="$_left $_pkg"
		done
		_left=`echo $_left`
		if [ ! "$_bad" ] || [ "$_left" = "$_pkgs" ]; then
		    break
		fi
		echo "-o $1 -i: unsatisfiable:" $_bad ": $_lineno" >> /tmp/log.$TMP_LABEL
		echo "unsatisfiable:" $_bad
		_pkgs=$_left
		_try=$((_try+1))
	    done
	    # 設定の問い（debconf）は出さずに既定で答える。ダイアログが無いので Readline の問いになり、プロンプトも出ずに
	    # 止まって見える（2026-11-25、live の記録の kdump-tools の「既定で有効にするか」）。kdump-tools は既定が有効
	    # （起動時に crashkernel= でメモリを予約する）なので、先に無効と答えておく
	    echo "kdump-tools kdump-tools/use_kdump boolean false" | debconf-set-selections
	    echo "apt-get install -y $_pkgs"
	    if ! DEBIAN_FRONTEND=noninteractive apt-get install -y $_pkgs; then # それでも全体が止まったら、1つずつ入れる
		for _pkg in $_pkgs
		do
		    echo "apt-get install -y $_pkg"
		    DEBIAN_FRONTEND=noninteractive apt-get install -y $_pkg > /dev/null 2>&1 || echo "$_pkg: not installed. : $LINENO" >> /tmp/log.$TMP_LABEL
		done
	    fi
	    _u="current.u"
	fi
    elif [ "$2" ] && [ "$2" != "$_lineno" ]; then
	echo "$2: Please type '. U -o . or filename [-i)nstall or -p)urge]'[Enter] : $LINENO" >> /tmp/log.$TMP_LABEL
	return
    else
	_u=$1
    fi
    if ( [ "$_u" = BASE.u ] && [ -f /opt/BASE.u ] ) || ( [ "$_u" = DEBIAN.u ] && [ -f /opt/DEBIAN.u ] ); then
	echo "$_option: /opt/$_u: already exist. : $LINENO" >> /tmp/log.$TMP_LABEL
	# DEBIAN.uおよびBASE.uは存在しない場合のみ作成
    else
	if [ "$_u" = "." ]; then
	    _u="`hostname`".u
	elif [ ! "`echo $_u | grep \.u$`" ]; then
	    _u=${_u}.u
	fi
	echo $_bar | sh > /opt/$_u
	echo "-> /opt/$_u"
	if [ "$ON_RAMDISK" ]; then
	    _fs $LINENO mount $MNT_DEBIAN
	    cp /opt/$_u $MNT_DEBIAN/opt
	    echo "-> $MNT_DEBIAN/opt/$_u"
	fi
    fi
}

_change_root() {
    local _dir _chroot_dir=""

    if [ ! "$2" ] || [ "$2" = "$MNT_DEBIAN" ]; then # $2が無ければ$MNT_DEBIANをマウント
        if [ ! "$ON_RAMDISK" ]; then
            echo "$1: Not on a RAMDISK." >> /tmp/log.$TMP_LABEL
            return
        else
            _fs $LINENO mount $MNT_DEBIAN
        fi
        _chroot_dir=$MNT_DEBIAN
    else
        _dir=${2%/}
        if [ -e "$_dir" ]; then
            if ! mountpoint -q "$_dir"; then
                echo "$1: $_dir: not mounted." >> /tmp/log.$TMP_LABEL
            elif [ ! -e $_dir/dev ] || [ ! -e $_dir/proc ] || [ ! -e $_dir/sys ]; then
                echo "$1: $_dir: not a root system." >> /tmp/log.$TMP_LABEL
            else
                _chroot_dir=$_dir
            fi
        else
            echo "$1: $_dir: No such directory." >> /tmp/log.$TMP_LABEL
        fi
    fi
    if [ "$_chroot_dir" ]; then
        _is_inst arch-install-scripts +
        echo "arch-chroot $_chroot_dir"
        if [ "$3" ]; then
            arch-chroot $_chroot_dir /bin/sh -c "$3"
        else
            arch-chroot $_chroot_dir
        fi
        cd /opt
        _fs $LINENO umount -R $_chroot_dir
    fi
}

_list_strage() { # $1 から番号を振って、$2（ntfs・exfat・ext4 等）の区画を出す。/tmp/strage.* に「_i=次の番号 #番号)STRAGE=/mnt/$2 UUID=〜」
    # DEBIAN区画は出さない。マウント中の区画は外さずにそのまま空きを見る（前は外して、外したままだった）
    local _i=$1 _type=$2
    local _device _uuid _strage_gb _mp _tmp=/tmp/mount.$TMP_LABEL

    for _device in `blkid -o device -t TYPE=$_type 2>/dev/null | grep -v '^/dev/ram' | sort`
    do
	_uuid=`blkid -c /dev/null -s UUID -o value $_device`
	if [ ! "$_uuid" ] || [ "$_uuid" = "$DEBIAN_ROOT_UUID" ]; then
	    continue
	fi
	_mp=`findmnt -no TARGET $_device 2>/dev/null | head -n1`
	if [ "$_mp" ]; then
	    _strage_gb=$((`df -k $_mp | sed -n 2p | awk '{print $4}'`/1000/1000))
	    _mp=" [$_mp]"
	else # 空きを見るための仮のマウント（休止したWindowsのNTFS等は読むだけで）。できなくても止めない（_fs を使わない）
	    mkdir -p $_tmp
	    umount $_tmp 2>/dev/null
	    mount -o rw UUID=$_uuid $_tmp 2>/dev/null || mount -o ro UUID=$_uuid $_tmp 2>/dev/null
	fi
	if [ "$_mp" ]; then
	    :
	elif mountpoint -q $_tmp; then
	    _strage_gb=$((`df -k $_tmp | sed -n 2p | awk '{print $4}'`/1000/1000))
	    _fs $LINENO umount $_tmp
	else
	    echo "  -)$_type $_device $_uuid (can't mount)"
	    continue
	fi
	if [ "$_i" -lt 10 ]; then
	    echo " $_i)$_type $_device $_uuid ($_strage_gb GB)$_mp"
	else
	    echo "$_i)$_type $_device $_uuid ($_strage_gb GB)$_mp"
	fi
	_i=$((_i+1))
	echo "_i=$_i #$((_i-1)))STRAGE=/mnt/$_type UUID=$_uuid" >> /tmp/strage.$TMP_LABEL
    done
}

_list_parts() { # $2 以降の種類の区画を 1) から一覧に出し、番号を選んでもらう（$1 が「m」なら複数）。選んだ行（「STRAGE=/mnt/種類 UUID=〜」）を出す
    local _i=1 _n _type _sel _ok _multi=$1 _min=0

    shift
    cat /dev/null > /tmp/strage.$TMP_LABEL
    for _type in "$@"
    do
	if [ "$_type" = "ntfs" ]; then
	    _is_inst ntfs-3g + > $U_TTY
	elif [ "$_type" = "exfat" ]; then
	    _is_inst exfat-fuse + > $U_TTY
	fi
	_list_strage $_i $_type > $U_TTY
	. /tmp/strage.$TMP_LABEL
    done
    echo "  )<EXIT>" > $U_TTY
    if [ "$_multi" = "m" ]; then
	_min=1
    fi
    while :
    do
	if [ "$_multi" = "m" ]; then
	    _ask _sel "Input No (e.g. '2' or '2 5'):"
	else
	    _ask _sel "Input No:"
	fi
	_ok=y
	for _n in $_sel
	do
	    if [ ! "`echo $_n | grep -E '^[0-9]+$'`" ] || [ "$_n" -lt "$_min" ] || [ "$_n" -ge "$_i" ]; then
		_ok=""
	    fi
	done
	if [ "$_multi" != "m" ] && [ "`echo $_sel | wc -w`" -gt 1 ]; then
	    _ok=""
	fi
	if [ "$_ok" ]; then
	    break
	fi
	echo "$_sel: Invalid No. ($_min-$((_i-1)))" > $U_TTY
	if [ "$U_BATCH" ]; then # 聞き直せない
	    return
	fi
    done
    for _n in $_sel
    do
	if [ "$_n" -eq 0 ]; then
	    echo "0)"
	else
	    grep "#${_n})" /tmp/strage.$TMP_LABEL | head -n1 | awk -F\# '{print $2}'
	fi
    done
}

_release_part() { # UUID $1 の区画が $2 以外にマウントされていれば外す（-a: ntfs-3g・exfat-fuse は同じ区画を2か所にマウント
    # できず、-f の /mnt/番号 にマウントしたままの区画を STRAGE に選ぶと「already exclusively opened」で止まった）。
    # 外せなければ（使用中）1 を返す。外した -f の /mnt/番号 は RELINK に（後で STRAGE へのリンクにする）
    local _mp

    RELINK=""
    for _mp in `findmnt -rno TARGET -S UUID=$1 2>/dev/null`
    do
        if [ "$_mp" = "$2" ]; then
            continue
        fi
        echo "umount $_mp"
        umount $_mp 2>/dev/null
        if mountpoint -q $_mp; then
            echo "$_mp: busy." >> /tmp/log.$TMP_LABEL
            return 1
        fi
        case "$_mp" in
            /mnt/[0-9]*) RELINK="$RELINK $_mp" ;;
        esac
    done
}

add_strage() { # -a)
    local _opt_img _STRAGE_INFO _uuid _new _x
    local _i=1 _n STRAGE

    if [ "$ON_WSL" ]; then
	echo "$1: Not supported. : $LINENO" >> /tmp/log.$TMP_LABEL
	return
    fi
    if [ "$mnt_strage" = "/mnt/cifs" ]; then
	echo "$1: STRAGE=/mnt/cifs." >> /tmp/log.$TMP_LABEL
	return
    fi
    grep -v ^STRAGE= /opt/U.opt > /tmp/opt.$TMP_LABEL
    echo " 0)TYPE DEVICENAME UUID (free) [mounted]"
    # 一覧に無い番号は入れられない（もう一度聞く。空なら終える）
    _STRAGE_INFO=`_list_parts - ntfs exfat ext4`
    _n=`echo $_STRAGE_INFO | awk -F\) '{print $1}'`
    echo
    if [ ! "$_n" ]; then
	return
    elif [ "$_n" = "0" ]; then
	echo "STRAGE=\"\"" >> /tmp/opt.$TMP_LABEL
	mv /tmp/opt.$TMP_LABEL /opt/U.opt
	chmod 644 /opt/U.opt
	echo "$1: STRAGE=\"\" -> /opt/U.opt" >> /tmp/log.$TMP_LABEL
    elif [ "$_n" ]; then
	cd /opt
	_uuid=`echo $_STRAGE_INFO | sed -n 's/.* UUID=//p'`
	_new=`echo $_STRAGE_INFO | sed -n 's/.*STRAGE=\([^ ]*\).*/\1/p'`
	if ! _release_part $_uuid $_new; then # 使用中なら、U.opt を書き換える前にやめる
	    echo "$1: close it (or '. U -f -'), then '. U -a' again. : $LINENO" >> /tmp/log.$TMP_LABEL
	    return
	fi
	if [ "$ON_RAMDISK" ]; then
	    _opt_img=$MNT_DEBIAN
	    _fs $LINENO mount $_opt_img
	else
	    _opt_img=""
	fi
	if [ "$mnt_strage" != "$MNT_DEBIAN" ]; then
	    echo "umount $mnt_strage"
	    _fs $LINENO umount $mnt_strage
	fi
	_STRAGE_INFO=`echo $_STRAGE_INFO | awk -F\) '{print $2}'`
	echo $_STRAGE_INFO | sed -e 's/"//g' >> /tmp/opt.$TMP_LABEL # $STRAGEおよび$UUIDの新規設定値を保存
	mv /tmp/opt.$TMP_LABEL /opt/U.opt
	chmod 644 /opt/U.opt
	echo "$_STRAGE_INFO -> /opt/U.opt" >> /tmp/log.$TMP_LABEL
	UUID=""
	DEVICE=""
	if [ "$_opt_img" ]; then
	    cp /opt/U.opt $_opt_img/opt
	fi
	. /opt/U.opt
	mnt_strage=$STRAGE
	_set_strage_opt
	if [ ! -d "$mnt_strage" ]; then
	    mkdir $mnt_strage
	fi
	if [ "$UUID" ]; then
	    grep -ve /mnt/ext4 -ve /mnt/ntfs -ve /mnt/exfat -ve ^$ /etc/fstab > /tmp/fstab.$TMP_LABEL
	    echo "UUID=$UUID $mnt_strage `echo $mnt_strage | awk -F /mnt/ '{print $2}'` rw,noauto 0 1" >> /tmp/fstab.$TMP_LABEL 
	    mv /tmp/fstab.$TMP_LABEL /etc/fstab
	    systemctl daemon-reload
	fi
	_fs $LINENO mount $mnt_strage
	for _x in $RELINK # 外した -f の /mnt/番号 は、STRAGE へのリンクに
	do
	    rmdir $_x 2>/dev/null && ln -s $mnt_strage $_x && echo "ln -s $mnt_strage $_x"
	done
	if [ ! -d "$strage_opt" ]; then
	    mkdir -p $strage_opt
	fi
	_old_strage_opt
	df
	if [ "$((`df $mnt_strage | grep $mnt_strage | awk '{print $4}'`/1000/1000))" -gt 10 ]; then
	    if [ -f $_opt_img/opt/$BUILD.img ]; then
		echo "cp -iuv $_opt_img/opt/$BUILD.img $strage_opt"
		cp -iuv $_opt_img/opt/$BUILD.img $strage_opt
	    fi
	fi
	echo
	echo "STRAGE($strage_opt):"
	ls -l $strage_opt/ | grep -v total
	echo
    fi
}

change_root() { # -c)
    if ( [ "$ON_NONE" ] || [ "$ON_WSL" ] ) && [ ! "$2" ]; then
	echo "$1: Not on a RAMDISK. : $LINENO" >> /tmp/log.$TMP_LABEL
    fi
    _change_root $1 $2
}

make_usb() { # -m)
    local _SRC_DISK _DST_DISK _DST_EFI_PART _DST_ROOT_PART _DST_STRAGE_PART
    local _need _root_end _root_kb _src_kb _code _rescue_entry _yn

    if [ ! "$ON_RAMDISK" ]; then
        echo "$1: Not on a RAMDISK. : $LINENO" >> /tmp/log.$TMP_LABEL
        return
    fi
    if [ ! "$2" ] || ( [ "$3" ] && [ "$3" != "-" ] ); then
        echo "$1: Please type '. U -m devicename'[Enter] : $LINENO" >> /tmp/log.$TMP_LABEL # 「-」は前のファイルごとのコピーとの区別（今は dd だけなので無くてもよい）
        return
    fi
    if [ ! "`echo $2 | grep '^sd[a-z][a-z]*$'`" ]; then
        echo "$1: $2: sd? only. : $LINENO" >> /tmp/log.$TMP_LABEL
        return
    fi
    _DST_DISK=/dev/$2
    if [ ! -b "$_DST_DISK" ]; then
        echo "$1: $_DST_DISK: No such device. : $LINENO" >> /tmp/log.$TMP_LABEL
        return
    fi
    _SRC_DISK=/dev/`lsblk -no pkname $DEBIAN_ROOT_PART | head -n1`
    if [ "$_SRC_DISK" = "$_DST_DISK" ]; then
        echo "$1: $_DST_DISK: $DEBIAN_ROOT_PART is on it. : $LINENO" >> /tmp/log.$TMP_LABEL
        return
    fi
    _DST_EFI_PART=${_DST_DISK}1
    _DST_ROOT_PART=${_DST_DISK}3
    _DST_STRAGE_PART=${_DST_DISK}4
    cd /opt
    _fs $LINENO umount $MNT_DEBIAN
    _unmount_part $_SRC_DISK
    _unmount_part $_DST_DISK
    if mountpoint -q $MNT_DEBIAN || [ "`fgrep $_DST_DISK /etc/mtab`" ]; then
        echo "$1: $MNT_DEBIAN or $_DST_DISK: Busy. : $LINENO" >> /tmp/log.$TMP_LABEL
        return
    fi
    # EFI(100MiB)・BIOS boot(1MiB)・ルート(ROOT_GB)・STRAGE(残り)。残りが512MiBに満たなければ、ルートを残り全部にする
    # 区画ごと dd で写すので、ルートはDEBIAN区画より小さくできない。DEBIAN区画の方が大きければそれ（MiB単位に切り上げ＋1MiB）にする
    # （ファイルごとの tar は16GBのUSBメモリで1時間以上かかったので止めた。dd は15分）
    _root_kb=$((ROOT_GB*1024*1024))
    _src_kb=$((`blockdev --getsize64 $DEBIAN_ROOT_PART`/1024))
    if [ "$_src_kb" -gt "$_root_kb" ]; then
        _root_kb=$(( (_src_kb+2047)/1024*1024 ))
    fi
    # 足りなければ、消す前にやめる（EFI・BIOS boot・GPT の分）
    if [ "`blockdev --getsize64 $_DST_DISK`" -lt "$(( (_src_kb + 103*1024) * 1024 ))" ]; then
        echo "$1: $_DST_DISK: too small for $DEBIAN_ROOT_PART ($((_src_kb/1024))MiB). : $LINENO" >> /tmp/log.$TMP_LABEL
        return
    fi
    _need=$(( (_root_kb + (101 + 512)*1024) * 1024 ))
    if [ "`blockdev --getsize64 $_DST_DISK`" -lt "$_need" ]; then
        _root_end=0
        _DST_STRAGE_PART=""
    else
        _root_end=+${_root_kb}K
    fi
    lsblk -o NAME,SIZE,TYPE,FSTYPE,LABEL,MODEL $_DST_DISK
    if [ "$_DST_STRAGE_PART" ]; then
        echo "-> ${_DST_DISK}1:EFI(100MiB) ${_DST_DISK}2:BIOS boot(1MiB) $_DST_ROOT_PART:/($((_root_kb/1024))MiB) $_DST_STRAGE_PART:STRAGE($DST_STRAGE_FS)"
    else
        echo "-> ${_DST_DISK}1:EFI(100MiB) ${_DST_DISK}2:BIOS boot(1MiB) $_DST_ROOT_PART:/(rest)"
    fi
    _ask _yn "Erase $_DST_DISK and copy $DEBIAN_ROOT_PART? (Y/N): " N
    if [ "$_yn" != "Y" ] && [ "$_yn" != "y" ]; then
        return
    fi
    _is_inst gdisk +
    _is_inst dosfstools +
    echo "sgdisk -Z $_DST_DISK"
    sgdisk -Z $_DST_DISK > /dev/null 2>&1
    case "$DST_STRAGE_FS" in
        ext4) _code=8300 ;;
        *) _code=0700 ;;
    esac
    if [ "$_DST_STRAGE_PART" ]; then
        echo "sgdisk -n 1:0:+100M -t 1:ef00 -n 2:0:+1M -t 2:ef02 -n 3:0:$_root_end -t 3:8300 -n 4:0:0 -t 4:$_code $_DST_DISK"
        sgdisk -n 1:0:+100M -t 1:ef00 -n 2:0:+1M -t 2:ef02 -n 3:0:$_root_end -t 3:8300 -n 4:0:0 -t 4:$_code $_DST_DISK
    else
        echo "sgdisk -n 1:0:+100M -t 1:ef00 -n 2:0:+1M -t 2:ef02 -n 3:0:0 -t 3:8300 $_DST_DISK"
        sgdisk -n 1:0:+100M -t 1:ef00 -n 2:0:+1M -t 2:ef02 -n 3:0:0 -t 3:8300 $_DST_DISK
    fi
    if [ $? -ne 0 ]; then
        echo "$1: $_DST_DISK: sgdisk failed. : $LINENO" >> /tmp/log.$TMP_LABEL
        return
    fi
    udevadm settle
    sleep 2
    cat /dev/null > /tmp/error.$TMP_LABEL
    if [ "`blockdev --getsize64 $DEBIAN_ROOT_PART`" -gt "`blockdev --getsize64 $_DST_ROOT_PART`" ]; then
        echo "$1: $_DST_ROOT_PART is smaller than $DEBIAN_ROOT_PART. : $LINENO" >> /tmp/log.$TMP_LABEL
        return
    fi
    # 大きな塊で順に書くので、ファイルごと（小さなファイルが多い）より速い。空き領域も書くので、時間は区画の大きさで決まる
    # 先頭の4MiB（ext4 のスーパーブロック＝UUID がある）は最後に書き、すぐ UUID を変える。途中で止まっても（USBの不良・
    # 停電等）USB側に同じ UUID の ext4 が無いので、SSD の GRUB（search --fs-uuid）が USB の書きかけを選ばない
    echo "dd if=/dev/zero of=$_DST_ROOT_PART bs=4M count=1 conv=fsync" # 前に途中で止まった時の残り
    dd if=/dev/zero of=$_DST_ROOT_PART bs=4M count=1 conv=fsync 2>/dev/null
    echo "dd if=$DEBIAN_ROOT_PART of=$_DST_ROOT_PART bs=4M skip=1 seek=1 conv=fsync status=progress"
    if ! dd if=$DEBIAN_ROOT_PART of=$_DST_ROOT_PART bs=4M skip=1 seek=1 conv=fsync status=progress; then
        echo "dd failed." > /tmp/error.$TMP_LABEL
    else
        echo "dd if=$DEBIAN_ROOT_PART of=$_DST_ROOT_PART bs=4M count=1 conv=fsync; tune2fs -U random $_DST_ROOT_PART"
        if dd if=$DEBIAN_ROOT_PART of=$_DST_ROOT_PART bs=4M count=1 conv=fsync 2>/dev/null; then
            echo y | tune2fs -U random $_DST_ROOT_PART # 確かめ（e2fsck）は後。先にやると同じ UUID の時間が延びる
            e2fsck -f -p $_DST_ROOT_PART
            echo "resize2fs $_DST_ROOT_PART"
            resize2fs $_DST_ROOT_PART
        else
            echo "dd failed (first 4MiB)." > /tmp/error.$TMP_LABEL
        fi
    fi
    if [ -s /tmp/error.$TMP_LABEL ]; then
        echo "$1: `cat /tmp/error.$TMP_LABEL` : $LINENO" | tee -a /tmp/log.$TMP_LABEL
        return
    fi
    e2fsck -f -p $_DST_ROOT_PART
    echo "mkfs.vfat -F 32 -n EFI $_DST_EFI_PART"
    mkfs.vfat -F 32 -n EFI $_DST_EFI_PART > /dev/null
    if [ "$_DST_STRAGE_PART" ]; then
        echo "mkfs($DST_STRAGE_FS) $_DST_STRAGE_PART"
        case "$DST_STRAGE_FS" in
            ext4) mkfs.ext4 -q -F -L STRAGE $_DST_STRAGE_PART ;;
            vfat) mkfs.vfat -F 32 -n STRAGE $_DST_STRAGE_PART > /dev/null ;;
            exfat) _is_inst exfatprogs +; mkfs.exfat -n STRAGE $_DST_STRAGE_PART ;;
            ntfs) _is_inst ntfs-3g +; mkfs.ntfs -Q -L STRAGE $_DST_STRAGE_PART ;;
            *) wipefs -a $_DST_STRAGE_PART ;;
        esac
    fi
    udevadm settle
    _fs $LINENO mount $_DST_ROOT_PART /mnt/usb
    mkdir -p /mnt/usb/boot/efi
    _correct_uuid /mnt/usb/etc/fstab $DEBIAN_ROOT_PART $_DST_ROOT_PART $_DST_EFI_PART || return
    sed -i /mnt/usb/etc/fstab -e '/^[^#].*[[:space:]]swap[[:space:]]/s/^/#/' # SSDのスワップはUSBには無い
    _correct_uuid /mnt/usb/etc/grub.d/40_custom $DEBIAN_ROOT_PART $_DST_ROOT_PART || return
    rm -f /mnt/usb/opt/U.opt /mnt/usb/opt/U.diff # U.diff は以前の版が残したもの
    printf 'STRAGE=""\nNEXT%s=%sGB-rescue\nSH=%s\n' $RESCUE_GB $RESCUE_GB $SH > /mnt/usb/opt/U.opt
    _fs $LINENO mount $_DST_EFI_PART /mnt/usb/boot/efi
    _rescue_entry=`_entry_titles /mnt/usb | grep "^RAM-.*+${RESCUE_GB}gb-base$" | tail -n1`
    # BIOS（MBR＋BIOS boot区画）とUEFI（EFI/BOOT/BOOTX64.EFI）の両方。grub.cfg は共用。--no-nvram で今の機械の起動設定には触らない
    _change_root $1 /mnt/usb "
dpkg -s grub-pc-bin > /dev/null 2>&1 || apt-get install -y grub-pc-bin
dpkg -s grub-efi-amd64-bin > /dev/null 2>&1 || apt-get install -y grub-efi-amd64-bin
update-grub 2> /dev/null
grub-install --target=i386-pc $_DST_DISK
grub-install --target=x86_64-efi --efi-directory=/boot/efi --removable --no-nvram --no-uefi-secure-boot
grub-editenv /boot/grub/grubenv set 'saved_entry=$_rescue_entry'
"
    echo "-> $_DST_DISK (BIOS/UEFI, $_rescue_entry)"
}

desktop_select() { # -d)
    local _list=/tmp/desktop.$TMP_LABEL _sel _n _i _task _name _line _pairs="" _yn _from="" _opt
    # 使う RAMDISK の大きさ: RESCUE_GB（4GB）では最小の xfce4 でも収まらないので、EXTRA_GB（8GB、-u が作る）
    local _gb=${EXTRA_GB:-$((RESCUE_GB*2))}
    # debconf の問い（既定のディスプレイマネージャ等）・設定ファイルを置き換えるかの問いで止まらないように（既定の答え／今のファイルを残す）
    # （. U で読むので export せず、apt-get ごとに付ける。export は ( ) の中だけ）
    local _apt="env DEBIAN_FRONTEND=noninteractive apt-get -y -o Dpkg::Options::=--force-confdef -o Dpkg::Options::=--force-confold"

    if [ ! "$ON_RAMDISK" ] || [ ! "`hostname | grep GB-`" ] || [ "$RAMDISK_GB" -lt "$_gb" ]; then
        # SSD の上（RAMDISK_GB は RESCUE_GB）や 4GB-base で「Please boot 4GB-base」と出していた
        echo "$1: Please boot ${_gb}GB-base (RAM-*+${_gb}gb-base, '. U -n'; ${RESCUE_GB}GB is too small for a desktop). : $LINENO" >> /tmp/log.$TMP_LABEL
        return
    fi
    if [ ! "`hostname | grep GB-base$`" ]; then # デスクトップ等のホスト（8GB-KDE 等）: BASE.u へ戻してから入れる（起動し直さなくていい）
        if [ "`_in_gui`" ]; then
            # 戻すとデスクトップ（とその中の端末）ごと消えるので、テキストのコンソール（Ctrl+Alt+F3 等）か ssh から
            echo "$1: On `hostname`: log in on a text console (Ctrl+Alt+F3) and type '. U -d' there. : $LINENO" >> /tmp/log.$TMP_LABEL
            return
        fi
        if [ ! -f /opt/BASE.u ]; then
            echo "$1: /opt/BASE.u: not exist. : $LINENO" >> /tmp/log.$TMP_LABEL
            return
        fi
        _from=`hostname`
    fi
    _fs $LINENO mount $MNT_DEBIAN
    _fs $LINENO mount $mnt_strage
    if [ "$mnt_strage" = "$MNT_DEBIAN" ]; then
        echo "$1: Strage-area needed ('. U -a'). : $LINENO" >> /tmp/log.$TMP_LABEL
        return
    fi
    # 一覧（1行に「パッケージ 名前」）
    if [ "$ON_UBUNTU" ]; then
        echo $DESKTOP_LIST | tr ' ' '\n' | tr ':' ' ' > $_list
    else
        LANG=C tasksel --list-tasks | awk -F'\t' '{split($1, a, " "); if (a[2] ~ /.-desktop$/) {d = $2; gsub(/[^A-Za-z0-9]+/, "-", d); print a[2], d}}' > $_list
    fi
    if [ ! -s $_list ]; then
        echo "$1: No desktop list. : $LINENO" >> /tmp/log.$TMP_LABEL
        return
    fi
    echo " No)NAME (package) [image in STRAGE]"
    _n=1
    while read _task _name
    do
        if [ -f $strage_opt/${RAMDISK_GB}GB-$_name.img ]; then
            echo " $_n)$_name ($_task) [${RAMDISK_GB}GB-$_name.img]"
        else
            echo " $_n)$_name ($_task)"
        fi
        _n=$((_n+1))
    done < $_list
    echo "  )<EXIT>"
    _ask _sel "Input No (e.g. '2' or '2 5'):"
    for _i in $_sel
    do
        if [ ! "`echo $_i | grep -E '^[0-9]+$'`" ] || [ "$_i" -lt 1 ] || [ "$_i" -ge "$_n" ]; then
            echo "$1: $_i: Invalid No. : $LINENO" >> /tmp/log.$TMP_LABEL
            return
        fi
        _line=`sed -n ${_i}p $_list | tr ' ' ':'`
        case " $_pairs " in
            *" $_line "*) ;; # 同じ番号を2回
            *) _pairs="$_pairs $_line" ;;
        esac
    done
    if [ ! "$_pairs" ]; then
        return
    fi
    echo
    for _line in $_pairs
    do
        echo "-> $strage_opt/${RAMDISK_GB}GB-${_line#*:}.img"
    done
    if [ "$_from" ]; then
        echo "($_from -> BASE.u: the desktop stops now)"
    fi
    _ask _yn "Install and save? (Y/N): " N
    if [ "$_yn" != "Y" ] && [ "$_yn" != "y" ]; then
        return
    fi
    if [ "$_from" ]; then
        echo "systemctl isolate multi-user.target"
        systemctl isolate multi-user.target # ディスプレイマネージャとデスクトップを止める
        _output_u BASE.u -p $LINENO
        $_apt autoremove --purge < /dev/null
        if [ "$ON_UBUNTU" ]; then
            _desktop_ubuntu off
        fi
    fi
    if [ ! "`_on_line`" ]; then
        _net_restart
    fi
    cd /opt
    echo "apt-get update -y"
    apt-get update -y
    echo "apt-get upgrade -y"
    $_apt upgrade < /dev/null
    (export DEBIAN_FRONTEND=noninteractive; _install_standard < /dev/null)
    echo "apt-get install -y $JP_LIST"
    $_apt install $JP_LIST < /dev/null
    _output_u JP.u $LINENO # デスクトップを入れる前（2つ目からはここまで戻してから入れる）
    _n=0
    for _line in $_pairs
    do
        _task=${_line%%:*}
        _name=${_line#*:}
        if [ "$_n" -gt 0 ]; then
            _output_u JP.u -p $LINENO
            $_apt autoremove --purge < /dev/null
            if [ "$ON_UBUNTU" ]; then
                _desktop_ubuntu off
            fi
            if [ ! "`_on_line`" ]; then
                _net_restart
            fi
        fi
        echo "HOST:${RAMDISK_GB}GB-$_name"
        if [ "$ON_UBUNTU" ]; then
            case "$_task" in
                +*) _opt="" ;; # 「+」: 推奨も入れる
                *) _opt="$DESKTOP_APT_OPT" ;;
            esac
            echo "apt-get install -y $_opt `echo ${_task#+} | tr , ' '`"
            $_apt install $_opt `echo ${_task#+} | tr , ' '` < /dev/null # < /dev/null: 抜けた問いがあっても待たずにエラーで終わる
            _desktop_ubuntu on
        else
            echo "apt-get install -y task-$_task"
            $_apt install task-$_task < /dev/null
        fi
        # 保存する前に、起動の時（-r）と同じ設定（swapcaps の kxkbrc・dconf 等）と usilic を入れておく
        # （起動の時に書くだけだと、自動ログインがそれより先だと最初のログインで効かない）
        rm -f /tmp/dot_files.$TMP_LABEL
        DOT_FILES
        _put_usilic $1
        _save_host $1 $_name . # STRAGEの ?GB-名前.img（あれば上書き）
        _n=$((_n+1))
    done
    rm -f /opt/JP.u $MNT_DEBIAN/opt/JP.u
    echo "$1:$_pairs : $LINENO" >> /tmp/log.$TMP_LABEL
    # 最後に入れたもの（今の RAMDISK の中身）は、そのままそのホストとして起動できる
    _name=${RAMDISK_GB}GB-$_name
    _ask _yn "Start $_name now? (Y/n): " n
    if [ "$_yn" ] && [ "$_yn" != "Y" ] && [ "$_yn" != "y" ]; then
        echo "$1: '. U -n' to boot. : $LINENO" >> /tmp/log.$TMP_LABEL
        return
    fi
    echo $_name > /etc/hostname
    hostnamectl set-hostname $_name
    sed -i /etc/hosts -e "s/^127\.0\.1\.1[[:space:]].*/127.0.1.1\t$_name/"
    grep -v "^NEXT${RAMDISK_GB}=" /opt/U.opt > /tmp/opt.$TMP_LABEL # 次に起動する時も同じホスト（-n と同じ）
    echo "NEXT${RAMDISK_GB}=$_name" >> /tmp/opt.$TMP_LABEL
    cat /tmp/opt.$TMP_LABEL > /opt/U.opt
    cp /opt/U.opt $MNT_DEBIAN/opt
    if [ "$ON_UBUNTU" ] && command -v netplan > /dev/null 2>&1; then # レンダラが NetworkManager になっていれば、ここで移す
        echo "netplan apply"
        netplan apply
    fi
    echo "systemctl isolate graphical.target"
    systemctl isolate graphical.target
}

_usilic_libs() { # $2 の usilic に足りないライブラリ（ldd の not found）を、DEBIAN区画の dpkg の記録（/var/lib/dpkg/info/*.list）から引いて入れる（$1 オプション）
    local _lib _pkg _pkgs="" _missing=""

    for _lib in `ldd $2 | awk '/=> not found/ {print $1}'`
    do
        _pkg=`grep -l "/$_lib\$" $MNT_DEBIAN/var/lib/dpkg/info/*.list 2>/dev/null | head -n1 | sed -e 's@.*/@@' -e 's/\.list$//'`
        if [ ! "$_pkg" ]; then
            # DEBIAN区画に無ければ（-u が BASE.u へ戻した時に外れる）名前から推して、apt にあれば
            # （libSDL2_ttf-2.0.so.0 -> libsdl2-ttf-2.0-0、libfoo.so.6 -> libfoo6）
            _pkg=`echo $_lib | tr 'A-Z_' 'a-z-' | sed -e 's/\.so\.\([0-9]*\)$/-\1/' -e 's/^\(.*[a-z]\)-\([0-9]*\)$/\1\2/'`
            apt-cache show $_pkg > /dev/null 2>&1 || _pkg=""
        fi
        if [ "$_pkg" ]; then
            _pkgs="$_pkgs $_pkg"
        else
            _missing="$_missing $_lib"
        fi
    done
    if [ "$_missing" ]; then
        echo "$1:$_missing: package not found. : $LINENO" >> /tmp/log.$TMP_LABEL
    fi
    if [ "$_pkgs" ]; then
        if [ ! "`_on_line`" ]; then
            _net_restart
        fi
        echo "apt-get install -y$_pkgs"
        if ! apt-get install -y $_pkgs; then
            apt-get update -y
            apt-get install -y $_pkgs
        fi
        apt-get clean
    fi
}

_put_usilic() { # DEBIAN区画でビルドした usilic（~/.local）を RAMDISK のホームへ入れる（$1 オプション）。ライブラリが足りなければ入れる
    local _home=/home/$USR _src=$MNT_DEBIAN/home/$USR _rc _i _tools="" _libs
    local _line='PATH=$HOME/.local/bin:$PATH; export PATH'
    local _files=".local/bin/usilic .local/lib/usilic .local/share/applications/usilic.desktop .local/usilic-web"

    if [ ! "$ON_RAMDISK" ] || ! mountpoint -q $MNT_DEBIAN || [ ! -x $_src/.local/bin/usilic ]; then
        return
    fi
    if [ ! -x $_home/.local/bin/usilic ]; then # 前に入れて保存したもの（イメージの中）があれば、そのまま
        cd $_src
        echo "cd $_src; tar cf - $_files | tar xfp - -C $_home"
        tar cf - `ls -d $_files 2>/dev/null` | tar xfp - -C $_home
        cd /opt
        for _rc in .profile .bashrc
        do
            if [ ! "`grep -xF "$_line" $_home/$_rc 2>/dev/null`" ]; then
                echo "$_line" >> $_home/$_rc
                chown $USR:$USR $_home/$_rc
            fi
        done
        command -v update-desktop-database > /dev/null 2>&1 && update-desktop-database $_home/.local/share/applications
        echo "$1: usilic -> $_home/.local : $LINENO" >> /tmp/log.$TMP_LABEL
    fi
    # ログインした時に usilic（-g）を出す（デスクトップによって端末が出たり出なかったりするので、usilic は必ず）
    if [ -f $_home/.local/share/applications/usilic.desktop ]; then
        mkdir -p $_home/.config/autostart
        cp $_home/.local/share/applications/usilic.desktop $_home/.config/autostart
        chown -R $USR:$USR $_home/.config/autostart
    fi
    for _i in $USILIC_TOOLS
    do
        if [ ! "`_is_inst $_i`" ]; then
            _tools="$_tools $_i"
        fi
    done
    _libs=`ldd $_home/.local/bin/usilic 2>/dev/null | grep 'not found'`
    if [ "$_tools" ] || [ "$_libs" ]; then
        # （アイコンから起動すると、端末からと違って「shared libraries」のメッセージも出ずに何も起きない）
        for _i in `seq 1 30` # rc.local はネットにつながる前に動くことがある
        do
            if [ "`_on_line`" ]; then
                break
            fi
            sleep 2
        done
    fi
    if [ "$_tools" ]; then
        echo "$1: usilic:$_tools -> install : $LINENO" >> /tmp/log.$TMP_LABEL
        echo "apt-get install -y$_tools"
        if ! apt-get install -y $_tools < /dev/null; then
            apt-get update -y
            apt-get install -y $_tools < /dev/null
        fi
        apt-get clean
    fi
    if [ "$_libs" ]; then
        echo "$1: usilic: missing libraries -> install : $LINENO" >> /tmp/log.$TMP_LABEL
        _usilic_libs $1 $_home/.local/bin/usilic
    fi
}

_has_desktop() { # デスクトップ（X11 か Wayland のセッション）が入っていれば「desktop」
    if [ "`ls /usr/share/xsessions /usr/share/wayland-sessions 2>/dev/null | grep '\.desktop$'`" ]; then
        echo desktop
    fi
}

_in_gui() { # この U.sh がデスクトップの中（の端末）から動いていれば「gui」（祖先のプロセスの DISPLAY・WAYLAND_DISPLAY を見る。sudo は消すので）
    local _p=$$

    while [ "$_p" ] && [ "$_p" -gt 1 ]
    do
        if tr '\0' '\n' < /proc/$_p/environ 2>/dev/null | grep -qE '^(DISPLAY|WAYLAND_DISPLAY)='; then
            echo gui
            return
        fi
        _p=`awk '/^PPid:/ {print $2}' /proc/$_p/status 2>/dev/null`
    done
}

erase_base() { # -e)
    local _SSD_PART _SHM_PART _mounted_part _pkgs _yn _option=$1

    if [ ! "$ON_NONE" ] && [ ! "$ON_WSL" ]; then
        echo "$_option: Not on a NONE-KERNEL. : $LINENO" >> /tmp/log.$TMP_LABEL
        return
    fi
    if [ ! -f /opt/BASE.u ]; then
        echo "$_option: BASE.u: No such file.  Please type 'sudo touch /opt/BASE.u'[Enter] : $LINENO" >> /tmp/log.$TMP_LABEL
        return
    fi
    _ask _yn "Delete base? (Y/N): " N
    if [ "$_yn" != "Y" ] && [ "$_yn" != "y" ]; then
        return
    fi
    if cd /usr/src; then
        rm -f build CONFIG_NONE
        rm -rf linux-source-$KERNEL_VERSION linux-config-$KERNEL_VERSION
    fi
    cd /opt
    _fs $LINENO umount $OPT_BUILD
    _fs $LINENO umount $mnt_strage
    if [ "`_is_inst light-locker`" ] && [ "`ps auxw | grep light-locker | grep -v grep`" ]; then
        killall light-locker
    fi
    _pkgs=`dpkg -l | awk '$1 == "ii" && $2 ~ /^linux-image/ {print $2}' | grep -E '[+][0-9]+gb$'`
    if [ "$_pkgs" ]; then
        dpkg --purge $_pkgs
    fi
    _SSD_PART=`grep ^/dev /etc/mtab | grep -ve $DEBIAN_ROOT_PART -ve ^/dev/shm | awk '{print $1}'`
    _SHM_PART=`grep ^/dev/shm /etc/mtab | awk '{print $2}'`
    for _mounted_part in $_SSD_PART $_SHM_PART
    do
        _fs $LINENO umount $_mounted_part
    done
    _SSD_PART=`grep ^/dev /etc/mtab | grep -ve $DEBIAN_ROOT_PART -ve ^/dev/shm | awk '{print $1}'`
    _SHM_PART=`grep ^/dev/shm /etc/mtab | awk '{print $2}'`
    if [ "$_SSD_PART" ] || [ "$_SHM_PART" ]; then
        echo "$_option: $_SSD_PART $_SHM_PART: Busy! : $LINENO" >> /tmp/log.$TMP_LABEL
    else
        _output_u DEBIAN.u -p $LINENO
        echo "rm -f /opt/BASE.u"
        rm -f /opt/BASE.u
        echo "apt-get clean"
        apt-get clean
        echo "rm -rf /home/$USR/.cache"
        rm -rf /home/$USR/.cache
        echo "cp -fv /opt/grub.ORG /etc/default/grub"
        cp -fv /opt/grub.ORG /etc/default/grub
        head -n5 /etc/grub.d/40_custom > /tmp/40_custom.$TMP_LABEL
        cat /tmp/40_custom.$TMP_LABEL > /etc/grub.d/40_custom
        mkdir -p /tmp/bak.$TMP_LABEL
        cd /opt || return
        # カーネルの.deb（linux-image-*+??gb 等）も消す（-u でビルドし直す。ビルド用イメージはSTRAGEに残るので初回の1/4ほどで済む）
        echo "mv -fv *.sh U.opt DEBIAN.u grub.ORG U_update.log /tmp/bak.$TMP_LABEL"
        mv -fv *.sh U.opt DEBIAN.u grub.ORG U_update.log /tmp/bak.$TMP_LABEL 2>/dev/null
        echo "rm -rf /opt/* /usr/local/bin/U"
        rm -rf /opt/* /usr/local/bin/U
        cd /tmp/bak.$TMP_LABEL || return
        echo "mv -fv *.sh U.opt DEBIAN.u grub.ORG U_update.log /opt"
        mv -fv *.sh U.opt DEBIAN.u grub.ORG U_update.log /opt 2>/dev/null
        cd /opt
        echo "update-grub"
        update-grub
        _output_u . $LINENO
        echo "Please type 'sudo bash (or dash) /opt/U.sh'[Enter]" >> /tmp/log.$TMP_LABEL
    fi
    ls -l /opt | grep -v total
}

kernel_remake() { # -k)
    if ( [ "$ON_NONE" ] || [ "$ON_RAMDISK" ] ) && [ "$mnt_strage" = "$MNT_DEBIAN" ]; then # $BUILD.img を展開する場所
        echo "$1: Strage-area needed ('. U -a'). : $LINENO" >> /tmp/log.$TMP_LABEL
        return
    fi
    if [ "$ON_WSL" ] && [ "$2" = "." ]; then # WSL用カーネルのカレント設定はパス
        echo "$1: Please type '. U -k ??(GB)'[Enter]" >> /tmp/log.$TMP_LABEL
        return
    fi
    _is_inst openssl +
    _compile $1 $2 $3 $4
}

_entry_titles() { # $1/etc/grub.d/40_custom の起動項目名（1行に1つ）
    grep "^menuentry '" $1/etc/grub.d/40_custom 2>/dev/null | sed -e "s/^menuentry '\([^']*\)'.*/\1/"
}

next_host() { # -n)
    local _list=/tmp/next.$TMP_LABEL _entry _name _line _gb _n=1 _yn

    if [ "$ON_WSL" ]; then
        echo "$1: Not supported. : $LINENO" >> /tmp/log.$TMP_LABEL
        return
    fi
    _fs $LINENO mount $MNT_DEBIAN
    _fs $LINENO mount $mnt_strage
    cat /dev/null > $_list
    # ホスト（?GB-*.img と ?GB-base）: その大きさの RAM-*+?gb-base で起動し、rc.local が .img を展開する
    for _name in `ls $strage_opt $MNT_DEBIAN/opt 2>/dev/null | grep '^[0-9][0-9]*GB-.*\.img$' | sed -e 's/\.img$//' | sort -u` \
                 `_entry_titles $MNT_DEBIAN | sed -n 's/^RAM-.*+\([0-9][0-9]*\)gb-base$/\1GB-base/p' | sort -u`
    do
        _gb=${_name%%GB-*}
        if [ "$_gb" -gt "$MEM_TOTAL_GB" ] || [ ! "`_entry_titles $MNT_DEBIAN | grep "^RAM-.*+${_gb}gb-base$"`" ]; then
            continue # メモリが足りないか、そのカーネルが無い
        fi
        echo "H:$_name" >> $_list
    done
    _entry_titles $MNT_DEBIAN | grep -v '^RAM-' | sed -e 's/^/E:/' >> $_list
    while read _line
    do
        echo " $_n)${_line#*:}"
        _n=$((_n+1))
    done < $_list
    echo "  )<EXIT>"
    _ask _n "Input No:"
    case "$_n" in
        ''|*[!0-9]*) return ;;
    esac
    _line=`sed -n "${_n}p" $_list`
    if [ ! "$_line" ]; then
        return
    fi
    _name=${_line#*:}
    if [ "${_line%%:*}" = H ]; then
        _gb=${_name%%GB-*}
        grep -v "^NEXT${_gb}=" /opt/U.opt > /tmp/opt.$TMP_LABEL
        echo "NEXT${_gb}=$_name" >> /tmp/opt.$TMP_LABEL
        cat /tmp/opt.$TMP_LABEL > /opt/U.opt
        if [ "$ON_RAMDISK" ]; then
            cp /opt/U.opt $MNT_DEBIAN/opt
        fi
        echo "NEXT${_gb}=$_name -> /opt/U.opt"
        _name=`_entry_titles $MNT_DEBIAN | grep "^RAM-.*+${_gb}gb-base$" | tail -n1`
    fi
    # GRUB_DEFAULT=saved（-u で設定）なので、update-grub せず grubenv の saved_entry だけ書き換える
    echo "grub-editenv $MNT_DEBIAN/boot/grub/grubenv set saved_entry=$_name"
    grub-editenv $MNT_DEBIAN/boot/grub/grubenv set "saved_entry=$_name"
    if [ ! "`grep '^GRUB_DEFAULT=saved' $MNT_DEBIAN/etc/default/grub`" ]; then
        echo "$1: GRUB_DEFAULT is not 'saved' in $MNT_DEBIAN/etc/default/grub. : $LINENO" >> /tmp/log.$TMP_LABEL
    fi
    _ask _yn "Reboot now? (Y/n): " n
    if [ ! "$_yn" ] || [ "$_yn" = "Y" ] || [ "$_yn" = "y" ]; then
        cd /opt
        # STRAGE も外してから（exFAT・NTFS（FUSE）は、再起動の時に systemd が外すのを待たされることがある）
        if [ "$mnt_strage" ] && [ "$mnt_strage" != "$MNT_DEBIAN" ]; then
            _fs $LINENO umount $mnt_strage
        fi
        _fs $LINENO umount $MNT_DEBIAN
        sync
        reboot
    fi
}

output_u() { # -o)
    _output_u $2 $3 $4
}

_enabled_units() { # 有効なサービス（init.d スクリプトから systemd が生成したもの＝generated を含む）
    systemctl list-unit-files --type=service --state=enabled,generated --no-legend | awk '{print $1}' | grep -v '@\.service$'
}

_start_rc() { # next_host でホストイメージを展開した後、systemd で全サービスを初期化
    local _BASE_UNITS=`cat $MNT_DEBIAN/opt/rc_systemd.txt 2>/dev/null | tr '\n' ' '`
    local _unit

    cat /dev/null > /tmp/error.$TMP_LABEL
    if [ ! "$ON_UBUNTU" ] && [ -f "$NETWORK_MANAGER_CONF" ]; then # Debian: ifupdown から NetworkManager へ
	echo "systemctl stop networking.service"
	systemctl stop networking.service 2>/dev/null
	sed -i /etc/network/interfaces -e "s/allow-hotplug/#allow-hotplug/" -e "s/iface/#iface/" -e "s/#iface lo/iface lo/"
	if [ "`fgrep 'managed=false' $NETWORK_MANAGER_CONF`" ]; then
	    sed -i $NETWORK_MANAGER_CONF -e "s/managed=false/managed=true/"
	fi
	systemctl enable NetworkManager.service 2>/dev/null
    fi
    echo "systemctl daemon-reload"
    systemctl daemon-reload # 展開したイメージのユニット（init.d スクリプト分も含む）を読み込み直す
    if [ "$ON_UBUNTU" ] && command -v netplan > /dev/null 2>&1; then
        # Ubuntu: イメージの netplan（デスクトップでは 90-U-network-manager.yaml でレンダラが NetworkManager）を反映する。
        # しないと、NetworkManager を入れたイメージ（GNOME-Live 等）で、networkd が持っていた enp5s0 の IP アドレスが無くなった
        echo "netplan apply"
        netplan apply 2>> /tmp/error.$TMP_LABEL
    fi
    for _unit in `_enabled_units`
    do
	case "$_unit" in
	    rc-local.service|display-manager.service|gdm*|lightdm*|sddm*|wdm*|xdm*) # 自分自身（rc.local）と DM は除外
		continue ;;
	    dbus.service|dbus-broker.service|systemd-logind.service|systemd-journald*) # システムのバスは再起動しない
		# （再起動すると logind がバスから切れて応えなくなり、lightdm が「Failed to get list of logind seats:
		# Timeout was reached」で25秒待った。バスは設定の変化を自分で読み直す）
		continue ;;
	esac
	case " $_BASE_UNITS " in
	    *" $_unit "*) # BASE にもあるもの: 動いていれば再起動（設定がイメージ側に変わっているため）
		systemctl try-restart --no-block $_unit 2>> /tmp/error.$TMP_LABEL ;;
	    *) # イメージで追加されたもの: この起動中に一度も起動していなければ起動
		if [ "`systemctl show -p ExecMainStartTimestampMonotonic --value $_unit 2>/dev/null`" = "0" ]; then
		    echo "systemctl start $_unit"
		    systemctl start --no-block $_unit 2>> /tmp/error.$TMP_LABEL
		fi ;;
	esac
    done
    if [ -f "$NETWORK_MANAGER_CONF" ]; then
	echo "systemctl start NetworkManager.service"
	systemctl start --no-block NetworkManager.service 2>> /tmp/error.$TMP_LABEL
    fi
    # キーボードの設定（/etc/default/keyboard の XKBOPTIONS 等）を udev に読み直させてから X を起動する。
    # udev が起動の時に読んだのは土台（BASE）の /etc/default/keyboard で、イメージ側の ctrl:swapcaps が X の既定に入らなかった
    echo "udevadm trigger --subsystem-match=input --action=change"
    udevadm trigger --subsystem-match=input --action=change 2>> /tmp/error.$TMP_LABEL
    udevadm settle --timeout=10 2>/dev/null
    if [ -L /etc/systemd/system/display-manager.service ]; then # DM は最後に
	echo "systemctl start display-manager.service"
	systemctl start --no-block display-manager.service 2>> /tmp/error.$TMP_LABEL
    elif [ "`systemctl get-default`" = "graphical.target" ]; then
	echo "systemctl start graphical.target"
	systemctl start --no-block graphical.target 2>> /tmp/error.$TMP_LABEL
    fi
    if [ -s /tmp/error.$TMP_LABEL ]; then
	echo "`cat /tmp/error.$TMP_LABEL` : $LINENO" | tee -a /tmp/log.$TMP_LABEL
    fi
}

_tmp_debian() {
    if [ -d /tmp/$DEBIAN.$TMP_LABEL ]; then
	return
    fi
    if [ ! "$DEBIAN_ROOT_UUID" ]; then
	DEBIAN_ROOT_UUID=`fgrep -e 'remount-ro' /etc/fstab | awk -F= '{print $2}' | awk '{print $1}'`
    fi
    echo "mkdir /tmp/$DEBIAN.$TMP_LABEL"
    mkdir /tmp/$DEBIAN.$TMP_LABEL
    echo "cd /tmp/$DEBIAN.$TMP_LABEL"
    cd /tmp/$DEBIAN.$TMP_LABEL
    echo "mkdir -p $RAM_DIR_LIST"
    mkdir -p $RAM_DIR_LIST
    echo "mknod dev/console c 5 1"
    mknod dev/console c 5 1
    echo "chmod 600 dev/console"
    chmod 600 dev/console
    echo "mknod dev/null c 1 3"
    mknod dev/null c 1 3
    echo "chmod 666 dev/null"
    chmod 666 dev/null
    echo "chmod 777 tmp"
    chmod 777 tmp
    echo "ln -sf usr/bin usr/lib usr/lib64 usr/sbin ."
    ln -sf usr/bin usr/lib usr/lib64 usr/sbin .
    if [ ! "$1" ] || [ "$1" = "+" ]; then
	echo "mkdir etc"
	mkdir etc
	echo 'fgrep -ve /mnt/debian -ve $MNT_DEBIAN -ve /boot/efi -ve /mnt/ext4 -ve /mnt/ntfs -ve /mnt/exfat /etc/fstab | awk (/ -> /dev/ram, swap file -> #) > etc/fstab'
	# ルートの行は「/dev/ram / ext4 errors=remount-ro」（ON_RAMDISK はこれで見分ける。Ubuntu の fstab は /dev/disk/by-uuid/〜 で
	# errors=remount-ro も無い）。ルートの区画の中のスワップファイル（Ubuntu の /swap.img）は RAMDISK に無いので外す
	fgrep -ve /mnt/debian -ve $MNT_DEBIAN -ve /boot/efi -ve /mnt/ext4 -ve /mnt/ntfs -ve /mnt/exfat /etc/fstab | \
	    awk '$1 !~ /^#/ && $2 == "/" {print "/dev/ram / ext4 errors=remount-ro 0 1"; next} $1 ~ /^\// && $1 !~ /^\/dev\// && $3 == "swap" {print "#" $0; next} {print}' > etc/fstab
	echo 'echo "UUID=$DEBIAN_ROOT_UUID $MNT_DEBIAN ext4 rw,noauto	    0 1" >> etc/fstab'
	echo "UUID=$DEBIAN_ROOT_UUID $MNT_DEBIAN ext4 rw,noauto	    0 1" >> etc/fstab
	if [ "$1" = "+" ]; then
	    echo "mkdir bin.usr-is-merged lib.usr-is-merged sbin.usr-is-merged"
	    mkdir bin.usr-is-merged lib.usr-is-merged sbin.usr-is-merged
	fi
    fi
    echo "cd -"
    cd -
}

rc_local_only() { # -r)
    local _shm _next_host _img
    local _TMPFS_OPT="-o size=${TMPFS_KB}K"

    if [ -f "/opt/BASE.u" ]; then
        return
    fi
    timedatectl set-local-rtc 0
    # RAMDISK（ram0）の ext4 は root 用の予約（mkfs.ext4 の既定で5%、4GB で約210MB）を持たない。RAMDISK では使い道が無い
    if [ -b /dev/ram0 ]; then
        echo "tune2fs -m 0 /dev/ram0"
        tune2fs -m 0 /dev/ram0 > /dev/null
    fi
    if [ "$SHM_LIST" ] && [ "$MEM_TOTAL_GB" -ge "$RESCUE_GB" ]; then
        cd /
        mkdir -p /opt/shm.$TMP_LABEL
        for _shm in $SHM_LIST
        do
            mkdir -p /opt/shm.$TMP_LABEL/$_shm
            # 「.*」は「.」「..」も含む。uutils（Ubuntu 26.04 の rust-coreutils）の mv は「/home/.」を /home そのものとして
            # 写し、/home/home ができた。隠しファイルは「.[!.]*」「..?*」で（合うものが無ければ、そのまま mv のエラーになるだけ）
            mv /$_shm/* /$_shm/.[!.]* /$_shm/..?* /opt/shm.$TMP_LABEL/$_shm 2>/dev/null
            _fs_try $LINENO mount -t tmpfs $_TMPFS_OPT /dev/shm /$_shm # できなければ、元のディレクトリへ戻すだけ
            mv /opt/shm.$TMP_LABEL/$_shm/* /opt/shm.$TMP_LABEL/$_shm/.[!.]* /opt/shm.$TMP_LABEL/$_shm/..?* /$_shm 2>/dev/null
        done
    fi
    # 起動の時なので、マウントできなくても止めない（イメージが見つからず ?GB-base のまま起動する）
    _fs_try $LINENO mount $MNT_DEBIAN
    _fs_try $LINENO mount $mnt_strage
    cp $MNT_DEBIAN/opt/BASE.u /opt
    cd /opt
    _next_host=`grep "^NEXT${RAMDISK_GB}=" /opt/U.opt | tail -n1 | sed -e "s/^NEXT${RAMDISK_GB}=//"`
    if [ ! "$_next_host" ]; then
        _next_host=${RAMDISK_GB}GB-base
    fi
    if [ ! "`echo $_next_host | grep base$`" ]; then # ホストのイメージを展開（rescue はDEBIAN区画だけ、それ以外はSTRAGEを先に探す）
        # rescue は _save_host が必ずDEBIAN区画に置く。STRAGEにあるのは前の版の U.sh が置いた古いもの（使わない）
        if [ "$_next_host" = "${RAMDISK_GB}GB-rescue" ]; then
            if [ -f $MNT_DEBIAN/opt/$_next_host.img ]; then
                _img=$MNT_DEBIAN/opt/$_next_host.img
            fi
        elif [ -f $strage_opt/$_next_host.img ]; then
            _img=$strage_opt/$_next_host.img
        elif [ -f $MNT_DEBIAN/opt/$_next_host.img ]; then
            _img=$MNT_DEBIAN/opt/$_next_host.img
        fi
        if [ ! "$_img" ]; then
            echo "-r: $_next_host.img: not exist (booted as ${RAMDISK_GB}GB-base). : $LINENO" | tee -a /tmp/log.$TMP_LABEL
            _next_host=${RAMDISK_GB}GB-base
        fi
    fi
    if [ "$_img" ]; then
        echo "tar xfp $_img -> /"
        if _fs_try $LINENO mount -o loop,ro $_img /tmp/loop0.$TMP_LABEL; then
            cd /tmp/loop0.$TMP_LABEL
            # 起動中のRAMDISKのもの（fstab は rc.local が u_root= で書いた）とtar自身は上書きしない。イメージ自体は書き換えない
            tar cf - --exclude=etc/fstab --exclude=etc/rc.local --exclude=etc/network/interfaces --exclude=usr/bin/tar * | tar xfp - -C /
            cd /opt
            _fs $LINENO umount /tmp/loop0.$TMP_LABEL
        else
            _next_host=${RAMDISK_GB}GB-base
            _img=""
        fi
    fi
    DOT_FILES
    cat <<EOF > /tmp/hosts.$TMP_LABEL
127.0.0.1	localhost
127.0.1.1	$_next_host
EOF
    if [ "$BAK_HOST" ]; then
        echo $BAK_HOST|tr ' ' '\t' >> /tmp/hosts.$TMP_LABEL
        grep -ve "127.0.0.1" -ve "127.0.1.1" -ve "`echo $BAK_HOST|awk '{print $1}'`" /etc/hosts >> /tmp/hosts.$TMP_LABEL
    else
        grep -ve "127.0.0.1" -ve "127.0.1.1" /etc/hosts >> /tmp/hosts.$TMP_LABEL
    fi
    cp /tmp/hosts.$TMP_LABEL /etc/hosts
    if [ "$_img" ]; then
        echo $_next_host > /etc/hostname # /opt/U.optの「NEXT?=」の値をホスト名に設定
        hostnamectl set-hostname $_next_host
        _start_rc
    fi
    if [ "`_has_desktop`" ]; then # -i（取り込み）は廃止。デスクトップのあるホストには、無ければ入れる
        _put_usilic -r
    fi
    cd /opt
}

_save_host() {
    local _new_host _host_list _ram_img_kb _used_kb _free_kb _plus _bak="" _excl _x _dst _bld
    local _work_area=$WORK_AREA
    local _shm_kb _cap_kb _mod_kb _need_kb

    if [ "`hostname | grep GB-base$`" ] && [ "$2" = "." ]; then
        echo "$1: Can't save to base-host. : $LINENO" >> /tmp/log.$TMP_LABEL
        return
    fi
    _fs $LINENO mount $MNT_DEBIAN
    _fs $LINENO mount $mnt_strage
    if [ "$ON_UBUNTU" ] && [ ! "$ON_RAMDISK" ] && [ ! "$ON_NONE" ]; then
        _new_host=`hostname`
        if [ ! "`echo $_new_host|fgrep ${RAMDISK_GB}GB`" ]; then
            _new_host=${RAMDISK_GB}GB-$_new_host
        fi
        if [ "$2" = "+" ]; then
            _plus=+
        fi
    elif [ "$2" = "." ]; then
        _new_host=`hostname`
    elif [ "$ON_NONE" ]; then
        _new_host=${RAMDISK_GB}GB-rescue
    elif [ "$2" ] && [ "$3" = "." ]; then
        _new_host=${RAMDISK_GB}GB-${2#${RAMDISK_GB}GB-}
    elif [ "$2" ]; then
        _host_list=`ls -r $strage_opt $MNT_DEBIAN/opt 2>/dev/null | grep "^${RAMDISK_GB}GB-.*\.img$" | sed -e 's/\.img$//' | sort -u`
        _new_host=${RAMDISK_GB}GB-${2#${RAMDISK_GB}GB-}
        if [ "`echo "$_host_list" | grep -x "$_new_host"`" ]; then
            echo "$1: $_new_host: already exist. To overwrite, type '. U $1 $2 .'[Enter] : $LINENO" >> /tmp/log.$TMP_LABEL
            return
        fi
    fi
    if [ ! "$_new_host" ]; then
        echo "$1: Please type '. U $1 name' or '. U $1 .'[Enter] : $LINENO" >> /tmp/log.$TMP_LABEL
        return
    fi
    # STRAGEが無い（DEBIAN区画に保存する）時は、rescue の上書きだけ
    if ( [ ! "$ON_UBUNTU" ] || [ "$ON_RAMDISK" ] ) && [ "$mnt_strage" = "$MNT_DEBIAN" ] && [ "$_new_host" != "${RAMDISK_GB}GB-rescue" ]; then
        echo "$1: $_new_host: Strage-area needed ('. U -a'). Without it, only ${RAMDISK_GB}GB-rescue can be saved. : $LINENO" >> /tmp/log.$TMP_LABEL
        return
    fi
    # rescue-USB（DEBIAN区画がUSBメモリ）でSTRAGEが無い: USBメモリの上で4GBのイメージを作り、控えも置くと1時間以上かかるので、
    # 先にSTRAGEを設定してもらう（STRAGEがあれば、作るのも控えもSTRAGEで、USBメモリへは出来たイメージを1回写すだけ）
    if [ "$mnt_strage" = "$MNT_DEBIAN" ] && [ "$_new_host" = "${RAMDISK_GB}GB-rescue" ] && [ "`_on_usb`" ]; then
        echo "$1: $_new_host: on a USB memory, Strage-area needed (too slow without it). Type '. U -a', then '. U $1 $2'. : $LINENO" | tee -a /tmp/log.$TMP_LABEL
        return
    fi
    if [ "$4" = "-" ] || [ "$ON_NONE" ] || ( [ "$ON_UBUNTU" ] && [ ! "$ON_RAMDISK" ] ); then
        _work_area=opt
    fi
    echo "HOST:$_new_host"
    _fs $LINENO umount /usr/src
    if [ "`fgrep /usr/src /etc/mtab`" ]; then
        echo "$1: /usr/src: Busy. : $LINENO" >> /tmp/log.$TMP_LABEL
        return
    fi
    cd $mnt_strage
    echo "apt-get clean"
    apt-get clean
    if [ "$ON_NONE" ] || ( [ "$ON_UBUNTU" ] && [ ! "$ON_RAMDISK" ] ); then
        # 使用量（カーネルのモジュールと IMG_EXCLUDE を除く）の NUMERATOR/10 倍＋100MB
        # （RAMDISKに入るかは、作った後にイメージの中身の実際の使用量で確かめる）
        cd /
        _used_kb=`du -skxc [e-h]* root srv usr var 2>/dev/null | tail -n1 | cut -f1`
        _used_kb=$((_used_kb - `du -skxc usr/lib/modules $IMG_EXCLUDE 2>/dev/null | tail -n1 | cut -f1`))
        _ram_img_kb=$((_used_kb*NUMERATOR/10 + 100000))
        echo "used: ${_used_kb}KB -> img: ${_ram_img_kb}KB"
    else
        _ram_img_kb=`df | grep \/$ | awk '{print $3}'`
        _ram_img_kb=$((`_tmpfs_size $_ram_img_kb | tail -n1`*11/10)) # ルートシステム長の1.1倍
    fi
    # 保存先: rescue はDEBIAN区画（-m でUSBへそのまま写り、STRAGEが無くても起動できる）、それ以外はSTRAGE
    if [ "$_new_host" = "${RAMDISK_GB}GB-rescue" ]; then
        _dst=$MNT_DEBIAN/opt
    else
        _dst=$strage_opt
    fi
    # イメージを作る場所: rescue でSTRAGEがあればSTRAGE（出来てから保存先へ写す）、それ以外は保存先
    _bld=$_dst
    if [ "$_new_host" = "${RAMDISK_GB}GB-rescue" ] && [ "$mnt_strage" != "$MNT_DEBIAN" ]; then
        _bld=$strage_opt
    fi
    mkdir -p $_dst $_bld
    # rescue の上書き: 今のイメージを控えてから消す（失敗したらそこから戻す）。
    # STRAGEがあれば、STRAGEへ .BAK.img としてそのまま写す（圧縮しない。保存先（USBメモリ等）に控えを置かない）。
    # STRAGEが無ければ、保存先に .BAK.gz を圧縮して置く
    if [ "$_new_host" = "${RAMDISK_GB}GB-rescue" ] && [ -f $_dst/$_new_host.img ]; then
        if [ "$_bld" != "$_dst" ]; then
            echo "cp $_dst/$_new_host.img $_bld/$_new_host.BAK.img"
            if cp $_dst/$_new_host.img $_bld/$_new_host.BAK.img; then
                _bak=$_bld/$_new_host.BAK.img
            else
                rm -f $_bld/$_new_host.BAK.img
            fi
        else
            echo "gzip -c $_dst/$_new_host.img > $_dst/$_new_host.BAK.gz"
            if gzip -c $_dst/$_new_host.img > $_dst/$_new_host.BAK.gz; then
                _bak=$_dst/$_new_host.BAK.gz
            else
                rm -f $_dst/$_new_host.BAK.gz
            fi
        fi
        if [ ! "$_bak" ]; then
            echo "$1: $_new_host.BAK: failed (no space?). : $LINENO" | tee -a /tmp/log.$TMP_LABEL
            cd /opt
            return
        fi
        rm -f $_dst/$_new_host.img
        ls -l $_bak
    fi
    # 保存先（と作る場所）にイメージを置く空きがあるか
    for _x in `echo $_dst $_bld | tr ' ' '\n' | sort -u`
    do
        _free_kb=`df -k $_x/ | sed -n 2p | awk '{print $4}'`
        echo "free: ${_free_kb}KB ($_x)"
        if [ "$_ram_img_kb" -gt "$_free_kb" ]; then
            echo "$1: $_new_host: img ${_ram_img_kb}KB > free ${_free_kb}KB ($_x). : $LINENO" | tee -a /tmp/log.$TMP_LABEL
            _restore_bak $_bak $_dst/$_new_host.img
            cd /opt
            return
        fi
    done
    if [ "$_work_area" = "opt" ] || [ $_ram_img_kb -gt "`_tmpfs_size | tail -n1`" ]; then # _ram_img_kbが/dev/shmの空きよりも大きい
        # dd で 0 を書かず、中身の無いファイルにする（空きは上で確かめた）
        echo "truncate -s $((_ram_img_kb*1000)) $_bld/IMG0.img"
        rm -f $_bld/IMG0.img
        truncate -s $((_ram_img_kb*1000)) $_bld/IMG0.img 2>/tmp/error.$TMP_LABEL
        if [ $? -ne 0 ]; then
            echo "$1: IMG0.img: `tail -n1 /tmp/error.$TMP_LABEL` : $LINENO" | tee -a /tmp/log.$TMP_LABEL
            rm -f $_bld/IMG0.img
            _restore_bak $_bak $_dst/$_new_host.img
            cd /opt
            return
        fi
        mkfs.ext4 -q -m 0 $_bld/IMG0.img # root 用の予約（既定の5%）は要らない（展開するだけ。予約があると df が 100% に見えた）
        echo "mount -o loop $_bld/IMG0.img /tmp/loop0.$TMP_LABEL"
        _fs $LINENO mount -o loop $_bld/IMG0.img /tmp/loop0.$TMP_LABEL
    else
        _fs $LINENO umount /tmp/loop0.$TMP_LABEL
        echo "mount -t tmpfs -o size=${_ram_img_kb}K /dev/shm /tmp/shm0.$TMP_LABEL"
        _fs $LINENO mount -t tmpfs -o size=${_ram_img_kb}K /dev/shm /tmp/shm0.$TMP_LABEL
        echo "truncate -s $((_ram_img_kb*1000)) /tmp/shm0.$TMP_LABEL/IMG0.img"
        truncate -s $((_ram_img_kb*1000)) /tmp/shm0.$TMP_LABEL/IMG0.img
        mkfs.ext4 -q -m 0 /tmp/shm0.$TMP_LABEL/IMG0.img
        echo "mount -o loop /tmp/shm0.$TMP_LABEL/IMG0.img /tmp/loop0.$TMP_LABEL"
        _fs $LINENO mount -o loop /tmp/shm0.$TMP_LABEL/IMG0.img /tmp/loop0.$TMP_LABEL
    fi
    cd /
    # カーネルのモジュールは入れない（RAMの土台の側にある）。tar で外す（前は /tmp へ mv していたが、Debian 13 の /tmp は
    # tmpfs なので、途中で止まったり停電したりすると SSD の /usr/lib/modules が無くなった）
    _excl=""
    if [ "$_plus" = "+" ]; then # 今のカーネルのものだけ入れる
        for _x in usr/lib/modules/*
        do
            if [ "${_x##*/}" != "$UNAME_R" ]; then
                _excl="$_excl --exclude=$_x"
            fi
        done
    else
        _excl="--exclude=usr/lib/modules/*"
    fi
    for _x in $IMG_EXCLUDE # 「--exclude=home/*/…」はファイル名に当たらないので展開されない
    do
        _excl="$_excl --exclude=$_x"
    done
    echo "tar cf - $_excl [e-h]* root srv usr var | tar xfp - -C /tmp/loop0.$TMP_LABEL"
    tar cf - $_excl [e-h]* root srv usr var | tar xfp - -C /tmp/loop0.$TMP_LABEL 2>/tmp/error.$TMP_LABEL
    _tmp_debian $_plus
    cd /tmp/$DEBIAN.$TMP_LABEL
    tar cf - * | tar xfp - -C /tmp/loop0.$TMP_LABEL
    cd /opt
    df /tmp/loop0.$TMP_LABEL
    _used_kb=`df -k /tmp/loop0.$TMP_LABEL | sed -n 2p | awk '{print $3}'` # イメージの中身の実際の使用量
    # そのうちRAMDISK（ram0）に入る分（SHM_LIST のディレクトリは、-r で tmpfs の方に入る）
    _shm_kb=0
    for _x in $SHM_LIST
    do
        if [ -d /tmp/loop0.$TMP_LABEL/$_x ]; then
            _shm_kb=$((_shm_kb + `du -skx /tmp/loop0.$TMP_LABEL/$_x | cut -f1`))
        fi
    done
    _fs $LINENO umount /tmp/loop0.$TMP_LABEL
    if [ -s /tmp/error.$TMP_LABEL ]; then
        echo "$1: `cat /tmp/error.$TMP_LABEL` : $LINENO" | tee -a /tmp/log.$TMP_LABEL
        rm -f $_bld/IMG0.img
        _fs $LINENO umount /tmp/shm0.$TMP_LABEL
        _restore_bak $_bak $_dst/$_new_host.img
        return
    fi
    if [ -f $_bld/IMG0.img ]; then
        echo "mv $_bld/IMG0.img $_dst/$_new_host.img"
        if ! mv $_bld/IMG0.img $_dst/$_new_host.img; then
            echo "$1: $_new_host.img: failed (no space?). : $LINENO" | tee -a /tmp/log.$TMP_LABEL
            rm -f $_dst/$_new_host.img $_bld/IMG0.img
            _restore_bak $_bak $_dst/$_new_host.img
            return
        fi
    else
        echo "mv /tmp/shm0.$TMP_LABEL/IMG0.img $_dst/$_new_host.img"
        if ! mv /tmp/shm0.$TMP_LABEL/IMG0.img $_dst/$_new_host.img; then #_44)
            echo "$1: $_new_host.img: failed (no space?). : $LINENO" | tee -a /tmp/log.$TMP_LABEL
            rm -f $_dst/$_new_host.img
            _fs $LINENO umount /tmp/shm0.$TMP_LABEL
            _restore_bak $_bak $_dst/$_new_host.img
            return
        fi
        _fs $LINENO umount /tmp/shm0.$TMP_LABEL
    fi
    ls -l $_dst/${RAMDISK_GB}GB* $_bak
    df $_dst/
    # RAMDISKに収まるか（最後のチェック）。収まらなくてもイメージは残す（中を見て削るものを決められるように）
    # RAMDISKに入るのは「中身 − SHM_LIST の分 ＋ カーネルのモジュール（RAMの土台の側にある）＋ 余裕」。
    # RAMDISK の容量は ${RAMDISK_GB}GB ではなく、同じ大きさで mkfs.ext4 した時の df の大きさ（4GB で 3764096KB）
    _cap_kb=`_ramdisk_cap_kb $RAMDISK_GB`
    _mod_kb=`du -skc /usr/lib/modules/*+${RAMDISK_GB}gb 2>/dev/null | tail -n1 | cut -f1`
    _need_kb=$((_used_kb - _shm_kb + ${_mod_kb:-0} + RAMDISK_MARGIN_KB))
    echo "$_new_host.img: used ${_used_kb}KB - $SHM_LIST ${_shm_kb}KB + modules ${_mod_kb}KB + margin ${RAMDISK_MARGIN_KB}KB = ${_need_kb}KB / RAMDISK ${_cap_kb}KB"
    if [ ! "$_plus" ] && [ "$_cap_kb" ] && [ "$_need_kb" -gt "$_cap_kb" ]; then
        echo "$1: $_new_host.img: ${_need_kb}KB > ${RAMDISK_GB}GB RAMDISK ${_cap_kb}KB (over $((_need_kb - _cap_kb))KB, can't boot). : $LINENO" | tee -a /tmp/log.$TMP_LABEL
        echo "mount -o loop,ro $_dst/$_new_host.img /mnt; du -sh /mnt/* /mnt/usr/* /mnt/usr/share/*" >> /tmp/log.$TMP_LABEL
    fi
    cd /opt
}

_ramdisk_cap_kb() { # $1 GB の RAMDISK（_update_base と同じ大きさで mkfs.ext4）に入る量（KB、df の大きさ）
    # /tmp に作る（RAMDISKでは /opt は ram0 の中で、空きが少ないと mkfs.ext4 のジャーナル等が書けない。/tmp は tmpfs）
    local _img=/tmp/cap.$TMP_LABEL.img _mnt=/tmp/cap.$TMP_LABEL _kb=""

    rm -f $_img
    if truncate -s $(($1*1000*1000*1000)) $_img && mkfs.ext4 -q $_img; then # 中身の無いファイル（実際の場所はほとんど使わない）
        mkdir -p $_mnt
        if mount -o loop,ro $_img $_mnt; then
            _kb=`df -k $_mnt | sed -n 2p | awk '{print $2}'`
            umount $_mnt
        fi
        rmdir $_mnt
    fi
    rm -f $_img
    echo $_kb
}

_restore_bak() { # $1（.BAK.gz か .BAK.img）があれば $2 へ戻す
    if [ "$1" ] && [ -f "$1" ]; then
        case "$1" in
            *.gz)
                echo "zcat $1 > $2"
                zcat $1 > $2 ;;
            *)
                echo "cp $1 $2"
                cp $1 $2 ;;
        esac
    fi
}

_on_usb() { # DEBIAN区画がUSBのディスク（USBメモリ等）にあれば「usb」と出す
    local _disk

    _disk=`lsblk -ndo PKNAME $DEBIAN_ROOT_PART 2>/dev/null`
    if [ "$_disk" ]; then
        lsblk -ndo TRAN /dev/$_disk 2>/dev/null | grep -x usb
    fi
}

save_host() { # -s)
    if [ ! "$ON_RAMDISK" ] && ( [ ! "$ON_UBUNTU" ] || [ "$ON_NONE" ] || [ "$ON_TORAM" ] ); then
        echo "$1: Not on a RAMDISK. : $LINENO" >> /tmp/log.$TMP_LABEL
        return
    fi
    _save_host $1 $2 $3 $4
}

file_system() { # -f) マウントできる区画（-a と同じ一覧＋vfat）から番号を選んで /mnt/番号 へ。'. U -f -' で全部外す
    # fstab は書き換えない（再起動すれば無くなる）。もうどこかにマウントされている区画は、/mnt/番号 をそこへのリンクにする
    local _sel=/tmp/sel.$TMP_LABEL _line _n _uuid _dev _dir _mp _src

    if [ "$ON_WSL" ]; then
	echo "$1: Not supported. : $LINENO" >> /tmp/log.$TMP_LABEL
	return
    fi
    cd /opt
    if [ "$2" = "-" ]; then
	for _dir in `ls -d /mnt/[0-9]* 2>/dev/null`
	do
	    if [ -L $_dir ]; then
		echo "rm $_dir"
		rm -f $_dir
	    else
		echo "umount $_dir"
		_fs $LINENO umount $_dir
		if ! _is_mounted $_dir; then
		    rmdir $_dir 2>/dev/null
		fi
	    fi
	done
	return
    elif [ "$2" ]; then
	echo "$1 $2: Invalid option ('. U -f' or '. U -f -'). : $LINENO" >> /tmp/log.$TMP_LABEL
	return
    fi
    echo " No)TYPE DEVICENAME UUID (free) [mounted]"
    _list_parts m ntfs exfat ext4 vfat > $_sel
    echo
    while read _line
    do
	_n=${_line%%)*}
	_uuid=`echo $_line | sed -n 's/.* UUID=//p'`
	_dev=`blkid -U $_uuid`
	_dir=/mnt/$_n
	_mp=`findmnt -no TARGET $_dev 2>/dev/null | head -n1`
	# 前の /mnt/番号（番号は区画の増減で変わる）は外す
	if [ -L $_dir ]; then
	    rm -f $_dir
	elif _is_mounted $_dir; then
	    _src=`findmnt -no SOURCE $_dir`
	    if [ "$_src" != "$_dev" ]; then
		echo "umount $_dir ($_src)"
		_fs $LINENO umount $_dir
	    fi
	fi
	if [ "$_mp" = "$_dir" ]; then
	    echo "$_dir: $_dev (already mounted)"
	elif [ "$_mp" ]; then
	    rmdir $_dir 2>/dev/null
	    echo "ln -s $_mp $_dir"
	    ln -s $_mp $_dir
	else
	    mkdir -p $_dir
	    # 休止したWindows（高速スタートアップ）のNTFS等は読み書きではマウントできないので、読むだけで
	    if mount -o rw UUID=$_uuid $_dir 2>/tmp/error.$TMP_LABEL || mount -o ro UUID=$_uuid $_dir 2>>/tmp/error.$TMP_LABEL; then
		echo "mount $_dev $_dir (`findmnt -no FSTYPE,OPTIONS $_dir | cut -d, -f1`)"
	    else
		echo "$1: $_dev: can't mount. `head -n1 /tmp/error.$TMP_LABEL` : $LINENO" >> /tmp/log.$TMP_LABEL
		rmdir $_dir 2>/dev/null
	    fi
	fi
    done < $_sel
    rm -f $_sel
}

_list_mnt_no() { # -v: -f でマウントした /mnt/番号 の素性と中身
    local _dir _real _info

    for _dir in `ls -d /mnt/[0-9]* 2>/dev/null`
    do
	_real=`readlink -f $_dir`
	if _is_mounted $_real; then
	    _info=`findmnt -no SOURCE,FSTYPE $_real`
	    _info="$_info `blkid -c /dev/null -s UUID -o value ${_info%% *}` (`findmnt -no OPTIONS $_real | cut -d, -f1`)"
	else
	    _info="(not mounted)"
	fi
	if [ "$_real" != "$_dir" ]; then
	    _info="$_info -> $_real"
	fi
	echo
	echo "$_dir: $_info"
	ls -l $_dir/ | grep -v '^total'
    done
}

tm() { # -t)
    local _mnt _part
    
    if [ "$2" = "-" ]; then
	if [ "`fgrep /mnt/xxx_disk /etc/mtab`" ]; then
	    umount /mnt/xxx_disk
	fi
	for _mnt in `echo $MNTS`
	do
	    if [ "$_mnt" != "xxx_disk" ] && [ "`fgrep /mnt/$_mnt /etc/mtab`" ]; then
		echo "umount /mnt/$_mnt"
		umount /mnt/$_mnt
	    fi
	done
    elif [ ! "$2" ]; then
	for _mnt in `echo $MNTS`
	do
	    if [ ! -d /mnt/$_mnt ]; then
		mkdir /mnt/$_mnt
	    fi
	    if [ ! "`fgrep /mnt/$_mnt /etc/mtab`" ]; then
		mount /mnt/$_mnt > /dev/null 2>&1
		if [ ! -d /mnt/$_mnt/lost+found ]; then
		    umount /mnt/$_mnt > /dev/null 2>&1
		else
		    echo "mount /mnt/$_mnt"
		fi
	    fi
	done
	for _part in /dev/sd?; do [ -b $_part ] && /sbin/hdparm -S 255 $_part; done
    else
	echo "$1 $2: Invalid option. : $LINENO" >> /tmp/log.$TMP_LABEL
    fi
}

_get_usilic_repo() { # usilic のリポジトリ（~/git/usilic）を取る・新しくする
    local _home=`getent passwd "$USR" | cut -d: -f6`

    _is_inst git +
    if [ ! -d "$_home/git/usilic" ]; then
        echo "git clone https://github.com/haa15030/usilic.git $_home/git/usilic"
        su "$USR" -c 'git clone https://github.com/haa15030/usilic.git ~/git/usilic' || return 1
    else
        echo "git pull ($_home/git/usilic)"
        su "$USR" -c 'cd ~/git/usilic && git pull --ff-only'
    fi
}

_install_usilic_py() { # Python 版の usilic（src/python、RESCUE の ^J^R で U.sh を使う）を /usr/local/lib/usilic-py へ、usilic-py で起動
    # （RAM-*-base.gz を作る前に入れるので、?GB-base・rescue・デスクトップのどのホストにもある。python3 は _output_u が外さない）
    local _src=`getent passwd "$USR" | cut -d: -f6`/git/usilic/src/python

    _is_inst python3 +
    if [ ! -f $_src/usilic.py ]; then
        echo "-u: $_src/usilic.py: not exist (usilic-py not installed). : $LINENO" >> /tmp/log.$TMP_LABEL
        return
    fi
    if [ ! -f $_src/ush.py ] || [ ! -f $_src/usilic/ush/data/rc.local ]; then # リポジトリ（GitHub）のものが古い（Python 版の U.sh が
        # まだ無い）時は、入れ替えない（前は消してから写したので、今のもの・動いている ush.py の data/ も消えた）
        echo "-u: $_src: old (no ush.py), /usr/local/lib/usilic-py not replaced. : $LINENO" >> /tmp/log.$TMP_LABEL
        return
    fi
    rm -rf /usr/local/lib/usilic-py
    mkdir -p /usr/local/lib/usilic-py
    (cd $_src && tar cf - --exclude=__pycache__ usilic.py ush.py usilic) | tar xf - -C /usr/local/lib/usilic-py # ush.py: U.sh の Python 版
    printf '#!/bin/sh\nexec python3 /usr/local/lib/usilic-py/usilic.py "$@"\n' > /usr/local/bin/usilic-py
    chmod 755 /usr/local/bin/usilic-py
    echo "usilic-py -> /usr/local/lib/usilic-py (RESCUE: ^J^R)"
}

_setup_usilic() { # usilicの環境（tools/setup-debian.sh）を作り、ビルドにだけ要るもの（RESCUE_DEV_PURGE）を外す
    local _home=`getent passwd "$USR" | cut -d: -f6`
    local _bin _lib _pkgs

    _get_usilic_repo || return 1
    if [ "$ON_UBUNTU" ]; then # setup-debian.sh が入れる firefox を snap でなく Mozilla の apt リポジトリから（snap の
        _no_snap_ubuntu       # firefox は snap store から落とすので長く、RAMDISK には snapd も要る）
    fi
    sh "$_home/git/usilic/tools/setup-debian.sh" "$USR" $SETUP_OPT || return 1
    _bin=$_home/.local/bin/usilic
    if [ "$RESCUE_DEV_PURGE" ] && [ -x "$_bin" ]; then
        # usilicが使うライブラリ（libSDL2等）は、-dev を外しても autoremove で消えないよう手動で入れた扱いにする
        _pkgs=`ldd $_bin | awk '$2 == "=>" && $3 ~ /^\// {print $3}' | \
            while read _lib
            do
                dpkg -S $_lib 2>/dev/null || dpkg -S /usr$_lib 2>/dev/null
            done | cut -d: -f1 | sort -u`
        echo "apt-mark manual $_pkgs"
        apt-mark manual $_pkgs > /dev/null
        echo "apt-get purge -y $RESCUE_DEV_PURGE"
        apt-get purge -y $RESCUE_DEV_PURGE
        apt-get autoremove --purge -y
    fi
}

_toram_setup() { # Ubuntu（dracut）: GRUBの「TORAM」（起動オプション toram）で起動した時だけ、ルートの区画を tmpfs へコピーしてそこで動かす（26.04で動いた99ramdisk-rootの作り）
    local _mod=/usr/lib/dracut/modules.d/99ramdisk-root
    local _root_uuid=`findmnt -no UUID /`

    if [ ! "`_is_inst dracut`" ]; then
        echo "$1: dracut is not installed (initramfs-tools?). : $LINENO" >> /tmp/log.$TMP_LABEL
        return
    fi
    echo "create $_mod"
    mkdir -p $_mod
    cat <<'EOF' > $_mod/module-setup.sh
#!/bin/sh
# U.sh -u が作る。initrd の中で、ルートの区画（/sysroot）を tmpfs へコピーして差し替える（ramdisk-root.sh）

check() {
    return 0  # 常に組み込む（カーネルが更新されても入る。動くのは起動オプション toram がある時だけ）
}

depends() {
    echo "systemd"  # systemd モジュールに依存
}

install() {
    inst_multiple cp sed grep cat mkdir mount umount df du
    # スクリプトを initramfs に配置
    inst_simple "$moddir/ramdisk-root.sh" /bin/ramdisk-root.sh

    # systemd unit を initramfs に配置して有効化
    inst_simple "$moddir/ramdisk-root.service" \
        "${systemdsystemunitdir}/ramdisk-root.service"

    # initrd-switch-root.target の wants に追加
    mkdir -p "${initdir}/${systemdsystemunitdir}/initrd-switch-root.target.wants"
    ln -sf ../ramdisk-root.service \
        "${initdir}/${systemdsystemunitdir}/initrd-switch-root.target.wants/ramdisk-root.service"
}
EOF
    cat <<'EOF' > $_mod/ramdisk-root.service
[Unit]
Description=Copy rootfs to tmpfs and switch root to RAM
DefaultDependencies=no
After=initrd-root-fs.target
Before=initrd-switch-root.target
Wants=initrd-switch-root.target

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/bin/ramdisk-root.sh

[Install]
WantedBy=initrd-switch-root.target
EOF
    cat <<'EOF' > $_mod/ramdisk-root.sh
#!/bin/sh
# rootfs を tmpfs にコピーして switch_root 先を差し替える（U.sh -u が作る）
# 起動オプション toram がある時だけ動く（無ければ区画のまま起動。カーネルが更新されて initramfs が作り直されても同じ）。
# toram.size=（既定は80%）で tmpfs の上限。/etc/toram.skip に書いたトップの名前（1行に1つ、swap.img等）はコピーしない。
# tmpfs に入りきらない（コピーした後の空きが toram.free=（MB、既定1024）に満たない）時や、コピーに失敗した時は区画のまま起動する。

NEWROOT="/sysroot"   # dracut では $rootmnt の代わりに /sysroot を使う
RAMROOT="/ramroot"

if ! grep -qw toram /proc/cmdline; then
    exit 0
fi
size=`sed -n 's/.*toram\.size=\([^ ]*\).*/\1/p' /proc/cmdline`
[ -n "$size" ] || size=80%
free_mb=`sed -n 's/.*toram\.free=\([0-9]*\).*/\1/p' /proc/cmdline`
[ -n "$free_mb" ] || free_mb=1024

echo "=== ramdisk-root: start ($size) ==="
mkdir -p ${RAMROOT}
mount -t tmpfs -o size=$size,mode=0755 none ${RAMROOT} || exit 0

# 入りきるか: 区画の使用量（toram.skip の分を引く）＋toram.free ≦ tmpfs の大きさ
set -- `df -k ${NEWROOT} | sed -n 2p`
used_kb=$3
for name in `cat ${NEWROOT}/etc/toram.skip 2>/dev/null`; do
    if [ -e "${NEWROOT}/$name" ]; then
        set -- `du -skx "${NEWROOT}/$name"`
        used_kb=$((used_kb - $1))
    fi
done
set -- `df -k ${RAMROOT} | sed -n 2p`
ram_kb=$2
if [ $((used_kb + free_mb * 1024)) -gt $ram_kb ]; then
    echo "=== ramdisk-root: rootfs ${used_kb}KB + ${free_mb}MB > tmpfs ${ram_kb}KB, booting from disk ==="
    umount ${RAMROOT}
    exit 0
fi

echo "=== ramdisk-root: copying rootfs to RAM ==="
for d in ${NEWROOT}/* ${NEWROOT}/.[!.]*; do
    [ -e "$d" ] || [ -L "$d" ] || continue
    name=${d##*/}
    if grep -qx "$name" ${NEWROOT}/etc/toram.skip 2>/dev/null; then
        if [ -d "$d" ] && [ ! -L "$d" ]; then mkdir -p "${RAMROOT}/$name"; fi
        continue
    fi
    if ! cp -a "$d" ${RAMROOT}/; then
        echo "=== ramdisk-root: copy failed ($name), booting from disk ==="
        umount ${RAMROOT}
        exit 0
    fi
done

# コピーしていないスワップファイルの行は外す（tmpfs上のファイルはどのみちスワップにできない）
sed -i -e 's@^\(/[^[:space:]]*[[:space:]][[:space:]]*none[[:space:]][[:space:]]*swap[[:space:]]\)@#\1@' ${RAMROOT}/etc/fstab

mkdir -p ${RAMROOT}/usr/local/sbin
cat > ${RAMROOT}/usr/local/sbin/update-grub <<'EOS'
#!/bin/sh
echo "[INFO] tmpfs環境のため update-grub はスキップされました"
exit 0
EOS
chmod +x ${RAMROOT}/usr/local/sbin/update-grub

echo "=== ramdisk-root: switching /sysroot to RAM ==="
# /sysroot を tmpfs に差し替え（区画はその下に隠れたまま残る）
# initrd-switch-root.service がここを switch_root するので
mount --make-rprivate /
mount --move ${RAMROOT} ${NEWROOT}

echo "=== ramdisk-root: done ==="
EOF
    chmod 755 $_mod/module-setup.sh $_mod/ramdisk-root.sh
    chmod 644 $_mod/ramdisk-root.service
    if [ ! -f /etc/toram.skip ]; then
        printf 'swap.img\nlost+found\n' > /etc/toram.skip
    fi
    echo "dracut --force --kver $UNAME_R"
    dracut --force --kver $UNAME_R || return
    if [ ! "`fgrep "menuentry 'TORAM'" /etc/grub.d/40_custom`" ]; then # tmpfs で起動する項目（/boot/vmlinuz は最新のカーネル）
        cat <<EOF >> /etc/grub.d/40_custom
menuentry 'TORAM' {
	insmod part_gpt
	insmod part_msdos
	insmod ext2
	search --no-floppy --fs-uuid --set=root $_root_uuid
	echo	'Loading vmlinuz (toram) ...'
	linux	/boot/vmlinuz root=UUID=$_root_uuid ro toram
	echo	'Loading initial ramdisk ...'
	initrd	/boot/initrd.img
}
EOF
        chmod 755 /etc/grub.d/40_custom
    fi
    if [ "`grep '^GRUB_TIMEOUT_STYLE=hidden' /etc/default/grub`" ]; then # メニューを出す
        sed -i /etc/default/grub -e 's/^GRUB_TIMEOUT_STYLE=hidden/GRUB_TIMEOUT_STYLE=menu/' -e 's/^GRUB_TIMEOUT=0$/GRUB_TIMEOUT=5/'
    fi
    update-grub
    df -h /
    free -h
    echo "$1: Select 'TORAM' to run on tmpfs (from another GRUB: 'e' -> add 'toram' to the linux line -> Ctrl-x). : $LINENO" >> /tmp/log.$TMP_LABEL
}

_purge_ubuntu() { # Ubuntu: BASE.u を記録する前に、RAMDISK では要らないもの（UBUNTU_PURGE_LIST）を外す
    local _pkgs _keep

    if [ ! "$ON_UBUNTU" ] || [ ! "$UBUNTU_PURGE_LIST" ]; then
        return
    fi
    set -f # linux-tools-* 等を、ファイル名でなく dpkg-query の型として
    _pkgs=`dpkg-query -W -f='${db:Status-Abbrev} ${Package}\n' $UBUNTU_PURGE_LIST 2>/dev/null | awk '$1 == "ii" {print $2}'`
    set +f
    if [ ! "$_pkgs" ]; then
        return
    fi
    echo "apt-get purge -y" $_pkgs
    apt-get purge -y $_pkgs
    # ubuntu-server-minimal 等のメタが一緒に外れると、それが入れたもの（netplan.io・chrony・sudo-rs 等、残すもの）まで
    # autoremove で消えるので、今残っているものは手で入れた扱いに
    _keep=`apt-get -s autoremove 2>/dev/null | awk '$1 == "Remv" {print $2}'`
    if [ "$_keep" ]; then
        echo "apt-mark manual" $_keep
        apt-mark manual $_keep > /dev/null
    fi
}

_nvidia_gpu() { # NVIDIA の GPU があるか（PCI のベンダー 0x10de）
    cat /sys/bus/pci/devices/*/vendor 2>/dev/null | grep -qx 0x10de
}

_nvidia_base() { # NVIDIA_LIST（純正ドライバのカーネル側）を BASE に。BASE.u を作った後でも足す
    local _p _missing="" _keep

    if [ ! "$NVIDIA_LIST" ] || ! _nvidia_gpu; then
        return
    fi
    for _p in $NVIDIA_LIST
    do
        grep -q "^$_p[[:space:]]" /opt/BASE.u 2>/dev/null || _missing="$_missing $_p"
    done
    if [ ! "$_missing" ]; then
        for _p in $NVIDIA_LIST
        do
            _is_inst $_p + # BASE.u にあるのに外れていたら
        done
        return
    fi
    # BASE.u へ戻してから入れ、nouveau 用の firmware を外し、その状態を BASE.u に（依存の dkms・gcc 等も BASE.u に載り、
    # TEMPLATE を作る前の「BASE.u -p」で外れない）。前の BASE.u は BASE.u.bak に。前の版の nvidia-*・libnvidia-* は
    # BASE.u から除いてから戻す（外れる）
    chmod 644 /opt/BASE.u
    cp -p /opt/BASE.u /opt/BASE.u.bak
    grep -vE '^(lib)?nvidia-' /opt/BASE.u.bak > /opt/BASE.u
    _output_u BASE.u -p $LINENO
    echo "apt-get install -y --no-install-recommends $NVIDIA_LIST"
    if ! apt-get install -y --no-install-recommends $NVIDIA_LIST; then
        echo "-u: $NVIDIA_LIST: not installed. : $LINENO" >> /tmp/log.$TMP_LABEL
        return
    fi
    if [ "$NVIDIA_NOUVEAU_FW" ] && [ "`_is_inst $NVIDIA_NOUVEAU_FW`" ]; then
        echo "apt-get purge -y $NVIDIA_NOUVEAU_FW"
        apt-get purge -y $NVIDIA_NOUVEAU_FW
        _keep=`apt-get -s autoremove 2>/dev/null | awk '$1 == "Remv" {print $2}'` # linux-firmware 等のメタが一緒に外れても残す
        if [ "$_keep" ]; then
            echo "apt-mark manual" $_keep
            apt-mark manual $_keep > /dev/null
        fi
    fi
    rm -f /opt/BASE.u
    _output_u BASE.u $LINENO
    chmod 444 /opt/BASE.u
}

_nvidia_module() { # $1（作ったカーネル）向けに dkms で純正ドライバのモジュールを組み立てる（RAM-*-base.gz へ /lib/modules ごと写る）
    if [ ! "$NVIDIA_LIST" ] || [ ! "`_is_inst dkms`" ]; then
        return
    fi
    if [ ! -e /lib/modules/$1/build ] && [ "`ls /opt/linux-headers-${1}_*.deb 2>/dev/null`" ]; then
        echo "dpkg -i /opt/linux-headers-${1}_*.deb"
        dpkg -i /opt/linux-headers-${1}_*.deb > /dev/null
    fi
    echo "dkms autoinstall -k $1"
    dkms autoinstall -k $1
    if [ ! "`ls /lib/modules/$1/updates/dkms/nvidia.ko* 2>/dev/null`" ]; then
        echo "-u: /lib/modules/$1/updates/dkms/nvidia.ko: not built (dkms). : $LINENO" >> /tmp/log.$TMP_LABEL
    fi
}

_deb_installed() { # /opt の .deb（名前_版_arch.deb）と同じ版が入っているか
    local _p=`echo $1 | cut -d_ -f1` _v=`echo $1 | cut -d_ -f2`

    dpkg-query -W -f='${Status} ${Version}\n' $_p 2>/dev/null | grep -q " ok installed $_v\$"
}

_no_swap_file() { # /etc/fstab のスワップファイル（Ubuntu のインストーラが作る /swap.img 等）を止めて消す。
    # -m は DEBIAN区画を USB へ写すので、何GBもの使わないファイルが乗る（USB のスワップは遅くて使えず、RAMDISK でも使わない）。
    # スワップの区画（/dev/〜・UUID=〜）はそのまま
    local _f

    for _f in `awk '$1 ~ /^\// && $1 !~ /^\/dev\// && $3 == "swap" {print $1}' /etc/fstab`
    do
        echo "swapoff $_f; rm -f $_f (/etc/fstab)"
        swapoff $_f 2>/dev/null
        awk -v f="$_f" '!($1 == f && $3 == "swap")' /etc/fstab > /tmp/fstab.$TMP_LABEL
        cat /tmp/fstab.$TMP_LABEL > /etc/fstab
        rm -f $_f
    done
}

update_base() { # -u)
    local _new_gb _new_kernel _rescue_kernel _rescue_entry _first="" _yn

    if [ ! "$ON_NONE" ]; then
        echo "$1: Not on a NONE-KERNEL. : $LINENO" >> /tmp/log.$TMP_LABEL
        return
    fi
    export DEBIAN_FRONTEND=noninteractive
    _lap_start $1
    _no_swap_file
    cd /opt
    if [ ! -f /opt/BASE.u ]; then
        _output_u DEBIAN.u -p $LINENO
        _purge_ubuntu
        apt-get install -y $BASE_LIST #_９)　必要不可欠なパッケージ追加
        _output_u BASE.u $LINENO #_10)　BASE.u保存
        chmod 444 /opt/BASE.u
    fi
    _nvidia_base
    # Python 版の usilic（RESCUE）と python3 を、RAM-*-base.gz を作る前に（取れなくても止めない）
    _get_usilic_repo
    _install_usilic_py
    apt-get autoremove -y
    _output_u current.u $LINENO
    head -n5 /etc/grub.d/40_custom > /tmp/40_custom.$TMP_LABEL
    if [ "$GRUB_CUSTOM" ]; then
        echo "$GRUB_CUSTOM" >> /tmp/40_custom.$TMP_LABEL
    fi
    cat /tmp/40_custom.$TMP_LABEL > /etc/grub.d/40_custom
    chmod 755 /etc/grub.d/40_custom
    cd /opt
    # journald: SSD（NONE-KERNEL）は既定（/var/log/journal、前の起動の記録が残る）のまま。RAMDISK は土台の中の
    # journald.conf.d/U-ram.conf でメモリ（volatile）に。前に SSD の journald.conf を volatile にしていたら戻す
    if [ "`grep '^Storage=volatile' /etc/systemd/journald.conf 2>/dev/null`" ]; then
        echo "sed -i /etc/systemd/journald.conf -e s/^Storage=volatile/#Storage=auto/"
        sed -i /etc/systemd/journald.conf -e 's/^Storage=volatile/#Storage=auto/'
    fi
    if [ ! -f /opt/grub.ORG ]; then
        echo "cp /etc/default/grub /opt/grub.ORG"
        cp /etc/default/grub /opt/grub.ORG
    fi
    # 起動する項目は名前で選ぶ（GRUB_DEFAULT=saved、-n と -m は grubenv の saved_entry を書き換える）
    if [ ! "`grep '^GRUB_DEFAULT=saved' /etc/default/grub`" ]; then
        _first=y
        sed -i /etc/default/grub \
            -e "s/^GRUB_DEFAULT=.*/GRUB_DEFAULT=saved\nGRUB_SAVEDEFAULT=true/" \
            -e s/GRUB_DISABLE_OS_PROBER=false/GRUB_DISABLE_OS_PROBER=true/ \
            -e s/#GRUB_DISABLE_OS_PROBER=/GRUB_DISABLE_OS_PROBER=/ \
            -e s/#GRUB_TERMINAL=console/GRUB_TERMINAL=console/
        sed -i /etc/grub.d/30_uefi-firmware -e 's/OS_INDICATIONS=/#OS_INDICATIONS=/' 2>/dev/null
    fi
    _fs $LINENO umount /usr/src
    _fs $LINENO mount $mnt_strage
    if [ ! "`fgrep NONE-KERNEL /etc/grub.d/40_custom`" ]; then
        cat <<EOF >> /etc/grub.d/40_custom
menuentry 'NONE-KERNEL' {
	insmod part_gpt
	insmod part_msdos
	insmod ext2
	search --no-floppy --fs-uuid --set=root $DEBIAN_ROOT_UUID
	echo	'Loading base:vmlinuz-$UNAME_R from $DEBIAN_ROOT_UUID ...'
	linux	/boot/vmlinuz-$UNAME_R root=UUID=$DEBIAN_ROOT_UUID ro
	echo	'Loading initial ramdisk ...'
	initrd	/boot/initrd.img-$UNAME_R
	savedefault
}
EOF
    fi
    if [ "$ON_UBUNTU" ]; then # TORAM（dracut の時だけ。update-grub もする）
        _toram_setup $1
    fi
    if [ -L /usr/src/build ] && cd /usr/src; then
        rm -f build CONFIG_NONE
        rm -rf linux-source-$KERNEL_VERSION linux-config-$KERNEL_VERSION
    fi
    cd /opt
    if [ ! -f /opt/$TEMPLATE_BASE.gz ]; then # RAMDISKの土台（BASEの状態のルート、カーネルのモジュールは除く）
        _output_u BASE.u -p $LINENO
        free
        _fs $LINENO umount /tmp/shm0.$TMP_LABEL
        echo "mount -t tmpfs -o size=${TEMPLATE_KB}K /dev/shm /tmp/shm0.$TMP_LABEL"
        _fs $LINENO mount -t tmpfs -o size=${TEMPLATE_KB}K /dev/shm /tmp/shm0.$TMP_LABEL
        echo "truncate -s $((TEMPLATE_KB*1000)) /tmp/shm0.$TMP_LABEL/IMG1.img"
        truncate -s $((TEMPLATE_KB*1000)) /tmp/shm0.$TMP_LABEL/IMG1.img
        mkfs.ext4 -q /tmp/shm0.$TMP_LABEL/IMG1.img
        _fs $LINENO umount /tmp/loop1.$TMP_LABEL
        echo "mount -o loop /tmp/shm0.$TMP_LABEL/IMG1.img /tmp/loop1.$TMP_LABEL"
        _fs $LINENO mount -o loop /tmp/shm0.$TMP_LABEL/IMG1.img /tmp/loop1.$TMP_LABEL
        apt-get clean
        cd /
        # カーネルのモジュールとヘッダは tar で外す（/tmp へ mv すると、tmpfs なので途中で止まった時に SSD から消える）
        # ホームの git（-u が clone した usilic）・.local（前の rescue で作った usilic）・.cache は RAM の土台に要らない（167MB あった）
        # usr/src/nvidia-*: dkms が組み立てる元（約30MB）。組み立ては SSD でするので、RAMDISK には要らない
        echo "tar cf - --exclude='usr/lib/modules/*' --exclude='usr/src/linux-headers*' --exclude='usr/src/nvidia-*' --exclude='home/*/git' --exclude='home/*/.local' --exclude='home/*/.cache' [e-h]* root srv usr var | tar xfp - -C /tmp/loop1.$TMP_LABEL"
        tar cf - --exclude='usr/lib/modules/*' --exclude='usr/src/linux-headers*' --exclude='usr/src/nvidia-*' --exclude='home/*/git' --exclude='home/*/.local' --exclude='home/*/.cache' [e-h]* root srv usr var | tar xfp - -C /tmp/loop1.$TMP_LABEL 2>/tmp/error.$TMP_LABEL
        if [ -s /tmp/error.$TMP_LABEL ]; then
            echo "$1: `cat /tmp/error.$TMP_LABEL` : $LINENO" | tee -a /tmp/log.$TMP_LABEL
            cd /opt
            _fs $LINENO umount /tmp/loop1.$TMP_LABEL
            _fs $LINENO umount /tmp/shm0.$TMP_LABEL
            return
        fi
        rm -rf /tmp/loop1.$TMP_LABEL/var/log/journal
        _tmp_debian
        cd /tmp/$DEBIAN.$TMP_LABEL
        echo "tar cf - * | tar xfp - -C /tmp/loop1.$TMP_LABEL"
        tar cf - * | tar xfp - -C /tmp/loop1.$TMP_LABEL
        cd /opt
        #_34)　rc.local（RAMDISK稼働用）。起動オプション u_root= のUUIDを $MNT_DEBIAN の区画としてfstabへ書く（SSDでもUSBでも同じ土台で起動できる）
        echo "create /tmp/loop1.$TMP_LABEL/etc/rc.local"
        cat <<'EOF' > /tmp/loop1.$TMP_LABEL/etc/rc.local
#!/bin/sh
MNT=@MNT_DEBIAN@
_u=`sed -n 's/.*u_root=\([^ ]*\).*/\1/p' /proc/cmdline`
if [ "$_u" ]; then
    sed -i -e "s@^[^#[:space:]]*\([[:space:]][[:space:]]*$MNT[[:space:]]\)@UUID=$_u\1@" /etc/fstab
    systemctl daemon-reload
fi
for _i in `seq 1 10`
do
    if mountpoint -q $MNT; then
        break
    fi
    mount $MNT 2>/dev/null
    sleep 1
done
cp $MNT/opt/U.sh $MNT/opt/U.opt /opt
# Python版(ush.py)もDEBIAN区画の新しいものに(土台の中のものは、-uの時のもの)
if [ -f $MNT/usr/local/lib/usilic-py/ush.py ]; then
    rm -rf /usr/local/lib/usilic-py
    cp -a $MNT/usr/local/lib/usilic-py /usr/local/lib/
fi
for _i in `seq 1 10`
do
    if ! mountpoint -q $MNT; then
        break
    fi
    umount $MNT 2>/dev/null
    sleep 1
done
# Python版があればそれで(U.optのUSH=shならU.sh(旧シェル版)で)
_ush=`grep ^USH= /opt/U.opt | awk -FUSH= '{print $2}'`
if [ "$_ush" != "sh" ] && [ -f /usr/local/lib/usilic-py/ush.py ] && command -v python3 > /dev/null 2>&1; then
    python3 /usr/local/lib/usilic-py/ush.py -r
else
    _sh=`grep ^SH= /opt/U.opt | awk -FSH= '{print $2}'`
    ${_sh:-bash} /opt/U.sh -r
fi
EOF
        sed -i /tmp/loop1.$TMP_LABEL/etc/rc.local -e "s|@MNT_DEBIAN@|$MNT_DEBIAN|"
        chmod 755 /tmp/loop1.$TMP_LABEL/etc/rc.local
        # RAMDISK のログはメモリ（/run の下、/run の 10% まで）に。/var は SHM_LIST で tmpfs なのでどちらもメモリだが、
        # apt 等が使う /var の tmpfs と取り合わない。drop-in なので、journald.conf の書き方が変わっても効く
        mkdir -p /tmp/loop1.$TMP_LABEL/etc/systemd/journald.conf.d
        printf '[Journal]\nStorage=volatile\n' > /tmp/loop1.$TMP_LABEL/etc/systemd/journald.conf.d/U-ram.conf
        if [ "$ON_UBUNTU" ]; then # rc.local を動かすユニット（systemd の rc.local の互換（generator）より優先される。無くなっても動く）
            # Before=: イメージを展開し終わるまでログインさせない（systemd 258 で rc.local の互換が無くなり、getty が
            # rc-local.service を待たなくなった。展開の途中でログインすると、/home の付け替えの最中で / に出た）
            cat <<'EOF' > /tmp/loop1.$TMP_LABEL/etc/systemd/system/rc-local.service
[Unit]
Description=/etc/rc.local (U.sh -r)
ConditionFileIsExecutable=/etc/rc.local
After=network.target
Before=systemd-user-sessions.service getty.target

[Service]
Type=forking
ExecStart=/etc/rc.local
TimeoutSec=0
RemainAfterExit=yes
GuessMainPID=no

[Install]
WantedBy=multi-user.target
EOF
            mkdir -p /tmp/loop1.$TMP_LABEL/etc/systemd/system/multi-user.target.wants
            ln -sf ../rc-local.service /tmp/loop1.$TMP_LABEL/etc/systemd/system/multi-user.target.wants/rc-local.service
        fi
        _fs $LINENO umount /tmp/loop1.$TMP_LABEL
        echo "gzip -c /tmp/shm0.$TMP_LABEL/IMG1.img > /opt/$TEMPLATE_BASE.gz"
        gzip -c /tmp/shm0.$TMP_LABEL/IMG1.img > /opt/$TEMPLATE_BASE.gz
        _lap "$TEMPLATE_BASE.gz"
        _fs $LINENO umount /tmp/shm0.$TMP_LABEL
        df
    fi
    # RAMDISK（RESCUE_GB）用のカーネル
    _rescue_kernel=`ls -t /opt | grep linux-image | grep +${RESCUE_GB}gb | head -n1`
    if [ ! "$_rescue_kernel" ]; then #_17)　/optにRESCUE_GBカーネルパッケージが存在しない
        if [ "$mnt_strage" = "$MNT_DEBIAN" ]; then # $BUILD.img はSTRAGEにだけ置く
            echo "$1: To build linux-image-*+${RESCUE_GB}gb, Strage-area needed ('. U -a'). : $LINENO" >> /tmp/log.$TMP_LABEL
            return
        fi
        _compile $1 $RESCUE_GB /
        _rescue_kernel=`ls -t /opt | grep linux-image | grep +${RESCUE_GB}gb | head -n1`
        if [ ! "$_rescue_kernel" ]; then
            echo "$1: linux-image-*+${RESCUE_GB}gb: build failed. : $LINENO" >> /tmp/log.$TMP_LABEL
            return
        fi
    fi
    # EXTRA_GB のカーネル（無ければここでビルド。RAM-*+${EXTRA_GB}gb-base.gz は下の for で作る）
    if [ "$EXTRA_GB" ] && [ "$EXTRA_GB" != "$RESCUE_GB" ] && [ ! "`ls /opt | grep linux-image | grep +${EXTRA_GB}gb`" ]; then
        if [ "$mnt_strage" = "$MNT_DEBIAN" ]; then
            echo "$1: To build linux-image-*+${EXTRA_GB}gb, Strage-area needed ('. U -a'). : $LINENO" >> /tmp/log.$TMP_LABEL
        else
            _compile $1 $EXTRA_GB /
            cd /opt
            if [ ! "`ls /opt | grep linux-image | grep +${EXTRA_GB}gb`" ]; then
                echo "$1: linux-image-*+${EXTRA_GB}gb: build failed. : $LINENO" >> /tmp/log.$TMP_LABEL
                return
            fi
        fi
    fi
    cd /opt
    # 入っているのが /opt の .deb と同じ版でなければ入れる（前は「+8gb のどれかが入っていれば入れない」だったので、
    # 作り直した（KERNEL_DISABLE 等）カーネルが入らず、RAM-*-base.gz は前のカーネルのモジュールのままだった）
    if ! _deb_installed $_rescue_kernel; then
        echo "dpkg -i /opt/$_rescue_kernel"
        dpkg -i /opt/$_rescue_kernel > /dev/null
    fi
    rm -f /boot/initrd*+${RESCUE_GB}gb* # RAMDISKの土台（RAM-*-base.gz）を initrd として使うので要らない
    _update_base $RESCUE_GB
    for _new_kernel in `ls /opt | grep linux-image | grep -E '[+][0-9]+gb_' | grep -v +${RESCUE_GB}gb`
    do
        _new_gb=`echo $_new_kernel | awk -F+ '{print $2}' | awk -F_ '{print $1}' | sed -e 's/gb//'`
        if [ "$mnt_strage" = "$MNT_DEBIAN" ]; then
            echo "$1: To update 'RAM-*+${_new_gb}gb-base.gz', please setup strage-area. : $LINENO" >> /tmp/log.$TMP_LABEL
            continue
        fi
        if ! _deb_installed $_new_kernel; then
            echo "dpkg -i /opt/$_new_kernel"
            dpkg -i /opt/$_new_kernel > /dev/null
        fi
        _update_base $_new_gb
    done
    # 止まらずに ${RESCUE_GB}GB-rescue と usilic まで作る（rescue-USB を作るのが本分。usilic を含める形を既定にする）
    if [ ! -f /opt/${RESCUE_GB}GB-rescue.img ]; then
        ls -l /opt/RAM-* /opt/linux-image-*
        df -h / $mnt_strage
    fi
    # 4GB-rescue: tools/setup-debian.sh と RESCUE_LIST で作った環境（ビルドにだけ要るものは外す）を 4GB-rescue.img に保存する。
    # DEBIAN区画（SSDのroot）はこの環境のまま残す
    if [ ! -f /opt/${RESCUE_GB}GB-rescue.img ]; then
        if [ "`_is_inst libelf-dev`" ]; then # カーネルのビルドに使ったもの（build-dep等）が残っていれば、BASEへ戻して外す
            _output_u BASE.u -p $LINENO
            apt-get autoremove --purge -y
        fi
        apt-get update -y
        apt-get upgrade -y
        if ! _setup_usilic; then
            echo "$1: tools/setup-debian.sh failed. : $LINENO" >> /tmp/log.$TMP_LABEL
            _lap "(stopped) tools/setup-debian.sh failed"
            return
        fi
        _lap "usilic (tools/setup-debian.sh)"
        echo "apt-get install -y $RESCUE_LIST"
        apt-get install -y $RESCUE_LIST
        if [ "$RESCUE_LIST_NOREC" ]; then
            echo "apt-get install -y --no-install-recommends $RESCUE_LIST_NOREC"
            apt-get install -y --no-install-recommends $RESCUE_LIST_NOREC
        fi
        if [ "$RESCUE_KERNEL_DEV" ] && [ "$BUILD_LIST" ]; then # カーネルを組み立てる環境も
            echo "apt-get $BUILD_LIST -y; apt-get install -y fakeroot libncurses-dev openssl"
            apt-get $BUILD_LIST -y
            apt-get install -y fakeroot libncurses-dev openssl
        fi
        _output_u ${RESCUE_GB}GB-rescue.u $LINENO # 何が入っているかの記録
        _save_host $1 rescue
        _lap "${RESCUE_GB}GB-rescue.img"
        if [ "$RESCUE_DEV_PURGE" ]; then # SSDのrootでは、またusilicをビルドできるように戻す
            echo "apt-get install -y $RESCUE_DEV_PURGE"
            apt-get install -y $RESCUE_DEV_PURGE
        fi
        if [ ! -s $MNT_DEBIAN/opt/${RESCUE_GB}GB-rescue.img ]; then # _save_host は rescue をDEBIAN区画に置く（STRAGEではない）
            echo "$1: ${RESCUE_GB}GB-rescue.img: not created. : $LINENO" >> /tmp/log.$TMP_LABEL
            return
        fi
    fi
    rm -f /opt/current.u
    update-grub
    _lap "done"
    _rescue_entry=`_entry_titles / | grep "^RAM-.*+${RESCUE_GB}gb-base$" | tail -n1`
    if [ "$_rescue_entry" ]; then
        echo "grub-set-default '$_rescue_entry'"
        grub-set-default "$_rescue_entry"
    fi
    grep -v ^NEXT${RESCUE_GB}= /opt/U.opt > /tmp/opt.$TMP_LABEL
    echo "NEXT${RESCUE_GB}=${RESCUE_GB}GB-rescue" >> /tmp/opt.$TMP_LABEL
    cat /tmp/opt.$TMP_LABEL > /opt/U.opt
    ls -l /opt
    df /
    if [ "$_first" ]; then
        _ask _yn "Reboot now ($_rescue_entry)? (Y/n): " n
        if [ ! "$_yn" ] || [ "$_yn" = "Y" ] || [ "$_yn" = "y" ]; then
            reboot
        fi
    fi
}

verbose() { # -v)
    echo > /tmp/mess.$TMP_LABEL
    if [ "$ON_NONE" ] || [ "$ON_WSL" ]; then
	echo "DEBIAN(/opt):" >> /tmp/mess.$TMP_LABEL
	ls -l /opt/ >> /tmp/mess.$TMP_LABEL
    else
	_fs $LINENO mount $MNT_DEBIAN
	echo "DEBIAN($MNT_DEBIAN/opt):" >> /tmp/mess.$TMP_LABEL
	ls -l $MNT_DEBIAN/opt/ >> /tmp/mess.$TMP_LABEL
    fi
    if [ "$mnt_strage" ] && [ "$mnt_strage" != "$MNT_DEBIAN" ]; then
	mkdir -p $mnt_strage
	if [ "$UUID" ]; then
	    if [ ! "`fgrep $mnt_strage /etc/mtab`" ]; then
		_fs $LINENO mount -o rw UUID=$UUID $mnt_strage
	    fi
	else
	    if [ ! "`fgrep $mnt_strage /etc/mtab`" ]; then
		_fs $LINENO mount $mnt_strage
	    fi
	fi
	echo >> /tmp/mess.$TMP_LABEL
	echo "STRAGE($strage_opt):" >> /tmp/mess.$TMP_LABEL
	ls -l $strage_opt/ >> /tmp/mess.$TMP_LABEL
	_old_strage_opt
	cd /opt
    fi
    _list_mnt_no >> /tmp/mess.$TMP_LABEL
    echo >> /tmp/mess.$TMP_LABEL
    df >> /tmp/mess.$TMP_LABEL
    if [ "$U_BATCH" ]; then
        cat /tmp/mess.$TMP_LABEL
    else
        _page /tmp/mess.$TMP_LABEL
    fi
}

_set_strage_opt() { # STRAGE のイメージ等（?GB-名前.img・$BUILD.img・IMG0.img）の置き場所（$strage_opt）。
    # STRAGE の区画ならディストリビューションごとに img.$DEBIAN、STRAGE が無ければ（DEBIAN区画）今までどおり /opt
    if [ "$mnt_strage" ] && [ "$mnt_strage" != "$MNT_DEBIAN" ]; then
        strage_opt=$mnt_strage/img.$DEBIAN
    else
        strage_opt=$mnt_strage/opt
    fi
}

_old_strage_opt() { # 前の置き場所（STRAGE/opt）に残っているイメージを知らせる（どのディストリビューションのものか分からないので、動かさない）
    if [ "$strage_opt" = "$mnt_strage/opt" ] || ! mountpoint -q $mnt_strage 2>/dev/null; then
        return
    fi
    if [ "`ls $mnt_strage/opt 2>/dev/null | grep -e '^[0-9][0-9]*GB-.*\.img$' -e '^BUILD_.*\.img$'`" ]; then
        echo "$mnt_strage/opt: ?GB-*.img etc. -> please move to $mnt_strage/img.debian or $mnt_strage/img.ubuntu. : $LINENO" >> /tmp/log.$TMP_LABEL
    fi
}

UUID=""
temp_name=""
mnt_strage=""
strage_opt=/opt
main() {
    local STRAGE _PART _error
    local _eth_device _ETH_DEVICE_LIST
    local _option _arg _ARG_LIST _i _n=n
    local _ARG_1 _ARG_2 _ARG_3 _ARG_4 _ARG_5
    local _numerator=$NUMERATOR
    local _NEW_KERNEL_LIST

    if [ "`stat -c %d:%i / 2>/dev/null`" != "`stat -c %d:%i /proc/1/root/. 2>/dev/null`" ]; then # arch-chroot等の中
        echo "U.sh: in chroot (kernel `uname -r`, hostname `hostname`). Please run it outside the chroot."
        return
    fi
    if [ "$TERM" = "linux" ]; then
	LANG=C # 文字化け回避
    fi
    # /usr/local/bin/U（'. U 〜' で読む）。中身が今の版と違えば書き直す（前は無い時だけ作っていた）
    cat <<'EOF' > /tmp/U.$TMP_LABEL
ARG="$1 $2 $3 $4 $5 $6 $7 $8 $9"
if [ "`id -u`" -ne 0 ]; then
    SUDO=sudo
else
    SUDO=""
fi
# 2つ以上の引数の最後が -d・-b（-dash・-bash）と同じ時だけ、U.sh を動かすシェルを切り替える（/opt/U.opt の SH=）
# （前は最後の引数に「-d」「-b」が含まれるだけで切り替わり、'. U -s test-build' 等で SH=buildash になった）
# -sh・-py は、U.sh（旧シェル版）と Python 版（/usr/local/lib/usilic-py/ush.py）を切り替える（/opt/U.opt の USH=、既定は Python 版）
SH=""
USH=""
if [ $# -ge 2 ]; then
    eval "OPTION=\${$#}"
    case "$OPTION" in
        -d|-dash) SH=dash ;;
        -b|-bash) SH=bash ;;
        -sh) USH=sh ;;
        -py) USH=py ;;
    esac
fi
if [ "$SH" ] || [ "$USH" ]; then
    if [ "$SH" ]; then
        $SUDO sed -i -e "s/^SH=.*/SH=$SH/" /opt/U.opt
    elif grep -q '^USH=' /opt/U.opt; then
        $SUDO sed -i -e "s/^USH=.*/USH=$USH/" /opt/U.opt
    else
        echo "USH=$USH" | $SUDO tee -a /opt/U.opt > /dev/null
    fi
    ARG=""
    _u_i=1
    for _u_a in "$@"
    do
        if [ $_u_i -lt $# ]; then
            ARG="$ARG $_u_a"
        fi
        _u_i=$((_u_i+1))
    done
fi
SH=`grep ^SH= /opt/U.opt | awk -FSH= '{print $2}'`
USH=`grep ^USH= /opt/U.opt | awk -FUSH= '{print $2}'`
if [ "$USH" != "sh" ] && [ -f /usr/local/lib/usilic-py/ush.py ] && command -v python3 > /dev/null 2>&1; then
    $SUDO python3 /usr/local/lib/usilic-py/ush.py $ARG
else
    $SUDO ${SH:-bash} /opt/U.sh $ARG
fi
EOF
    if ! cmp -s /tmp/U.$TMP_LABEL /usr/local/bin/U; then
        echo "-> /usr/local/bin/U"
        cat /tmp/U.$TMP_LABEL > /usr/local/bin/U
    fi
    rm -f /tmp/U.$TMP_LABEL
    if [ ! -f /opt/U.opt ]; then
	echo "STRAGE=\"\"" > /opt/U.opt
	echo "SH=$SH" >> /opt/U.opt
    fi
    if [ "$1" = "-o" ]; then
	_output_u $2 $3 $4
	return
    fi
    . /opt/U.opt
    mnt_strage=$STRAGE
    _set_strage_opt
    if [ -d /tmp/modules.$TMP_LABEL ]; then # 前の版の U.sh が /tmp へ逃がしたまま止まった分（今の版は逃がさない）
	if [ -d /tmp/modules.$TMP_LABEL/modules ] && [ "`ls /tmp/modules.$TMP_LABEL/modules`" ]; then # モジュールバックアップ分（by save_host()）があればリストア
	    echo "mv /tmp/modules.$TMP_LABEL/modules/* /usr/lib/modules/ 2>/dev/null"
	    mv /tmp/modules.$TMP_LABEL/modules/* /usr/lib/modules/ 2>/dev/null
	fi
	if [ -d /tmp/modules.$TMP_LABEL/headers ] && [ "`ls /tmp/modules.$TMP_LABEL/headers`" ]; then
	    echo "mv /tmp/modules.$TMP_LABEL/headers/* /usr/src/ 2>/dev/null"
	    mv /tmp/modules.$TMP_LABEL/headers/* /usr/src/ 2>/dev/null
	fi
    fi
    if [ -d /tmp/bak.$TMP_LABEL/wsl ]; then # モジュールバックアップ分（by save_host()）があればリストア
	echo "mv /tmp/bak.$TMP_LABEL/wsl /usr/lib"
	mv /tmp/bak.$TMP_LABEL/wsl /usr/lib
    fi
    if [ -d /tmp/$DEBIAN.$TMP_LABEL ]; then
	rm -rf /tmp/$DEBIAN.$TMP_LABEL
    fi
    _fs $LINENO umount /tmp/shm0.$TMP_LABEL
    if [ ! -f "/etc/systemd/timesyncd.conf" ] && ( [ ! "$ON_UBUNTU" ] || [ ! "`_is_inst chrony`" ] ); then #_５)　時刻設定をUTCへ
	apt update -y
	apt upgrade -y
	apt install -y systemd-timesyncd
	sed -i /etc/systemd/timesyncd.conf -e s/#NTP=/NTP=ntp\.nict\.jp/
	systemctl restart systemd-timesyncd
	timedatectl set-local-rtc 0
    fi
    if [ "$ON_UBUNTU" ]; then
	_apt_sources_ubuntu
    else
	if [ -f /etc/apt/sources.list.d/debian.sources ]; then # Debian 13（deb822 の形）
	    if [ ! "`grep '^Components:.*non-free-firmware' /etc/apt/sources.list.d/debian.sources`" ] || [ ! "`grep '^Types:.*deb-src' /etc/apt/sources.list.d/debian.sources`" ]; then
		echo "sed -i /etc/apt/sources.list.d/debian.sources (non-free-firmware, deb-src)"
		sed -i /etc/apt/sources.list.d/debian.sources -e '/^Components:/{/non-free-firmware/!s/$/ non-free-firmware/}' -e 's/^Types: deb$/Types: deb deb-src/'
		apt update -y
	    fi
	else
	    if [ ! "`grep non-free-firmware /etc/apt/sources.list`" ]; then
		echo "sed -i /etc/apt/sources.list -e 's/ main/ main non-free-firmware/'"
		sed -i /etc/apt/sources.list -e 's/ main/ main non-free-firmware/'
		apt update -y
	    fi
	    if [ ! "`grep deb-src /etc/apt/sources.list`" ]; then
		sed /etc/apt/sources.list -e 's/^deb /deb-src /' > /tmp/src.$TMP_LABEL
		cat /tmp/src.$TMP_LABEL >> /etc/apt/sources.list
		apt update -y
	    fi
	fi
    fi
    cat /dev/null  > /tmp/log.$TMP_LABEL
    ln -sf /tmp/log.$TMP_LABEL /opt/U.log
    cd /opt
    cd ..
    if [ "$ON_NONE" ] && [ ! "$ON_UBUNTU" ]; then
	if [ "`dpkg -l | grep wget | grep ^ii`" ] && [ ! "`_is_inst apt-listchanges`" ]; then # wget標準システムユーティリティの有無をapt-listchanges
	    dpkg --purge wget # U.sh をゲットしたあとは不要
	fi
    fi
    if [ "$ON_UBUNTU" ] && [ "$GNU_COREUTILS" ] && [ "`_is_inst coreutils-from-uutils`" ]; then # DEBIAN.u を記録する前に GNU 版へ
	# uutils は必須（Essential）なので、apt は自分では外さない（「競合」で止まる）。名前の後ろの「-」で外すと指定する
	echo "apt-get install -y --allow-remove-essential coreutils-from-gnu coreutils-from-uutils-"
	apt-get install -y --allow-remove-essential coreutils-from-gnu coreutils-from-uutils-
    fi
    if [ "$ON_NONE" ] && [ ! -f /opt/DEBIAN.u ]; then # 入れたばかりの状態の記録（Ubuntu も。-u がここへ戻してから BASE を作る）
	_output_u DEBIAN.u $LINENO
    fi
    if [ ! "`ip a | grep \<BROADCAST`" ]; then # ネットに繋がってないかまたは有効なethデバイス設定が無い
	echo "Network unreachable.: $LINENO" >> /tmp/log.$TMP_LABEL
    fi
    if [ ! "$ON_LINE" ] && [ "$ON_UBUNTU" ]; then # Ubuntu は netplan で再設定
	_net_restart
	if [ ! "`_on_line`" ]; then
	    echo "Network unreachable.: $LINENO" >> /tmp/log.$TMP_LABEL
	fi
    elif [ ! "$ON_LINE" ]; then # ネットに繋がってないかまたは有効なethデバイス設定が無い
	if [ ! -f "$NETWORK_MANAGER_CONF" ] && [ "`grep ^#allow-hotplug /etc/network/interfaces`" ]; then # network-manager がインストールされている状態で'. U BASE.u -p'（BASE構築直後の状態へ復旧）した場合、network-managerも削除されるため、同プログラムをインストールした際の対処（/etc/network/interfaces 内の設定をコメントアウト）を戻しておかないとネットに繋がらない
	    sed -i /etc/network/interfaces -e "s/#allow\-hotplug/allow-hotplug/" -e "s/#iface/iface/"
	    systemctl start networking.service
	fi
	if [ ! "`_on_line`" ]; then #_２)　それでも繋がらなければ改めて新規の設定を行う（USBデバイス等へ移行したシステムを別マシンで起動した場合等、新たなethデバイスへの対処）
#	    _ETH_DEVICE_LIST=`dmesg | grep 'renamed from eth' | awk '{print $5}' | sed -e s/:// | sort`
	    _ETH_DEVICE_LIST=`ip a | grep ^[0-9]: | grep -ve lo: | awk -F': ' '{print $2}' | awk -F: '{print $1}'`
echo _ETH_DEVICE_LIST=$_ETH_DEVICE_LIST
	    for _eth_device in $_ETH_DEVICE_LIST # イーサネットデバイスが複数ある場合
	    do
		head -n10 /etc/network/interfaces > /tmp/interfaces.$TMP_LABEL
		echo "echo 'allow-hotplug $_eth_device' >> /tmp/interfaces.$TMP_LABEL"
		echo "allow-hotplug $_eth_device" >> /tmp/interfaces.$TMP_LABEL
		echo "echo 'iface $_eth_device inet dhcp' >> /tmp/interfaces.$TMP_LABEL"
		echo "iface $_eth_device inet dhcp" >> /tmp/interfaces.$TMP_LABEL
		mv /tmp/interfaces.$TMP_LABEL /etc/network/interfaces
		echo "ifdown $_eth_device"
		ifdown $_eth_device
		echo "ifup $_eth_device"
		ifup $_eth_device
		if [ "`_on_line`" ]; then # ネットに繋がったら正式採用
		    _n=y
		    break
		fi
	    done
	    if [ $_n != "y" ]; then
		echo "Network unreachable.: $LINENO" >> /tmp/log.$TMP_LABEL
	    fi
	fi
    fi
    if [ ! -f /opt/rc_systemd.txt ]; then # BASE の有効サービス一覧（_start_rc で使用）
	_enabled_units > /opt/rc_systemd.txt
    fi
    grep -ve /mnt/cifs -ve /mnt/ntfs -ve /mnt/exfat -ve /mnt/ext4 /etc/fstab > /tmp/fstab.$TMP_LABEL
    if [ "$SMBFS" ]; then
	grep -ve /mnt/cifs /etc/fstab > /tmp/fstab.$TMP_LABEL
	_is_inst cifs-utils +
	if ! _fs_try $LINENO mount -t cifs -o `echo $SMBFS | awk '{print $4}'` `echo $SMBFS | awk '{print $1}'` `echo $SMBFS | awk '{print $2}'`; then
	    mnt_strage="" # 失敗はログに書いた
	    _set_strage_opt
	else
	    mnt_strage=/mnt/cifs
	    _set_strage_opt
	    echo $SMBFS >> /tmp/fstab.$TMP_LABEL
	    _fs $LINENO umount /mnt/cifs
	fi
    fi
    if [ "$mnt_strage" != "$MNT_DEBIAN" ] && [ "$mnt_strage" != "/mnt/cifs" ]; then
	mnt_strage=$STRAGE
	_set_strage_opt
	if [ "$mnt_strage" = "/mnt/ntfs" ]; then
	    _is_inst ntfs-3g +
	elif [ "$mnt_strage" = "/mnt/exfat" ]; then
	    _is_inst exfat-fuse +
	fi
	if [ "$UUID" ] && [ "`blkid | grep $UUID`" ]; then # STRAGEが設定されている場合はデバイスの有効性を検証
	    echo "UUID=$UUID $mnt_strage `echo $mnt_strage | awk -F\/ '{print $3}'` rw,noauto	    0 1" >> /tmp/fstab.$TMP_LABEL
	    _fs $LINENO umount $mnt_strage
	else
	    mnt_strage=""
	    _set_strage_opt
	fi
    fi
    mv /tmp/fstab.$TMP_LABEL /etc/fstab
    chmod 644 /etc/fstab
    systemctl daemon-reload
    if [ ! "$mnt_strage" ]; then
	mnt_strage=$MNT_DEBIAN
	_set_strage_opt
    fi
    _NEW_KERNEL_LIST=`ls /opt | grep linux-image | grep -E '[+][0-9]+gb_' | grep -ve +${RESCUE_GB}gb`
    if [ "$_NEW_KERNEL_LIST" ] && [ "$mnt_strage" = "$MNT_DEBIAN" ]; then
	echo "To update 'RAM-`echo $_NEW_KERNEL_LIST | awk -Flinux-image- '{print $2}' | awk -F_ '{print $1}'`-base.gz', please setup strage-area."
    fi
    cat /dev/null > /tmp/mess.$TMP_LABEL
    if [ "$1" ]; then
	_option=$1
	_i=1
	for _arg in $2 $3 $4 $5
	do
	    echo "_ARG_${_i}=${_arg}" >> /tmp/mess.$TMP_LABEL
	    _i=$((_i+1))
	done
	. /tmp/mess.$TMP_LABEL
    else
	if [ "$ON_WSL" ]; then
	    echo "  -k(ernel_remake) - or $RESCUE_GB or ??" >> /tmp/mess.$TMP_LABEL
	    echo "  -o(utput_u) . or filename [-i(nstall) or -p(urge)]" >> /tmp/mess.$TMP_LABEL
	elif [ "$ON_RAMDISK" ]; then
	    echo "/dev/shm : `_tmpfs_size | tail -n1`"
	    echo "img-size : $((`df | grep \/$ | awk '{print $3}'`*$_numerator/10))"
	    echo ". U [-acdfkmnosv]"
	    echo "  -c(hange_root) [/mnt/usb]" >> /tmp/mess.$TMP_LABEL
	    echo "  -f(ile_system) [-] : /mnt/No." >> /tmp/mess.$TMP_LABEL
	    echo "  -d(esktop) : on ??GB-* (8GB~, STRAGE; on a desktop host: from a text console)" >> /tmp/mess.$TMP_LABEL
	    echo "  -k(ernel_remake) . or - or / or ?? (STRAGE)" >> /tmp/mess.$TMP_LABEL
	    echo "  -m(ake_usb) device [-]" >> /tmp/mess.$TMP_LABEL
	    echo "  -n(ext_host)" >> /tmp/mess.$TMP_LABEL
	    echo "  -o(utput_u) . or filename [-i(nstall) or -p(urge)]" >> /tmp/mess.$TMP_LABEL
	    echo "  -s(ave_host) name [.] or . (without STRAGE: ${RESCUE_GB}GB-rescue only)" >> /tmp/mess.$TMP_LABEL
	    echo "  -a(dd_strage)" >> /tmp/mess.$TMP_LABEL
	elif [ "$ON_NONE" ]; then
	    if [ -f /opt/BASE.u ]; then
		echo ". U [-acefkonuv]"
		echo "  -c(hange_root) [/mnt/usb]" >> /tmp/mess.$TMP_LABEL
		echo "  -e(rase_base)" >> /tmp/mess.$TMP_LABEL
		echo "  -k(ernel_remake) . or - or / or $RESCUE_GB or ??" >> /tmp/mess.$TMP_LABEL
		echo "  -n(ext_host)" >> /tmp/mess.$TMP_LABEL
		echo "  -o(utput_u) . or filename [-i(nstall) or -p(urge)]" >> /tmp/mess.$TMP_LABEL
	    else
		echo ". U [-afuv]"
	    fi
	    echo "  -f(ile_system) [-] : /mnt/No." >> /tmp/mess.$TMP_LABEL
	    echo "  -u(pdate_base)" >> /tmp/mess.$TMP_LABEL
	    echo "  -a(dd_strage)" >> /tmp/mess.$TMP_LABEL
	elif [ "$ON_TORAM" ]; then
	    echo ". U [-ov]"
	    echo "  -o(utput_u) . or filename [-i(nstall) or -p(urge)]" >> /tmp/mess.$TMP_LABEL
	else
	    echo ". U [-afknouv]"
	    echo "  -f(ile_system) [-] : /mnt/No." >> /tmp/mess.$TMP_LABEL
	    echo "  -k(ernel_remake) . or - or $RESCUE_GB or ??" >> /tmp/mess.$TMP_LABEL
	    echo "  -n(ext_host)" >> /tmp/mess.$TMP_LABEL
	    echo "  -o(utput_u) . or filename [-i(nstall) or -p(urge)]" >> /tmp/mess.$TMP_LABEL
	    echo "  -u(pdate_base) : TORAM (dracut)" >> /tmp/mess.$TMP_LABEL
	    echo "  -a(dd_strage)" >> /tmp/mess.$TMP_LABEL
	fi
	echo "  -v(ervose)" >> /tmp/mess.$TMP_LABEL
	sort < /tmp/mess.$TMP_LABEL
	cat /dev/null > /tmp/mess.$TMP_LABEL
	if [ "$U_BATCH" ]; then # --list: 使えるオプションの一覧だけ
	    return
	fi
	read -p "Option: -" _n
	_ARG_LIST=-${_n} # -eおよび-nがechoのためのオプションではないことを明示
	if [ "`echo $_ARG_LIST | fgrep ' '`" ]; then
	    _option=`echo $_ARG_LIST | awk '{print $1}'`
	else
	    _option=$_ARG_LIST
	fi
	_i=0
	for _arg in $_ARG_LIST
	do
	    echo _ARG_$_i=$_arg >> /tmp/mess.$TMP_LABEL
	    _i=$((_i+1))
	done
	. /tmp/mess.$TMP_LABEL
	if [ "$_ARG_0" = "\-" ]; then
	    _option="-"
	fi
    fi
    if ( [ "$ON_WSL" ] || [ "$ON_NONE" ] || ( [ "$ON_UBUNTU" ] && [ ! "$ON_RAMDISK" ] && [ ! "$ON_TORAM" ] ) ) && [ ! -L $MNT_DEBIAN ] && [ ! "$KEEP_MNT_DEBIAN" ]; then
	if [ -d $MNT_DEBIAN ]; then
	    echo "cd /opt"
	    cd /opt
	    echo "rmdir $MNT_DEBIAN"
	    rmdir $MNT_DEBIAN
	fi
	echo "ln -sf / $MNT_DEBIAN"
	ln -sf / $MNT_DEBIAN
    fi
    if [ "$_option" != "-r" ]; then
	DOT_FILES
    fi
    _c=`echo \\-${_option} | awk -F-- '{print $2}'` # 「echo -e（および-n）」を回避
    if [ "$_c" ] && [ "$_c" != "-" ] && [ "`grep -e '-.)' $U_SH | grep -ve '# -.)' -ve '-.).' | grep $_c`" ]; then    
	case "$_option" in
	    -a)
		add_strage $_option
		;;
	    -c)
		change_root $_option $_ARG_1
		;;
	    -d)
		desktop_select $_option
		;;
	    -e)
		erase_base $_option
		;;
	    -f)
		file_system $_option $_ARG_1
		;;
	    -k)
		kernel_remake $_option $_ARG_1 $_ARG_2 $_ARG_3
		;;
	    -m)
		make_usb $_option $_ARG_1 $_ARG_2
		;;
	    -n)
		next_host $_option
		;;
	    -o)
		output_u $_option $_ARG_1 $_ARG_2 $_ARG_3
		;;
	    -r)
		rc_local_only $_option
		;;
	    -s)
		save_host $_option $_ARG_1 $_ARG_2 $_ARG_3
		;;
	    -t)
		tm $_option $_ARG_1
		;;
	    -u | -U)
		update_base $_option
		;;
	    -v)
		verbose $_option
		;;
	esac
    else
	echo "$_option: Invalid option. : $LINENO" >> /tmp/log.$TMP_LABEL
    fi
    _fs $LINENO umount /tmp/shm0.$TMP_LABEL
    if [ "$_option" != "-v" ] && [ "$_option" != "-a" ] && [ "$_option" != "-f" ]; then
	if [ "$ON_RAMDISK" ]; then
	    _fs $LINENO umount $MNT_DEBIAN
	    _fs $LINENO umount /boot/efi
	fi
	_fs $LINENO umount $OPT_BUILD
	_fs $LINENO umount $mnt_strage
    fi
    grep -v '^ : ' /tmp/log.$TMP_LABEL > /tmp/tmp.$TMP_LABEL
    sed '/^$/d' < /tmp/tmp.$TMP_LABEL > /tmp/log.$TMP_LABEL
    _save_log /tmp/log.$TMP_LABEL
}

# --- 前から使う時（usilic の RESCUE、^J^R）の引数。どこに置いてもいい（オプションの引数としては数えない）
#   --list     一覧を出して、最初の問いで止まる（選ばない・N）。オプションが無ければ、使えるオプションの一覧
#   --ans=答え  問いへの答え（問いの順に、いくつでも）。例: /opt/U.sh -f --ans=3、/opt/U.sh -n --ans=2 --ans=n
#   --batch    問いの無いオプション（-s name・-v 等）を、/dev/tty から読まず・more を使わずに（問いがあれば既定の答え）
# どちらかがあれば /dev/tty から読まず、一覧も標準エラー出力へ出す（usilic の画面を乱さない）
U_BATCH=""
U_ANS=""
U_TTY=/dev/tty
_args=""
for _x in "$@"
do
    case "$_x" in
        --list) U_BATCH=y ;;
        --batch) U_BATCH=y ;;
        --ans=*) U_BATCH=y; U_ANS="$U_ANS${_x#--ans=}|" ;;
        *) _args="$_args $_x" ;;
    esac
done
if [ "$U_BATCH" ]; then
    U_TTY=/dev/stderr
fi
set -- $_args

main $1 $2 $3 $4 $5
