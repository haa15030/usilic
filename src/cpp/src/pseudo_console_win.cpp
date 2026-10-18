// pseudo_console_win.hpp(WindowsのネイティブのSHELLの擬似端末、ConPTY)の実装。
// ConPTYは、シェルが終わっても出力のパイプを閉じない(ClosePseudoConsoleを呼ぶまで)。しかもClosePseudoConsoleは
// (Windowsの版によっては)出力が読み尽くされるまで戻らない。そこで出力は読み取り用のスレッドが読み続けて
// バッファへ溜め、もう1つのスレッドがシェルの終わりを待ってClosePseudoConsoleを呼ぶ。出力のパイプの終わりを
// 読み取りのスレッドが見たら、それが「シェルが終わった」(readPseudoConsole()の-1)になる。
#include "pseudo_console_win.hpp"

#include <windows.h>

#include <chrono>
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace fm {

namespace {

std::wstring widen(const std::string &text) {
  if (text.empty()) return L"";
  int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring out(static_cast<std::size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n);
  return out;
}

std::string narrow(const std::wstring &text) {
  if (text.empty()) return "";
  int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
  std::string out(static_cast<std::size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n, nullptr, nullptr);
  return out;
}

// child_process_win.cppのquoteArgument()と同じ(CommandLineToArgvWの規則)。
std::wstring quoteArgument(const std::wstring &arg) {
  if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos) return arg;
  std::wstring out = L"\"";
  std::size_t backslashes = 0;
  for (wchar_t c : arg) {
    if (c == L'\\') {
      backslashes++;
      continue;
    }
    if (c == L'"') out.append(backslashes * 2 + 1, L'\\');
    else out.append(backslashes, L'\\');
    backslashes = 0;
    out += c;
  }
  out.append(backslashes * 2, L'\\');
  out += L'"';
  return out;
}

void closeHandle(HANDLE &h) {
  if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h);
  h = nullptr;
}

// 子を作る間だけ親の環境に足す(子は親の環境を受け継ぐ)。child_process_win.cppのScopedLocaleEnvironmentと同じ。
class ScopedEnvironment {
 public:
  explicit ScopedEnvironment(const std::vector<std::pair<std::string, std::string>> &env) {
    for (const auto &[name, value] : env) {
      std::wstring wname = widen(name);
      wchar_t buf[1024];
      DWORD n = GetEnvironmentVariableW(wname.c_str(), buf, 1024);
      saved_.push_back({wname, n > 0 && n < 1024, n > 0 && n < 1024 ? std::wstring(buf) : std::wstring()});
      SetEnvironmentVariableW(wname.c_str(), widen(value).c_str());
    }
  }
  ~ScopedEnvironment() {
    for (auto it = saved_.rbegin(); it != saved_.rend(); ++it)
      SetEnvironmentVariableW(it->name.c_str(), it->had ? it->value.c_str() : nullptr);
  }

 private:
  struct Saved {
    std::wstring name;
    bool had;
    std::wstring value;
  };
  std::vector<Saved> saved_;
};

COORD consoleSize(int rows, int columns) {
  COORD size;
  size.X = static_cast<SHORT>(columns < 1 ? 80 : columns > 32767 ? 32767 : columns);
  size.Y = static_cast<SHORT>(rows < 1 ? 24 : rows > 32767 ? 32767 : rows);
  return size;
}

}  // namespace

struct PseudoConsole {
  HPCON console = nullptr;
  HANDLE input = nullptr;   // シェルへの入力(書く側)
  HANDLE output = nullptr;  // シェルからの出力(読む側)
  HANDLE process = nullptr;
  std::thread reader;
  std::thread closer;
  std::mutex mutex;         // buffer・ended
  std::mutex consoleMutex;  // console・consoleClosed(読み取りのスレッドは取らない、ClosePseudoConsoleが待つので)
  std::condition_variable arrived;
  std::string buffer;  // 読み取りのスレッドが溜めた、まだ渡していない出力
  bool ended = false;  // 出力のパイプが終わった
  bool consoleClosed = false;
};

PseudoConsole *startPseudoConsole(const std::vector<std::string> &argv, int rows, int columns,
                                  const std::string &directory,
                                  const std::vector<std::pair<std::string, std::string>> &env, int &pidOut) {
  if (argv.empty()) return nullptr;
  HANDLE inRead = nullptr, inWrite = nullptr, outRead = nullptr, outWrite = nullptr;
  if (!CreatePipe(&inRead, &inWrite, nullptr, 0)) return nullptr;
  if (!CreatePipe(&outRead, &outWrite, nullptr, 0)) {
    closeHandle(inRead);
    closeHandle(inWrite);
    return nullptr;
  }
  HPCON hpc = nullptr;
  HRESULT hr = CreatePseudoConsole(consoleSize(rows, columns), inRead, outWrite, 0, &hpc);
  // ConPTYが自分の写しを持つので、こちらの側の端はすぐ閉じる(閉じないと、出力のパイプの終わりが来ない)。
  closeHandle(inRead);
  closeHandle(outWrite);
  if (FAILED(hr)) {
    closeHandle(inWrite);
    closeHandle(outRead);
    return nullptr;
  }

  SIZE_T attrSize = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
  std::vector<unsigned char> attrBuf(attrSize);
  auto *attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
  bool ok = InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize) &&
            UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, hpc, sizeof hpc, nullptr,
                                      nullptr);
  PROCESS_INFORMATION pi{};
  if (ok) {
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof si;
    si.lpAttributeList = attrs;
    // 2026-10-18、直接のユーザー報告("git bashはSHELLが起動しない。ネイティブはOK（cmd）"): Git Bash(mintty)から
    // 起動するとusilicの標準入出力はパイプで、指定しないと子はそれを引き継いでConPTYへ出力しない。標準入出力を
    // 無効と明示すると、子はConPTYのものを使う(WezTerm等も同じ)。
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = INVALID_HANDLE_VALUE;
    si.StartupInfo.hStdOutput = INVALID_HANDLE_VALUE;
    si.StartupInfo.hStdError = INVALID_HANDLE_VALUE;
    std::wstring line;
    for (const auto &arg : argv) {
      if (!line.empty()) line += L' ';
      line += quoteArgument(widen(arg));
    }
    std::wstring dir = widen(directory);
    for (wchar_t &c : dir)
      if (c == L'/') c = L'\\';
    ScopedEnvironment scoped(env);
    ok = CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE,
                        EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT, nullptr,
                        dir.empty() ? nullptr : dir.c_str(), &si.StartupInfo, &pi);
    DeleteProcThreadAttributeList(attrs);
  }
  if (!ok) {
    ClosePseudoConsole(hpc);
    closeHandle(inWrite);
    closeHandle(outRead);
    return nullptr;
  }
  CloseHandle(pi.hThread);

  auto *pc = new PseudoConsole;
  pc->console = hpc;
  pc->input = inWrite;
  pc->output = outRead;
  pc->process = pi.hProcess;
  pidOut = static_cast<int>(pi.dwProcessId);
  pc->reader = std::thread([pc] {
    char buf[4096];
    DWORD n = 0;
    while (ReadFile(pc->output, buf, sizeof buf, &n, nullptr) && n > 0) {
      std::lock_guard<std::mutex> lock(pc->mutex);
      pc->buffer.append(buf, n);
      pc->arrived.notify_all();
    }
    std::lock_guard<std::mutex> lock(pc->mutex);
    pc->ended = true;
    pc->arrived.notify_all();
  });
  pc->closer = std::thread([pc] {
    WaitForSingleObject(pc->process, INFINITE);
    // シェルが終わった: ConPTYを閉じると、残りの出力を出し切ってから出力のパイプが終わる。
    std::lock_guard<std::mutex> lock(pc->consoleMutex);
    if (!pc->consoleClosed) {
      pc->consoleClosed = true;
      ClosePseudoConsole(pc->console);
    }
  });
  return pc;
}

int readPseudoConsole(PseudoConsole *pc, char *buf, std::size_t size) {
  if (!pc) return -1;
  std::lock_guard<std::mutex> lock(pc->mutex);
  if (pc->buffer.empty()) return pc->ended ? -1 : 0;
  std::size_t n = std::min(size, pc->buffer.size());
  pc->buffer.copy(buf, n);
  pc->buffer.erase(0, n);
  return static_cast<int>(n);
}

bool waitPseudoConsole(PseudoConsole *pc, int timeoutMs) {
  if (!pc) return false;
  std::unique_lock<std::mutex> lock(pc->mutex);
  return pc->arrived.wait_for(lock, std::chrono::milliseconds(timeoutMs < 0 ? 0 : timeoutMs),
                              [pc] { return !pc->buffer.empty() || pc->ended; });
}

bool writePseudoConsole(PseudoConsole *pc, const std::string &bytes) {
  if (!pc || !pc->input) return false;
  std::size_t off = 0;
  while (off < bytes.size()) {
    DWORD n = 0;
    if (!WriteFile(pc->input, bytes.data() + off, static_cast<DWORD>(bytes.size() - off), &n, nullptr) || n == 0)
      return false;
    off += n;
  }
  return true;
}

void resizePseudoConsole(PseudoConsole *pc, int rows, int columns) {
  if (!pc) return;
  std::lock_guard<std::mutex> lock(pc->consoleMutex);
  if (!pc->consoleClosed) ResizePseudoConsole(pc->console, consoleSize(rows, columns));
}

void closePseudoConsole(PseudoConsole *pc) {
  if (!pc) return;
  if (WaitForSingleObject(pc->process, 0) == WAIT_TIMEOUT) TerminateProcess(pc->process, 1);
  if (pc->closer.joinable()) pc->closer.join();  // ConPTYを閉じる(シェルが終わるのを待って)
  if (pc->reader.joinable()) pc->reader.join();  // 出力のパイプが終わるまで
  closeHandle(pc->input);
  closeHandle(pc->output);
  closeHandle(pc->process);
  delete pc;
}

std::string findWindowsProgram(const std::string &program) {
  wchar_t found[MAX_PATH];
  DWORD n = SearchPathW(nullptr, widen(program).c_str(), L".exe", MAX_PATH, found, nullptr);
  if (n == 0 || n >= MAX_PATH) return "";
  std::string path = narrow(std::wstring(found, n));
  for (char &c : path)
    if (c == '\\') c = '/';
  return path;
}

}  // namespace fm
