#include "tcb.h"
#include "code.P"
#include <signal.h>
#include <sys/wait.h>
typedef int (CONV) (int, int, u_char*, u_char[], JIS *);
typedef void (DECODE) (u_char*, u_char[], u_char[]);

int is_man(char *man)/*tcb*/
{ /*
   * \012.\\042
   * \012.B 
   * \012.I
   * \012.aa \012.ab ... \012.zy \012.zz
   * \012.Aa \012.Ab ... \012.Zy \012.Zz 
   * \012.AA \012.AB ... \012.ZY \012.ZZ
   */
  int i, c1, c2;

  if (man[0] == '.') {
    c1 = man[1];
    c2 = man[2];
    if (c1 == '\\' && c2 == '\042') return 1;
    if ((c1 == 'B' || c1 == 'I') && c2 == ' ') return 1;
    if ((i = c1 >= 'A' && c1 <= 'Z') || (c1 >= 'a' && c1 <= 'z'))
      if ((c2 >= 'a' && c2 <= 'z') || (i && c2 >= 'A' && c2 <= 'Z'))
	if (man[3] == ' ' || man[3] == '\n') return 1;
  }
  return 0;
}

char *type_pipe(char *buff, int size, int code)/*tcb*/
{ /* print file-type (from pipe) */
  CHILD *chp;
  int pv[4];
  char *cp, type[N_line];
  FILE *fp;
  
  if ((chp = check_chp(S_file))) {
    if (!(chp = get_child(NULL, "...", s_S(S_file), "-", NULL))) return NULL;
    pipe(pv);
    if (!fork()) {
      load_signal();
      pipe(&pv[2]);
      if (!fork()) {
	close(pv[2]);
	/* check N_pipe bytes */
	fp = fdopen(pv[3], "w");
	fwrite(buff, 1, (size > N_pipe) ? N_pipe : size, fp);
	fclose(fp);
	exit(0);
      }
      dup_error();
      close(0);
      dup2(pv[2], 0);
      close(pv[2]);
      close(pv[3]);
      close(1);
      dup2(pv[1], 1);
      close(pv[1]);
      close(pv[0]);
      execvp(*chp->argv, chp->argv);
      exit(0);
    }
    close(pv[1]);
    sprintf(type, "(^%c) ", '@'-code-1);
    fp = fdopen(pv[0], "r");
    cp = &type[5];
    fgets(cp, N_pipe, fp);
    fclose(fp);
    if ((cp = strchr(cp, '\n'))) *cp = '\0';
    for (cp = &(strchr(type,':'))[1]; *cp == ' ' || *cp == '\t'; cp++);
    memmove(&type[5], cp, strlen((char*)cp) + 1);
  } else sprintf(type, "%s `%s\047", s_S(S_errc), s_S(S_file));
  cp = type;
  free(buff);
  buff = strdup((chp) ? tobo(cp, 1) : toul(cp, 1));
  return buff;
}

char *type_file(char *buff, char name[], int code)/*tcb*/
{ /* print file-type (from file) */
  CHILD *chp;
  int pv[2];
  char *cp, type[N_line];
  FILE *fp;

  if ((chp = check_chp(S_file))) {
    if (!(chp = get_child(NULL, "...", s_S(S_file), name, NULL))) return NULL;
    pipe(pv);
    if (!fork()) {
      load_signal();
      dup_error();
      close(1);
      dup2(pv[1], 1);
      close(pv[1]);
      close(pv[0]);
      execvp(*chp->argv, chp->argv);
      exit(0);
    }
    close(pv[1]);
    sprintf(type, "(^%c) ", '@'-code-1);
    fp = fdopen(pv[0], "r");
    cp = &type[5];
    fgets(cp, N_pipe, fp);
    fclose(fp);
    if ((cp = strchr(cp, '\n'))) *cp = '\0';
    for (cp = &(strchr(type,':'))[1]; *cp == ' ' || *cp == '\t'; cp++);
    memmove(&type[5], cp, strlen((char*)cp) + 1);
  } else sprintf(type, "%s `%s\047", s_S(S_errc), s_S(S_file));
  cp = type;
  if (buff) free(buff);
  save_error("%s: %s: %s.\n", s_S(S_file), name, cp);
  buff = strdup((chp) ? tobo(cp, 1) : toul(cp, 1));
  return buff;
}

u_char *check_type(u_char *buff, int *sizep, char *name, int *f_Mp, int f_chkbin, int *codep)/*tcb*/
{
  int size, oldsize, code;
  JIS jis;
  
  if (!f_chkbin) {
    *codep = C_bin;
    return buff;
  }
  oldsize = size = *sizep;
  switch((code = init_code(&size, buff, f_chkbin))) {
  case C_jis: case C_base64: case C_quote:
    jis.c = 0;
    do {
      if (code == C_base64) jis.f.e = 1;
      else if (code == C_quote) jis.f.q = 1;
      else continue;
      code = C_jis;
    } while (0);
    if (f_chkbin == 3) jis.f.i = 1;
    size = jistoeuc(size, buff, &jis);
    break;
  case C_sjis:
    size = sjistoeuc(size, &buff, oldsize, NULL);
    break;
  case C_euc_jp:
    size = euctoeuc(size, &buff, oldsize, NULL);
    break;
  default:
    if (code < 0 && f_chkbin == 1) {
      buff = (name)
	? type_file(buff, name, code) : type_pipe(buff, size, code);
      if (buff) size = strlen(buff);
      *f_Mp = T_type;
    }
    break;
  }
  if (vt_charset(0) == C_utf8
      && (code == C_jis || code == C_sjis || code == C_euc_jp || code == C_euc))
    size = euctoutf8(size, &buff);
  *codep = code;
  *sizep = size;
  return buff;
}

static int b_code(int c)
{
  if (c >= '[') c -= 'G';
  else if (c >= 'A') c -= 'A';
  else if (c >= '0') c -= ('0' - '4');
  else if (c == '+') c = '>';
  else c = '?';
  return c & 0x3f;
}

static int q_code(int c)
{
  if (c >= '0' && c <= '9') c -= '0';
  else if (c >= 'A' && c <= 'F') c -= 'A' - 10;
  else if (c >= 'a' && c <= 'f') c -= 'a' - 10;
  else c = 0;
  return c;
}

static void decode_base64(u_char *code, u_char mime[], u_char end[])/*code*/
{
  u_char *mp;
  int i, ma[4];

  for (mp = mime; mp < end; ) {
    for (i = 0; i < 4; i++) ma[i] = b_code(*mp++);
    *code++ = ((ma[0] << 2) & 0xfc) | ((ma[1] >> 4) & 0x03);
    *code++ = ((ma[1] << 4) & 0xf0) | ((ma[2] >> 2) & 0x0f);
    *code++ = ((ma[2] << 6) & 0xc0) | (ma[3] & 0x3f);
  }
  *code = 0;
}

static void decode_quote(u_char *code, u_char mime[], u_char end[])/*code*/
{
  u_char *mp;

  for (mp = mime; mp < end; mp++)
    if (mp[0] == '_') *code++ = ' ';
    else if (mp[0] != '=') *code++ = mp[0];
    else {
      mp++;
      if (mp[0] <= ' ') *code++ = mp[0];
      else {
	*code++ = (q_code(mp[0]) << 4) + q_code(mp[1]);
	mp++;
      }
    }
  *code = 0;
}

static int set_func(u_char *cp, CONV **convp, DECODE **decodep)
{
  static CONV *conv[] = {
    conv_jis,
    conv_q,
    conv_sjis,
    conv_q,
    conv_euc,
    NULL,
  };
  static DECODE *decode[] = {
    decode_base64,
    decode_quote,
    decode_base64,
    decode_quote,
    decode_base64,
    NULL,
  };
  char **s_pp;
  int i, len;
  
  for (i = 0, s_pp = &s_P(P_code)[C_base64 - C_ascii]; s_pp[i]; i++)
    if (!strncasecmp(s_pp[i], cp, strlen(s_pp[i]))) break;
  if (s_pp[i]) {
    len = strlen(s_pp[i]);
    *convp = conv[i];
    *decodep = decode[i];
  } else {
    len = 0;
    *convp = NULL;
    *decodep = NULL;
  }
  return len;
}

static int conv_q(int buff_i, int len, u_char *cp, u_char euc[], JIS *dummy)/*code*/
{
  u_char *p = cp;
  int i;
  JIS jis;

  i = len;
  switch (init_code(&len, p, 0)) {
  case C_jis:
    p = malloc(len);
    jis.c = 0;
    len = conv_jis(0, len, cp, p, &jis);
    break;
  case C_sjis:
    p = malloc(len);
    len = conv_sjis(0, i, cp, p, NULL);
    break;
  default:
    break;
  }
  if (buff_i) buff_i--;
  for (i = 0; len > 0; len--, i++) euc[buff_i++] = p[i];
  if (p != cp) free(p);
  return buff_i;
}

static int conv_jis(int buff_i, int len, u_char *cp, u_char euc[], JIS *jisp)/*code*/
{
  static int jis_i;
  int  c, f_mime, c0, c1, c2;
  u_char *cp0 = cp;

  if ((f_mime = buff_i < 0)) {
    jis_i = len; 
    buff_i = 0;
  }
  for (c0 = c1 = c2 = 0; len > 0 && buff_i < jis_i; c0 = 0) {
    switch(c = *cp) {
    case K_j:
      if (jisp->f.s) {
	jisp->f.s = 0;
	break;
      }
    case K_i:
    case K_h:
    case K_n:
    case K_o:
      break;
    case K_ec:
      cp++;
      jisp->f.s++;
      len--;
      break;
    case '=':
      if (cp[1] == '?' && f_mime
	  && decode_mime(&cp, &buff_i, &len, euc, cp0 + jis_i - 1))
	continue;
      else if ((q_code(cp[1]) << 4) + q_code(cp[2]) == K_ec) {
	cp += 3;
	jisp->f.s++;
	len -= 3;
	break;
      }
    default:
      if (c < 0x20) {
	if (jisp->f.s == 3) buff_i--;
	cp++;
	len--;
	continue;
      }
      break;
    }
    if ((c = *cp) == '=' && !jisp->f.e)
      if (jisp->f.q && !(c0 = q_code(cp[1]))) {
	cp += 2;
	len -= 2;
	if (jisp->f.i) {
	  if (jisp->f.s) jisp->f.s = 0;
	  cp--;
	  len++;
	} else if (jisp->f.s 
		   && jisp->f.s != 3
		   && *cp == '=' 
		   && (q_code(cp[1]) << 4) + q_code(cp[2]) == K_ec)
	  jisp->f.s = 0;
	continue;
      }
    switch(jisp->f.s) {
    case 0:
      if (c0) {
	if ((c = (c0 << 4) + q_code(cp[2])) == '=') ;
	else if (c == 0xa0) c &= ~0x80;
	euc[buff_i++] = c;
	cp += 3;
	len -= 3;
      } else {
	if (c == K_n) {
	  jisp->f.s = 6;
	  cp++;
	  len--;
	  continue;
	} else euc[buff_i++] = c;
	cp++;
      }
      break;
    case 1:
    case 8:
      if (c == '$') jisp->f.s = 2;
      else if (c == '(') jisp->f.s = 5;
      else if (jisp->f.s == 1) {
	len += jisp->f.s;
	jisp->f.s = 0;
	euc[buff_i++] = K_ec;
	if (buff_i < jis_i) euc[buff_i++] = c;
      }
      cp++;
      break;
    case 2:
      if (c == 'B' || c == '@') jisp->f.s = 3;
      if (cp[1] == K_ec) jisp->f.s = 0;
      cp++;
      break;
    case 3:
      if (c0) {
	if ((c = (c0 << 4) + q_code(cp[2])) == '=') {
	  c |= 0x80;
	  euc[buff_i++] = c;
	} else if (c2) {
	  euc[buff_i++] = ' ';
	  euc[buff_i++] = ' ';
	  c1 = c2 = 0;
	} else if (c >= 0x80) c2 = c;
	else euc[buff_i++] = c;
	cp += 2;
	len -= 2;
      } else if (c2) {
	euc[buff_i++] = ' ';
	c1 = c2 = 0;
      } else euc[buff_i++] = (c >= ' ') ? (c | 0x80) : c;
      cp++;
      break;
    case 4:
      if (c == '(') jisp->f.s = 5;
      cp++;
      break;
    case 5:
      if (c == 'B' || c == 'J') jisp->f.s = 0;
      else if (c == 'I') jisp->f.s = 7;
      cp++;
      break;
    case 6:
      if (c == K_o) {
	cp++;
	jisp->f.s = 0;
	len--;
	continue;
      }
    case 7:
      euc[buff_i++] = 0x8e;
      if (buff_i < jis_i) euc[buff_i++] = c | 0x80;
      cp++;
      break;
    }  
    len--;
  }
  return buff_i;
}

static int decode_mime(u_char **cpp, int *buff_ip, int *lenp, u_char euc[], u_char *end)/*code*/
{ 
  int i, len, buff_i, f_nl;
  u_char *code, *mime, *cp_b, *cp = *cpp;
  CONV *conv;
  DECODE *decode;
  JIS jis;

  buff_i = *buff_ip;
  len = *lenp;
  jis.c = 0;
  if ((i = set_func(cp, &conv, &decode))) {
    mime = &cp[i];
    if ((f_nl = (cp_b = strchr(mime, '\n')) != NULL)) {
      for (cp = mime; cp < cp_b; cp++) if (!strncmp(cp, "?=", 2)) break;
      if (cp < cp_b) cp_b = cp;
    } else cp = cp_b = end;
    code = malloc(cp_b - mime + 1);
    decode(code, mime, cp_b);
    if (*cp != '\n') cp_b += 2;
    len -= i + cp_b - mime;
    buff_i = conv(buff_i, strlen(code), code, euc, &jis);
    free(code);
    *cpp = cp_b;
    *buff_ip = buff_i;
    *lenp = len;
  }
  return i;
}

static int conv_sjis(int buff_i, int len, u_char *cp, u_char euc[], JIS *dummy)/*code*/
{
  static int euc_e;
  int c1, c2, sjis_i, f_mime, f_len = 0, f_quit = 0;
  u_char *cp0 = cp;

  if ((f_mime = buff_i < 0)) {
    euc_e = -buff_i;
    buff_i = 0;
  } else if ((f_quit = len < 0)) {
    euc_e = len = -len;
    buff_i = 0;
  } else f_len = buff_i;
  for (sjis_i = len; len > 0; len--) { 
    if (!f_len && buff_i >= euc_e) break;
    c1 = *cp;
    if (f_quit && !c1) break;
    if (is_sjis_1_byte(c1)) {
      c2 = cp[1];
      c1 = (c1 << 1) -((c1 > 0x9f) ? 0x161 : 0xe1);
      if (c2 < 0x9f) c2 = c2 - ((c2 > 0x7f) ? 0x20 : 0x1f);
      else {
	c2 = c2 - 0x7e;
	c1++;
      }
      euc[buff_i++] = c1 | 0x80;
      if (f_len || buff_i < euc_e) euc[buff_i++] = c2 | 0x80;
      cp += 2;
      len--;
    } else if (is_sjiskana(c1)) {
      euc[buff_i++] = 0x8e;
      if (f_len || buff_i < euc_e) euc[buff_i++] = c1;
      cp++;
    } else if (c1 == '=' && f_mime) {
      if (cp[1] == '?' 
	  && decode_mime(&cp, &buff_i, &len, euc, cp0 + sjis_i - 1))
	continue;
      euc[buff_i++] = c1;  
      cp++;
    } else {
      if (c1 < ' ' && !strchr(s_S(S_code), c1)) ;
      else euc[buff_i++] = c1;  
      cp++;
    }
  }
  if (f_quit) euc[buff_i] = '\0';
  return buff_i;
}

static int conv_euc(int buff_i, int len, u_char *cp, u_char euc[], JIS *dummy)/*code*/
{
  static int euc_e;
  int c, euc_i, f_mime, f_len = 0, f_quit = 0;
  u_char *cp0 = cp;

  if ((f_mime = buff_i < 0)) {
    euc_e = -buff_i;
    buff_i = 0;
  } else if ((f_quit = len < 0)) {
    euc_e = len = -len;
    buff_i = 0;
  } else f_len = buff_i;
  for (euc_i = len; len > 0; len--) { 
    if (!f_len && buff_i >= euc_e) break;
    c = *cp;
    if (f_quit && !c) break;
    if (c == '=' && f_mime) {
      if (cp[1] == '?' 
	  && decode_mime(&cp, &buff_i, &len, euc, cp0 + euc_i - 1))
	continue;
      euc[buff_i++] = c;  
      cp++;
    } else {
      if (c < ' ' && !strchr(s_S(S_code), c)) ;
      else euc[buff_i++] = c;  
      cp++;
    }
  }
  if (f_quit) euc[buff_i] = '\0';
  return buff_i;
}

int jistoeuc(int buff_i, u_char jis[], JIS *jisp)/*tcb*/
{ /* convert jis to euc */
  buff_i = conv_jis(-1, buff_i, jis, jis, jisp);
  return buff_i;
}

int sjistoeuc(int buff_i, u_char **sjisp, int sjis_i, JIS *dummy)/*tcb*/
{ /* convert sjis to euc */
  u_char *euc;

  if ((euc = malloc(buff_i + 1))) {
    buff_i = conv_sjis(-buff_i, sjis_i, *sjisp, euc, NULL);
    free(*sjisp);
    *sjisp = euc;
  } else {
    save_intr(S_errl);
    save_error("%s: %s.\n", s_S(S_tcb), s_S(S_errl));
  }
  return buff_i;
}

int euctoeuc(int buff_i, u_char **eucp, int euc_i, JIS *dummy)/*tcb*/
{ /* convert sjis to euc without allocation of memory */
  u_char *euc;

  if ((euc = malloc(buff_i + 1))) {
    buff_i = conv_euc(-buff_i, euc_i, *eucp, euc, NULL);
    free(*eucp);
    *eucp = euc;
  } else {
    save_intr(S_errl);
    save_error("%s: %s.\n", s_S(S_tcb), s_S(S_errl));
  }
  return buff_i;
}

#include "euc_uni.h"

/*
 * euctoutf8() (UTF-8 support, step 6): re-encode EUC-JP content
 * (already normalized to EUC by jistoeuc()/sjistoeuc()/euctoeuc()
 * above, or plain EUC to begin with) as UTF-8, using the JIS X 0208
 * table in euc_uni.h. Unlike sjis<->euc<->jis, which all share the
 * same underlying JIS X 0208 character set and so only need a byte
 * arithmetic remap, EUC-JP and UTF-8 encode entirely different
 * (though overlapping) character repertoires under the Unicode
 * standard, so an actual codepoint table is unavoidable.
 *
 * Called only when vt_charset(0)==C_utf8; without it, a legacy
 * EUC-JP file (e.g. this project's own README.jp from 2003) would
 * be left as raw EUC-JP bytes and rendered as mojibake by a real
 * UTF-8 terminal, since tcb's display layer has a single global
 * charset interpretation rather than per-file charset tracking.
 */
static int uni_to_utf8(u_int cp, u_char *out)
{
  if (cp < 0x80) {
    out[0] = cp;
    return 1;
  }
  if (cp < 0x800) {
    out[0] = 0xc0 | (cp >> 6);
    out[1] = 0x80 | (cp & 0x3f);
    return 2;
  }
  out[0] = 0xe0 | (cp >> 12);
  out[1] = 0x80 | ((cp >> 6) & 0x3f);
  out[2] = 0x80 | (cp & 0x3f);
  return 3;
}

static int euc_to_utf8_core(u_char *euc, int size, u_char *out)
{
  u_char *end = euc + size, *op = out;
  u_int cp;

  while (euc < end) {
    u_char c = *euc;
    if (c == 0x8e && euc + 1 < end
	&& euc[1] >= 0xa1 && euc[1] <= 0xdf) {
      cp = euc_kana_uni[euc[1] - 0xa1];
      euc += 2;
    } else if (c >= 0xa1 && c <= 0xfe && euc + 1 < end
	       && euc[1] >= 0xa1 && euc[1] <= 0xfe) {
      cp = euc_jisx0208_uni[c - 0xa1][euc[1] - 0xa1];
      euc += 2;
    } else {
      cp = c;
      euc++;
    }
    op += uni_to_utf8((cp) ? cp : '?', op);
  }
  return op - out;
}

int euctoutf8(int size, u_char **eucp)/*tcb*/
{ /* for callers (check_type()) that own *eucp and can free/replace it */
  u_char *out;
  int newsize;

  if (!(out = malloc(size * 3 + 4))) return size;
  newsize = euc_to_utf8_core(*eucp, size, out);
  free(*eucp);
  *eucp = out;
  return newsize;
}

int euctoutf8_buf(u_char *buff, int size)/*tcb*/
{ /* for callers (alloc.c's linein()) that read into a fixed-size
     shared line buffer instead of a reallocatable pointer. A
     N_buff/2-byte input can expand at most 1.5x when re-encoded as
     UTF-8, which always fits back into the N_buff-sized buffer. */
  u_char *out;
  int newsize;

  if (!(out = malloc(size * 3 + 4))) return size;
  newsize = euc_to_utf8_core(buff, size, out);
  memcpy(buff, out, newsize + 1);
  free(out);
  return newsize;
}

/*
 * UTF-8 helpers (step 1 of UTF-8 support).
 *
 * utf8_seqlen(): returns the byte length (1-4) of the UTF-8
 * sequence that starts with lead byte c. A stray/invalid
 * continuation byte returns 1 so callers never desync/run past
 * the buffer even on malformed input.
 *
 * utf8_width(): returns the display width (0, 1, or 2 columns) of
 * the codepoint encoded by the `len'-byte UTF-8 sequence at *cp.
 * This is a pragmatic subset of the East Asian Wide/Fullwidth
 * ranges (enough for Japanese/CJK text, tcb's primary use case)
 * plus a few zero-width combining-mark ranges; it is not a
 * complete Unicode East-Asian-Width table.
 */
int utf8_seqlen(u_char c)/*tcb*/
{
  if (c < 0x80) return 1;
  if ((c & 0xe0) == 0xc0) return 2;
  if ((c & 0xf0) == 0xe0) return 3;
  if ((c & 0xf8) == 0xf0) return 4;
  return 1;
}

int utf8_width(u_char *cp, int len)/*tcb*/
{
  u_int cpt;

  if (len <= 1) return 1;
  else if (len == 2) cpt = ((cp[0] & 0x1f) << 6) | (cp[1] & 0x3f);
  else if (len == 3) cpt = ((cp[0] & 0x0f) << 12) | ((cp[1] & 0x3f) << 6)
			 | (cp[2] & 0x3f);
  else cpt = ((cp[0] & 0x07) << 18) | ((cp[1] & 0x3f) << 12)
	   | ((cp[2] & 0x3f) << 6) | (cp[3] & 0x3f);

  /* zero-width combining marks */
  if ((cpt >= 0x0300 && cpt <= 0x036f)
      || (cpt >= 0x1ab0 && cpt <= 0x1aff)
      || (cpt >= 0x1dc0 && cpt <= 0x1dff)
      || (cpt >= 0x20d0 && cpt <= 0x20ff)
      || (cpt >= 0xfe20 && cpt <= 0xfe2f))
    return 0;

  /* East Asian Wide / Fullwidth (CJK, Hangul, fullwidth forms,
     common symbols/emoji) */
  if ((cpt >= 0x1100 && cpt <= 0x115f)
      || (cpt >= 0x2e80 && cpt <= 0xa4cf && cpt != 0x303f)
      || (cpt >= 0xac00 && cpt <= 0xd7a3)
      || (cpt >= 0xf900 && cpt <= 0xfaff)
      || (cpt >= 0xfe30 && cpt <= 0xfe4f)
      || (cpt >= 0xff00 && cpt <= 0xff60)
      || (cpt >= 0xffe0 && cpt <= 0xffe6)
      || (cpt >= 0x20000 && cpt <= 0x3fffd)
      || (cpt >= 0x1f300 && cpt <= 0x1faff)
      || (cpt >= 0x2600 && cpt <= 0x27bf))
    return 2;

  return 1;
}

int count_k(u_char *cp, int ci)/*tcb*/
{
  int i, f_kanji;

  if (vt_charset(0) == C_utf8) {
    int pos = 0, len;
    while (pos < ci) {
      len = utf8_seqlen(cp[pos]);
      if (pos + len > ci) return 1;	/* ci splits a multi-byte char */
      pos += len;
    }
    return 0;
  }
  for (i = f_kanji = 0; i < ci; i++)
    f_kanji = (*cp++ >= 0x80) ? !f_kanji : 0;
  return f_kanji;
}

static int count_h(u_char *cp, int ci)
{
  int i, j, h, col;

  for (col = i = 0; i < ci; i++)
    if (!(h = *cp++)) {
      col += N_line;
      break;
    } else if (h == '\t') {
      for (j = 8, h = col % 8; h; h--) j--;
      col += j;
    } else col++;
  return col;
}

static int count_i(u_char *cp, int col)
{
  int ci;

  for (ci = 0; count_h(cp, ci) < col; ci++) ;
  return ci;
}

/*
 * is_utf8() (step 2 of UTF-8 support): pragmatic well-formedness
 * check used to auto-detect UTF-8 file content. Returns true only
 * if every multi-byte lead byte is followed by the right number of
 * valid continuation bytes (0x80-0xbf) and at least one multi-byte
 * sequence was seen (so plain ASCII text, which is valid but
 * ambiguous UTF-8, falls through to the existing C_ascii handling
 * instead of being force-tagged C_utf8). A sequence truncated by
 * the end of `buff' (e.g. a preview-sized read cut mid-character)
 * is tolerated rather than rejected.
 */
static int is_utf8(u_char *buff, int size)
{
  int i, len, j, f_multi;

  for (i = f_multi = 0; i < size; i += len) {
    if (buff[i] < 0x80) { len = 1; continue; }
    if (buff[i] < 0xc2 || buff[i] > 0xf4) return 0;
    len = utf8_seqlen(buff[i]);
    if (i + len > size) break;
    for (j = 1; j < len; j++)
      if ((buff[i+j] & 0xc0) != 0x80) return 0;
    f_multi = 1;
  }
  return f_multi;
}

int init_code(int *sizep, u_char *buff, int f_chkbin)/*tcb*/
{
  int i, code, size, c1, c2, c3, c4, c5, kn[3];
  int f_bin; /* for an unexpected control-character in JIS-code */
  u_char *end;

  if (vt_charset(0) == C_utf8 && is_utf8(buff, *sizep)) return C_utf8;

  kn[0] = kn[1] = kn[2] = 0;
  c1 = c4 = c5 = f_bin = 0;
  size = *sizep;
  code = C_ascii;
  end = buff + size;
  for (; buff < end; buff++) {
    if (!c1 && (c1 = buff[0]) == '\n') {
      c1 = 0;
      continue;
    }
    if (!f_bin && *s_S(S_code) && (f_chkbin == 1 || f_chkbin == 3)
	&& (!c1 || (c1 < ' ' && !strchr(s_S(S_code), c1)))) f_bin = -(c1+1);
    else if (c1 == K_h && buff < end - 1)
      switch(buff[1]) {
      case 0:
	f_bin = -1;
	break;
      case '\n':
      case '\r':
	if (buff[-1] == ' ') break;
	f_bin = -(K_h+1);
	break;
      case K_h:
	if (buff < end - 2 && buff[2] == K_h) f_bin = -(K_h+1);
	break;
      }
    if (vt_charset(0) == C_ascii && c5) {
      c1 = 0;
      continue;
    }
    if (is_sjiskana(c1)) kn[2]++; /* count sjis-kana */
    if (buff == end - 1) break;
    c2 = buff[1];
    if (is_sjis_1_byte(c1) && is_sjis_2_byte(c2)) {
      if (f_bin) return f_bin;
      kn[0]++;
      buff++;
      i++;
    } else 
      if ((is_euckana_1_byte(c1) && is_euckana_2_byte(c2)) ||
	  (is_euc_1_byte(c1) && is_euc_2_byte(c2))) {
	if (f_bin) return f_bin;
	kn[1]++;
	buff++;
	i++;
	if (is_sjiskana(c2)) kn[2]++; /* sjis-kana sequence ? */
      } else if (c1 == K_ec)
	if (buff >= end - 2) break;
	else {
	  c3 = buff[2];
	  if (c2 == '$' && (c3 == 'B' || c3 == '@')) c5 = C_jis;
	  else if (c2 == '(' && (c3 == 'B' || c3 == 'J') && c5 == C_jis) 
	    return C_jis;
	  else if (c5) c5 = 0;
	}
      else if (!kn[0] && !kn[1]) {
	if (!strncmp(buff, "=?", 2) 
	    && buff < end - strlen(s_P(P_code)[C_base64 - C_ascii])) {
	  if (!strncasecmp(buff, s_P(P_code)[C_base64 - C_ascii]
			   , strlen(s_P(P_code)[C_base64 - C_ascii]))) 
	    c5 = C_base64;
	  else if (!strncasecmp(buff, s_P(P_code)[C_quote - C_ascii]
				, strlen(s_P(P_code)[C_quote - C_ascii]))) 
	    c5 = C_quote;
	  else if (!strncasecmp(buff, s_P(P_code)[C_shift_jis - C_ascii]
				, strlen(s_P(P_code)[C_shift_jis - C_ascii]))) 
	    c5 = C_sjis;
	  else if (!strncasecmp(buff, s_P(P_code)[C_euc_jp - C_ascii]
				, strlen(s_P(P_code)[C_euc_jp - C_ascii]))) 
	    c5 = C_euc_jp;
	} else if (c1 == '=' && buff < end - 3
		   && ((q_code(buff[1]) << 4) + q_code(buff[2])) == K_ec
		   && (buff[3] == '$' || buff[3] == '(')) 
	  c5 = C_quote;
      } else if (!f_bin) {
	if (c1 == K_n) c4 = C_jis;
	else if (c1 == K_o && c4 == C_jis) return C_jis;
      }
    c1 = 0;
  }
  if (c5) return (vt_charset(0) == C_ascii) ? C_ascii : c5;
  if (f_bin) return f_bin;
  if (vt_charset(0) == C_ascii || (!kn[0] && !kn[1])) code = C_ascii;
  else if (kn[0] >= kn[1]) {
    code = C_sjis;
    size += kn[2]; /* kn[2]: euc-kana */
  } else code = C_euc;
  *sizep = size;
  return code;
}

void make_map(RV *rvp, int nlen)/*tcb*/
{
  static int key_copy[] = {
    M_up, M_dn, M_mark, M_kprev, M_knext, 
    M_kup, M_kdn, M_head, M_tail, M_clear, 
    M_forw, M_back, M_kforw, M_kback, M_prev, 
    M_next, 0
  };
  int c, i, j, h, m, f_copy, f_kanji, f_sign;
  int row, rowe, col, col0, ci, c0;
  PP *pp = rvp->ps;
  u_char *p, *p1, *cp, **cpp;

  cpp = malloc(sizeof(u_char*) * (vt_row(0) - 1));
  for (i = 0; i < vt_row(0) - 1; i++) 
    cpp[i] = calloc(1, vt_col(0) + 1);
  for (j = 0; j < vt_row(0) - 1; j++) {
    if (!pp) break;
    cp = pp->cs;
    p = cpp[j];
    for (i = h = 0, c = 1; c && i < vt_col(0); ) {
      if (cp > pp->cs + pp->f.len) c = 0;
      else 
	switch((c = *cp)) {
	case 0:
	  break;
	case '\n':
	case '\r':
	  c = ' ';
	  break;
	case '_':
	  if (cp[1] == K_h) {
	    cp += 2;
	    continue;
	  }
	  break;
	case K_h:
	  if (cp[1] == K_h) cp += 4;
	  else if (cp[1] >= 0x80) cp += 3;
	  else cp += 2;
	  continue;
	default:
	  break;
	}
      *p++ = c;
      i++;
      cp++;
      if (cp >= pp->cs + pp->f.len) *p = '\0';
    }
    if (count_k(cpp[j], vt_col(0))) cpp[j][vt_col(0)-1] = '\0';
    if (pp) pp = pp->next;
  }
  disp_copy(cpp, nlen);
  rowe = j - 1;
  row = rvp->row;
  vt_move(row, 0);
  col = ci = 0;
  col0 = c0 = 0;
  f_copy = f_kanji = c = h = 0;
  f_sign = 1;
  p = calloc(1, vt_col(0) + 1);
  p1 = malloc(vt_col(0) + 1);
  while (1) {
    m = read_m(F11, M_copy, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    if (m < N_char && !strint(key_copy, m)) break;
    switch(m) {
    case 0:
      continue;
    case M_clear:
      disp_copy(cpp, nlen);
      vt_move(row, col);
      break;
    case M_head: case M_back: case M_kback: case M_prev:
      row = 0;
      col = 0;
      vt_move(row, col);
      ci = 0;
      break;
    case M_tail: case M_forw: case M_kforw: case M_next:
      row = rowe;
      col = strlen(cpp[row]) - 1;
      vt_move(row, col);
      ci = count_i(cpp[row], col);
      if (count_k(cpp[row], ci)) {
	if (ci < vt_col(0)) ci++;
	else ci--;
	col = count_h(cpp[row], ci);
      }
      break;
    case M_up: /* ^P */
    case M_kup: /* up-arrow */
      if (!f_copy && row) vt_move(--row, col);
      ci = count_i(cpp[row], col);
      if (count_k(cpp[row], ci)) {
	if (ci < vt_col(0)) ci += f_sign;
	else ci--;
	col = count_h(cpp[row], ci);
	f_sign = -f_sign;
      }
      break;
    case M_dn: /* ^N */
    case M_kdn: /* down-arrow */
      if (!f_copy && row < rowe) vt_move(++row, col);
      ci = count_i(cpp[row], col);
      if (count_k(cpp[row], ci)) {
	if (ci < vt_col(0)) ci += f_sign;
	else ci--;
	col = count_h(cpp[row], ci);
	f_sign = -f_sign;
      }
      break;
    case K_a + N_char: /* ^A */
      vt_endstand();
      vt_move(row, 0);
      vt_printf("%s", cpp[row]);
      ci = col = 0;
      break;
    case K_e + N_char: /* ^E */
      vt_endstand();
      vt_move(row, 0);
      vt_printf("%s", cpp[row]);
      col = strlen(cpp[row]);
      vt_move(row, col);
      ci = count_i(cpp[row], col);
      if (count_k(cpp[row], ci)) {
	if (ci < vt_col(0)) ci++;
	else ci--;
	col = count_h(cpp[row], ci);
      }
      break;
    case K_b + N_char: /* ^B */
    case M_kprev: /* left-arrow */
      if (col) {
	if (ci && !(col % 8) && cpp[row][ci-1] == '\t') {
	  ci--;
	  col = count_h(cpp[row], ci);
	  vt_move(row, col);
	} else {
	  vt_move(row, --col);
	  ci--;
	  if (count_k(cpp[row], col)) {
	    col--;
	    ci--;
	  }
	}
      }
      break;
    case K_f + N_char: /* ^F */
    case M_knext: /* right-arrow */
      if (cpp[row][ci] == '\t') {
	for (i = 8, h = col % 8; h; h--) i--;
	col += i;
	vt_move(row, col);
	ci++;
      } else if (cpp[row][ci] && col < vt_col(0)) {
	if (col > vt_col(0) - 1) vt_move(row, vt_col(0) - 1);
	else vt_move(row, col + 1);
	col++;
	ci++;
	if (count_k(cpp[row], ci)) {
	  col++;
	  ci++;
	}
      }
      break;
    case M_mark:
      if ((f_copy = !f_copy)) vt_move(row, col); 
      else {
	vt_endstand();
	if ((c = (col > col0) ? col0 : col) > vt_col(0) - 1) 
	  vt_move(row, vt_col(0) - 1);
	else vt_move(row, c);
	vt_printf("%s", p);
	if (c == col) 
	  if (c > vt_col(0) - 1) vt_move(row, vt_col(0) - 1);
	  else vt_move(row, c);
	else vt_flush();
	*p = '\0';
      }
      col0 = col;
      c0 = ci;
      continue;
      break;
    default:
      break;
    }
    if (f_copy) {
      if (ci > c0) {
	memcpy(p, &cpp[row][c0], ci - c0);
	p[ci - c0] = '\0';
	strcpy(p1, &cpp[row][ci]);
	vt_move(row, col0);
	vt_stand();
	vt_printf("%s", p);
	vt_endstand();
	vt_printf("%s", p1);
      } else {
	memcpy(p1, cpp[row], ci);
	p1[ci] = '\0';
	i = count_k(cpp[row], c0 + 1);
	memcpy(p, &cpp[row][ci], c0 - ci + 1 + i);
	p[c0 - ci + 1 + i] = '\0';
	vt_move(row, 0);
	vt_endstand();
	vt_printf("%s", p1);
	vt_stand();
	vt_printf("%s", p);
      }
    }
    if (col > vt_col(0) - 1) vt_move(row, vt_col(0) - 1);
    else vt_move(row, col);
    vt_flush();
  }
  if (*p) strcpy(rvp->copy, p);
  for (i = 0; i < vt_row(0) - 1; i++) free(cpp[i]);
  free(cpp);
  strcpy(p, mtoa(M_bl));
  if (m != M_bl) strcat(p, mtoa(m));
  strcat(p, mtoa(M_clear));
  read_m(NULL, 2, 0, p, NULL, NULL, NULL, NULL, NULL, NULL);
  free(p);
  free(p1);
}

int toeuc(u_char sjis[])/*tcb*/
{ /* convert sjis to euc without allocation of memory */
  int buff_i;
  u_char *euc, *p;

  if (!(buff_i = strlen(sjis) * 2)) return 0;
  p = strchr(sjis, '\n');
  euc = alloca(buff_i);
  buff_i = conv_sjis(0, -buff_i, sjis, euc, NULL);
  strcpy(sjis, euc);
  if (p && !strchr(sjis, '\n')) {
    strcat(sjis, "\n"); 
    buff_i++;
  }
  return buff_i;
}

