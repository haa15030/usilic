// Windowsのネイティブ(MinGW-w64、windows.mk、.steering/20261015-windows-native-gui/)のChildProcess。
// POSIX版(child_process.cpp)のfork/execvp/パイプ/waitpidを、CreateProcessW・匿名パイプ・
// WaitForSingleObjectで置き換える。引数はWindowsのコマンドラインの規則(CommandLineToArgvWが戻せる形)で
// クォートしてUTF-16にする。標準入力への書き込みは別のスレッド(POSIX版の書き込み用の子プロセスと同じく、
// 大きな入力で満杯の出力パイプとデッドロックしないため)。pid_tはWindowsのプロセスIDで、プロセスの
// ハンドルは表(g_processes)で持つ。`ls`はPATHにls.exeが無ければnativeLsAlF()で答える。
#include "console_win.hpp"
#include "child_process.hpp"

#include <fcntl.h>
#include <io.h>
#include <windows.h>

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include "native_file.hpp"
#include "native_grep.hpp"
#include "native_ls.hpp"
#include "native_unzip.hpp"

namespace fm {

namespace {

std::wstring widen(const std::string &text) {
  if (text.empty()) return L"";
  int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring out(static_cast<std::size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n);
  return out;
}

// CommandLineToArgvW(MSVCの実行時ライブラリ)の規則: 空白・タブ・"を含む(か空の)引数は"で囲み、"の前と
// 末尾の\は2倍にする。
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

std::wstring commandLine(const std::vector<std::string> &argv) {
  std::wstring line;
  for (const auto &arg : argv) {
    if (!line.empty()) line += L' ';
    line += quoteArgument(widen(arg));
  }
  return line;
}

bool onPath(const std::string &program) {
  wchar_t found[MAX_PATH];
  return SearchPathW(nullptr, widen(program).c_str(), L".exe", MAX_PATH, found, nullptr) > 0;
}

// 子に渡す(継承する)ハンドル。
SECURITY_ATTRIBUTES inheritable() {
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof sa;
  sa.bInheritHandle = TRUE;
  return sa;
}

// 匿名パイプ。childEndの側だけを子へ継承させる(readForChild: 子が読む側=標準入力)。
bool makePipe(HANDLE &read, HANDLE &write, bool readForChild) {
  SECURITY_ATTRIBUTES sa = inheritable();
  if (!CreatePipe(&read, &write, &sa, 0)) return false;
  SetHandleInformation(readForChild ? write : read, HANDLE_FLAG_INHERIT, 0);
  return true;
}

HANDLE openNul(bool forWrite) {
  SECURITY_ATTRIBUTES sa = inheritable();
  return CreateFileW(L"NUL", forWrite ? GENERIC_WRITE : GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                     OPEN_EXISTING, 0, nullptr);
}

HANDLE openStderr(const ChildProcess::Options &options) {
  if (!options.stderrLogPath) return openNul(true);
  SECURITY_ATTRIBUTES sa = inheritable();
  HANDLE h = CreateFileW(widen(*options.stderrLogPath).c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                         &sa, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  return h == INVALID_HANDLE_VALUE ? openNul(true) : h;
}

void closeHandle(HANDLE &h) {
  if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h);
  h = nullptr;
}

// POSIX版のprepareChild()のLC_ALL等(Windowsのプログラムの多くは読まないが、MSYS2等のものは読む)。
// 子は親の環境を受け継ぐので、作る間だけ親の環境を変えて戻す。
class ScopedLocaleEnvironment {
 public:
  explicit ScopedLocaleEnvironment(const ChildProcess::Options &options) {
    if (options.forceCLocale) set(L"LC_ALL", L"C");
    if (options.forceUtf8CTypeForListing) {
      set(L"LC_ALL", nullptr);
      set(L"LC_TIME", L"C");
      set(L"LC_CTYPE", L"en_US.UTF-8");
    }
  }
  ~ScopedLocaleEnvironment() {
    for (auto it = saved_.rbegin(); it != saved_.rend(); ++it)
      SetEnvironmentVariableW(it->first.c_str(), it->second ? it->second->c_str() : nullptr);
  }

 private:
  void set(const wchar_t *name, const wchar_t *value) {
    wchar_t buf[1024];
    DWORD n = GetEnvironmentVariableW(name, buf, 1024);
    saved_.push_back({name, n > 0 && n < 1024 ? std::optional<std::wstring>(buf) : std::nullopt});
    SetEnvironmentVariableW(name, value);
  }
  std::vector<std::pair<std::wstring, std::optional<std::wstring>>> saved_;
};

// 子を作る。hIn/hOut/hErrは継承できるハンドル(nullptrなら親のものを使わずNULにする)。
bool launch(const std::vector<std::string> &argv, HANDLE hIn, HANDLE hOut, HANDLE hErr, DWORD flags,
            PROCESS_INFORMATION &pi, bool useStdHandles = true) {
  std::wstring line = commandLine(argv);
  STARTUPINFOW si{};
  si.cb = sizeof si;
  if (useStdHandles) {
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = hIn;
    si.hStdOutput = hOut;
    si.hStdError = hErr;
  }
  return CreateProcessW(nullptr, line.data(), nullptr, nullptr, useStdHandles ? TRUE : FALSE, flags | CREATE_UNICODE_ENVIRONMENT,
                        nullptr, nullptr, &si, &pi) != 0;
}

int waitExitCode(HANDLE process) {
  WaitForSingleObject(process, INFINITE);
  DWORD code = 0;
  if (!GetExitCodeProcess(process, &code)) return -1;
  return static_cast<int>(code);
}

// 出力を読み切る(maxBytesを超えたら最後の改行で切ってtruncated)。
std::string readAllLimited(HANDLE h, std::size_t maxBytes, bool *truncated) {
  std::string out;
  char buf[4096];
  DWORD n = 0;
  while (ReadFile(h, buf, sizeof buf, &n, nullptr) && n > 0) {
    out.append(buf, n);
    if (maxBytes && out.size() > maxBytes) {
      *truncated = true;
      std::size_t newline = out.rfind('\n', maxBytes);
      out.resize(newline == std::string::npos ? maxBytes : newline + 1);
      break;
    }
  }
  return out;
}

std::thread startWriter(HANDLE &write, const std::string &input) {
  HANDLE h = write;
  write = nullptr;  // スレッドが閉じる
  return std::thread([h, &input] {
    std::size_t off = 0;
    while (off < input.size()) {
      DWORD n = 0;
      DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(input.size() - off, 1 << 16));
      if (!WriteFile(h, input.data() + off, chunk, &n, nullptr) || n == 0) break;
      off += n;
    }
    CloseHandle(h);
  });
}

// spawnInteractive()・runDetached()・startCapturing()の子のハンドル(hasExited()・terminate()が使う)。
std::mutex g_mutex;
std::map<pid_t, HANDLE> g_processes;

pid_t remember(PROCESS_INFORMATION &pi) {
  CloseHandle(pi.hThread);
  std::lock_guard<std::mutex> lock(g_mutex);
  g_processes[static_cast<pid_t>(pi.dwProcessId)] = pi.hProcess;
  return static_cast<pid_t>(pi.dwProcessId);
}

HANDLE takeHandle(pid_t pid) {
  std::lock_guard<std::mutex> lock(g_mutex);
  auto it = g_processes.find(pid);
  if (it == g_processes.end()) return nullptr;
  HANDLE h = it->second;
  g_processes.erase(it);
  return h;
}

}  // namespace

ChildProcess::Result ChildProcess::run(const std::vector<std::string> &argv, const Options &options) {
  Result result;
  if (argv.empty()) return result;
  if (argv[0] == "ls" && !onPath("ls")) {
    bool ok = false;
    result.output = nativeLsAlF(std::vector<std::string>(argv.begin() + 1, argv.end()), &ok);
    result.ok = ok;
    result.exitStatus = ok ? 0 : 2;
    return result;
  }
  // grep・egrep・fgrep(CUT/PART・KEY・EPUBの章分け・FGREP/EGREP)も、PATHに無ければ内蔵のもので
  // (2026-10-15、直接のユーザー報告"^K不能"、native_grep.hpp参照)。
  if ((argv[0] == "grep" || argv[0] == "egrep" || argv[0] == "fgrep") && !onPath(argv[0])) {
    std::vector<std::string> args(argv.begin() + 1, argv.end());
    if (argv[0] != "grep") args.insert(args.begin(), argv[0] == "egrep" ? "-E" : "-F");
    result.exitStatus = nativeGrep(args, options.input ? *options.input : std::string(), result.output);
    if (options.maxOutputBytes && result.output.size() > options.maxOutputBytes) {
      std::size_t newline = result.output.rfind('\n', options.maxOutputBytes);
      result.output.resize(newline == std::string::npos ? options.maxOutputBytes : newline + 1);
      result.truncated = true;
    }
    result.ok = result.exitStatus == 0 || result.truncated;
    return result;
  }
  // unzip(EPUBの中身・ARCの一覧と展開)も、PATHに無ければ内蔵のもので(native_unzip.hpp参照)。
  if (argv[0] == "unzip" && !onPath("unzip")) {
    result.exitStatus = nativeUnzip(std::vector<std::string>(argv.begin() + 1, argv.end()), result.output);
    result.ok = result.exitStatus == 0;
    return result;
  }
  // file(バイナリのファイルの「(^X) 種類」・クイックルックの判定)も、PATHに無ければ内蔵のもので(native_file.hpp参照)。
  if (argv[0] == "file" && !onPath("file")) {
    result.exitStatus = nativeFile(std::vector<std::string>(argv.begin() + 1, argv.end()), result.output);
    result.ok = result.exitStatus == 0;
    return result;
  }
  HANDLE outRead = nullptr, outWrite = nullptr, inRead = nullptr, inWrite = nullptr;
  if (!makePipe(outRead, outWrite, false)) return result;
  if (options.input) {
    if (!makePipe(inRead, inWrite, true)) {
      closeHandle(outRead);
      closeHandle(outWrite);
      return result;
    }
  } else {
    inRead = openNul(false);
  }
  HANDLE errHandle = openStderr(options);
  PROCESS_INFORMATION pi{};
  bool started;
  {
    ScopedLocaleEnvironment env(options);
    started = launch(argv, inRead, outWrite, errHandle, CREATE_NO_WINDOW, pi);
  }
  closeHandle(outWrite);
  closeHandle(inRead);
  closeHandle(errHandle);
  if (!started) {
    closeHandle(outRead);
    closeHandle(inWrite);
    return result;
  }
  CloseHandle(pi.hThread);
  std::thread writer;
  if (options.input) writer = startWriter(inWrite, *options.input);
  result.output = readAllLimited(outRead, options.maxOutputBytes, &result.truncated);
  if (result.truncated) TerminateProcess(pi.hProcess, 1);  // ChildProcessOptions::maxOutputBytes参照
  closeHandle(outRead);
  if (writer.joinable()) writer.join();
  result.exitStatus = waitExitCode(pi.hProcess);
  CloseHandle(pi.hProcess);
  result.ok = result.exitStatus == 0 || result.truncated;
  return result;
}

ChildProcess::Result ChildProcess::runPipeline(const std::vector<std::string> &argv1,
                                               const std::vector<std::string> &argv2, const Options &options) {
  Result result;
  if (argv1.empty() || argv2.empty()) return result;
  HANDLE outRead = nullptr, outWrite = nullptr, midRead = nullptr, midWrite = nullptr, inRead = nullptr,
         inWrite = nullptr;
  if (!makePipe(outRead, outWrite, false)) return result;
  // 真ん中のパイプは両端とも子へ渡す(1つ目の子が書き、2つ目の子が読む)。
  SECURITY_ATTRIBUTES sa = inheritable();
  if (!CreatePipe(&midRead, &midWrite, &sa, 0)) {
    closeHandle(outRead);
    closeHandle(outWrite);
    return result;
  }
  if (options.input) makePipe(inRead, inWrite, true);
  else inRead = openNul(false);
  HANDLE err1 = openStderr(options), err2 = openStderr(options);
  PROCESS_INFORMATION pi1{}, pi2{};
  bool started1, started2;
  {
    ScopedLocaleEnvironment env(options);
    started1 = launch(argv1, inRead, midWrite, err1, CREATE_NO_WINDOW, pi1);
    started2 = launch(argv2, midRead, outWrite, err2, CREATE_NO_WINDOW, pi2);
  }
  closeHandle(midRead);
  closeHandle(midWrite);
  closeHandle(outWrite);
  closeHandle(inRead);
  closeHandle(err1);
  closeHandle(err2);
  std::thread writer;
  if (started1 && options.input && inWrite) writer = startWriter(inWrite, *options.input);
  closeHandle(inWrite);
  if (started2) result.output = readAllLimited(outRead, options.maxOutputBytes, &result.truncated);
  closeHandle(outRead);
  if (writer.joinable()) writer.join();
  if (started1) {
    if (result.truncated) TerminateProcess(pi1.hProcess, 1);
    waitExitCode(pi1.hProcess);
    CloseHandle(pi1.hProcess);
    CloseHandle(pi1.hThread);
  }
  if (started2) {
    if (result.truncated) TerminateProcess(pi2.hProcess, 1);
    result.exitStatus = waitExitCode(pi2.hProcess);
    CloseHandle(pi2.hProcess);
    CloseHandle(pi2.hThread);
  }
  result.ok = started1 && started2 && (result.exitStatus == 0 || result.truncated);
  return result;
}

// 端末を継承する対話的なプログラム(less・vi等)。-gのusilicは端末を持たないので、新しいコンソールの窓で動かして
// 終わるのを待つ。CUI(usilic-c.exe)ではそのコンソールで動かす。
int ChildProcess::runInteractive(const std::vector<std::string> &argv) {
  if (argv.empty()) return -1;
  PROCESS_INFORMATION pi{};
  if (!launch(argv, nullptr, nullptr, nullptr, consoleAvailable() ? 0 : CREATE_NEW_CONSOLE, pi, /*useStdHandles=*/false))
    return -1;
  CloseHandle(pi.hThread);
  int code = waitExitCode(pi.hProcess);
  CloseHandle(pi.hProcess);
  return code;
}

pid_t ChildProcess::spawnInteractive(const std::vector<std::string> &argv) {
  if (argv.empty()) return -1;
  PROCESS_INFORMATION pi{};
  if (!launch(argv, nullptr, nullptr, nullptr, consoleAvailable() ? 0 : CREATE_NEW_CONSOLE, pi, /*useStdHandles=*/false))
    return -1;
  return remember(pi);
}

pid_t ChildProcess::runDetached(const std::vector<std::string> &argv) {
  if (argv.empty()) return -1;
  HANDLE in = openNul(false), out = openNul(true), err = openNul(true);
  PROCESS_INFORMATION pi{};
  bool ok = launch(argv, in, out, err, CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP, pi);
  closeHandle(in);
  closeHandle(out);
  closeHandle(err);
  return ok ? remember(pi) : -1;
}

pid_t ChildProcess::runInNewConsole(const std::vector<std::string> &argv) {
  if (argv.empty()) return -1;
  PROCESS_INFORMATION pi{};
  // 標準入出力は渡さない(新しいコンソールのものになる。NULを渡すと子はパイプからの起動だと思う)。
  bool ok = launch(argv, nullptr, nullptr, nullptr, CREATE_NEW_CONSOLE | CREATE_NEW_PROCESS_GROUP, pi, /*useStdHandles=*/false);
  return ok ? remember(pi) : -1;
}

bool ChildProcess::hasExited(pid_t pid) {
  if (pid <= 0) return true;
  HANDLE h;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_processes.find(pid);
    if (it == g_processes.end()) return true;
    h = it->second;
  }
  if (WaitForSingleObject(h, 0) != WAIT_OBJECT_0) return false;
  if ((h = takeHandle(pid))) CloseHandle(h);
  return true;
}

namespace {

// startCapturing()で内蔵のもの(run()参照)を動かすスレッドに渡すもの。
struct BuiltinCapture {
  std::vector<std::string> argv;
  ChildProcess::Options options;
  HANDLE write;
};

DWORD WINAPI runBuiltinCapture(LPVOID param) {
  std::unique_ptr<BuiltinCapture> job(static_cast<BuiltinCapture *>(param));
  std::string output = ChildProcess::run(job->argv, job->options).output;
  std::size_t off = 0;
  while (off < output.size()) {
    DWORD n = 0;
    DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(output.size() - off, 1 << 16));
    if (!WriteFile(job->write, output.data() + off, chunk, &n, nullptr) || n == 0) break;  // 読む側が閉じた(^G)
    off += n;
  }
  CloseHandle(job->write);
  return 0;
}

}  // namespace

ChildProcess::AsyncCapture ChildProcess::startCapturing(const std::vector<std::string> &argv, const Options &options) {
  AsyncCapture capture;
  if (argv.empty()) return capture;
  // 2026-10-16、直接のユーザー報告(Windows 11、".zipは未実装？"): (ls)の^MのARCの一覧(Application::
  // startArchiveLoadAsync())はここを通るので、run()が内蔵のもの(ls・grep・unzip・file)で済ませる時も、
  // 別のスレッドでrun()を動かしてその出力をパイプへ書く。以前はunzip.exeを起動しようとして失敗し、zipを
  // 普通のファイルとして開いていた。スレッドのハンドルはプロセスと同じく待てるので、hasExited()はそのまま
  // 使える(スレッドIDはプロセスIDと同じ番号の空間から振られるので、生きている間は重ならない)。
  bool builtin = argv[0] == "ls" || argv[0] == "grep" || argv[0] == "egrep" || argv[0] == "fgrep" ||
                 argv[0] == "unzip" || argv[0] == "file";
  if (builtin && !onPath(argv[0])) {
    HANDLE outRead = nullptr, outWrite = nullptr;
    if (!makePipe(outRead, outWrite, false)) return capture;
    SetHandleInformation(outWrite, HANDLE_FLAG_INHERIT, 0);  // 同じ頃に起動する子に渡さない(閉じられなくなる)
    auto *job = new BuiltinCapture{argv, options, outWrite};
    job->options.input = nullptr;
    DWORD threadId = 0;
    HANDLE thread = CreateThread(nullptr, 0, runBuiltinCapture, job, 0, &threadId);
    if (!thread) {
      delete job;
      closeHandle(outWrite);
      closeHandle(outRead);
      return capture;
    }
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      g_processes[static_cast<pid_t>(threadId)] = thread;
    }
    capture.pid = static_cast<pid_t>(threadId);
    capture.fd = _open_osfhandle(reinterpret_cast<intptr_t>(outRead), _O_RDONLY | _O_BINARY);
    return capture;
  }
  HANDLE outRead = nullptr, outWrite = nullptr;
  if (!makePipe(outRead, outWrite, false)) return capture;
  HANDLE in = openNul(false), err = openStderr(options);
  PROCESS_INFORMATION pi{};
  bool ok;
  {
    ScopedLocaleEnvironment env(options);
    ok = launch(argv, in, outWrite, err, CREATE_NO_WINDOW, pi);
  }
  closeHandle(outWrite);
  closeHandle(in);
  closeHandle(err);
  if (!ok) {
    closeHandle(outRead);
    return capture;
  }
  capture.pid = remember(pi);
  capture.fd = _open_osfhandle(reinterpret_cast<intptr_t>(outRead), _O_RDONLY | _O_BINARY);
  return capture;
}

// 今読める分だけ(パイプに溜まっている分をPeekNamedPipeで確かめて読む。待たない)。
std::string ChildProcess::readAvailable(int fd) {
  std::string out;
  if (fd < 0) return out;
  HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
  char buf[4096];
  for (;;) {
    DWORD available = 0;
    if (!PeekNamedPipe(h, nullptr, 0, nullptr, &available, nullptr) || available == 0) break;
    int n = _read(fd, buf, static_cast<unsigned>(std::min<DWORD>(available, sizeof buf)));
    if (n <= 0) break;
    out.append(buf, static_cast<std::size_t>(n));
  }
  return out;
}

void ChildProcess::terminate(pid_t pid) {
  if (pid <= 0) return;
  HANDLE h = takeHandle(pid);
  if (!h) return;
  TerminateProcess(h, 1);
  WaitForSingleObject(h, INFINITE);
  CloseHandle(h);
}

}  // namespace fm
