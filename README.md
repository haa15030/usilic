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
| macOS | `HOMEBREW_NO_ASK=1 brew install font-ipafont; HOMEBREW_NO_ASK=1 brew install groff mg texinfo sevenzip w3m` |
| macOS(Homebrew が無い時) | Homebrew 自体のインストール(https://brew.sh 、`NONINTERACTIVE=1`)の後に上のコマンド |
| Ubuntu・WSL | `sudo apt install -y groff mg info p7zip-full w3m unzip fonts-noto-cjk` |
| MSYS2 | `pacman -S --needed --noconfirm groff texinfo p7zip unzip` |
| Windows(usilic.exe) | 何も入れません |

実際に実行するのは、このうち見つからないものだけです。
macOS の font-ipafont は IPA ゴシックで、入ればその場で `-g` の窓のフォントになります。
パスワード(sudo)を聞かれた時は、SHELL の画面で入力してください。
`-g` では、インストールの間は上に HELP の表紙と案内を出し、終わると HELP が開きます。
