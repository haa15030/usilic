#include "doctest.h"
#include "mode_stack.hpp"

using fm::ModeStack;
using fm::ModeStackFrame;

TEST_CASE("push/pop: LIFO order, empty stack pops nullopt") {
  ModeStack stack;
  CHECK(stack.empty());
  CHECK_FALSE(stack.pop().has_value());

  ModeStackFrame a;
  a.name = "a";
  ModeStackFrame b;
  b.name = "b";
  stack.push(a);
  stack.push(b);
  CHECK(stack.size() == 2);

  auto popped1 = stack.pop();
  REQUIRE(popped1.has_value());
  CHECK(popped1->name == "b");  // most recently pushed comes back first

  auto popped2 = stack.pop();
  REQUIRE(popped2.has_value());
  CHECK(popped2->name == "a");

  CHECK(stack.empty());
  CHECK_FALSE(stack.pop().has_value());
}

TEST_CASE("top(): reflects the most recent push without removing it") {
  ModeStack stack;
  CHECK(stack.top() == nullptr);

  ModeStackFrame f;
  f.name = "only";
  stack.push(f);
  REQUIRE(stack.top() != nullptr);
  CHECK(stack.top()->name == "only");
  CHECK(stack.size() == 1);  // top() didn't remove it
}

TEST_CASE("a frame carries the full LINE/CUT payload, round-tripped through push/pop") {
  ModeStack stack;
  ModeStackFrame f;
  f.mode = 42;
  f.previousMarker = "prev-marker";
  f.name = "/a/b";
  f.directory = "/a/";
  f.previousDirectory = "/old/";
  f.modeLineArg = "3 files";
  f.cutName = "cutname";
  f.cutSearchText = "search text";
  f.cutProgram = "grep";
  stack.push(f);

  auto popped = stack.pop();
  REQUIRE(popped.has_value());
  CHECK(popped->mode == 42);
  CHECK(popped->previousMarker == "prev-marker");
  CHECK(popped->name == "/a/b");
  CHECK(popped->directory == "/a/");
  CHECK(popped->previousDirectory == "/old/");
  CHECK(popped->modeLineArg == "3 files");
  CHECK(popped->cutName == "cutname");
  CHECK(popped->cutSearchText == "search text");
  CHECK(popped->cutProgram == "grep");
  CHECK_FALSE(popped->searchPattern.has_value());
}

TEST_CASE("searchPattern is the one field distinguishing a grep-results frame from an ordinary one") {
  ModeStack stack;
  ModeStackFrame nav;
  nav.name = "ordinary navigation frame";
  ModeStackFrame grep;
  grep.name = "grep results frame";
  grep.searchPattern = "TODO.*fixme";
  stack.push(nav);
  stack.push(grep);

  auto poppedGrep = stack.pop();
  REQUIRE(poppedGrep.has_value());
  REQUIRE(poppedGrep->searchPattern.has_value());
  CHECK(*poppedGrep->searchPattern == "TODO.*fixme");

  auto poppedNav = stack.pop();
  REQUIRE(poppedNav.has_value());
  CHECK_FALSE(poppedNav->searchPattern.has_value());
}

TEST_CASE("view/cutBuffer ownership: shared_ptr refcounting replaces the legacy f.bp flag") {
  ModeStack stack;
  auto refCounted = std::make_shared<int>(7);
  CHECK(refCounted.use_count() == 1);

  ModeStackFrame f;
  f.view = refCounted;
  stack.push(std::move(f));
  CHECK(refCounted.use_count() == 2);  // the stack now shares ownership

  auto popped = stack.pop();
  REQUIRE(popped.has_value());
  CHECK(refCounted.use_count() == 2);  // ownership moved to the popped-out copy, still alive
  CHECK(*static_cast<int *>(popped->view.get()) == 7);
}

TEST_CASE("view ownership: a no-op-deleter shared_ptr lets a pool slot survive the stack dropping it") {
  // Simulates legacy's f.bp==1 "this points into the shared reusable
  // pool, don't actually destroy it" case -- an ordinary stack-local
  // object (no heap allocation) wrapped in a shared_ptr whose deleter
  // callback runs (as any shared_ptr deleter does once the refcount
  // hits zero) but takes no action, so the pooled object itself is
  // untouched.
  int poolSlot = 99;
  bool deleterRan = false;
  std::shared_ptr<void> pooled(&poolSlot, [&deleterRan](void *) { deleterRan = true; });

  {
    ModeStack stack;
    ModeStackFrame f;
    f.view = pooled;
    stack.push(std::move(f));
    stack.pop();
  }
  pooled.reset();
  CHECK(deleterRan);      // the deleter callback did fire once the last shared_ptr dropped...
  CHECK(poolSlot == 99);  // ...but poolSlot itself was never touched, matching pool-slot semantics
}

TEST_CASE("pushFloor: establishes a single floor frame, clearing any existing frames") {
  ModeStack stack;
  ModeStackFrame stale;
  stale.name = "stale frame from a previous session";
  stack.push(stale);
  REQUIRE(stack.size() == 1);

  stack.pushFloor("/home/user/", "/home/", nullptr, 7);
  REQUIRE(stack.size() == 1);
  REQUIRE(stack.top() != nullptr);
  CHECK(stack.top()->directory == "/home/user/");
  CHECK(stack.top()->previousDirectory == "/home/");
  CHECK(stack.top()->mode == 7);
  CHECK(stack.isFloor(*stack.top()));
}

TEST_CASE("isFloor: only the floor frame (or one crafted with the same marker) is a floor") {
  ModeStack stack;
  stack.pushFloor("/x/", "/", nullptr, 1);
  ModeStackFrame ordinary;
  ordinary.name = "not a floor";
  stack.push(ordinary);

  auto popped = stack.pop();  // the ordinary frame
  REQUIRE(popped.has_value());
  CHECK_FALSE(stack.isFloor(*popped));

  REQUIRE(stack.top() != nullptr);
  CHECK(stack.isFloor(*stack.top()));  // the floor is still underneath
}

TEST_CASE("findByView / findByName: read-only existence checks that don't mutate the stack") {
  ModeStack stack;
  auto viewA = std::make_shared<int>(1);
  auto viewB = std::make_shared<int>(2);

  ModeStackFrame a;
  a.name = "a";
  a.view = viewA;
  ModeStackFrame b;
  b.name = "b";
  b.view = viewB;
  stack.push(a);
  stack.push(b);

  const ModeStackFrame *foundByView = stack.findByView(viewA.get());
  REQUIRE(foundByView != nullptr);
  CHECK(foundByView->name == "a");

  const ModeStackFrame *foundByName = stack.findByName("b");
  REQUIRE(foundByName != nullptr);
  CHECK(foundByName->view.get() == viewB.get());

  CHECK(stack.findByView(nullptr) == nullptr);
  CHECK(stack.findByName("nonexistent") == nullptr);
  CHECK(stack.size() == 2);  // reads didn't pop anything
}

TEST_CASE("deep nesting: no depth limit, matching legacy's unbounded linked-list behavior") {
  ModeStack stack;
  const int depth = 5000;
  for (int i = 0; i < depth; i++) {
    ModeStackFrame f;
    f.name = "level" + std::to_string(i);
    f.mode = i;
    stack.push(f);
  }
  CHECK(stack.size() == static_cast<std::size_t>(depth));

  for (int i = depth - 1; i >= 0; i--) {
    auto popped = stack.pop();
    REQUIRE(popped.has_value());
    CHECK(popped->mode == i);
  }
  CHECK(stack.empty());
}
