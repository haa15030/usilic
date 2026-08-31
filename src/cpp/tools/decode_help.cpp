// 2026-08-31、直接のユーザー依頼("HELP.jpをソースに取り込んでもらう
// 必要がある"): src/doc/HELP.jp(実際の重ね打ちバイト列を持つ、legacy
// バイナリが今も表示するのと全く同じファイル)を、src/cpp/src/
// help_text.hpp自身のkHelpTextJaが使う<b>...</b>/<u>...</u>という軽量
// マークアップへ変換する、使い捨てではなく再利用前提の小さなツール。
//
// なぜこれが必要か: kHelpTextJaはHELP.jpの重ね打ちバイト列(1文字ごとの
// "X\bX"=太字、"_\bX"=下線、PipeBuffer::wrapLine()自身のコメント参照)
// を「直接」埋め込んでいるわけではない――そのままではソース上で読めない
// 制御バイトの塊になってしまう。代わりに、実際のPipeBuffer::wrapLine()
// (このC++移植自身の、重ね打ちバイト列を解釈する本物のデコーダ)へ
// 通して得た各行のtext+AttributeRun(Bold/Underline)を、隣接する同種の
// 属性ランをまとめた上で<b>/<u>タグへ変換する――expandHelpMarkup()
// (help_text.cpp)がその逆変換を行い、1文字ごとの重ね打ちバイト列へ
// 戻す。この往復が情報を一切失わないことは、隣接するverify_help.cpp
// (このディレクトリ)で確認できる。
//
// 使い方その1(kHelpTextJa再生成、ユーザーが今後src/HELP.txt上のdiffで
// 新しい変更を示した場合、またはsrc/doc/HELP.jp自体が更新された場合):
//   1. このツールをビルドして実行し、最新のHELP.jpをタグ付きテキスト
//      へ変換する:
//        c++ -std=c++17 -I../src decode_help.cpp ../src/pipe_buffer.cpp \
//            ../src/charset.cpp ../src/utf8.cpp -o /tmp/decode_help
//        /tmp/decode_help ../../doc/HELP.jp > /tmp/helpjp_decoded.txt
//      (下線タグは省略時"u"のまま――expandHelpMarkup()の固定の語彙に
//      合わせる。使い方その2とは異なる。)
//   2. 出力(<b>/<u>タグ付きの読みやすいテキスト)を見比べ、実際のC++
//      移植で動作が変わり明確に誤りになる箇所「だけ」を、周辺の文体・
//      装飾を保ったまま最小限で修正する(help_text.hpp冒頭のコメントが
//      列挙する既知の修正点を参照、今後もリストを更新すること)。
//   3. 結果をsrc/cpp/src/help_text.hppのkHelpTextJa(R"HELPTXT(...
//      )HELPTXT";で囲まれた本文)へ丸ごと差し替える。
//   4. verify_help.cpp(同ディレクトリ)で、修正「前」のHELP.jpとの
//      差分が意図した箇所だけであることを確認する(完全一致ではなく、
//      意図的な修正以外に差分が無いことの確認用)。
//   5. make test / make usilic で確認してからコミットする。
//
// 使い方その2(src/HELP.txt自身への「逆」の書き戻し、2026-09-01追加、
// 直接のユーザー依頼: "逆の作業（HELP.jpの`bold`と*アンダーライン*
// 箇所を前出のルールでHELP.txtに書き戻し）を機械的に行えるなら...
// 誤変換防止の意味では<b></b><i></i>の方がいいのでそちらでやって"、
// 直後に訂正: "うっかりしていたけど、<i></i>はアンダーラインじゃなくて
// イタリック（斜体）だった...<u>?それに修正よろしく"): src/HELP.txt
// (ユーザー自身が編集する、業務連絡版の下書き)自体はkHelpTextJaの
// 入力では「ない」(一次資料は常にsrc/doc/HELP.jp自身、help_text.hpp
// 冒頭のコメント参照)が、ユーザーが手作業で`X`/*X*とマークアップを
// 再構築する手間(誤変換の危険もある)を省くため、HELP.jp自身が既に
// 持つ装飾を、<b></b>/<u></u>タグでsrc/HELP.txtへそのまま書き戻せる
// ――内容(プローズ)自体は一切変えず、装飾の位置だけを機械的に反映する:
//   /tmp/decode_help ../../doc/HELP.jp 100000 u > /tmp/helptxt_new.txt
//   cp /tmp/helptxt_new.txt ../../HELP.txt
// (下線タグの第3引数は結局デフォルトの"u"と同じ値になった――上の
// 訂正の経緯どおり<i>は誤りだったため。この引数自体は残してある:
// 万一src/HELP.txt側で今後<u>と異なるタグを使いたくなった場合に
// 備えて。src/HELP.txt自身に手作業での上書き変更が既にある場合は、
// 事前にその内容(プローズの変更点)を控えてから、この機械的な書き戻し
// の後に改めて適用し直すこと――このツール自体はHELP.txt側の既存の
// 手作業編集を一切認識しない、単純な全文置き換えである。)

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "pipe_buffer.hpp"

using fm::Charset;
using fm::Display;
using fm::PipeBuffer;

int main(int argc, char **argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <HELP.jp> [width] [underline-tag]\n", argv[0]);
    return 1;
  }
  // 2026-09-01追加、直接のユーザー依頼("誤変換防止の意味では
  // <b></b><i></i>の方がいいのでそちらでやってみて"): src/HELP.txt
  // 自身(ユーザーの手書き編集用、業務連絡版)へ書き戻す時はユーザー
  // 自身の指定通り下線に<i>を使いたいが、kHelpTextJa自身の生成では
  // expandHelpMarkup()(help_text.cpp)が固定で<u>を要求する――同じ
  // ツールをどちらの出力にも使えるよう、下線タグ名を引数化した
  // (デフォルトは既存の呼び出し元との後方互換のため"u"のまま)。
  std::string underlineTag = argc >= 4 ? argv[3] : "u";
  std::ifstream in(argv[1], std::ios::binary);
  if (!in) {
    std::fprintf(stderr, "cannot open %s\n", argv[1]);
    return 1;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  std::string raw = ss.str();

  // 幅は意図的に大きくしておく(折り返しではなく、元のHELP.jp自身の
  // 改行位置をそのまま保つため) -- 折り返しはこの移植の他の全ての
  // 呼び出し元と同様、実際の表示時にPipeBufferが改めて行う。
  int width = argc >= 3 ? std::atoi(argv[2]) : 100000;
  PipeBuffer pipe(Charset::Encoding::Utf8, width, 16L * 1024 * 1024);
  pipe.feed(raw);
  pipe.flush();

  for (const auto &line : pipe.lines()) {
    const std::string &text = line.text;
    // 隣接する同種の属性ランをまとめる -- wrapLine()は重ね打ちを検出
    // した文字1つごとに別々のAttributeRunを作るため、まとめないと
    // <b>c</b><b>o</b><b>n</b>...のように読みにくくなる。
    // expandHelpMarkup()は<b>/<u>で囲まれた範囲を内部で1文字ずつ
    // 展開するので、まとめても展開結果は変わらない。
    std::vector<Display::AttributeRun> merged;
    for (const auto &run : line.attributes) {
      if (!merged.empty() && merged.back().attribute == run.attribute && merged.back().end == run.start) {
        merged.back().end = run.end;
      } else {
        merged.push_back(run);
      }
    }
    std::string out;
    std::size_t pos = 0;
    for (const auto &run : merged) {
      if (static_cast<std::size_t>(run.start) > pos) out += text.substr(pos, run.start - pos);
      std::string tag = (run.attribute == Display::Attribute::Bold) ? "b" : underlineTag;
      out += "<" + tag + ">" + text.substr(run.start, run.end - run.start) + "</" + tag + ">";
      pos = run.end;
    }
    if (pos < text.size()) out += text.substr(pos);
    std::printf("%s\n", out.c_str());
  }
  return 0;
}
