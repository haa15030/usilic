#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>

#include "doctest.h"
#include "exec_mode.hpp"
#include "key_input.hpp"
#include "recording_display.hpp"

namespace fs = std::filesystem;

using fm::ExecMode;
using fm::KeyInput;
using fm::ProgramCommand;
using fm::test::RecordingDisplay;

namespace {

struct Fixture {
  RecordingDisplay display{24, 80};
  KeyInput keyInput{display};
  ExecMode mode{keyInput};
};

}  // namespace

TEST_CASE_FIXTURE(Fixture, "run: appends path as the final argument and runs it") {
  char tmpl[] = "/tmp/execmode_testXXXXXX";
  int fd = mkstemp(tmpl);
  REQUIRE(fd >= 0);
  close(fd);
  std::string markerPath(tmpl);

  ProgramCommand command;
  command.argv = {"sh", "-c", "echo ran > $0"};
  CHECK(mode.run(command, markerPath) == 0);

  std::ifstream in(markerPath);
  std::string content;
  std::getline(in, content);
  CHECK(content == "ran");
  fs::remove(markerPath);
}

TEST_CASE_FIXTURE(Fixture, "run: returns the real exit status") {
  ProgramCommand command;
  command.argv = {"sh", "-c", "exit $0"};
  CHECK(mode.run(command, "5") == 5);
}

TEST_CASE_FIXTURE(Fixture, "run: an empty command argv fails cleanly") {
  ProgramCommand command;
  CHECK(mode.run(command, "/tmp/whatever") == -1);
}
