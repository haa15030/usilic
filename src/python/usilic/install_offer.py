"""起動時の、足りないプログラムのインストールの案内(C++のmain.cppのofferGroffInstallOnce())。

groff・mg・info・7-Zip・w3m(macOSはHomebrewのformula、他はapt)と、macOSは-gの既定のフォントのIPAゴシック、
Linuxは日本語のNoto CJKフォントのうち、見つからないものをSHELLで入れる。案内したパッケージは
~/.usilic/install-offeredに1行ずつ書き、まだ案内していないものがあればそれだけを次の起動でまた案内する。
"""
import os
import subprocess
import sys

if sys.platform == 'darwin':
    TOOLS = [('groff', 'groff'), ('mg', 'mg'), ('info', 'texinfo'), ('7zz', 'sevenzip'), ('w3m', 'w3m')]
else:
    TOOLS = [('groff', 'groff'), ('mg', 'mg'), ('info', 'info'), ('7z', 'p7zip-full'), ('w3m', 'w3m'),
             ('unzip', 'unzip')]


def find_executable(name, include_homebrew_defaults=True):
    """C++のfindExecutableIn(): PATH(と、FinderからでもHomebrewの既定の場所)。"""
    dirs = [d for d in os.environ.get('PATH', '').split(':') if d]
    if include_homebrew_defaults:
        dirs += ['/opt/homebrew/bin', '/usr/local/bin']
    for d in dirs:
        candidate = os.path.join(d, name)
        if os.access(candidate, os.X_OK) and not os.path.isdir(candidate):
            return candidate
    return ''


def ipa_gothic_font_path():
    home = os.path.expanduser('~')
    for name in ('ipag.ttc', 'ipag.ttf'):
        for d in (os.path.join(home, 'Library', 'Fonts'), '/Library/Fonts', '/usr/share/fonts/opentype/ipafont-gothic',
                  '/usr/share/fonts/truetype/ipafont-gothic'):
            if os.path.isfile(os.path.join(d, name)):
                return os.path.join(d, name)
    try:
        out = subprocess.run(['fc-list', '-f', '%{file}\n', ':family=IPAGothic'], capture_output=True,
                             text=True).stdout
    except OSError:
        return ''
    path = out.split('\n', 1)[0]
    return path if path and os.path.isfile(path) else ''


def missing_packages():
    missing = []
    for command, package in TOOLS:
        found = bool(find_executable(command))
        if command in ('7zz', '7z'):
            found = bool(find_executable('7z') or find_executable('7zz'))
        if not found:
            missing.append(package)
    if sys.platform == 'darwin':
        if not ipa_gothic_font_path():
            missing.append('font-ipafont')
    elif not any(os.path.exists(p) for p in ('/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc',
                                             '/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc')):
        missing.append('fonts-noto-cjk')
    return missing


def plan(usilic_dir, first_start=False):
    """案内するなら(最初のコマンド, 続きのコマンドを返す関数, 案内できた時に印を書く関数)、無ければNone。
    最初の起動(first_start)では、案内済みの印によらず見つからないものを全部(C++の項目1187)。"""
    marker = os.path.join(usilic_dir, 'install-offered')
    try:
        with open(marker, encoding='utf-8') as f:
            offered = [line.strip() for line in f if line.strip()]
    except OSError:
        offered = []
    to_offer = [p for p in missing_packages() if first_start or p not in offered]
    if not to_offer:
        return None

    def install_command():
        packages = [p for p in missing_packages() if p in to_offer]
        if not packages:
            return ''
        if sys.platform == 'darwin':
            brew = find_executable('brew')
            if not brew:
                return ('NONINTERACTIVE=1 /bin/bash -c "$(curl -fsSL '
                        'https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"'
                        '  # Homebrew未インストール(Xcode Command Line Toolsも必要なら入る)。終わると次は brew install ' +
                        ' '.join(packages))
            program = 'HOMEBREW_NO_ASK=1 ' + ('brew' if brew == find_executable('brew', False) else brew)
            if 'font-ipafont' in packages and packages != ['font-ipafont']:
                rest = [p for p in packages if p != 'font-ipafont']
                return program + ' install font-ipafont; ' + program + ' install ' + ' '.join(rest)
            return program + ' install ' + ' '.join(packages)
        return 'sudo apt install -y ' + ' '.join(packages)

    command = install_command()
    if not command:
        return None

    def on_offered():
        try:
            os.makedirs(usilic_dir, exist_ok=True)
            with open(marker, 'w', encoding='utf-8') as f:
                for package in offered + [p for p in to_offer if p not in offered]:
                    f.write(package + '\n')
        except OSError:
            pass
    return command, install_command, on_offered
