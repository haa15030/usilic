# usilic

テキストベースのファイルマネジャー(旧 tcb)の C++ 版です。
本体は `src/cpp/` にあります。

## ビルドとインストール

```
cd src/cpp
make usilic      # build/usilic
make install     # ~/.local/bin/usilic と ~/.local/lib/usilic/
make app         # macOS: build/usilic.app を作り、/Applications へコピー
make -f windows.mk dist   # Windows 版: build-win/usilic-win64.zip
```

macOS では Xcode の Command Line Tools(`xcode-select --install`)が必要です。

## 最初の起動で自動で入るもの

usilic は groff(HELP・MAN の成形)、mg、info、7-Zip(ARC の .7z)、w3m(EPUB の Web のリンク)等の外部プログラムを使います。
最初の起動時、見つからないものがあれば SHELL に切り替わり、次のコマンドを**確認無しで自動で実行**します。
同じものを入れるのは一度だけです(`~/.usilic/install-offered` に記録します)。

| 環境 | 実行するコマンド |
|------|------------------|
| macOS | IPA フォントを配布元の zip から `~/Library/Fonts` へ入れた後、`sudo port -N install groff mg texinfo 7zip w3m fontconfig`(MacPorts。macOS の版や CPU によらず。MacPorts が無ければ https://www.macports.org/install.php を開く) |
| Ubuntu・WSL | `sudo apt install -y groff mg info p7zip-full w3m unzip fonts-noto-cjk` |
| MSYS2 | `pacman -S --needed --noconfirm groff texinfo p7zip unzip` |
| Windows(usilic.exe) | 何も入れません |

実際に実行するのは、このうち見つからないものだけです。
macOS の IPA フォントは、入ればその場で IPA ゴシックが `-g` の窓のフォントになります。
パスワード(sudo)を聞かれた時は、SHELL の画面で入力してください。
`-g` では、インストールの間は上に HELP の表紙と案内を出し、終わると HELP が開きます。
