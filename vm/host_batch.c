/* host_batch.c -- plain stdin/stdout host: no screen, no cursor addressing.
 * Used for scripts and the regression tests. Screen writes go to stdout in
 * the order they are made; positions are ignored. */
#include "vm.h"

#include <poll.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>

/* Bytes 128-255 are CP437 characters (box drawing etc.): they are written
 * as UTF-8, unless FPC_RAW is set. Control codes stay control codes. */
static int raw = -1;
static void out_byte(int c)
{
    if (raw < 0) raw = getenv("FPC_RAW") != NULL;
    if (c < 128 || raw) { putchar(c); return; }
    char u[3];
    fwrite(u, 1, (size_t)cp437_utf8((uint8_t)c, u), stdout);
}
static void b_emit(host_t *h, int c) { (void)h; if (c != '\r') out_byte(c & 0xFF); }
static void b_type(host_t *h, const uint8_t *s, size_t n) { (void)h; for (size_t i = 0; i < n; i++) out_byte(s[i]); }
/* KEY? is true only when input is waiting; at end of input it stays false
 * and KEY-EOF? (host->eof) becomes true, so (KEY) stops waiting and KEY
 * ends the session. After 200 ms of polling without input, polls wait
 * 10 ms so that a loop waiting for a key does not spin the CPU. */
static int pending = -2;
static double idle_since;

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1e9;
}

static int b_keyq(host_t *h)
{
    (void)h;
    if (pending != -2) return pending != EOF;
    struct pollfd p = { 0, POLLIN, 0 };
    if (!idle_since) idle_since = now();
    int wait = now() - idle_since > 0.2 ? 10 : 0;
    if (poll(&p, 1, wait) <= 0) return 0;
    idle_since = 0;
    pending = getchar();
    return pending != EOF;
}

static int b_eof(host_t *h) { (void)h; return pending == EOF; }

static int  b_key(host_t *h)
{
    (void)h;
    fflush(stdout);
    int c;
    idle_since = 0;
    if (pending != -2) { c = pending; pending = -2; if (c == EOF) pending = EOF; }
    else c = getchar();
    if (c == EOF) return -1;
    if (c == '\n') return 13;               /* Enter */
    if (c == '\r') return b_key(h);
    if (c == 8 || c == 127) return 0x0E08;  /* Backspace */
    return c & 0xFF;
}
static void b_flush(host_t *h) { (void)h; fflush(stdout); }
static void b_put(host_t *h, int x, int y, const uint8_t *s, size_t n, int attr)
{
    (void)x; (void)y; (void)attr;
    b_type(h, s, n);
}
static void b_gotoxy(host_t *h, int x, int y) { (void)h; (void)x; (void)y; }
static void b_tty(host_t *h, int c, int attr) { (void)attr; if (c != 7) b_emit(h, c); }
static void b_size(host_t *h, int *c, int *r) { (void)h; *c = 80; *r = 25; }

host_t *host_batch_new(void)
{
    host_t *h = calloc(1, sizeof *h);
    h->emit = b_emit; h->type = b_type; h->key = b_key; h->keyq = b_keyq; h->flush = b_flush;
    h->eof = b_eof;
    h->stream = 1;
    h->put = b_put; h->gotoxy = b_gotoxy; h->tty = b_tty; h->size = b_size;
    return h;
}
