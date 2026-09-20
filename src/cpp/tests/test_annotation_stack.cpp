#include "annotation_stack.hpp"
#include "doctest.h"

using fm::AnnotationStack;

namespace {
enum class TestKind { Grep, Cut, Du };
}

TEST_CASE("AnnotationStack: push sets topKind/displayText") {
  AnnotationStack<TestKind> stack;
  CHECK(stack.empty());
  CHECK(!stack.topKind().has_value());
  CHECK(stack.displayText() == "");

  stack.push(TestKind::Grep, "GREP: pattern");
  CHECK(!stack.empty());
  REQUIRE(stack.topKind().has_value());
  CHECK(*stack.topKind() == TestKind::Grep);
  CHECK(stack.displayText() == "GREP: pattern");
}

TEST_CASE("AnnotationStack: pop restores the previous frame") {
  AnnotationStack<TestKind> stack;
  stack.push(TestKind::Grep, "GREP: a");
  stack.push(TestKind::Cut, "CUT: b");
  CHECK(stack.displayText() == "CUT: b");

  stack.pop();
  CHECK(!stack.empty());
  CHECK(*stack.topKind() == TestKind::Grep);
  CHECK(stack.displayText() == "GREP: a");

  stack.pop();
  CHECK(stack.empty());
}

TEST_CASE("AnnotationStack: pop on an empty stack is a no-op (does not throw)") {
  AnnotationStack<TestKind> stack;
  CHECK_NOTHROW(stack.pop());
  CHECK(stack.empty());
  CHECK_NOTHROW(stack.pop());  // 空スタックへの2回目のpop()も安全
  CHECK(stack.empty());
}

TEST_CASE("AnnotationStack: clear empties the stack regardless of depth") {
  AnnotationStack<TestKind> stack;
  stack.push(TestKind::Grep, "GREP: a");
  stack.push(TestKind::Cut, "CUT: b");
  stack.push(TestKind::Du, "DU: c");
  stack.clear();
  CHECK(stack.empty());
  CHECK(!stack.topKind().has_value());
  CHECK(stack.displayText() == "");
}

TEST_CASE("AnnotationStack: top() exposes the Frame without popping it") {
  AnnotationStack<TestKind> stack;
  CHECK(stack.top() == nullptr);
  stack.push(TestKind::Du, "DU: path");
  const auto *frame = stack.top();
  REQUIRE(frame != nullptr);
  CHECK(frame->kind == TestKind::Du);
  CHECK(frame->text == "DU: path");
  CHECK(!frame->nameOverride.has_value());
  // top()は取り除かない
  CHECK(!stack.empty());
  CHECK(stack.displayText() == "DU: path");
}

TEST_CASE("AnnotationStack: pushWithNameOverride sets both nameOverride and text") {
  AnnotationStack<TestKind> stack;
  stack.pushWithNameOverride(TestKind::Du, "label", "detail");
  const auto *frame = stack.top();
  REQUIRE(frame != nullptr);
  CHECK(frame->kind == TestKind::Du);
  REQUIRE(frame->nameOverride.has_value());
  CHECK(*frame->nameOverride == "label");
  CHECK(frame->text == "detail");
}

TEST_CASE("AnnotationStack: a plain push() (no name override) after pushWithNameOverride "
          "correctly reports no nameOverride on the new top") {
  AnnotationStack<TestKind> stack;
  stack.pushWithNameOverride(TestKind::Du, "label", "detail");
  stack.push(TestKind::Grep, "GREP: x");
  const auto *frame = stack.top();
  REQUIRE(frame != nullptr);
  CHECK(!frame->nameOverride.has_value());
  CHECK(frame->text == "GREP: x");
}
