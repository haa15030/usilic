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
