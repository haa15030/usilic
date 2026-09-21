#include "usilic.h"
/*
 * 【機能】nroff形式の下線(`_\b文字`)と太字(`文字\b文字`)の重ね打ちを読み飛ばし、タブを空白に
 *       変換して、最大256バイトをstrへコピーする(モードラインに出す文字列の下ごしらえ)。
 * 【C++】PipeBuffer::wrapLine (src/cpp/src/pipe_buffer.cpp)
 * 【備考】C++では重ね打ちの検出とタブの展開をPipeBufferが一括して行い、下線・太字はAttributeRunとして
 *       保持する(取り除かずに描き分ける)。
 */
static void tabtospc(char str[], char tmp[])
{ /* underlineまたはboldの属性を読み飛ばす */
  /* '\t'を' 'へ変換する */
  int c, i = 0, j = 0;

  do {
    if ((c = tmp[i]) == '_') {
      if (tmp[i+1] == K_h) {
	i += 2;
	continue;
      } else if (tmp[i+1] == '_' && tmp[i+2] == K_h && tmp[i+3] == K_h) {
	i += 4;
	continue;
      }
    } else if (c == K_h) {
      if (tmp[i+1] != K_h) {
	i += 2;
	continue;
      } else {
	i += 4;
	continue;
      }
    }
    str[j++] = (c == '\t') ? ' ' : c;
    i++;
  } while (c && j < 256);
  if (j == 256) str[255] = '\0';
}

/*
 * 【機能】1行(pp)を画面へ描く。属性列(ATTR)に従って太字・下線を切り替え、制御コードは反転の
 *       `^X`や`<XX>`で見せる。f_markが真のときは反転表示にする。ウィンドウ右端での
 *       バックスペースの扱いと、UTF-8では継続バイト0xa0を番兵と誤認しない処理を含む。
 * 【C++】ContentView::renderLine (src/cpp/src/content_view.cpp)
 * 【備考】旧はバイト列を1文字ずつ端末へ出す。C++は幅計算(Utf8::display_width)を使い、全角の
 *       境界や最終桁の扱いを明示的に決めている。
 */
void put_str(PP *pp, int f_mark)/*usilic*/
{
  int c2, c3;
  int i, n, i_attr, n_attr, f_reset, f_code, f_ctrl, f_grep;
  static int attr;
  ATTR *ap;

  if (!pp || pp->cs == F3) /* 仮想行 */
    return;
  if ((f_code = pp->f.code) || !pp->attr) {
    ap = NULL;
    i_attr = -1;
  } else {
    ap = pp->attr;
    i_attr = ap[0].n;
  }
  f_grep = pp->split == -T_grep;
  for (n = n_attr = c3 = f_ctrl = i = 0; n < pp->f.len; n++, i++) {
    if (f_mark && !n)
      if (pp->split == -f_mark) vt_stand();
      else vt_endstand();
    else if (ap && i_attr >= 0 && i_attr == n) {
      if ((attr = ap[n_attr++].attr) == '^' || attr == -'^') {
	if (attr > 0) vt_stand();
	else vt_endstand();
	vt_printf("^%c", ap[n_attr++].attr);
	if (attr > 0) vt_endstand();
	else vt_stand();
	i_attr = ap[n_attr].n;
	i++;
	continue;
      } else if (attr == '<' || attr == -'<') {
	if (attr > 0) vt_stand();
	else vt_endstand();
	vt_printf("<%2X>", ap[n_attr++].attr);
	if (attr > 0) vt_endstand();
	else vt_stand();
	i_attr = ap[n_attr].n;
	i += 3;
	continue;
      } else {
	if ((f_reset = attr < 0)) attr = -attr;
	if (attr == A_md3) vt_putchar(K_h);
	vt_seta(attr);
	i_attr = ap[n_attr].n;
	if (f_reset) {
	  n--;
	  i--;
	  continue;
	}
      }
    }
    c2 = pp->cs[n];
    if (f_code) {
      if (c2 == K_dl) c2 -= 0x80;
      if ((c3 = c2 < ' ')) {
	vt_seta(A_cs);
	c2 += '@';
      } else if ((c3 = c2 >= 0x80)) {
	vt_seta(A_cs);
	c2 = '*';
      }
    } else if (c2 == K_a0 && vt_charset(0) != C_utf8) {
      /* K_a0(0x00a0)は「次のバイトを^Xとして表示する」ことを意味する
	 内部の番兵バイトである(fork.cのf.codeのダンプ参照)。EUCでは
	 生のコンテンツバイトが0xa0になることは決して無い(EUCの2バイト
	 シーケンスは0xa1から始まる)ため、これに使い回しても安全だった。
	 UTF-8では、0xa0はよくある、全く普通の継続バイトである(例えば
	 U+5831「報」= E5 A0 B1の2バイト目)。そのためここでこれを番兵
	 として扱うと、本物の文字を壊してしまっていた。 */
      f_ctrl = 1;
      if (f_grep) vt_endstand();
      else vt_stand();
      vt_putchar('^');
      continue;
    } else if (c2 < ' ' && c2 != K_ec && c2 != K_i && c2 != K_h) continue;
    do {
      if (c2 == K_h) { 
	i -= 2;
	if (i == vt_col(0) - 2) {
	  vt_rc(); /* ウィンドウの右端での`backspace' */
	  continue;
	}
      } else if (i == vt_col(0) - 1 && pp->cs[n+1] == K_h) vt_sc();
      vt_putchar(c2);
    } while (0);
    if (c3) {
      c3 = 0;
      vt_seta(A_ce);
    }
    if (f_ctrl) {
      f_ctrl = 0;
      if (f_grep) vt_stand();
      else vt_endstand();
    }
  }
  if (ap && i_attr >= 0 && i_attr == n) 
    vt_seta(((attr = ap[n_attr].attr) < 0) ? -attr : attr);
  if (f_mark) vt_stand();
  else vt_endstand();
}

/*
 * 【機能】画面の本文部分(最下行のモードラインを除く全行)を、ppから順に描き直す。行が尽きた
 *       残りは空行にする。
 * 【C++】ContentView::redraw (src/cpp/src/content_view.cpp)
 */
PP *disp_pp(PP *pp, int f_mark)/*usilic*/
{ /* 行を表示する */
  int i, f_pp, f_rc = 0;

  if ((f_pp = (int)pp)) i = 0;
  else { /* 行が無い */
    vt_move(0, 0);
    vt_eeol();
    i = 1;
  }
  for (; i < vt_row(0) - 1; i++) {
    vt_endstand();
    vt_move(i, 0);
    vt_eeol();
    if (f_pp) put_str(pp, f_mark);
    if ((f_pp = pp && pp->next)) pp = pp->next;
    else if (!f_rc) {
      vt_sc();
      f_rc = 1;
    }
  }
  if (f_rc) vt_rc();
  return pp;
}

/*
 * 【機能】最下行のモードラインを組み立てて描く。左から、モード名(反転)、現在行/総行数と%、
 *       引数(バイト数や`(ls)`等)、直前のモード(戻り先)、右端のプログラム名。幅が足りなければ
 *       名前や戻り先の先頭を`...`で切り詰める。文字列を入力させるモード(f_enter)では入力欄を
 *       確保する。最後に必要なら本文も描き直す(disp_pp)。
 * 【C++】ContentView::renderModeLine; ContentView::fitModeLineFields; ContentView::ModeLineFields (src/cpp/src/content_view.cpp)
 * 【備考】旧は状態をstatic変数に溜めて、呼び出しごとに全部を組み立て直す。C++はModeLineFieldsという
 *       構造体で各欄を渡し、幅に収まらない場合の欠落順もfitModeLineFieldsに集約されている。
 */
int disp_window(PP *ps0, PP *pp0, int f_M0, char name0[], u_char prev0[], char *argp, int *lenp0)/*usilic*/
{
  int f_mark, f_arc, i, j, n, len, llen;
  u_char line[30];
  PP p = {0,};
  static int f_M;
  static PP *ps, *pp;
  static char name[N_char];
  static u_char prev[N_char], *arg;
  static int vlen, k;
  static int nlen, f_enter, *lenp;
  static char *copy[3];

  if (!vlen) vlen = strlen(getenv(ENV_PROG)) + 2;
  switch(f_M0) {
  case -1:
    return strlen(name);
    break;
  case -2:
    if (*copy) for (i = 0; i < 3; i++) free(copy[i]);
    return 0;
    break;
  default:
    ps = ps0;
    pp = pp0;
    f_M = f_M0;
    lenp = lenp0;
    break;
  }
  if (f_M == T_shell2) {
    f_M = M_shell;
    vt_mode(-'d');
  }
  if (argp) {
    arg = argp;
    i = strlen(name0) - 1;
    /* mode-line [1] (access-name) */
    if ((j = *name0 == '/'))
      if (*prev0 == K_y) strcpy(name, name0); /* pager */
      else if (name0[i] == '/')
	if (*prev0 == K_z) strcpy(name, name0); /* file-selector */
	else strcpy(name, ".");
      else {
	while (i && name0[i] != '/') i--;
	strcpy(name, &name0[i+1]);
      }
    else if (i)
      /* name[0] == ' ': mode名 */
      strcpy(name, (*name0 == ' ') ? &name0[1] : name0);
    else *name = '\0';
    k = vt_col(0) / 4 - 1;
    switch(f_M) {
    case M_man:
      k = vt_col(0) / 2 - 1;
    case M_prog:
    case M_egrep: case M_cut:
    case M_lfile: case M_ldir:
    case M_hist: case T_hist:
    case T_hfile: case T_hdir: case T_hman: case T_hlocate:
    case T_hfgrep: case T_hegrep:
    case M_cmd: case T_cmd:
    case M_list: case T_list:
    case T_locate:
      /* mode-lineへ文字列を入力する */
      f_enter = 1;
      break;
    case M_man2:
    case M_cut2:
    case M_fgrep:
    case M_locate:
      k = vt_col(0) / 2 - 1;
    case M_prog2:
    case M_lfile2:
    case M_ldir2:
      /* mode-lineへ文字列を入力する */
      f_enter = 2;
      break;
    default:
      k = vlen;
      f_enter = 0;
      break;
    }
  }
  llen = 3;
  switch(f_M) {
  case M_exit:
    sprintf(line, "%s (y/n) ?  ", s_S(S_errg));
    break;
  case M_copy:
    strcpy(line, "---");
    llen += strlen(line);
    break;
  case T_shell:
    llen += 17; /* "000000/000000 END" */
    break;
  default:
    /* T_manはマニュアルページ全体を前もって読む(init_man()参照)ので、
       その合計は即座に分かる――これは、下の本当にまだストリーミング
       中のpipe用に確保された固定6桁幅ではなく、完全に読み込み済みの
       コンテンツと同じ可変幅の"N/M"形式を使う。 */
    llen += print_line(ps, pp
		       , (f_M == M_shell || f_M == T_pipe2
			  || f_M == T_pipe
			  || f_M == T_pipe4 || f_M == T_pipe5
			  || f_M == T_pipe3)
		       ? -1 : 1, line);
    break;
  }
  if (argp) {
    if (f_M == M_exit) j = strlen(line);
    else j = (*arg == K_z && !arg[1]) ? 0 : strlen(arg);
    nlen = strlen(name);
    /* previous-mode(mode-line [6]) */
    tabtospc(prev, prev0);
    if ((len = (f_enter) ? k : vt_col(0) - (nlen + llen + j) + 1) < k) 
      j = k - 1;
    else j = len;
    i = k;
    if (*prev != K_z && *prev != K_y) {
      if (strlen(prev) > j) {
	n = strlen(prev) - j + 3;
	if (count_k(prev, n)) sprintf(prev, "... %s", &prev[n + 1]);
	else sprintf(prev, "...%s", &prev[n]);
      } else i = strlen(prev) + 1;
    }
    if (len < i - 1) {
      k = (*arg == K_z && !arg[1]) ? 0 : strlen(arg);
      j = vt_col(0) - (llen + k + i) + 4;
      n = nlen - j + 5;
      if (count_k(name, n)) sprintf(name, "... %s", &name[n + 1]);
      else sprintf(name, "...%s", &name[n]);
      nlen = strlen(name);
      k = i;
    }
  }
  vt_move(vt_row(0) - 1, 0);
  vt_stand();
  p.cs = name;
  p.f.len = strlen(p.cs);
  put_str(&p, 0);
  vt_endstand();
  vt_putchar(' ');
  if (f_M == M_copy) len = llen + 2;
  else if (f_enter == 2) len = *lenp = 0;
  else len = (f_M == T_shell) ? *lenp : print_line(ps, F2, nlen, line) + 1;
  if (len) {
    vt_move(vt_row(0) - 1, len);
    vt_putchar(' ');
  }
  if (f_M != M_exit && f_enter != 2 && arg[1]) {
    /* print_arg()は、バイト数と"(mode)"ラベルを1つの文字列に結合する
       ("1410 (ls)"、"1307 (man)"等、そのT_dir/T_manのcaseとchpの分岐
       参照)――実際の報告(スクリーンショット=bh)により、この文字列
       全体が、ここにある他のmode-lineフィールドの組(name/position/
       prev、これらは全て、間にstandoutでない素の空白を挟んでいる)
       とは異なり、途切れの無い1つの反転表示として描画されている
       ことが指摘された。" ("(print_arg()がラベルの前に常に使う正確な
       セパレータ)で分割することで、バイト数とラベルが、本当の隙間
       を挟んだそれぞれ独立の反転表示となり、この関数の他の部分と
       一致するようにする; ラベルの無い素の数値(例えば単なるFileの
       バイト数)や、数値の無い素の"(label)"(例えばT_du/T_pipe4)には
       見つけるべき" ("が無く、単一の反転表示のケースへ変更無く
       フォールスルーする。 */
    u_char *paren = (u_char *)strchr((char *)&arg[1], '(');
    if (paren && paren > &arg[1] && paren[-1] == ' ') {
      vt_stand();
      vt_printf("%.*s", (int)(paren - 1 - &arg[1]), &arg[1]);
      vt_endstand();
      vt_putchar(' ');
      vt_stand();
      vt_printf("%s", paren);
      vt_endstand();
    } else {
      vt_stand();
      vt_printf("%s", &arg[1]);
      vt_endstand();
    }
    vt_putchar(' ');
    len += strlen(&arg[1]) + 1;
  }
  f_mark = fmark(f_M);
  f_arc = f_M == T_tar || f_M == T_lha || f_M == T_unzip 
    || f_M == T_rpm || f_M == T_deb;
  if (f_enter) {
    *lenp = (f_enter == 1) ? len + 1 : nlen + 1;
    len = vt_col(0) - (k + 1);
    vt_eeol();
  }
  if (*prev != K_z && *prev != K_y) {
    char *cp, *cp2;
    vt_move(vt_row(0) - 1, len + 1);
    while ((cp = strchr(prev, '\033')))
      if ((cp2 = strchr(cp, 'm'))) {
	memmove(cp, &cp2[1], strlen(&cp2[1]) + 1);
      } else break;
    if (argp) {
      for (i = strlen(prev), j = vt_col(0) - (len + i + 1); j > 0; i++, j--)
	prev[i] = ' ';
      prev[i] = '\0';
    }
    if (f_M == M_copy) {
      if (!*copy) {
	copy[0] = strdup(name);
	copy[1] = strdup(line);
      } else free(copy[2]);
      copy[2] = strdup(prev);
      disp_copy(copy, 0);
    } else {
      vt_stand();
      p.cs = prev;
      p.f.len = strlen(p.cs);
      /* prev[]はunderlineまたはboldの属性を含み得るため */
      put_str(&p, 0);
    }
    if (f_mark || f_arc) *lenp = len + 1; 
  } else {
    vt_eeol();
    vt_move(vt_row(0) - 1, vt_col(0) - vlen);
    vt_printf(" %s%s%s ", vt_geta(A_md), getenv(ENV_PROG), vt_geta(A_me));
  }
  vt_endstand();
  if (!f_mark && !f_arc && !f_enter && f_M != T_shell && lenp) *lenp = 0;
  if (f_M == T_arc3 || f_M == M_exit) f_mark = 0;
  if (f_M != M_copy && (!ps0 || (argp && vt_mode('D')))) disp_pp(ps, f_mark);
  return (f_M == M_shell) ? len - strlen(arg) : nlen;
}

/*
 * 【機能】モードラインの「現在行/総行数 %」の文字列を作る(linepへ返すか、直接描く)。linepがF2のときは、
 *       行番号のジャンプ先を数字で入力させて値を返す(^Gで取消し)。ps0がF2のときは`Mark set`を
 *       表示する。
 * 【C++】ContentView::renderModeLine (src/cpp/src/content_view.cpp); Application::resolveGotoLineRow (src/cpp/src/application.cpp)
 * 【備考】旧は1つの関数に、表示文字列の生成・行番号の入力・Mark set表示が同居している。C++では
 *       位置表示(renderModeLine)と行番号ジャンプ(Application側)が別々になっている。
 */
int print_line(void *ps0, PP *pp, int len, u_char *linep)/*usilic*/
{ /* mode-lineに現在行と総行数を表示する */
  int c, i, j, k, row, e_row, n_row, f_code;
  u_char str[10], line[32], tmp[32], num[8];
  static int tlen, tcol;
  static FILE **pinp;
  PP *ps;

  if (pp == F2 && !linep) {
    pinp = ps0;
    return 0;
  }
  if (ps0 == F2) {
    vt_sc();
    vt_endstand();
    vt_move(vt_row(0) - 1, tcol + 1);
    vt_printf("Mark se");
    if ((i = tlen - 8) > 0) for (vt_putchar('t'); i > 0; i--) vt_putchar(' ');
    vt_endstand();
    vt_rc();
    return 0;
  }
  if (linep == F2) {
    vt_sc();
    vt_endstand();
    if (len < 0) {
      len = -len;
      j = strlen(s_P(P_mode)[M_shell]) - 1;
      k = 16;
    } else {
      j = tcol;
      k = tlen;
    }
    vt_move(vt_row(0) - 1, j + 1);
    vt_putchar(len);
    num[0] = len;
    if ((i = k - 1) > 0) for (vt_putchar(' '); i > 0; i--) vt_putchar(' ');
    i = 1;
    vt_move(vt_row(0) - 1, j + 1 + i);
    c = 0;
    do {
      if ((c = vt_getch()) != K_m && c != K_g && c != K_dl && c != K_h
	  && (c < '0' || c > '9')) continue;
      if (c >= '0' && c <= '9') {
	if (i >= 7) continue;
	num[i++] = c;
	vt_move(vt_row(0) - 1, j + i);
	vt_putchar(c);
	vt_flush();
      } else if (c == K_m) num[i] = '\0';
      else if (c == K_dl || c == K_h) {
	if (i) {
	  vt_move(vt_row(0) - 1, j + i);
	  vt_putchar(' ');
	  vt_flush();
	  vt_move(vt_row(0) - 1, j + i);
	  i--;
	} else c = K_g;
      }
    } while (c != K_m && c != K_g);
    i = (c == K_g) ? 0 : atoi(num);
    vt_rc();
    return i;
  }
  vt_seta(A_me);
  ps = ps0;
  *line = '\0';
  if (!*line && pp != F2) {
    f_code = pp && pp->f.code;
    if (!pp || (pp && pp->tell == (uintptr_t)F7)) row = n_row = e_row = 0;
    else {
      row = pp->f.line;
      while (pp->next) pp = pp->next;
      n_row = pp->f.line;
      for (i = vt_row(0) - 2; i && ps->next; i--) ps = ps->next;
      e_row = ps->f.line;
      if (pinp) n_row++;
    }
    if (len <= 0) { 
      /* 現在行・総行数を常に6桁の数値として設定する */
      for (j = 6, i = 0; i < j; i++) str[i] = '0';
      str[i] = '\0';
      strcpy(tmp, str);
      strcpy(line, utoa(row));
      for (i = strlen(line), k = j; i; i--, j--) str[j-1] = line[i-1];
      strcpy(line, utoa(n_row));
      for (i = strlen(line), j = k; i; i--, j--) tmp[j-1] = line[i-1];
      len = -len;
    } else {
      strcpy(tmp, utoa(n_row));
      for (j = strlen(tmp), i = 0; i < j; i++) str[i] = '0';
      str[i] = '\0';
      strcpy(line, utoa(row));
      for (i = strlen(line); i; i--, j--) str[j-1] = line[i-1];
    }
    if (f_code)
      if (e_row == n_row || !n_row)
	sprintf(line, "--- END");
      else
	sprintf(line, "--- %2d%%", e_row * 100 / n_row);
    else if (e_row == n_row || !n_row)
      sprintf(line, "%s/%s END", str, tmp);
    else
      sprintf(line, "%s/%s %2d%%", str, tmp, e_row * 100 / n_row);
  }
  if (!len) len = -1;
  if (pp != F2 && linep) {
    strcpy(linep, line);
    return strlen(line);
  }
  if (linep) strcpy(line, linep);
  if (!strncmp(line, s_S(S_errg), strlen(s_S(S_errg)))) vt_endstand();
  else vt_stand();
  vt_move(vt_row(0) - 1, len + 1);
  vt_printf("%s", line);
  vt_move(vt_row(0) - 2, 0);
  vt_endstand();
  tlen = strlen(line);
  tcol = len;
  print_intr(NULL, F8);
  return tlen + tcol;
}

/*
 * 【機能】モードライン右側の「直前のモード(戻り先)」欄を描く。長ければ先頭を`...`で切り詰め、短ければ
 *       空白で埋める。dirがNULLのときは描かずに、整形した文字列をprevへ返す。
 * 【C++】ContentView::ModeLineFields (src/cpp/src/content_view.cpp); ContentView::fitModeLineFields (src/cpp/src/content_view.cpp)
 * 【備考】C++では戻り先の欄はModeLineFieldsのtrailing3にあたる。
 */
void disp_prev(u_char *prev, void *dir, int klen0)/*usilic*/
{
  int i, j, k, col;
  u_char str[N_line];
  static PP p = {0,};
  static int klen, f_mark;
  static PP *ps;
 
  if (!prev) {
    ps = dir;
    f_mark = klen0;
    return;
  }
  if (klen0) {
    if (klen0 > 0) {
      klen = klen0;
      k = vt_col(0) / 4 - 1;
      col = vt_col(0) - k;
    } else {
      klen = -klen0;
      k = vt_col(0) - klen;
      col = klen;
    }
  } else {
    k = vt_col(0) - klen;
    col = klen;
  }
  strcpy(str, (*prev == K_z) ? dir : (char*)prev);
  if ((i = strlen(str)) > k) {
    j = strlen(str) - k + 3;
    if (count_k(str, j)) sprintf(str, "... %s", &str[j + 1]);
    else sprintf(str, "...%s", &str[j]);
    if ((j = strlen(str)) > 0) j--;
    str[j] = '\0';
  } else {
    for (j = vt_col(0) - col - i; j > 0; i++, j--) str[i] = ' ';
    i--;
    str[i] = '\0';
  }
  if (dir) {
    vt_move(vt_row(0) - 1, klen);
    vt_eeol();
    vt_stand();
    vt_move(vt_row(0) - 1, col);
    p.cs = str;
    p.f.len = strlen(p.cs);
    p.f.len--;
    put_str(&p, 0);
  } else strcpy(prev, str);
}
  
/*
 * 【機能】COPYモードの画面を描く。本文の各行を出し、最下行にモード名・位置・戻り先の3欄を反転で
 *       並べる。
 * 【C++】CopyMode (src/cpp/src/copy_mode.cpp)
 * 【備考】C++のCopyModeは、COPY開始時点の画面をスナップショットとして保持して選択を扱う。
 */
void disp_copy(void *p, int nlen)/*usilic*/
{
  int i;
  char **cpp;
  static char **copyp;

  if (!nlen) {
    copyp = p;
    return;
  }
  cpp = p;
  for (i = 0; i < vt_row(0) - 1; i++) {
    vt_move(i, 0);
    vt_eeol();
    if (cpp[i]) vt_printf("%s", cpp[i]);
  }
  vt_move(vt_row(0) - 1, 0);
  for (i = 0; i < 3; i++) {
    vt_stand();
    vt_printf("%s", copyp[i]);
    if (i == 2) break;
    vt_endstand();
    vt_putchar(' ');
  }
}

