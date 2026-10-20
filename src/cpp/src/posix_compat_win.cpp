// posix_compat.hpp(Windowsのネイティブ、.steering/20261015-windows-native-gui/)の実装のうち、windows.hの要るもの。
#include "posix_compat.hpp"

#include <windows.h>

#include <cerrno>

int kill(long long pid, int sig) {
  HANDLE h = OpenProcess(sig == 0 ? PROCESS_QUERY_LIMITED_INFORMATION : PROCESS_TERMINATE |
                                                                          PROCESS_QUERY_LIMITED_INFORMATION,
                         FALSE, static_cast<DWORD>(pid));
  // 無い・終わったプロセスはerrno=ESRCH(main.cppのtransientParentGone()が見る、2026-10-20)。
  if (!h) {
    if (GetLastError() == ERROR_INVALID_PARAMETER) errno = ESRCH;
    return -1;
  }
  int result = 0;
  if (sig == 0) {
    DWORD code = 0;
    if (!GetExitCodeProcess(h, &code) || code != STILL_ACTIVE) {
      result = -1;
      errno = ESRCH;
    }
  } else if (!TerminateProcess(h, 1)) {
    result = -1;
  }
  CloseHandle(h);
  return result;
}

void preciseSleepMicroseconds(long microseconds) {
  if (microseconds <= 0) return;
  LARGE_INTEGER frequency, start, now;
  QueryPerformanceFrequency(&frequency);
  QueryPerformanceCounter(&start);
  const long long target = start.QuadPart + frequency.QuadPart * microseconds / 1000000;
  for (;;) {
    QueryPerformanceCounter(&now);
    long long remaining = (target - now.QuadPart) * 1000000 / frequency.QuadPart;  // マイクロ秒
    if (remaining <= 0) return;
    if (remaining > 2000) {
      Sleep(static_cast<DWORD>(remaining / 1000 - 1));
    } else {
      YieldProcessor();
    }
  }
}
