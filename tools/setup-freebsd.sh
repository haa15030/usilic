#!/bin/sh
# setup-freebsd.sh ユーザー名 [--gpu=nvidia|amd|radeon|intel|none] [--swapcaps]
#
# FreeBSDにusilicを入れる手順(docs/BUILD.mdのFreeBSDの節の2.〜8.)を、rootで一度に実行する(2026-11-07、
# 直接のユーザー提案"ユーザーとrootとで実行する手順をまとめたシェルスクリプトを作るのはどう？　一発実行で終わり")。
#
# 使い方(インストーラが終わって再起動し、rootでログインした所から。sshでつないでもよい):
#   pkg bootstrap -y && pkg install -y git
#   su ユーザー名 -c 'git clone https://github.com/haa15030/usilic.git ~/git/usilic'
#   sh ~ユーザー名/git/usilic/tools/setup-freebsd.sh ユーザー名
# 終わったら reboot し、Xfceにログインして、Fcitx5の設定でMozcを確かめ(docs/BUILD.mdの9.)、usilicを起動する。
#
# することと順番:
#   1. パッケージ(X11・Xfce・lightdm・firefox・日本語フォント・fcitx5・usilicのビルドと最初の起動に要るもの)
#   2. GPUのドライバ(pciconf -lvで見分ける。--gpuで指定もできる)。kldloadで読み込めるか試し、drm-kmodが
#      今のカーネルと版が合わなければ(「version mismatch」)、/usr/srcに合わせてportsからビルドしてpkg lockする
#   3. dbus・lightdmの起動、sudo(wheel)、ユーザーをwheel・videoへ、キーボード(コンソールがjpならjp106)
#   4. 日本語入力に対応したSDL2をportsからビルドしてpkg lockする(時間がかかる)
#   5. ユーザーの設定(~/.xprofile、~/.profile・~/.bashrcのPATH)と、usilicのビルド・インストール
# どの段も、済んでいるものは飛ばすので、途中で止まったらもう一度実行すれば続きから進む。
# 出力は画面と /var/log/usilic-setup.log の両方に残す。

set -u

LOG=/var/log/usilic-setup.log
if [ -z "${USILIC_SETUP_LOGGED:-}" ]; then
  status_file=$(mktemp -t usilic-setup)
  started=$(date +%s)
  echo "開始: $(date '+%Y-%m-%d %H:%M:%S')" | tee -a "$LOG"
  { USILIC_SETUP_LOGGED=1 sh "$0" "$@"; echo $? > "$status_file"; } 2>&1 | tee -a "$LOG"
  status=$(cat "$status_file")
  rm -f "$status_file"
  elapsed=$(( $(date +%s) - started ))
  printf '終了: %s(かかった時間 %d時間%02d分%02d秒)\n' "$(date '+%Y-%m-%d %H:%M:%S')" \
    $((elapsed / 3600)) $((elapsed % 3600 / 60)) $((elapsed % 60)) | tee -a "$LOG"
  exit "${status:-1}"
fi

say() { printf '\n=== %s\n' "$*"; }
die() { printf '\n*** %s\n' "$*"; exit 1; }

[ "$(uname -s)" = FreeBSD ] || die "FreeBSD専用です"
[ "$(id -u)" -eq 0 ] || die "rootで実行してください(su -)"

user=""
gpu=""
swapcaps=no
for arg in "$@"; do
  case "$arg" in
    --gpu=*) gpu=${arg#--gpu=} ;;
    --swapcaps) swapcaps=yes ;;
    -*) die "知らないオプション: $arg" ;;
    *) user=$arg ;;
  esac
done
[ -n "$user" ] || die "使い方: sh setup-freebsd.sh ユーザー名 [--gpu=nvidia|amd|radeon|intel|none] [--swapcaps]"
id "$user" >/dev/null 2>&1 || die "ユーザーがありません: $user"
home=$(pw usershow "$user" | cut -d: -f9)
repo="$home/git/usilic"
[ -d "$repo/src/cpp" ] || die "リポジトリがありません: $repo(先に git clone してください)"

export ASSUME_ALWAYS_YES=yes  # pkgの確認を省く(make install-missing-packagesの中のpkgも)

# ---------------------------------------------------------------- 1. パッケージ
say "1. パッケージ"
pkg update -q
pkg install -y git gmake pkgconf sudo bash sdl2 sdl2_ttf xfce4-screenshooter-plugin firefox xdg-utils python3 \
  xorg xfce lightdm lightdm-gtk-greeter ja-font-ipa dejavu noto-jp \
  fcitx5 fcitx5-configtool fcitx5-gtk3 fcitx5-qt6 ja-fcitx5-mozc3 \
  groff mg texinfo 7-zip w3m || die "pkg install に失敗しました"

# ---------------------------------------------------------------- 2. GPU
say "2. GPUのドライバ"
if [ -z "$gpu" ]; then
  vendors=$(pciconf -l | awk '/class=0x03/ { for (i = 1; i <= NF; i++) if ($i ~ /^vendor=/) print substr($i, 8) }')
  case "$vendors" in
    *0x10de*) gpu=nvidia ;;
    *0x1002*) gpu=amd ;;
    *0x8086*) gpu=intel ;;
    *) gpu=none ;;
  esac
  echo "見つけたGPU: ${vendors:-なし} → $gpu(違えば --gpu=… で指定し直す)"
fi
case "$gpu" in
  nvidia) module=nvidia-modeset ;;
  amd) module=amdgpu ;;
  radeon) module=radeonkms ;;  # AMDのHD 7000番台以前
  intel) module=i915kms ;;
  none) module="" ;;
  *) die "--gpu は nvidia・amd・radeon・intel・none のどれか" ;;
esac

# drm-kmodのポートを/usr/srcに合わせてビルドし直す(pkgのドライバが今のカーネルより新しい版向けの時)。
build_drm_from_ports() {
  drm=$(pkg info -d drm-kmod | awk '/drm-[0-9a-z]+-kmod-/ { sub(/^[ \t]+/, ""); sub(/-[0-9][^-]*$/, ""); print; exit }')
  [ -n "$drm" ] || die "drm-kmodの中身のポートが分かりません(pkg info -d drm-kmod)"
  [ -f /usr/src/sys/sys/param.h ] ||
    die "/usr/src がありません。インストーラのDistribution Selectでsrcを選んで入れ直すか、カーネルと同じ版のソースを置いてください"
  kernel=$(sysctl -n kern.osreldate)
  src=$(awk '/^#define __FreeBSD_version/ { print $3 }' /usr/src/sys/sys/param.h)
  [ "$kernel" = "$src" ] || die "/usr/src の版($src)がカーネル($kernel)と違います。カーネルと同じ版のソースが要ります"
  ensure_ports
  echo "$drm をportsからビルドします(カーネル $kernel 向け)"
  (cd "/usr/ports/graphics/$drm" && make BATCH=yes install-missing-packages && make BATCH=yes reinstall clean) ||
    die "$drm のビルドに失敗しました"
  pkg lock -y "$drm"
}

ensure_ports() {
  if [ ! -d /usr/ports/.git ]; then
    say "portsを取ってくる(数分)"
    git clone --depth 1 https://git.FreeBSD.org/ports.git /usr/ports || die "portsを取ってこられません"
  fi
}

if [ -n "$module" ]; then
  if [ "$gpu" = nvidia ]; then
    pkg install -y nvidia-driver nvidia-xconfig || die "nvidia-driver を入れられません"
  else
    pkg install -y drm-kmod || die "drm-kmod を入れられません"
  fi
  sysrc -n kld_list 2>/dev/null | tr ' ' '\n' | grep -qx "$module" || sysrc kld_list+="$module"
  if ! kldstat -n "$module" >/dev/null 2>&1; then
    if ! kldload "$module"; then
      if [ "$gpu" != nvidia ] && dmesg | tail -20 | grep -q "version mismatch"; then
        build_drm_from_ports
        kldload "$module" || die "ビルドし直しても $module を読み込めません(dmesg | tail を見てください)"
      else
        die "$module を読み込めません(dmesg | tail を見てください)"
      fi
    fi
  fi
  echo "$module を読み込みました"
  if [ "$gpu" = nvidia ] && [ ! -f /etc/X11/xorg.conf ]; then
    nvidia-xconfig || die "nvidia-xconfig に失敗しました"
  fi
  if [ "$gpu" != nvidia ] && [ ! -e /dev/dri/card0 ]; then
    die "/dev/dri/card0 がありません(ドライバが働いていません。dmesg | tail を見てください)"
  fi
fi

# ---------------------------------------------------------------- 3. 起動するもの・sudo・キーボード
say "3. dbus・lightdm、sudo、キーボード"
sysrc dbus_enable=YES lightdm_enable=YES
mkdir -p /usr/local/etc/sudoers.d
if [ ! -f /usr/local/etc/sudoers.d/wheel ]; then
  echo '%wheel ALL=(ALL:ALL) ALL' > /usr/local/etc/sudoers.d/wheel
  chmod 440 /usr/local/etc/sudoers.d/wheel
fi
for group in wheel video operator; do
  id -Gn "$user" | tr ' ' '\n' | grep -qx "$group" || pw groupmod "$group" -m "$user"
done
keyboard=/usr/local/etc/X11/xorg.conf.d/keyboard.conf
if [ ! -f "$keyboard" ]; then
  case "$(sysrc -n keymap 2>/dev/null)" in
    jp*)
      mkdir -p /usr/local/etc/X11/xorg.conf.d
      {
        echo 'Section "InputClass"'
        echo 'Identifier "keyboard"'
        echo 'MatchIsKeyboard "on"'
        echo 'Option "XkbModel" "jp106"'
        echo 'Option "XkbLayout" "jp"'
        [ "$swapcaps" = yes ] && echo 'Option "XkbOptions" "ctrl:swapcaps"'
        echo 'EndSection'
      } > "$keyboard"
      echo "Xのキーボードをjp106にしました($keyboard)"
      ;;
    *) echo "コンソールのキーマップがjpではないので、Xのキーボードは設定しません" ;;
  esac
fi

# ---------------------------------------------------------------- 4. SDL2(日本語入力)
say "4. 日本語入力に対応したSDL2"
sdl_ok() { pkg query '%Ok=%Ov' sdl2 | grep -q '^DBUS=on$' && pkg query '%Ok=%Ov' sdl2 | grep -q '^FCITX=on$'; }
if sdl_ok; then
  echo "済んでいます(DBUS=on、FCITX=on)"
else
  grep -q '^devel_sdl20_SET+=DBUS FCITX' /etc/make.conf 2>/dev/null || echo 'devel_sdl20_SET+=DBUS FCITX' >> /etc/make.conf
  ensure_ports
  pkg unlock -y sdl2 >/dev/null 2>&1
  (cd /usr/ports/devel/sdl20 && make BATCH=yes rmconfig && make BATCH=yes install-missing-packages &&
    make BATCH=yes reinstall clean) || die "SDL2のビルドに失敗しました"
  sdl_ok || die "SDL2をビルドしましたが、DBUS・FCITXがonになっていません(pkg query '%Ok=%Ov' sdl2)"
  pkg lock -y sdl2
fi

# ---------------------------------------------------------------- 5. ユーザーの設定とusilic
say "5. $user の設定とusilicのビルド"
su - "$user" -c 'sh -s' <<'EOF' || die "usilicのビルドに失敗しました"
set -e
line='PATH=$HOME/.local/bin:$PATH; export PATH'
for rc in .profile .bashrc; do
  grep -qxF "$line" "$HOME/$rc" 2>/dev/null || echo "$line" >> "$HOME/$rc"
done
if [ -f "$HOME/.bash_profile" ]; then grep -qxF "$line" "$HOME/.bash_profile" || echo "$line" >> "$HOME/.bash_profile"; fi
if [ ! -f "$HOME/.xprofile" ]; then
  cat > "$HOME/.xprofile" <<'XPROFILE'
export LANG=ja_JP.UTF-8
export GTK_IM_MODULE=fcitx
export QT_IM_MODULE=fcitx
export XMODIFIERS=@im=fcitx
export SDL_IM_MODULE=fcitx
XPROFILE
fi
cd "$HOME/git/usilic/src/cpp"
gmake usilic
gmake install
EOF

# NVIDIAはウィンドウの合成表示を切っておく(有効のままだと画面が固まることがある、docs/BUILD.mdの注意点)。
# Xfceがまだ一度も起動していないので、xfconf-queryの代わりに設定ファイルを置く(既にあれば触らない)。
if [ "$gpu" = nvidia ]; then
  xfwm="$home/.config/xfce4/xfconf/xfce-perchannel-xml/xfwm4.xml"
  if [ ! -f "$xfwm" ]; then
    su - "$user" -c "mkdir -p '$(dirname "$xfwm")' && cat > '$xfwm'" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<channel name="xfwm4" version="1.0">
  <property name="general" type="empty">
    <property name="use_compositing" type="bool" value="false"/>
  </property>
</channel>
EOF
  fi
fi

say "終わりました"
echo "reboot して、Xfceに $user でログインしてください。"
echo "その後: アプリケーション → 設定 → Fcitx5 設定 でMozcがあることを確かめ、"
echo "        アプリケーション → アクセサリー → usilic で起動します(docs/BUILD.mdの9.・10.)。"
echo "記録: $LOG"
