#!/usr/bin/env python3
"""U.sh(src/U_proto.sh)のPython版(usilic/ush/、移したオプションだけ。他は/opt/U.sh)。

  sudo python3 ush.py -f            (区画を選んで/mnt/番号へ)
  sudo python3 ush.py -f --ans=3    (前から答えを渡す、usilicのRESCUE(^J^R)が使う)
  sudo python3 ush.py -v
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from usilic.ush.main import main  # noqa: E402

if __name__ == '__main__':
    main()
