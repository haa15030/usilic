#include "delegating_display.hpp"
#include "doctest.h"
#include "recording_display.hpp"

using fm::DelegatingDisplay;
using fm::Display;
using fm::test::RecordingDisplay;

TEST_CASE("DelegatingDisplay: forwards writes/geometry to the initial backend") {
  RecordingDisplay real(10, 20);
  DelegatingDisplay delegate(real);

  delegate.moveCursor(2, 3);
  delegate.write("hi");
  CHECK(real.rowText(2).substr(3, 2) == "hi");
  CHECK(delegate.rows() == 10);
  CHECK(delegate.columns() == 20);
}

TEST_CASE("DelegatingDisplay: setBackend() redirects subsequent calls to the new backend") {
  RecordingDisplay first(10, 20);
  RecordingDisplay second(5, 40);
  DelegatingDisplay delegate(first);

  delegate.moveCursor(0, 0);
  delegate.write("before");
  CHECK(first.rowText(0).substr(0, 6) == "before");

  delegate.setBackend(second);
  delegate.moveCursor(1, 1);
  delegate.write("after");
  CHECK(second.rowText(1).substr(1, 5) == "after");
  CHECK(first.rowText(1).substr(1, 5) != "after");  // the old backend is untouched by post-swap calls
  CHECK(delegate.rows() == 5);                       // geometry now reflects the NEW backend
  CHECK(delegate.columns() == 40);
  CHECK(&delegate.backend() == static_cast<Display *>(&second));
}

TEST_CASE("DelegatingDisplay: attribute/scroll/cursor calls all forward correctly") {
  RecordingDisplay real(10, 20);
  DelegatingDisplay delegate(real);

  delegate.setAttribute(Display::Attribute::Standout);
  delegate.moveCursor(0, 0);
  delegate.write("x");
  CHECK(real.standoutAt(0, 0));

  delegate.scrollForward();
  delegate.scrollReverse();
  CHECK(real.scrollForwardCount() == 1);
  CHECK(real.scrollReverseCount() == 1);

  delegate.setSmoothScrollEnabled(true);
  CHECK(delegate.smoothScrollEnabled());
  CHECK(real.smoothScrollEnabled());
}
