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
