// posix_compat.hpp(Windowsのネイティブ、.steering/20261015-windows-native-gui/)の実装のうち、windows.hの要るもの。
#include "posix_compat.hpp"

#include "console_win.hpp"

#include <windows.h>

#include <cerrno>
#include <csignal>
#include <string>

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

static std::string quitEventName(long long pid) { return "Local\\usilic-quit-" + std::to_string(pid); }

static DWORD WINAPI waitForQuitRequest(LPVOID parameter) {
  HANDLE *handles = static_cast<HANDLE *>(parameter);
  WaitForSingleObject(handles[0], INFINITE);
  *reinterpret_cast<volatile std::sig_atomic_t *>(handles[1]) = 1;
  return 0;
}

void listenForQuitRequest(volatile std::sig_atomic_t *flag) {
  HANDLE event = CreateEventA(nullptr, TRUE, FALSE, quitEventName(GetCurrentProcessId()).c_str());
  if (!event) return;
  // スレッドもイベントもプロセスの終わりまで使う(閉じない)
  static HANDLE handles[2];
  handles[0] = event;
  handles[1] = reinterpret_cast<HANDLE>(const_cast<std::sig_atomic_t *>(flag));
  HANDLE thread = CreateThread(nullptr, 0, waitForQuitRequest, handles, 0, nullptr);
  if (thread) CloseHandle(thread);
}

bool requestProcessQuit(long long pid) {
  HANDLE event = OpenEventA(EVENT_MODIFY_STATE, FALSE, quitEventName(pid).c_str());
  if (!event) return false;
  bool sent = SetEvent(event) != 0;
  CloseHandle(event);
  return sent;
}

static std::string channelSlotPath(const std::string &name) { return "\\\\.\\mailslot\\" + name; }

void *openChannelSlot(const std::string &name) {
  HANDLE slot = CreateMailslotA(channelSlotPath(name).c_str(), 0, 0, nullptr);  // タイムアウト0: 待たない
  return slot == INVALID_HANDLE_VALUE ? nullptr : slot;
}

bool writeChannelSlot(const std::string &name, const std::string &message) {
  HANDLE slot = CreateFileA(channelSlotPath(name).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (slot == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  bool ok = WriteFile(slot, message.data(), static_cast<DWORD>(message.size()), &written, nullptr) &&
            written == message.size();
  CloseHandle(slot);
  return ok;
}

std::string readChannelSlot(void *slot) {
  std::string out;
  for (;;) {
    DWORD next = 0, count = 0;
    if (!GetMailslotInfo(static_cast<HANDLE>(slot), nullptr, &next, &count, nullptr) || next == MAILSLOT_NO_MESSAGE) break;
    std::string message(next, '\0');
    DWORD got = 0;
    if (!ReadFile(static_cast<HANDLE>(slot), message.data(), next, &got, nullptr)) break;
    out.append(message, 0, got);
  }
  return out;
}

void closeChannelSlot(void *slot) { CloseHandle(static_cast<HANDLE>(slot)); }

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

int poll(struct pollfd *fds, unsigned long count, int timeout) {
  int ready = 0;
  for (unsigned long i = 0; i < count; i++) {
    fds[i].revents = 0;
    if (fds[i].fd != 0 || !fm::consoleRawInputActive()) continue;
    if (fm::consoleInputReady(timeout)) {
      fds[i].revents = POLLIN;
      ready++;
    }
  }
  return ready;
}
