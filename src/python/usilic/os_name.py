"""INFOの1行目「OS名 : コマンドライン」(C++のosDisplayName()・main.cppのbootOptions)。"""
import os
import platform
import sys

_MAC_NAMES = {11: 'Big Sur', 12: 'Monterey', 13: 'Ventura', 14: 'Sonoma', 15: 'Sequoia', 26: 'Tahoe'}
_MAC_10_NAMES = {13: 'High Sierra', 14: 'Mojave', 15: 'Catalina'}


def mac_release_name(version):
    """macOSの版の名前(「15.7.8」→「Sequoia」)。知らない番号は空。"""
    parts = (version.split('.') + ['0'])[:2]
    try:
        major, minor = int(parts[0]), int(parts[1])
    except ValueError:
        return ''
    return _MAC_10_NAMES.get(minor, '') if major == 10 else _MAC_NAMES.get(major, '')


def linux_name(release):
    """os-releaseの値(辞書)から「Ubuntu 26.10 Stonking Stingray」。VERSIONが無ければPRETTY_NAME。"""
    name, version = release.get('NAME', ''), release.get('VERSION', '')
    if name and version:
        return ' '.join((name + ' ' + version.replace('(', '').replace(')', '').replace(',', '')).split())
    return release.get('PRETTY_NAME', '')


def os_display_name():
    """「macOS Sequoia 15.7.8」「Ubuntu 26.10 Stonking Stingray」「Windows 11 Pro 25H2」。"""
    if sys.platform == 'darwin':
        version = platform.mac_ver()[0]
        name = mac_release_name(version)
        return ' '.join(x for x in ('macOS', name, version) if x)
    if sys.platform.startswith('win'):
        try:
            import winreg
            key = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\Microsoft\Windows NT\CurrentVersion')
            value = lambda n: str(winreg.QueryValueEx(key, n)[0]) if n else ''  # noqa: E731
            product = value('ProductName')
            try:
                display = value('DisplayVersion')
            except OSError:
                display = ''
            build = int(value('CurrentBuildNumber') or 0)
        except (OSError, ImportError, ValueError):
            return 'Windows'
        if build >= 22000 and product.startswith('Windows 10'):  # 11でもProductNameは「Windows 10 …」のまま
            product = 'Windows 11' + product[10:]
        return (product + ' ' + display).strip()
    try:
        name = linux_name(platform.freedesktop_os_release())
    except (OSError, AttributeError):
        name = ''
    return name or (platform.system() + ' ' + platform.release()).strip()


def command_line():
    """起動のコマンドライン(「python3 usilic.py -c」、プログラムとスクリプトは名前だけ)。"""
    argv = list(getattr(sys, 'orig_argv', None) or [os.path.basename(sys.executable)] + sys.argv)
    for i in range(min(2, len(argv))):
        if not argv[i].startswith('-'):
            argv[i] = os.path.basename(argv[i])
    if argv and argv[0].lower().startswith('python'):  # macOSのPythonは本体の名前(「Python」)になるので
        argv[0] = 'python3'
    home = os.path.expanduser('~')
    return ' '.join(home + a[1:] if a == '~' or a.startswith('~/') else a for a in argv)
