#pragma once

// "fm"(file manager) -- 製品名そのものではなく意図的にこの名前にして
// いる。名前自体は今後変わる予定のため(CLAUDE.md/IDEAS.md参照)。
namespace fm {

// UTF-8のバイト長/表示幅ヘルパー。
//
// Cソース側のUTF-8対応作業(CLAUDE.md参照)で追加されたsrc/code.cの
// utf8_seqlen()/utf8_width()からの移植。あちらのプロジェクトでは、
// マルチバイト文字の「バイト数」と「表示カラム数」を混同するという
// 同種のバグを何度も踏んだ。両者を明確に別の、名前で意味が分かる関数
// として分離しているのは意図的なもの――呼び出し側は自分が本当に
// 必要な方を選ばなければならない(どちらか勝手に推測する単一の
// "count()"にはしない)。
class Utf8 {
 public:
  // 先頭バイトcで始まるUTF-8シーケンスのバイト長(1〜4)。
  // 迷子/不正な継続バイトの場合は1を返すため、不正な入力に対しても
  // 呼び出し側がバッファをはみ出したりずれたりすることはない。
  static int sequence_length(unsigned char c);

  // cpにあるlenバイトのUTF-8シーケンスが表すコードポイントの表示幅
  // (0、1、または2カラム)。East Asian Wide/Fullwidth範囲の実用上
  // 十分な部分集合(日本語/CJKテキストというusilicの主用途には足りる)に、
  // いくつかの幅ゼロの結合文字範囲を加えたもの。完全なUnicode
  // East-Asian-Widthテーブルではない。
  static int display_width(const unsigned char *cp, int len);
};

}  // namespace fm
