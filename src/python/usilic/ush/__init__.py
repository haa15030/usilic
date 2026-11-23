"""U.sh(src/U_proto.sh、rescue-USB・RAMDISKの道具)のPython版。

旧C版(old/)とC++版(src/cpp/)の関係と同じく、U.sh(旧シェル版)の機能をオプションごとに移す。移したオプションは
PORTED、まだのものは今まで通りU.shで動かす(RESCUEのrescue.pyがどちらを呼ぶか決める)。/opt/BASE.uの状態には
python3が無い(-uの最初の一歩)ので、U.shはそのために残る。

引数はU.shと同じ(`ush.py -f`、`ush.py -f --ans=3`、`--list`・`--batch`)。rootで動かす(sudo)。

  system.py   外への働きかけ(コマンド・ファイル・マウントの状態)。テストではFakeSystemで記録するだけ
  env.py      U.shの始めの変数(ON_RAMDISK・MNT_DEBIAN・STRAGE等)
  fs.py       _fs・_fs_try・_is_mounted
  parts.py    _list_strage・_list_parts(区画の一覧と選ぶ番号)
  options.py  -a(add_strage)・-f(file_system)・-n(next_host)・-v(verbose)
  packages.py -o(_output_u、パッケージの記録と、記録へ戻す・足す)
  save.py     -s(save_host・_save_host、-d・-uも使う)
  main.py     引数・ログ(/opt/U.log_日付)
"""
PORTED = ('-a', '-c', '-f', '-n', '-o', '-s', '-v')
