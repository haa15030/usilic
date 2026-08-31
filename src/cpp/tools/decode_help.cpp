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
// 使い方(ユーザーが今後src/HELP.txt上のdiffで新しい変更を示した場合、
// またはsrc/doc/HELP.jp自体が更新された場合の再生成手順):
//   1. このツールをビルドして実行し、最新のHELP.jpをタグ付きテキスト
//      へ変換する:
//        c++ -std=c++17 -I../src decode_help.cpp ../src/pipe_buffer.cpp \
//            ../src/charset.cpp ../src/utf8.cpp -o /tmp/decode_help
//        /tmp/decode_help ../../doc/HELP.jp > /tmp/helpjp_decoded.txt
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
// src/HELP.txt(ユーザー自身が用意した平文の「業務連絡版」の下書き)は
// この変換の入力では「ない」――一次資料は常にsrc/doc/HELP.jp自身で
// あることについてはhelp_text.hpp冒頭のコメント参照。

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
    std::fprintf(stderr, "usage: %s <HELP.jp> [width]\n", argv[0]);
    return 1;
  }
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
      std::string tag = (run.attribute == Display::Attribute::Bold) ? "b" : "u";
      out += "<" + tag + ">" + text.substr(run.start, run.end - run.start) + "</" + tag + ">";
      pos = run.end;
    }
    if (pos < text.size()) out += text.substr(pos);
    std::printf("%s\n", out.c_str());
  }
  return 0;
}
