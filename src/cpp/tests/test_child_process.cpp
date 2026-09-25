#include "child_process.hpp"
#include "doctest.h"

using fm::ChildProcess;

// These tests genuinely fork/exec real, standard Unix utilities
// (echo/cat/wc/tr) rather than faking anything -- the whole point of
// this class is the fork/pipe/exec plumbing itself, so that's what
// needs verifying end to end. Chosen commands are POSIX-standard and
// present on both Mac and Ubuntu.

TEST_CASE("run: captures a command's stdout") {
  auto result = ChildProcess::run({"echo", "hello"});
  CHECK(result.ok == true);
  CHECK(result.exitStatus == 0);
  CHECK(result.output == "hello\n");
}

TEST_CASE("run: reports a non-zero exit status") {
  auto result = ChildProcess::run({"sh", "-c", "exit 3"});
  CHECK(result.ok == false);
  CHECK(result.exitStatus == 3);
}

TEST_CASE("run: a nonexistent program fails cleanly instead of hanging or crashing") {
  auto result = ChildProcess::run({"this-command-does-not-exist-12345"});
  CHECK(result.ok == false);
}

TEST_CASE("run: feeds input to the child's stdin") {
  std::string input = "hello world\n";
  ChildProcess::Options options;
  options.input = &input;
  auto result = ChildProcess::run({"cat"}, options);
  CHECK(result.ok == true);
  CHECK(result.output == input);
}

TEST_CASE("run: large input doesn't deadlock against a full output pipe") {
  // Bigger than a typical 64KB pipe buffer, exercising the dedicated-
  // writer-process design (see child_process.hpp's Options::input
  // comment for why a same-process write would risk deadlock here).
  std::string input(1 << 20, 'x');  // 1 MB
  ChildProcess::Options options;
  options.input = &input;
  auto result = ChildProcess::run({"cat"}, options);
  CHECK(result.ok == true);
  CHECK(result.output.size() == input.size());
  CHECK(result.output == input);
}

TEST_CASE("run: forceCLocale sets LC_ALL=C in the child only") {
  ChildProcess::Options options;
  options.forceCLocale = true;
  auto result = ChildProcess::run({"sh", "-c", "echo $LC_ALL"}, options);
  CHECK(result.ok == true);
  CHECK(result.output == "C\n");
}

TEST_CASE("run: without forceCLocale, LC_ALL is not forced") {
  auto result = ChildProcess::run({"sh", "-c", "echo [$LC_ALL]"});
  CHECK(result.ok == true);
  CHECK(result.output == "[]\n");  // empty unless the test environment itself sets it
}

TEST_CASE("runPipeline: argv1 | argv2") {
  auto result = ChildProcess::runPipeline({"echo", "hello world"}, {"tr", "a-z", "A-Z"});
  CHECK(result.ok == true);
  CHECK(result.output == "HELLO WORLD\n");
}

TEST_CASE("runPipeline: with piped-in input as well (three stages: writer | argv1 | argv2)") {
  std::string input = "hello\n";
  ChildProcess::Options options;
  options.input = &input;
  auto result = ChildProcess::runPipeline({"cat"}, {"tr", "a-z", "A-Z"}, options);
  CHECK(result.ok == true);
  CHECK(result.output == "HELLO\n");
}

// ChildProcessOptions::maxOutputBytes自身のコメント参照(2026-09-25、usilicrcのBUFFER)。
TEST_CASE("run with maxOutputBytes: stops an endless writer, cuts at the last newline, and counts as ok") {
  ChildProcess::Options options;
  options.maxOutputBytes = 1000;
  ChildProcess::Result result = ChildProcess::run({"yes", "line"}, options);
  CHECK(result.truncated);
  CHECK(result.ok);
  CHECK(result.output.size() <= 1000);
  CHECK(result.output.size() > 900);
  CHECK(result.output.back() == '\n');
}
