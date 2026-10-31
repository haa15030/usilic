#include "usilic.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>

static struct {
  int sfd;
  char path[N_line];
} Usilic;

static void usage(char usilic[], char version[])
{

  char *cp;
  static char tmp[256];

  memcpy(tmp, version, 80);
  *strchr(tmp, '\n') = '\0';
  if ((cp = strchr(tmp, '\n'))) *cp = '\0';
  fprintf(stderr, "Usage: %s\n", usilic);
  fprintf(stderr, "       %s [directory]\n", usilic);
  fprintf(stderr, "       %s [file] [< file]\n", usilic);
  fprintf(stderr, "%s\n", tmp);
}
 
static void main_pipe(int dummy)
{
  if (Usilic.sfd) {
    close(Usilic.sfd);
    Usilic.sfd = 0;
    unlink(Usilic.path);
  }
  free_S();
  exit(0);
}

static int set_path(char dir[], char name[])
{
  int f_M, i;
  struct stat st;
  
  if (*name != '/') {
    char *p = alloca(strlen(dir) + strlen(name) + 1);
    strcpy(p, dir);
    strcat(p, name);
    strcpy(name, p); /* full path */
  }
  stat(name, &st);
  if ((i = strchr(name, '\0')[-1] == '/') || S_ISDIR(st.st_mode)) {
    f_M = T_dir;
    if (!i) strcat(name, "/");
  } else f_M = T_file;
  erase_dot(name);
  strcpy(dir, name);
  get_dir(dir);
  return f_M;
}

static void from_shell(char *name, int pid, int f, char *mess)
{
  char str[N_pipe+1];
  int i, c, len, row;
  u_char m;

  get_winsize(&row, &c);
  sprintf(&strstr(mess, "/s.")[3], "%d.%d", (int)getuid()
	  , (pid) ? pid : (int)getppid());
  if (!name && (!f || f == 'p')) {
    struct sockaddr_un cli_addr, serv_addr;
    int sfd, clilen;
    m = T_pipe2;
    signal(SIGPIPE, main_pipe);
    strcpy(Usilic.path, mess);
    if ((sfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
      fprintf(stderr, "%s: Can't open socket\n", s_S(S_usilic));
      free_S();
      exit(0);
    }
    serv_addr.sun_family = AF_UNIX;
    strcpy(serv_addr.sun_path, Usilic.path);
    unlink(Usilic.path);
    if (bind(sfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) >= 0) {
      clilen = sizeof(cli_addr);
      if (listen(sfd, 5) >= 0)
	if ((Usilic.sfd = accept(sfd, (struct sockaddr *)&cli_addr, &clilen)) 
	    >= 0) {
	  send(Usilic.sfd, &m, sizeof(u_char), 0);
	  c = getpid();
	  send(Usilic.sfd, &c, sizeof(int), 0);
	  for (i = row-1; i && (c = fgetc(stdin)) != EOF; ) {
	    send(Usilic.sfd, &c, 1, 0);
	    if (c == '\n') i--;
	    else if (c == '\r') {
	      if ((c = fgetc(stdin)) == '\n') i--;
	      send(Usilic.sfd, &c, 1, 0);
	    }
	  }
	  if (c != EOF) 
	    do {
	      c = fread(str, 1, N_pipe, stdin);
	      send(Usilic.sfd, str, c, 0);
	    } while (c == N_pipe);
	  close(Usilic.sfd);
	  unlink(Usilic.path);
	} else perror("accept");
      else perror("listen");
    } else perror("bind");
  } else {
    FILE *fp;
    char *cp, dir[N_line];
    getcwd(dir, N_line);
    if (strchr(dir, '\0')[-1] != '/') strcat(dir, "/");
    switch(f) {
    case 'u':
      m = M_du;
      break;
    case 'U':
      m = T_du2;
      break;
    default:
      if ((cp = strchr(name, '\r'))) *cp = '\0';
      m = set_path(dir, name);
      if (access(name, F_OK) < 0) {
	printf("%s: %s: %s\n", s_S(S_usilic), name, strerror(ENOENT));
	exit(1);
      }
      break;
    }
    sprintf(str, "%s.%s", mess, s_S(S_usilic));
    if (!(fp = fopen(str, "w"))) { /* usilic.old: fwrite() to a NULL FILE* crashed */
      fprintf(stderr, "%s: %s: %s\n", s_S(S_usilic), str, strerror(errno));
      free_S();
      exit(1);
    }
    fwrite(&m, 1, sizeof(u_char), fp);
    len = strlen(dir) + 1;
    fwrite(&len, 1, sizeof(int), fp);
    fwrite(dir, 1, len, fp);
    if (m != M_du) {
      len = strlen(name) + 1;
      fwrite(&len, 1, sizeof(int), fp);
      fwrite(name, 1, len, fp);
    }
    fclose(fp);
    rename(str, mess);
  }
  free_S();
  exit(0);
}

static int check_ps(int f_tty, int pid, char **err)
{
  int n_pid, pv[4];
  char str[256];
  FILE *fp;
  static char n[4];
  static char *sp[] = {
    "ps",
    "grep",
    "usilic.old", /* usilic.old: was "usilic" */
    n,
  };
  
  if (f_tty) sprintf(sp[3], "\\-%d", f_tty);
  pipe(pv);
  if (!fork()) {
    pipe(&pv[2]);
    if (!fork()) {
      close(1);
      dup2(pv[3], 1);
      close(pv[3]);
      close(pv[2]);
      execlp(sp[0], sp[0], "a", NULL);
    }
    close(0);
    dup2(pv[2], 0);
    close(pv[3]);
    close(pv[2]);
    close(1);
    dup2(pv[1], 1);
    close(pv[1]);
    close(pv[0]);
    if (f_tty) execlp(sp[1], sp[1], sp[3], "-", NULL);
    else execlp(sp[1], sp[1], sp[2], "-", NULL);
  }
  close(pv[1]);
  fp = fdopen(pv[0], "r");
  n_pid = 0;
  if (f_tty) {
    do {
      if (!fgets(str, 256, fp)) break;
      if (strstr(str, sp[2])) n_pid++;
    } while (n_pid < 2);
    if (n_pid == 2) 
      fprintf(stderr, "%s -%d: Invalid terminal\n", sp[2], f_tty);
    else {
      fclose(fp);
      return 0;
    }
  } else {
    do {
      if (!fgets(str, 256, fp)) break;
      sscanf(str, "%d", &n_pid);
    } while (n_pid != pid);
    if (!n_pid) fprintf(stderr, "Unknown error\n");
    else fprintf(stderr, "\n     Already running.\n");
  }
  fclose(fp);
  return 1;
}
    
static void check_pl(int f_tty, char **err)
{
  struct stat st;
  int i, pid;
  FILE *fp;
  char *s_pid;

  if (!f_tty) {
    char *cp = getenv("HOME");
    s_pid = alloca(strlen(cp) + 32); /* usilic.old: + "/.usilic.old/pid": +10 overflowed once renamed from tcb */
    strcpy(s_pid, cp);
    if (strchr(s_pid, '\0')[-1] != '/') strcat(s_pid, "/");
    strcat(s_pid, ".usilic.old/pid"); /* usilic.old: was .usilic/pid */
    if (!stat(s_pid, &st)) {
      fp = fopen(s_pid, "r");
      fscanf(fp, "%d", &pid);
      fclose(fp);
      if (pid && !kill(pid, 0)) {
	struct stat old;
	stat(s_pid, &old);
	fprintf(stderr, "usilic.old: Process-ID %d ... ", pid); /* usilic.old: was usilic */
	fflush(stdout);
	for (i = 6; i; i--) {
	  kill(pid, SIGCONT);
	  stat(s_pid, &st);
	  if (st.st_mtime != old.st_mtime) break;
	  sleep(1);
	}
	if (i) {
	  if (check_ps(0, pid, err)) {
	    free_S();
	    exit(1);
	  }
	} else unlink(s_pid);
      }
    }
  } else if (check_ps(f_tty, 0, err)) {
    free_S();
    exit(1);
  }
} 

int main(int argc, char *argv[])
{
  char pathname[N_line], args[N_line] = {'\0',};
  struct stat st;
  struct termios otty;
  static struct {
    u_int shell	: 1;
    u_int usilic	: 8;
  } f;
  static char usilic[] = "usilic.old"; /* usilic.old: was "usilic"; also names ~/.usilic.old/ and usilic.oldrc */
  static char tmp[N_line];
  static char *err[] = {
    "Invalid option",
    "USILIC environment variable is set",
  };
  /*
   * f.usilic
   * 	& 0x80	: file manager (with history-list)
   *	& 0x40	: file manager [directory]
   *	& 0x20	: pager (stdin)
   *	& 0x10	: pager [file]
   *	& 0x08	: -h
   *	& 0x04	: -?
   */
  static char version[] = {
#include "version.h"
  };
  int i = 0;
  FILE *pin = F1;

  printf("ggg\n");
  conf(tmp, NULL, NULL, 0);
  if (stat(tmp, &st) < 0) {
    if (mkdir(tmp, 0) < 0) {
      fprintf(stderr, "%s: %s: %s\n", usilic, tmp, strerror(errno));
      exit(1);
    }
    chmod(tmp, S_IRWXU|S_IRWXG|S_IRWXO);
  } else if (!S_ISDIR(st.st_mode) || access(tmp, R_OK|W_OK) < 0 ) {
    fprintf(stderr, "%s: %s: %s\n", usilic, tmp, strerror(errno));
    exit(1);
  }
  /* usilic.old: NOT sprintf(tmp, "%s/s.", tmp) -- output and the %s source argument
     would be the same buffer, which is undefined behavior. In practice
     this corrupted tmp down to just "/s." (losing the "/tmp/.usilic" prefix),
     so from_shell() later tried to fopen() a path at the filesystem root,
     got EACCES, and fwrite()'d into the resulting NULL FILE*. strcat has
     no such aliasing since its 2nd argument is a literal, not tmp itself. */
  strcat(tmp, "/s.");
  *pathname = '\0';
  /* usilic.old: USILICVT/USILIC were renamed, they are the new C++ usilic's */
  if (!getenv("USILIC_OLDVT") && tcgetattr(0, &otty) < 0) f.usilic |= 0x20;
  f.shell = getenv("USILIC_OLD") != NULL;
  if (argc == 1) {
    if (f.shell)
      if (f.usilic & 0x20) from_shell(NULL, 0, 0, tmp);
      else {
	fprintf(stderr, "%s: %s\n", usilic, err[1]);
	exit(1);
      }
    else if (!(f.usilic & 0x20)) f.usilic |= 0x80;
  } else {
    while (*++argv)
      if (**argv == '-')
	switch(i = (*argv)[1]) {
	case 'p':
	  if (!f.shell || !(f.usilic & 0x20)) {
	    fprintf(stderr, "%s: %s -p\n", usilic, err[0]);
	    usage(usilic, version);
	    exit(1);
	  }
	  from_shell(NULL, atoi(argv[1]), i, tmp);
	  break;
	case 'l':
	  if (f.shell) {
	    fprintf(stderr, "%s: %s\n", usilic, err[1]);
	    exit(1);
	  }
	  f.usilic |= 0x80;
	  strcpy(args, *argv);
	  break;
	case 'u':
	case 'U':
	  if (!f.shell) {
	    fprintf(stderr, "%s: %s -%c\n", usilic, err[0], i);
	    usage(usilic, version);
	    exit(1);
	  }
	  from_shell(NULL, 0, i, tmp);
	  break;
	case '?':
	case 'h':
	  f.usilic &= ~(0x80 | 0x20);
	  f.usilic |= (i == '?') ? 0x04 : 0x08;
	  sprintf(strchr(args, '\0'), " %s", *argv);
	  break;
	default:
	  fprintf(stderr, "%s: %s -%c\n", usilic, err[0], i);
	  usage(usilic, version);
	  exit(1);
	  break;
	}
      else if (f.usilic & 0x20) continue;
      else
	if (stat(*argv, &st) < 0 || access(*argv, R_OK) < 0) {
	  fprintf(stderr, "%s: %s: %s\n", usilic, *argv, strerror(errno));
	  exit(1);
	} else {
	  if (f.shell) from_shell(*argv, 0, 0, tmp);
	  f.usilic &= ~0x80;
	  f.usilic |= (S_ISDIR(st.st_mode)) ? 0x40 : 0x10;
	  strcpy(pathname, *argv);
	  sprintf(strchr(args, '\0'), " %s", *argv);
	}
  }  
  if (f.usilic & 0x20) {
    strcpy(pathname, s_P(P_mode)[T_pipe]);
    pin = stdin;
  }
  if (f.shell) {
    fprintf(stderr, "%s: %s\n", usilic, err[1]);
    free_S();
    exit(1);
  }
  if (f.usilic & 0x80) check_pl(0, err);
  signal(SIGCHLD, sig_child);
  if (conf(NULL, args, version, f.usilic) < 0) {
    free_S();
    exit(1);
  }
  if (vt_set(f.usilic) < 0) {
    free_S();
    exit(1);
  }
  save_signal(f.usilic);
  sprintf(tmp, "%s v%s", usilic, &(strchr(version, ' '))[1]);
  *strchr(tmp, '-') = '\0';
  setenv("USILIC_OLD", tmp, 1); /* usilic.old: was USILIC, now the new C++ usilic's */
  if (access(s_S(S_usilicrc), F_OK) < 0) {
    fprintf(stderr, "%s: %s: %s\n", usilic, s_S(S_usilicrc), strerror(ENOENT));
    free_S();
    exit(1);
  }
  if (vt_init(1) < 0) {
    free_S();
    exit(1);
  }
  print_stderr(s_S(S_stderr));
  menu(pathname, pin);
  exit_usilic();
  print_stderr(NULL);
  exit(exit_ALLOC(NULL));
  return 0;
}

void exit_usilic(void)/*usilic*/
{
  signal(SIGSEGV, SIG_DFL);
  if (is_fm()) {
    start_shell(0, 0, NULL, NULL, NULL, NULL);
    if (vt_set(0) & 0x80) {
      int i;
      char str[256];
      strcpy(str, s_S(S_write));
      i = ichrstr(str, 0, "w.usilic.old"); /* usilic.old: was w.usilic */
      strcpy(&str[i-1], "shell");
      rename(s_S(S_log), str);
      strcpy(&str[i-1], "error");
      rename(s_S(S_error), str);
    }
  }
  unlink_file();
  vt_init(-1);
  free_S();
  free_h(NULL);
}  

