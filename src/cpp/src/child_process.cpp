#include "child_process.hpp"

#include <csignal>
#include <cstdlib>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fm {

namespace {

std::vector<char *> toArgv(const std::vector<std::string> &args) {
  std::vector<char *> argv;
  argv.reserve(args.size() + 1);
  for (const auto &a : args) argv.push_back(const_cast<char *>(a.c_str()));
  argv.push_back(nullptr);
  return argv;
}

// forkされた子プロセスは全て、まずシグナルの処理方針を既定値へ
// リセットする。これにより、親(tcb自身)がインストールしたどんな
// ハンドラ方針も継承しない――load_signal()からの移植で、fork.cの
// 全ての子プロセス分岐の先頭で呼ばれる。
void resetSignalsToDefault() {
  for (int i = 1; i < NSIG; i++) std::signal(i, SIG_DFL);
}

// dup_error()からの移植: 子プロセスの標準エラーを、親のものを継承
// する代わりにログファイルへ追記する。
void redirectStderr(const std::string &path) {
  int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
  if (fd < 0) return;
  dup2(fd, 2);
  close(fd);
}

// run()とrunPipeline()の第2段階が共有する子プロセス側の準備処理:
// シグナルをリセットし、必要ならLC_ALL=Cを強制し、必要なら標準
// エラーをリダイレクトし、それからoutFdを標準出力へ(inFdが与え
// られていれば標準入力へも)つなぐ。
void prepareChild(int outFd, int inFd, const ChildProcess::Options &options) {
  resetSignalsToDefault();
  if (options.forceCLocale) setenv("LC_ALL", "C", 1);
  if (options.stderrLogPath) redirectStderr(*options.stderrLogPath);
  if (inFd >= 0) dup2(inFd, 0);
  dup2(outFd, 1);
}

std::string readAll(int fd) {
  std::string out;
  char buf[4096];
  ssize_t n;
  while ((n = read(fd, buf, sizeof(buf))) > 0) out.append(buf, n);
  return out;
}

// `input`を`fd`へ書き込んで終了する、小さな専用プロセスをforkする
// ――これから子プロセスの標準出力を読むためにブロックしようとして
// いるのと同じプロセスから書き込むと、入力がパイプバッファより
// 大きくなった時点でデッドロックの危険がある(子プロセスは、まだ
// 誰もその標準出力を読んでいなければ自身の標準入力を排出できず、
// 親は、標準出力を読み始める前にパイプが保持できる以上を書こうと
// 待っていると標準入力の書き込みを終えられない)。legacy自身の
// fork_arc()/fork_chp()も、パイプラインの書き込み側専用の追加の
// fork()で、同じようにこれを回避している。
pid_t spawnWriter(int fd, const std::string &input) {
  pid_t pid = fork();
  if (pid == 0) {
    resetSignalsToDefault();
    std::size_t off = 0;
    while (off < input.size()) {
      ssize_t n = write(fd, input.data() + off, input.size() - off);
      if (n <= 0) break;
      off += static_cast<std::size_t>(n);
    }
    close(fd);
    _exit(0);
  }
  return pid;
}

int reapStatus(pid_t pid) {
  if (pid <= 0) return -1;
  int status = 0;
  if (waitpid(pid, &status, 0) < 0) return -1;
  return (WIFEXITED(status)) ? WEXITSTATUS(status) : -1;
}

}  // namespace

ChildProcess::Result ChildProcess::run(const std::vector<std::string> &argv,
                                        const Options &options) {
  Result result;
  if (argv.empty()) return result;

  int outPv[2];
  if (pipe(outPv) < 0) return result;

  int inPv[2] = {-1, -1};
  pid_t writerPid = -1;
  if (options.input) {
    if (pipe(inPv) < 0) { close(outPv[0]); close(outPv[1]); return result; }
  }

  pid_t childPid = fork();
  if (childPid == 0) {
    close(outPv[0]);
    if (options.input) close(inPv[1]);
    prepareChild(outPv[1], options.input ? inPv[0] : -1, options);
    close(outPv[1]);
    if (options.input) close(inPv[0]);
    auto cargv = toArgv(argv);
    execvp(cargv[0], cargv.data());
    _exit(127);  // execvp()失敗――そのまま処理を続けtcbとして動き続けることは絶対に無い
  }

  close(outPv[1]);
  if (options.input) {
    close(inPv[0]);
    writerPid = spawnWriter(inPv[1], *options.input);
    close(inPv[1]);
  }

  result.output = readAll(outPv[0]);
  close(outPv[0]);

  reapStatus(writerPid);
  result.exitStatus = reapStatus(childPid);
  result.ok = (result.exitStatus == 0);
  return result;
}

ChildProcess::Result ChildProcess::runPipeline(const std::vector<std::string> &argv1,
                                                const std::vector<std::string> &argv2,
                                                const Options &options) {
  Result result;
  if (argv1.empty() || argv2.empty()) return result;

  int outPv[2], midPv[2];
  if (pipe(outPv) < 0) return result;
  if (pipe(midPv) < 0) { close(outPv[0]); close(outPv[1]); return result; }

  int inPv[2] = {-1, -1};
  pid_t writerPid = -1;
  if (options.input) {
    if (pipe(inPv) < 0) {
      close(outPv[0]); close(outPv[1]); close(midPv[0]); close(midPv[1]);
      return result;
    }
  }

  // 第1段階: argv1、標準出力 -> midPv、標準入力 <- inPv(inputが与えられていれば)。
  pid_t pid1 = fork();
  if (pid1 == 0) {
    close(outPv[0]); close(outPv[1]);
    close(midPv[0]);
    if (options.input) close(inPv[1]);
    resetSignalsToDefault();
    if (options.forceCLocale) setenv("LC_ALL", "C", 1);
    if (options.input) dup2(inPv[0], 0);
    dup2(midPv[1], 1);
    close(midPv[1]);
    if (options.input) close(inPv[0]);
    auto cargv = toArgv(argv1);
    execvp(cargv[0], cargv.data());
    _exit(127);
  }
  close(midPv[1]);
  if (options.input) close(inPv[0]);

  // 第2段階: argv2、標準入力 <- midPv、標準出力 -> outPv。
  pid_t pid2 = fork();
  if (pid2 == 0) {
    prepareChild(outPv[1], midPv[0], options);
    close(outPv[1]);
    close(midPv[0]);
    // pid2は、親が自身のoutPv[0](「この同じ」出力パイプの読み込み側)
    // と、パイプ入力がある場合はinPv[1](入力パイプの書き込み側)を
    // 閉じ終える前にforkされている――どちらもこの時点では親でまだ
    // 開いたままであり、そのためfork()をまたいで継承されたコピーと
    // してpid2の中にも入り込んでしまう。下のexecをまたいで開いた
    // ままにしておくと、その迷子のinPv[1]コピーだけで、入力パイプの
    // 書き込み側の参照カウントを永遠にゼロより上に保つのに十分と
    // なる: すると第1段階(inPv[0]を自身の標準入力として読む)は
    // 決してEOFを見ず、他の全ての書き込み側(writerプロセス、
    // pid1自身、親)が自分のコピーを閉じた後でさえハングしてしまう。
    // 無関係だったOptions初期化のSIGSEGV(child_process.hpp参照)が
    // 修正され、このコード経路がついに最後まで実行できるようになった
    // 際、ハングしたテスト実行経由で発見された。
    close(outPv[0]);
    if (options.input) close(inPv[1]);
    auto cargv = toArgv(argv2);
    execvp(cargv[0], cargv.data());
    _exit(127);
  }
  close(midPv[0]);
  close(outPv[1]);

  if (options.input) {
    writerPid = spawnWriter(inPv[1], *options.input);
    close(inPv[1]);
  }

  result.output = readAll(outPv[0]);
  close(outPv[0]);

  reapStatus(writerPid);
  reapStatus(pid1);
  result.exitStatus = reapStatus(pid2);
  result.ok = (result.exitStatus == 0);
  return result;
}

int ChildProcess::runInteractive(const std::vector<std::string> &argv) {
  if (argv.empty()) return -1;

  pid_t pid = fork();
  if (pid < 0) return -1;

  if (pid == 0) {
    resetSignalsToDefault();
    auto cargv = toArgv(argv);
    execvp(cargv[0], cargv.data());
    _exit(127);  // execvp()失敗――そのまま処理を続けusilicとして動き続けることは絶対に無い
  }

  return reapStatus(pid);
}

pid_t ChildProcess::runDetached(const std::vector<std::string> &argv) {
  if (argv.empty()) return -1;

  pid_t pid = fork();
  if (pid < 0) return -1;

  if (pid == 0) {
    resetSignalsToDefault();
    int devNull = open("/dev/null", O_WRONLY);
    if (devNull >= 0) {
      dup2(devNull, 1);
      dup2(devNull, 2);
      close(devNull);
    }
    auto cargv = toArgv(argv);
    execvp(cargv[0], cargv.data());
    _exit(127);  // execvp()失敗――そのまま処理を続けusilicとして動き続けることは絶対に無い
  }

  return pid;
}

bool ChildProcess::hasExited(pid_t pid) {
  if (pid <= 0) return true;
  int status = 0;
  return waitpid(pid, &status, WNOHANG) != 0;  // >0: たった今回収した; -1(例えば既に回収済み): 完了扱いにする
}

}  // namespace fm
