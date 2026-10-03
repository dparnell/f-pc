/* dos.c -- DOS/BIOS service emulation for the F-PC VM.
 *
 * F-PC's kernel calls DOS through a few seam words (<BDOS> BDOS2, and the
 * handle words in HANDLES.SEQ) passing INT 21h function numbers. These C
 * built-ins emulate the functions F-PC uses over the host OS
 * (kernel-design.md 6). Also here: BIOS keyboard, the heap allocator that
 * replaces DOS memory blocks, and the video primitives.
 */
#define _GNU_SOURCE
#include "vm.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>

/* ---- DOS state: handles, DTA, directory search ----------------------------- */
#define MAXH 64
typedef struct {
    int   fd[MAXH];             /* DOS handle -> host fd, -1 = free          */
    ucell dta;                  /* disk transfer address                     */
    DIR  *dir;                  /* FIND-FIRST / FIND-NEXT state              */
    char  dirpath[1024];
    char  pattern[256];
    int   attr;
} dos_t;

static dos_t *dos(vm_t *vm)
{
    if (!vm->dos) {
        dos_t *d = calloc(1, sizeof *d);
        for (int i = 0; i < MAXH; i++) d->fd[i] = -1;
        d->fd[0] = 0; d->fd[1] = 1; d->fd[2] = 2;   /* 3 AUX and 4 PRN discard */
        d->dta = vm->dosbuf;
        vm->dos = d;
    }
    return vm->dos;
}

/* ---- DOS path -> host path ----------------------------------------------------- */
/* Resolve each component case-insensitively. If the last component does not
 * exist and create is set, keep it as given. Returns 0 on success. */
static int resolve(const char *in, char *out, size_t outsz, int create)
{
    char p[1024];
    size_t i, j = 0;
    if (in[0] && in[1] == ':') in += 2;                /* drop the drive */
    for (i = 0; in[i] && j < sizeof p - 1; i++) p[j++] = in[i] == '\\' ? '/' : in[i];
    p[j] = 0;
    if (!*p) return -1;
    char cur[1024] = "";
    const char *s = p;
    if (*s == '/') { strcpy(cur, "/"); while (*s == '/') s++; }
    else strcpy(cur, ".");
    while (*s) {
        const char *e = strchr(s, '/');
        size_t len = e ? (size_t)(e - s) : strlen(s);
        char comp[256];
        if (len >= sizeof comp) return -1;
        memcpy(comp, s, len); comp[len] = 0;
        int last = 1;
        if (e) { const char *q = e; while (*q == '/') q++; last = !*q; }
        char next[1100];
        int hit = 0;
        if (!strcmp(comp, ".") || !strcmp(comp, "..")) {
            hit = 1;
            snprintf(next, sizeof next, "%s%s%s", cur, cur[strlen(cur) - 1] == '/' ? "" : "/", comp);
        } else {
            DIR *d = opendir(cur);
            if (d) {
                struct dirent *de;
                while ((de = readdir(d)))
                    if (!strcasecmp(de->d_name, comp)) {
                        snprintf(next, sizeof next, "%s%s%s", cur, cur[strlen(cur) - 1] == '/' ? "" : "/", de->d_name);
                        hit = 1;
                        break;
                    }
                closedir(d);
            }
            if (!hit) {
                if (!(last && create)) return -1;
                snprintf(next, sizeof next, "%s%s%s", cur, cur[strlen(cur) - 1] == '/' ? "" : "/", comp);
            }
        }
        snprintf(cur, sizeof cur, "%s", next);
        s += len;
        while (*s == '/') s++;
    }
    if (!strncmp(cur, "./", 2)) memmove(cur, cur + 2, strlen(cur + 2) + 1);
    snprintf(out, outsz, "%s", cur);
    return 0;
}

static int vm_asciiz(vm_t *vm, ucell a, char *buf, size_t sz)
{
    size_t i;
    for (i = 0; i + 1 < sz; i++) {
        int c = (int)rd8(vm, a + (ucell)i);
        if (!c) break;
        buf[i] = (char)c;
    }
    buf[i] = 0;
    return (int)i;
}

static int new_handle(vm_t *vm, int fd)
{
    dos_t *d = dos(vm);
    for (int h = 5; h < MAXH; h++) if (d->fd[h] < 0) { d->fd[h] = fd; return h; }
    close(fd);
    return -1;
}

static int dos_errno(void)
{
    switch (errno) {
    case ENOENT: return 2;
    case ENOTDIR: return 3;
    case EMFILE: case ENFILE: return 4;
    case EACCES: case EPERM: case EISDIR: case EROFS: return 5;
    case EBADF: return 6;
    case EEXIST: return 80;
    default: return 31;
    }
}

/* ---- DTA / FIND-FIRST ----------------------------------------------------------------- */
static void dos_datetime(time_t t, ucell *time16, ucell *date16)
{
    struct tm tm; localtime_r(&t, &tm);
    *time16 = (ucell)(tm.tm_hour << 11 | tm.tm_min << 5 | tm.tm_sec / 2);
    *date16 = (ucell)((tm.tm_year - 80) << 9 | (tm.tm_mon + 1) << 5 | tm.tm_mday);
}

static int find_next(vm_t *vm)          /* 0 = found, else DOS error (18) */
{
    dos_t *d = dos(vm);
    if (!d->dir) return 18;
    struct dirent *de;
    while ((de = readdir(d->dir))) {
        if (de->d_name[0] == '.' && strcmp(de->d_name, ".") && strcmp(de->d_name, "..")) continue;
        if (strlen(de->d_name) > 12) continue;              /* not an 8.3 name */
        if (fnmatch(d->pattern, de->d_name, FNM_CASEFOLD) != 0) {
            /* DOS: "*.*" matches names without a dot too */
            if (strcmp(d->pattern, "*.*")) continue;
        }
        char full[1400]; struct stat st;
        snprintf(full, sizeof full, "%s/%s", d->dirpath, de->d_name);
        if (stat(full, &st) != 0) continue;
        int isdir = S_ISDIR(st.st_mode);
        if (isdir && !(d->attr & 0x10)) continue;
        ucell dta = d->dta, t, dt;
        uint8_t *p = vm_ptr(vm, dta, 43);
        memset(p, 0, 43);
        p[21] = (uint8_t)(isdir ? 0x10 : 0x20);
        dos_datetime(st.st_mtime, &t, &dt);
        wr16(vm, dta + 22, t); wr16(vm, dta + 24, dt);
        wr32(vm, dta + 26, (ucell)st.st_size);
        for (int i = 0; de->d_name[i] && i < 12; i++) p[30 + i] = (uint8_t)toupper((unsigned char)de->d_name[i]);
        return 0;
    }
    closedir(d->dir); d->dir = NULL;
    return 18;
}

static int find_first(vm_t *vm, ucell name, int attr)
{
    dos_t *d = dos(vm);
    char spec[512], dirpart[512];
    vm_asciiz(vm, name, spec, sizeof spec);
    for (char *q = spec; *q; q++) if (*q == '\\') *q = '/';
    char *s = spec;
    if (s[0] && s[1] == ':') s += 2;
    char *sl = strrchr(s, '/');
    if (sl) { size_t n = (size_t)(sl - s); memcpy(dirpart, s, n); dirpart[n] = 0; if (!n) strcpy(dirpart, "/");
              snprintf(d->pattern, sizeof d->pattern, "%s", sl + 1); }
    else { strcpy(dirpart, "."); snprintf(d->pattern, sizeof d->pattern, "%s", s); }
    if (!*d->pattern) strcpy(d->pattern, "*.*");
    if (d->dir) { closedir(d->dir); d->dir = NULL; }
    if (resolve(dirpart, d->dirpath, sizeof d->dirpath, 0) != 0) return 3;
    d->dir = opendir(d->dirpath);
    if (!d->dir) return 3;
    d->attr = attr;
    return find_next(vm);
}

/* ---- INT 21h ---------------------------------------------------------------------------- */
typedef struct { ucell ax, bx, cx, dx, si, di; int carry; } regs_t;

#define AL(r) ((r)->ax & 0xFF)
#define SET_AL(r, v) ((r)->ax = ((r)->ax & ~0xFFu) | ((ucell)(v) & 0xFF))
#define FAIL(r, e) do { (r)->ax = (ucell)(e); (r)->carry = 1; return; } while (0)

static void int21(vm_t *vm, regs_t *r)
{
    dos_t *d = dos(vm);
    int ah = (r->ax >> 8) & 0xFF;
    int dl = r->dx & 0xFF;
    char name[512], path[1100];
    r->carry = 0;
    switch (ah) {
    case 0x00:                              /* terminate */
        vm->host->flush(vm->host);
        vm->bye = 1; vm->exit_code = 0;
        vm_throw(vm, E_BYE, NULL);
    case 0x4C:                              /* terminate with code */
        vm->host->flush(vm->host);
        vm->bye = 1; vm->exit_code = (int)AL(r);
        vm_throw(vm, E_BYE, NULL);
    case 0x02:                              /* display char DL */
        screen_tty(vm, dl);
        SET_AL(r, dl);
        break;
    case 0x06:                              /* direct console I/O */
        if (dl != 0xFF) { screen_tty(vm, dl); break; }
        __attribute__((fallthrough));      /* DL = FF: input */
    case 0x07: case 0x08: {                 /* character input */
        int k = vm->host->key(vm->host);
        if (k < 0) { vm->bye = 1; vm_throw(vm, E_BYE, NULL); }
        SET_AL(r, k);
        break; }
    case 0x0B:                              /* input status */
        SET_AL(r, vm->host->keyq(vm->host) ? 0xFF : 0);
        break;
    case 0x0E: SET_AL(r, 26); break;        /* select disk: number of drives */
    case 0x19: SET_AL(r, 2); break;         /* current disk: C: */
    case 0x1A: d->dta = r->dx; break;       /* set DTA */
    case 0x2F: r->bx = d->dta; break;       /* get DTA */
    case 0x2A: {                            /* get date */
        time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm);
        r->cx = (ucell)(tm.tm_year + 1900);
        r->dx = (ucell)((tm.tm_mon + 1) << 8 | tm.tm_mday);
        SET_AL(r, tm.tm_wday);
        break; }
    case 0x2C: {                            /* get time */
        struct timeval tv; gettimeofday(&tv, NULL);
        struct tm tm; localtime_r(&tv.tv_sec, &tm);
        r->cx = (ucell)(tm.tm_hour << 8 | tm.tm_min);
        r->dx = (ucell)(tm.tm_sec << 8 | (int)(tv.tv_usec / 10000));
        break; }
    case 0x2B: case 0x2D: SET_AL(r, 0xFF); break;   /* set date/time: refused */
    case 0x30: r->ax = 0x0005; r->bx = 0; r->cx = 0; break;  /* DOS 5.0 */
    case 0x33: r->dx = (r->dx & 0xFF00) | 1; break;
    case 0x36: r->ax = 64; r->bx = 0xFFFF; r->cx = 512; r->dx = 0xFFFF; break;
    case 0x25: case 0x35: r->bx = 0; break; /* interrupt vectors: ignored */

    case 0x39: case 0x3A: case 0x3B:        /* mkdir rmdir chdir */
        vm_asciiz(vm, r->dx, name, sizeof name);
        if (resolve(name, path, sizeof path, ah == 0x39) != 0) FAIL(r, 3);
        if ((ah == 0x39 ? mkdir(path, 0777) : ah == 0x3A ? rmdir(path) : chdir(path)) != 0)
            FAIL(r, dos_errno());
        break;
    case 0x3C: {                            /* create */
        vm_asciiz(vm, r->dx, name, sizeof name);
        if (resolve(name, path, sizeof path, 1) != 0) FAIL(r, 3);
        int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0666);
        if (fd < 0) FAIL(r, dos_errno());
        int h = new_handle(vm, fd);
        if (h < 0) FAIL(r, 4);
        r->ax = (ucell)h;
        break; }
    case 0x3D: {                            /* open */
        static const int modes[3] = { O_RDONLY, O_WRONLY, O_RDWR };
        vm_asciiz(vm, r->dx, name, sizeof name);
        if (!strcasecmp(name, "CON") || !strcasecmp(name, "CON.")) { r->ax = 1; break; }
        if (!strcasecmp(name, "PRN") || !strcasecmp(name, "PRN.")) { r->ax = 4; break; }
        if (resolve(name, path, sizeof path, 0) != 0) FAIL(r, 2);
        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) FAIL(r, 5);
        int fd = open(path, modes[AL(r) & 3 ? (AL(r) & 3) % 3 : 0]);
        if (fd < 0) FAIL(r, dos_errno());
        int h = new_handle(vm, fd);
        if (h < 0) FAIL(r, 4);
        r->ax = (ucell)h;
        break; }
    case 0x3E: {                            /* close */
        ucell h = r->bx;
        if (h >= MAXH || d->fd[h] < 0) FAIL(r, 6);
        if (h > 4) { close(d->fd[h]); d->fd[h] = -1; }
        break; }
    case 0x3F: {                            /* read */
        ucell h = r->bx, n = r->cx & 0xFFFFFFFFu;
        if (h >= MAXH || (d->fd[h] < 0 && h != 3 && h != 4)) FAIL(r, 6);
        if (h == 3 || h == 4) { r->ax = 0; break; }
        if (h == 0) {                       /* console input: one line */
            ucell i = 0;
            while (i < n) {
                int k = vm->host->key(vm->host);
                if (k < 0) break;
                k &= 0xFF;
                wr8(vm, r->dx + i++, (ucell)k);
                if (k == 13) { if (i < n) wr8(vm, r->dx + i++, 10); break; }
            }
            r->ax = i;
            break;
        }
        ssize_t got = read(d->fd[h], vm_ptr(vm, r->dx, n), n);
        if (got < 0) FAIL(r, dos_errno());
        r->ax = (ucell)got;
        break; }
    case 0x40: {                            /* write */
        ucell h = r->bx, n = r->cx;
        if (h >= MAXH || (d->fd[h] < 0 && h != 3 && h != 4)) FAIL(r, 6);
        if (h == 1 || h == 2) {             /* console: through the screen */
            const uint8_t *p = vm_ptr(vm, r->dx, n);
            for (ucell i = 0; i < n; i++) screen_tty(vm, p[i]);
            r->ax = n;
            break;
        }
        if (h == 3 || h == 4) { r->ax = n; break; }
        if (n == 0) { off_t pos = lseek(d->fd[h], 0, SEEK_CUR);   /* DOS: truncate */
                      if (pos >= 0 && ftruncate(d->fd[h], pos) != 0) FAIL(r, 5);
                      r->ax = 0; break; }
        ssize_t put = write(d->fd[h], vm_ptr(vm, r->dx, n), n);
        if (put < 0) FAIL(r, dos_errno());
        r->ax = (ucell)put;
        break; }
    case 0x41:                              /* delete */
        vm_asciiz(vm, r->dx, name, sizeof name);
        if (resolve(name, path, sizeof path, 0) != 0) FAIL(r, 2);
        if (unlink(path) != 0) FAIL(r, dos_errno());
        break;
    case 0x42: {                            /* lseek CX:DX, AL = whence */
        ucell h = r->bx;
        if (h >= MAXH || d->fd[h] < 0) FAIL(r, 6);
        if (h <= 4) { r->ax = 0; r->dx = 0; break; }
        off_t off = (off_t)(int32_t)((r->cx & 0xFFFF) << 16 | (r->dx & 0xFFFF));
        if (r->cx > 0xFFFF) off = (off_t)(int64_t)((uint64_t)r->cx << 32 | r->dx);  /* 32-bit callers */
        off_t pos = lseek(d->fd[h], off, AL(r) == 0 ? SEEK_SET : AL(r) == 1 ? SEEK_CUR : SEEK_END);
        if (pos < 0) FAIL(r, 25);
        r->ax = (ucell)(pos & 0xFFFF); r->dx = (ucell)((pos >> 16) & 0xFFFF);
        break; }
    case 0x43: {                            /* get/set attributes */
        vm_asciiz(vm, r->dx, name, sizeof name);
        struct stat st;
        if (resolve(name, path, sizeof path, 0) != 0 || stat(path, &st) != 0) FAIL(r, 2);
        r->cx = S_ISDIR(st.st_mode) ? 0x10 : (st.st_mode & S_IWUSR) ? 0x20 : 0x21;
        break; }
    case 0x47: {                            /* get cwd into DS:SI, no drive or leading \ */
        char cwd[1024];
        if (!getcwd(cwd, sizeof cwd)) FAIL(r, 15);
        const char *c = cwd;
        while (*c == '/') c++;
        size_t n = strlen(c);
        if (n > 63) n = 63;
        uint8_t *p = vm_ptr(vm, r->si, 64);
        for (size_t i = 0; i < n; i++) p[i] = (uint8_t)(c[i] == '/' ? '\\' : c[i]);
        p[n] = 0;
        break; }
    case 0x4E:                              /* find first: CX attr, DS:DX spec */
        { int e = find_first(vm, r->dx, (int)r->cx); if (e) FAIL(r, e); }
        break;
    case 0x4F:
        { int e = find_next(vm); if (e) FAIL(r, e); }
        break;
    case 0x56: {                            /* rename DS:DX -> ES:DI */
        char name2[512], path2[1100];
        vm_asciiz(vm, r->dx, name, sizeof name);
        vm_asciiz(vm, r->di, name2, sizeof name2);
        if (resolve(name, path, sizeof path, 0) != 0) FAIL(r, 2);
        if (resolve(name2, path2, sizeof path2, 1) != 0) FAIL(r, 3);
        if (rename(path, path2) != 0) FAIL(r, dos_errno());
        break; }
    case 0x57: {                            /* file date/time (get only) */
        ucell h = r->bx; struct stat st;
        if (h >= MAXH || d->fd[h] < 0 || fstat(d->fd[h], &st) != 0) FAIL(r, 6);
        if (AL(r) == 0) dos_datetime(st.st_mtime, &r->cx, &r->dx);
        break; }
    default:
        FAIL(r, 1);                         /* invalid function */
    }
}

void p_BDOS(vm_t *vm)                       /* ( n fun -- m ) */
{
    regs_t r = { 0 };
    ucell fun = pop(vm);
    r.dx = pop(vm);
    r.ax = (fun & 0xFF) << 8;
    int21(vm, &r);
    push(vm, r.ax & 0xFF);
}

void p_BDOS2(vm_t *vm)                      /* ( CX DX AX -- CX DX AX ) */
{
    regs_t r = { 0 };
    ucell ax = pop(vm);
    r.dx = pop(vm);
    r.cx = pop(vm);
    r.ax = (ax & 0xFF) << 8;                /* MOV AH, AL */
    int21(vm, &r);
    push(vm, r.cx); push(vm, r.dx); push(vm, r.ax);
}

static void hdos_result(vm_t *vm, regs_t *r)    /* -- ax cf */
{
    push(vm, r->carry ? (r->ax & 0xFF) : r->ax);
    push(vm, r->carry ? 1 : 0);
}

void p_HDOS1(vm_t *vm)                      /* ( cx dx fun -- ax cf ) */
{
    regs_t r = { 0 };
    r.ax = pop(vm); r.dx = pop(vm); r.cx = pop(vm);
    int21(vm, &r);
    hdos_result(vm, &r);
}

void p_HDOS4(vm_t *vm)                      /* ( bx cx dx fun -- ax cf ) */
{
    regs_t r = { 0 };
    r.ax = pop(vm); r.dx = pop(vm); r.cx = pop(vm); r.bx = pop(vm);
    int21(vm, &r);
    hdos_result(vm, &r);
}

#define HNDLOFFSET 136
static ucell hcb_handle(vm_t *vm, ucell hcb) { return rd32(vm, hcb + HNDLOFFSET); }

static void seek_handle(vm_t *vm, ucell h, int64_t off, int whence, int64_t *pos)
{
    dos_t *d = dos(vm);
    *pos = 0;
    if (h >= MAXH || d->fd[h] < 0 || h <= 4) return;
    off_t p = lseek(d->fd[h], (off_t)off, whence);
    *pos = p < 0 ? 0 : p;
}

void p_MOVEPOINTER(vm_t *vm)                /* ( d handle -- ) */
{
    ucell hcb = pop(vm);
    int64_t off = (int64_t)dpop(vm), pos;
    seek_handle(vm, hcb_handle(vm, hcb), off, SEEK_SET, &pos);
}

void p_ENDFILE(vm_t *vm)                    /* ( handle -- d ) */
{
    int64_t pos;
    seek_handle(vm, hcb_handle(vm, pop(vm)), 0, SEEK_END, &pos);
    dpush(vm, (udcell)pos);
}

void p_CURPOINTER(vm_t *vm)                 /* ( handle -- d ) */
{
    int64_t pos;
    seek_handle(vm, hcb_handle(vm, pop(vm)), 0, SEEK_CUR, &pos);
    dpush(vm, (udcell)pos);
}

void p_HRENAME(vm_t *vm)                    /* ( hcb1 hcb2 -- ax cf ) */
{
    regs_t r = { 0 };
    ucell h2 = pop(vm), h1 = pop(vm);
    r.ax = 0x5600; r.dx = h1 + 1; r.di = h2 + 1;
    int21(vm, &r);
    hdos_result(vm, &r);
}

void p_FINDFIRST(vm_t *vm)                  /* ( adr attr -- f ) false = found */
{
    int attr = (int)pop(vm);
    ucell a = pop(vm);
    push(vm, find_first(vm, a, attr) ? TRUE_F : 0);
}

void p_FINDNEXT(vm_t *vm) { push(vm, find_next(vm) ? TRUE_F : 0); }
void p_DTAFETCH(vm_t *vm) { push(vm, dos(vm)->dta); }
void p_DTASTORE(vm_t *vm) { dos(vm)->dta = pop(vm); }

void p_PDOS(vm_t *vm)                       /* ( addr drive -- f ) */
{
    regs_t r = { 0 };
    r.dx = pop(vm); r.si = pop(vm); r.ax = 0x4700;
    int21(vm, &r);
    push(vm, r.carry ? 1 : 0);
}

void p_SYSTEM(vm_t *vm)                     /* ( addr len -- rc ) */
{
    ucell n = pop(vm), a = pop(vm);
    char cmd[1024];
    if (n >= sizeof cmd) n = sizeof cmd - 1;
    memcpy(cmd, vm_ptr(vm, a, n), n);
    cmd[n] = 0;
    vm->host->flush(vm->host);
    if (vm->host->suspend) vm->host->suspend(vm->host, 1);
    int rc = system(n ? cmd : "${SHELL:-/bin/sh}");
    if (vm->host->suspend) vm->host->suspend(vm->host, 0);
    push(vm, (ucell)(rc < 0 ? 255 : WEXITSTATUS(rc)));
}

/* ---- BIOS keyboard ------------------------------------------------------------
 * Values are BIOS style: scan code * 256 + ASCII. A terminal resize is
 * delivered as K-RESIZE ($FF00) after RESIZED has run. host->key returns
 * -1 at end of input and -2 when interrupted (e.g. by a resize). */
static int next_key(vm_t *vm, int wait)
{
    for (;;) {
        screen_check_resize(vm);
        int rk = screen_take_resize_key(vm);
        if (rk) return rk;
        if (vm->host->refresh) vm->host->refresh(vm->host, vm, 0);
        if (!wait) return vm->host->keyq(vm->host) ? 1 : 0;
        int k = vm->host->key(vm->host);
        if (k == -2) continue;
        if (k < 0) { vm->bye = 1; vm_throw(vm, E_BYE, NULL); }
        if (k == 0) continue;               /* ignore Control-Break (0) */
        return k;
    }
}

void p_BIOSKEYQ(vm_t *vm)
{
    int q = next_key(vm, 0);
    if (q > 1) screen_unget_key(vm, q);     /* a resize: report it as a key */
    sv_set(vm, SV_BIOSCHAR, (ucell)q);
    push(vm, q ? TRUE_F : 0);
}

void p_BIOSKEY(vm_t *vm)
{
    int k = screen_unget_take(vm);
    if (!k) k = next_key(vm, 1);
    sv_set(vm, SV_BIOSKEYVAL, (ucell)k);
    push(vm, (ucell)k);
}

void p_SHIFTSTATE(vm_t *vm)                 /* ( -- flags ) as INT 16h AH=2 */
{
    push(vm, vm->host->shift ? (ucell)vm->host->shift(vm->host) : 0);
}

/* ---- heap allocator --------------------------------------------------------------
 * Blocks live in the HEAP region with an 8-byte header: size (bytes, incl.
 * header, multiple of 16) and a "used" cell. Free neighbours are coalesced
 * while scanning. All state is in VM memory, so images carry it.
 */
#define HDR 8u
static void heap_init(vm_t *vm)
{
    if (vm->heap_free) return;
    ucell base = (vm->heap_base + 15) & ~15u;
    wr32(vm, base, (vm->heap_end - base) & ~15u);
    wr32(vm, base + 4, 0);
    vm->heap_free = base;
}

static ucell heap_alloc(vm_t *vm, ucell n)
{
    heap_init(vm);
    ucell need = (n + HDR + 15) & ~15u;
    if (need < n) return 0;
    for (ucell b = vm->heap_free; b < vm->heap_end; ) {
        ucell size = rd32(vm, b);
        if (size == 0) break;
        if (!rd32(vm, b + 4)) {
            /* coalesce following free blocks */
            for (ucell nb = b + size; nb < vm->heap_end && rd32(vm, nb) && !rd32(vm, nb + 4); nb = b + size) {
                size += rd32(vm, nb);
                wr32(vm, b, size);
            }
            if (size >= need) {
                if (size - need >= 32) {
                    wr32(vm, b + need, size - need);
                    wr32(vm, b + need + 4, 0);
                    wr32(vm, b, need);
                }
                wr32(vm, b + 4, 1);
                return b + HDR;
            }
        }
        b += size;
    }
    return 0;
}

ucell heap_alloc_block(vm_t *vm, ucell n) { return heap_alloc(vm, n); }

static int heap_valid(vm_t *vm, ucell a)
{
    return a >= vm->heap_base + HDR && a < vm->heap_end && (a & 15) == HDR % 16 && rd32(vm, a - 4) == 1;
}

void p_ALLOCATE(vm_t *vm)                   /* ( n -- addr ior ) */
{
    ucell n = pop(vm);
    ucell a = heap_alloc(vm, n);
    if (a) memset(vm->mem + a, 0, rd32(vm, a - HDR) - HDR);
    push(vm, a);
    push(vm, a ? 0 : (ucell)-59);
}

void heap_free_block(vm_t *vm, ucell a) { if (heap_valid(vm, a)) wr32(vm, a - 4, 0); }

void p_FREE(vm_t *vm)                       /* ( addr -- ior ) */
{
    ucell a = pop(vm);
    if (!heap_valid(vm, a)) { push(vm, (ucell)-60); return; }
    wr32(vm, a - 4, 0);
    push(vm, 0);
}

void p_RESIZE(vm_t *vm)                     /* ( addr n -- addr' ior ) */
{
    ucell n = pop(vm), a = pop(vm);
    if (!heap_valid(vm, a)) { push(vm, a); push(vm, (ucell)-61); return; }
    ucell have = rd32(vm, a - HDR) - HDR;
    if (n <= have) { push(vm, a); push(vm, 0); return; }
    ucell b = heap_alloc(vm, n);
    if (!b) { push(vm, a); push(vm, (ucell)-61); return; }
    memcpy(vm->mem + b, vm->mem + a, have);
    memset(vm->mem + b + have, 0, rd32(vm, b - HDR) - HDR - have);
    wr32(vm, a - 4, 0);
    push(vm, b); push(vm, 0);
}

void p_SETCURSOR(vm_t *vm) { sv_set(vm, SV_CURSOR, pop(vm)); }
void p_GETCURSOR(vm_t *vm) { push(vm, sv(vm, SV_CURSOR)); }

/* ---- host environment (ENVIRON.SEQ) ----------------------------------------- */
extern char **environ;

static void push_env_string(vm_t *vm, const char *v)   /* copy into a VM buffer */
{
    size_t n = strlen(v);
    if (n > 1023) n = 1023;
    static ucell envbuf;
    if (!envbuf) envbuf = heap_alloc_block(vm, 1024);
    if (!envbuf) { push(vm, 0); push(vm, 0); push(vm, 0); return; }
    memcpy(vm->mem + envbuf, v, n);
    push(vm, envbuf); push(vm, (ucell)n); push(vm, TRUE_F);
}

void p_GETENV(vm_t *vm)                     /* ( a1 n1 -- a2 n2 f ) */
{
    ucell n = pop(vm), a = pop(vm);
    char name[256];
    if (n >= sizeof name) n = sizeof name - 1;
    memcpy(name, vm_ptr(vm, a, n), n);
    name[n] = 0;
    const char *v = getenv(name);
    if (!v) { push(vm, 0); push(vm, 0); push(vm, 0); return; }
    push_env_string(vm, v);
}

void p_ENVSTRING(vm_t *vm)                  /* ( n -- a n f ) n-th NAME=value */
{
    ucell i = pop(vm), k = 0;
    for (char **e = environ; *e; e++, k++)
        if (k == i) { push_env_string(vm, *e); return; }
    push(vm, 0); push(vm, 0); push(vm, 0);
}
