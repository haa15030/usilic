#include "usilic.h"
#include <signal.h>
#include <termios.h>
/*
 * 【機能】カーソル行(rvp->pp)の文字列のうち、名前の開始桁(rvp->col)以降だけを
 *       rvp->strへ取り出す(選択中のエントリ名の取得)。SHELLの仮想行では何もしない。
 * 【C++】DirectoryMode::selectedEntry (src/cpp/src/directory_mode.cpp)
 * 【備考】旧は表示行の文字列そのものから名前を切り出す。C++はDirectoryEntry.nameへ構造化済み。
 */
static void rv_str(RV *rvp, int f_M)
{
  if (f_M == M_shell /* rvp->pp->cs == F3 : 仮想行(SHELLプロンプト) */
      || !strlen(rvp->str)) return; /* T_file: rvp->str == "" */
  strcpy(rvp->str, &rvp->pp->cs[rvp->col]);
  if (!strlen(rvp->str)) strcpy(rvp->str, " ");
  rvp->str[vt_col(0) - rvp->col - (is_ls(f_M) != 0)] = '\0';
}

/*
 * 【機能】ページ先頭行(pp)が、行のリストの先頭から数えて何行目か(0始まり)を返す。
 * 【C++】ContentView::topIndex (src/cpp/src/content_view.cpp)
 * 【備考】旧は双方向リストを辿って数える。C++は配列の添字をそのまま保持する。
 */
static int set_top(PP *pp)
{
  int top;

  for (top = 0; pp->prev; top++, pp = pp->prev) ;
  return top;
}

/*
 * 【機能】ページ単位の移動。M_back/M_kback/M_headは前のページ・先頭へ、M_forw/M_kforw/M_tailは
 *       次のページ・末尾へ移す。パイプ入力は必要なだけ追加で読み込み(alloc_pipe)、移動後に
 *       画面を更新する(端末にステータス行(hs、vt_mode 'H')が有れば端末のスクロールで
 *       1行ずつ、無ければ全体を再描画)。位置情報(pnp)を更新し、端末の入力バッファを捨てる
 *       (tcflush)。
 * 【C++】ContentView::pageUp; ContentView::pageDown; ContentView::goToTop; ContentView::goToBottom (src/cpp/src/content_view.cpp)
 * 【備考】旧はPP(行の双方向リスト)のポインタ操作。C++は行の配列+添字で、端末スクロールと描画は
 *       Display側(TerminalDisplay::scrollForward等)に分離されている。
 */
int move_page(int m, RV *rvp, PN *pnp, FILE **pinp, int f_M)/*usilic*/
{
  FILE *pin;
  int i = 0, j, f_mark, top;

  pin = (f_M == M_egrep || f_M == M_fgrep || f_M == M_shell) ? NULL : *pinp;
  f_mark = fmark(f_M);
  switch(m) {
  case M_back:
  case M_kback:
  case M_head:
    if (!rvp->ps->prev) {
      while (rvp->pp->prev) {
	rvp->pp = rvp->pp->prev;
	rvp->row--;
	pnp->row--;
      }
      rv_str(rvp, f_M);
      if (pinp) *pinp = pin;
      pnp->line = rvp->pp->f.line;
      return 0;
    }
    while (rvp->row) {
      rvp->pp = rvp->pp->prev;
      rvp->row--;
      pnp->row--;
    }
    if (pnp->nrow > vt_row(0) - 1) {
      if (m == M_head)
	while (rvp->ps->prev) {
	  rvp->ps = rvp->ps->prev;
	  rvp->pe = rvp->pe->prev;
	  rvp->pp = rvp->pp->prev;
	}
      else  
	for (i = 0; i < vt_row(0) - 1 && rvp->ps->prev; i++) {
	  rvp->ps = (vt_mode('H')) 
	    ? sr_row(rvp->ps->prev, f_mark) : rvp->ps->prev;
	  rvp->pe = rvp->pe->prev;
	  rvp->pp = rvp->pp->prev;
	}
    }
    if (!vt_mode('H') || m == M_head) disp_pp(rvp->ps, f_mark);
    rv_str(rvp, f_M);
    break;
  case M_tail:
    if (pin && pin != F1) pin = seek_intr(pin, &pnp->nrow);
  case M_forw:
  case M_kforw:
    while (1) {
      if (!rvp->pe->next) {
	if (pin && pin != F1) {
	  for (i = pnp->nrow; pin && pnp->nrow - i < vt_row(0) - 2; ) 
	    alloc_pipe(0, &pin, &pnp->nrow, NULL, 0);
	  if (pin) continue;
	}
	while (rvp->pp->next) {
	  rvp->pp = rvp->pp->next;
	  rvp->row++;
	  pnp->row++;
	}
	rv_str(rvp, f_M);
	if (pinp) *pinp = pin;
	pnp->line = rvp->pp->f.line;
	return 0;
      }
      break;
    }
    while (rvp->row < vt_row(0) - 2) {
      rvp->pp = rvp->pp->next;
      rvp->row++;
      pnp->row++;
    }
    if (pnp->nrow > vt_row(0) - 1) {
      if (m == M_tail)
	if (pin && pin != F1)
	  while (rvp->pe->next->next) {
	    rvp->ps = rvp->ps->next;
	    rvp->pe = rvp->pe->next;
	    rvp->pp = rvp->pp->next;
	  }
        else
	  while (rvp->pe->next) {
	    rvp->ps = rvp->ps->next;
	    rvp->pe = rvp->pe->next;
	    rvp->pp = rvp->pp->next;
	  }
      else
	for (i = 0; ; ) {
	  for (; i < vt_row(0) - 1 && rvp->pe->next; i++) {
	    rvp->ps = rvp->ps->next;
	    rvp->pe = (vt_mode('H')) 
	      ? sf_row(rvp->pe->next, f_mark) : rvp->pe->next;
	    if (!rvp->pp->next) break;
	    rvp->pp = rvp->pp->next;
	  }
	  if (!rvp->pe->next && pin && pin != F1) {
	    for (j = pnp->nrow; pin && pnp->nrow - j < vt_row(0) - i; )
	      alloc_pipe(0, &pin, &pnp->nrow, NULL, 0);
	    continue;
	  }
	  break;
	}
    }
    if (!vt_mode('H') || m == M_tail) disp_pp(rvp->ps, f_mark);
    rv_str(rvp, f_M);
    break;
  }
  if (pinp) *pinp = pin;
  top = pnp->top;
  pnp->top = set_top(rvp->ps);
  pnp->line = rvp->pp->f.line;
  tcflush(vt_tfd(0), TCIFLUSH);
  return i;
}

/*
 * 【機能】行単位の移動。M_up/M_kupは1行上、M_dn/M_kdnは1行下へカーソルを動かす。ページの端では
 *       画面をスクロールする(パイプ入力は必要に応じて追加で読み込む)。M_prev/M_next系は、
 *       一度ページの端へ寄せてから同じ動作をする。
 * 【C++】ContentView::moveUp; ContentView::moveDown; ContentView::scrollLineUp; ContentView::scrollLineDown (src/cpp/src/content_view.cpp)
 * 【備考】旧はカーソル移動とスクロールを同じ関数で扱う。C++はmoveUp/Down(カーソル)と
 *       scrollLineUp/Down(画面だけ)に分かれている。
 */
void move_row(int m, RV *rvp, PN *pnp, FILE **pinp, int f_M)/*usilic*/
{
  FILE *pin;
  int f_mark, top;

  pin = (f_M == M_egrep || f_M == M_fgrep || f_M == M_shell) ? NULL : *pinp;
  f_mark = is_comm(f_M, 0) || is_comm(f_M, 1);
  switch(m) {
  case M_prev:
  case M_kprev:
    if (!rvp->ps->prev) {
      while (rvp->pp->prev) {
	rvp->pp = rvp->pp->prev;
	rvp->row--;
	pnp->row--;
      }
      rv_str(rvp, f_M);
    }
    if (pnp->nrow <= vt_row(0) - 1) return;
    while (rvp->row) {
      rvp->pp = rvp->pp->prev;
      rvp->row--;
      pnp->row--;
    }
  case M_kup:
  case M_up:
    do {
      if (rvp->row) {
	rvp->row--;
	pnp->row--;
      } else if (rvp->ps->prev) {
	rvp->ps = sr_row(rvp->ps->prev, f_mark);
	rvp->pe = rvp->pe->prev;
      } else continue;
      rvp->pp = rvp->pp->prev;
    } while (0);
    rv_str(rvp, f_M);
    break;
  case M_next:
  case M_knext:
    if ((!pin || pin == F1) && !rvp->pe->next) {
      while (rvp->pp->next) {
	rvp->pp = rvp->pp->next;
	rvp->row++;
	pnp->row++;
      }
      rv_str(rvp, f_M);
    }
    if (pnp->nrow <= vt_row(0) - 1) return;
    while (rvp->row < vt_row(0) - 2) {
      rvp->pp = rvp->pp->next;
      rvp->row++;
      pnp->row++;
    }
  case M_kdn:
  case M_dn:
    do {
      if ((!pin || pin == F1) && pnp->row > pnp->nrow - 2 - (f_M == M_shell))
	continue;
      if (rvp->row < vt_row(0) - 2) {
	rvp->row++;
	pnp->row++;
      } else if (pin && pin != F1 && rvp->pe->next && !rvp->pe->next->next) {
	alloc_pipe(0, &pin, &pnp->nrow, NULL, 0);
	rvp->pe = sf_row(rvp->pe->next, f_mark);
	rvp->ps = rvp->ps->next;
      } else if (rvp->pe->next) {
	rvp->pe = sf_row(rvp->pe->next, f_mark);
	rvp->ps = rvp->ps->next;
      } else continue;
      rvp->pp = rvp->pp->next;
    } while (0);
    rv_str(rvp, f_M);
    break;
  }
  if (pinp) *pinp = pin;
  top = pnp->top;
  pnp->top = set_top(rvp->ps);
  pnp->line = rvp->pp->f.line;
  tcflush(vt_tfd(0), TCIFLUSH);
}

/*
 * 【機能】画面を1行分下へずらして(端末のsrコマンド)、空いた先頭行にppを描く。ウィンドウ表示が
 *       無効(vt_mode 'D'が偽)のときは何もしない。
 * 【C++】TerminalDisplay::scrollReverse (src/cpp/src/terminal_display.cpp)
 */
PP *sr_row(PP *pp, int f_mark)/*usilic*/
{
  if (!vt_mode('D')) return pp;
  vt_move(0, 0);
  vt_sr();
  put_str(pp, f_mark);
  return pp;
}  

/*
 * 【機能】画面を1行分上へずらして(端末のsfコマンド)、空いた最下行にppを描く。ウィンドウ表示が
 *       無効(vt_mode 'D'が偽)のときは何もしない。端末にステータス行(hs、vt_mode 'H')が
 *       有るかどうかで、カーソル位置と描く順序が変わる。
 * 【C++】TerminalDisplay::scrollForward (src/cpp/src/terminal_display.cpp)
 */
PP *sf_row(PP *pp, int f_mark)/*usilic*/
{
  if (!vt_mode('D')) return pp;
  if (vt_mode('H')) vt_move(vt_row(0) - 1, 0);
  else vt_move(vt_row(0) - 2, 0);
  if (vt_mode('H')) {
    vt_eeol();
    put_str(pp, f_mark);
    vt_sf();
  } else {
    vt_sf();
    vt_eeol();
    put_str(pp, f_mark);
  }
  return pp;
}  

/*
 * 【機能】モード(f_M)ごとに、カーソルが指す名前の開始桁の初期値を返す。lsの出力を持つモード(T_dir、
 *       アーカイブ各種)はget_col()で求め、一覧系は0、CP/MV/RM等は-1(桁の概念なし)。
 * 【C++】DirectoryEntry::nameStartColumn (src/cpp/src/directory_mode.hpp); ContentView::Line (src/cpp/src/content_view.hpp)
 * 【備考】C++では一覧の各行がhighlightStartColumn(名前の開始桁)を持つ。
 */
int init_col(int f_M, char str[], int size)/*usilic*/
{
  int col, row;

  if (size < 0) return size;
  row = vt_row(0);
  col = 0;
  switch(f_M) { 
  case M_lfile: case M_ldir: 
  case M_cut: case T_cut:
  case M_prog: 
  case M_man:
  case M_egrep: case T_grep:
  case T_locate:
  case M_hist: case T_hist:
  case T_hfile: case T_hdir: case T_hman: case T_hlocate:
  case T_hfgrep: case T_hegrep:
  case M_cmd: case T_cmd:
  case M_list: case T_list:
  case M_du: case T_du:
  case M_key:
    col = 0;
    break;
  case T_dir:
  case T_tar:
  case T_lha:
  case T_unzip:
  case T_rpm:
  case T_deb:
    col = get_col(str);
    break;
  case M_cp:
  case M_mv:
  case M_rm:
  case M_arc:
  case T_arc2:
  case T_key:
  default:
    col = -1;
    break;
  }  
  return col;
}

/*
 * 【機能】記憶していた位置(tell=行番号、1始まり)から、ページ先頭のPP(*ppp)とカーソル行(pnp->row)を
 *       復元する。総行数が1画面に収まらないときは、記憶した行が画面内に入るようtopを決める。
 * 【C++】History::positionFor (src/cpp/src/history.cpp); ContentView::setCursorIndex (src/cpp/src/content_view.cpp)
 */
int init_row(PP **ppp, PN *pnp, int tell, int etop)/*usilic*/
{
  int i, j, row;
  PP *pp;

  pp = *ppp;
  row = pnp->row;
  if (pnp->nrow > vt_row(0) - 1) {
    if (tell) {
      row = 0;
      if ((j = (pnp->top = tell - 1) > etop)) pnp->top = etop;
    } else j = 0;
    for (i = 0; i < pnp->top && pp->next; i++) pp = pp->next;
    if (j) for (i = tell - etop - 1; i > 0; i--) row++;
  } else if (tell) for (row = 0, i = tell - 1; i > 0; i--) row++;
  pnp->row = row;
  *ppp = pp;
  return row;
}

/*
 * 【機能】`ls -alF`の1行から、ファイル名が始まる桁(0始まり)を返す。月の略称(Jan〜Dec)を目印に、
 *       その13バイト後ろをファイル名の開始とする(ファイル名に空白があっても壊れない)。目印が無い
 *       行(アーカイブ一覧等)だけ、旧来の「最後の空白」で代用する。
 * 【C++】DirectoryMode::parseLsLine (src/cpp/src/directory_mode.cpp)
 * 【備考】lsの日付が常に英語形式になるよう、子プロセスにLC_ALL=Cを強制する(fork_bin)ことが前提。
 */
int get_col(char str[])/*usilic*/
{
  static char *mon[] = {"Jan","Feb","Mar","Apr","May","Jun",
			 "Jul","Aug","Sep","Oct","Nov","Dec"};
  int col, i;
  char *cp, *best, *p = alloca(strlen(str) + 1);

  strcpy(p, str);
  if ((cp = strstr(p, " -> "))) *cp = '\0';
  if ((cp = strchr(p, '\\'))) {
    while (*cp != ' ' && cp > p) cp--;
    col = cp - p + 2;
  } else {
    /* `str'は通常`ls -alF'出力の1行であり(fork_bin()は、まさに下の
       日付フィールドが常にこの固定の英語形式になるよう、その子
       プロセスにLC_ALL=Cを強制する)、以前は行全体の中の「最後の」
       空白を「ファイル名の直前」として単純に取る方法でパースして
       いた――これは、ファイル名自体に空白が含まれた瞬間に壊れる
       (Finder/QuickTimeのエクスポートによくあるパターン、
       "2024-09-14 12.13.08.mp4"のような): その内部の空白が新しい
       「最後の空白」になってしまい、ファイル名からそれより前の
       部分が静かに切り詰められる。get_col()の呼び出し元はその後、
       出鱈目なパスに対してstat()/access()(あるいはRM/MVでの選択、
       mode.cのis_write())を行い、理由の手がかりも無いまま失敗する。
       "Mon DD HH:MM "(最近のファイル)と"Mon DD  YYYY "(古い
       ファイル)は、どちらも月の略称からファイル名まで正確に13
       バイトであり、その略称は、素の空白検索とは異なり、
       信頼できるロケールに依存しない目印である――そのためまず
       それを探し、それが無い場合(`ls -alF'形式に従わないことが
       あるT_arc3のアーカイブ一覧等)だけ、旧来の最後の空白による
       ヒューリスティックへフォールバックする。 */
    for (i = 0, best = NULL; i < 12; i++) {
      char *q = p;
      while ((q = strstr(q, mon[i]))) {
	if ((q == p || q[-1] == ' ') && q[3] == ' '
	    && (int)strlen(q) > 13 && (!best || q < best))
	  best = q;
	q++;
      }
    }
    if (best) col = best - p + 14;
    else col = (cp = strrchr(p, ' ')) ? cp - p + 2 : 1;
  }
  return col - 1;
}

