/* screen.c -- the virtual text screen and BIOS video/keyboard emulation.
 *
 * The screen is COLS x ROWS cells of (char, attribute) bytes in VM memory,
 * laid out like PC text-mode video memory (row stride COLS*2), at VIDEO-BUF.
 * F-PC code that wrote to $B800 writes there instead. The host renders it:
 * a streaming host (batch) is also told about each write as it happens; a
 * full-screen host diffs the buffer on refresh (kernel-design.md 6).
 *
 * The screen follows the terminal size: on a resize the buffer is
 * reallocated (VIDEO-BUF may move), COLS/ROWS change, RESIZED runs and the
 * next key read returns K-RESIZE (BIOS $0200, F-PC key 130).
 */
#include "vm.h"

#include <stdlib.h>
#include <string.h>

ucell heap_alloc_block(vm_t *vm, ucell n);     /* dos.c */
void  heap_free_block(vm_t *vm, ucell a);

#define K_RESIZE 0x0200             /* BIOS-style key value for a resize:
                                       scan 2 never comes from a real key */

typedef struct {
    int cols, rows;
    int x, y;                       /* cursor */
    ucell buf;                      /* VM address */
    int pending_resize;
    int resize_key;                 /* deliver K_RESIZE on next key read */
    int unget;                      /* a key value pushed back by KEY? */
    /* pop-up mapping (POPUP): F-PC's boxes are laid out for 80x25; output
       and cursor positions inside the box's designed rectangle pl..pr x
       pt..pb are shown moved by (pox, poy). pr < pl: no mapping. */
    int pl, pt, pr, pb, pox, poy;
} screen_t;

static screen_t *scr(vm_t *vm)
{
    if (!vm->screen) { vm->screen = calloc(1, sizeof(screen_t)); ((screen_t *)vm->screen)->pr = -1; }
    return vm->screen;
}

static uint8_t *cellp(vm_t *vm, int x, int y)
{
    screen_t *s = scr(vm);
    return vm->mem + s->buf + ((ucell)y * (ucell)s->cols + (ucell)x) * 2;
}

static void publish(vm_t *vm)
{
    screen_t *s = scr(vm);
    sv_set(vm, SV_COLS, (ucell)s->cols);
    sv_set(vm, SV_ROWS, (ucell)s->rows);
    sv_set(vm, SV_VIDEOBUF, s->buf);
}

/* (re)allocate the buffer for cols x rows, keeping the overlapping text */
static void screen_alloc(vm_t *vm, int cols, int rows)
{
    screen_t *s = scr(vm);
    if (cols < 20) cols = 20;
    if (rows < 5) rows = 5;
    if (cols > 400) cols = 400;
    if (rows > 200) rows = 200;
    ucell nb = heap_alloc_block(vm, (ucell)(cols * rows * 2));
    if (!nb) vm_throw(vm, E_RANGE, "No memory for the screen");
    int attr = (int)sv(vm, SV_ATTRIB) & 0xFF;
    for (int i = 0; i < cols * rows; i++) { vm->mem[nb + i * 2] = ' '; vm->mem[nb + i * 2 + 1] = (uint8_t)attr; }
    if (s->buf) {
        int ch = rows < s->rows ? rows : s->rows, cw = cols < s->cols ? cols : s->cols;
        /* keep the bottom of the old screen if it shrank vertically */
        int skip = s->rows > rows && s->y >= rows ? s->y - rows + 1 : 0;
        for (int y = 0; y < ch && y + skip < s->rows; y++)
            memcpy(vm->mem + nb + (ucell)(y * cols * 2),
                   vm->mem + s->buf + (ucell)((y + skip) * s->cols * 2), (size_t)cw * 2);
        s->y -= skip;
        heap_free_block(vm, s->buf);
    }
    s->buf = nb;
    s->cols = cols; s->rows = rows;
    if (s->x >= cols) s->x = cols - 1;
    if (s->y >= rows) s->y = rows - 1;
    publish(vm);
}

void screen_init(vm_t *vm)
{
    int c = 80, r = 25;
    if (vm->host->size) vm->host->size(vm->host, &c, &r);
    screen_alloc(vm, c, r);
}

/* called by hosts (from a signal handler: just set a flag) */
void screen_note_resize(vm_t *vm) { scr(vm)->pending_resize = 1; vm->attention |= 4; }

/* apply a pending resize; run RESIZED. Returns 1 if the size changed. */
int screen_check_resize(vm_t *vm)
{
    screen_t *s = scr(vm);
    if (!s->pending_resize) return 0;
    s->pending_resize = 0;
    vm->attention &= ~4;
    int c = s->cols, r = s->rows;
    if (vm->host->size) vm->host->size(vm->host, &c, &r);
    if (c == s->cols && r == s->rows) return 0;
    screen_alloc(vm, c, r);
    if (vm->host->refresh) vm->host->refresh(vm->host, vm, 1);
    s->resize_key = 1;
    ucell xt = vm->resized_xt;
    if (xt) vm_execute(vm, xt);
    return 1;
}

int screen_take_resize_key(vm_t *vm)
{
    screen_t *s = scr(vm);
    if (!s->resize_key) return 0;
    s->resize_key = 0;
    return K_RESIZE;
}

void screen_unget_key(vm_t *vm, int k) { scr(vm)->unget = k; }
int  scr_unget_peek(vm_t *vm) { return scr(vm)->unget; }
int  screen_unget_take(vm_t *vm) { int k = scr(vm)->unget; scr(vm)->unget = 0; return k; }

void screen_cursor(vm_t *vm, int *x, int *y) { *x = scr(vm)->x; *y = scr(vm)->y; }
int  screen_cols(vm_t *vm) { return scr(vm)->cols; }
int  screen_rows(vm_t *vm) { return scr(vm)->rows; }
ucell screen_buf(vm_t *vm) { return scr(vm)->buf; }

/* designed (x,y) -> where it is shown */
static int pop_in(screen_t *s, int x, int y) { return x >= s->pl && x <= s->pr && y >= s->pt && y <= s->pb; }
static void pop_map(screen_t *s, int *x, int *y)
{
    if (pop_in(s, *x, *y)) { *x += s->pox; *y += s->poy; }
}
/* a shown position -> the designed one */
static void pop_unmap(screen_t *s, int *x, int *y)
{
    if (pop_in(s, *x - s->pox, *y - s->poy)) { *x -= s->pox; *y -= s->poy; }
}

static void gotoxy(vm_t *vm, int x, int y)
{
    screen_t *s = scr(vm);
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= s->cols) x = s->cols - 1;
    if (y >= s->rows) y = s->rows - 1;
    s->x = x; s->y = y;
    if (vm->host->gotoxy) vm->host->gotoxy(vm->host, x, y);
}

/* scroll the window (x1,y1)-(x2,y2) up by n lines (n = 0: clear) */
static void scroll(vm_t *vm, int n, int attr, int x1, int y1, int x2, int y2, int down)
{
    screen_t *s = scr(vm);
    if (x2 >= s->cols) x2 = s->cols - 1;
    if (y2 >= s->rows) y2 = s->rows - 1;
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x1 > x2 || y1 > y2) return;
    int h = y2 - y1 + 1, w = x2 - x1 + 1;
    if (n <= 0 || n > h) n = h;
    for (int i = 0; i < h; i++) {
        int dy = down ? y2 - i : y1 + i, sy = down ? dy - n : dy + n;
        uint8_t *d = cellp(vm, x1, dy);
        if ((down ? sy >= y1 : sy <= y2)) memmove(d, cellp(vm, x1, sy), (size_t)w * 2);
        else for (int k = 0; k < w; k++) { d[k * 2] = ' '; d[k * 2 + 1] = (uint8_t)attr; }
    }
    if (vm->host->scrolled) vm->host->scrolled(vm->host, n, down);
}

/* BIOS TTY output at the cursor: CR LF BS BEL handled, wraps and scrolls */
void screen_tty(vm_t *vm, int c)
{
    screen_t *s = scr(vm);
    if (vm->host->tty) vm->host->tty(vm->host, c, (int)sv(vm, SV_ATTRIB));
    switch (c) {
    case 7: return;
    case 8: if (s->x > 0) s->x--; return;
    case 13: s->x = 0; return;
    case 10:
        if (++s->y >= s->rows) { scroll(vm, 1, cellp(vm, 0, s->rows - 1)[1], 0, 0, s->cols - 1, s->rows - 1, 0); s->y = s->rows - 1; }
        return;
    default:
        cellp(vm, s->x, s->y)[0] = (uint8_t)c;
        if (++s->x >= s->cols) {
            s->x = 0;
            if (++s->y >= s->rows) { scroll(vm, 1, cellp(vm, 0, s->rows - 1)[1], 0, 0, s->cols - 1, s->rows - 1, 0); s->y = s->rows - 1; }
        }
    }
}

/* write n chars at (x,y) with attr, clipped to the row; cursor not moved */
void screen_put(vm_t *vm, int x, int y, const uint8_t *str, int n, int attr)
{
    screen_t *s = scr(vm);
    if (vm->host->stream && vm->host->put && n > 0) vm->host->put(vm->host, x, y, str, (size_t)n, attr);
    if (y < 0 || y >= s->rows || x >= s->cols || n <= 0) return;
    if (x < 0) { str -= x; n += x; x = 0; }
    if (x + n > s->cols) n = s->cols - x;
    uint8_t *p = cellp(vm, x, y);
    for (int i = 0; i < n; i++) { p[i * 2] = str[i]; p[i * 2 + 1] = (uint8_t)attr; }
    if (vm->host->put && !vm->host->stream) vm->host->put(vm->host, x, y, str, (size_t)n, attr);
}

/* ---- primitives ---------------------------------------------------------- */
void p_VIDEOBUF(vm_t *vm) { push(vm, scr(vm)->buf); }

void p_VIDEOTYPE(vm_t *vm)                  /* ( a n -- ) at #OUT,#LINE */
{
    cell n = (cell)pop(vm);
    ucell a = pop(vm);
    if (n <= 0) return;
    screen_t *s = scr(vm);
    cell x = (cell)sv(vm, SV_OUT), y = (cell)sv(vm, SV_LINE);
    int px = (int)x, py = (int)y;               /* where it is shown */
    pop_map(s, &px, &py);
    if (py >= s->rows) { y -= py - (s->rows - 1); py = s->rows - 1; }
    if (py < 0) { y -= py; py = 0; }
    sv_set(vm, SV_LINE, (ucell)y);
    cell nx = x + n;                            /* #OUT stays designed */
    if (px + n >= s->cols) nx = x + (s->cols - 1 - px);
    sv_set(vm, SV_OUT, (ucell)nx);
    screen_put(vm, px, py, vm_ptr(vm, a, (ucell)n), n, (int)sv(vm, SV_ATTRIB));
    gotoxy(vm, px + (int)(nx - x), py);
}

void p_QVMODE(vm_t *vm)                     /* ( -- mode ) */
{
    screen_check_resize(vm);
    publish(vm);
    sv_set(vm, SV_CROWS, 7);
    push(vm, 3);
}

/* BIOS-VIDEO ( ax bx cx dx -- ax bx cx dx )   INT 10h subset */
void p_BIOSVIDEO(vm_t *vm)
{
    ucell dx = pop(vm), cx = pop(vm), bx = pop(vm), ax = pop(vm);
    screen_t *s = scr(vm);
    int ah = (ax >> 8) & 0xFF, al = ax & 0xFF;
    switch (ah) {
    case 0x00: break;                                       /* set mode */
    case 0x01: sv_set(vm, SV_CURSOR, cx & 0xFFFF);          /* cursor shape */
               if (vm->host->cursor_shape) vm->host->cursor_shape(vm->host, (int)(cx & 0xFFFF));
               break;
    case 0x02: { int x = (int)(dx & 0xFF), y = (int)((dx >> 8) & 0xFF);
        pop_map(s, &x, &y); gotoxy(vm, x, y); break; }
    case 0x03: { int x = s->x, y = s->y;
        pop_unmap(s, &x, &y);
        dx = (ucell)(y << 8 | x); cx = sv(vm, SV_CURSOR); break; }
    case 0x06: case 0x07: {                                 /* scroll up / down */
        int x1 = (int)(cx & 0xFF), y1 = (int)((cx >> 8) & 0xFF);
        int x2 = (int)(dx & 0xFF), y2 = (int)((dx >> 8) & 0xFF);
        pop_map(s, &x1, &y1); pop_map(s, &x2, &y2);
        scroll(vm, al, (int)((bx >> 8) & 0xFF), x1, y1, x2, y2, ah == 0x07);
        break; }
    case 0x08: {                                            /* read char+attr */
        uint8_t *p = cellp(vm, s->x, s->y);
        ax = (ucell)(p[1] << 8 | p[0]);
        break; }
    case 0x09: case 0x0A: {                                 /* write char (+attr) n times */
        int n = (int)(cx & 0xFFFF);
        for (int i = 0; i < n && s->x + i < s->cols; i++) {
            uint8_t *p = cellp(vm, s->x + i, s->y);
            p[0] = (uint8_t)al;
            if (ah == 0x09) p[1] = (uint8_t)bx;
            if (vm->host->put) vm->host->put(vm->host, s->x + i, s->y, p, 1, p[1]);
        }
        break; }
    case 0x0E: screen_tty(vm, al); break;                   /* teletype */
    case 0x0F: ax = (ucell)(s->cols << 8 | 3); bx = 0; break; /* get mode */
    case 0x10: break;                                       /* palette: ignored */
    default: break;
    }
    push(vm, ax); push(vm, bx); push(vm, cx); push(vm, dx);
}

void p_ATXY(vm_t *vm)                       /* ( x y -- ) move the cursor */
{
    int y = (int)pop(vm), x = (int)pop(vm);
    pop_map(scr(vm), &x, &y);
    gotoxy(vm, x, y);
}

void p_GETXY(vm_t *vm)                      /* ( -- x y ) */
{
    int x = scr(vm)->x, y = scr(vm)->y;
    pop_unmap(scr(vm), &x, &y);
    push(vm, (ucell)x);
    push(vm, (ucell)y);
}

/* POPUP ( l t r b -- l t r' b' ) a box laid out for 80x25 is being drawn.
 * The first box after POPUP-OFF picks the offset: centred as an 80 column
 * screen would be, moved to fit the screen when it is smaller; later boxes
 * share it and widen the mapped area. r' b' are clipped to the screen. */
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
void p_POPUP(vm_t *vm)
{
    screen_t *s = scr(vm);
    int b = (int)pop(vm), r = (int)pop(vm), t = (int)pop(vm), l = (int)pop(vm);
    if (s->pr < s->pl) {
        int lo = -l, hi = s->cols - 1 - r;
        s->pox = lo > hi ? lo : clampi((s->cols - 80) / 2, lo, hi);
        lo = -t; hi = s->rows - 1 - b;
        s->poy = lo > hi ? lo : clampi(0, lo, hi);
        s->pl = l; s->pt = t; s->pr = r; s->pb = b;
    } else {
        if (l < s->pl) s->pl = l;
        if (t < s->pt) s->pt = t;
        if (r > s->pr) s->pr = r;
        if (b > s->pb) s->pb = b;
    }
    if (r + s->pox > s->cols - 1) r = s->cols - 1 - s->pox;
    if (b + s->poy > s->rows - 1) b = s->rows - 1 - s->poy;
    push(vm, (ucell)l); push(vm, (ucell)t); push(vm, (ucell)r); push(vm, (ucell)b);
}
void p_POPUPOFF(vm_t *vm) { screen_t *s = scr(vm); s->pl = 0; s->pr = -1; s->pox = s->poy = 0; }
void p_POPUPFETCH(vm_t *vm)                 /* ( -- ox oy l t r b ) */
{
    screen_t *s = scr(vm);
    push(vm, (ucell)s->pox); push(vm, (ucell)s->poy);
    push(vm, (ucell)s->pl); push(vm, (ucell)s->pt); push(vm, (ucell)s->pr); push(vm, (ucell)s->pb);
}
void p_POPUPSTORE(vm_t *vm)                 /* ( ox oy l t r b -- ) */
{
    screen_t *s = scr(vm);
    s->pb = (int)pop(vm); s->pr = (int)pop(vm); s->pt = (int)pop(vm); s->pl = (int)pop(vm);
    s->poy = (int)pop(vm); s->pox = (int)pop(vm);
}

void p_REFRESH(vm_t *vm)                    /* ( -- ) show the screen now */
{
    if (vm->host->refresh) vm->host->refresh(vm->host, vm, 0);
    vm->host->flush(vm->host);
}

/* ---- mouse (MOUSE.SEQ). Positions are in character cells. A host that
 * cannot report the mouse says so; MOUSE! positions are remembered so that
 * MOUSE@ returns them until the mouse moves. ---------------------------- */
static int mouse_x, mouse_y, mouse_on;
void p_MOUSEPRESENTQ(vm_t *vm)
{
    mouse_on = vm->host->mouse_enable && vm->host->mouse_enable(vm->host);
    push(vm, mouse_on ? TRUE_F : 0);
}
void p_MOUSEFETCH(vm_t *vm)
{
    int b = 0;
    if (mouse_on && vm->host->mouse_state) vm->host->mouse_state(vm->host, &mouse_x, &mouse_y, &b);
    screen_t *s = scr(vm);
    if (mouse_x >= s->cols) mouse_x = s->cols - 1;
    if (mouse_y >= s->rows) mouse_y = s->rows - 1;
    int x = mouse_x, y = mouse_y;
    pop_unmap(s, &x, &y);                   /* inside a pop-up: its coordinates */
    push(vm, (ucell)x); push(vm, (ucell)y); push(vm, (ucell)b);
}
void p_MOUSESTORE(vm_t *vm)
{
    int y = (int)pop(vm), x = (int)pop(vm);
    screen_t *s = scr(vm);
    mouse_x = x < 0 ? 0 : x >= s->cols ? s->cols - 1 : x;
    mouse_y = y < 0 ? 0 : y >= s->rows ? s->rows - 1 : y;
}
void p_MOUSESHOW(vm_t *vm) { (void)vm; }
void p_MOUSEHIDE(vm_t *vm) { (void)vm; }
