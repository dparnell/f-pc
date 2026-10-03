/* host_tty.c -- full-screen terminal host (ANSI/xterm).
 *
 * The VM's virtual screen (VIDEO-BUF) is drawn by diffing it against what
 * is on the terminal; CP437 is shown as UTF-8 and CGA attributes as ANSI
 * colours. The screen follows the terminal size (SIGWINCH). A reader thread
 * turns terminal input into BIOS-style key values (scan*256 + ascii), so
 * Control-C can interrupt a busy program yet still be typed as a key while
 * F-PC is waiting for one.
 */
#define _GNU_SOURCE
#include "vm.h"

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* ---- CP437 -> Unicode -------------------------------------------------------- */
static const uint16_t cp437[256] = {
    0x0020,0x263A,0x263B,0x2665,0x2666,0x2663,0x2660,0x2022,0x25D8,0x25CB,0x25D9,0x2642,0x2640,0x266A,0x266B,0x263C,
    0x25BA,0x25C4,0x2195,0x203C,0x00B6,0x00A7,0x25AC,0x21A8,0x2191,0x2193,0x2192,0x2190,0x221F,0x2194,0x25B2,0x25BC,
    0x0020,0x0021,0x0022,0x0023,0x0024,0x0025,0x0026,0x0027,0x0028,0x0029,0x002A,0x002B,0x002C,0x002D,0x002E,0x002F,
    0x0030,0x0031,0x0032,0x0033,0x0034,0x0035,0x0036,0x0037,0x0038,0x0039,0x003A,0x003B,0x003C,0x003D,0x003E,0x003F,
    0x0040,0x0041,0x0042,0x0043,0x0044,0x0045,0x0046,0x0047,0x0048,0x0049,0x004A,0x004B,0x004C,0x004D,0x004E,0x004F,
    0x0050,0x0051,0x0052,0x0053,0x0054,0x0055,0x0056,0x0057,0x0058,0x0059,0x005A,0x005B,0x005C,0x005D,0x005E,0x005F,
    0x0060,0x0061,0x0062,0x0063,0x0064,0x0065,0x0066,0x0067,0x0068,0x0069,0x006A,0x006B,0x006C,0x006D,0x006E,0x006F,
    0x0070,0x0071,0x0072,0x0073,0x0074,0x0075,0x0076,0x0077,0x0078,0x0079,0x007A,0x007B,0x007C,0x007D,0x007E,0x2302,
    0x00C7,0x00FC,0x00E9,0x00E2,0x00E4,0x00E0,0x00E5,0x00E7,0x00EA,0x00EB,0x00E8,0x00EF,0x00EE,0x00EC,0x00C4,0x00C5,
    0x00C9,0x00E6,0x00C6,0x00F4,0x00F6,0x00F2,0x00FB,0x00F9,0x00FF,0x00D6,0x00DC,0x00A2,0x00A3,0x00A5,0x20A7,0x0192,
    0x00E1,0x00ED,0x00F3,0x00FA,0x00F1,0x00D1,0x00AA,0x00BA,0x00BF,0x2310,0x00AC,0x00BD,0x00BC,0x00A1,0x00AB,0x00BB,
    0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,0x2555,0x2563,0x2551,0x2557,0x255D,0x255C,0x255B,0x2510,
    0x2514,0x2534,0x252C,0x251C,0x2500,0x253C,0x255E,0x255F,0x255A,0x2554,0x2569,0x2566,0x2560,0x2550,0x256C,0x2567,
    0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256B,0x256A,0x2518,0x250C,0x2588,0x2584,0x258C,0x2590,0x2580,
    0x03B1,0x00DF,0x0393,0x03C0,0x03A3,0x03C3,0x00B5,0x03C4,0x03A6,0x0398,0x03A9,0x03B4,0x221E,0x03C6,0x03B5,0x2229,
    0x2261,0x00B1,0x2265,0x2264,0x2320,0x2321,0x00F7,0x2248,0x00B0,0x2219,0x00B7,0x221A,0x207F,0x00B2,0x25A0,0x00A0,
};

/* ---- output buffering ----------------------------------------------------------- */
typedef struct {
    host_t h;
    struct termios saved;
    int raw;
    int cols, rows;
    uint8_t *shadow;            /* what the terminal shows: char,attr pairs */
    int scols, srows;
    int cur_attr;
    int cx, cy, cursor_on;
    char *out; size_t outlen, outcap;

    /* keyboard */
    pthread_t reader;
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int q[256]; int qh, qt;
    int eof;
    int waiting;                /* VM is blocked in key() */
    int shift;                  /* modifiers of the last key */
    int mouse, mx, my, mbuttons; /* xterm SGR mouse reporting */
    vm_t *vm;
} tty_t;

static tty_t *the_tty;

static void out(tty_t *t, const void *s, size_t n)
{
    if (t->outlen + n > t->outcap) {
        t->outcap = (t->outlen + n) * 2 + 4096;
        t->out = realloc(t->out, t->outcap);
    }
    memcpy(t->out + t->outlen, s, n);
    t->outlen += n;
}
static void outs(tty_t *t, const char *s) { out(t, s, strlen(s)); }
static void flushout(tty_t *t)
{
    size_t off = 0;
    while (off < t->outlen) {
        ssize_t w = write(1, t->out + off, t->outlen - off);
        if (w < 0) { if (errno == EINTR) continue; break; }
        off += (size_t)w;
    }
    t->outlen = 0;
}

static void put_utf8(tty_t *t, uint8_t c)
{
    unsigned u = cp437[c];
    char b[4];
    if (u < 0x80) { b[0] = (char)u; out(t, b, 1); }
    else if (u < 0x800) { b[0] = (char)(0xC0 | u >> 6); b[1] = (char)(0x80 | (u & 0x3F)); out(t, b, 2); }
    else { b[0] = (char)(0xE0 | u >> 12); b[1] = (char)(0x80 | ((u >> 6) & 0x3F)); b[2] = (char)(0x80 | (u & 0x3F)); out(t, b, 3); }
}

static void set_attr(tty_t *t, int a)
{
    static const int cga2ansi[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };
    if (a == t->cur_attr) return;
    int fg = a & 15, bg = (a >> 4) & 15;     /* bit 7: bright background (blink off) */
    char b[32];
    snprintf(b, sizeof b, "\033[0;%d;%dm",
             (fg & 8 ? 90 : 30) + cga2ansi[fg & 7], (bg & 8 ? 100 : 40) + cga2ansi[bg & 7]);
    outs(t, b);
    t->cur_attr = a;
}

static void move_to(tty_t *t, int x, int y)
{
    char b[24];
    snprintf(b, sizeof b, "\033[%d;%dH", y + 1, x + 1);
    outs(t, b);
}

/* ---- terminal setup ---------------------------------------------------------------- */
static void term_size(int *c, int *r)
{
    struct winsize ws;
    if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) { *c = ws.ws_col; *r = ws.ws_row; }
    else { *c = 80; *r = 25; }
}

static void enter_screen(tty_t *t)
{
    struct termios raw = t->saved;
    raw.c_iflag &= ~(unsigned)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(unsigned)OPOST;
    raw.c_cflag |= CS8;
    raw.c_lflag &= ~(unsigned)(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0;
    tcsetattr(0, TCSAFLUSH, &raw);
    t->raw = 1;
    outs(t, "\033[?1049h\033[H\033[2J\033[?7l");   /* alt screen, clear, no autowrap */
    if (t->mouse) outs(t, "\033[?1002h\033[?1006h");
    t->cur_attr = -1;
    flushout(t);
    free(t->shadow); t->shadow = NULL;           /* force a full redraw */
}

static void leave_screen(tty_t *t)
{
    if (!t->raw) return;
    outs(t, "\033[0m\033[?7h\033[?25h\033[?1002l\033[?1006l\033[?1049l");
    flushout(t);
    tcsetattr(0, TCSAFLUSH, &t->saved);
    t->raw = 0;
}

static void at_exit(void) { if (the_tty) leave_screen(the_tty); }

static void on_winch(int sig) { (void)sig; if (the_tty && the_tty->vm) screen_note_resize(the_tty->vm);
                                if (the_tty) { pthread_cond_broadcast(&the_tty->cv); } }
static void on_alarm(int sig) { (void)sig; if (the_tty && the_tty->vm) the_tty->vm->attention |= 2; }
static void on_fatal(int sig) { at_exit(); signal(sig, SIG_DFL); raise(sig); }

/* ---- keyboard: escape sequences -> BIOS key values ---------------------------------- */
static void enqueue(tty_t *t, int k)
{
    pthread_mutex_lock(&t->mu);
    if (k == 3 && !t->waiting && t->vm) t->vm->interrupt = 1;   /* ^C while busy: break */
    else if ((t->qt + 1) % 256 != t->qh) { t->q[t->qt] = k; t->qt = (t->qt + 1) % 256; }
    pthread_cond_broadcast(&t->cv);
    pthread_mutex_unlock(&t->mu);
}

static int rd(int timeout_ms)       /* read one byte, -1 on timeout, -2 on EOF */
{
    struct pollfd p = { 0, POLLIN, 0 };
    for (;;) {
        int r = poll(&p, 1, timeout_ms);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return -1;
        unsigned char c;
        ssize_t n = read(0, &c, 1);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -2;
        return c;
    }
}

/* scan codes for a-z with Alt */
static const uint8_t alt_scan[26] = { 0x1E,0x30,0x2E,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,
                                      0x31,0x18,0x19,0x10,0x13,0x1F,0x14,0x16,0x2F,0x11,0x2D,0x15,0x2C };

static int fkey(int n, int mod)     /* F1..F12 with modifier (1 none,2 shift,3 alt,5 ctrl) */
{
    static const uint8_t base[12] = { 0x3B,0x3C,0x3D,0x3E,0x3F,0x40,0x41,0x42,0x43,0x44,0x85,0x86 };
    if (n < 1 || n > 12) return 0;
    if (n <= 10) {
        if (mod == 2) return (0x54 + n - 1) << 8;
        if (mod == 5) return (0x5E + n - 1) << 8;
        if (mod == 3) return (0x68 + n - 1) << 8;
    }
    return base[n - 1] << 8;
}

static int cursor_key(char final, int mod)
{
    int scan = 0;
    switch (final) {
    case 'A': scan = mod == 5 ? 0x8D : 0x48; break;     /* Up */
    case 'B': scan = mod == 5 ? 0x91 : 0x50; break;     /* Down */
    case 'C': scan = mod == 5 ? 0x74 : 0x4D; break;     /* Right */
    case 'D': scan = mod == 5 ? 0x73 : 0x4B; break;     /* Left */
    case 'H': scan = mod == 5 ? 0x77 : 0x47; break;     /* Home */
    case 'F': scan = mod == 5 ? 0x75 : 0x4F; break;     /* End */
    default: return 0;
    }
    return scan << 8;
}

static int tilde_key(int n, int mod)
{
    switch (n) {
    case 1: case 7: return (mod == 5 ? 0x77 : 0x47) << 8;       /* Home */
    case 4: case 8: return (mod == 5 ? 0x75 : 0x4F) << 8;       /* End */
    case 2: return 0x52 << 8;                                    /* Ins */
    case 3: return 0x53 << 8;                                    /* Del */
    case 5: return (mod == 5 ? 0x84 : 0x49) << 8;               /* PgUp */
    case 6: return (mod == 5 ? 0x76 : 0x51) << 8;               /* PgDn */
    case 11: case 12: case 13: case 14: return fkey(n - 10, mod);
    case 15: return fkey(5, mod);
    case 17: case 18: case 19: case 20: case 21: return fkey(n - 11, mod);
    case 23: case 24: return fkey(n - 12, mod);
    }
    return 0;
}

static void *reader(void *arg)
{
    tty_t *t = arg;
    for (;;) {
        int c = rd(-1);
        if (c == -2) { pthread_mutex_lock(&t->mu); t->eof = 1; pthread_cond_broadcast(&t->cv); pthread_mutex_unlock(&t->mu); return NULL; }
        if (c < 0) continue;
        int k = 0, mod = 1;
        if (c == 27) {
            int c2 = rd(40);
            if (c2 < 0) { k = 0x011B; }                         /* plain Esc */
            else if (c2 == '[' || c2 == 'O') {
                int p[4] = { 0, 0, 0, 0 }, np = 0, f, sgr = 0;
                for (;;) {
                    f = rd(40);
                    if (f < 0) break;
                    if (f == '<' && np == 0 && p[0] == 0) { sgr = 1; continue; }
                    if (f >= '0' && f <= '9') { p[np] = p[np] * 10 + (f - '0'); continue; }
                    if (f == ';') { if (np < 3) np++; continue; }
                    break;
                }
                if (sgr && (f == 'M' || f == 'm')) {        /* mouse report */
                    int b = p[0];
                    if (b & 64) {                           /* wheel: arrow keys */
                        enqueue(t, (b & 1) ? 0x5000 : 0x4800);
                        continue;
                    }
                    pthread_mutex_lock(&t->mu);
                    t->mx = p[1] - 1; t->my = p[2] - 1;
                    if (!(b & 32)) {                        /* press / release */
                        static const int bit[3] = { 1, 4, 2 }; /* left, middle, right */
                        int which = bit[(b & 3) < 3 ? (b & 3) : 0];
                        if (f == 'M' && (b & 3) != 3) t->mbuttons |= which;
                        else t->mbuttons &= ~which;
                    }
                    pthread_mutex_unlock(&t->mu);
                    continue;
                }
                if (np >= 1) mod = p[1] ? p[1] : 1;
                if (f == '~') k = tilde_key(p[0], mod);
                else if (c2 == 'O' && f >= 'P' && f <= 'S') k = fkey(f - 'P' + 1, mod);
                else if (f == 'P' || f == 'Q' || f == 'R' || f == 'S') k = fkey(f - 'P' + 1, mod);
                else if (f == 'Z') k = 0x0F00;                  /* Shift-Tab */
                else k = cursor_key((char)f, mod);
            } else if (c2 >= 'a' && c2 <= 'z') k = alt_scan[c2 - 'a'] << 8;
            else if (c2 >= 'A' && c2 <= 'Z') k = alt_scan[c2 - 'A'] << 8;
            else if (c2 >= '1' && c2 <= '9') k = (0x78 + c2 - '1') << 8;   /* Alt-1.. */
            else if (c2 == '0') k = 0x8100;
            else k = 0x011B;
        } else if (c == 13 || c == 10) k = 0x1C0D;
        else if (c == 127 || c == 8) k = 0x0E08;
        else if (c == 9) k = 0x0F09;
        else if (c >= 0xC0) {                                   /* UTF-8: map back to CP437 */
            unsigned u = (unsigned)c & (c >= 0xE0 ? 0x0F : 0x1F);
            int more = c >= 0xE0 ? 2 : 1;
            for (int i = 0; i < more; i++) { int cc = rd(40); if (cc < 0) break; u = u << 6 | ((unsigned)cc & 0x3F); }
            k = '?';
            for (int i = 128; i < 256; i++) if (cp437[i] == u) { k = i; break; }
        } else k = c;
        if (!k) continue;
        t->shift = (mod - 1) & 1 ? 0x03 : 0;          /* shift held: both shift bits */
        if ((mod - 1) & 4) t->shift |= 0x04;          /* ctrl */
        if ((mod - 1) & 2) t->shift |= 0x08;          /* alt */
        enqueue(t, k);
    }
}

/* ---- host_t methods ------------------------------------------------------------------- */
static int t_key(host_t *h)
{
    tty_t *t = (tty_t *)h;
    pthread_mutex_lock(&t->mu);
    t->waiting = 1;
    while (t->qh == t->qt && !t->eof) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 100 * 1000000L;
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
        pthread_cond_timedwait(&t->cv, &t->mu, &ts);
        if (t->qh == t->qt && t->vm && t->vm->attention & 4) {   /* resize pending */
            t->waiting = 0;
            pthread_mutex_unlock(&t->mu);
            return -2;
        }
    }
    int k = -1;
    if (t->qh != t->qt) { k = t->q[t->qh]; t->qh = (t->qh + 1) % 256; }
    t->waiting = 0;
    pthread_mutex_unlock(&t->mu);
    return k;
}

static int t_keyq(host_t *h)
{
    tty_t *t = (tty_t *)h;
    pthread_mutex_lock(&t->mu);
    int r = t->qh != t->qt;
    pthread_mutex_unlock(&t->mu);
    return r;
}

static void t_refresh(host_t *h, vm_t *vm, int full)
{
    tty_t *t = (tty_t *)h;
    t->vm = vm;
    int cols = screen_cols(vm), rows = screen_rows(vm);
    const uint8_t *buf = vm->mem + screen_buf(vm);
    if (full || !t->shadow || cols != t->scols || rows != t->srows) {
        free(t->shadow);
        t->shadow = malloc((size_t)cols * rows * 2);
        memset(t->shadow, 0xFF, (size_t)cols * rows * 2);   /* nothing matches */
        t->scols = cols; t->srows = rows;
        t->cur_attr = -1;
        outs(t, "\033[0m\033[2J");
    }
    outs(t, "\033[?25l");
    for (int y = 0; y < rows; y++) {
        int last = -2;
        for (int x = 0; x < cols; x++) {
            size_t i = ((size_t)y * cols + x) * 2;
            if (buf[i] == t->shadow[i] && buf[i + 1] == t->shadow[i + 1]) continue;
            if (y == rows - 1 && x == cols - 1) {       /* avoid scrolling at the corner */
                t->shadow[i] = buf[i]; t->shadow[i + 1] = buf[i + 1];
            }
            if (x != last + 1) move_to(t, x, y);
            set_attr(t, buf[i + 1]);
            put_utf8(t, buf[i]);
            t->shadow[i] = buf[i]; t->shadow[i + 1] = buf[i + 1];
            last = x;
        }
    }
    int cx, cy;
    screen_cursor(vm, &cx, &cy);
    move_to(t, cx, cy);
    if (((sv(vm, SV_CURSOR) >> 8) & 0x20) == 0) outs(t, "\033[?25h");   /* $2000 = hidden */
    flushout(t);
}

static void t_emit(host_t *h, int c) { (void)h; (void)c; }       /* drawn from VIDEO-BUF */
static void t_type(host_t *h, const uint8_t *s, size_t n) { (void)h; (void)s; (void)n; }
static void t_flush(host_t *h) { tty_t *t = (tty_t *)h; if (t->vm) t_refresh(h, t->vm, 0); }
static void t_size(host_t *h, int *c, int *r) { (void)h; term_size(c, r); }
static int  t_shift(host_t *h) { return ((tty_t *)h)->shift; }
static int t_mouse_enable(host_t *h)
{
    tty_t *t = (tty_t *)h;
    if (!t->mouse) {
        t->mouse = 1;
        outs(t, "\033[?1002h\033[?1006h");       /* button + drag, SGR coordinates */
        flushout(t);
    }
    return 1;
}

static void t_mouse_state(host_t *h, int *x, int *y, int *b)
{
    tty_t *t = (tty_t *)h;
    pthread_mutex_lock(&t->mu);
    *x = t->mx; *y = t->my; *b = t->mbuttons;
    pthread_mutex_unlock(&t->mu);
}

static void t_suspend(host_t *h, int on)
{
    tty_t *t = (tty_t *)h;
    if (on) leave_screen(t);
    else { enter_screen(t); if (t->vm) t_refresh(h, t->vm, 1); }
}

host_t *host_tty_new(void)
{
    tty_t *t = calloc(1, sizeof *t);
    host_t *h = &t->h;
    h->emit = t_emit; h->type = t_type; h->key = t_key; h->keyq = t_keyq; h->flush = t_flush;
    h->size = t_size; h->refresh = t_refresh; h->suspend = t_suspend; h->shift = t_shift;
    h->mouse_enable = t_mouse_enable; h->mouse_state = t_mouse_state;
    tcgetattr(0, &t->saved);
    pthread_mutex_init(&t->mu, NULL);
    pthread_cond_init(&t->cv, NULL);
    the_tty = t;
    atexit(at_exit);
    signal(SIGWINCH, on_winch);
    signal(SIGTERM, on_fatal); signal(SIGSEGV, on_fatal); signal(SIGABRT, on_fatal);
    enter_screen(t);
    pthread_create(&t->reader, NULL, reader, t);
    /* refresh the screen ~30 times a second while the program is busy */
    signal(SIGALRM, on_alarm);
    struct itimerval it = { { 0, 33000 }, { 0, 33000 } };
    setitimer(ITIMER_REAL, &it, NULL);
    return h;
}

void host_tty_attach(host_t *h, vm_t *vm) { ((tty_t *)h)->vm = vm; }
