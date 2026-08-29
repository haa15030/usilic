#pragma once

#include <string>

namespace fm {

// フォントのPostScript名/ファミリー名(例: "MS-Gothic"、Terminal.appの
// AppleScript "font name"プロパティがそのまま報告する形式)を、macOSの
// CoreTextフレームワーク(CTFontDescriptorCreateWithNameAndSize +
// kCTFontURLAttribute)経由で実際に読み込み可能なフォント「ファイル」
// パスへ解決する――これがmacOS上でフォント「名前」からフォント
// 「ファイル」へたどり着く唯一の確実な方法であり、両者を対応付ける
// ファイルシステム上の命名規則は存在しない(実際に確認済み:
// "MS-Gothic" -> "/Users/.../Library/Fonts/msgothic.ttc"、
// ユーザーごとにインストールされるフォントであり、固定の候補パス
// 一覧では絶対にカバーできない)。2026-07-31追加――main.cppの
// resolveGuiFontPath()自身のコメントには、以前から「フォント名による
// マッチングはまさにこの理由(文字列比較ではなく本物のフォント検索APIが
// 必要)により意図的にスコープ外」と書かれていたが、実際にGUIウィンドウ
// が呼び出し元ターミナルの設定フォント(日本語をフルカバーするための
// 「ＭＳゴシック14」等)を反映しないという実報告を受けて追加した。
//
// 何らかの理由で解決に失敗した場合(フォント未インストール、CoreText
// APIの失敗)、またはmacOS以外のプラットフォームでは無条件に(.cpp側の
// #ifdef __APPLE__参照)、空文字列""を返す――呼び出し側はいずれの場合も
// 自前の候補パス一覧へフォールバックする必要があり、これは絶対的な
// 保証ではない。
std::string resolveMacFontPath(const std::string &fontName, double pointSize);

}  // namespace fm
