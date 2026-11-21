# --- 基本設定
readonly DEBIAN=debian 	# LiveUSB稼働想定なら「vbox」非推奨
readonly USR=`ls /home`	#  _7)で設定のユーザー名
SH=bash	# U.sh デフォルト実行シェル

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
#
RESCUE_LIST="ssh xorg network-manager network-manager-gnome gparted fonts-noto-cjk fcitx5-mozc emacs firefox-esr qterminal xfce4-terminal xfce4"

# --- xGB-rescue（オプション）
# 例）4GB-rescue＋カーネルビルド環境
#
RESCUE_LIST_X="$RESCUE_LIST fakeroot libncurses5-dev"
readonly BUILD_LIST="build-dep linux"
#
#RESCUE_LIST_X=""
#readonly BUILD_LIST=""

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
# --- /opt/U.sh に変更がある場合、差分を表示 ->
# 『Patch now? (Y/n):』に'y'[Enter] ->
# $MNT_DEBIAN/opt/U.shに変更を反映
# 下記は差分表示に使用するコマンドとオプション
# コメントアウトで差分記録なし
#
readonly DIFF_SH="diff -ru"

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
    chown -R $USR:$USR /home/$USR/.emacs.d/init.el

    # ~/.config/autostart/qterminal.desktop　4GB-rescue版ログイン後にqterminalを自動起動
    mkdir -p /home/$USR/.config/autostart
    cat <<EOF > /home/$USR/.config/autostart/qterminal.desktop
[Desktop Entry]
Encoding=UTF-8
Version=0.9.4
Type=Application
Name=qterminal
Comment=
Exec=/usr/bin/qterminal -geometry 1260x730
OnlyShowIn=XFCE;
RunHook=0
StartupNotify=false
Terminal=false
Hidden=false
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
#
readonly GRUB_CUSTOM="
menuentry 'macOS Sonoma' {
	insmod part_msdos
	insmod fat
	set root='hd0,msdos1'
	search --no-floppy --set=root --label SONOMA
	chainloader /EFI/Boot/BOOTx64.efi
	savedefault
}
menuentry 'macOS Mojave' {
	insmod part_msdos
	insmod fat
	set root='hd0,msdos1'
	search --no-floppy --set=root --label MOJAVE
	chainloader /EFI/Boot/BOOTx64.efi
	savedefault
}
menuentry 'Windows 11' {
	insmod part_msdos
	insmod fat
	set root='hd0,msdos1'
	search --no-floppy --set=root --label WIN11
	chainloader /EFI/Microsoft/Boot/bootmgfw.efi
	savedefault
}
"
#
#readonly GRUB_CUSTOM=""


# set -ux;
#
if [ ! "`ls /tmp/log.?????? 2>/dev/null`" ]; then
    mktemp /tmp/log.XXXXXX >/dev/null
fi
readonly TMP_LABEL=`chmod 644 /tmp/log.??????; ls /tmp/log.?????? | awk -F/tmp/log. '{print $2}'`
readonly MNT_DEBIAN=/mnt/$DEBIAN
readonly OPT_BUILD=/opt/build # カーネル.deb は build/の親ディレクトリに作成される
readonly MEM_TOTAL_KB=`grep MemTotal /proc/meminfo | awk -F: '{print $2}' | awk '{print $1}'`
readonly MEM_TOTAL_GB=$((MEM_TOTAL_KB/1000/1000))
readonly TMPFS_KB=$((MEM_TOTAL_KB/2))
readonly NETWORK_MANAGER_CONF=/etc/NetworkManager/NetworkManager.conf
readonly NUMERATOR=11 # $NUMERATOR * ｢ルートファイルシステムサイズ｣ / 10 -> ｢同システム.img｣サイズ
readonly DISPLAY_MANAGER_LIST="lightdm sddm gdm3 wdm xdm"
readonly UNAME_R=`uname -r`
readonly ON_WSL=`echo $UNAME_R | fgrep 'WSL'`
readonly ON_NONE=`echo $UNAME_R | fgrep 'amd64'`
readonly ON_RAMDISK=`fgrep -e 'remount-ro' /etc/fstab | fgrep /dev/ram`
readonly ON_LINE=`ip a | grep 'inet ' | grep global`
readonly ON_EXTERN=`cat /etc/issue|grep -v 'Debian GNU/Linux'`
# 通常カーネル稼働時に $MNT_DEBIAN が既にマウント済みなら、symlink 化も mount/umount もせずそのまま使う
if [ ! "$ON_RAMDISK" ] && mountpoint -q $MNT_DEBIAN 2>/dev/null; then
    readonly KEEP_MNT_DEBIAN=y
else
    readonly KEEP_MNT_DEBIAN=""
fi
if [ "$ON_EXTERN" ]; then
    readonly BUILD=BUILD_EXTERN
    readonly BUILD_IMG_GB=7 # kernel-7.3.0 では 6GB では不足
    readonly RESCUE_GB=9
    readonly DESKTOP_GB=16
    readonly KERNEL_VERSION=`echo $UNAME_R | sed -e 's/[-+].*//'` # 7.0.0-xx-generic / 7.0.0+9gb -> 7.0.0
    readonly RAMDISK_GB=$RESCUE_GB
    readonly TEMPLATE_MB=3000
    if [ "$ON_RAMDISK" ]; then
	readonly LINUX_CONFIG=config-$UNAME_R
	DEBIAN_ROOT_UUID=`fgrep $MNT_DEBIAN /etc/fstab | awk '{print $1}' | sed -e 's/UUID=//'`
    elif [ "$ON_EXTERN" != "$UNAME_R" ]; then
	readonly LINUX_CONFIG=`ls /boot/config*generic 2>/tmp/error.$TMP_LABEL|tail -n1|sed -e 's/\/boot\///'`
	if [ ! "$LINUX_CONFIG" ]; then
	    cat /tmp/error.$TMP_LABEL
	    exit
	fi
	DEBIAN_ROOT_UUID=`fgrep -e 'remount-ro' /etc/fstab | awk -F= '{print $2}' | awk '{print $1}'`
    fi
    readonly PURGE_LIST=""
    readonly DEBIAN_ROOT_PART=`blkid | grep $DEBIAN_ROOT_UUID | awk '{print $1}' | sed -e 's/://'`
else
    readonly BUILD=BUILD_DEBIAN
    readonly BUILD_IMG_GB=4
    readonly RESCUE_GB=4
    readonly DESKTOP_GB=8
    readonly KERNEL_VERSION=6.1
    readonly TEMPLATE_MB=2000
    if [ "$ON_WSL" ]; then
	readonly RAMDISK_GB=$RESCUE_GB #_13)　最初に作成するカーネルのRAMDISKサイズを4GBに設定
    else
	if [ "$ON_RAMDISK" ]; then # RAMDISK上
	    DEBIAN_ROOT_UUID=`fgrep $MNT_DEBIAN /etc/fstab | awk '{print $1}' | sed -e 's/UUID=//'`
	    readonly RAMDISK_GB=`echo $UNAME_R | awk -F'+' '{print $2}' | sed -e s/gb//` # それ以外はカーネル名から抽出
	else # DEBIAN上 #_４)　DEBIANのルートファイルシステムUUIDを記録
	    DEBIAN_ROOT_UUID=`fgrep -e 'remount-ro' /etc/fstab | awk -F= '{print $2}' | awk '{print $1}'`
	    if [ "$ON_NONE" ]; then
		readonly RAMDISK_GB=$RESCUE_GB #_13)　最初に作成するカーネルのRAMDISKサイズを4GBに設定
	    else
		readonly RAMDISK_GB=`echo $UNAME_R | awk -F'+' '{print $2}' | sed -e s/gb//` # それ以外はカーネル名から抽出
	    fi
	fi
	readonly DEBIAN_ROOT_PART=`blkid | grep $DEBIAN_ROOT_UUID | awk '{print $1}' | sed -e 's/://'`
	# --- tasksel-DESKTOP設定およびRAMDISKサイズの指定
	# xGB-RAMDISKカーネルで起動後、STRAGE区画にxGB-*.imgが無ければ下記の$TASKSEL_LISTを「xGB-*.img」で自動保存
	#
	# 例）x=16の場合
	# readonly TASKSEL_GB=16
	#
	readonly TASKSEL_GB=8
	#readonly TASKSEL_LIST=`tasksel --list | grep -v 'u desktop' | sed -e 's/u //' | head -n8 | awk '{print $1}'`
	#readonly DESKTOP_LIST="GNOME Xfce GNOME-Flashback KDE-Plasma Cinnamon MATE LXDE LXQt"
	#
	# 例）STRAGEが狭い
	#readonly TASKSEL_LIST=xfce-desktop
	#readonly DESKTOP_LIST="Xfce"
	#
    fi
    readonly LINUX_CONFIG=linux-config-$KERNEL_VERSION
    readonly PURGE_LIST="texlive-base texlive-binaries texlive-latex-base texlive-latex-extra texlive-latex-recommended texlive-pictures dvipng"
    readonly CONFIG_NONE=config.amd64_none_amd64 # Debian12デフォルトカーネルのCONFIG設定(/usr/src/$LINUX_SOURCE/.config)
fi
readonly BUILD_IMG_KB=$((BUILD_IMG_GB*1000*1000)) # buildディレクトリイメージサイズ
readonly RESCUE_KB=$((RESCUE_GB*1000*1000))
readonly TEMPLATE_KB=$((TEMPLATE_MB*1000))
readonly LINUX_SOURCE=linux-source-$KERNEL_VERSION
readonly TEMPLATE_BASE=TEMPLATE-$KERNEL_VERSION-base
readonly RAM_DIR_LIST="boot dev media opt proc run sys usr/lib/modules tmp mnt/$DEBIAN" # $MNT_DEBIAN はNG
readonly DUP_DIR_LIST="dev media mnt proc run sys tmp"
BASE_LIST="firmware-realtek firmware-misc-nonfree firmware-amd-graphics firmware-nvidia-gsp"
readonly PAGER=more

# --- Ubuntu（ON_EXTERN）用の設定 ------------------------------------
# Debian の tasksel(task-*) の代わりにデスクトップのメタパッケージを使用
# 名前は 'apt-cache search -- -desktop$' 等で確認
if [ "$ON_EXTERN" ]; then
    readonly TASKSEL_GB=$DESKTOP_GB
    #readonly TASKSEL_LIST="ubuntu-desktop-minimal xubuntu-core kubuntu-desktop lubuntu-desktop ubuntu-mate-core"
    #readonly DESKTOP_LIST="GNOME Xfce KDE-Plasma LXQt MATE"
    readonly TASKSEL_LIST="xubuntu-core"
    readonly DESKTOP_LIST="Xfce"
    readonly DESKTOP_APT_OPT="--no-install-recommends" # 推奨パッケージも入れるなら ""
    readonly JP_LIST="language-pack-ja language-pack-gnome-ja fcitx5-mozc fonts-noto-cjk"
    readonly STD_LIST="ubuntu-standard"
    readonly NO_SNAP=y # snapd を入れず Firefox は Mozilla 公式 apt リポジトリの deb 版を使う（snap を使うなら ""）
    readonly NETPLAN_NM=/etc/netplan/90-U-network-manager.yaml
    RESCUE_LIST=`echo " $RESCUE_LIST " | sed -e 's/ firefox-esr / firefox /'`
    if [ "$RESCUE_LIST_X" ]; then
	RESCUE_LIST_X="$RESCUE_LIST fakeroot libncurses-dev"
    fi
    BASE_LIST="" # 必要なら "linux-firmware"（1GB前後あるので RAMDISK 容量に注意）
else
    readonly DESKTOP_APT_OPT=""
    readonly JP_LIST="task-japanese-desktop"
fi
readonly RESCUE_LIST RESCUE_LIST_X BASE_LIST

_on_line() { # 現在のネット接続状態（readonly の $ON_LINE は起動時の値のまま）
    ip a | grep 'inet ' | grep global
}

_net_restart() {
    if [ "$ON_EXTERN" ]; then
	echo "netplan apply"
	netplan apply
    else
	systemctl restart networking.service
    fi
    sleep 3
}

_apt_sources_extern() { # Ubuntu: deb822 形式で universe/multiverse と deb-src を有効化
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

_no_snap_extern() { # snapd を禁止し、Firefox を Mozilla 公式 apt リポジトリから入れる設定
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
    if [ "$ON_EXTERN" ]; then
	_apt_sources_extern
	_no_snap_extern
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

_desktop_extern() { # Ubuntu: デスクトップ導入後の設定（on）／BASE へ戻した後の後始末（off）
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

_fs() {
    local _i _op _lineno _args="$1 $2 $3 $4 $5 $6 $7 $8 $9"
    local _mount_point=`echo $_args | awk '{print $NF}'`

    cd /opt
    if [ ! "$_mount_point" ] || [ "$_mount_point" = "/opt/" ] || [ -L "$_mount_point" ]; then
	return
    fi
    if [ "$KEEP_MNT_DEBIAN" ] && [ "$_mount_point" = "$MNT_DEBIAN" ]; then
	return
    fi
    if [ "`echo $1 | egrep '^[0-9]+$'`" ]; then
	_lineno="${1}: "
	_args=`echo $_args | sed -e "s/$1 //"`
	_op=$2
    else
	_lineno=""
	_op=$1
    fi
    cat /dev/null > /tmp/tmp.$TMP_LABEL
    if [ "$_op" = "mount" ]; then
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
	if ( [ "$_op" = "mount" ] && [ "`fgrep $_mount_point /etc/mtab`" ] ) || ( [ "$_op" = "umount" ] && [ ! "`fgrep $_mount_point /etc/mtab`" ] ); then
	    break
	fi
	`$_args > /tmp/tmp.$TMP_LABEL 2>&1`
	sleep 1
    done
    cd - > /dev/null
}

_is_inst() {
    local _is=`dpkg -l|grep "^ii  $1"`

    if [ ! "$2" ]; then
	echo $_is
    elif [ "$2" = "+" ] && [ ! "$_is" ]; then
	 echo "apt-get install -y $1"
	 apt-get install -y $1 >> /tmp/log.$TMP_LABEL
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
    echo "dd if=/dev/zero of=$mnt_strage/opt/$BUILD.img bs=1KB count=$BUILD_IMG_KB"
    dd if=/dev/zero of=$mnt_strage/opt/$BUILD.img bs=1KB count=$BUILD_IMG_KB
    echo "mkfs.ext4 -q $mnt_strage/opt/$BUILD.img"
    mkfs.ext4 -q $mnt_strage/opt/$BUILD.img
}

_mount_build_img() {
    local _build_img_kb

    if [ -f $MNT_DEBIAN/opt/$BUILD.img ]; then
	_build_img_kb=$((`ls -l $MNT_DEBIAN/opt/$BUILD.img 2>/dev/null | awk '{print $5}'`/1000))
    else
	_build_img_kb=0
    fi
    if [ "$mnt_strage" = "$MNT_DEBIAN" ]; then
	if [ -f $MNT_DEBIAN/opt/$BUILD.gz ]; then #_18)　buildイメージを作成
	    echo "zcat $MNT_DEBIAN/opt/$BUILD.gz > $MNT_DEBIAN/opt/$BUILD.img"
	    zcat $MNT_DEBIAN/opt/$BUILD.gz > $MNT_DEBIAN/opt/$BUILD.img
	    echo "rm $MNT_DEBIAN/opt/$BUILD.gz"
	    rm $MNT_DEBIAN/opt/$BUILD.gz
	elif [ "$_build_img_kb" -lt "$BUILD_IMG_KB" ]; then # /opt/$BUILD.imgが無いか破損
	    _dd_build_img
	fi
    else
	_fs $LINENO mount $mnt_strage
	if [ ! -f $mnt_strage/opt/$BUILD.img ]; then
	    _fs $LINENO mount $MNT_DEBIAN
	    if [ -f $MNT_DEBIAN/opt/$BUILD.gz ]; then
		echo "zcat $MNT_DEBIAN/opt/$BUILD.gz > $mnt_strage/opt/$BUILD.img"
		zcat $MNT_DEBIAN/opt/$BUILD.gz > $mnt_strage/opt/$BUILD.img
	    else
		if [ "$_build_img_kb" -lt "$BUILD_IMG_KB" ]; then # $MNT_DEBIAN/opt/$BUILD.imgが無いか破損
		    rm -f $MNT_DEBIAN/opt/$BUILD.img
		    _dd_build_img
		else
		    echo "gzip -c $MNT_DEBIAN/opt/$BUILD.img > $MNT_DEBIAN/opt/$BUILD.gz"
		    gzip -c $MNT_DEBIAN/opt/$BUILD.img > $MNT_DEBIAN/opt/$BUILD.gz
		    echo "mv -v $MNT_DEBIAN/opt/$BUILD.img $mnt_strage/opt"
		    mv -v $MNT_DEBIAN/opt/$BUILD.img $mnt_strage/opt
		fi
	    fi
	elif [ "$((`ls -l $mnt_strage/opt/$BUILD.img 2>/dev/null | awk '{print $5}'`/1000))" -lt "$BUILD_IMG_KB" ]; then
	    _dd_build_img
	fi
    fi
    _fs $LINENO umount $OPT_BUILD
    echo "mount -o loop $mnt_strage/opt/$BUILD.img $OPT_BUILD"
    _fs $LINENO mount -o loop $mnt_strage/opt/$BUILD.img $OPT_BUILD
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
	elif [ "$ON_EXTERN" ]; then
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
	    if [ "$ON_EXTERN" ]; then
		if [ "$MEM_TOTAL_GB" -ge "$DESKTOP_GB" ]; then
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
	    elif [ "$ON_RAMDISK" ] || [ "$ON_WSL" ] || ( [ "$ON_NONE" ] && [ "$MEM_TOTAL_GB" -ge "$DESKTOP_GB" ] ); then
		echo "mount -t tmpfs -o size=1600000K /dev/shm /usr/src"
		_fs $LINENO mount -t tmpfs -o size=1600000K /dev/shm /usr/src
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
    fi
    echo "cd /usr/src/$LINUX_SOURCE"
    cd /usr/src/$LINUX_SOURCE
    if [ ! -f .config ]; then
	if [ "$_new_config" ]; then
	    echo "cp $MNT_DEBIAN/boot/$_new_config .config"
	    cp $MNT_DEBIAN/boot/$_new_config .config
	else
	    if [ "$ON_EXTERN" ]; then
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
	if [ "$ON_EXTERN" ]; then
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
    if [ "$ON_EXTERN" ]; then # Ubuntu の署名鍵（debian/canonical-*.pem）はソースに含まれないので常に空にする
	echo "scripts/config --file ../build/.config --set-str SYSTEM_TRUSTED_KEYS '' --set-str SYSTEM_REVOCATION_KEYS ''"
	scripts/config --file ../build/.config --set-str SYSTEM_TRUSTED_KEYS "" --set-str SYSTEM_REVOCATION_KEYS ""
    fi
    echo "make O=../build/ olddefconfig"
    make O=../build/ olddefconfig
    echo "make -j9 O=../build/ LOCALVERSION=$_new_version"
    make -j9 O=../build/ LOCALVERSION=$_new_version
    echo "make modules -j9 O=../build/ LOCALVERSION=$_new_version"
    make modules -j9 O=../build/ LOCALVERSION=$_new_version
    echo "make bindeb-pkg O=../build/ LOCALVERSION=$_new_version"
    make bindeb-pkg O=../build/ LOCALVERSION=$_new_version
    if [ ! -L $MNT_DEBIAN ]; then
	echo "mv /opt/*.deb /opt/linux-upstream* $MNT_DEBIAN/opt"
	mv /opt/*.deb /opt/linux-upstream* $MNT_DEBIAN/opt
    fi
    _fs $LINENO umount $OPT_BUILD
    _fs $LINENO umount $MNT_DEBIAN
}

_correct_uuid() {
    local _dev_1=`echo $2 | sed -e 's/\/dev\///'`
    local _dev_2=`echo $3 | sed -e 's/\/dev\///'`
    local _uuid_1=`ls -l /dev/disk/by-uuid | grep $_dev_1 | awk '{print $9}'`
    local _uuid_2=`ls -l /dev/disk/by-uuid | grep $_dev_2 | awk '{print $9}'`
    local _dev_3

    echo "sed $1 -e s/$_uuid_1/$_uuid_2/ > /tmp/uuid.$TMP_LABEL"
    sed $1 -e s/$_uuid_1/$_uuid_2/ > /tmp/uuid.$TMP_LABEL
    if [ "$4" ]; then
	echo "grep -ve /boot/efi -ve /mnt/cifs -ve /mnt/ext4 -ve /mnt/ntfs -ve /mnt/exfat /tmp/uuid.$TMP_LABEL > $1"
	grep -ve /boot/efi -ve /mnt/cifs -ve /mnt/ext4 -ve /mnt/ntfs -ve /mnt/exfat /tmp/uuid.$TMP_LABEL > $1
	_dev_3=`echo $4 | sed -e 's/\/dev\///'`
	echo "UUID=`blkid | grep $_dev_3 | awk -FUUID= '{print $2}' | awk '{print $1}' | sed -e 's/\"//g'` /boot/efi vfat umask=0077 0 1" >> $1
    else
	mv /tmp/uuid.$TMP_LABEL $1
    fi
}

_update_base() {
    local _new_kb _new_gb=$1
    local _new_base _new_module _new_kernel

    _new_kernel=`ls -t /opt | grep linux-image | grep +${_new_gb}gb | head -n1`
    _new_module=`ls -t /lib/modules | grep +${_new_gb}gb | head -n1`
    _new_base=RAM-$_new_module-base

    if [ -f "$_new_base.gz" ] && [ ! "`ls -t $_new_base.gz $_new_kernel | head -n1 | grep ^linux`" ]; then
	if [ ! "`fgrep $_new_base /etc/grub.d/40_custom`" ]; then
	    cat <<EOF >> /etc/grub.d/40_custom
menuentry '$_new_base' {
      	insmod part_msdos
	insmod ext2
	set root='hd0,gpt3'
	search --no-floppy --fs-uuid --set=root $DEBIAN_ROOT_UUID
	echo	'Loading base:vmlinuz-$_new_module from $DEBIAN_ROOT_UUID ...'
	linux	/boot/vmlinuz-$_new_module root=/dev/ram0 ro
	echo	'Loading initial ramdisk ...'
	initrd 	/opt/$_new_base.gz
	savedefault
}
EOF
	fi
	return
    fi
    if [ "$mnt_strage" = "$MNT_DEBIAN" ] && [ -f /opt/${RESCUE_GB}GB-rescue.img ]; then
	if [ ! "`fgrep /tmp/bak.$TMP_LABEL /etc/mtab`" ]; then # /tmp/bak.$TMP_LABEL未使用ならrescue.img（<--カーネルリメーク時）のバックアップも無し
	    echo "mount -t tmpfs -o size=${TMPFS_KB}K /dev/shm /tmp/bak.$TMP_LABEL"
	    _fs $LINENO mount -t tmpfs -o size=${TMPFS_KB}K /dev/shm /tmp/bak.$TMP_LABEL
	fi
	_img_backup ${RESCUE_GB}GB-rescue /opt
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
    echo "mkdir -p /tmp/modules.$TMP_LABEL/headers"
    mkdir -p /tmp/modules.$TMP_LABEL/headers
    echo "mv /usr/src/linux-headers* /tmp/modules.$TMP_LABEL/headers 2>/dev/null"
    mv /usr/src/linux-headers* /tmp/modules.$TMP_LABEL/headers 2>/dev/null
    echo "tar cf - /lib/modules/$_new_module /usr/src/linux-headers-$_new_module 2>/dev/null| tar xf - -C /tmp/loop1.$TMP_LABEL"
    tar cf - /lib/modules/$_new_module /usr/src/linux-headers-$_new_module 2>/dev/null| tar xf - -C /tmp/loop1.$TMP_LABEL
    echo "mv /tmp/modules.$TMP_LABEL/headers/* /usr/src 2>/dev/null"
    mv /tmp/modules.$TMP_LABEL/headers/* /usr/src 2>/dev/null
    _new_kb=$((_new_gb*1000*1000))
    if [ -f /opt/$BUILD.img ]; then
	if [ ! "`fgrep /tmp/bak.$TMP_LABEL /etc/mtab`" ]; then # /tmp/bak.$TMP_LABEL未使用ならrescue.img（<--カーネルリメーク時）のバックアップも無し
	    echo "mount -t tmpfs -o size=${TMPFS_KB}K /dev/shm /tmp/bak.$TMP_LABEL"
	    _fs $LINENO mount -t tmpfs -o size=${TMPFS_KB}K /dev/shm /tmp/bak.$TMP_LABEL
	fi
	_img_backup BUILD /opt
	echo "mv /tmp/bak.$TMP_LABEL/$BUILD.gz /opt"
	mv /tmp/bak.$TMP_LABEL/$BUILD.gz /opt
    fi
    echo "dd if=/dev/zero of=$mnt_strage/opt/IMG0.img bs=1KB count=$_new_kb"
    dd if=/dev/zero of=$mnt_strage/opt/IMG0.img bs=1KB count=$_new_kb
    echo "df"
    df
    echo "mkfs.ext4 -q $mnt_strage/opt/IMG0.img"
    mkfs.ext4 -q $mnt_strage/opt/IMG0.img
    echo "mount -o loop $mnt_strage/opt/IMG0.img /tmp/loop0.$TMP_LABEL"
    _fs $LINENO mount -o loop $mnt_strage/opt/IMG0.img /tmp/loop0.$TMP_LABEL
    echo "cd /tmp/loop1.$TMP_LABEL"
    cd /tmp/loop1.$TMP_LABEL
    echo "tar cf - * | tar xfp - -C /tmp/loop0.$TMP_LABEL"
    tar cf - * | tar xfp - -C /tmp/loop0.$TMP_LABEL
    echo '"${_new_gb}GB-base" > /tmp/loop0.$TMP_LABEL/etc/hostname'
    echo "${_new_gb}GB-base" > /tmp/loop0.$TMP_LABEL/etc/hostname
    echo 'sed -i /tmp/loop0.$TMP_LABEL/etc/hosts -e "s/$DEBIAN/${_new_gb}GB-base/"'
    sed -i /tmp/loop0.$TMP_LABEL/etc/hosts -e "s/$DEBIAN/${_new_gb}GB-base/"
    echo "cd /opt"
    cd /opt
    echo "umount /tmp/loop0.$TMP_LABEL /tmp/loop1.$TMP_LABEL /tmp/shm0.$TMP_LABEL"
    _fs $LINENO umount /tmp/loop0.$TMP_LABEL /tmp/loop1.$TMP_LABEL /tmp/shm0.$TMP_LABEL
    echo "gzip -c $mnt_strage/opt/IMG0.img > /opt/$_new_base.gz"
    gzip -c $mnt_strage/opt/IMG0.img > /opt/$_new_base.gz
    echo "rm $mnt_strage/opt/IMG0.img"
    rm $mnt_strage/opt/IMG0.img
    echo "df /"
    df /
    echo "ls -l /opt/RAM-*"
    ls -l /opt/RAM-*
    if [ ! "`fgrep $_new_base /etc/grub.d/40_custom`" ]; then
	cat <<EOF >> /etc/grub.d/40_custom
menuentry '$_new_base' {
      	insmod part_msdos
	insmod ext2
	set root='hd0,gpt3'
	search --no-floppy --fs-uuid --set=root $DEBIAN_ROOT_UUID
	echo	'Loading base:vmlinuz-$_new_module from $DEBIAN_ROOT_UUID ...'
	linux	/boot/vmlinuz-$_new_module root=/dev/ram0 ro
	echo	'Loading initial ramdisk ...'
	initrd 	/opt/$_new_base.gz
	savedefault
}
EOF
    fi
}

_output_u() {
    local _u="." _diff
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
       mokutil -ve \
       os-prober -ve \
       shim-helpers-amd64-signed -ve \
       shim-signed -ve \
       shim-signed-common -ve \
       shim-unsigned -ve \
       linux-headers -ve \
       linux-image | \
       sed -e 's/\tinstall//' -e 's/\tpurge//'"
    local _to_u="grep -v \
       ^+++ | \
       grep ^+ | \
       cut -c2-"
    
    _ARG="$1 $2 $3 $4 $5 $6 $7 $8 $9"
    _lineno=`echo $_ARG | awk '{print $NF}'`
    _lineno=`echo $_lineno | egrep '^[0-9]+$'`
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
	    echo "$_diff | $_to_u | tr '\n' ' '" | sh | xargs dpkg --purge
	    _u="current.u"
	fi
    elif [ "$2" = "-i" ]; then
	_diff="diff -ru /tmp/current.$TMP_LABEL /opt/$1"
	if [ "`$_diff`" ]; then
	    echo "$_diff | $_to_u | tr '\n' ' '" | sh | xargs apt install -y
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
	if [ "`echo $2 | grep \/$`" ]; then
	    _dir=${2%?}
	else
	    _dir=$2
	fi
	if [ -e "$_dir" ]; then
	    if [ ! "`fgrep $_dir /etc/mtab`" ]; then
		echo "$1: $_dir: not mounted." >> /tmp/log.$TMP_LABEL
	    else
		if [ ! -e $_dir/dev ] || [ ! -e $_dir/proc ] || [ ! -e $_dir/sys ]; then
		    echo "$1: $_dir: not a root system." >> /tmp/log.$TMP_LABEL
		else
		    _chroot_dir=$_dir
		fi
	    fi
	else
	    echo "$1: $_dir: No such directory." >> /tmp/log.$TMP_LABEL
	fi
    fi
    if [ "$_chroot_dir" ]; then
	echo "chroot: $_chroot_dir"
	for _dir in dev dev/pts proc sys
	do
	    _fs $LINENO mount --bind /$_dir $_chroot_dir/$_dir
	done
	if [ "$3" ]; then
	    chroot $_chroot_dir <<EOF
$3
EOF
	else
	    chroot $_chroot_dir
	fi
	cd /opt
	_fs $LINENO umount -R $_chroot_dir
    fi
}

_list_strage() {
    local _i=$1 _j=2 _fs=$2
    local _device _uuid _line _line_list _strage_gb

#    _line_list=`blkid | fgrep -v /dev/ram | fgrep -v $DEBIAN_ROOT_UUID | grep $_fs`
    _line_list=`blkid | fgrep -v /dev/ram | grep $_fs`
    _line=`echo $_line_list | awk -F/dev/ '{print $'$_j'}'`
    while [ "$_line" ]
    do
	_device=/dev/`echo $_line | awk -F: '{print $1}'`
	_uuid=`echo $_line | awk -F' UUID=' '{print $2}' | awk '{print $1}' | sed -e 's/\"//g'`
	if [ "$_uuid" ]; then
	    if [ "`grep ^$_device /etc/mtab`" ]; then
		cd /opt
		umount $_device
	    fi
	    _fs $LINENO mount -o rw UUID=$_uuid /tmp/mount.$TMP_LABEL
	    _strage_gb=$((`df $_device | grep $_device | awk '{print $4}'`/1000/1000))
	    if [ "$_i" -lt 10 ]; then
		echo " $_i)$_fs $_device $_uuid ($_strage_gb GB)"
	    else
		echo "$_i)$_fs $_device $_uuid ($_strage_gb GB)"
	    fi
	    while [ "`fgrep /tmp/mount.$TMP_LABEL /etc/mtab`" ]
	    do
		umount /tmp/mount.$TMP_LABEL
		sleep 1
	    done
	    _i=$((_i+1))
 	    echo "_i=$_i #$((_i-1)))STRAGE=/mnt/$_fs UUID=$_uuid" >> /tmp/strage.$TMP_LABEL
	fi
	_j=$((_j+1))
	_line=`echo $_line_list | awk -F/dev/ '{print $'$_j'}'`
    done
}

add_strage() { # -a)
    local _opt_img _STRAGE_INFO
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
    cat /dev/null > /tmp/strage.$TMP_LABEL
    echo " 0)TYPE DEVICENAME UUID (free)"
    _is_inst ntfs-3g +
    _list_strage $_i ntfs
    . /tmp/strage.$TMP_LABEL
    _is_inst exfat-fuse +
    _list_strage $_i exfat
    . /tmp/strage.$TMP_LABEL
    _list_strage $_i ext4
    . /tmp/strage.$TMP_LABEL
    echo "  )<EXIT>"
    read -p "Input No:" _n
    _STRAGE_INFO=`cat /tmp/strage.$TMP_LABEL | awk -F\# '{print $2}' | fgrep $_n\) | head -n1`
    echo
    if [ ! "$_n" ] || [ ! "`echo $_n | egrep '^[0-9]+$'`" ]; then
	echo "$1: $_n: Invalid No." >> /tmp/log.$TMP_LABEL
    elif [ "$_n" = "0" ]; then
	echo "STRAGE=\"\"" >> /tmp/opt.$TMP_LABEL
	mv /tmp/opt.$TMP_LABEL /opt/U.opt
	chmod 644 /opt/U.opt
	echo "$1: STRAGE=\"\" -> /opt/U.opt" >> /tmp/log.$TMP_LABEL
    elif [ "$_n" ]; then
	cd /opt
	if [ "$ON_RAMDISK" ]; then
	    _opt_img=$MNT_DEBIAN
	    mount $_opt_img
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
	if [ ! -d "$mnt_strage" ]; then
	    mkdir $mnt_strage
	fi
	if [ "$UUID" ]; then
	    grep -ve /mnt/ext4 -ve /mnt/ntfs -ve /mnt/exfat -ve ^$ /etc/fstab > /tmp/fstab.$TMP_LABEL
	    echo "UUID=$UUID $mnt_strage `echo $mnt_strage | awk -F /mnt/ '{print $2}'` rw,noauto 0 1" >> /tmp/fstab.$TMP_LABEL 
	    mv /tmp/fstab.$TMP_LABEL /etc/fstab
	    systemctl daemon-reload
	fi
	if [ ! "`fgrep $mnt_strage /etc/mtab`" ]; then
	    _fs $LINENO mount $mnt_strage
	fi
	if [ ! -d "$mnt_strage/opt" ]; then
	    mkdir $mnt_strage/opt
	fi
	df
	if [ "$((`df $mnt_strage | grep $mnt_strage | awk '{print $4}'`/1000/1000))" -gt 10 ]; then
	    if [ "`ls $_opt_img/opt/*.img 2>/dev/null | grep 'GB-'`" ]; then
		echo "cp -iuv $_opt_img/opt/*GB-*.img $mnt_strage/opt"
		cp -iuv $_opt_img/opt/*GB-*.img $mnt_strage/opt
	    fi
	    if [ -f $_opt_img/opt/$BUILD.img ]; then
		echo "cp -iuv $_opt_img/opt/$BUILD.img $mnt_strage/opt"
		cp -iuv $_opt_img/opt/$BUILD.img $mnt_strage/opt
	    elif [ -f $_opt_img/opt/$BUILD.gz ] && [ ! -f $mnt_strage/opt/$BUILD.img ]; then
		echo "zcat $_opt_img/opt/$BUILD.gz > $mnt_strage/opt/$BUILD.img"
		zcat $_opt_img/opt/$BUILD.gz > $mnt_strage/opt/$BUILD.img
	    fi
	fi
	echo
	echo "STRAGE($mnt_strage/opt):"
	ls -l $mnt_strage/opt/ | grep -v total
	echo
    fi
}

change_root() { # -c)
    if ( [ "$ON_NONE" ] || [ "$ON_WSL" ] ) && [ ! "$2" ]; then
	echo "$1: Not on a RAMDISK. : $LINENO" >> /tmp/log.$TMP_LABEL
    fi
    _change_root $1 $2
}

_dup_gz() {
    local _base_new=$1

    echo "mount $mnt_strage"
    _fs $LINENO mount $mnt_strage
    echo "zcat $MNT_DEBIAN/opt/$_base_new.gz > $mnt_strage/opt/IMG0.img"
    zcat $MNT_DEBIAN/opt/$_base_new.gz > $mnt_strage/opt/IMG0.img 2> /tmp/error.$TMP_LABEL
    if [ -s /tmp/error.$TMP_LABEL ]; then
	echo "$mnt_strage/opt/IMG0.img: `cat /tmp/error.$TMP_LABEL`"
    else
	echo "mount -o loop $mnt_strage/opt/IMG0.img /tmp/loop0.$TMP_LABEL"
	_fs $LINENO mount -o loop $mnt_strage/opt/IMG0.img /tmp/loop0.$TMP_LABEL
	echo "_correct_uuid /tmp/loop0.$TMP_LABEL/etc/fstab $DEBIAN_ROOT_PART $_DST_ROOT_PART $_DST_EFI_PART"
	_correct_uuid /tmp/loop0.$TMP_LABEL/etc/fstab $DEBIAN_ROOT_PART $_DST_ROOT_PART $_DST_EFI_PART
	echo "umount /tmp/loop0.$TMP_LABEL"
	_fs $LINENO umount /tmp/loop0.$TMP_LABEL
	echo "gzip -c $mnt_strage/opt/IMG0.img > /mnt/usb/opt/$_base_new.gz"
	gzip -c $mnt_strage/opt/IMG0.img > /mnt/usb/opt/$_base_new.gz
    fi
}

make_usb() { # -m)
    local _opt_dup _OPT_DUP
    local _SRC_PART _DST_PART _DEV_SD
    local _DST_EFI_PART _DST_ROOT_PART
    local _DST_STRAGE_PART
    local _module_other _MODULE_OTHER_LIST
    local _base_rescue _base_desktop _base_other
    local _entry _ENTRY_LIST
    local _module_rescue _module_desktop
    local _GRUB_DEFAULT _i _j _n _yn _cat=$3

    if [ ! "$ON_RAMDISK" ]; then
	echo "$1: Not on a RAMDISK. : $LINENO" >> /tmp/log.$TMP_LABEL
	return
    fi
    if [ ! "$2" ] || ( [ "$3" ] && [ "$3" != "-" ] ); then
	echo "$1: Please type '. U -d devicename (-)'[Enter] : $LINENO" >> /tmp/log.$TMP_LABEL
	return
    fi
    if [ "`fgrep $MNT_DEBIAN /etc/mtab`" ]; then
	cd /opt
	_fs $LINENO umount $MNT_DEBIAN
	if [ "`fgrep $MNT_DEBIAN /etc/mtab`" ]; then
	    echo "$1: $MNT_DEBIAN: busy!" >> /tmp/log.$TMP_LABEL
	    return
	fi
    fi
    _fs $LINENO mount $MNT_DEBIAN
    _module_rescue=`ls -t $MNT_DEBIAN/lib/modules | grep ${RESCUE_GB}gb | head -n1`
    _base_rescue=RAM-$_module_rescue-base
    _module_desktop=`ls -t $MNT_DEBIAN/lib/modules | grep ${DESKTOP_GB}gb | head -n1`
    _base_desktop=RAM-$_module_desktop-base
    _fs $LINENO umount $MNT_DEBIAN
    _SRC_PART=${DEBIAN_ROOT_PART%?}
    _DST_PART=/dev/$2
    _DEV_SD=`echo $2 | grep ^sd`
    if [ "`echo $_SRC_PART | grep p$`" ] && [ ! "$_DEV_SD" ]; then
	_SRC_PART=${_SRC_PART%?}
    fi
    if [ "$_DEV_SD" ]; then
	_DST_EFI_PART=${_DST_PART}1
	_DST_ROOT_PART=${_DST_PART}3
	_DST_STRAGE_PART=${_DST_PART}4
    else
	_DST_EFI_PART=${_DST_PART}p1
	_DST_ROOT_PART=${_DST_PART}p3
	_DST_STRAGE_PART=${_DST_PART}p4
    fi
    _unmount_part $_SRC_PART
    _unmount_part $_DST_PART
    if [ "`fgrep $_DST_PART /etc/mtab`" ]; then
	echo "$1: $_DST_PART: Busy." >> /tmp/log.$TMP_LABEL
	return
    fi
    if [ ! "`blkid | fgrep $_DST_PART`" ]; then
	echo "$1: $_DST_PART: No such device." >> /tmp/log.$TMP_LABEL
	return
    fi
    blkid | grep $_DST_PART | awk -FBLOCK_SIZE '{print $1}'
    read -p "Copy to $_DST_ROOT_PART? (Y/N): " _yn
    if [ "$_yn" = "Y" ] || [ "$_yn" = "y" ]; then
	if [ "$_DST_STRAGE_PART" ]; then
	    _is_inst gdisk +
	    echo "sgdisk -Z $_DST_PART"
	    sgdisk -Z $_DST_PART
	    sleep 2
	    echo "gdisk $_DST_PART"
	    gdisk $_DST_PART <<EOF
n


+100MB
ef00
n


+1MB
ef02
n


+15GB

n




p
w
y
EOF
	    sleep 2
	fi
	echo "mkfs.ext4 -q $_DST_ROOT_PART"
	mkfs.ext4 -q $_DST_ROOT_PART <<EOF
y
EOF
	echo "cd /opt"
	cd /opt
	if [ "$_cat" ]; then
	    echo "cat $DEBIAN_ROOT_PART > $_DST_ROOT_PART 2>/tmp/error.$TMP_LABEL"
	    cat $DEBIAN_ROOT_PART > $_DST_ROOT_PART 2>/tmp/error.$TMP_LABEL
	else
	    echo "mount $MNT_DEBIAN"
	    _fs $LINENO mount $MNT_DEBIAN
	    echo "mount $_DST_ROOT_PART /mnt/usb"
	    _fs $LINENO mount $_DST_ROOT_PART /mnt/usb
	    echo "cd $MNT_DEBIAN"
	    cd $MNT_DEBIAN
	    echo "tar cf - boot [e-i]* opt root srv [u-v]* | tar xfpvv - -C /mnt/usb 2>/tmp/error.$TMP_LABEL"
	    tar cf - boot [e-i]* opt root srv [u-v]* | tar xfpvv - -C /mnt/usb 2>/tmp/error.$TMP_LABEL
	    if [ ! -s /tmp/error.$TMP_LABEL ]; then
		_tmp_debian -
		echo "cd /tmp/$DEBIAN.$TMP_LABEL"
		cd /tmp/$DEBIAN.$TMP_LABEL
		echo "tar cf - * | tar xfp - -C /mnt/usb"
		tar cf - * | tar xfp - -C /mnt/usb
		_fs $LINENO umount $MNT_DEBIAN
		_fs $LINENO umount /mnt/usb
	    fi
	fi
	if [ -s /tmp/error.$TMP_LABEL ]; then
	    echo "$1: `cat /tmp/error.$TMP_LABEL` : $LINENO" | tee -a /tmp/log.$TMP_LABEL
	else
	    echo "e2fsck -f $_DST_ROOT_PART"
	    e2fsck -f $_DST_ROOT_PART
	    _is_inst uuid-runtime +
	    echo "tune2fs -U `uuidgen` $_DST_ROOT_PART"
	    tune2fs -U `uuidgen` $_DST_ROOT_PART <<EOF
y
EOF
	    echo "mount $_DST_ROOT_PART /mnt/usb"
	    _fs $LINENO mount $_DST_ROOT_PART /mnt/usb
	    if [ -f /mnt/usb/opt/U.opt ]; then
		rm /mnt/usb/opt/U.opt
	    fi
	    echo "mount $MNT_DEBIAN"
	    _fs $LINENO mount $MNT_DEBIAN
	    echo "cd $MNT_DEBIAN"
	    cd $MNT_DEBIAN # _diff_sh() による『_fs $LINENO umount $MNT_DEBIAN』回避
	    echo "mkfs.vfat $_DST_EFI_PART"
	    mkfs.vfat $_DST_EFI_PART
	    sleep 1
	    echo "mkdir -p /mnt/usb/boot/efi"
	    mkdir -p /mnt/usb/boot/efi
	    echo "_correct_uuid /mnt/usb/etc/fstab $DEBIAN_ROOT_PART $_DST_ROOT_PART $_DST_EFI_PART"
	    _correct_uuid /mnt/usb/etc/fstab $DEBIAN_ROOT_PART $_DST_ROOT_PART $_DST_EFI_PART
	    echo "_correct_uuid /mnt/usb/etc/grub.d/40_custom $DEBIAN_ROOT_PART $_DST_ROOT_PART"
	    _correct_uuid /mnt/usb/etc/grub.d/40_custom $DEBIAN_ROOT_PART $_DST_ROOT_PART
	    echo "chmod 755 /mnt/usb/etc/grub.d/40_custom"
	    chmod 755 /mnt/usb/etc/grub.d/40_custom
	    if [ "$mnt_strage" = "$MNT_DEBIAN" ]; then
		rm $MNT_DEBIAN/opt/$BUILD.gz $MNT_DEBIAN/opt/${RESCUE_GB}GB-rescue.img # 4GBイメージ用の空き確保（/mnt/usb/optから復旧）
	    fi
	    _dup_gz $TEMPLATE_BASE
	    _dup_gz $_base_rescue
	    _MODULE_OTHER_LIST=`ls $MNT_DEBIAN/lib/modules | grep -ve ${RESCUE_GB}gb$ -ve ${DESKTOP_GB}gb$ | fgrep +`
	    for _module_other in $_MODULE_OTHER_LIST
	    do
		_base_other=RAM-$_module_other-base
		_dup_gz $_base_other
	    done
	    echo "rm $mnt_strage/opt/IMG0.img"
	    rm $mnt_strage/opt/IMG0.img
	    if [ "$mnt_strage" = "$MNT_DEBIAN" ]; then
		echo "cp -v /mnt/usb/opt/$BUILD.gz /mnt/usb/opt/${RESCUE_GB}GB-rescue.img $MNT_DEBIAN/opt"
		cp -v /mnt/usb/opt/$BUILD.gz /mnt/usb/opt/${RESCUE_GB}GB-rescue.img $MNT_DEBIAN/opt # USBより復旧
	    fi
	    if [ "$_DST_STRAGE_PART" ]; then
		if [ "$DST_STRAGE_FS"="ext4" ]; then
		    echo "mkfs.ext4 -q $_DST_STRAGE_PART"
		    mkfs.ext4 -q $_DST_STRAGE_PART <<EOF
y
EOF
		elif [ "$DST_STRAGE_FS"="vfat" ]; then
		    echo "gdisk $_DST_PART"
		    gdisk $_DST_PART <<EOF
t
4
0700
p
w
y
sleep 1
echo "mkfs.vfat $_DST_STRAGE_PART"
mkfs.vfat $_DST_STRAGE_PART
EOF
		else
		    dd if=/dev/zero of=${_DST_PART}4 bs=512 count=1
		fi
		_j=2
		_i=2
		_ENTRY_LIST=`grep menuentry /mnt/usb/etc/grub.d/40_custom | sed -e 's/{//' | awk -Fmenuentry '{print $'$_j'}'`
		_entry=`echo $_ENTRY_LIST | awk -F\' '{print $'$_j'}'`
		while [ ! "`echo $_entry | grep +\${RESCUE_GB}gb-base`" ] && [ $_i -lt 30 ]
		do
		    _i=$((_i+1))
		    _j=$((_j+2))
		    _entry=`echo $_ENTRY_LIST | awk -F\' '{print $'$_j'}'`
		done
		_n=$_i
		_GRUB_DEFAULT=`fgrep GRUB_DEFAULT= /mnt/usb/etc/default/grub`
		sed -i /mnt/usb/etc/default/grub -e s/$_GRUB_DEFAULT/GRUB_DEFAULT=$_n/
		echo "STRAGE=\"\"" >  /mnt/usb/opt/U.opt
		echo "NEXT${RESCUE_GB}=${RESCUE_GB}GB-rescue" >> /mnt/usb/opt/U.opt
		echo "SH=bash" >> /mnt/usb/opt/U.opt
		echo "mount $_DST_EFI_PART /mnt/usb/boot/efi"
		_fs $LINENO mount $_DST_EFI_PART /mnt/usb/boot/efi
		sleep 1
		_change_root $1 /mnt/usb "
update-grub 2> /dev/null
grub-install $_DST_PART
apt install -y grub-legacy
apt install -y grub-efi-amd64
apt autoremove -y
grub-install $_DST_PART --removable --target=x86_64-efi --no-uefi-secure-boot
"
	    fi
	fi
    fi
}

erase_base() { # -e)
    local _SSD_PART _SHM_PART _mounted_part _MOUNTED_PART_LIST
    local _pkg _yn _option=$1

    if [ ! "$ON_NONE" ] && [ ! "$ON_WSL" ]; then
	echo "$_option: Not on a NONE-KERNEL. : $LINENO" >> /tmp/log.$TMP_LABEL
	return
    fi
    if [ ! -f /opt/BASE.u ]; then
	echo "$_option: BASE.u: No such file.  Please type 'sudo touch /opt/BASE.u'[Enter] : $LINENO" >> /tmp/log.$TMP_LABEL
	return
    fi
    read -p "Delete base? (Y/N): " _yn
    if [ "$_yn" = "Y" ] || [ "$_yn" = "y" ]; then
	cd /usr/src
	rm -f build CONFIG_NONE
	rm -rf linux-source-$KERNEL_VERSION linux-config-$KERNEL_VERSION
	cd -
	_fs $LINENO umount $OPT_BUILD
	_fs $LINENO umount $mnt_strage
	if [ "`dpkg -l | grep light-locker`" ] && [ "`ps auxw | grep light-locker | grep -v grep`" ]; then
	    killall light-locker
	fi
	for _pkg in "`dpkg -l | grep linux-image | awk '{print $2}' | grep $KERNEL_VERSION | grep +`"
	do
	    if [ "$_pkg" ]; then
		dpkg --purge $_pkg
	    fi
	done
	cd /opt
	_SSD_PART=`grep ^/dev /etc/mtab | grep -ve $DEBIAN_ROOT_PART -ve ^/dev/shm | awk '{print $1}'`
	_SHM_PART=`grep ^/dev/shm /etc/mtab | awk '{print $2}'`
	_MOUNTED_PART_LIST="${_SSD_PART} ${_SHM_PART}"
	echo $_SSD_PART | 
	    while read _mounted_part
	    do
		_fs $LINENO umount $_mounted_part
	    done
	echo $_SHM_PART | 
	    while read _mounted_part
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
	    echo "head -n5 /etc/grub.d/40_custom > /tmp/40_custom.$TMP_LABEL"
	    head -n5 /etc/grub.d/40_custom > /tmp/40_custom.$TMP_LABEL
	    echo "mv -f /tmp/40_custom.$TMP_LABEL /etc/grub.d/40_custom"
	    mv /tmp/40_custom.$TMP_LABEL /etc/grub.d/40_custom
	    echo "chmod 755 /etc/grub.d/40_custom"
	    chmod 755 /etc/grub.d/40_custom
	    echo "mkdir -p /tmp/bak.$TMP_LABEL"
	    mkdir -p /tmp/bak.$TMP_LABEL
	    echo "cd /opt"
	    cd /opt
	    echo "mv -fv *.sh U.opt $BUILD.gz DEBIAN.u grub.ORG *.deb /tmp/bak.$TMP_LABEL"
	    mv -fv *.sh U.opt $BUILD.gz DEBIAN.u grub.ORG *.deb /tmp/bak.$TMP_LABEL
	    echo "rm -rf * /usr/local/bin/U"
	    rm -rf * /usr/local/bin/U
	    echo "cd /tmp/bak.$TMP_LABEL/"
	    cd /tmp/bak.$TMP_LABEL/
	    echo "mv -fv *.sh U.opt $BUILD.gz DEBIAN.u grub.ORG *.deb /opt"
	    mv -fv *.sh U.opt $BUILD.gz DEBIAN.u grub.ORG *.deb /opt
	    echo "cd -"
	    cd -
	    echo "update-grub"
	    update-grub
	    _output_u . $LINENO
	    echo "Please type 'sudo bash (or dash) /opt/U.sh'[Enter]" >> /tmp/log.$TMP_LABEL
	fi
	ls -l /opt | grep -v total
    fi
}

_diff_sh() {
    local _log
    
    if [ ! "$DIFF_SH" ]; then
	return
    fi
    while [ -f "/opt/BASE.u" ]
    do
	while [ "`ls -t /opt | head -n1`" != "U.sh" ]
	do
	    sleep 1
	done
	touch /opt/BASE.u
	_fs $LINENO mount $MNT_DEBIAN
	if [ ! "`$DIFF_SH $MNT_DEBIAN/opt/U.sh /opt/U.sh | tee $MNT_DEBIAN/opt/U.diff`" ]; then
	    rm $MNT_DEBIAN/opt/U.diff /opt/U.diff
	else
	    cp $MNT_DEBIAN/opt/U.diff /opt
	fi
	for _log in `ls /opt/U.log_*`
	do
	    cat $_log >> $MNT_DEBIAN/opt/$_log
	done
	_fs $LINENO umount $MNT_DEBIAN
	sleep 1
    done
}

_img_backup() {
    if [ ! -f $2/$1.img ]; then
	return
    fi
    echo "gzip -c $2/$1.img > /tmp/bak.$TMP_LABEL/$1.gz"
    gzip -c $2/$1.img > /tmp/bak.$TMP_LABEL/$1.gz # 8GBイメージ用の空きを/optに確保
    echo "rm $2/$1.img"
    rm $2/$1.img
}

kernel_remake() { # -k)
    if [ "$ON_NONE" ]; then
	if [ "$mnt_strage" = "$MNT_DEBIAN" ]; then # DEBIAN稼働でSTRAGE無しだと$BUILD.imgを展開するスペースが無い
	    echo "$1: Strage-area needed." >> /tmp/log.$TMP_LABEL
	    return
	fi
	echo "umount /tmp/bak.$TMP_LABEL"
	_fs $LINENO umount /tmp/bak.$TMP_LABEL
	echo "mount -t tmpfs -o size=${TMPFS_KB}K /dev/shm /tmp/bak.$TMP_LABEL"
	_fs $LINENO mount -t tmpfs -o size=${TMPFS_KB}K /dev/shm /tmp/bak.$TMP_LABEL
	_img_backup ${RESCUE_GB}GB-rescue /opt
    fi
    if [ "$ON_WSL" ]; then # WSL用カーネルのカレント設定はパス
	if [ "$2" = "." ]; then 
	    echo "$1: Please type '. U -k ??(GB)'[Enter]" >> /tmp/log.$TMP_LABEL
	    return
	fi
    fi
    _is_inst openssl +
    _compile $1 $2 $3 $4
    if [ "$ON_NONE" ]; then
	echo "zcat /tmp/bak.$TMP_LABEL/${RESCUE_GB}GB-rescue.gz > /opt/${RESCUE_GB}GB-rescue.img"
	zcat /tmp/bak.$TMP_LABEL/${RESCUE_GB}GB-rescue.gz > /opt/${RESCUE_GB}GB-rescue.img
        echo "umount /tmp/bak.$TMP_LABEL"
	_fs $LINENO umount /tmp/bak.$TMP_LABEL
    fi
}

next_host() { # -n)
    local _next_host _next_gb _new_gb
    local _GRUB_DEFAULT
    local _base_list _host_list _real_list
    local _entry _ENTRY_LIST _next _NEXT_LIST
    local _gb _yn _i=0 _j=2 _k=0 _n=0

    if [ "$ON_WSL" ]; then
	echo "$1: Not supported. : $LINENO" >> /tmp/log.$TMP_LABEL
	return
    fi
    if [ "$SH" = "bash" ]; then
	declare -A num
    elif [ "$SH" = "dash" ]; then
	cat /dev/null > /tmp/num.$TMP_LABEL
    fi
    _fs $LINENO mount $MNT_DEBIAN
    _fs $LINENO mount $mnt_strage
    _base_list=`ls $MNT_DEBIAN/opt/RAM* | grep + | awk -F+ '{print $2}' | awk -F. '{print $1}' | sed -e 's/gb/GB/' | sort -g`
    _base_list=\'$_base_list\'
    _base_list=`echo $_base_list | sed -e "s/ /' '/g"`
    _host_list=`ls -r $mnt_strage/opt | grep img$ 2>/dev/null | grep -e ^.GB -e ^..GB -e ^...GB | awk -F\. '{print $1}' | sort -g` # ファイル名｢?GB-DESKTOP.img｣から｢.img｣撤去
    _host_list=\'$_host_list\'
    _host_list=`echo $_host_list | sed -e "s/ /' '/g"`
    _ENTRY_LIST=`grep menuentry $MNT_DEBIAN/etc/grub.d/40_custom | grep -v + | sed -e 's/{//' | awk -Fmenuentry '{print $2}'`
    _NEXT_LIST="${_ENTRY_LIST} ${_base_list} ${_host_list}"
    _next=`echo $_NEXT_LIST | awk -F\' '{print $'$_j'}'`
    while [ "$_next" ]
    do
	if [ "$_next" ]; then
	    _gb=`echo $_next | grep GB`
	    if [ "$_gb" ] && [ "`echo $_gb | awk -FGB '{print $1}'`" -gt $MEM_TOTAL_GB ]; then
		echo "  )$_next"
		_k=$((_k+1))
	    else
		if [ "$_i" -lt 10 ]; then
		    echo " $_i)$_next"
		else
		    echo "$_i)$_next"
		fi
		_real_list="$_real_list $_next"
		if [ "$SH" = "bash" ]; then
		    num["$_i"]=$_k
		elif [ "$SH" = "dash" ]; then
		    echo "$_i $_k" >> /tmp/num.$TMP_LABEL
		fi
		_i=$((_i+1))
		_k=$((_k+1))
	    fi
	    _j=$((_j+2))
	fi
	_next=`echo $_NEXT_LIST | awk -F\' '{print $'$_j'}'`
    done
    echo "  )<EXIT>"
    read -p "Input No:" _n
    if [ ! "$_n" ] || [ ! "`echo $_n | egrep '^[0-9]+$'`" ]; then
	if [ "$3" = "." ]; then
	    if [ "`hostname`" = "debian" ]; then
		_next_host=${RAMDISK_GB}GB-base
	    else
		_next_host=`hostname`
	    fi
	else
	    _n=-
	fi
    else
	if [ "$SH" = "bash" ]; then
	    _n="${num["$_n"]}"
	elif [ "$SH" = "dash" ]; then
	    _n=`grep ^\$_n /tmp/num.$TMP_LABEL | awk '{print $2}'`
	fi
	_n=$(((_n+1)*2))
	_next_host=`echo $_NEXT_LIST | awk -F\' '{print $'$_n'}' | sed -e "s/'//g"`
	if [ "$_next_host" ]; then
	    _new_gb=""
	    if [ "`echo $_next_host | grep ' '`" ]; then
		_next_host=\'$_next_host\'
	    elif [ "`echo $_next_host | grep 'GB-'`" ]; then
		_new_gb=`echo $_next_host | awk -FGB '{print $1}'`
	    fi
	fi
    fi
    if [ "$_n" != - ] && [ "$_next_host" ]; then
	if [ ! "`echo $_next_host | grep .GB-`" ]; then
	    _next_gb=NEXT
	else
	    _next_gb=NEXT${_new_gb}
	fi
	if [ -f /opt/U.opt ]; then
	    grep -v ^$_next_gb= /opt/U.opt > /tmp/opt.$TMP_LABEL
	else
	    cat /dev/null > /tmp/opt.$TMP_LABEL
	fi
	echo "$_next_gb=$_next_host" >> /tmp/opt.$TMP_LABEL
	mv /tmp/opt.$TMP_LABEL /opt/U.opt
	chmod 644 /opt/U.opt
	echo
	fgrep "$_next_host" /opt/U.opt
	echo ">/opt/U.opt"
	echo
	_next_host=`echo $_next_host | sed -e "s/'//g"`
	_i=2
	_j=2
	_ENTRY_LIST=`grep menuentry $MNT_DEBIAN/etc/grub.d/40_custom | sed -e 's/{//' | awk -Fmenuentry '{print $'$_j'}'`
	_entry=`echo $_ENTRY_LIST | awk -F\' '{print $'$_j'}'`
	while [ "$_entry" != "$_next_host" ] && [ ! "`echo $_entry | fgrep ${_new_gb}gb`" ]
	do
	    _i=$((_i+1))
	    _j=$((_j+2))
	    _entry=`echo $_ENTRY_LIST | awk -F\' '{print $'$_j'}'`
	done
	_n=$_i
	_GRUB_DEFAULT=`fgrep GRUB_DEFAULT= $MNT_DEBIAN/etc/default/grub`
	if [ "$_GRUB_DEFAULT" != "GRUB_DEFAULT=$_n" ]; then
	    sed -i $MNT_DEBIAN/etc/default/grub -e s/$_GRUB_DEFAULT/GRUB_DEFAULT=$_n/
	    if [ "$ON_RAMDISK" ]; then
		cp /opt/U.opt $MNT_DEBIAN/opt
		_change_root $1 $MNT_DEBIAN "
                	     update-grub 2>/dev/null
                "
	    else
		update-grub 2>/dev/null
	    fi
	fi
	read -p "Reboot now? (Y/n): " _yn
	if [ ! "$_yn" ] || [ "$_yn" = "Y" ] || [ "$_yn" = "y" ]; then
	    reboot
	fi
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
    if [ ! "$ON_EXTERN" ] && [ -f "$NETWORK_MANAGER_CONF" ]; then # Debian: ifupdown から NetworkManager へ
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
    for _unit in `_enabled_units`
    do
	case "$_unit" in
	    rc-local.service|display-manager.service|gdm*|lightdm*|sddm*|wdm*|xdm*) # 自分自身（rc.local）と DM は除外
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
    if [ "$1" = "-" ]; then
	echo "mkdir -p $DUP_DIR_LIST"
	mkdir -p $DUP_DIR_LIST
    else
	echo "mkdir -p $RAM_DIR_LIST"
	mkdir -p $RAM_DIR_LIST
    fi
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
	echo 'fgrep -ve /boot/efi -ve /mnt/ext4 -ve /mnt/ntfs -ve /mnt/exfat /etc/fstab | sed -e "s/UUID=$DEBIAN_ROOT_UUID/\/dev\/ram/" > etc/fstab'
	fgrep -ve /mnt/debian -ve /boot/efi -ve /mnt/ext4 -ve /mnt/ntfs -ve /mnt/exfat /etc/fstab | sed -e "s/UUID=$DEBIAN_ROOT_UUID/\/dev\/ram/" > etc/fstab
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
    if [ -f "/opt/BASE.u" ]; then
	return
    fi
    local _GRUB_DEFAULT
    local _shm
    local _next_host _next_gb
    local _work_area=$WORK_AREA _i=0
    local _TMPFS_OPT="-o size=${TMPFS_KB}K"

    timedatectl set-local-rtc 0
    if [ "$SHM_LIST" ] && [ "$MEM_TOTAL_GB" -ge "$RESCUE_GB" ]; then
	cd /
	mkdir -p /opt/shm.$TMP_LABEL
	for _shm in $SHM_LIST
	do
	    mkdir -p /opt/shm.$TMP_LABEL/$_shm
	    mv /$_shm/* /$_shm/.*  /opt/shm.$TMP_LABEL/$_shm 2>/dev/null
	    _fs $LINENO mount -t tmpfs $_TMPFS_OPT /dev/shm /$_shm
	    mv /opt/shm.$TMP_LABEL/$_shm/.* /opt/shm.$TMP_LABEL/$_shm/* /$_shm 2>/dev/null
	done
    fi
    _fs $LINENO mount $MNT_DEBIAN
    _GRUB_DEFAULT=`fgrep GRUB_DEFAULT= $MNT_DEBIAN/etc/default/grub`
    sed -i $MNT_DEBIAN/etc/default/grub -e s/$_GRUB_DEFAULT/GRUB_DEFAULT=saved/ #_42)　/etc/default/grubのGRUB
    _change_root $1 $MNT_DEBIAN "
update-grub 2>/dev/null
    "
    _fs $LINENO mount $MNT_DEBIAN
    _fs $LINENO mount $mnt_strage
    cp $MNT_DEBIAN/opt/BASE.u /opt
    cd /opt
    if [ "$RAMDISK_GB" = "$TASKSEL_GB" ] && [ "$mnt_strage" != "$MNT_DEBIAN" ] && [ "$TASKSEL_LIST" ] && [ ! "`ls $mnt_strage/opt/${TASKSEL_GB}GB-* 2>/dev/null`" ]; then
	if [ ! "`_on_line`" ]; then
	    _net_restart
	fi
	apt-get update -y
	apt-get upgrade -y
	_i=1
	_install_standard # 標準システムユーティリティをインストール
	_output_u STD.u # ｢標準システム｣環境のパッケージ構成リストを保存
	apt-get install -y $JP_LIST # 日本語デスクトップをインストール
	_output_u JP.u # ｢標準システム＋日本語デスクトップ｣環境のパッケージ構成リストを保存
	for _tasksel in $TASKSEL_LIST
	do
	    _desktop=`echo $DESKTOP_LIST | awk '{print $'$_i'_i}'`
	    if [ "$ON_EXTERN" ]; then
		echo "apt install -y $DESKTOP_APT_OPT $_tasksel"
		apt install -y $DESKTOP_APT_OPT $_tasksel
		_desktop_extern on
	    else
		apt install -y task-$_tasksel 2> /dev/null
	    fi
	    _save_host $1 $_desktop . # 8GBイメージを保存（/opt/8GB-$DESKTOP.img）
	    _output_u JP.u -p # パッケージ構成をJP.uまで戻す
	    if [ "$ON_EXTERN" ]; then
		_desktop_extern off
	    fi
	    _i=$((_i+1))
	    if [ ! "`_on_line`" ]; then
		_net_restart
	    fi
	done
	rm /opt/JP.u
	if [ "$RESCUE_LIST_X" ] && [ ! -f $mnt_strage/opt/${RAMDISK_GB}GB-rescue.img ]; then
	    _output_u STD.u -p # パッケージ構成をSTD.uまで戻す
	    apt-get install -y $RESCUE_LIST_X
	    if [ "$BUILD_LIST" ]; then
		apt $BUILD_LIST -y 2>/tmp/error.$TMP_LABEL
		if [ -s /tmp/error.$TMP_LABEL ] && [ "$PURGE_LIST" ]; then
		    echo "$1: `cat /tmp/error.$TMP_LABEL` : $LINENO" | tee -a /tmp/log.$TMP_LABEL
		    dpkg --purge $PURGE_LIST
		fi
	    fi
	    if [ "$ON_EXTERN" ]; then
		_desktop_extern on
	    fi
	    _save_host $1 rescue . # 8GBイメージを保存（/opt/8GB-$DESKTOP.img）
	fi
	rm /opt/STD.u
	_output_u BASE.u -p # パッケージ構成をBASE.u（HOST名が｢8GB-base｣）まで戻す
	if [ "$ON_EXTERN" ]; then
	    _desktop_extern off
	fi
	_next_host=${RAMDISK_GB}GB-base
	cat <<EOF >> /home/$USR/.bash_aliases
LANG=C . U -n # for next_host
grep -v "for next_host" /home/$USR/.bash_aliases > /tmp/bash_aliases.$TMP_LABEL # for next_host
cp /tmp/bash_aliases.$TMP_LABEL /home/$USR/.bash_aliases # for next_host
chown $USR:$USR /home/$USR/.bash_aliases # for next_host
EOF
    else
	if [ "$RAMDISK_GB" = "$RESCUE_GB" ] && [ ! -f $MNT_DEBIAN/opt/${RESCUE_GB}GB-rescue.img ]; then
	    if [ -f $mnt_strage/opt/${RESCUE_GB}GB-rescue.img ]; then
		cp $mnt_strage/opt/${RESCUE_GB}GB-rescue.img $MNT_DEBIAN/opt
	    else
		if [ -f $MNT_DEBIAN/opt/${RESCUE_GB}GB-rescue.u ]; then
		    _output_u ${RESCUE_GB}GB-rescue.u -i
		else
		    echo "apt-get update -y"
		    apt-get update -y
		    echo "apt-get upgrade -y"
		    apt-get upgrade -y
		    _install_standard #_36)　標準システムユーティリティをインストール
		    echo "apt-get install -y $RESCUE_LIST"
		    apt-get install -y $RESCUE_LIST
		fi
		if [ "$ON_EXTERN" ]; then
		    _desktop_extern on
		fi
		_work_area=opt
		_next_host="${RAMDISK_GB}GB-base"
		_save_host $1 rescue .
	    fi
	elif [ "$RESCUE_LIST_X" ] && [ "$mnt_strage" != "$MNT_DEBIAN" ] && [ ! -f $mnt_strage/opt/${RAMDISK_GB}GB-rescue.img ]; then
	    _install_standard # 標準システムユーティリティをインストール
	    apt-get install -y $RESCUE_LIST_X
	    if [ "$BUILD_LIST" ]; then
		apt $BUILD_LIST -y 2>/tmp/error.$TMP_LABEL
		if [ -s /tmp/error.$TMP_LABEL ] && [ "$PURGE_LIST" ]; then
		    echo "$1: `cat /tmp/error.$TMP_LABEL` : $LINENO" | tee -a /tmp/log.$TMP_LABEL
		    dpkg --purge $PURGE_LIST
		fi
	    fi
	    if [ "$ON_EXTERN" ]; then
		_desktop_extern on
	    fi
	    _save_host $1 rescue .
	    _next_host=${RAMDISK_GB}GB-rescue
	    echo "NEXT${RAMDISK_GB}=$_next_host" >> $MNT_DEBIAN/opt/U.opt
	    cat <<EOF >> /home/$USR/.bash_aliases
. U -n # next_host
EOF
	    grep -v "next_host" /home/$USR/.bash_aliases > /tmp/bash_aliases.$TMP_LABEL
	    mv /tmp/bash_aliases.$TMP_LABEL /home/$USR/.bash_aliases
	    chown $USR:$USR /home/$USR/.bash_aliases
	fi
	if [ ! "$_next_host" ]; then
	    _next_gb=NEXT${RAMDISK_GB}
	    _next_host=`grep ^\$_next_gb= /opt/U.opt | grep -v ^\$_next_gb=${RAMDISK_GB}GB-base | sed -e s/$_next_gb=// | head -n1`
	    if [ "$_next_host" ]; then
		if [ -f $mnt_strage/opt/$_next_host.img ]; then
		    _fs $LINENO mount -o loop $mnt_strage/opt/$_next_host.img /tmp/loop0.$TMP_LABEL
		    rm -f /tmp/loop0.$TMP_LABEL/etc/network/interfaces /tmp/loop0.$TMP_LABEL/etc/rc.local /tmp/loop0.$TMP_LABEL/usr/bin/tar #_40 イメージ内の不要なファイル削除
		    cd /tmp/loop0.$TMP_LABEL
		    tar cf - * | tar xfp - -C /
		    cd /opt
		    _fs $LINENO umount /tmp/loop0.$TMP_LABEL
		fi
	    else
		_next_host=${RAMDISK_GB}GB-base
	    fi
	fi
	DOT_FILES
	cat <<EOF > /tmp/hosts.$TMP_LABEL
127.0.0.1   	localhost
127.0.1.1	$_next_host
EOF
	if [ "$BAK_HOST" ]; then
	    echo $BAK_HOST|tr ' ' '\t' >> /tmp/hosts.$TMP_LABEL
	    grep -ve "127.0.0.1" -ve "127.0.1.1" -ve "`echo $BAK_HOST|awk '{print $1}'`" /etc/hosts >> /tmp/hosts.$TMP_LABEL
	else
	    grep -ve "127.0.0.1" -ve "127.0.1.1" /etc/hosts >> /tmp/hosts.$TMP_LABEL
	fi
	cp /tmp/hosts.$TMP_LABEL /etc/hosts
	if [ "`echo $_next_host | grep base$`" ]; then
	    grep -v ^NEXT${RESCUE_GB}= $MNT_DEBIAN/opt/U.opt > /tmp/opt.$TMP_LABEL
	    echo "NEXT${RESCUE_GB}=$_next_host" >> /tmp/opt.$TMP_LABEL
	    mv /tmp/opt.$TMP_LABEL $MNT_DEBIAN/opt/U.opt
	else
	    echo $_next_host > /etc/hostname # /opt/U.optの「NEXT?=」の値をホスト名に設定
	    hostnamectl set-hostname $_next_host
	    _start_rc
#	    cat <<EOF > /etc/hosts
#127.0.0.1   	localhost
#127.0.1.1	$_next_host
#
## The following lines are desirable for IPv6 capable hosts
#::1     localhost ip6-localhost ip6-loopback
#
#ff02::1 ip6-allnodes
#ff02::2 ip6-allrouters
#EOF
	fi
	if [ "$HOST" ]; then
	    grep -v "`echo $HOST|awk '{print $1}'`" /etc/hosts|grep -i -e "127.0.1.1" -e  "127.0.0.1"; echo $HOST|tr ' ' '\t' > /tmp/hosts.$TMP_LABEL
	    grep -ve "127.0.0.1" -ve "127.0.1.1" -ve "`echo $HOST|awk '{print $1}'`" /etc/hosts >> /tmp/hosts.$TMP_LABEL
	    mv /tmp/hosts.$TMP_LABEL /etc/hosts
	fi
    fi
    cd /opt
    _diff_sh &
}

_save_host() {
    local _new_host _hostname _host_list _ram_img_kb
    local _DEBIAN_FREE_KB _SAVED_HOST _plus
    local _work_area=$WORK_AREA
    local _numerator=$NUMERATOR
    local _RAMDISK_KB=$((RAMDISK_GB*1000*1000))

    if [ "`hostname`" = "base" ] && [ "$2" = "." ]; then
	echo "$1: Can't save to base-host. : $LINENO" >> /tmp/log.$TMP_LABEL
	return
    fi
    if [ "$ON_EXTERN" ] && [ ! "$ON_RAMDISK" ]; then
	_new_host=`hostname`
	if [ ! "`echo $_new_host|fgrep ${RAMDISK_GB}GB`" ]; then
	    _new_host=${RAMDISK_GB}GB-$_new_host
	fi
	if [ "$2" = "+" ]; then
	    _plus=+
	fi
    elif [ "$2" = "." ]; then
	_new_host=`hostname`
    else
	_fs $LINENO mount $mnt_strage
	if [ "$ON_NONE" ]; then
	    _new_host=${RAMDISK_GB}GB-rescue
	elif [ "$3" = "." ]; then
	    _new_host=${RAMDISK_GB}GB-$2
	else
	    _host_list=`ls -r $mnt_strage/opt | grep img$ 2>/dev/null | grep -e ^${RAMDISK_GB}GB | awk -F\. '{print $1}'` # ファイル名｢?GB-DESKTOP.img｣から｢.img｣撤去
	    _host_list=`echo $_host_list | sed -e "s/${RAMDISK_GB}GB-//g" -e 's/ /\//g'`
	    if [ "$2" ]; then
		if [ "`echo $_host_list | grep $2`" ]; then
		    echo "$1: ${RAMDISK_GB}GB-$2: already exist. To overwrite, type '. U $1 $2 .'[Enter][Enter] : $LINENO" >> /tmp/log.$TMP_LABEL
		    return
		fi
		if [ "`echo $2 | grep ^\${RAMDISK_GB}GB-`" ]; then
		    _new_host=$2
		else
		    _new_host=${RAMDISK_GB}GB-$2
		fi
	    fi
	fi
    fi
    if [ "$4" = "-" ] || [ "$ON_NONE" ] || ( [ "$ON_EXTERN" ] && [ ! "$ON_RAMDISK" ] ); then
	_work_area=opt
    fi
    echo "HOST:$_new_host"
    _fs $LINENO umount /usr/src
    if [ "`fgrep /usr/src /etc/mtab`" ]; then
	echo "$1: /usr/src: Busy. : $LINENO" :  >> /tmp/log.$TMP_LABEL
	return
    fi
    _fs $LINENO mount $mnt_strage
    cd $mnt_strage
    echo "apt-get clean"
    apt-get clean
    if [ "$ON_NONE" ] || ( [ "$ON_EXTERN" ] && [ ! "$ON_RAMDISK" ] ); then
	_ram_img_kb=$_RAMDISK_KB
    else
	_DEBIAN_FREE_KB=`df | fgrep $MNT_DEBIAN | awk '{print $4}'`
	_ram_img_kb=`df | grep \/$ | awk '{print $3}'`
	_ram_img_kb=$((`_tmpfs_size $_ram_img_kb | tail -n1`*11/10)) # ルートシステム長の1.1倍
    fi
    if [ "$_work_area" = "opt" ] || [ $_ram_img_kb -gt "`_tmpfs_size | tail -n1`" ]; then # _ram_img_kbが/dev/shmの空きよりも大きい
	echo "dd if=/dev/zero of=$mnt_strage/opt/IMG0.img bs=1KB count=$_ram_img_kb"
	dd if=/dev/zero of=$mnt_strage/opt/IMG0.img bs=1KB count=$_ram_img_kb
	echo "mkfs.ext4 -q $mnt_strage/opt/IMG0.img"
	mkfs.ext4 -q $mnt_strage/opt/IMG0.img
	echo "mount -o loop $mnt_strage/opt/IMG0.img /tmp/loop0.$TMP_LABEL"
	_fs $LINENO mount -o loop $mnt_strage/opt/IMG0.img /tmp/loop0.$TMP_LABEL
    else
	echo "umount /tmp/loop0.$TMP_LABEL"
	_fs $LINENO umount /tmp/loop0.$TMP_LABEL
	echo "mount -t tmpfs -o size=${_ram_img_kb}K /dev/shm /tmp/shm0.$TMP_LABEL"
	_fs $LINENO mount -t tmpfs -o size=${_ram_img_kb}K /dev/shm /tmp/shm0.$TMP_LABEL
	echo "dd if=/dev/zero of=/tmp/shm0.$TMP_LABEL/IMG0.img bs=1KB count=$_ram_img_kb"
	dd if=/dev/zero of=/tmp/shm0.$TMP_LABEL/IMG0.img bs=1KB count=$_ram_img_kb
	echo "mkfs.ext4 -q /tmp/shm0.$TMP_LABEL/IMG0.img"
	mkfs.ext4 -q /tmp/shm0.$TMP_LABEL/IMG0.img
	echo "mount -o loop /tmp/shm0.$TMP_LABEL/IMG0.img /tmp/loop0.$TMP_LABEL"
	_fs $LINENO mount -o loop /tmp/shm0.$TMP_LABEL/IMG0.img /tmp/loop0.$TMP_LABEL
    fi
    echo "mkdir -p /tmp/modules.$TMP_LABEL"
    mkdir -p /tmp/modules.$TMP_LABEL
    echo "cd /"
    cd /
    echo "mv usr/lib/modules* /tmp/modules.$TMP_LABEL"
    mv usr/lib/modules/* /tmp/modules.$TMP_LABEL
    if [ "$_plus" = "+" ]; then
	mv /tmp/modules.$TMP_LABEL/$UNAME_R usr/lib/modules
    fi
    echo "tar cf - [e-h]* root srv usr var | tar xfp - -C /tmp/loop0.$TMP_LABEL >/tmp/error.$TMP_LABEL"
    tar cf - [e-h]* root srv usr var | tar xfp - -C /tmp/loop0.$TMP_LABEL >/tmp/error.$TMP_LABEL
    echo "mv /tmp/modules.$TMP_LABEL/* usr/lib/modules"
    mv /tmp/modules.$TMP_LABEL/* usr/lib/modules
    _tmp_debian $_plus
    echo "cd /tmp/$DEBIAN.$TMP_LABEL"
    cd /tmp/$DEBIAN.$TMP_LABEL
    echo "tar cf - * | tar xfp - -C /tmp/loop0.$TMP_LABEL"
    tar cf - * | tar xfp - -C /tmp/loop0.$TMP_LABEL
    echo "umount /tmp/loop0.$TMP_LABEL"
    _fs $LINENO umount /tmp/loop0.$TMP_LABEL
    if [ -s /tmp/error.$TMP_LABEL ]; then
	echo "$1: `cat /tmp/error.$TMP_LABEL` : $LINENO" | tee -a /tmp/log.$TMP_LABEL
	return
    fi	    
    echo "df"
    df
    if [ -f $mnt_strage/opt/IMG0.img ]; then
	if [ "$ON_NONE" ] || ( [ "$ON_EXTERN" ] && [ ! "$ON_RAMDISK" ] ); then
	    _fs $LINENO mount -o loop $mnt_strage/opt/IMG0.img /tmp/loop0.$TMP_LABEL
	    _ram_img_kb=`df | grep /tmp/loop0.$TMP_LABEL$ | awk '{print $3}'`
	    _ram_img_kb=$((_ram_img_kb*_numerator/10))
	    echo "dd if=/dev/zero of=$mnt_strage/opt/IMG1.img bs=1KB count=$_ram_img_kb"
	    dd if=/dev/zero of=$mnt_strage/opt/IMG1.img bs=1KB count=$_ram_img_kb
	    mkfs.ext4 -q $mnt_strage/opt/IMG1.img
	    echo "mount -o loop $mnt_strage/opt/IMG1.img /tmp/loop1.$TMP_LABEL"
	    _fs $LINENO mount -o loop $mnt_strage/opt/IMG1.img /tmp/loop1.$TMP_LABEL
	    echo "cd /tmp/loop0.$TMP_LABEL"
	    cd /tmp/loop0.$TMP_LABEL
	    echo "tar cf - * | tar xfp - -C /tmp/loop1.$TMP_LABEL"
	    tar cf - * | tar xfp - -C /tmp/loop1.$TMP_LABEL
	    echo "cd /opt"
	    cd /opt
	    echo "umount /tmp/loop0.$TMP_LABEL /tmp/loop1.$TMP_LABEL"
	    _fs $LINENO umount /tmp/loop0.$TMP_LABEL /tmp/loop1.$TMP_LABEL
	    echo "mv $mnt_strage/opt/IMG1.img $mnt_strage/opt/$_new_host.img"
	    mv $mnt_strage/opt/IMG1.img $mnt_strage/opt/$_new_host.img
	    echo "rm $mnt_strage/opt/IMG0.img"
	    rm $mnt_strage/opt/IMG0.img
	else
	    echo "mv $mnt_strage/opt/IMG0.img $mnt_strage/opt/$_new_host.img"
	    mv $mnt_strage/opt/IMG0.img $mnt_strage/opt/$_new_host.img
	fi
    else
	echo "mv /tmp/shm0.$TMP_LABEL/IMG0.img $mnt_strage/opt/$_new_host.img"
	mv /tmp/shm0.$TMP_LABEL/IMG0.img $mnt_strage/opt/$_new_host.img #_44)　/tmp/shm0.$TMP_LABEL/IMG0.imgを/opt/4GB-rescue.imgへリネーム
	_fs $LINENO mount $MNT_DEBIAN
    fi
    if [ "$RAMDISK_GB" = "$RESCUE_GB" ] && [ ! -f "$MNT_DEBIAN/opt/${RESCUE_GB}GB-rescue.img" ] && [ "$mnt_strage" != "$MNT_DEBIAN" ]; then
	cp $mnt_strage/opt/$_new_host.img $MNT_DEBIAN/opt/${RESCUE_GB}GB-rescue.img
    fi
    echo "ls -l $mnt_strage/opt/${RAMDISK_GB}GB*.img"
    ls -l $mnt_strage/opt/${RAMDISK_GB}GB*.img
    cd /opt
}

save_host() { # -s)
    if [ ! "$ON_RAMDISK" ] && [ ! "$ON_EXTERN" ] ; then
	echo "$1: Not on a RAMDISK. : $LINENO" >> /tmp/log.$TMP_LABEL
	return
    fi
    _save_host $1 $2 $3 $4
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
	for _part in {a..r};do /sbin/hdparm -S 255 /dev/sd$_part ;done
    else
	echo "$1 $2: Invalid option. : $LINENO" >> /tmp/log.$TMP_LABEL
    fi
}

update_base() { # -u)
    local _new_gb _new_kernel _NEW_KERNEL_LIST
    local _entry _ENTRY_LIST _linux_image
    local _rescue_kernel _desktop_kernel
    local _GRUB_DEFAULT
    local _i _j _n

    if [ ! "$ON_NONE" ] && [ ! "$ON_EXTERN" ]; then
	echo "$1: Not on a NONE-KERNEL. : $LINENO" >> /tmp/log.$TMP_LABEL
	return
    fi
    cd /opt
    if [ ! -f /opt/BASE.u ]; then
	_output_u DEBIAN.u -p $LINENO
	apt install -y $BASE_LIST #_９)　必要不可欠なパッケージ追加（sddmでフリーズの場合は下記以外のグラボ用firmwareが必要）
	_output_u BASE.u $LINENO
	#_10)　BASE.u保存
	chmod 444 /opt/BASE.u
    fi
    apt autoremove -y
    _output_u current.u $LINENO
    echo "head -n5 /etc/grub.d/40_custom > /tmp/40_custom.$TMP_LABEL"
    head -n5 /etc/grub.d/40_custom > /tmp/40_custom.$TMP_LABEL
    if [ "$GRUB_CUSTOM" ]; then
	echo 'echo "$GRUB_CUSTOM" >> /tmp/40_custom.$TMP_LABEL'
	echo "$GRUB_CUSTOM" >> /tmp/40_custom.$TMP_LABEL
    fi
    echo "mv /tmp/40_custom.$TMP_LABEL /etc/grub.d/40_custom"
    cp /tmp/40_custom.$TMP_LABEL /etc/grub.d/40_custom
    echo "chmod 755 /etc/grub.d/40_custom"
    chmod 755 /etc/grub.d/40_custom
    cd /opt
    if [ ! "`fgrep Storage=volatile /etc/systemd/journald.conf`" ]; then
	sed -i /etc/systemd/journald.conf -e s/#Storage=auto/Storage=volatile/ #_11)　ログはメモリに保存（/etc/systemd/journal.conf）
    fi
    if [ ! -f /opt/grub.ORG ]; then
	echo "cp /etc/default/grub /opt/grub.ORG"
	cp /etc/default/grub /opt/grub.ORG
    fi
    _GRUB_DEFAULT=`fgrep GRUB_DEFAULT= /etc/default/grub`
    if [ "`echo $_GRUB_DEFAULT | fgrep =0`" ]; then
	_i=2
	_j=2
	_ENTRY_LIST=`grep menuentry /etc/grub.d/40_custom | sed -e 's/{//' | awk -Fmenuentry '{print $'$_j'}'`
	_entry=`echo $_ENTRY_LIST | awk -F\' '{print $'$_j'}' | sed -e "s/'//g"`
	while [ "$_entry" ]
	do
	    _i=$((_i+1))
	    _j=$((_j+2))
	    _entry=`echo $_ENTRY_LIST | awk -F\' '{print $'$_j'}'`
	done
	_n=$((_i+1))
	sed -i /etc/default/grub \
	    -e "s/$_GRUB_DEFAULT/GRUB_DEFAULT=$_n\\nGRUB_SAVEDEFAULT=true/" \
	    -e s/GRUB_DISABLE_OS_PROBER=false/GRUB_DISABLE_OS_PROBER=true/ \
	    -e s/#GRUB_DISABLE_OS_PROBER=/GRUB_DISABLE_OS_PROBER=/ \
	    -e s/#GRUB_TERMINAL=console/GRUB_TERMINAL=console/
	sed -i /etc/grub.d/30_uefi-firmware -e 's/OS_INDICATIONS=/#OS_INDICATIONS=/'
	update-grub
    else
	_n=""
    fi
    _fs $LINENO umount /usr/src
    _fs $LINENO mount $mnt_strage
    cd /
    if [ ! "`fgrep NONE-KERNEL /etc/grub.d/40_custom`" ]; then
	cat <<EOF >> /etc/grub.d/40_custom
menuentry 'NONE-KERNEL' {
      	insmod part_msdos
	insmod ext2
	set root='hd0,gpt3'
	search --no-floppy --fs-uuid --set=root $DEBIAN_ROOT_UUID
	echo	'Loading base:vmlinuz-$UNAME_R from $DEBIAN_ROOT_UUID ...'
	linux	/boot/vmlinuz-$UNAME_R root=UUID=$DEBIAN_ROOT_UUID
	echo	'Loading initial ramdisk ...'
	initrd 	/boot/initrd.img-$UNAME_R
	savedefault
}
EOF
    fi
    echo "umount /usr/src"
    _fs $LINENO umount /usr/src
    if [ -L /usr/src/build ]; then
	cd /usr/src
	rm -f build CONFIG_NONE
	rm -rf linux-source-$KERNEL_VERSION linux-config-$KERNEL_VERSION
	cd -
    fi
    cd /opt
    if [ ! -f /opt/$TEMPLATE_BASE.gz ]; then
	_output_u BASE.u -p $LINENO
	# TEMPLATE作成時のみ極小BASEシステムが必要（'. U -e'[Enter] -> 'sudo apt install ｢〜｣'[Enter] -> '. U -u'[Enter]の際に｢〜｣を削除等）
	if [ "$ON_EXTERN" ]; then
	    _is_inst dkms +
	fi
	cd /opt
	echo "free"
	free
	echo "umount /tmp/shm0.$TMP_LABEL"
	_fs $LINENO umount /tmp/shm0.$TMP_LABEL
	echo "mount -t tmpfs -o size=${TEMPLATE_KB}K /dev/shm /tmp/shm0.$TMP_LABEL"
	_fs $LINENO mount -t tmpfs -o size=${TEMPLATE_KB}K /dev/shm /tmp/shm0.$TMP_LABEL
	echo "dd if=/dev/zero of=/tmp/shm0.$TMP_LABEL/IMG1.img bs=1KB count=$TEMPLATE_KB"
	dd if=/dev/zero of=/tmp/shm0.$TMP_LABEL/IMG1.img bs=1KB count=$TEMPLATE_KB
	echo "mkfs.ext4 -q /tmp/shm0.$TMP_LABEL/IMG1.img"
	mkfs.ext4 -q /tmp/shm0.$TMP_LABEL/IMG1.img
	echo "umount /tmp/loop1.$TMP_LABEL"
	_fs $LINENO umount /tmp/loop1.$TMP_LABEL
	echo "mount -o loop /tmp/shm0.$TMP_LABEL/IMG1.img /tmp/loop1.$TMP_LABEL"
	_fs $LINENO mount -o loop /tmp/shm0.$TMP_LABEL/IMG1.img /tmp/loop1.$TMP_LABEL
	echo "apt-get clean"
	apt-get clean
	echo "mkdir -p /tmp/modules.$TMP_LABEL/modules /tmp/modules.$TMP_LABEL/headers"
	mkdir -p /tmp/modules.$TMP_LABEL/modules /tmp/modules.$TMP_LABEL/headers
	echo "cd /"
	cd /
	echo "mv /usr/lib/modules/* /tmp/modules.$TMP_LABEL/modules"
	mv /usr/lib/modules/* /tmp/modules.$TMP_LABEL/modules
	echo "mv /usr/src/linux-headers* /tmp/modules.$TMP_LABEL/headers"
	mv /usr/src/linux-headers* /tmp/modules.$TMP_LABEL/headers
	echo "tar cf - [e-h]* root srv usr var | tar xfp - -C /tmp/loop1.$TMP_LABEL >/tmp/error.$TMP_LABEL"
	tar cf - [e-h]* root srv usr var | tar xfp - -C /tmp/loop1.$TMP_LABEL >/tmp/error.$TMP_LABEL
	echo "mv /tmp/modules.$TMP_LABEL/modules/* /usr/lib/modules"
	mv /tmp/modules.$TMP_LABEL/modules/* /usr/lib/modules
	echo "mv /tmp/modules.$TMP_LABEL/headers/* /usr/src 2>/dev/null"
	mv /tmp/modules.$TMP_LABEL/headers/* /usr/src 2> /dev/null
	echo "rm -rf /tmp/loop1.$TMP_LABEL/var/log/journal >/tmp/error.$TMP_LABEL"
	rm -rf /tmp/loop1.$TMP_LABEL/var/log/journal >/tmp/error.$TMP_LABEL
	_tmp_debian
	echo "cd /tmp/$DEBIAN.$TMP_LABEL"
	cd /tmp/$DEBIAN.$TMP_LABEL
	echo "tar cf - * | tar xfp - -C /tmp/loop1.$TMP_LABEL"
	tar cf - * | tar xfp - -C /tmp/loop1.$TMP_LABEL
	echo "cd /opt"
	cd /opt
	echo "cat <<EOF > /tmp/loop1.$TMP_LABEL/etc/rc.local" #_34)　rc.local（RAMDISK稼働用）をあらかじめ作成
	cat <<EOF > /tmp/loop1.$TMP_LABEL/etc/rc.local #_34)　rc.local（RAMDISK稼働用）をあらかじめ作成
#!/bin/sh
for _i in \`seq 1 10\`
do
    if [ "\`fgrep $MNT_DEBIAN /etc/mtab\`" ]; then
	break
    fi
    mount $MNT_DEBIAN 2>/dev/null
    sleep 1
done
cp $MNT_DEBIAN/opt/U.sh $MNT_DEBIAN/opt/U.opt /opt
for _i in \`seq 1 10\`
do
    if [ ! "\`fgrep $MNT_DEBIAN /etc/mtab\`" ]; then
	break
    fi
    umount $MNT_DEBIAN 2>/dev/null
    sleep 1
done
\`grep ^SH= /opt/U.opt | awk -FSH= '{print \$2}'\` /opt/U.sh -r
EOF
	echo "#!/bin/sh"
	echo "for _i in \`seq 1 10\`"
	echo "do"
	echo 'if [ "\`fgrep $MNT_DEBIAN /etc/mtab\`" ]; then'
	echo     "break"
	echo "fi"
	echo "mount $MNT_DEBIAN 2>/dev/null"
	echo "sleep 1"
	echo 'echo "done"'
	echo "cp $MNT_DEBIAN/opt/U.sh $MNT_DEBIAN/opt/U.opt /opt"
	echo "for _i in \`seq 1 10\`"
	echo "do"
	echo 'if [ ! "\`fgrep $MNT_DEBIAN /etc/mtab\`" ]; then'
	echo     "break"
	echo "fi"
	echo "umount $MNT_DEBIAN 2>/dev/null"
	echo "sleep 1"
	echo "done"
	echo "\`grep ^SH= /opt/U.opt | awk -FSH= '{print \$2}'\` /opt/U.sh -r"
	echo "EOF"
	echo
	echo "chmod 755 /tmp/loop1.$TMP_LABEL/etc/rc.local"
	chmod 755 /tmp/loop1.$TMP_LABEL/etc/rc.local
	echo "umount /tmp/loop1.$TMP_LABEL"
	_fs $LINENO umount /tmp/loop1.$TMP_LABEL
	echo "gzip -c /tmp/shm0.$TMP_LABEL/IMG1.img > /opt/$TEMPLATE_BASE.gz"
	gzip -c /tmp/shm0.$TMP_LABEL/IMG1.img > /opt/$TEMPLATE_BASE.gz
	echo "umount /tmp/shm0.$TMP_LABEL"
	_fs $LINENO umount /tmp/shm0.$TMP_LABEL
	echo "df"
	df
    fi
    if [ ! "$ON_EXTERN" ] && [ ! "`_is_inst apt-listchanges`" ]; then
	echo "apt-get update -y"
	apt-get update -y
	echo "apt-get upgrade -y"
	apt-get upgrade -y
	_install_standard #_36)　標準システムユーティリティをインストール
	echo "apt install -y $RESCUE_LIST"
	apt install -y $RESCUE_LIST
	DOT_FILES
	_output_u ${RESCUE_GB}GB-rescue.u $LINENO
	echo "apt-get clean"
	apt-get clean
    fi
    if [ ! "$ON_EXTERN" ] && [ ! -f /opt/${RESCUE_GB}GB-rescue.img ]; then
	if [ "$mnt_strage" != "$MNT_DEBIAN" ] && [ -f $mnt_strage/opt/${RESCUE_GB}GB-rescue.img ] && [ ! -f /opt/${RESCUE_GB}GB-rescue.img ]; then
	    echo "cp $mnt_strage/opt/${RESCUE_GB}GB-rescue.img /opt"
	    cp $mnt_strage/opt/${RESCUE_GB}GB-rescue.img /opt
	elif [ -f /opt/${RESCUE_GB}GB-rescue.u ]; then # コンパイル環境は4GB-rescue版には収容不能なのでカーネルリメーク前に保存
	    _output_u ${RESCUE_GB}GB-rescue.u -i $LINENO
	    _output_u ${RESCUE_GB}GB-rescue.u -p $LINENO
	fi
	_save_host ${RAMDISK_GB}GB-rescue
    fi
    _rescue_kernel=`ls -t /opt | grep linux-image | grep +${RESCUE_GB}gb | head -n1`
    _desktop_kernel=`ls -t /opt | grep linux-image | grep +${DESKTOP_GB}gb | head -n1`
    if [ ! "$_rescue_kernel" ] || [ ! "$_desktop_kernel" ]; then
	echo "umount /tmp/bak.$TMP_LABEL"
	_fs $LINENO umount /tmp/bak.$TMP_LABEL
	echo "mount -t tmpfs -o size=${TMPFS_KB}K /dev/shm /tmp/bak.$TMP_LABEL"
	_fs $LINENO mount -t tmpfs -o size=${TMPFS_KB}K /dev/shm /tmp/bak.$TMP_LABEL
	if [ ! "$ON_EXTERN" ] && [ "$mnt_strage" = "$MNT_DEBIAN" ]; then
	    _img_backup ${RESCUE_GB}GB-rescue /opt
	fi
	if [ ! "$_rescue_kernel" ]; then #_17)　/optにRESCUE_GBカーネルパッケージが存在しない
	    _compile $1 $RESCUE_GB /
	    rm -f /boot/initrd*+${RESCUE_GB}gb*
	    _rescue_kernel=`ls -t /opt | grep linux-image | grep +${RESCUE_GB}gb | head -n1`
	fi
	if [ ! "$_desktop_kernel" ]; then #_17)　/optにDESKTOP_GBカーネルパッケージが存在しない
	    _compile $1 $DESKTOP_GB /
	    rm -f /boot/initrd*+${DESKTOP_GB}gb*
	    _desktop_kernel=`ls -t /opt | grep linux-image | grep +${DESKTOP_GB}gb | head -n1`
	fi
    fi
    if [ ! "`_is_inst linux-image | grep +${RESCUE_GB}gb | grep ^ii`" ]; then #_26)　作成した4GBカーネルがシステムに組み込まれていない（_25 から再帰呼び出し）
	dpkg -i /opt/$_rescue_kernel > /dev/null
    fi
    _update_base $RESCUE_GB
    _NEW_KERNEL_LIST=`ls | grep linux-image | grep -ve ${RESCUE_GB}gb | grep -ve -amd64`
    if [ "$ON_EXTERN" ] || [ "$mnt_strage" != "$MNT_DEBIAN" ]; then
	for _new_kernel in $_NEW_KERNEL_LIST
	do
	    _new_gb=`echo $_new_kernel | awk -F+ '{print $2}' | awk -F_ '{print $1}' | sed -e 's/gb//'`
	    if [ ! "`_is_inst linux-image | grep +${_new_gb}gb | grep ^ii`" ]; then
		dpkg -i /opt/$_new_kernel > /dev/null
	    fi
	    _update_base $_new_gb
	done
    elif [ "$_NEW_KERNEL_LIST" ]; then
	echo "$1: To update 'RAM-`echo $_NEW_KERNEL_LIST | awk -Flinux-image- '{print $2}' | awk -F_ '{print $1}'`-base.gz', please setup strage-area." >> /tmp/log.$TMP_LABEL
    fi
    if [ -f "/opt/${RESCUE_GB}GB-rescue.img" ]; then
	gzip -c /opt/${RESCUE_GB}GB-rescue.img > /opt/${RESCUE_GB}GB-rescue.gz
	rm /opt/${RESCUE_GB}GB-rescue.img
    elif [ "`fgrep /tmp/bak.$TMP_LABEL /etc/mtab`" ] && [ -f /tmp/bak.$TMP_LABEL/${RESCUE_GB}GB-rescue.gz ]; then
	echo "zcat /tmp/bak.$TMP_LABEL/${RESCUE_GB}GB-rescue.gz > /opt/${RESCUE_GB}GB-rescue.img"
	zcat /tmp/bak.$TMP_LABEL/${RESCUE_GB}GB-rescue.gz > /opt/${RESCUE_GB}GB-rescue.img
        echo "umount /tmp/bak.$TMP_LABEL"
	_fs $LINENO umount /tmp/bak.$TMP_LABEL
    fi
    HOMEDIR=$(getent passwd "$USR" | cut -d: -f6)
    ls -l /opt
    apt-get update && apt-get install -y git
    su "$USR" -c 'git clone https://github.com/haa15030/usilic.git ~/git/usilic'
    sh "$HOMEDIR/git/usilic/tools/setup-debian.sh" "$USR"
    echo "update-grub"
    sleep 6
    update-grub
    if [ ! "$ON_EXTERN" ] && [ "$_n" ]; then
	grep -v ^NEXT${RESCUE_GB}= /opt/U.opt > /tmp/opt.$TMP_LABEL
	echo "NEXT${RESCUE_GB}=${RESCUE_GB}GB-rescue" >> /tmp/opt.$TMP_LABEL
	mv /tmp/opt.$TMP_LABEL /opt/U.opt
	chmod 644 /opt/U.opt
        reboot
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
	echo "STRAGE($mnt_strage/opt):" >> /tmp/mess.$TMP_LABEL
	ls -l $mnt_strage/opt/ >> /tmp/mess.$TMP_LABEL
	cd /opt
    fi
    echo >> /tmp/mess.$TMP_LABEL
    df >> /tmp/mess.$TMP_LABEL
    $PAGER /tmp/mess.$TMP_LABEL
}

UUID=""
temp_name=""
mnt_strage=""
main() {
    local STRAGE _PART _error
    local _eth_device _ETH_DEVICE_LIST
    local _option _arg _ARG_LIST _i _n=n
    local _ARG_1 _ARG_2 _ARG_3 _ARG_4 _ARG_5
    local _numerator=$NUMERATOR
    local _NEW_KERNEL_LIST=`ls | grep linux-image | grep -ve +${RESCUE_GB}gb`

    if [ "$TERM" = "linux" ]; then
	LANG=C # 文字化け回避
    fi
    if [ ! -f /usr/local/bin/U ]; then
	cat <<EOF > /usr/local/bin/U
ARG="\$1 \$2 \$3 \$4 \$5 \$6 \$7 \$8 \$9"
OPTION=\`echo \$ARG | awk '{print \$NF}'\`
if [ "\`id -u\`" -ne 0 ]; then
    SUDO=sudo
else
    SUDO=""
fi
if [ "\`echo \$OPTION | grep -i -e \-d -e \-b\`" ]; then
    if [ "\$1" != "\$OPTION" ]; then
        SH=\`echo \$OPTION | awk -F\- '{print \$2}'\`ash
        sudo sed -i -e s/\`grep ^SH= /opt/U.opt\`/SH=\`echo \$OPTION | awk -F\- '{print \$2}'\`ash/ /opt/U.opt
	ARG=\`echo \$ARG | sed -e s/\\\$OPTION//\`
    fi   
else
    SH=\`grep ^SH= /opt/U.opt | awk -FSH= '{print \$2}'\`
fi
\$SUDO \$SH /opt/U.sh \$ARG
EOF
    fi
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
    if [ -d /tmp/modules.$TMP_LABEL ]; then
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
    if [ -f /tmp/bak.$TMP_LABEL/${RESCUE_GB}GB-rescue.gz ]; then
	echo "zcat /tmp/bak.$TMP_LABEL/${RESCUE_GB}GB-rescue.gz > /opt/${RESCUE_GB}GB-rescue.img"
	zcat /tmp/bak.$TMP_LABEL/${RESCUE_GB}GB-rescue.gz > /opt/${RESCUE_GB}GB-rescue.img
    fi
    if [ -d /tmp/$DEBIAN.$TMP_LABEL ]; then
	rm -rf /tmp/$DEBIAN.$TMP_LABEL
    fi
    _fs $LINENO umount /tmp/shm0.$TMP_LABEL
    if [ ! -f "/etc/systemd/timesyncd.conf" ] && ( [ ! "$ON_EXTERN" ] || [ ! "`_is_inst chrony`" ] ); then #_５)　時刻設定をUTCへ
	apt update -y
	apt upgrade -y
	apt install -y systemd-timesyncd
	sed -i /etc/systemd/timesyncd.conf -e s/#NTP=/NTP=ntp\.nict\.jp/
	systemctl restart systemd-timesyncd
	timedatectl set-local-rtc 0
    fi
    if [ "$ON_EXTERN" ]; then
	_apt_sources_extern
    else
	if [ ! "`grep non-free-firmware /etc/apt/sources.list`" ]; then
	    echo "sed -i /etc/apt/sources.list -e 's/ main/ main non-free-firmware/'"
	    sed -i /etc/apt/sources.list -e 's/ main/ main non-free-firmware/'
	    echo "apt update -y"
	    apt update -y
	fi
	if [ ! "`grep deb-src /etc/apt/sources.list`" ]; then
	    sed /etc/apt/sources.list -e 's/deb /deb-src /' >> /etc/apt/sources.list
	fi
    fi
    cat /dev/null  > /tmp/log.$TMP_LABEL
    ln -sf /tmp/log.$TMP_LABEL /opt/U.log
    cd /opt
    cd ..
    if [ "$ON_NONE" ]; then
	if [ "`dpkg -l | grep wget | grep ^ii`" ] && [ ! "`_is_inst apt-listchanges`" ]; then # wget標準システムユーティリティの有無をapt-listchanges
	    dpkg --purge wget # U.sh をゲットしたあとは不要
	fi
	if [ ! -f /opt/DEBIAN.u ]; then
	    _output_u DEBIAN.u $LINENO
	fi
    fi
    if [ ! "`ip a | grep \<BROADCAST`" ]; then # ネットに繋がってないかまたは有効なethデバイス設定が無い
	echo "Network unreachabl.: $LINENO" >> /tmp/log.$TMP_LABEL
    fi
    if [ ! "$ON_LINE" ] && [ "$ON_EXTERN" ]; then # Ubuntu は netplan で再設定
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
    if [ "$ON_EXTERN" ] && [ "$ON_EXTERN" != "$UNAME_R" ]; then
	mnt_strage=$MNT_DEBIAN
    elif [ "$SMBFS" ]; then
	grep -ve /mnt/cifs /etc/fstab > /tmp/fstab.$TMP_LABEL
	_is_inst cifs-utils +
	_fs $LINENO mount -t cifs -o `echo $SMBFS | awk '{print $4}'` `echo $SMBFS | awk '{print $1}'` `echo $SMBFS | awk '{print $2}'` > /tmp/error.$TMP_LABEL
	if [ -s /tmp/error.$TMP_LABEL ]; then
	    cat /tmp/error.$TMP_LABEL >> /tmp/log.$TMP_LABEL
	    mnt_strage=""
	else
	    mnt_strage=/mnt/cifs
	    echo $SMBFS >> /tmp/fstab.$TMP_LABEL
	    _fs $LINENO umount /mnt/cifs
	fi
    fi
    if [ "$mnt_strage" != "$MNT_DEBIAN" ] && [ "$mnt_strage" != "/mnt/cifs" ]; then
	mnt_strage=$STRAGE
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
	fi
    fi
    mv /tmp/fstab.$TMP_LABEL /etc/fstab
    chmod 644 /etc/fstab
    systemctl daemon-reload
    if [ ! "$mnt_strage" ]; then
	mnt_strage=$MNT_DEBIAN
    fi
    _NEW_KERNEL_LIST=`ls /opt | grep linux-image | grep + | grep -ve +${RESCUE_GB}gb | grep -ve +${DESKTOP_GB}gb`
    if [ ! "$ON_EXTERN" ] && [ "$_NEW_KERNEL_LIST" ] && [ "$mnt_strage" = "$MNT_DEBIAN" ]; then
	echo "To update 'RAM-`echo $_NEW_KERNEL_LIST | awk -Flinux-image- '{print $2}' | awk -F_ '{print $1}'`-base.gz', please setup strage-area."
    fi
    if [ "$DIFF_SH" ] && [ -f "/opt/U.diff" ]; then
	_fs $LINENO mount $MNT_DEBIAN
	if [ "$PAGER" = "less" ]; then
	    LANG=ja_JP.UTF-8 # コメント欄にある日本語コードのバイナリ解釈を回避
	fi
	$PAGER $MNT_DEBIAN/opt/U.diff
	read -p "Patch now? (Y/n): " _yn
	if [ ! "$_yn" ] || [ "$_yn" = "Y" ] || [ "$_yn" = "y" ]; then
	    _is_inst patch +
	    patch -d $MNT_DEBIAN/opt < $MNT_DEBIAN/opt/U.diff
	    rm $MNT_DEBIAN/opt/U.diff
	    if [ -L $MNT_DEBIAN ]; then
		echo "-> /opt/U.sh"
	    else
		echo "-> $MNT_DEBIAN/opt/U.sh"
		rm /opt/U.diff
	    fi
	fi
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
	    echo "  -k(ernel_remake) - or $RESCUE_GB or $DESKTOP_GB or ??" >> /tmp/mess.$TMP_LABEL
 	    echo "  -o(utput_u) . or filename [-i(nstall) or -p(urge)]" >> /tmp/mess.$TMP_LABEL
	else
	    if [ ! "$ON_NONE" ]; then
		echo "/dev/shm : `_tmpfs_size | tail -n1`"
		echo "img-size : $((`df | grep \/$ | awk '{print $3}'`*$_numerator/10))"
	    fi
	    if [ "$ON_RAMDISK" ]; then
		echo ". U [-acdknosv]"
	    elif [ "$ON_NONE" ]; then
		if [ -f /opt/BASE.u ]; then
		    echo ". U [-acenouv] "
		else
		    echo ". U [-auv] "
		fi
	    else
		echo ". U [-acnov] "
	    fi
	    if [ -f /opt/BASE.u ]; then
		echo "  -c(hange_root) [/mnt/usb]" >> /tmp/mess.$TMP_LABEL
		echo "  -k(ernel_remake) . or - or / or $RESCUE_GB or $DESKTOP_GB or ??" >> /tmp/mess.$TMP_LABEL
		echo "  -n(ext_host)" >> /tmp/mess.$TMP_LABEL
 		echo "  -o(utput_u) . or filename [-i(nstall) or -p(urge)]" >> /tmp/mess.$TMP_LABEL
		if [ "$ON_NONE" ]; then
		    echo "  -e(rase_base)" >> /tmp/mess.$TMP_LABEL
		    echo "  -u(pdate_base)" >> /tmp/mess.$TMP_LABEL
		elif [ "$ON_RAMDISK" ]; then
		    echo "  -m(ake_usb) device" >> /tmp/mess.$TMP_LABEL
 		    echo "  -s(ave_host) [.] " >> /tmp/mess.$TMP_LABEL
		fi
	    elif [ "$ON_NONE" ]; then
		echo "  -u(pdate_base)" >> /tmp/mess.$TMP_LABEL
	    fi
	    echo "  -a(dd_strage)" >> /tmp/mess.$TMP_LABEL
	fi
	echo "  -v(ervose)" >> /tmp/mess.$TMP_LABEL
	sort < /tmp/mess.$TMP_LABEL
	cat /dev/null > /tmp/mess.$TMP_LABEL
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
    if ( [ "$ON_WSL" ] || [ "$ON_NONE" ] || ( [ "$ON_EXTERN" ] && [ ! "$ON_RAMDISK" ] ) ) && [ ! -L $MNT_DEBIAN ] && [ ! "$KEEP_MNT_DEBIAN" ]; then
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
    if [ "$_c" ] && [ "$_c" != "-" ] && [ "`grep -e '-.)' /opt/U.sh | grep -ve '# -.)' -ve '-.).' | grep $_c`" ]; then    
	case "$_option" in
	    -a)
		add_strage $_option
		;;
	    -c)
		change_root $_option $_ARG_1
		;;
	    -e)
		erase_base $_option
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
    if [ "$_option" != "-v" ] && [ "$_option" != "-a" ]; then
	if [ "$ON_RAMDISK" ]; then
	    _fs $LINENO umount $MNT_DEBIAN
	    _fs $LINENO umount /boot/efi
	fi
	_fs $LINENO umount $OPT_BUILD
	_fs $LINENO umount $mnt_strage
    fi
    grep -v '^ : ' /tmp/log.$TMP_LABEL > /tmp/tmp.$TMP_LABEL
    sed '/^$/d' < /tmp/tmp.$TMP_LABEL > /tmp/log.$TMP_LABEL
    if [ -s /tmp/log.$TMP_LABEL ]; then 
	cat /tmp/log.$TMP_LABEL | tee -a /opt/U.log_`date "+%F"`
    fi
}

main $1 $2 $3 $4 $5
