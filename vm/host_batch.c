/* host_batch.c -- plain stdin/stdout host: no screen, no cursor addressing.
 * Used for scripts and the regression tests. Screen writes go to stdout in
 * the order they are made; positions are ignored. */
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>

static void b_emit(host_t *h, int c) { (void)h; if (c != '\r') putchar(c); }
static void b_type(host_t *h, const uint8_t *s, size_t n) { (void)h; fwrite(s, 1, n, stdout); }
static int  b_key(host_t *h)
{
    (void)h;
    fflush(stdout);
    int c = getchar();
    if (c == EOF) return -1;
    if (c == '\n') return 13;               /* Enter */
    if (c == '\r') return b_key(h);
    if (c == 8 || c == 127) return 0x0E08;  /* Backspace */
    return c & 0xFF;
}
static int  b_keyq(host_t *h) { (void)h; return 1; }   /* a read never fails to return */
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
    h->put = b_put; h->gotoxy = b_gotoxy; h->tty = b_tty; h->size = b_size;
    return h;
}
