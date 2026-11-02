#pragma once

// Windowsのネイティブ(MinGW-w64、_WIN32、.steering/20261015-windows-native-gui/)に無いPOSIXの小さな関数の
// 代わり。Windows以外では何もしない(このヘッダを読んでもPOSIXのものがそのまま使われる)。fork・擬似端末・
// ソケット等の大きなものは、使う所で_WIN32の分岐にする(ここでは真似しない)。
#ifdef _WIN32

#include <sys/stat.h>
#include <sys/types.h>

#include <csignal>
#include <cstdlib>
#include <string>

// setenv/unsetenv: Windowsは_putenv_s(値が空なら消える)。
inline int setenv(const char *name, const char *value, int overwrite) {
  if (!overwrite && std::getenv(name)) return 0;
  return _putenv_s(name, value);
}
inline int unsetenv(const char *name) { return _putenv_s(name, ""); }

// 所有者の考えが無いので、root(0)ではない固定の利用者番号。
inline short getuid() { return 1000; }  // MinGWのstat::st_uidはshort

// シンボリックリンクは扱わない(リンクも実体として見る)。
inline int lstat(const char *path, struct stat *st) { return stat(path, st); }
#ifndef S_ISLNK
#define S_ISLNK(mode) 0
#endif

// kill: sigが0ならそのプロセスが生きているか(0)、それ以外なら終わらせる(TerminateProcess)。
// 実装はposix_compat_win.cpp(windows.hをこのヘッダへ持ち込まないため)。
int kill(long long pid, int sig);
// Windowsにはプロセスへ送れるSIGTERMが無いので、「終わってほしい」は名前付きイベントで伝える(2026-11-02)。
// listenForQuitRequest(): 自分のPIDのイベントを作り、合図が来たら*flagを1にする(SIGTERMのハンドラと同じ役目)。
// requestProcessQuit(): pidのイベントへ合図する。イベントはusilicだけが作り、プロセスと一緒に消えるので、
// falseならpidは動いているusilicではない(PIDが別のプロセスに使い回されていても誤って止めない)。
void listenForQuitRequest(volatile std::sig_atomic_t *flag);
bool requestProcessQuit(long long pid);
// 親子の受け渡し(Application::openChildChannel())のFIFOの代わりのメールスロット(2026-11-02)。FIFOと同じく、読む側が
// いなければ書けず(false)、読む側は待たずに読む(来ていなければ空)。
void *openChannelSlot(const std::string &name);
bool writeChannelSlot(const std::string &name, const std::string &message);
std::string readChannelSlot(void *slot);
void closeChannelSlot(void *slot);
#ifndef SIGKILL
#define SIGKILL 9
#endif
#ifndef SIGHUP
#define SIGHUP 1
#endif
#ifndef SIGTSTP
#define SIGTSTP 20
#endif

// 2026-10-15、直接のユーザー報告(Windowsで"7段目くらいから急激に速くなる"): MinGWのusleepはSleep(us/1000)で、
// 1000未満は待たず(Sleep(0))、それ以上も1ミリ秒単位でしか待てない。慣性スクロールの1ドットごとの待ち(usilicrcの
// SPEED・ARROWの500・200・100等)が0になり、その段から一気に速くなっていた。usleepの代わりに、残りが2ミリ秒を
// 超える間はSleepで、最後は高分解能の時計(QueryPerformanceCounter)を見ながら回って、マイクロ秒単位で待つ。
void preciseSleepMicroseconds(long microseconds);
#define usleep(us) preciseSleepMicroseconds(static_cast<long>(us))

// mkdir(path, mode): Windowsのmkdirは権限を取らない。
#include <direct.h>
inline int mkdir(const char *path, int mode) {
  (void)mode;
  return _mkdir(path);
}

// poll: Windowsのネイティブで待てるのはfd 0(キー、KeyInput::open()の後のコンソール、console_win.hpp)だけ。他は
// 「何も届いていない」(0)を返す(SHELLはShell::outputReady())。コンソールが無ければ待たない(-gのフレームのループ)。
// winsock2.hのpollfd(WSAPoll用)とは別物なので、両方を読む所では使わない。
#ifndef POLLIN
struct pollfd {
  int fd;
  short events;
  short revents;
};
#define POLLIN 0x0001
#define POLLOUT 0x0004
#define POLLERR 0x0008
#define POLLHUP 0x0010
int poll(struct pollfd *fds, unsigned long count, int timeout);
#endif

#endif  // _WIN32
