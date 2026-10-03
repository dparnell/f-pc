/* image.c -- save and load F-PC system images (kernel-design.md 7).
 *
 * An image holds the used part of each memory region and the code-token
 * handlers created after the built-ins (DOES> clauses, abstract-machine
 * code). It never contains host pointers or machine code. Built-in tokens
 * are identified by a checksum of the built-in names: an image only loads
 * into the same fpc build that the seed lays out identically.
 */
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMG_MAGIC   "FPCIMG\r\n"
#define IMG_VERSION 1

static uint32_t builtin_sum(vm_t *vm)
{
    uint32_t h = 2166136261u;
    for (ucell t = 0; t < T_NBUILTIN; t++)
        for (const char *p = vm->handlers[t].name; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
    return h;
}

static void put32(FILE *f, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    fwrite(b, 1, 4, f);
}
static int get32(FILE *f, uint32_t *v)
{
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4) return -1;
    *v = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
    return 0;
}

/* end of the last used heap block */
static ucell heap_top(vm_t *vm)
{
    ucell top = vm->heap_base, b = (vm->heap_base + 15) & ~15u;
    if (!vm->heap_free) return top;
    while (b < vm->heap_end) {
        ucell size = rd32(vm, b);
        if (!size) break;
        if (rd32(vm, b + 4)) top = b + size;
        b += size;
    }
    return top;
}

int image_save(vm_t *vm, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fwrite(IMG_MAGIC, 1, 8, f);
    put32(f, IMG_VERSION);
    put32(f, CELL);
    put32(f, builtin_sum(vm));
    put32(f, vm->memsize);
    ucell regions[4][2] = {
        { 0, uv(vm, U_DP) },
        { vm->list_base, sv(vm, SV_XDP) },
        { vm->head_base, sv(vm, SV_YDP) },
        { vm->heap_base, heap_top(vm) },
    };
    put32(f, 4);
    for (int i = 0; i < 4; i++) {
        ucell a = regions[i][0], e = regions[i][1];
        if (e < a) e = a;
        put32(f, a); put32(f, e - a);
        fwrite(vm->mem + a, 1, e - a, f);
    }
    put32(f, vm->heap_free);
    put32(f, vm->nhandlers - T_NBUILTIN);
    for (ucell t = T_NBUILTIN; t < vm->nhandlers; t++) {
        vm_handler *h = &vm->handlers[t];
        fputc(h->kind, f);
        put32(f, h->data);
    }
    int bad = ferror(f);
    if (fclose(f) != 0 || bad) return -1;
    return 0;
}

/* load into a VM fresh from vm_new + seed_init; 0 on success */
int image_load(vm_t *vm, const char *path, char *err, size_t errsz)
{
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, errsz, "cannot open %s", path); return -1; }
    char magic[8];
    uint32_t ver, cell, sum, memsize, nreg;
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, IMG_MAGIC, 8) || get32(f, &ver) || get32(f, &cell)
        || get32(f, &sum) || get32(f, &memsize) || get32(f, &nreg)) {
        snprintf(err, errsz, "%s: not an fpc image", path); fclose(f); return -1;
    }
    if (ver != IMG_VERSION || cell != CELL || memsize != vm->memsize) {
        snprintf(err, errsz, "%s: incompatible image format", path); fclose(f); return -1;
    }
    if (sum != builtin_sum(vm)) {
        snprintf(err, errsz, "%s: built by a different fpc; rebuild it", path); fclose(f); return -1;
    }
    for (uint32_t i = 0; i < nreg; i++) {
        uint32_t a, n;
        if (get32(f, &a) || get32(f, &n) || a > vm->memsize || n > vm->memsize - a
            || fread(vm->mem + a, 1, n, f) != n) {
            snprintf(err, errsz, "%s: truncated", path); fclose(f); return -1;
        }
    }
    uint32_t hfree, nh;
    if (get32(f, &hfree) || get32(f, &nh)) { snprintf(err, errsz, "%s: truncated", path); fclose(f); return -1; }
    vm->heap_free = hfree;
    vm->nhandlers = T_NBUILTIN;
    for (uint32_t i = 0; i < nh; i++) {
        int kind = fgetc(f);
        uint32_t data;
        if (kind == EOF || get32(f, &data)) { snprintf(err, errsz, "%s: truncated", path); fclose(f); return -1; }
        ucell ct = vm_add_handler(vm, kind, NULL, data, NULL);
        if (kind == HK_AMCODE) am_bind(vm, &vm->handlers[ct]);   /* ops are in the image */
    }
    fclose(f);
    vm->sp = vm->sp0;
    vm->rp = vm->rp0;
    return 0;
}

/* (SAVE-IMAGE) ( a n -- ior ): write the image to the host path a n */
void p_SAVEIMAGE(vm_t *vm)
{
    ucell n = pop(vm), a = pop(vm);
    char path[1024];
    if (n >= sizeof path) n = sizeof path - 1;
    memcpy(path, vm_ptr(vm, a, n), n);
    path[n] = 0;
    for (char *p = path; *p; p++) if (*p == '\\') *p = '/';
    push(vm, image_save(vm, path) ? (ucell)-37 : 0);
}
