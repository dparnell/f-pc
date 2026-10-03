/* host_sdl.c -- an SDL2 window host for the F-PC VM.
 *
 * Draws the VM's text screen (VIDEO-BUF) with a PC Screen Font (PSF1/PSF2,
 * as used by the Linux console; gzip-compressed fonts are fine), mapping
 * CP437 to the font's glyphs through its Unicode table. The window can be
 * resized freely: its size in character cells becomes COLS and ROWS.
 * Keys are turned into BIOS-style values; the mouse is reported in cells.
 *
 * The VM runs on the main thread, so SDL events are pumped whenever the VM
 * waits for a key, polls KEY?, or refreshes the screen (30 times a second
 * while busy), which is also when Control-C (or Control-Break) interrupts.
 *
 *   FPC_FONT=/path/font.psf[.gz]   font to use
 *   FPC_SCALE=n                    pixel scale (default 1, or 2 if the font is 8 wide)
 *   FPC_SDL_SNAPSHOT=file.bmp      write the window to a BMP after each redraw
 *   FPC_SDL_KEYS=text              type text at start-up (testing; \r is Enter)
 */
#ifdef HAVE_SDL
#include "vm.h"

#include <SDL2/SDL.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <zlib.h>

static const uint16_t cp437u[256] = {
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

/* CGA palette */
static const uint32_t cga[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
};

typedef struct {
    host_t h;
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex;
    uint32_t *pix;              /* window-sized framebuffer, cols*cw x rows*ch */
    int cw, ch, scale;          /* glyph cell in font pixels; scale */
    uint8_t *glyph[256];        /* bitmap per CP437 code, rows of bytes */
    int gstride;                /* bytes per glyph row */
    int cols, rows;             /* current cells */
    uint8_t *shadow;
    int scols, srows;
    int q[256], qh, qt;         /* key queue */
    int shift, mx, my, mbuttons, mouse;
    int cursor_x, cursor_y;
    vm_t *vm;
    int quit;
} sdl_t;

static sdl_t *the;

/* ---- PSF fonts --------------------------------------------------------------- */
static uint8_t *load_file(const char *path, size_t *len)
{
    gzFile f = gzopen(path, "rb");
    if (!f) return NULL;
    size_t cap = 1 << 16, n = 0;
    uint8_t *buf = malloc(cap);
    for (;;) {
        if (n == cap) buf = realloc(buf, cap *= 2);
        int r = gzread(f, buf + n, (unsigned)(cap - n));
        if (r <= 0) break;
        n += (size_t)r;
    }
    gzclose(f);
    *len = n;
    return buf;
}

static int utf8_next(const uint8_t **p, const uint8_t *end)
{
    const uint8_t *s = *p;
    if (s >= end) return -1;
    unsigned c = *s++, u;
    if (c < 0x80) u = c;
    else if (c < 0xE0 && s < end) u = (c & 0x1F) << 6 | (*s++ & 0x3F);
    else if (c < 0xF0 && s + 1 < end) { u = (c & 0x0F) << 12 | (s[0] & 0x3F) << 6 | (s[1] & 0x3F); s += 2; }
    else { u = '?'; while (s < end && (*s & 0xC0) == 0x80) s++; }
    *p = s;
    return (int)u;
}

/* map CP437 codes to glyphs of the font. The first font sets the cell size;
 * later fonts of the same size only fill codes still missing. Returns the
 * number of codes still missing, or -1 if the font can't be used. */
static int load_font(sdl_t *t, const char *path)
{
    size_t len;
    uint8_t *f = load_file(path, &len);
    if (!f) return -1;
    uint8_t *glyphs = NULL; int nglyphs = 0, charsize = 0, w = 8, h = 0;
    int *uni = NULL;            /* glyph index for codepoints < 0x10000 */
    if (len > 4 && f[0] == 0x36 && f[1] == 0x04) {                      /* PSF1 */
        int mode = f[2]; h = f[3]; charsize = h; w = 8;
        nglyphs = (mode & 1) ? 512 : 256;
        glyphs = f + 4;
        if ((mode & 2) && len >= 4 + (size_t)nglyphs * charsize) {
            uni = malloc(0x10000 * sizeof *uni);
            for (int i = 0; i < 0x10000; i++) uni[i] = -1;
            const uint8_t *p = glyphs + (size_t)nglyphs * charsize, *end = f + len;
            for (int g = 0; g < nglyphs && p + 1 < end; ) {
                unsigned v = p[0] | p[1] << 8; p += 2;
                if (v == 0xFFFF) { g++; continue; }
                if (v != 0xFFFE && uni[v] < 0) uni[v] = g;
            }
        }
    } else if (len > 32 && f[0] == 0x72 && f[1] == 0xB5 && f[2] == 0x4A && f[3] == 0x86) { /* PSF2 */
        uint32_t *hd = (uint32_t *)f;
        uint32_t hsize = hd[2], flags = hd[3];
        nglyphs = (int)hd[4]; charsize = (int)hd[5]; h = (int)hd[6]; w = (int)hd[7];
        glyphs = f + hsize;
        if ((flags & 1) && len >= hsize + (size_t)nglyphs * charsize) {
            uni = malloc(0x10000 * sizeof *uni);
            for (int i = 0; i < 0x10000; i++) uni[i] = -1;
            const uint8_t *p = glyphs + (size_t)nglyphs * charsize, *end = f + len;
            for (int g = 0; g < nglyphs && p < end; ) {
                if (*p == 0xFF) { g++; p++; continue; }
                if (*p == 0xFE) { p++; continue; }
                int u = utf8_next(&p, end);
                if (u >= 0 && u < 0x10000 && uni[u] < 0) uni[u] = g;
            }
        }
    } else { free(f); return -1; }
    if (h <= 0 || w <= 0 || w > 32) { free(f); free(uni); return -1; }
    if (t->cw && (w != t->cw || h != t->ch)) { free(f); free(uni); return -1; }
    t->cw = w; t->ch = h; t->gstride = (w + 7) / 8;
    int missing = 0, used = 0;
    for (int c = 0; c < 256; c++) {
        if (t->glyph[c]) continue;
        int g = uni ? uni[cp437u[c]] : c;
        static const uint16_t alias[][2] = {            /* look-alikes fonts often have */
            { 0x00A0, 0x0020 }, { 0x25BA, 0x25B6 }, { 0x25C4, 0x25C0 }, { 0x25AC, 0x2580 },
            { 0x2302, 0x0394 }, { 0x2310, 0x00AC }, { 0x263C, 0x00A4 }, { 0x2022, 0x00B7 },
        };
        for (size_t k = 0; uni && g < 0 && k < sizeof alias / sizeof *alias; k++)
            if (alias[k][0] == cp437u[c]) g = uni[alias[k][1]];
        if (g < 0 || g >= nglyphs) { missing++; continue; }
        t->glyph[c] = glyphs + (size_t)g * charsize;
        used = 1;
    }
    free(uni);
    if (!used) free(f);         /* else keep f: the glyphs point into it */
    return missing;
}

/* ---- generated glyphs: box drawing, blocks and shades (CP437 B0-DF, FE).
 * Drawn geometrically so lines join exactly whatever the font. ---------- */
static const uint8_t boxdirs[0xDB - 0xB3][4] = {   /* up down left right: 0, 1 single, 2 double */
    {1,1,0,0},{1,1,1,0},{1,1,2,0},{2,2,1,0},{0,2,1,0},{0,1,2,0},{2,2,2,0},{2,2,0,0}, /* B3-BA */
    {0,2,2,0},{2,0,2,0},{2,0,1,0},{1,0,2,0},{0,1,1,0},{1,0,0,1},{1,0,1,1},{0,1,1,1}, /* BB-C2 */
    {1,1,0,1},{0,0,1,1},{1,1,1,1},{1,1,0,2},{2,2,0,1},{2,0,0,2},{0,2,0,2},{2,0,2,2}, /* C3-CA */
    {0,2,2,2},{2,2,0,2},{0,0,2,2},{2,2,2,2},{1,0,2,2},{2,0,1,1},{0,1,2,2},{0,2,1,1}, /* CB-D2 */
    {2,0,0,1},{1,0,0,2},{0,1,0,2},{0,2,0,1},{2,2,1,1},{1,1,2,2},{1,0,1,0},{0,1,0,1}, /* D3-DA */
};

static void setpx(uint8_t *g, int stride, int w, int h, int x, int y)
{
    if (x >= 0 && x < w && y >= 0 && y < h) g[y * stride + (x >> 3)] |= (uint8_t)(0x80 >> (x & 7));
}

static void synth_glyphs(sdl_t *t)
{
    int w = t->cw, h = t->ch, st = t->gstride;
    int mx = w / 2, my = h / 2;
    for (int c = 0xB0; c <= 0xFE; c++) {
        if (c > 0xDF && c != 0xFE) continue;
        uint8_t *g = calloc((size_t)h, (size_t)st);
        if (c <= 0xB2) {                                   /* shades */
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    int on = c == 0xB0 ? ((x & 1) == 0 && (y & 1) == 0)
                           : c == 0xB1 ? ((x + y) & 1) == 0
                           : !((x & 1) == 1 && (y & 1) == 1);
                    if (on) setpx(g, st, w, h, x, y);
                }
        } else if (c <= 0xDA) {                            /* box drawing */
            const uint8_t *d = boxdirs[c - 0xB3];
            int vdbl = d[0] == 2 || d[1] == 2, hdbl = d[2] == 2 || d[3] == 2;
            for (int dir = 0; dir < 2; dir++) {
                int wgt = d[dir];
                if (!wgt) continue;
                int y0 = dir == 0 ? 0 : my, y1 = dir == 0 ? my : h - 1;
                int ext = hdbl ? 1 : 0;                    /* reach the far line of a double */
                if (dir == 0) y1 += ext; else y0 -= ext;
                if (wgt == 1) for (int y = y0; y <= y1; y++) setpx(g, st, w, h, mx, y);
                else for (int y = y0; y <= y1; y++) { setpx(g, st, w, h, mx - 1, y); setpx(g, st, w, h, mx + 1, y); }
            }
            for (int dir = 2; dir < 4; dir++) {
                int wgt = d[dir];
                if (!wgt) continue;
                int x0 = dir == 2 ? 0 : mx, x1 = dir == 2 ? mx : w - 1;
                int ext = vdbl ? 1 : 0;
                if (dir == 2) x1 += ext; else x0 -= ext;
                if (wgt == 1) for (int x = x0; x <= x1; x++) setpx(g, st, w, h, x, my);
                else for (int x = x0; x <= x1; x++) { setpx(g, st, w, h, x, my - 1); setpx(g, st, w, h, x, my + 1); }
            }
        } else {                                           /* blocks */
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    int on = c == 0xDB ? 1 : c == 0xDC ? y >= h / 2 : c == 0xDD ? x < w / 2
                           : c == 0xDE ? x >= w / 2 : c == 0xDF ? y < h / 2
                           : (x >= w / 4 && x < w - w / 4 && y >= h / 4 + 1 && y < h - h / 4 - 1);   /* FE */
                    if (on) setpx(g, st, w, h, x, y);
                }
        }
        t->glyph[c] = g;
    }
}

static int find_font(sdl_t *t)
{
    static const char *tries[] = {
        "/usr/share/consolefonts/Lat15-VGA16.psf.gz", "/usr/share/kbd/consolefonts/default8x16.psfu.gz",
        "/usr/share/kbd/consolefonts/lat9w-16.psfu.gz", "/usr/share/consolefonts/Lat2-VGA16.psf.gz",
        "/usr/share/consolefonts/FullGreek-TerminusBoldVGA16.psf.gz",
        "/usr/share/consolefonts/FullGreek-Terminus16.psf.gz", "/usr/share/consolefonts/FullGreek-Fixed16.psf.gz",
        "/usr/share/consolefonts/Uni2-VGA16.psf.gz", "/usr/share/consolefonts/Uni3-Terminus16.psf.gz",
    };
    const char *env = getenv("FPC_FONT");
    int missing = 256;
    if (env) missing = load_font(t, env);
    for (size_t i = 0; i < sizeof tries / sizeof *tries && missing != 0; i++) {
        int m = load_font(t, tries[i]);
        if (m >= 0) missing = m;
    }
    if (!t->cw) return -1;
    for (int c = 0; c < 256; c++)               /* anything left: a box outline */
        if (!t->glyph[c]) {
            uint8_t *g = calloc((size_t)t->ch, (size_t)t->gstride);
            for (int y = 2; y < t->ch - 2; y++)
                for (int x = 1; x < t->cw - 1; x++)
                    if (y == 2 || y == t->ch - 3 || x == 1 || x == t->cw - 2)
                        setpx(g, t->gstride, t->cw, t->ch, x, y);
            t->glyph[c] = g;
        }
    return 0;
}

/* ---- drawing ------------------------------------------------------------------------- */
static void draw_cell(sdl_t *t, int x, int y, int ch, int attr, int cursor)
{
    uint32_t fg = cga[attr & 15], bg = cga[(attr >> 4) & 15];  /* bit 7: bright bg */
    int W = t->cols * t->cw;
    const uint8_t *g = t->glyph[ch & 255];
    int curtop = t->ch - 2;
    for (int r = 0; r < t->ch; r++) {
        uint32_t *row = t->pix + (size_t)(y * t->ch + r) * W + x * t->cw;
        const uint8_t *bits = g + r * t->gstride;
        for (int c = 0; c < t->cw; c++) {
            int on = (bits[c >> 3] >> (7 - (c & 7))) & 1;
            if (cursor && r >= curtop) on = !on;
            row[c] = on ? fg : bg;
        }
    }
}

static void s_refresh(host_t *h, vm_t *vm, int full);

static void present(sdl_t *t)
{
    SDL_UpdateTexture(t->tex, NULL, t->pix, t->cols * t->cw * 4);
    SDL_RenderClear(t->ren);
    SDL_RenderCopy(t->ren, t->tex, NULL, NULL);
    SDL_RenderPresent(t->ren);
    const char *snap = getenv("FPC_SDL_SNAPSHOT");
    if (snap) {
        SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(t->pix, t->cols * t->cw, t->rows * t->ch, 32,
                                                            t->cols * t->cw * 4, SDL_PIXELFORMAT_ARGB8888);
        if (s) { SDL_SaveBMP(s, snap); SDL_FreeSurface(s); }
    }
}

static void resize_surfaces(sdl_t *t)
{
    free(t->pix);
    t->pix = calloc((size_t)t->cols * t->cw * t->rows * t->ch, 4);
    if (t->tex) SDL_DestroyTexture(t->tex);
    t->tex = SDL_CreateTexture(t->ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                               t->cols * t->cw, t->rows * t->ch);
    free(t->shadow); t->shadow = NULL;
}

static void s_refresh(host_t *h, vm_t *vm, int full)
{
    sdl_t *t = (sdl_t *)h;
    t->vm = vm;
    int cols = screen_cols(vm), rows = screen_rows(vm);
    if (cols != t->cols || rows != t->rows) {          /* VM caught up with a resize */
        t->cols = cols; t->rows = rows;
        resize_surfaces(t);
    }
    if (full || !t->shadow) {
        free(t->shadow);
        t->shadow = malloc((size_t)cols * rows * 2);
        memset(t->shadow, 0xFF, (size_t)cols * rows * 2);
    }
    const uint8_t *buf = vm->mem + screen_buf(vm);
    int cx, cy;
    screen_cursor(vm, &cx, &cy);
    int show = ((sv(vm, SV_CURSOR) >> 8) & 0x20) == 0;
    int changed = 0;
    for (int y = 0; y < rows; y++)
        for (int x = 0; x < cols; x++) {
            size_t i = ((size_t)y * cols + x) * 2;
            int iscur = show && x == cx && y == cy, wascur = x == t->cursor_x && y == t->cursor_y;
            if (buf[i] == t->shadow[i] && buf[i + 1] == t->shadow[i + 1] && iscur == wascur) continue;
            draw_cell(t, x, y, buf[i], buf[i + 1], iscur);
            t->shadow[i] = buf[i]; t->shadow[i + 1] = buf[i + 1];
            changed = 1;
        }
    if (t->cursor_x != (show ? cx : -1) || t->cursor_y != (show ? cy : -1)) changed = 1;
    t->cursor_x = show ? cx : -1; t->cursor_y = show ? cy : -1;
    if (changed) present(t);
}

/* ---- events -------------------------------------------------------------------------- */
static void enqueue(sdl_t *t, int k)
{
    if ((t->qt + 1) % 256 != t->qh) { t->q[t->qt] = k; t->qt = (t->qt + 1) % 256; }
}

static int keysym_to_bios(SDL_Keysym ks)
{
    int ctrl = ks.mod & KMOD_CTRL, alt = ks.mod & KMOD_ALT, shift = ks.mod & KMOD_SHIFT;
    SDL_Keycode k = ks.sym;
    static const struct { SDL_Keycode k; uint8_t n, s, c, a; } nav[] = {
        { SDLK_UP, 0x48, 0x48, 0x8D, 0x98 }, { SDLK_DOWN, 0x50, 0x50, 0x91, 0xA0 },
        { SDLK_LEFT, 0x4B, 0x4B, 0x73, 0x9B }, { SDLK_RIGHT, 0x4D, 0x4D, 0x74, 0x9D },
        { SDLK_HOME, 0x47, 0x47, 0x77, 0x97 }, { SDLK_END, 0x4F, 0x4F, 0x75, 0x9F },
        { SDLK_PAGEUP, 0x49, 0x49, 0x84, 0x99 }, { SDLK_PAGEDOWN, 0x51, 0x51, 0x76, 0xA1 },
        { SDLK_INSERT, 0x52, 0x52, 0x92, 0xA2 }, { SDLK_DELETE, 0x53, 0x53, 0x93, 0xA3 },
    };
    for (size_t i = 0; i < sizeof nav / sizeof *nav; i++)
        if (k == nav[i].k) return (ctrl ? nav[i].c : alt ? nav[i].a : shift ? nav[i].s : nav[i].n) << 8;
    if (k >= SDLK_F1 && k <= SDLK_F10) {
        int n = (int)(k - SDLK_F1);
        return (alt ? 0x68 + n : ctrl ? 0x5E + n : shift ? 0x54 + n : 0x3B + n) << 8;
    }
    if (k == SDLK_F11) return (alt ? 0x8B : ctrl ? 0x89 : shift ? 0x87 : 0x85) << 8;
    if (k == SDLK_F12) return (alt ? 0x8C : ctrl ? 0x8A : shift ? 0x88 : 0x86) << 8;
    if (k == SDLK_ESCAPE) return 0x011B;
    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) return ctrl ? 0x1C0A : 0x1C0D;
    if (k == SDLK_BACKSPACE) return 0x0E08;
    if (k == SDLK_TAB) return shift ? 0x0F00 : 0x0F09;
    if (alt && k >= SDLK_a && k <= SDLK_z) {
        static const uint8_t as[26] = { 0x1E,0x30,0x2E,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,
                                        0x31,0x18,0x19,0x10,0x13,0x1F,0x14,0x16,0x2F,0x11,0x2D,0x15,0x2C };
        return as[k - SDLK_a] << 8;
    }
    if (alt && k >= SDLK_1 && k <= SDLK_9) return (0x78 + (int)(k - SDLK_1)) << 8;
    if (alt && k == SDLK_0) return 0x8100;
    if (ctrl && k >= SDLK_a && k <= SDLK_z) return (int)(k - SDLK_a + 1);
    return 0;                   /* printable keys arrive as SDL_TEXTINPUT */
}

static void pump(sdl_t *t)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT: t->quit = 1; enqueue(t, -1); break;
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED && t->vm) screen_note_resize(t->vm);
            if (e.window.event == SDL_WINDOWEVENT_EXPOSED && t->vm) { free(t->shadow); t->shadow = NULL; }
            break;
        case SDL_KEYDOWN: {
            SDL_Keymod m = e.key.keysym.mod;
            t->shift = (m & KMOD_LSHIFT ? 2 : 0) | (m & KMOD_RSHIFT ? 1 : 0) | (m & KMOD_CTRL ? 4 : 0) | (m & KMOD_ALT ? 8 : 0);
            int k = keysym_to_bios(e.key.keysym);
            if (k == 3 && t->vm && !t->h.priv) t->vm->interrupt = 1;   /* busy: break */
            else if (k) enqueue(t, k);
            break; }
        case SDL_KEYUP: {
            SDL_Keymod m = e.key.keysym.mod;
            t->shift = (m & KMOD_LSHIFT ? 2 : 0) | (m & KMOD_RSHIFT ? 1 : 0) | (m & KMOD_CTRL ? 4 : 0) | (m & KMOD_ALT ? 8 : 0);
            break; }
        case SDL_TEXTINPUT: {
            const uint8_t *p = (const uint8_t *)e.text.text, *end = p + strlen(e.text.text);
            int u;
            while ((u = utf8_next(&p, end)) >= 0) {
                int c = u < 128 ? u : '?';
                for (int i = 128; i < 256 && u >= 128; i++) if (cp437u[i] == u) { c = i; break; }
                enqueue(t, c);
            }
            break; }
        case SDL_MOUSEMOTION:
            t->mx = e.motion.x / (t->cw * t->scale); t->my = e.motion.y / (t->ch * t->scale);
            break;
        case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP: {
            int bit = e.button.button == SDL_BUTTON_LEFT ? 1 : e.button.button == SDL_BUTTON_RIGHT ? 2 : 4;
            if (e.type == SDL_MOUSEBUTTONDOWN) t->mbuttons |= bit; else t->mbuttons &= ~bit;
            t->mx = e.button.x / (t->cw * t->scale); t->my = e.button.y / (t->ch * t->scale);
            break; }
        case SDL_MOUSEWHEEL:
            enqueue(t, e.wheel.y > 0 ? 0x4800 : 0x5000);
            break;
        }
    }
}

/* ---- host_t methods ------------------------------------------------------------------- */
static int s_key(host_t *h)
{
    sdl_t *t = (sdl_t *)h;
    h->priv = (void *)1;        /* waiting for a key: ^C is a key */
    for (;;) {
        pump(t);
        if (t->qh != t->qt) {
            int k = t->q[t->qh]; t->qh = (t->qh + 1) % 256;
            h->priv = NULL;
            return k;
        }
        if (t->vm && (t->vm->attention & 4)) { h->priv = NULL; return -2; }
        if (t->vm) s_refresh(h, t->vm, 0);
        SDL_WaitEventTimeout(NULL, 50);
    }
}

static int s_keyq(host_t *h)
{
    sdl_t *t = (sdl_t *)h;
    pump(t);
    return t->qh != t->qt;
}

static void s_refresh_hook(host_t *h, vm_t *vm, int full) { pump((sdl_t *)h); s_refresh(h, vm, full); }
static void s_emit(host_t *h, int c) { (void)h; (void)c; }
static void s_type(host_t *h, const uint8_t *s, size_t n) { (void)h; (void)s; (void)n; }
static void s_flush(host_t *h) { sdl_t *t = (sdl_t *)h; if (t->vm) s_refresh(h, t->vm, 0); }
static void s_size(host_t *h, int *c, int *r)
{
    sdl_t *t = (sdl_t *)h;
    int w, hh;
    SDL_GetWindowSize(t->win, &w, &hh);
    *c = w / (t->cw * t->scale); *r = hh / (t->ch * t->scale);
    if (*c < 20) *c = 20;
    if (*r < 5) *r = 5;
}
static int s_shift(host_t *h) { return ((sdl_t *)h)->shift; }
static int s_mouse_enable(host_t *h) { ((sdl_t *)h)->mouse = 1; return 1; }
static void s_mouse_state(host_t *h, int *x, int *y, int *b)
{
    sdl_t *t = (sdl_t *)h;
    pump(t);
    *x = t->mx; *y = t->my; *b = t->mbuttons;
}

static void on_alarm(int sig) { (void)sig; if (the && the->vm) the->vm->attention |= 2; }

host_t *host_sdl_new(void)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "fpc: SDL: %s\n", SDL_GetError());
        return NULL;
    }
    sdl_t *t = calloc(1, sizeof *t);
    if (find_font(t) != 0) { fprintf(stderr, "fpc: no PSF font found (set FPC_FONT)\n"); return NULL; }
    synth_glyphs(t);
    t->scale = getenv("FPC_SCALE") ? atoi(getenv("FPC_SCALE")) : 1;
    if (t->scale < 1) t->scale = 1;
    t->cols = 80; t->rows = 25;
    t->win = SDL_CreateWindow("F-PC", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              t->cols * t->cw * t->scale, t->rows * t->ch * t->scale,
                              SDL_WINDOW_RESIZABLE);
    if (!t->win) { fprintf(stderr, "fpc: SDL: %s\n", SDL_GetError()); return NULL; }
    t->ren = SDL_CreateRenderer(t->win, -1, 0);
    if (!t->ren) t->ren = SDL_CreateRenderer(t->win, -1, SDL_RENDERER_SOFTWARE);
    resize_surfaces(t);
    SDL_StartTextInput();
    host_t *h = &t->h;
    h->emit = s_emit; h->type = s_type; h->key = s_key; h->keyq = s_keyq; h->flush = s_flush;
    h->size = s_size; h->refresh = s_refresh_hook; h->shift = s_shift;
    h->mouse_enable = s_mouse_enable; h->mouse_state = s_mouse_state;
    the = t;
    const char *keys = getenv("FPC_SDL_KEYS");
    for (; keys && *keys; keys++)
        enqueue(t, *keys == '\r' || *keys == '\n' ? 0x1C0D : (uint8_t)*keys);
    signal(SIGALRM, on_alarm);
    struct itimerval it = { { 0, 33000 }, { 0, 33000 } };
    setitimer(ITIMER_REAL, &it, NULL);
    return h;
}

void host_sdl_attach(host_t *h, vm_t *vm) { ((sdl_t *)h)->vm = vm; }

#endif
