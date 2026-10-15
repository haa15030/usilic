#include "doctest.h"

#include "child_process.hpp"
#include "native_grep.hpp"

using namespace fm;

// 2026-10-15(.steering/20261015-windows-native-gui/): Windowsのネイティブの内蔵grep(nativeGrep())が、usilicの使う
// 形(-Fne/-Ene/-ne パターン、本文は標準入力)で本物のgrepと同じ出力・終了状態になること。
TEST_CASE("nativeGrep matches the real grep for usilic's -Fne/-Ene/-ne use") {
  const std::string input =
      "HELP\tmanual\t\n"
      "\t^K：KEY\n"
      "  ^H：HELP\n"
      "\n"
      "plain line\n"
      "Mixed Case KEY\n"
      "no newline at end";
  const std::vector<std::vector<std::string>> cases = {
      {"-Fne", "\xEF\xBC\x9A"},        // 「：」(KEY)
      {"-Fne", "\t"},                  // タブ(EPUBの章の印)
      {"-ne", "^[^\t].*\t[ ]*$"},      // 基本正規表現(行頭がタブでなく行末がタブ)
      {"-Ene", "^(  |\t)\\^[HK]"},     // 拡張正規表現
      {"-Ene", "KEY$"},
      {"-Fne", "absent"},              // 一致なし(終了状態1)
      {"-ine", "mixed case"},          // -i
  };
  for (const auto &args : cases) {
    CAPTURE(args[1]);
    std::string out;
    int status = nativeGrep(args, input, out);
    ChildProcess::Options options;
    options.input = &input;
    std::vector<std::string> argv{"grep"};
    argv.insert(argv.end(), args.begin(), args.end());
    ChildProcess::Result real = ChildProcess::run(argv, options);
    CHECK(status == real.exitStatus);
    CHECK(out == real.output);
  }
  std::string out;
  CHECK(nativeGrep({"-Ene", "("}, input, out) == 2);  // パターンの誤り
}
