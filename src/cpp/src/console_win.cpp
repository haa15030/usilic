// console_win.hpp(Windowsのネイティブのコンソール)の実装。
#include "console_win.hpp"

#include <fcntl.h>
#include <io.h>
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <deque>

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#ifndef DISABLE_NEWLINE_AUTO_RETURN
#define DISABLE_NEWLINE_AUTO_RETURN 0x0008
#endif
#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#endif

namespace fm {
namespace {

HANDLE g_in = INVALID_HANDLE_VALUE, g_out = INVALID_HANDLE_VALUE;
DWORD g_savedInMode = 0, g_savedOutMode = 0;
UINT g_savedInCp = 0, g_savedOutCp = 0;
bool g_outPrepared = false, g_inSaved = false, g_rawActive = false, g_resized = false;
std::deque<unsigned char> g_bytes;
wchar_t g_highSurrogate = 0;

// 標準入出力がパイプでも(`type x | usilic-c`)、キーと画面はコンソールそのものを相手にする。
HANDLE consoleHandle(const wchar_t *name) {
  return CreateFileW(name, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                     nullptr);
}

void pushUtf8(unsigned long cp) {
  if (cp < 0x80) {
    g_bytes.push_back(static_cast<unsigned char>(cp));
  } else if (cp < 0x800) {
    g_bytes.push_back(static_cast<unsigned char>(0xC0 | (cp >> 6)));
    g_bytes.push_back(static_cast<unsigned char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    g_bytes.push_back(static_cast<unsigned char>(0xE0 | (cp >> 12)));
    g_bytes.push_back(static_cast<unsigned char>(0x80 | ((cp >> 6) & 0x3F)));
    g_bytes.push_back(static_cast<unsigned char>(0x80 | (cp & 0x3F)));
  } else {
    g_bytes.push_back(static_cast<unsigned char>(0xF0 | (cp >> 18)));
    g_bytes.push_back(static_cast<unsigned char>(0x80 | ((cp >> 12) & 0x3F)));
    g_bytes.push_back(static_cast<unsigned char>(0x80 | ((cp >> 6) & 0x3F)));
    g_bytes.push_back(static_cast<unsigned char>(0x80 | (cp & 0x3F)));
  }
}

void pushKey(const KEY_EVENT_RECORD &key) {
  if (!key.bKeyDown) return;
  wchar_t ch = key.uChar.UnicodeChar;
  // ^SPC: VT入力でも字はNUL。KeyInput::readByte()が0x80(^SPCの印)にする。
  if (ch == 0) {
    bool ctrl = key.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED);
    if (ctrl && (key.wVirtualKeyCode == VK_SPACE || key.wVirtualKeyCode == '2')) g_bytes.push_back(0);
    return;
  }
  for (WORD i = 0; i < (key.wRepeatCount ? key.wRepeatCount : 1); i++) {
    if (ch >= 0xD800 && ch < 0xDC00) {
      g_highSurrogate = ch;
    } else if (ch >= 0xDC00 && ch < 0xE000) {
      if (g_highSurrogate)
        pushUtf8(0x10000 + ((static_cast<unsigned long>(g_highSurrogate) - 0xD800) << 10) +
                 (static_cast<unsigned long>(ch) - 0xDC00));
      g_highSurrogate = 0;
    } else {
      pushUtf8(ch);
    }
  }
}

// 届いているコンソールのイベントを全部読む。
void drainEvents() {
  if (g_in == INVALID_HANDLE_VALUE) return;
  INPUT_RECORD records[64];
  DWORD pending = 0;
  while (GetNumberOfConsoleInputEvents(g_in, &pending) && pending > 0) {
    DWORD got = 0;
    if (!ReadConsoleInputW(g_in, records, 64, &got) || got == 0) return;
    for (DWORD i = 0; i < got; i++) {
      if (records[i].EventType == KEY_EVENT) pushKey(records[i].Event.KeyEvent);
      else if (records[i].EventType == WINDOW_BUFFER_SIZE_EVENT) g_resized = true;
    }
  }
}

void restoreAtExit() {
  consoleLeaveRawInput();
  std::fflush(stdout);
  if (g_outPrepared && g_out != INVALID_HANDLE_VALUE) {
    SetConsoleMode(g_out, g_savedOutMode);
    SetConsoleOutputCP(g_savedOutCp);
    SetConsoleCP(g_savedInCp);
  }
}

}  // namespace

bool consoleAvailable() {
  HANDLE h = consoleHandle(L"CONOUT$");
  if (h == INVALID_HANDLE_VALUE) return false;
  DWORD mode = 0;
  bool ok = GetConsoleMode(h, &mode) != 0;
  CloseHandle(h);
  return ok;
}

bool consolePrepareOutput() {
  if (g_outPrepared) return true;
  g_out = consoleHandle(L"CONOUT$");
  if (g_out == INVALID_HANDLE_VALUE || !GetConsoleMode(g_out, &g_savedOutMode)) return false;
  // Windows 10 1809より前等、VTを受け付けないコンソールでは描けない
  if (!SetConsoleMode(g_out, g_savedOutMode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING |
                                 DISABLE_NEWLINE_AUTO_RETURN))
    return false;
  g_savedOutCp = GetConsoleOutputCP();
  g_savedInCp = GetConsoleCP();
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
  // 標準出力がリダイレクトされていても画面はコンソールへ。行の途中で切れないよう、flushまで溜める。
  DWORD ignored = 0;
  if (!GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &ignored)) std::freopen("CONOUT$", "w", stdout);
  _setmode(_fileno(stdout), _O_BINARY);
  std::setvbuf(stdout, nullptr, _IOFBF, 1 << 16);
  g_outPrepared = true;
  std::atexit(restoreAtExit);
  return true;
}

bool consoleEnterRawInput() {
  if (g_in == INVALID_HANDLE_VALUE) {
    g_in = consoleHandle(L"CONIN$");
    if (g_in == INVALID_HANDLE_VALUE) return false;
  }
  if (!g_inSaved) {
    if (!GetConsoleMode(g_in, &g_savedInMode)) return false;
    g_inSaved = true;
  }
  // ^C・^S等もバイトで受ける(PROCESSED無し)。QuickEdit(クリックで選択)を外すとマウスの報告が届く。
  if (!SetConsoleMode(g_in, ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS)) return false;
  g_rawActive = true;
  return true;
}

void consoleLeaveRawInput() {
  if (g_inSaved && g_in != INVALID_HANDLE_VALUE) SetConsoleMode(g_in, g_savedInMode);
  g_rawActive = false;
}

bool consoleRawInputActive() { return g_rawActive; }

bool consoleWindowSize(int *rows, int *columns) {
  HANDLE h = g_out != INVALID_HANDLE_VALUE ? g_out : consoleHandle(L"CONOUT$");
  CONSOLE_SCREEN_BUFFER_INFO info;
  bool ok = h != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(h, &info);
  if (h != g_out && h != INVALID_HANDLE_VALUE) CloseHandle(h);
  if (!ok) return false;
  *rows = info.srWindow.Bottom - info.srWindow.Top + 1;
  *columns = info.srWindow.Right - info.srWindow.Left + 1;
  return *rows > 0 && *columns > 0;
}

bool consoleInputReady(int timeoutMs) {
  if (!g_rawActive) return false;
  drainEvents();
  if (!g_bytes.empty()) return true;
  if (timeoutMs <= 0) return false;
  if (WaitForSingleObject(g_in, static_cast<DWORD>(timeoutMs)) != WAIT_OBJECT_0) return false;
  drainEvents();
  return !g_bytes.empty();
}

int consoleReadByte(int timeoutMs) {
  if (!consoleInputReady(timeoutMs)) return -1;
  int byte = g_bytes.front();
  g_bytes.pop_front();
  return byte;
}

bool consoleTakeResize() {
  drainEvents();
  bool resized = g_resized;
  g_resized = false;
  return resized;
}

}  // namespace fm
