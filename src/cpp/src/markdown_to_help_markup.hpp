#pragma once

#include <string>

namespace fm {

// Markdown(見出し・太字・斜体・インラインコード・コードブロック・箇条書き・引用・水平線・
// リンク/画像・エスケープ)を、help_text.hpp自身のexpandHelpMarkup()が理解する
// <b>/<u>/<i>/<indentN>タグの文字列へ変換する。実際の制御バイトへの変換は
// expandHelpMarkup()へ任せる(二段構成、design.md参照)。
//
// 2026-09-22追加、直接のユーザー依頼("せっかく.mdフォーマットで書かれているので
// usilicに文字装飾機能を実装してほしい。ベタで読みたければ^[^Mということで")。
// .steering/20260922-markdown-format-support/design.md自身の対応表参照。
//
// カラー非対応の端末(Display::Attribute参照、Bold/Underline/Italic/Standoutのみ)なので、
// GFMのテーブル整列・取り消し線(~~)は変換せず素通しする(スコープ外、既知の限界)。
std::string markdownToHelpMarkup(const std::string &text);

}  // namespace fm
