/* host_batch.c -- plain stdin/stdout host: no screen, no cursor addressing.
 * Used for scripts and the regression tests. */
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>

static void b_emit(host_t *h, int c) { (void)h; putchar(c); }
static void b_type(host_t *h, const uint8_t *s, size_t n) { (void)h; fwrite(s, 1, n, stdout); }
static int  b_key(host_t *h) { (void)h; fflush(stdout); int c = getchar(); return c == EOF ? -1 : c; }
static int  b_keyq(host_t *h) { (void)h; return 0; }
static void b_flush(host_t *h) { (void)h; fflush(stdout); }

host_t *host_batch_new(void)
{
    host_t *h = calloc(1, sizeof *h);
    h->emit = b_emit; h->type = b_type; h->key = b_key; h->keyq = b_keyq; h->flush = b_flush;
    return h;
}
