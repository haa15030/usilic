"""_zfs_ready・_zfs_pool・_zfs_import・_zfs_dataset・_strage_fstab(U.sh): ZFSのSTRAGE。

2026-11-28、直接のユーザー依頼(外付けSSDのSTORAGEをZFSにして、FreeBSDとLinuxで共有する): STRAGEにZFSのプールも
選べるように。区画はblkidのTYPE=zfs_member、UUIDはプールのguid(zpool importに渡せる)。UUID=では指せないので、
/etc/fstabにはデータセット(プール名/data、無ければプール名)を書く。データセットはmountpoint=legacyにしておく
(プールに書いたマウント先はどのOSでも同じになるので、OSごとにfstabで決める)。マウントの前にプールを読み込む
(-N: マウントしない、-f: FreeBSD等で使ったままexportしていなくても)。"""

STRAGE = '/mnt/zfs'


def ready(env, out=None):
    """_zfs_ready(): ZFSのモジュールとzpoolが使えるか(無ければzfsutils-linuxを入れてみる。モジュールはzfs-dkms等)。"""
    from .parts import install_if_missing
    install_if_missing(env, 'zfsutils-linux', out or (lambda line: None))
    system = env.system
    return system.run(['which', 'zpool'])[0] == 0 and system.run(['modprobe', 'zfs'])[0] == 0


def pool(env, guid):
    """_zfs_pool(): guidのプールが読み込まれていれば、その名前(無ければ'')。"""
    for line in env.system.output(['zpool', 'list', '-H', '-o', 'name,guid']).splitlines():
        f = line.split()
        if len(f) == 2 and f[1] == guid:
            return f[0]
    return ''


def import_pool(env, guid, out=None):
    """_zfs_import(): guidのプールを読み込み(済んでいればそのまま)、名前を返す。"""
    if not pool(env, guid) and ready(env, out):
        env.system.run(['zpool', 'import', '-N', '-f', guid])
    return pool(env, guid)


def dataset(env, guid, out=None):
    """_zfs_dataset(): guidのプールのSTRAGEにするデータセット(プール名/dataがあればそれ、無ければプール名。
    読み込めなければ'')。"""
    name = import_pool(env, guid, out)
    if not name:
        return ''
    if env.system.run(['zfs', 'list', '-H', '-o', 'name', name + '/data'])[0] == 0:
        return name + '/data'
    return name


def fstab_line(env, uuid, strage, out=None):
    """_strage_fstab(): STRAGEの/etc/fstabの行(ZFSはデータセットで。読み込めなければ'')。"""
    if strage == STRAGE:
        name = dataset(env, uuid, out)
        return '%s %s zfs rw,noauto 0 0' % (name, strage) if name else ''
    return 'UUID=%s %s %s rw,noauto 0 1' % (uuid, strage, strage.split('/')[2])
