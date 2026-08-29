#pragma once

#include <optional>
#include <string>

namespace fm {

// GNOME Terminal自身の`gsettings`/dconf出力形式の純粋なパース
// ヘルパー、および`fc-match`ベースのフォント「名前」→ファイル
// 「パス」解決器――mac_font_resolver.hppのLinux版に相当し、
// 2026-07-31追加。ユーザーが(Web検索により)、GNOME Terminalの
// フォント/色/プロファイル設定が結局はコマンドラインから問い合わせ
// 可能であることを発見したことを受けて追加した。このプロジェクトの
// 以前の回答は、Linux上にはそのような仕組みが存在しないと思い込んで
// いたが、それとは異なる。app/main.cpp自身のqueryTerminalAppColors()
// (macOS/AppleScript版、そちらは数値のみのsscanfパースを自前で
// インライン化している)から分離しているのは、まさにgsettingsの
// 出力が本物の「文字列」パース(クォート付き文字列、"rgb(r,g,b)"、
// Pangoフォント記述)を必要とするため――このプロジェクト自身の開発
// サンドボックスからは実際に稼働しているGNOME Terminalに到達できず
// これをエンドツーエンドで検証できないため、これらを純粋関数にして
// あるのはまさに、doctestが実際の検証手段になれるようにするため
// である。

struct GsettingsColor {
  unsigned char r = 0, g = 0, b = 0;
};

// gsettings自身の色形式、例えば"'rgb(255,255,255)'"(gsettingsは
// 文字列型の値をシングルクォートで囲む。サブプロセス自身の出力末尾の
// 改行も許容する)、あるいは実際のUbuntu 26.04上のgsettingsが代わりに
// 返すことが確認された(2026-07-31、このプロジェクト自身の開発
// コンテナ上でgnome-terminalをインストールしそのスキーマを登録した
// 上でその場で確認)"#RRGGBB"の16進形式、のどちらかをパースする
// ――両方に対応している理由は.cpp参照。どちらにも一致しない文字列
// ならnulloptを返す。
std::optional<GsettingsColor> parseGsettingsRgbColor(const std::string &raw);

struct ParsedFontDescription {
  std::string family;
  int pointSize;
};

// Pangoフォント記述文字列(GNOME Terminal自身の"font" gsettingsキー、
// およびorg.gnome.desktop.interfaceの"monospace-font-name"キー、
// どちらもこの形式で保存する)――例えば"Ubuntu Mono 11"や
// "Monospace Bold 12"――をファミリー名とポイントサイズへパースする。
// サイズはPango自身の慣習に従う: 空白区切りされたトークンのうち、
// 整数としてパースできる「最後」のもの。それより前の全て("Bold"
// のようなスタイルキーワードを含む)はまとめて`family`として保持
// する。これは、下にあるfc-match/resolveLinuxFontPath()にファミリー
// 文字列がそのまま渡され、本物のPangoベースのレンダラーと同じ形で
// fontconfig自身のマッチングにスタイルキーワードの処理を任せる
// ためである。末尾に整数が見つからない場合、fallbackPointSizeを
// そのまま使う。gsettings自身が付ける囲みのシングルクォートが
// あれば、それも取り除く(stripGsettingsQuotedString()参照)ので、
// gsettingsの生の出力をそのまま渡すことができる。
ParsedFontDescription parsePangoFontDescription(const std::string &raw, int fallbackPointSize);

// gsettings自身が文字列型の値の周りに付ける囲みのシングルクォート
// (例えば"'some value'\n" -> "some value")と、末尾の空白/改行を
// 取り除く――事実上あらゆるgsettingsの文字列型クエリ(プロファイル
// ID、フォント名、等)が返す汎用的な形。
std::string stripGsettingsQuotedString(const std::string &raw);

// フォントファミリー名を、fontconfigの`fc-match`コマンドラインツール
// (`fontconfig`パッケージの一部、GTK/Pango自身がそれに依存している
// ため、デスクトップ向けLinuxディストリビューションではほぼ普遍的な
// 依存関係――このプロジェクト自身のUbuntu 26.04開発コンテナには
// gnome-terminal自体はインストールされていないにもかかわらず存在
// することを確認済み)経由で実際に読み込み可能なフォント「ファイル」
// パスへ解決する。これはmac_font_resolver.hppのCTFontDescriptor
// ベースの解決処理に相当するLinux版であり、CoreTextとは異なり
// fc-matchは単なるサブプロセス呼び出しなので、特別なリンクは一切
// 不要である。fc-matchが未インストール、フォント名が解決できない、
// または(暗黙的に、これでもコンパイル・実行はできてしまう非Linux
// プラットフォーム上で)ツールが単に見つからない場合は""を返す
// ――ChildProcess::run()は既にそれをプラットフォームを問わず一律に
// `!ok`として報告するので、mac_font_resolver.cppがCoreTextの
// フレームワーク専用APIのために必要としているような#ifdefガードは
// ここには存在しない。
std::string resolveLinuxFontPath(const std::string &fontName);

}  // namespace fm
