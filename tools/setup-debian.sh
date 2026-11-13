#!/bin/sh
# setup-debian.sh ユーザー名 [--gpu=nvidia|amd|intel|none] [--noswapcaps]
#
# Debian 13・Ubuntu(26.10等)にusilicを入れる手順(docs/BUILD.mdのLinuxの節の2.〜6.)を、rootで一度に実行する
# (2026-11-13、直接のユーザー依頼"Debian13はインストール時のTaskselでSSHしか選ばない環境へログイン後、スクリプトで
# 完了まで"。tools/setup-freebsd.shと同じ作り)。/etc/os-releaseでDebianかUbuntuかを見分ける。
#
# 使い方(インストーラが終わって再起動し、sshでつないだ所から):
#   Debian: su -                        Ubuntu: sudo -i
#   apt-get update && apt-get install -y git
#   su ユーザー名 -c 'git clone https://github.com/haa15030/usilic.git ~/git/usilic'
#   sh ~ユーザー名/git/usilic/tools/setup-debian.sh ユーザー名
# 終わったら reboot し、Xfceにログインして、Fcitx5の設定でMozcを確かめ、usilicを起動する。
#
# することと順番:
#   1. パッケージ(X11・Xfce・lightdm・firefox・日本語フォント・fcitx5・usilicのビルドと最初の起動に要るもの、
#      mpvと音声(PipeWire・パネルの音量のプラグイン・pavucontrol))。Ubuntu Server (minimized)なら先にunminimize
#   2. GPUのドライバ(lspciで見分ける。--gpuで指定もできる)。DebianのNVIDIAはnon-freeを足してnvidia-driver、
#      UbuntuのNVIDIAはubuntu-drivers。AMD・IntelはカーネルのドライバでよいのでDebianはファームウェアだけ
#   3. 日本語のロケール、lightdmで起動、sudo、キーボード(/etc/default/keyboardがjpならjp106)
#   4. ユーザーの設定(~/.xprofile、~/.profile・~/.bashrcのPATH)と、usilicのビルド・インストール
# SDL2は、パッケージのもので日本語入力(fcitx5、D-Bus経由)が使えるので、FreeBSDのようにビルドし直さない。
# どの段も、済んでいるものは飛ばすので、途中で止まったらもう一度実行すれば続きから進む。
# 出力は画面と /var/log/usilic-setup.log の両方に残す。

set -u

LOG=/var/log/usilic-setup.log
if [ -z "${USILIC_SETUP_LOGGED:-}" ]; then
  status_file=$(mktemp -t usilic-setup.XXXXXX)
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

[ "$(uname -s)" = Linux ] || die "Linux(Debian・Ubuntu)専用です"
[ "$(id -u)" -eq 0 ] || die "rootで実行してください(Debianは su -、Ubuntuは sudo -i)"
[ -r /etc/os-release ] || die "/etc/os-release がありません"
. /etc/os-release
case "${ID:-}" in
  debian) distro=debian ;;
  ubuntu) distro=ubuntu ;;
  *)
    case " ${ID_LIKE:-} " in
      *" ubuntu "*) distro=ubuntu ;;
      *" debian "*) distro=debian ;;
      *) die "Debian・Ubuntu専用です(ID=${ID:-?})" ;;
    esac
    ;;
esac
echo "見つけたOS: ${PRETTY_NAME:-$ID}($distro として進めます)"

user=""
gpu=""
swapcaps=yes
for arg in "$@"; do
  case "$arg" in
    --gpu=*) gpu=${arg#--gpu=} ;;
    --noswapcaps) swapcaps=no ;;
    -*) die "知らないオプション: $arg" ;;
    *) user=$arg ;;
  esac
done
[ -n "$user" ] || die "使い方: sh setup-debian.sh ユーザー名 [--gpu=nvidia|amd|intel|none] [--noswapcaps]"
id "$user" >/dev/null 2>&1 || die "ユーザーがありません: $user"
[ "$user" != root ] || die "rootではなく、インストーラで作ったユーザー名を指定してください"
home=$(getent passwd "$user" | cut -d: -f6)
repo="$home/git/usilic"
[ -d "$repo/src/cpp" ] || die "リポジトリがありません: $repo(先に git clone してください)"

export DEBIAN_FRONTEND=noninteractive  # aptの途中の質問(キーボード・サービスの再起動等)を既定の答えで進める

# 入っていないものだけを入れる。
apt_install() {
  missing=""
  for p in "$@"; do
    dpkg-query -W -f='${Status}' "$p" 2>/dev/null | grep -q '^install ok installed$' || missing="$missing $p"
  done
  [ -z "$missing" ] || apt-get install -y $missing
}

# ---------------------------------------------------------------- 1. パッケージ
say "1. パッケージ"
# Ubuntu Server (minimized)は、manのページ等を削ってある(usilicのMANやHELPのman表示で困る)。unminimizeで戻す。
if [ "$distro" = ubuntu ] && [ -f /etc/dpkg/dpkg.cfg.d/excludes ]; then
  if command -v unminimize >/dev/null 2>&1; then
    echo "minimizedのUbuntuなので、unminimizeで削られたものを戻します(数分)"
    yes | unminimize || die "unminimize に失敗しました"
  else
    apt-get update && apt-get install -y unminimize && yes | unminimize || die "unminimize に失敗しました"
  fi
fi
apt-get update || die "apt-get update に失敗しました"
# firefox: DebianはESR(firefox-esr)。Ubuntuのfirefoxはsnapを入れる中継ぎのパッケージ。
if [ "$distro" = debian ]; then firefox=firefox-esr; else firefox=firefox; fi
apt_install git build-essential pkg-config libncurses-dev libsdl2-dev libsdl2-ttf-dev sudo bash \
  xdg-utils python3 pciutils locales man-db \
  xorg xfce4 xfce4-terminal xfce4-screenshooter lightdm lightdm-gtk-greeter dbus-x11 dbus-user-session \
  fonts-ipafont fonts-noto-cjk fonts-dejavu \
  fcitx5 fcitx5-mozc fcitx5-config-qt fcitx5-frontend-gtk3 \
  groff mg info w3m unzip \
  mpv pipewire pipewire-pulse pipewire-alsa wireplumber pavucontrol xfce4-pulseaudio-plugin ||
  die "apt-get install に失敗しました"
# 版によって名前が変わったり無くなったりするものは、1つずつ入れ、無くても止めない(後で手で入れる)。
# 7z: 以前のp7zip-fullは、新しい版では7zipへの中継ぎ(無い版もある)。usilicは7zを使う。
for p in "$firefox" fcitx5-frontend-qt6 fcitx5-frontend-qt5; do
  apt_install "$p" || echo "!!! $p を入れられませんでした(続けます)"
done
command -v 7z >/dev/null 2>&1 || apt_install p7zip-full || apt_install 7zip || echo "!!! 7z(p7zip-full・7zip)を入れられませんでした(続けます)"

# ---------------------------------------------------------------- 2. GPU
say "2. GPUのドライバ"
if [ -z "$gpu" ]; then
  vendors=$(lspci -nn | awk '/(VGA|3D|Display)/ { if (match($0, /\[[0-9a-f]{4}:[0-9a-f]{4}\]/)) print substr($0, RSTART + 1, 4) }')
  case "$vendors" in
    *10de*) gpu=nvidia ;;
    *1002*) gpu=amd ;;
    *8086*) gpu=intel ;;
    *) gpu=none ;;
  esac
  echo "見つけたGPU: $(echo $vendors) → $gpu(違えば --gpu=… で指定し直す)"
fi
# Debianでnon-free(NVIDIAのドライバ)・non-free-firmware(AMD・Intelのファームウェア)を使えるようにする。
debian_components() {
  changed=no
  if [ -f /etc/apt/sources.list.d/debian.sources ]; then
    if grep -q '^Components:' /etc/apt/sources.list.d/debian.sources &&
       ! grep '^Components:' /etc/apt/sources.list.d/debian.sources | grep -q ' non-free\( \|$\)'; then
      sed -i 's/^Components:.*/Components: main contrib non-free non-free-firmware/' /etc/apt/sources.list.d/debian.sources
      changed=yes
    fi
  fi
  if [ -f /etc/apt/sources.list ] && grep -q '^deb .* main' /etc/apt/sources.list &&
     ! grep '^deb ' /etc/apt/sources.list | grep -q ' non-free\( \|$\)'; then
    sed -i '/^deb\(-src\)\? / s/ main.*$/ main contrib non-free non-free-firmware/' /etc/apt/sources.list
    changed=yes
  fi
  if [ "$changed" = yes ]; then
    echo "aptにcontrib・non-free・non-free-firmwareを足しました"
    apt-get update || die "apt-get update に失敗しました"
  fi
}
case "$gpu" in
  nvidia)
    if [ "$distro" = debian ]; then
      debian_components
      # nvidia-driverはカーネルのモジュールをDKMSでビルドするので、今のカーネルのヘッダが要る。
      apt_install "linux-headers-$(dpkg --print-architecture)" nvidia-driver firmware-misc-nonfree || die "nvidia-driver を入れられません"
    else
      apt_install ubuntu-drivers-common || die "ubuntu-drivers-common を入れられません"
      if ! dpkg -l 'nvidia-driver-*' 2>/dev/null | grep -q '^ii'; then
        ubuntu-drivers install || die "ubuntu-drivers install に失敗しました"
      fi
    fi
    reboot_needed=yes
    ;;
  amd)
    [ "$distro" = debian ] && { debian_components; apt_install firmware-amd-graphics || die "firmware-amd-graphics を入れられません"; }
    ;;
  intel)
    [ "$distro" = debian ] && { debian_components; apt_install firmware-misc-nonfree || die "firmware-misc-nonfree を入れられません"; }
    ;;
  none) ;;
  *) die "--gpu は nvidia・amd・intel・none のどれか" ;;
esac
[ "$distro" = ubuntu ] && [ "$gpu" != nvidia ] && echo "Ubuntuはファームウェア(linux-firmware)が入っているので、何もしません"

# ---------------------------------------------------------------- 3. ロケール・lightdm・sudo・キーボード
say "3. 日本語のロケール、lightdm、sudo、キーボード"
if ! locale -a 2>/dev/null | grep -qi '^ja_JP\.utf-\?8$'; then
  if [ -f /etc/locale.gen ]; then
    sed -i 's/^# *\(ja_JP\.UTF-8 UTF-8\)/\1/' /etc/locale.gen
    grep -q '^ja_JP\.UTF-8 UTF-8' /etc/locale.gen || echo 'ja_JP.UTF-8 UTF-8' >> /etc/locale.gen
    locale-gen || die "locale-gen に失敗しました"
  else
    locale-gen ja_JP.UTF-8 || die "locale-gen に失敗しました"
  fi
fi
# 起動したらlightdmのログイン画面にする(SSHだけのインストールは文字の画面(multi-user.target)が既定)。
systemctl enable lightdm >/dev/null 2>&1 || true
[ "$(systemctl get-default)" = graphical.target ] || systemctl set-default graphical.target
# sudo: Debianでrootのパスワードを設定すると、作ったユーザーはsudoグループに入っていない。videoはGPU、
# audioは無くても(systemd-logindが権限を渡すので)鳴るが、入れておいて害は無い。
for group in sudo video audio; do
  getent group "$group" >/dev/null || continue
  id -Gn "$user" | tr ' ' '\n' | grep -qx "$group" || usermod -aG "$group" "$user"
done
# キーボード: インストーラで日本語を選んでいれば/etc/default/keyboardのXKBLAYOUTがjp。X(lightdm・Xfce)と
# コンソールの両方がこれを読む。モデルをjp106にし、Caps LockとCtrlを入れ替える(--noswapcapsなら入れ替えない)。
keyboard=/etc/default/keyboard
if [ -f "$keyboard" ] && grep -q '^XKBLAYOUT="jp"' "$keyboard"; then
  sed -i 's/^XKBMODEL=.*/XKBMODEL="jp106"/' "$keyboard"
  grep -q '^XKBOPTIONS=' "$keyboard" || echo 'XKBOPTIONS=""' >> "$keyboard"
  if [ "$swapcaps" = yes ]; then
    grep -q 'ctrl:swapcaps' "$keyboard" || sed -i 's/^XKBOPTIONS="\(.*\)"/XKBOPTIONS="\1,ctrl:swapcaps"/; s/^XKBOPTIONS=",/XKBOPTIONS="/' "$keyboard"
  fi
  echo "キーボード: $(grep -E '^XKB(MODEL|LAYOUT|OPTIONS)=' "$keyboard" | tr '\n' ' ')($keyboard)"
else
  echo "/etc/default/keyboard のレイアウトがjpではないので、キーボードは設定しません"
fi

# ---------------------------------------------------------------- 4. ユーザーの設定とusilic
say "4. $user の設定とusilicのビルド"
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
make usilic
make install
EOF

# NVIDIAはウィンドウの合成表示を切っておく(FreeBSDで画面が固まったことがある、docs/BUILD.mdのFreeBSDの注意点)。
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
if [ "${reboot_needed:-no}" = yes ]; then
  echo "NVIDIAのドライバを入れたので、必ず reboot してください(nouveauから切り替わるのは再起動の時)。"
  echo "UEFIのセキュアブートが有効だと、ドライバの署名の登録(MOK)を再起動の時に聞かれます(docs/BUILD.mdの注意点)。"
fi
echo "reboot して、Xfceに $user でログインしてください。"
echo "その後: アプリケーション → 設定 → Fcitx 5 の設定 でMozcがあることを確かめ、"
echo "        アプリケーション → アクセサリー → usilic で起動します(docs/BUILD.mdの5.・6.)。"
echo "音声: パネルに音量のアイコンが無ければ、パネルを右クリック → パネル → 新しいアイテムの追加 → PulseAudio プラグイン。出力先(内蔵・HDMI)は"
echo "      そのプラグインかpavucontrolで切り換える。HDMIが出ない時は pavucontrol の「設定」でカードの構成を変える。"
echo "記録: $LOG"
