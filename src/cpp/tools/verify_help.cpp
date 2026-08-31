// decode_help.cpp(同ディレクトリ)自身のコメント参照: kHelpTextJaを
// 書き換えた後、「意図した修正箇所以外は元のsrc/doc/HELP.jpと一致して
// いる」ことを確認するための往復検証ツール。
//
// 引数1: 比較対象のHELP.jp(通常はsrc/doc/HELP.jp自身、または過去の
// バージョン)。引数2: expandHelpMarkup()を通した後と比較したい
// <b>/<u>タグ付きテキスト(通常はhelp_text.hppのkHelpTextJaの中身を
// 抜き出したもの)。
//
// 完全一致(0件の不一致)を意図的な修正を「一切」加えていない場合の
// 期待値として使う(decode_help.cppの出力をそのまま検証する時等)。
// 修正を加えた後は、不一致が出るのが正常――出力される各行のdiffを
// 目視で確認し、それが「意図した修正だけ」であることを確かめること。
//
// 使い方:
//   c++ -std=c++17 -I../src verify_help.cpp ../src/pipe_buffer.cpp \
//       ../src/charset.cpp ../src/utf8.cpp ../src/help_text.cpp \
//       -o /tmp/verify_help
//   /tmp/verify_help ../../doc/HELP.jp /tmp/helpjp_tagged.txt

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "help_text.hpp"
#include "pipe_buffer.hpp"

using fm::Charset;
using fm::PipeBuffer;
using fm::expandHelpMarkup;

namespace {
std::string readFile(const char *path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}
}  // namespace

int main(int argc, char **argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <HELP.jp> <tagged.txt>\n", argv[0]);
    return 1;
  }
  std::string raw = readFile(argv[1]);
  std::string tagged = readFile(argv[2]);
  std::string reencoded = expandHelpMarkup(tagged);

  PipeBuffer original(Charset::Encoding::Utf8, 100000, 16L * 1024 * 1024);
  original.feed(raw);
  original.flush();
  PipeBuffer reconstructed(Charset::Encoding::Utf8, 100000, 16L * 1024 * 1024);
  reconstructed.feed(reencoded);
  reconstructed.flush();

  const auto &a = original.lines();
  const auto &b = reconstructed.lines();
  std::printf("line counts: %zu vs %zu\n", a.size(), b.size());
  int mismatches = 0;
  for (std::size_t i = 0; i < std::min(a.size(), b.size()); i++) {
    bool textDiffers = a[i].text != b[i].text;
    bool attrsDiffer = a[i].attributes.size() != b[i].attributes.size();
    if (!attrsDiffer) {
      for (std::size_t k = 0; k < a[i].attributes.size(); k++) {
        if (a[i].attributes[k].start != b[i].attributes[k].start ||
            a[i].attributes[k].end != b[i].attributes[k].end ||
            a[i].attributes[k].attribute != b[i].attributes[k].attribute) {
          attrsDiffer = true;
          break;
        }
      }
    }
    if (textDiffers || attrsDiffer) {
      mismatches++;
      std::printf("line %zu differs:\n  a: [%s]\n  b: [%s]\n", i, a[i].text.c_str(), b[i].text.c_str());
    }
  }
  std::printf("total differing lines: %d\n", mismatches);
  return 0;
}
