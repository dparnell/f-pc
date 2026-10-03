/* seed.c -- bootstrap ("seed") interpreter and compiler for the F-PC VM.
 *
 * The seed builds the dictionary in F-PC's layout and provides just enough
 * of an outer interpreter and compiler to load the ported kernel source,
 * whose own definitions then shadow the seed's (kernel-design.md 4.3).
 *
 * Header layout (HEAD region), all cells little-endian, possibly unaligned:
 *   +0  VFA  cell  LOADLINE at definition time
 *   +4  LFA  cell  address of the previous LFA in this hash thread (0 = end)
 *   +8  NFA  byte  count | $80 (| $40 immediate), then the name chars,
 *                  last char | $80
 *   then     cell  CFA (address of the code field in CODE space)
 * A vocabulary body is NTHREADS thread cells (each -> latest LFA) followed
 * by a voc-link cell; CONTEXT / CURRENT hold the address of the threads.
 */
#include "vm.h"
#include "seed.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct infile {
    FILE *f;
    char path[1024];
    ucell line;
    int stop;                   /* \S seen */
    struct infile *prev;
} infile_t;

typedef struct seed {
    ucell context;              /* CONTEXT array (NVOCS cells)              */
    ucell forth;                /* FORTH vocabulary threads                 */
    ucell ct_voc, ct_2const;    /* DOES> handlers for VOCABULARY, 2CONSTANT */
    ucell xt[T_NBUILTIN];       /* code fields of the built-ins             */
    infile_t *in;               /* current source file, NULL = terminal     */
    int interactive;
    char **paths; int npaths;
    ucell builtin_limit;        /* code fields below this came from the VM  */
} seed_t;

#define SEED(vm) ((seed_t *)(vm)->seed)
#define XT(id)   (SEED(vm)->xt[T_##id])

/* ---- dictionary space ----------------------------------------------------- */
static ucell here(vm_t *vm) { return uv(vm, U_DP); }
static void allot(vm_t *vm, cell n)
{
    ucell h = here(vm) + (ucell)n;
    if (h < vm->code_base || h > vm->code_end) vm_throw(vm, E_DSTACK_OVER, "Dictionary full");
    uv_set(vm, U_DP, h);
}
static void comma(vm_t *vm, ucell v) { ucell h = here(vm); allot(vm, 4); wr32(vm, h, v); }
static void ccomma(vm_t *vm, ucell v) { ucell h = here(vm); allot(vm, 1); wr8(vm, h, v); }
/* advance HERE to a cell boundary without writing: WORD's buffer is at HERE */
static void align(vm_t *vm) { allot(vm, (cell)(aligned(here(vm)) - here(vm))); }

static ucell xhere(vm_t *vm) { return sv(vm, SV_XDP); }
static void xcomma(vm_t *vm, ucell v)
{
    ucell x = xhere(vm);
    if (x + 4 > vm->list_end) vm_throw(vm, E_DSTACK_OVER, "List space full");
    wr32(vm, x, v);
    sv_set(vm, SV_XDP, x + 4);
}
static void xccomma(vm_t *vm, ucell v)
{
    ucell x = xhere(vm);
    if (x + 1 > vm->list_end) vm_throw(vm, E_DSTACK_OVER, "List space full");
    wr8(vm, x, v);
    sv_set(vm, SV_XDP, x + 1);
}
static void xalign(vm_t *vm) { while (xhere(vm) & 3) xccomma(vm, 0); }

static ucell yhere(vm_t *vm) { return sv(vm, SV_YDP); }
static void ycomma(vm_t *vm, ucell v)
{
    ucell y = yhere(vm);
    if (y + 4 > vm->head_end) vm_throw(vm, E_DSTACK_OVER, "Head space full");
    wr32(vm, y, v);
    sv_set(vm, SV_YDP, y + 4);
}
static void yccomma(vm_t *vm, ucell v)
{
    ucell y = yhere(vm);
    if (y + 1 > vm->head_end) vm_throw(vm, E_DSTACK_OVER, "Head space full");
    wr8(vm, y, v);
    sv_set(vm, SV_YDP, y + 1);
}

/* compile a counted string inline in list space, cell-padded */
static void xstring(vm_t *vm, const uint8_t *s, ucell n)
{
    if (n > 255) n = 255;
    xccomma(vm, n);
    for (ucell i = 0; i < n; i++) xccomma(vm, s[i]);
    xalign(vm);
}

/* ---- headers ---------------------------------------------------------------- */
static ucell hash_thread(const uint8_t *s, ucell n, ucell voc)
{
    n &= 31;
    ucell c0 = n > 0 ? s[0] & 0x7F : ' ', c1 = n > 1 ? s[1] & 0x7F : ' ';
    return voc + (((c0 * 2 + c1) * 2 + n) & (NTHREADS - 1)) * CELL;
}

static ucell nfa_cfa(vm_t *vm, ucell nfa) { return rd32(vm, nfa + 1 + (rd8(vm, nfa) & 31)); }

/* name in a header: copy out without delimiter bits */
static int nfa_name(vm_t *vm, ucell nfa, char *buf)
{
    int n = rd8(vm, nfa) & 31;
    for (int i = 0; i < n; i++) buf[i] = (char)(rd8(vm, nfa + 1 + (ucell)i) & 0x7F);
    buf[n] = 0;
    return n;
}

static ucell voc_find(vm_t *vm, ucell voc, const uint8_t *s, ucell n)
{
    ucell lfa = rd32(vm, hash_thread(s, n, voc));
    while (lfa) {
        ucell nfa = lfa + 4;
        if ((rd8(vm, nfa) & 31) == n) {
            ucell i;
            for (i = 0; i < n; i++)
                if ((rd8(vm, nfa + 1 + i) & 0x7F) != s[i]) break;
            if (i == n) return nfa;
        }
        lfa = rd32(vm, lfa);
    }
    return 0;
}

/* search order; returns nfa or 0 */
static ucell search(vm_t *vm, const uint8_t *s, ucell n)
{
    ucell ctx = SEED(vm)->context;
    for (int i = 0; i < NVOCS; i++) {
        ucell v = rd32(vm, ctx + (ucell)i * 4);
        if (!v) continue;
        int dup = 0;
        for (int j = 0; j < i; j++) if (rd32(vm, ctx + (ucell)j * 4) == v) dup = 1;
        if (dup) continue;
        ucell nfa = voc_find(vm, v, s, n);
        if (nfa) return nfa;
    }
    return 0;
}

/* create a header for a name; the CFA field is set to cfa */
static ucell make_header(vm_t *vm, const uint8_t *s, ucell n, ucell cfa)
{
    if (n == 0) vm_throw(vm, E_UNDEFINED, "Name expected");
    if (n > 31) vm_throw(vm, E_RANGE, "Name TOO LONG, > 31 chars!");
    if (sv(vm, SV_WARNING) && search(vm, s, n)) {
        vm_emit(vm, '\n');
        vm_type(vm, s, n);
        vm_types(vm, " isn't unique ");
    }
    ucell voc = sv(vm, SV_CURRENT);
    ucell thread = hash_thread(s, n, voc);
    ycomma(vm, sv(vm, SV_LOADLINE));            /* VFA */
    ucell lfa = yhere(vm);
    ycomma(vm, rd32(vm, thread));               /* LFA */
    ucell nfa = yhere(vm);
    yccomma(vm, n | 0x80);
    for (ucell i = 0; i < n; i++) yccomma(vm, s[i] | (i == n - 1 ? 0x80 : 0));
    ycomma(vm, cfa);
    wr32(vm, thread, lfa);
    sv_set(vm, SV_LAST, nfa);
    return nfa;
}

static void hide(vm_t *vm)
{
    ucell nfa = sv(vm, SV_LAST);
    if (!nfa) return;
    char name[32]; int n = nfa_name(vm, nfa, name);
    ucell thread = hash_thread((uint8_t *)name, (ucell)n, sv(vm, SV_CURRENT));
    if (rd32(vm, thread) == nfa - 4) wr32(vm, thread, rd32(vm, nfa - 4));
}
static void reveal(vm_t *vm)
{
    ucell nfa = sv(vm, SV_LAST);
    if (!nfa) return;
    char name[32]; int n = nfa_name(vm, nfa, name);
    ucell thread = hash_thread((uint8_t *)name, (ucell)n, sv(vm, SV_CURRENT));
    wr32(vm, thread, nfa - 4);
}

/* walk every vocabulary; return the nfa whose CFA is cfa, or 0 */
static ucell cfa_to_nfa(vm_t *vm, ucell cfa)
{
    for (ucell link = sv(vm, SV_VOCLINK); link; link = rd32(vm, link)) {
        ucell voc = link - NTHREADS * CELL;
        for (int t = 0; t < NTHREADS; t++)
            for (ucell lfa = rd32(vm, voc + (ucell)t * 4); lfa; lfa = rd32(vm, lfa))
                if (nfa_cfa(vm, lfa + 4) == cfa) return lfa + 4;
    }
    return 0;
}

/* ---- parsing ------------------------------------------------------------------ */
/* WORD: parse from TIB, leave a counted string at HERE followed by a blank */
static ucell parse_word(vm_t *vm, int delim)
{
    ucell tib = sv(vm, SV_TICKTIB), n = sv(vm, SV_NTIB), in = sv(vm, SV_TOIN);
    if (in > n) in = n;
#define ISDELIM(c) (delim == ' ' ? (c) <= ' ' : (c) == (ucell)delim)
    while (in < n && ISDELIM(rd8(vm, tib + in))) in++;
    ucell start = in;
    sv_set(vm, SV_TOINWORD, start);
    while (in < n && !ISDELIM(rd8(vm, tib + in))) in++;
    ucell len = in - start;
    if (in < n) in++;                       /* skip the delimiter */
#undef ISDELIM
    sv_set(vm, SV_TOIN, in);
    if (len > 255) len = 255;
    ucell h = here(vm);
    vm_ptr(vm, h, len + 2);
    wr8(vm, h, len);
    memmove(vm->mem + h + 1, vm->mem + tib + start, len);
    wr8(vm, h + 1 + len, ' ');
    return h;
}

/* PARSE: ( c -- a n ), no leading skip */
static void parse_delim(vm_t *vm, int delim, ucell *a, ucell *len)
{
    ucell tib = sv(vm, SV_TICKTIB), n = sv(vm, SV_NTIB), in = sv(vm, SV_TOIN);
    if (in > n) in = n;
    ucell start = in;
    while (in < n && rd8(vm, tib + in) != (ucell)delim) in++;
    *a = tib + start; *len = in - start;
    if (in < n) in++;
    sv_set(vm, SV_TOIN, in);
}

static void upcase_counted(vm_t *vm, ucell a)
{
    ucell n = rd8(vm, a);
    for (ucell i = 1; i <= n; i++) {
        ucell c = rd8(vm, a + i);
        if (c >= 'a' && c <= 'z') wr8(vm, a + i, c - 32);
    }
}

/* parse the next name, uppercased when CAPS is on */
static ucell parse_name(vm_t *vm)
{
    ucell w = parse_word(vm, ' ');
    if (sv(vm, SV_CAPS)) upcase_counted(vm, w);
    return w;
}

/* ---- numbers -------------------------------------------------------------------- */
static int digit_val(int c, int base)
{
    int v;
    if (c >= '0' && c <= '9') v = c - '0';
    else if (c >= 'A' && c <= 'Z') v = c - 'A' + 10;
    else if (c >= 'a' && c <= 'z') v = c - 'a' + 10;
    else return -1;
    return v < base ? v : -1;
}

/* convert digits in base; '.' marks a double. Returns 1 on success. */
static int conv(const uint8_t *s, int n, int base, udcell *out, int *dpl)
{
    int neg = 0, i = 0, any = 0;
    udcell d = 0;
    *dpl = -1;
    if (i < n && s[i] == '-') { neg = 1; i++; }
    for (; i < n; i++) {
        if (s[i] == '.') { *dpl = 0; continue; }
        int v = digit_val(s[i], base);
        if (v < 0) return 0;
        d = d * (udcell)base + (udcell)v;
        any = 1;
        if (*dpl >= 0) (*dpl)++;
    }
    if (!any) return 0;
    *out = neg ? (udcell)0 - d : d;
    return 1;
}

/* F-PC number syntax: digits in BASE, then $hex, nnnH, nnnB, 'c' */
static int number_q(vm_t *vm, const uint8_t *s, int n, udcell *d, int *isdouble)
{
    int dpl, base = (int)uv(vm, U_BASE);
    int ok = 0;
    if (base < 2 || base > 36) base = 10;
    if (n == 3 && s[0] == '\'' && s[2] == '\'') { *d = s[1]; dpl = -1; ok = 1; }
    else if (conv(s, n, base, d, &dpl)) ok = 1;
    else if (n > 1 && s[0] == '$' && conv(s + 1, n - 1, 16, d, &dpl)) ok = 1;
    else if (n > 1 && (s[n - 1] == 'H' || s[n - 1] == 'h') && conv(s, n - 1, 16, d, &dpl)) ok = 1;
    else if (n > 1 && (s[n - 1] == 'B' || s[n - 1] == 'b') && conv(s, n - 1, 2, d, &dpl)) ok = 1;
    if (!ok) return 0;
    sv_set(vm, SV_DPL, (ucell)dpl);
    *isdouble = dpl >= 0;
    if (!*isdouble) *d = (udcell)(dcell)(cell)(ucell)*d;    /* sign-extend single */
    return 1;
}

/* ---- compiling helpers ---------------------------------------------------------- */
static void compile_xt(vm_t *vm, ucell xt) { xcomma(vm, xt); }
static void compile_lit(vm_t *vm, ucell v) { xcomma(vm, XT(LIT)); xcomma(vm, v); }
static int compiling(vm_t *vm) { return sv(vm, SV_STATE) != 0; }
static void need_compiling(vm_t *vm)
{
    if (!compiling(vm)) vm_throw(vm, E_COMPILE_ONLY, "Compilation only, use in definition");
}

/* control-flow pairs: ( flag addr ) like F-PC's ?>MARK */
static void q_mark_fwd(vm_t *vm) { push(vm, TRUE_F); push(vm, xhere(vm)); xcomma(vm, 0); }
static void q_mark_back(vm_t *vm) { push(vm, TRUE_F); push(vm, xhere(vm)); }
static void q_resolve_fwd(vm_t *vm)
{
    ucell a = pop(vm);
    if (!pop(vm)) vm_throw(vm, E_ABORTQ, "Conditionals Wrong");
    wr32(vm, a, xhere(vm));
}
static void q_resolve_back(vm_t *vm)
{
    ucell a = pop(vm);
    if (!pop(vm)) vm_throw(vm, E_ABORTQ, "Conditionals Wrong");
    xcomma(vm, a);
}
static void two_swap(vm_t *vm)
{
    ucell d = pop(vm), c = pop(vm), b = pop(vm), a = pop(vm);
    push(vm, c); push(vm, d); push(vm, a); push(vm, b);
}

/* ---- defining helpers ------------------------------------------------------------- */
/* parse a name and build a header + code field with token ct */
static ucell define(vm_t *vm, ucell ct)
{
    ucell w = parse_name(vm);
    uint8_t name[32];
    ucell n = rd8(vm, w);
    if (n > 31) n = 31;
    memcpy(name, vm->mem + w + 1, n);
    align(vm);
    ucell cfa = here(vm);
    make_header(vm, name, n, cfa);
    comma(vm, ct);
    return cfa;
}

/* ' : parse a name and find it */
static ucell tick(vm_t *vm, int *imm)
{
    ucell w = parse_name(vm);
    ucell n = rd8(vm, w);
    ucell nfa = search(vm, vm->mem + w + 1, n);
    if (!nfa) vm_throw(vm, E_UNDEFINED, "%.*s <- What?", (int)n, (char *)vm->mem + w + 1);
    if (imm) *imm = (rd8(vm, nfa) & 0x40) != 0;
    return nfa_cfa(vm, nfa);
}

/* ---- input sources ------------------------------------------------------------------ */
static int read_line(FILE *f, char *buf, int max)
{
    int n = 0, c;
    for (;;) {
        c = fgetc(f);
        if (c == EOF || c == 0x1A) { if (n == 0) return -1; break; }
        if (c == '\n') break;
        if (c == '\r') continue;
        if (n < max) buf[n++] = (char)c;
    }
    buf[n] = 0;
    return n;
}

static void set_tib(vm_t *vm, const char *s, int n)
{
    if (n > TIB_SIZE) n = TIB_SIZE;
    sv_set(vm, SV_TICKTIB, vm->tib);
    memcpy(vm->mem + vm->tib, s, (size_t)n);
    sv_set(vm, SV_NTIB, (ucell)n);
    sv_set(vm, SV_TOIN, 0);
}

/* next line from the current source into TIB; 0 at end */
static int refill(vm_t *vm)
{
    seed_t *sd = SEED(vm);
    char buf[TIB_SIZE + 1];
    int n;
    if (sd->in) {
        if (sd->in->stop) return 0;
        n = read_line(sd->in->f, buf, TIB_SIZE);
        if (n < 0) return 0;
        sd->in->line++;
        sv_set(vm, SV_LOADLINE, sd->in->line);
    } else {
        vm->host->flush(vm->host);
        n = read_line(stdin, buf, TIB_SIZE);
        if (n < 0) return 0;
    }
    set_tib(vm, buf, n);
    return 1;
}

/* ---- the interpreter ------------------------------------------------------------------- */
static void interpret(vm_t *vm)
{
    for (;;) {
        if (SEED(vm)->in && SEED(vm)->in->stop) return;
        ucell w = parse_name(vm);
        ucell n = rd8(vm, w);
        if (n == 0) return;
        ucell nfa = search(vm, vm->mem + w + 1, n);
        if (nfa) {
            ucell cfa = nfa_cfa(vm, nfa);
            if (compiling(vm) && !(rd8(vm, nfa) & 0x40)) {
                compile_xt(vm, cfa);
            } else {
                vm_execute(vm, cfa);
                if (vm->sp > vm->sp0) {
                    vm->sp = vm->sp0;
                    vm_throw(vm, E_DSTACK_UNDER, "Stack Empty");
                }
            }
            continue;
        }
        udcell d; int isdouble;
        if (number_q(vm, vm->mem + w + 1, (int)n, &d, &isdouble)) {
            if (compiling(vm)) {
                if (isdouble) { compile_lit(vm, (ucell)d); compile_lit(vm, (ucell)(d >> 32)); }
                else compile_lit(vm, (ucell)d);
            } else {
                if (isdouble) dpush(vm, d); else push(vm, (ucell)d);
            }
            continue;
        }
        vm_throw(vm, E_UNDEFINED, "%.*s <- What?", (int)n, (char *)vm->mem + w + 1);
    }
}

/* ---- files ----------------------------------------------------------------------------------- */
/* open dir/name, matching each path component case-insensitively */
static FILE *ci_open(const char *path, char *found, size_t foundsz)
{
    FILE *f = fopen(path, "rb");
    if (f) { snprintf(found, foundsz, "%s", path); return f; }
    char cur[1024] = "";
    const char *p = path;
    if (*p == '/') { strcpy(cur, "/"); while (*p == '/') p++; }
    while (*p) {
        const char *e = strchr(p, '/');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        char comp[256];
        if (len >= sizeof comp) return NULL;
        memcpy(comp, p, len); comp[len] = 0;
        DIR *d = opendir(*cur ? cur : ".");
        if (!d) return NULL;
        struct dirent *de; int hit = 0;
        while ((de = readdir(d)))
            if (strcasecmp(de->d_name, comp) == 0) { hit = 1; break; }
        size_t cl = strlen(cur);
        if (hit) snprintf(cur + cl, sizeof cur - cl, "%s%s", (cl && cur[cl - 1] != '/') ? "/" : "", de->d_name);
        closedir(d);
        if (!hit) return NULL;
        p += len;
        while (*p == '/') p++;
    }
    f = fopen(cur, "rb");
    if (f) snprintf(found, foundsz, "%s", cur);
    return f;
}

static FILE *open_source(vm_t *vm, const char *name, char *found, size_t foundsz)
{
    seed_t *sd = SEED(vm);
    char nm[512], try[1600];
    size_t i;
    snprintf(nm, sizeof nm, "%s", name);
    for (i = 0; nm[i]; i++) if (nm[i] == '\\') nm[i] = '/';
    if (nm[0] && nm[1] == ':') memmove(nm, nm + 2, strlen(nm + 2) + 1);    /* drop drive */
    const char *base = strrchr(nm, '/');
    int hasext = strchr(base ? base : nm, '.') != NULL;
    for (int pass = 0; pass < 2; pass++) {
        char n2[520];
        if (pass == 1) { if (hasext) break; snprintf(n2, sizeof n2, "%s.SEQ", nm); }
        else snprintf(n2, sizeof n2, "%s", nm);
        FILE *f = ci_open(n2, found, foundsz);
        if (f) return f;
        if (n2[0] == '/') continue;
        if (sd->in) {                              /* relative to the current file */
            char dir[1024]; snprintf(dir, sizeof dir, "%s", sd->in->path);
            char *sl = strrchr(dir, '/');
            if (sl) { *sl = 0; snprintf(try, sizeof try, "%s/%s", dir, n2);
                      if ((f = ci_open(try, found, foundsz))) return f; }
        }
        for (int k = 0; k < sd->npaths; k++) {
            snprintf(try, sizeof try, "%s/%s", sd->paths[k], n2);
            if ((f = ci_open(try, found, foundsz))) return f;
        }
    }
    return NULL;
}

void seed_include(vm_t *vm, const char *name)
{
    seed_t *sd = SEED(vm);
    infile_t *in = calloc(1, sizeof *in);
    in->f = open_source(vm, name, in->path, sizeof in->path);
    if (!in->f) { free(in); vm_throw(vm, E_FILE, "%s: file not found", name); }
    /* save the caller's input state */
    uint8_t savetib[TIB_SIZE];
    memcpy(savetib, vm->mem + vm->tib, TIB_SIZE);
    ucell ntib = sv(vm, SV_NTIB), toin = sv(vm, SV_TOIN), line = sv(vm, SV_LOADLINE);
    ucell ttib = sv(vm, SV_TICKTIB);
    in->prev = sd->in;
    sd->in = in;
    sv_set(vm, SV_LOADING, sv(vm, SV_LOADING) + 1);

    while (refill(vm)) interpret(vm);

    sd->in = in->prev;
    fclose(in->f);
    free(in);
    sv_set(vm, SV_LOADING, sv(vm, SV_LOADING) - 1);
    memcpy(vm->mem + vm->tib, savetib, TIB_SIZE);
    sv_set(vm, SV_NTIB, ntib); sv_set(vm, SV_TOIN, toin); sv_set(vm, SV_LOADLINE, line);
    sv_set(vm, SV_TICKTIB, ttib);
}

/* ---- error reporting and the top level ---------------------------------------------------- */
static void report_error(vm_t *vm)
{
    seed_t *sd = SEED(vm);
    char buf[64];
    vm->host->flush(vm->host);
    vm_emit(vm, '\n');
    if (sd->in) {
        snprintf(buf, sizeof buf, " at Line %u", sd->in->line);
        vm_types(vm, "file = "); vm_types(vm, sd->in->path); vm_types(vm, buf);
        vm_emit(vm, '\n');
        vm_type(vm, vm_ptr(vm, sv(vm, SV_TICKTIB), sv(vm, SV_NTIB)), sv(vm, SV_NTIB));
        vm_emit(vm, '\n');
        ucell w = here(vm);
        ucell pos = sv(vm, SV_TOIN);
        ucell wl = rd8(vm, w) + 1;
        pos = pos > wl ? pos - wl : 0;
        for (ucell i = 0; i < pos; i++) vm_emit(vm, '-');
        vm_types(vm, "^-- ");
    }
    if (vm->throw_msg) vm_types(vm, vm->throw_msg);
    else { snprintf(buf, sizeof buf, "Error %d", vm->throw_code); vm_types(vm, buf); }
    vm_emit(vm, '\n');
}

static void reset_after_error(vm_t *vm)
{
    seed_t *sd = SEED(vm);
    while (sd->in) {
        infile_t *p = sd->in->prev;
        fclose(sd->in->f);
        free(sd->in);
        sd->in = p;
    }
    vm->sp = vm->sp0;
    vm->rp = vm->rp0;
    vm->depth = 0;
    sv_set(vm, SV_STATE, 0);
    sv_set(vm, SV_LOADING, 0);
    sv_set(vm, SV_NTIB, 0);
    sv_set(vm, SV_TOIN, 0);
    if (sv(vm, SV_DEFBASE)) uv_set(vm, U_BASE, sv(vm, SV_DEFBASE));
}

/* run fn under a catch frame; returns 0, or the throw code */
static int guarded(vm_t *vm, void (*fn)(vm_t *, const void *), const void *arg)
{
    jmp_buf jb, *prev = vm->catch_jmp;
    vm->catch_jmp = &jb;
    if (setjmp(jb)) {
        vm->catch_jmp = prev;
        if (vm->throw_code == E_BYE) return E_BYE;
        report_error(vm);
        reset_after_error(vm);
        return vm->throw_code ? vm->throw_code : -1;
    }
    fn(vm, arg);
    vm->catch_jmp = prev;
    return 0;
}

static void do_include(vm_t *vm, const void *name) { seed_include(vm, name); }
static void do_eval(vm_t *vm, const void *s)
{
    set_tib(vm, s, (int)strlen(s));
    interpret(vm);
}

int seed_load(vm_t *vm, const char *path) { return guarded(vm, do_include, path); }
int seed_eval(vm_t *vm, const char *line) { return guarded(vm, do_eval, line); }

static void do_quit_line(vm_t *vm, const void *unused)
{
    (void)unused;
    interpret(vm);
    if (!compiling(vm)) vm_types(vm, " ok");
    vm_emit(vm, '\n');
}

int seed_quit(vm_t *vm)
{
    for (;;) {
        vm->host->flush(vm->host);
        if (!refill(vm)) return 0;
        int r = guarded(vm, do_quit_line, NULL);
        if (r == E_BYE) return 0;
    }
}

/* ---- C primitives ------------------------------------------------------------------------------ */
void p_HERE(vm_t *vm)   { push(vm, here(vm)); }
void p_ALLOT(vm_t *vm)  { allot(vm, (cell)pop(vm)); }
void p_COMMA(vm_t *vm)  { comma(vm, pop(vm)); }
void p_CCOMMA(vm_t *vm) { ccomma(vm, pop(vm)); }
void p_ALIGN(vm_t *vm)  { align(vm); }
void p_XHERE(vm_t *vm)  { push(vm, xhere(vm)); }
void p_XCOMMA(vm_t *vm) { xcomma(vm, pop(vm)); }
void p_XCCOMMA(vm_t *vm) { xccomma(vm, pop(vm)); }
void p_YHERE(vm_t *vm)  { push(vm, yhere(vm)); }
void p_PAD(vm_t *vm)    { push(vm, here(vm) + 80); }
void p_YCOMMA(vm_t *vm) { ycomma(vm, pop(vm)); }
void p_YCCOMMA(vm_t *vm) { yccomma(vm, pop(vm)); }
void p_SOURCE(vm_t *vm) { push(vm, sv(vm, SV_TICKTIB)); push(vm, sv(vm, SV_NTIB)); }
void p_TIB(vm_t *vm)    { push(vm, vm->tib); }
void p_WORD(vm_t *vm)   { push(vm, parse_word(vm, (int)(pop(vm) & 0xFF))); }
void p_PARSE(vm_t *vm)
{
    ucell a, n;
    parse_delim(vm, (int)(pop(vm) & 0xFF), &a, &n);
    push(vm, a); push(vm, n);
}
void p_FIND(vm_t *vm)
{
    ucell a = pop(vm), n = rd8(vm, a);
    ucell nfa = search(vm, vm_ptr(vm, a + 1, n), n);
    if (!nfa) { push(vm, a); push(vm, 0); return; }
    push(vm, nfa_cfa(vm, nfa));
    push(vm, (rd8(vm, nfa) & 0x40) ? 1 : TRUE_F);
}
void p_HASH(vm_t *vm)           /* ( str voc -- thread ) */
{
    ucell voc = pop(vm), a = pop(vm), n = rd8(vm, a);
    push(vm, hash_thread(vm_ptr(vm, a + 1, n), n, voc));
}
void p_QUPPERCASE(vm_t *vm)     /* ( a -- a ) */
{
    if (sv(vm, SV_CAPS)) upcase_counted(vm, top(vm));
}
void p_NUMBERQ(vm_t *vm)        /* ( a -- d f ) */
{
    ucell a = pop(vm), n = rd8(vm, a);
    udcell d; int isd;
    if (number_q(vm, vm_ptr(vm, a + 1, n), (int)n, &d, &isd)) { dpush(vm, d); push(vm, TRUE_F); }
    else { dpush(vm, 0); push(vm, 0); }
}
void p_NUMBER(vm_t *vm)         /* ( a -- d ) */
{
    ucell a = pop(vm), n = rd8(vm, a);
    udcell d; int isd;
    if (!number_q(vm, vm_ptr(vm, a + 1, n), (int)n, &d, &isd))
        vm_throw(vm, E_UNDEFINED, "%.*s <- What?", (int)n, (char *)vm->mem + a + 1);
    dpush(vm, d);
}
void p_DIGIT(vm_t *vm)          /* ( c base -- n f | c f ) */
{
    int base = (int)pop(vm); ucell c = pop(vm);
    int v = (c >= 'a' && c <= 'z') ? -1 : digit_val((int)c, base);
    if (v < 0) { push(vm, c); push(vm, 0); } else { push(vm, (ucell)v); push(vm, TRUE_F); }
}
void p_TONUMBER(vm_t *vm)       /* ( ud a n -- ud' a' n' ) */
{
    ucell n = pop(vm), a = pop(vm);
    udcell d = dpop(vm);
    int base = (int)uv(vm, U_BASE);
    while (n > 0) {
        int v = digit_val((int)rd8(vm, a), base);
        if (v < 0) break;
        d = d * (udcell)base + (udcell)v;
        a++; n--;
    }
    dpush(vm, d); push(vm, a); push(vm, n);
}
void p_NAMEFROM(vm_t *vm) { push(vm, nfa_cfa(vm, pop(vm))); }
void p_TONAME(vm_t *vm)   { push(vm, cfa_to_nfa(vm, pop(vm))); }
void p_PFIND(vm_t *vm)          /* ( here alf -- cfa flag | here false ) */
{
    ucell lfa = pop(vm), a = top(vm), n = rd8(vm, a);
    const uint8_t *s = vm_ptr(vm, a + 1, n);
    for (; lfa; lfa = rd32(vm, lfa)) {
        ucell nfa = lfa + 4;
        if ((rd8(vm, nfa) & 31) != n) continue;
        ucell i;
        for (i = 0; i < n; i++)
            if ((rd8(vm, nfa + 1 + i) & 0x7F) != s[i]) break;
        if (i < n) continue;
        wr32(vm, vm->sp, nfa_cfa(vm, nfa));
        push(vm, (rd8(vm, nfa) & 0x40) ? 1 : TRUE_F);
        return;
    }
    push(vm, 0);
}
void p_NEWDOES(vm_t *vm) { push(vm, vm_add_handler(vm, HK_DOES, NULL, pop(vm), NULL)); }
void p_TOLINK(vm_t *vm)   { ucell n = cfa_to_nfa(vm, pop(vm)); push(vm, n ? n - 4 : 0); }
void p_TOBODY(vm_t *vm)   { push(vm, pop(vm) + 4); }
void p_BODYFROM(vm_t *vm) { push(vm, pop(vm) - 4); }
void p_DOTID(vm_t *vm)
{
    char name[32];
    int n = nfa_name(vm, pop(vm), name);
    vm_type(vm, (uint8_t *)name, (size_t)n);
    vm_emit(vm, ' ');
}

/* console */
void p_PEMIT(vm_t *vm)
{
    int c = (int)(pop(vm) & 0xFF);
    vm->host->emit(vm->host, c);
    if (c == '\n') sv_set(vm, SV_OUT, 0); else sv_set(vm, SV_OUT, sv(vm, SV_OUT) + 1);
}
void p_PTYPE(vm_t *vm)
{
    ucell n = pop(vm), a = pop(vm);
    vm->host->type(vm->host, vm_ptr(vm, a, n), n);
    sv_set(vm, SV_OUT, sv(vm, SV_OUT) + n);
}
void p_PKEY(vm_t *vm)
{
    int c = vm->host->key(vm->host);
    if (c < 0) { vm->bye = 1; c = 0; }
    push(vm, (ucell)c);
}
void p_PKEYQ(vm_t *vm) { push(vm, vm->host->keyq(vm->host) ? TRUE_F : 0); }
void p_CR(vm_t *vm)
{
    vm_emit(vm, '\n');
    sv_set(vm, SV_OUT, 0);
    sv_set(vm, SV_LINE, sv(vm, SV_LINE) + 1);
}
void p_SPACE(vm_t *vm) { vm_emit(vm, ' '); }
void p_SPACES(vm_t *vm) { cell n = (cell)pop(vm); while (n-- > 0) vm_emit(vm, ' '); }

/* number formatting */
static int fmt_ud(vm_t *vm, udcell d, char *buf)   /* digits, reversed into buf end */
{
    int base = (int)uv(vm, U_BASE);
    if (base < 2 || base > 36) base = 10;
    char tmp[72]; int n = 0;
    do { int v = (int)(d % (udcell)base); d /= (udcell)base;
         tmp[n++] = (char)(v < 10 ? '0' + v : 'A' + v - 10); } while (d);
    for (int i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
    buf[n] = 0;
    return n;
}
static void out_num(vm_t *vm, udcell mag, int neg, cell width, int trailing_space)
{
    char buf[80];
    int n = fmt_ud(vm, mag, buf + 1);
    char *s = buf + 1;
    if (neg) { *--s = '-'; n++; }
    for (cell i = n; i < width; i++) vm_emit(vm, ' ');
    vm_type(vm, (uint8_t *)s, (size_t)n);
    if (trailing_space) vm_emit(vm, ' ');
}
void p_DOT(vm_t *vm)  { cell n = (cell)pop(vm); out_num(vm, n < 0 ? (udcell)(0u - (ucell)n) : (udcell)n, n < 0, 0, 1); }
void p_UDOT(vm_t *vm) { out_num(vm, pop(vm), 0, 0, 1); }
void p_DDOT(vm_t *vm) { dcell d = (dcell)dpop(vm); out_num(vm, d < 0 ? 0 - (udcell)d : (udcell)d, d < 0, 0, 1); }
void p_UDDOT(vm_t *vm) { out_num(vm, dpop(vm), 0, 0, 1); }
void p_DOTR(vm_t *vm) { cell w = (cell)pop(vm), n = (cell)pop(vm); out_num(vm, n < 0 ? (udcell)(0u - (ucell)n) : (udcell)n, n < 0, w, 0); }
void p_UDOTR(vm_t *vm) { cell w = (cell)pop(vm); out_num(vm, pop(vm), 0, w, 0); }
void p_DDOTR(vm_t *vm) { cell w = (cell)pop(vm); dcell d = (dcell)dpop(vm); out_num(vm, d < 0 ? 0 - (udcell)d : (udcell)d, d < 0, w, 0); }

static ucell pad_addr(vm_t *vm) { return here(vm) + 80; }
void p_BEGINNUM(vm_t *vm) { uv_set(vm, U_HLD, pad_addr(vm)); }
void p_HOLD(vm_t *vm)
{
    ucell h = uv(vm, U_HLD) - 1;
    if (h <= here(vm)) vm_throw(vm, E_RANGE, "Pictured numeric output overflow");
    uv_set(vm, U_HLD, h);
    wr8(vm, h, pop(vm));
}
static void hold_digit(vm_t *vm)
{
    udcell d = dpop(vm);
    int base = (int)uv(vm, U_BASE);
    if (base < 2 || base > 36) base = 10;
    int v = (int)(d % (udcell)base);
    dpush(vm, d / (udcell)base);
    push(vm, (ucell)(v < 10 ? '0' + v : 'A' + v - 10));
    p_HOLD(vm);
}
void p_NUMSIGN(vm_t *vm) { hold_digit(vm); }
void p_NUMSIGNS(vm_t *vm)
{
    do hold_digit(vm); while (rd32(vm, vm->sp) | rd32(vm, vm->sp + 4));
}
void p_ENDNUM(vm_t *vm)
{
    dpop(vm);
    ucell h = uv(vm, U_HLD);
    push(vm, h); push(vm, pad_addr(vm) - h);
}
void p_SIGN(vm_t *vm) { if ((cell)pop(vm) < 0) { push(vm, '-'); p_HOLD(vm); } }
void p_DOTS(vm_t *vm)
{
    ucell depth = (vm->sp0 - vm->sp) / 4;
    if (vm->sp > vm->sp0) { vm_types(vm, " Stack Empty"); return; }
    if (depth == 0) { vm_types(vm, " Empty "); return; }
    for (ucell i = depth; i > 0; i--) {
        cell n = (cell)rd32(vm, vm->sp + (i - 1) * 4);
        out_num(vm, n < 0 ? (udcell)(0u - (ucell)n) : (udcell)n, n < 0, 0, 1);
    }
}

void p_BYE(vm_t *vm) { vm->bye = 1; vm_throw(vm, E_BYE, NULL); }
void p_ABORT(vm_t *vm) { vm_throw(vm, E_ABORT, NULL); }
void p_HEX(vm_t *vm) { uv_set(vm, U_BASE, 16); }
void p_DECIMAL(vm_t *vm) { uv_set(vm, U_BASE, 10); }
void p_TICK(vm_t *vm) { push(vm, tick(vm, NULL)); }
void p_IMMEDIATE(vm_t *vm)
{
    ucell nfa = sv(vm, SV_LAST);
    if (nfa) wr8(vm, nfa, rd8(vm, nfa) | 0x40);
}
void p_INTERPRET(vm_t *vm) { interpret(vm); }
void p_EVALUATE(vm_t *vm)
{
    ucell n = pop(vm), a = pop(vm);
    uint8_t savetib[TIB_SIZE];
    memcpy(savetib, vm->mem + vm->tib, TIB_SIZE);
    ucell ntib = sv(vm, SV_NTIB), toin = sv(vm, SV_TOIN), ttib = sv(vm, SV_TICKTIB);
    set_tib(vm, (const char *)vm_ptr(vm, a, n), (int)n);
    interpret(vm);
    memcpy(vm->mem + vm->tib, savetib, TIB_SIZE);
    sv_set(vm, SV_NTIB, ntib); sv_set(vm, SV_TOIN, toin); sv_set(vm, SV_TICKTIB, ttib);
}
void p_FLOAD(vm_t *vm)
{
    ucell w = parse_word(vm, ' ');
    char name[256];
    ucell n = rd8(vm, w);
    memcpy(name, vm->mem + w + 1, n); name[n] = 0;
    seed_include(vm, name);
}
void p_SEEDERROR(vm_t *vm)      /* ( a n f -- ) ?ERROR while bootstrapping */
{
    ucell f = pop(vm), n = pop(vm), a = pop(vm);
    if (!f) return;
    if (n > 150) n = 150;
    vm_throw(vm, E_ABORTQ, "%.*s", (int)n, (const char *)vm_ptr(vm, a, n));
}
void p_INCLUDE(vm_t *vm) { p_FLOAD(vm); }   /* the seed's FLOAD, never shadowed */
void p_WORDS(vm_t *vm)
{
    ucell voc = rd32(vm, SEED(vm)->context);
    char name[32];
    /* merge threads newest-first by header address */
    ucell cur[NTHREADS];
    for (int t = 0; t < NTHREADS; t++) cur[t] = rd32(vm, voc + (ucell)t * 4);
    for (;;) {
        int best = -1;
        for (int t = 0; t < NTHREADS; t++)
            if (cur[t] && (best < 0 || cur[t] > cur[best])) best = t;
        if (best < 0) break;
        int n = nfa_name(vm, cur[best] + 4, name);
        if (sv(vm, SV_OUT) + (ucell)n + 2 > 78) p_CR(vm);
        vm_type(vm, (uint8_t *)name, (size_t)n);
        vm_types(vm, "  ");
        cur[best] = rd32(vm, cur[best]);
    }
}
void p_DEFINITIONS(vm_t *vm) { sv_set(vm, SV_CURRENT, rd32(vm, SEED(vm)->context)); }
void p_ALSO(vm_t *vm)
{
    ucell ctx = SEED(vm)->context;
    for (int i = NVOCS - 1; i > 0; i--) wr32(vm, ctx + (ucell)i * 4, rd32(vm, ctx + (ucell)(i - 1) * 4));
}
void p_ONLY(vm_t *vm)
{
    ucell ctx = SEED(vm)->context;
    for (int i = 0; i < NVOCS; i++) wr32(vm, ctx + (ucell)i * 4, 0);
    wr32(vm, ctx, SEED(vm)->forth);
    wr32(vm, ctx + 4, SEED(vm)->forth);
}
void p_PREVIOUS(vm_t *vm)
{
    ucell ctx = SEED(vm)->context;
    for (int i = 0; i < NVOCS - 1; i++) wr32(vm, ctx + (ucell)i * 4, rd32(vm, ctx + (ucell)(i + 1) * 4));
    wr32(vm, ctx + (NVOCS - 1) * 4, 0);
}
void p_HEADER(vm_t *vm)
{
    ucell w = parse_name(vm);
    uint8_t name[32];
    ucell n = rd8(vm, w); if (n > 31) n = 31;
    memcpy(name, vm->mem + w + 1, n);
    align(vm);
    make_header(vm, name, n, here(vm));
}
void p_CREATE(vm_t *vm)   { define(vm, T_DOVAR); }
void p_VARIABLE(vm_t *vm) { define(vm, T_DOVAR); comma(vm, 0); }
void p_CONSTANT(vm_t *vm) { ucell v = pop(vm); define(vm, T_DOCONST); comma(vm, v); }
void p_VALUE(vm_t *vm)    { ucell v = pop(vm); define(vm, T_DOVALUE); comma(vm, v); }
void p_DEFER(vm_t *vm)    { define(vm, T_DODEFER); comma(vm, 0); }
void p_TWOVARIABLE(vm_t *vm) { define(vm, T_DOVAR); comma(vm, 0); comma(vm, 0); }
void p_TWOCONSTANT(vm_t *vm)
{
    ucell hi = pop(vm), lo = pop(vm);
    define(vm, SEED(vm)->ct_2const);
    comma(vm, hi); comma(vm, lo);
}
void p_VOCABULARY(vm_t *vm)
{
    define(vm, SEED(vm)->ct_voc);
    for (int i = 0; i < NTHREADS; i++) comma(vm, 0);
    ucell link = here(vm);
    comma(vm, sv(vm, SV_VOCLINK));
    sv_set(vm, SV_VOCLINK, link);
}
void p_HIDE(vm_t *vm) { hide(vm); }
void p_REVEAL(vm_t *vm) { reveal(vm); }
void p_RBRACKET(vm_t *vm) { sv_set(vm, SV_STATE, TRUE_F); }
/* BUILTIN name  -- assert that the VM provides name (kernel-design.md 4.2) */
void p_BUILTIN(vm_t *vm)
{
    ucell w = parse_name(vm);
    ucell n = rd8(vm, w);
    ucell nfa = search(vm, vm->mem + w + 1, n);
    if (!nfa || nfa_cfa(vm, nfa) >= SEED(vm)->builtin_limit)
        vm_throw(vm, E_UNDEFINED, "BUILTIN %.*s: not provided by the VM", (int)n, (char *)vm->mem + w + 1);
}
void p_CTSTORE(vm_t *vm) { ucell cfa = pop(vm); wr32(vm, cfa, pop(vm)); }
void p_FORGET(vm_t *vm)
{
    ucell w = parse_name(vm);
    ucell n = rd8(vm, w);
    ucell nfa = search(vm, vm->mem + w + 1, n);
    if (!nfa) vm_throw(vm, E_UNDEFINED, "%.*s <- What?", (int)n, (char *)vm->mem + w + 1);
    ucell cfa = nfa_cfa(vm, nfa), vfa = nfa - 8;
    if (vfa < sv(vm, SV_FENCE))
        vm_throw(vm, E_ABORTQ, "Below fence");
    /* drop vocabularies created after the word */
    ucell link = sv(vm, SV_VOCLINK);
    while (link && link >= cfa) link = rd32(vm, link);
    sv_set(vm, SV_VOCLINK, link);
    for (; link; link = rd32(vm, link)) {
        ucell voc = link - NTHREADS * CELL;
        for (int t = 0; t < NTHREADS; t++) {
            ucell th = voc + (ucell)t * 4, lfa = rd32(vm, th);
            while (lfa && lfa >= vfa) lfa = rd32(vm, lfa);
            wr32(vm, th, lfa);
        }
    }
    if (rd32(vm, cfa) == T_NEST) sv_set(vm, SV_XDP, rd32(vm, cfa + 4));
    uv_set(vm, U_DP, cfa);
    sv_set(vm, SV_YDP, vfa);
}

void p_COLON(vm_t *vm)
{
    sv_set(vm, SV_CSP, vm->sp);
    wr32(vm, SEED(vm)->context, sv(vm, SV_CURRENT));
    xalign(vm);
    ucell x = xhere(vm);
    define(vm, T_NEST);
    comma(vm, x);
    hide(vm);
    sv_set(vm, SV_STATE, TRUE_F);
}
void p_SEMI(vm_t *vm)
{
    if (!compiling(vm)) vm_throw(vm, E_COMPILE_ONLY, "Not Compiling!");
    if (vm->sp != sv(vm, SV_CSP)) vm_throw(vm, E_ABORTQ, "Stack Changed");
    compile_xt(vm, XT(UNNEST));
    reveal(vm);
    sv_set(vm, SV_STATE, 0);
}
void p_LBRACKET(vm_t *vm) { sv_set(vm, SV_STATE, 0); }

void p_IF(vm_t *vm)     { need_compiling(vm); compile_xt(vm, XT(QBRANCH)); q_mark_fwd(vm); }
void p_ELSE(vm_t *vm)   { need_compiling(vm); compile_xt(vm, XT(BRANCH)); q_mark_fwd(vm); two_swap(vm); q_resolve_fwd(vm); }
void p_THEN(vm_t *vm)   { need_compiling(vm); compile_xt(vm, XT(DOTHEN)); q_resolve_fwd(vm); }
void p_BEGIN(vm_t *vm)  { need_compiling(vm); compile_xt(vm, XT(DOBEGIN)); q_mark_back(vm); }
void p_AGAIN(vm_t *vm)  { need_compiling(vm); compile_xt(vm, XT(DOAGAIN)); q_resolve_back(vm); }
void p_UNTIL(vm_t *vm)  { need_compiling(vm); compile_xt(vm, XT(QUNTIL)); q_resolve_back(vm); }
void p_WHILE(vm_t *vm)  { need_compiling(vm); compile_xt(vm, XT(QWHILE)); q_mark_fwd(vm); two_swap(vm); }
void p_REPEAT(vm_t *vm) { need_compiling(vm); compile_xt(vm, XT(DOREPEAT)); q_resolve_back(vm); q_resolve_fwd(vm); }
void p_DO(vm_t *vm)     { need_compiling(vm); compile_xt(vm, XT(PDO)); q_mark_fwd(vm); }
void p_QDO(vm_t *vm)    { need_compiling(vm); compile_xt(vm, XT(PQDO)); q_mark_fwd(vm); }
static void loop_end(vm_t *vm, ucell xt)
{
    need_compiling(vm);
    compile_xt(vm, xt);
    ucell a = rd32(vm, vm->sp), f = rd32(vm, vm->sp + 4);
    push(vm, f); push(vm, a + 4);
    q_resolve_back(vm);              /* back to just after (DO)'s cell */
    q_resolve_fwd(vm);               /* (DO)'s leave address           */
}
void p_LOOP(vm_t *vm)     { loop_end(vm, XT(PLOOP)); }
void p_PLUSLOOP(vm_t *vm) { loop_end(vm, XT(PPLOOP)); }
void p_LEAVE(vm_t *vm)    { need_compiling(vm); compile_xt(vm, XT(PLEAVE)); }
void p_QLEAVE(vm_t *vm)   { need_compiling(vm); compile_xt(vm, XT(PQLEAVE)); }

static void parse_quote(vm_t *vm, ucell *a, ucell *n) { parse_delim(vm, '"', a, n); }
void p_DOTQUOTE(vm_t *vm)
{
    ucell a, n;
    parse_quote(vm, &a, &n);
    if (compiling(vm)) { compile_xt(vm, XT(PDOTQ)); xstring(vm, vm->mem + a, n); }
    else vm_type(vm, vm->mem + a, n);
}
void p_QUOTE(vm_t *vm)
{
    ucell a, n;
    parse_quote(vm, &a, &n);
    if (compiling(vm)) { compile_xt(vm, XT(PQUOTE)); xstring(vm, vm->mem + a, n); return; }
    /* interpreting: copy to a transient buffer above PAD */
    ucell buf = pad_addr(vm) + 512;
    if (n > 255) n = 255;
    memmove(vm_ptr(vm, buf, n + 1), vm->mem + a, n);
    push(vm, buf); push(vm, n);
}
void p_ABORTQ(vm_t *vm)
{
    ucell a, n;
    need_compiling(vm);
    parse_quote(vm, &a, &n);
    compile_xt(vm, XT(PABORTQ));
    xstring(vm, vm->mem + a, n);
}
void p_DOTPAREN(vm_t *vm)
{
    ucell a, n;
    parse_delim(vm, ')', &a, &n);
    vm_type(vm, vm->mem + a, n);
}
void p_PAREN(vm_t *vm) { ucell a, n; parse_delim(vm, ')', &a, &n); }
void p_BACKSLASH(vm_t *vm) { sv_set(vm, SV_TOIN, sv(vm, SV_NTIB)); }
void p_BACKSLASHS(vm_t *vm)
{
    sv_set(vm, SV_TOIN, sv(vm, SV_NTIB));
    if (SEED(vm)->in) SEED(vm)->in->stop = 1;
}
void p_COMMENTC(vm_t *vm)       /* skip to COMMENT; */
{
    for (;;) {
        ucell w = parse_word(vm, ' ');
        ucell n = rd8(vm, w);
        if (n == 0) { if (!refill(vm)) return; continue; }
        if (n == 8 && strncasecmp((char *)vm->mem + w + 1, "COMMENT;", 8) == 0) return;
    }
}
void p_LITERAL(vm_t *vm) { if (compiling(vm)) compile_lit(vm, pop(vm)); }
void p_BRACKTICK(vm_t *vm)
{
    need_compiling(vm);
    ucell xt = tick(vm, NULL);
    compile_xt(vm, XT(TICKLIT)); xcomma(vm, xt);
}
void p_BRACKCOMPILE(vm_t *vm) { need_compiling(vm); compile_xt(vm, tick(vm, NULL)); }
void p_RECURSE(vm_t *vm)
{
    need_compiling(vm);
    compile_xt(vm, nfa_cfa(vm, sv(vm, SV_LAST)));
}
static void char_literal(vm_t *vm, int ctl)
{
    ucell w = parse_word(vm, ' ');
    ucell c = rd8(vm, w) ? rd8(vm, w + 1) : 0;
    if (ctl) c &= 31;
    if (compiling(vm)) compile_lit(vm, c); else push(vm, c);
}
void p_ASCII(vm_t *vm)   { char_literal(vm, 0); }
void p_CONTROL(vm_t *vm) { char_literal(vm, 1); }
void p_DOES(vm_t *vm)
{
    need_compiling(vm);
    compile_xt(vm, XT(PDOES));
    ucell cell_at = xhere(vm);
    xcomma(vm, 0);
    ucell ct = vm_add_handler(vm, HK_DOES, NULL, xhere(vm), NULL);
    wr32(vm, cell_at, ct);
}
static void store_into(vm_t *vm, ucell runtime, int plus)
{
    ucell xt = tick(vm, NULL);
    if (compiling(vm)) { compile_xt(vm, runtime); xcomma(vm, xt); return; }
    ucell v = pop(vm);
    ucell a = (rd32(vm, xt) == T_DOUSERDEFER || rd32(vm, xt) == T_DOUSER) ? vm_up(vm) + rd32(vm, xt + 4) : xt + 4;
    wr32(vm, a, plus ? rd32(vm, a) + v : v);
}
void p_IS(vm_t *vm)          { store_into(vm, XT(PIS), 0); }
void p_STORETO(vm_t *vm)     { store_into(vm, XT(PSTORETO), 0); }
void p_PLUSSTORETO(vm_t *vm) { store_into(vm, XT(PPLUSSTORETO), 1); }

/* ---- initialisation ------------------------------------------------------------------------- */
static ucell head_for(vm_t *vm, const char *name, ucell cfa)
{
    return make_header(vm, (const uint8_t *)name, (ucell)strlen(name), cfa);
}
static ucell def_const(vm_t *vm, const char *name, ucell v)
{
    align(vm);
    ucell cfa = here(vm);
    comma(vm, T_DOCONST); comma(vm, v);
    head_for(vm, name, cfa);
    return cfa;
}

ucell seed_find(vm_t *vm, const char *name)
{
    char up[32];
    size_t n = strlen(name);
    if (n > 31) return 0;
    for (size_t i = 0; i < n; i++) up[i] = (char)toupper((unsigned char)name[i]);
    ucell nfa = search(vm, (const uint8_t *)up, (ucell)n);
    return nfa ? nfa_cfa(vm, nfa) : 0;
}

void seed_add_path(vm_t *vm, const char *dir)
{
    seed_t *sd = SEED(vm);
    sd->paths = realloc(sd->paths, (size_t)(sd->npaths + 1) * sizeof *sd->paths);
    sd->paths[sd->npaths++] = strdup(dir);
}

void seed_init(vm_t *vm)
{
    static const struct { const char *name; int flags; } prims[T_NBUILTIN] = {
#define PRIM(id, name, flags)  { name, flags },
#define CPRIM(id, name, flags) { name, flags },
#include "prims.def"
#undef PRIM
#undef CPRIM
    };
    seed_t *sd = calloc(1, sizeof *sd);
    vm->seed = sd;
    jmp_buf jb;
    vm->catch_jmp = &jb;
    if (setjmp(jb)) {
        fprintf(stderr, "fpc: seed initialisation failed: %s\n", vm->throw_msg ? vm->throw_msg : "?");
        exit(2);
    }

    /* 1. code fields for every built-in */
    for (int t = 0; t < T_NBUILTIN; t++) { sd->xt[t] = here(vm); comma(vm, (ucell)t); }

    /* 2. CONTEXT array and the vocabulary DOES> clause: CONTEXT ! */
    ucell ctx_cfa = here(vm);
    comma(vm, T_DOVAR);
    sd->context = here(vm);
    for (int i = 0; i < NVOCS; i++) comma(vm, 0);
    ucell list = xhere(vm);
    xcomma(vm, ctx_cfa); xcomma(vm, sd->xt[T_STORE]); xcomma(vm, sd->xt[T_UNNEST]);
    sd->ct_voc = vm_add_handler(vm, HK_DOES, NULL, list, "VOCABULARY");
    list = xhere(vm);
    xcomma(vm, sd->xt[T_TWOFETCH]); xcomma(vm, sd->xt[T_UNNEST]);
    sd->ct_2const = vm_add_handler(vm, HK_DOES, NULL, list, "2CONSTANT");

    /* 3. FORTH vocabulary */
    ucell forth_cfa = here(vm);
    comma(vm, sd->ct_voc);
    sd->forth = here(vm);
    for (int i = 0; i < NTHREADS; i++) comma(vm, 0);
    ucell link = here(vm);
    comma(vm, 0);
    sv_set(vm, SV_VOCLINK, link);
    wr32(vm, sd->context, sd->forth);
    wr32(vm, sd->context + 4, sd->forth);      /* as after ONLY FORTH ALSO */
    sv_set(vm, SV_CURRENT, sd->forth);

    /* 4. headers */
    sv_set(vm, SV_WARNING, 0);
    head_for(vm, "FORTH", forth_cfa);
    head_for(vm, "CONTEXT", ctx_cfa);
    for (int t = 0; t < T_NBUILTIN; t++) {
        if (prims[t].flags & F_NOHEAD) continue;
        ucell nfa = head_for(vm, prims[t].name, sd->xt[t]);
        if (prims[t].flags & F_IMM) wr8(vm, nfa, rd8(vm, nfa) | 0x40);
    }
    static const char *svnames[] = {
#define X(id, name) name,
        SYSVARS(X)
#undef X
    };
    for (int i = 0; i < SV_COUNT; i++) head_for(vm, svnames[i], vm->sysvar + (ucell)i * 8);
    static const char *uvnames[] = {
#define X(id, name) name,
        USERVARS(X)
#undef X
    };
    for (int i = 0; i < U_COUNT; i++) {
        align(vm);
        ucell cfa = here(vm);
        comma(vm, T_DOUSER); comma(vm, (ucell)i * CELL);
        head_for(vm, uvnames[i], cfa);
    }
    static const struct { const char *name; int slot; int prim; } udefers[] = {
        { "EMIT", U_EMIT, T_PEMIT }, { "KEY?", U_KEYQ, T_PKEYQ },
        { "KEY", U_KEY, T_PKEY },    { "TYPE", U_TYPE, T_PTYPE },
    };
    for (size_t i = 0; i < sizeof udefers / sizeof *udefers; i++) {
        align(vm);
        ucell cfa = here(vm);
        comma(vm, T_DOUSERDEFER); comma(vm, (ucell)udefers[i].slot * CELL);
        head_for(vm, udefers[i].name, cfa);
        uv_set(vm, udefers[i].slot, sd->xt[udefers[i].prim]);
    }
    def_const(vm, "TRUE", TRUE_F);
    def_const(vm, "FALSE", 0);
    def_const(vm, "BL", ' ');
    def_const(vm, "CELL", CELL);
    def_const(vm, "#VOCS", NVOCS);
    def_const(vm, "#THREADS", NTHREADS);
    def_const(vm, "CT-NEST", T_NEST);
    def_const(vm, "CT-DOVAR", T_DOVAR);
    def_const(vm, "CT-DOCONST", T_DOCONST);
    def_const(vm, "CT-DOVALUE", T_DOVALUE);
    def_const(vm, "CT-DODEFER", T_DODEFER);
    def_const(vm, "CT-DOUSER", T_DOUSER);
    def_const(vm, "CT-DOUSERDEFER", T_DOUSERDEFER);
    def_const(vm, "LIMIT", vm->code_end);
    def_const(vm, "FIRST", vm->code_end - 16);
    def_const(vm, "SP-LIMIT", vm->sp0 - DSTACK_CELLS * CELL);
    def_const(vm, "TIB0", vm->tib);
    def_const(vm, "DOS-LINE", vm->dosbuf + 128);
    def_const(vm, "USER-SIZE", U_END * CELL);
    def_const(vm, "USER-MAX", USER_CELLS * CELL);
    def_const(vm, "LIST-LIMIT", vm->list_end);
    def_const(vm, "HEAD-LIMIT", vm->head_end);
    sv_set(vm, SV_WARNING, TRUE_F);
    sv_set(vm, SV_FENCE, yhere(vm));
    sd->builtin_limit = here(vm);
    vm->catch_jmp = NULL;
}
