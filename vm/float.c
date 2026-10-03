/* float.c -- floating point for the F-PC VM.
 *
 * The original FFLOAT.SEQ drove an 8087. Here floats are IEEE doubles kept
 * on a separate float stack in VM memory (8 bytes each, little-endian), and
 * these primitives do the arithmetic in C. A float in memory (F@ F!) is the
 * same 8 bytes. FSP is the float stack pointer (a VM address, grows down).
 */
#include "vm.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FCELL 8

static double frd(vm_t *vm, ucell a) { double d; memcpy(&d, vm_ptr(vm, a, 8), 8); return d; }
static void fwr(vm_t *vm, ucell a, double d) { memcpy(vm_ptr(vm, a, 8), &d, 8); }

static ucell fsp(vm_t *vm) { return sv(vm, SV_FSP); }
static void fpush(vm_t *vm, double d)
{
    ucell p = fsp(vm) - FCELL;
    if (p < vm->fstack_lo) vm_throw(vm, E_RANGE, "Float stack overflow");
    fwr(vm, p, d);
    sv_set(vm, SV_FSP, p);
}
static double fpop(vm_t *vm)
{
    ucell p = fsp(vm);
    if (p >= vm->fstack_hi) vm_throw(vm, E_RANGE, "Float stack empty");
    sv_set(vm, SV_FSP, p + FCELL);
    return frd(vm, p);
}
static double ftop(vm_t *vm)
{
    if (fsp(vm) >= vm->fstack_hi) vm_throw(vm, E_RANGE, "Float stack empty");
    return frd(vm, fsp(vm));
}

#define F1(name, expr) void p_##name(vm_t *vm) { double a = fpop(vm); fpush(vm, (expr)); }
#define F2(name, expr) void p_##name(vm_t *vm) { double b = fpop(vm), a = fpop(vm); fpush(vm, (expr)); }
#define FCMP1(name, expr) void p_##name(vm_t *vm) { double a = fpop(vm); push(vm, (expr) ? TRUE_F : 0); }
#define FCMP2(name, expr) void p_##name(vm_t *vm) { double b = fpop(vm), a = fpop(vm); push(vm, (expr) ? TRUE_F : 0); }

F2(FPLUS, a + b)
F2(FMINUS, a - b)
F2(FSTAR, a * b)
F2(FSLASH, a / b)
F2(FSTARSTAR, pow(a, b))
F2(FATAN2, atan2(a, b))
F2(FMIN, a < b ? a : b)
F2(FMAX, a > b ? a : b)
F1(FNEGATE, -a)
F1(FABS, fabs(a))
F1(FSQRT, sqrt(a))
F1(FSIN, sin(a))
F1(FCOS, cos(a))
F1(FTAN, tan(a))
F1(FASIN, asin(a))
F1(FACOS, acos(a))
F1(FATAN, atan(a))
F1(FEXP, exp(a))
F1(FLN, log(a))
F1(FLOG, log10(a))
F1(FALOG, pow(10.0, a))
F1(FLOOR_F, floor(a))
F1(FROUND, nearbyint(a))
F1(FTRUNC, trunc(a))
FCMP1(FZEQ, a == 0.0)
FCMP1(FZLT, a < 0.0)
FCMP2(FLT, a < b)
FCMP2(FEQ, a == b)

void p_FDUP(vm_t *vm)   { fpush(vm, ftop(vm)); }
void p_FDROP(vm_t *vm)  { fpop(vm); }
void p_FSWAP(vm_t *vm)  { double b = fpop(vm), a = fpop(vm); fpush(vm, b); fpush(vm, a); }
void p_FOVER(vm_t *vm)  { double b = fpop(vm), a = fpop(vm); fpush(vm, a); fpush(vm, b); fpush(vm, a); }
void p_FROT(vm_t *vm)   { double c = fpop(vm), b = fpop(vm), a = fpop(vm); fpush(vm, b); fpush(vm, c); fpush(vm, a); }
void p_FDEPTH(vm_t *vm) { push(vm, (vm->fstack_hi - fsp(vm)) / FCELL); }
void p_FCLEAR(vm_t *vm) { sv_set(vm, SV_FSP, vm->fstack_hi); }
void p_FFETCH(vm_t *vm) { fpush(vm, frd(vm, pop(vm))); }
void p_FSTORE(vm_t *vm) { ucell a = pop(vm); fwr(vm, a, fpop(vm)); }
void p_SFFETCH(vm_t *vm) { float f; memcpy(&f, vm_ptr(vm, pop(vm), 4), 4); fpush(vm, f); }
void p_SFSTORE(vm_t *vm) { ucell a = pop(vm); float f = (float)fpop(vm); memcpy(vm_ptr(vm, a, 4), &f, 4); }
void p_FLOATS(vm_t *vm) { push(vm, pop(vm) * FCELL); }
void p_FLOATPLUS(vm_t *vm) { push(vm, pop(vm) + FCELL); }
void p_STOF(vm_t *vm)   { fpush(vm, (double)(cell)pop(vm)); }
void p_DTOF(vm_t *vm)   { fpush(vm, (double)(dcell)dpop(vm)); }
void p_FTOS(vm_t *vm)   { double a = fpop(vm); push(vm, (ucell)(cell)(a >= 2147483647.0 ? 2147483647.0 : a <= -2147483648.0 ? -2147483648.0 : a)); }
void p_FTOD(vm_t *vm)   { double a = fpop(vm); dpush(vm, (udcell)(dcell)trunc(a)); }
void p_FLIT(vm_t *vm)   /* runtime of FLITERAL: the float follows in the list */
{
    fpush(vm, frd(vm, vm->ip));
    vm->ip += FCELL;
}

/* >FLOAT ( a n -- true | false ) ( F: -- r | ) */
void p_TOFLOAT(vm_t *vm)
{
    ucell n = pop(vm), a = pop(vm);
    char buf[128];
    if (n == 0 || n >= sizeof buf) { push(vm, 0); return; }
    memcpy(buf, vm_ptr(vm, a, n), n);
    buf[n] = 0;
    for (ucell i = 0; i < n; i++) if (buf[i] == 'd' || buf[i] == 'D') buf[i] = 'e';
    char *end;
    double d = strtod(buf, &end);
    while (*end == ' ') end++;
    if (*end || end == buf) { push(vm, 0); return; }
    fpush(vm, d);
    push(vm, TRUE_F);
}

/* (F.) ( a n -- len ) ( F: r -- ) format r with n significant digits into a */
void p_PFDOT(vm_t *vm)
{
    ucell n = pop(vm), a = pop(vm);
    double d = fpop(vm);
    char buf[64];
    int prec = (int)sv(vm, SV_PRECISION);
    if (prec < 1) prec = 1;
    if (prec > 17) prec = 17;
    int len = snprintf(buf, sizeof buf, "%.*g", prec, d);
    if (len < 0) len = 0;
    if ((ucell)len > n) len = (int)n;
    memcpy(vm_ptr(vm, a, (ucell)len), buf, (size_t)len);
    push(vm, (ucell)len);
}

void float_init(vm_t *vm, ucell lo, ucell hi)
{
    vm->fstack_lo = lo;
    vm->fstack_hi = hi;
    sv_set(vm, SV_FSP, hi);
    sv_set(vm, SV_PRECISION, 6);
}
