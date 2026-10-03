/* vm.c -- F-PC native VM: memory layout, handler table, inner interpreter. */
#include "vm.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(__GNUC__)
#error "the dispatch loop needs computed goto (GCC or Clang)"
#endif

static vm_handler builtin_handlers[T_NBUILTIN] = {
#define PRIM(id, nm, flags)  { .kind = HK_PRIM, .name = nm },
#define CPRIM(id, nm, flags) { .kind = HK_CPRIM, .fn = p_##id, .name = nm },
#include "prims.def"
#undef PRIM
#undef CPRIM
};

/* ---- errors ------------------------------------------------------------- */
_Noreturn void vm_throw(vm_t *vm, int code, const char *fmt, ...)
{
    vm->throw_code = code;
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(vm->msgbuf, sizeof vm->msgbuf, fmt, ap);
        va_end(ap);
        vm->throw_msg = vm->msgbuf;
    } else {
        vm->throw_msg = NULL;
    }
    if (!vm->catch_jmp) {
        fprintf(stderr, "fpc: uncaught error %d: %s\n", code,
                vm->throw_msg ? vm->throw_msg : "");
        exit(2);
    }
    longjmp(*vm->catch_jmp, 1);
}

void vm_bad_address(vm_t *vm, ucell a, ucell n)
{
    vm_throw(vm, E_BADADDR, "Invalid memory access at $%X (%u bytes)", a, n);
}

/* ---- handler table ----------------------------------------------------------- */
ucell vm_add_handler(vm_t *vm, int kind, vm_cfn fn, ucell data, const char *name)
{
    if (vm->nhandlers == vm->maxhandlers) {
        vm->maxhandlers *= 2;
        vm->handlers = realloc(vm->handlers, vm->maxhandlers * sizeof *vm->handlers);
        if (!vm->handlers) { perror("fpc"); exit(2); }
    }
    vm_handler *h = &vm->handlers[vm->nhandlers];
    h->kind = (uint8_t)kind; h->fn = fn; h->data = data; h->name = name;
    h->jit_state = 0; h->jit = NULL;
    return vm->nhandlers++;
}

/* ---- construction ------------------------------------------------------------ */
vm_t *vm_new(ucell memsize, host_t *host)
{
    vm_t *vm = calloc(1, sizeof *vm);
    if (!vm) return NULL;
    vm->memsize = memsize;
    vm->mem = calloc(1, memsize + 16);          /* + slack for masked JIT access */
    if (!vm->mem) { free(vm); return NULL; }
    vm->host = host;

    vm->maxhandlers = 1024;
    vm->handlers = malloc(vm->maxhandlers * sizeof *vm->handlers);
    memcpy(vm->handlers, builtin_handlers, sizeof builtin_handlers);
    vm->nhandlers = T_NBUILTIN;

    /* fixed system area */
    ucell a = MEM_RESERVED;
    vm->sysvar = a;            a += SV_COUNT * 8;
    for (int i = 0; i < SV_COUNT; i++)          /* each slot is a VARIABLE or VALUE */
        wr32(vm, vm->sysvar + (ucell)i * 8, SV_IS_VALUE(i) ? T_DOVALUE : T_DOVAR);
    vm->tib = a;               a += TIB_SIZE;
    vm->dosbuf = a;            a += 512;   /* DTA, DOS-LINE, FPC-HOME */
    ucell up = a;              a += USER_CELLS * CELL;
    vm->tramp = a;             a += NTRAMP * 8 + 8;     /* + the HALT code field */
    wr32(vm, vm->tramp + NTRAMP * 8, T_HALT);
    a = aligned(a);
    a += DSTACK_CELLS * CELL;  vm->sp0 = a;
    a += 64;                                            /* underflow slack */
    a += RSTACK_CELLS * CELL;  vm->rp0 = a;
    a += 64;
    a = (a + 15) & ~15u;

    /* regions: code 1/6, list 1/6, head 1/12, rest heap */
    ucell rest = memsize - a;
    vm->code_base = a;               vm->code_end = a + (rest / 6 & ~15u);
    vm->list_base = vm->code_end;    vm->list_end = vm->list_base + (rest / 6 & ~15u);
    vm->head_base = vm->list_end;    vm->head_end = vm->head_base + (rest / 12 & ~15u);
    vm->heap_base = vm->head_end;    vm->heap_end = memsize;

    vm->sp = vm->sp0;
    vm->rp = vm->rp0;
    sv_set(vm, SV_UP, up);
    uv_set(vm, U_SP0, vm->sp0);
    uv_set(vm, U_RP0, vm->rp0);
    uv_set(vm, U_DP, vm->code_base);
    uv_set(vm, U_BASE, 10);
    sv_set(vm, SV_XDP, vm->list_base);
    sv_set(vm, SV_YDP, vm->head_base);
    sv_set(vm, SV_WIDTH, 31);
    sv_set(vm, SV_CAPS, TRUE_F);
    sv_set(vm, SV_WARNING, TRUE_F);
    sv_set(vm, SV_TICKTIB, vm->tib);
    sv_set(vm, SV_ATTRIB, 7);
    sv_set(vm, SV_CURSOR, 0x0607);
    {
        int c = 80, r = 25;
        if (host->size) host->size(host, &c, &r);
        sv_set(vm, SV_COLS, (ucell)c); sv_set(vm, SV_ROWS, (ucell)r); sv_set(vm, SV_CROWS, 7);
    }
    return vm;
}

void vm_free(vm_t *vm)
{
    if (!vm) return;
    free(vm->handlers);
    free(vm->mem);
    free(vm);
}

/* ---- console output through the EMIT / TYPE user defers --------------------- */
void vm_emit(vm_t *vm, int c)
{
    ucell xt = uv(vm, U_EMIT);
    if (xt == 0 || rd32(vm, xt) == T_PEMIT) {
        vm->host->emit(vm->host, c);
        sv_set(vm, SV_OUT, c == '\n' ? 0 : sv(vm, SV_OUT) + 1);
        return;
    }
    push(vm, (ucell)c);
    vm_execute(vm, xt);
}

void vm_type(vm_t *vm, const uint8_t *s, size_t n)
{
    ucell xt = uv(vm, U_TYPE);
    if (xt == 0 || rd32(vm, xt) == T_PTYPE || (s < vm->mem || s >= vm->mem + vm->memsize)) {
        vm->host->type(vm->host, s, n);
        sv_set(vm, SV_OUT, sv(vm, SV_OUT) + (ucell)n);
        return;
    }
    push(vm, (ucell)(s - vm->mem));
    push(vm, (ucell)n);
    vm_execute(vm, xt);
}

void vm_types(vm_t *vm, const char *s)
{
    vm->host->type(vm->host, (const uint8_t *)s, strlen(s));
    sv_set(vm, SV_OUT, sv(vm, SV_OUT) + (ucell)strlen(s));
}

/* ---- arithmetic helpers ---------------------------------------------------------- */
static cell fdiv(vm_t *vm, cell a, cell b, cell *rem)
{
    if (b == 0 || (a == INT32_MIN && b == -1))
        vm_throw(vm, E_DIV0, "Division by zero or overflow");
    cell q = a / b, r = a % b;
    if (r != 0 && ((r < 0) != (b < 0))) { q--; r += b; }
    *rem = r;
    return q;
}

/* floored 64/32 -> 32; quotient must fit */
static cell fdiv64(vm_t *vm, dcell a, cell b, cell *rem)
{
    if (b == 0) vm_throw(vm, E_DIV0, "Division by zero");
    dcell q = a / b, r = a % b;
    if (r != 0 && ((r < 0) != (b < 0))) { q--; r += b; }
    if (q < INT32_MIN || q > INT32_MAX) vm_throw(vm, E_RANGE, "Division overflow");
    *rem = (cell)r;
    return (cell)q;
}

/* a signal asked for attention: Control-C, or a screen refresh is due */
static void attend(vm_t *vm)
{
    if (vm->interrupt) { vm->interrupt = 0; vm_throw(vm, E_INTERRUPT, "Interrupted"); }
    if (vm->attention & 2) {
        vm->attention &= ~2;
        if (vm->host->refresh) vm->host->refresh(vm->host, vm, 0);
    }
}

static ucell name_to_cfa(vm_t *vm, ucell nfa)
{
    return rd32(vm, nfa + 1 + (rd8(vm, nfa) & 31));
}

/* ---- inner interpreter ------------------------------------------------------------- */
void vm_execute(vm_t *vm, ucell xt)
{
    static void *labels[T_NBUILTIN] = {
#define PRIM(id, name, flags)  &&L_##id,
#define CPRIM(id, name, flags) &&L_CPRIM,
#include "prims.def"
#undef PRIM
#undef CPRIM
    };

    if (vm->depth >= NTRAMP)
        vm_throw(vm, E_RSTACK_OVER, "C/Forth nesting too deep");
    ucell tr = vm->tramp + (ucell)vm->depth * 8;
    wr32(vm, tr, xt);
    wr32(vm, tr + 4, vm->tramp + NTRAMP * 8);   /* the HALT code field */
    vm->depth++;
    rpush(vm, vm->ip);

    ucell ip = tr, sp = vm->sp, rp = vm->rp, w = 0, ct;
    ucell t1, t2, t3;
    cell  n1, n2, n3;
    vm_handler *h;

#define SYNC()   (vm->ip = ip, vm->sp = sp, vm->rp = rp, vm->w = w)
#define RELOAD() (ip = vm->ip, sp = vm->sp, rp = vm->rp)
#define S(n)     rd32(vm, sp + 4u * (n))
#define SSET(n, v) wr32(vm, sp + 4u * (n), (ucell)(v))
#define PUSH(v)  (sp -= 4, wr32(vm, sp, (ucell)(v)))
#define POP()    (sp += 4, rd32(vm, sp - 4))
#define RPUSH(v) (rp -= 4, wr32(vm, rp, (ucell)(v)))
#define RPOP()   (rp += 4, rd32(vm, rp - 4))
#define R(n)     rd32(vm, rp + 4u * (n))
#define INLINE() (ip += 4, rd32(vm, ip - 4))
#define NEXT     goto next
#define CHECK_INTERRUPT() do { if (vm->interrupt | vm->attention) { SYNC(); \
        attend(vm); } } while (0)

next:
    /* debugger trace hook (DEBUG.SEQ): when DBG-ON and the IP is in
     * [DBG-LO, DBG-HI), call 'DEBUG with the IP, as if from here. The same
     * IP is not reported twice in a row (the trace word returns to it). */
    if (__builtin_expect(rd32(vm, sv_addr(vm, SV_DBGON)) != 0, 0)
        && ip >= sv(vm, SV_DBGLO) && ip < sv(vm, SV_DBGHI)) {
        if (ip != vm->dbg_last) {
            sv_set(vm, SV_DBGON, 0);
            vm->dbg_last = ip;
            PUSH(ip);
            w = sv(vm, SV_TICKDEBUG);
            goto exec;
        }
        vm->dbg_last = 0;                   /* reported last time: run it */
    }
    w = rd32(vm, ip); ip += 4;
exec:
    ct = rd32(vm, w);
    if (ct < T_NBUILTIN) goto *labels[ct];
    if (ct >= vm->nhandlers) { SYNC(); vm_throw(vm, E_BADADDR, "Invalid code field at $%X", w); }
    h = &vm->handlers[ct];
    switch (h->kind) {
    case HK_DOES:
        PUSH(w + 4); RPUSH(ip); ip = h->data; NEXT;
    case HK_CPRIM: case HK_AMCODE:
        SYNC(); vm->cur_handler = h; h->fn(vm); RELOAD(); NEXT;
    default:
        SYNC(); vm_throw(vm, E_BADADDR, "Invalid code field at $%X", w);
    }

L_CPRIM:
    SYNC(); vm->handlers[ct].fn(vm); RELOAD();
    if (vm->bye) { SYNC(); vm_throw(vm, E_BYE, NULL); }
    NEXT;

    /* ---- code-field handlers ---- */
L_INVALID: SYNC(); vm_throw(vm, E_BADADDR, "Invalid code field at $%X", w);
L_NEST:    CHECK_INTERRUPT(); RPUSH(ip); ip = rd32(vm, w + 4); NEXT;
L_DOVAR:   PUSH(w + 4); NEXT;
L_DOCONST:
L_DOVALUE: PUSH(rd32(vm, w + 4)); NEXT;
L_DODEFER:
    t1 = rd32(vm, w + 4);
    if (!t1) { SYNC(); vm_throw(vm, E_UNDEFINED, "Uninitialized DEFER"); }
    w = t1; goto exec;
L_DOUSER:  PUSH(vm_up(vm) + rd32(vm, w + 4)); NEXT;
L_DOUSERDEFER:
    t1 = rd32(vm, vm_up(vm) + rd32(vm, w + 4));
    if (!t1) { SYNC(); vm_throw(vm, E_UNDEFINED, "Uninitialized DEFER"); }
    w = t1; goto exec;
L_HALT:
    ip = RPOP();
    vm->depth--;
    SYNC();
    return;

    /* ---- threading / control ---- */
L_EXIT:
L_UNNEST:  ip = RPOP(); NEXT;
L_QEXIT:   if (POP()) ip = RPOP(); NEXT;
L_LIT:
L_TICKLIT: PUSH(INLINE()); NEXT;
L_BRANCH:
L_DOAGAIN:
L_DOREPEAT: CHECK_INTERRUPT(); ip = rd32(vm, ip); NEXT;
L_QBRANCH:
L_QUNTIL:
L_QWHILE:  if (POP()) ip += 4; else ip = rd32(vm, ip); NEXT;
L_DOBEGIN:
L_DOTHEN:
L_NOOP:    NEXT;
L_PDO:
    t1 = INLINE();                  /* leave address */
    n2 = (cell)POP(); n1 = (cell)POP();   /* limit n1, index n2 */
do_do:
    RPUSH(t1);
    t2 = (ucell)n1 + 0x80000000u;   /* biased limit */
    RPUSH(t2);
    RPUSH((ucell)n2 - t2);
    NEXT;
L_PQDO:
    t1 = INLINE();
    n2 = (cell)POP(); n1 = (cell)POP();
    if (n1 == n2) { ip = t1; NEXT; }
    goto do_do;
L_PLOOP:
    CHECK_INTERRUPT();
    t1 = R(0);
    if (t1 == 0x7FFFFFFFu) { rp += 12; ip += 4; NEXT; }
    wr32(vm, rp, t1 + 1);
    ip = rd32(vm, ip);
    NEXT;
L_PPLOOP:
    CHECK_INTERRUPT();
    n1 = (cell)POP();
    t1 = R(0);
    t2 = t1 + (ucell)n1;
    if ((cell)((t1 ^ t2) & ((ucell)n1 ^ t2)) < 0) { rp += 12; ip += 4; NEXT; }
    wr32(vm, rp, t2);
    ip = rd32(vm, ip);
    NEXT;
L_PLEAVE:  ip = R(2); rp += 12; NEXT;
L_PQLEAVE: if (POP()) { ip = R(2); rp += 12; } NEXT;
L_UNDO:    rp += 12; NEXT;
L_I:       PUSH(R(0) + R(1)); NEXT;
L_J:       PUSH(R(3) + R(4)); NEXT;
L_K:       PUSH(R(6) + R(7)); NEXT;
L_EXECUTE: w = POP(); goto exec;
L_PERFORM: w = rd32(vm, POP()); goto exec;
L_PDOTQ:
    t1 = rd8(vm, ip);
    SYNC();
    vm_type(vm, vm_ptr(vm, ip + 1, t1), t1);
    RELOAD();
    ip = aligned(ip + 1 + t1);
    NEXT;
L_PQUOTE:
    t1 = rd8(vm, ip);
    PUSH(ip + 1); PUSH(t1);
    ip = aligned(ip + 1 + t1);
    NEXT;
L_PABORTQ:
    t1 = rd8(vm, ip);
    if (POP()) {
        SYNC();
        vm_throw(vm, E_ABORTQ, "%.*s", (int)t1, (const char *)vm_ptr(vm, ip + 1, t1));
    }
    ip = aligned(ip + 1 + t1);
    NEXT;
L_COMPILE:
    t1 = INLINE();
    t2 = sv(vm, SV_XDP);
    wr32(vm, t2, t1);
    sv_set(vm, SV_XDP, t2 + 4);
    NEXT;
L_PFETCHTO:    t1 = INLINE(); PUSH(rd32(vm, t1 + 4)); NEXT;
L_PUFETCHTO:   t1 = INLINE(); PUSH(rd32(vm, vm_up(vm) + rd32(vm, t1 + 4))); NEXT;
L_PUSTORETO:   t1 = INLINE(); wr32(vm, vm_up(vm) + rd32(vm, t1 + 4), POP()); NEXT;
L_PINCRTO:     t1 = INLINE(); wr32(vm, t1 + 4, rd32(vm, t1 + 4) + 1); NEXT;
L_PDECRTO:     t1 = INLINE(); wr32(vm, t1 + 4, rd32(vm, t1 + 4) - 1); NEXT;
L_POFFTO:      t1 = INLINE(); wr32(vm, t1 + 4, 0); NEXT;
L_PONTO:       t1 = INLINE(); wr32(vm, t1 + 4, 0xFFFFFFFFu); NEXT;
L_PADDRTO:     t1 = INLINE(); PUSH(t1 + 4); NEXT;
L_PSAVESTORETO: t1 = INLINE() + 4; RPUSH(rd32(vm, t1)); wr32(vm, t1, POP()); NEXT;
L_PSAVETO:     t1 = INLINE() + 4; RPUSH(rd32(vm, t1)); NEXT;
L_PRESTORETO:  t2 = RPOP(); t1 = INLINE() + 4; wr32(vm, t1, t2); NEXT;
L_PUSAVESTORETO: t1 = INLINE(); t1 = vm_up(vm) + rd32(vm, t1 + 4); RPUSH(rd32(vm, t1)); wr32(vm, t1, POP()); NEXT;
L_PUSAVETO:    t1 = INLINE(); t1 = vm_up(vm) + rd32(vm, t1 + 4); RPUSH(rd32(vm, t1)); NEXT;
L_PURESTORETO: t2 = RPOP(); t1 = INLINE(); t1 = vm_up(vm) + rd32(vm, t1 + 4); wr32(vm, t1, t2); NEXT;
L_PSTORETO:    t1 = INLINE(); wr32(vm, t1 + 4, POP()); NEXT;
L_PPLUSSTORETO: t1 = INLINE(); wr32(vm, t1 + 4, rd32(vm, t1 + 4) + POP()); NEXT;
L_PIS:
    t1 = INLINE();
    if (rd32(vm, t1) == T_DOUSERDEFER) wr32(vm, vm_up(vm) + rd32(vm, t1 + 4), POP());
    else wr32(vm, t1 + 4, POP());
    NEXT;
L_DOCASE:
L_DOENDCASE: NEXT;
L_DOENDOF: ip = rd32(vm, ip); NEXT;
L_POF:                          /* ( n1 n2 -- n1 ) or ( n1 n1 -- ) */
    t1 = POP();
    if (t1 != S(0)) { ip = rd32(vm, ip); NEXT; }
    sp += 4; ip += 4;
    NEXT;
L_NEXTBAR:                      /* FOR ... NEXT */
    t1 = R(0);
    wr32(vm, rp, t1 - 1);
    if (t1 != 0) { ip = rd32(vm, ip); NEXT; }
    rp += 4; ip += 4;
    NEXT;
L_GOTO:   w = INLINE(); ip = RPOP(); goto exec;
L_EXECCOLON:                    /* ( n -- ) execute the n-th xt following, then exit */
    t1 = POP();
    w = rd32(vm, ip + 4 * t1);
    ip = RPOP();
    goto exec;
L_BOUNDS: t1 = S(0); t2 = S(1); SSET(1, t1 + t2); SSET(0, t2); NEXT;
L_PDOES:
    t1 = INLINE();                                  /* handler token */
    wr32(vm, name_to_cfa(vm, sv(vm, SV_LAST)), t1);
    ip = RPOP();
    NEXT;

    /* ---- stack ---- */
L_DROP:    sp += 4; NEXT;
L_DUP:     t1 = S(0); PUSH(t1); NEXT;
L_SWAP:    t1 = S(0); SSET(0, S(1)); SSET(1, t1); NEXT;
L_OVER:    t1 = S(1); PUSH(t1); NEXT;
L_ROT:     t1 = S(2); SSET(2, S(1)); SSET(1, S(0)); SSET(0, t1); NEXT;
L_MROT:    t1 = S(0); SSET(0, S(1)); SSET(1, S(2)); SSET(2, t1); NEXT;
L_NIP:     t1 = POP(); SSET(0, t1); NEXT;
L_TUCK:    t1 = S(0); t2 = S(1); SSET(1, t1); SSET(0, t2); PUSH(t1); NEXT;
L_PICK:    t1 = S(0); SSET(0, S(t1 + 1)); NEXT;
L_ROLL:
    t1 = POP();
    t2 = S(t1);
    for (t3 = t1; t3 > 0; t3--) SSET(t3, S(t3 - 1));
    SSET(0, t2);
    NEXT;
L_QDUP:    t1 = S(0); if (t1) PUSH(t1); NEXT;
L_TWODUP:  t1 = S(1); t2 = S(0); PUSH(t1); PUSH(t2); NEXT;
L_TWODROP: sp += 8; NEXT;
L_TWOSWAP:
    t1 = S(0); t2 = S(1);
    SSET(0, S(2)); SSET(1, S(3)); SSET(2, t1); SSET(3, t2);
    NEXT;
L_TWOOVER: t1 = S(3); t2 = S(2); PUSH(t1); PUSH(t2); NEXT;
L_THREEDUP: t1 = S(2); t2 = S(1); t3 = S(0); PUSH(t1); PUSH(t2); PUSH(t3); NEXT;
L_THREEDROP: sp += 12; NEXT;
L_PLUCK:   t1 = S(2); PUSH(t1); NEXT;
L_TOR:     RPUSH(POP()); NEXT;
L_RFROM:   PUSH(RPOP()); NEXT;
L_RFETCH:  PUSH(R(0)); NEXT;
L_TWOTOR:  t2 = POP(); t1 = POP(); RPUSH(t1); RPUSH(t2); NEXT;
L_TWORFROM: t2 = RPOP(); t1 = RPOP(); PUSH(t1); PUSH(t2); NEXT;
L_TWORFETCH: PUSH(R(1)); PUSH(R(0)); NEXT;
L_DUPTOR:  RPUSH(S(0)); NEXT;
L_RFROMDROP: rp += 4; NEXT;
L_SPFETCH: t1 = sp; PUSH(t1); NEXT;
L_SPSTORE: sp = POP(); NEXT;
L_RPFETCH: PUSH(rp); NEXT;
L_RPSTORE: rp = POP(); NEXT;
L_QDROP:  t1 = POP(); if (!t1) { sp += 4; } PUSH(t1); NEXT;
L_RPICK:  SSET(0, R(S(0))); NEXT;
L_DEPTH:   t1 = (vm->sp0 - sp) / 4; PUSH(t1); NEXT;

    /* ---- arithmetic ---- */
L_PLUS:    t1 = POP(); SSET(0, S(0) + t1); NEXT;
L_MINUS:   t1 = POP(); SSET(0, S(0) - t1); NEXT;
L_STAR:    t1 = POP(); SSET(0, S(0) * t1); NEXT;
L_SLASH:   n2 = (cell)POP(); SYNC(); SSET(0, fdiv(vm, (cell)S(0), n2, &n3)); NEXT;
L_MOD:     n2 = (cell)POP(); SYNC(); fdiv(vm, (cell)S(0), n2, &n3); SSET(0, n3); NEXT;
L_SLASHMOD:
    n2 = (cell)S(0); n1 = (cell)S(1);
    SYNC(); n1 = fdiv(vm, n1, n2, &n3);
    SSET(1, n3); SSET(0, n1);
    NEXT;
L_STARSLASH:
    n3 = (cell)POP(); n2 = (cell)POP(); n1 = (cell)S(0);
    SYNC(); SSET(0, fdiv64(vm, (dcell)n1 * n2, n3, &n3));
    NEXT;
L_STARSLASHMOD:
    n3 = (cell)POP(); n2 = (cell)S(0); n1 = (cell)S(1);
    SYNC(); n1 = fdiv64(vm, (dcell)n1 * n2, n3, &n3);
    SSET(1, n3); SSET(0, n1);
    NEXT;
L_UMSTAR: {
    udcell d = (udcell)S(1) * S(0);
    SSET(1, (ucell)d); SSET(0, (ucell)(d >> 32));
    NEXT; }
L_MSTAR:
L_STARD: {
    dcell d = (dcell)(cell)S(1) * (cell)S(0);
    SSET(1, (ucell)d); SSET(0, (ucell)((udcell)d >> 32));
    NEXT; }
L_UMSLASHMOD: {             /* ( ud u -- urem uquot ); overflow -> -1 -1 */
    ucell u = POP();
    udcell d = (udcell)S(0) << 32 | S(1);
    if (u == 0 || (d >> 32) >= u) { SSET(1, 0xFFFFFFFFu); SSET(0, 0xFFFFFFFFu); NEXT; }
    SSET(1, (ucell)(d % u)); SSET(0, (ucell)(d / u));
    NEXT; }
L_MUSLASHMOD: {             /* ( ud u -- urem udquot ) */
    ucell u = S(0);
    udcell d = (udcell)S(1) << 32 | S(2);
    if (u == 0) { SYNC(); vm_throw(vm, E_DIV0, "Division by zero"); }
    udcell q = d / u;
    SSET(2, (ucell)(d % u)); SSET(1, (ucell)q); SSET(0, (ucell)(q >> 32));
    NEXT; }
L_SMREM: {                  /* ( d n -- rem quot ) symmetric */
    cell n = (cell)POP();
    dcell d = (dcell)((udcell)S(0) << 32 | S(1));
    if (n == 0) { SYNC(); vm_throw(vm, E_DIV0, "Division by zero"); }
    dcell q = d / n;
    if (q < INT32_MIN || q > INT32_MAX) { SYNC(); vm_throw(vm, E_RANGE, "Division overflow"); }
    SSET(1, (ucell)(d % n)); SSET(0, (ucell)q);
    NEXT; }
L_FMMOD: {
    n2 = (cell)POP();
    dcell d = (dcell)((udcell)S(0) << 32 | S(1));
    SYNC(); n1 = fdiv64(vm, d, n2, &n3);
    SSET(1, n3); SSET(0, n1);
    NEXT; }
L_NEGATE:  SSET(0, 0u - S(0)); NEXT;
L_ABS:     n1 = (cell)S(0); if (n1 < 0) SSET(0, 0u - (ucell)n1); NEXT;
L_MIN:     n2 = (cell)POP(); if (n2 < (cell)S(0)) SSET(0, n2); NEXT;
L_MAX:     n2 = (cell)POP(); if (n2 > (cell)S(0)) SSET(0, n2); NEXT;
L_UMIN:    t2 = POP(); if (t2 < S(0)) SSET(0, t2); NEXT;
L_UMAX:    t2 = POP(); if (t2 > S(0)) SSET(0, t2); NEXT;
L_ZMAX:    if ((cell)S(0) < 0) SSET(0, 0); NEXT;
L_ONEPLUS: SSET(0, S(0) + 1); NEXT;
L_ONEMINUS: SSET(0, S(0) - 1); NEXT;
L_TWOPLUS: SSET(0, S(0) + 2); NEXT;
L_TWOMINUS: SSET(0, S(0) - 2); NEXT;
L_TWOSTAR: SSET(0, S(0) << 1); NEXT;
L_TWOSLASH: SSET(0, (ucell)((cell)S(0) >> 1)); NEXT;
L_UTWOSLASH: SSET(0, S(0) >> 1); NEXT;
L_CELLPLUS: SSET(0, S(0) + 4); NEXT;
L_CELLMINUS: SSET(0, S(0) - 4); NEXT;
L_CELLS:   SSET(0, S(0) << 2); NEXT;
L_AND:     t1 = POP(); SSET(0, S(0) & t1); NEXT;
L_OR:      t1 = POP(); SSET(0, S(0) | t1); NEXT;
L_XOR:     t1 = POP(); SSET(0, S(0) ^ t1); NEXT;
L_NOT:
L_INVERT:  SSET(0, ~S(0)); NEXT;
L_LSHIFT:  t1 = POP(); SSET(0, t1 >= 32 ? 0 : S(0) << t1); NEXT;
L_RSHIFT:  t1 = POP(); SSET(0, t1 >= 32 ? 0 : S(0) >> t1); NEXT;
L_FLIP:    t1 = S(0); SSET(0, ((t1 & 0xFF) << 8) | ((t1 >> 8) & 0xFF)); NEXT;
L_SPLIT:   t1 = S(0); SSET(0, t1 & 0xFF); PUSH((t1 >> 8) & 0xFF); NEXT;
L_JOIN:    t2 = POP(); SSET(0, (S(0) & 0xFF) | ((t2 & 0xFF) << 8)); NEXT;

    /* ---- comparison ---- */
#define FLAG(c) ((c) ? 0xFFFFFFFFu : 0u)
L_EQ:      t1 = POP(); SSET(0, FLAG(S(0) == t1)); NEXT;
L_NE:      t1 = POP(); SSET(0, FLAG(S(0) != t1)); NEXT;
L_LT:      n2 = (cell)POP(); SSET(0, FLAG((cell)S(0) < n2)); NEXT;
L_GT:      n2 = (cell)POP(); SSET(0, FLAG((cell)S(0) > n2)); NEXT;
L_ULT:     t2 = POP(); SSET(0, FLAG(S(0) < t2)); NEXT;
L_UGT:     t2 = POP(); SSET(0, FLAG(S(0) > t2)); NEXT;
L_ZEQ:     SSET(0, FLAG(S(0) == 0)); NEXT;
L_ZNE:     SSET(0, FLAG(S(0) != 0)); NEXT;
L_ZLT:     SSET(0, FLAG((cell)S(0) < 0)); NEXT;
L_ZGT:     SSET(0, FLAG((cell)S(0) > 0)); NEXT;
L_BETWEEN: n3 = (cell)POP(); n2 = (cell)POP(); n1 = (cell)S(0);
           SSET(0, FLAG(n1 >= n2 && n1 <= n3)); NEXT;
L_UBETWEEN: t3 = POP(); t2 = POP(); t1 = S(0);
           SSET(0, FLAG(t1 >= t2 && t1 <= t3)); NEXT;
L_WITHIN:  t3 = POP(); t2 = POP(); t1 = S(0);
           SSET(0, FLAG(t1 - t2 < t3 - t2)); NEXT;

    /* ---- double cell: ( lo hi ) with hi on top ---- */
#define DGET(n)    ((udcell)S(n) << 32 | S((n) + 1))
#define DSET(n, d) (SSET((n) + 1, (ucell)(d)), SSET(n, (ucell)((udcell)(d) >> 32)))
L_DPLUS:   { udcell b = DGET(0); sp += 8; udcell a = DGET(0); DSET(0, a + b); NEXT; }
L_DMINUS:  { udcell b = DGET(0); sp += 8; udcell a = DGET(0); DSET(0, a - b); NEXT; }
L_DNEGATE: { udcell a = DGET(0); DSET(0, 0 - a); NEXT; }
L_DABS:    { dcell a = (dcell)DGET(0); if (a < 0) DSET(0, (udcell)0 - (udcell)a); NEXT; }
L_STOD:    n1 = (cell)S(0); PUSH(n1 < 0 ? 0xFFFFFFFFu : 0); NEXT;
L_DTWOSTAR: { udcell a = DGET(0); DSET(0, a << 1); NEXT; }
L_DTWOSLASH: { dcell a = (dcell)DGET(0); DSET(0, (udcell)(a >> 1)); NEXT; }
L_DLT:     { dcell b = (dcell)DGET(0); sp += 8; dcell a = (dcell)DGET(0); sp += 4; SSET(0, FLAG(a < b)); NEXT; }
L_DULT:    { udcell b = DGET(0); sp += 8; udcell a = DGET(0); sp += 4; SSET(0, FLAG(a < b)); NEXT; }
L_DEQ:     { udcell b = DGET(0); sp += 8; udcell a = DGET(0); sp += 4; SSET(0, FLAG(a == b)); NEXT; }
L_DZEQ:    { udcell a = DGET(0); sp += 4; SSET(0, FLAG(a == 0)); NEXT; }
L_DMIN:    { dcell b = (dcell)DGET(0); sp += 8; dcell a = (dcell)DGET(0); if (b < a) DSET(0, b); NEXT; }
L_DMAX:    { dcell b = (dcell)DGET(0); sp += 8; dcell a = (dcell)DGET(0); if (b > a) DSET(0, b); NEXT; }

    /* ---- memory ---- */
L_FETCH:   SSET(0, rd32(vm, S(0))); NEXT;
L_STORE:   t1 = POP(); t2 = POP(); wr32(vm, t1, t2); NEXT;
L_CFETCH:  SSET(0, rd8(vm, S(0))); NEXT;
L_CSTORE:  t1 = POP(); t2 = POP(); wr8(vm, t1, t2); NEXT;
L_WFETCH:  SSET(0, rd16(vm, S(0))); NEXT;
L_WSTORE:  t1 = POP(); t2 = POP(); wr16(vm, t1, t2); NEXT;
L_PLUSSTORE: t1 = POP(); t2 = POP(); wr32(vm, t1, rd32(vm, t1) + t2); NEXT;
L_CPLUSSTORE: t1 = POP(); t2 = POP(); wr8(vm, t1, rd8(vm, t1) + t2); NEXT;
L_TWOFETCH: t1 = S(0); SSET(0, rd32(vm, t1 + 4)); PUSH(rd32(vm, t1)); NEXT;
L_TWOSTORE: t1 = POP(); t2 = POP(); t3 = POP(); wr32(vm, t1, t2); wr32(vm, t1 + 4, t3); NEXT;
L_ON:      wr32(vm, POP(), 0xFFFFFFFFu); NEXT;
L_OFF:     wr32(vm, POP(), 0); NEXT;
L_INCR:    t1 = POP(); wr32(vm, t1, rd32(vm, t1) + 1); NEXT;
L_DECR:    t1 = POP(); wr32(vm, t1, rd32(vm, t1) - 1); NEXT;
L_ZDECR:  t1 = POP(); n1 = (cell)rd32(vm, t1) - 1; wr32(vm, t1, n1 < 0 ? 0 : (ucell)n1); NEXT;
L_CSET:   t1 = POP(); t2 = POP(); wr8(vm, t1, rd8(vm, t1) | t2); NEXT;
L_CRESET: t1 = POP(); t2 = POP(); wr8(vm, t1, rd8(vm, t1) & ~t2); NEXT;
L_CTOGGLE: t1 = POP(); t2 = POP(); wr8(vm, t1, rd8(vm, t1) ^ t2); NEXT;
L_DPLUSSTORE: {                 /* ( d addr -- ); hi cell at addr */
    t1 = POP();
    udcell d = (udcell)POP() << 32; d |= POP();
    udcell m = (udcell)rd32(vm, t1) << 32 | rd32(vm, t1 + 4);
    m += d;
    wr32(vm, t1, (ucell)(m >> 32)); wr32(vm, t1 + 4, (ucell)m);
    NEXT; }
L_PCFETCH:
L_PFETCH: SSET(0, 0xFF); NEXT;  /* no I/O ports: read as floating bus */
L_PCSTORE:
L_PSTORE: sp += 8; NEXT;
L_CMOVE: {
    t3 = POP(); t2 = POP(); t1 = POP();
    if (t3) {
        uint8_t *d = vm_ptr(vm, t2, t3), *s = vm_ptr(vm, t1, t3);
        for (ucell i = 0; i < t3; i++) d[i] = s[i];
    }
    NEXT; }
L_CMOVEUP: {
    t3 = POP(); t2 = POP(); t1 = POP();
    if (t3) {
        uint8_t *d = vm_ptr(vm, t2, t3), *s = vm_ptr(vm, t1, t3);
        for (ucell i = t3; i > 0; i--) d[i - 1] = s[i - 1];
    }
    NEXT; }
L_MOVE:
    t3 = POP(); t2 = POP(); t1 = POP();
    if (t3) memmove(vm_ptr(vm, t2, t3), vm_ptr(vm, t1, t3), t3);
    NEXT;
L_FILL:
    t3 = POP(); t2 = POP(); t1 = POP();
    if (t2) memset(vm_ptr(vm, t1, t2), (int)(t3 & 0xFF), t2);
    NEXT;
L_ERASE:   t2 = POP(); t1 = POP(); if (t2) memset(vm_ptr(vm, t1, t2), 0, t2); NEXT;
L_BLANK:   t2 = POP(); t1 = POP(); if (t2) memset(vm_ptr(vm, t1, t2), ' ', t2); NEXT;
L_COUNT:   t1 = S(0); SSET(0, t1 + 1); PUSH(rd8(vm, t1)); NEXT;
L_DTRAILING:
    t2 = S(0); t1 = S(1);
    while (t2 > 0 && rd8(vm, t1 + t2 - 1) == ' ') t2--;
    SSET(0, t2);
    NEXT;
L_COMP: {                   /* ( a1 a2 n -- -1|0|1 ) */
    t3 = POP(); t2 = POP(); t1 = S(0);
    int r = t3 ? memcmp(vm_ptr(vm, t1, t3), vm_ptr(vm, t2, t3), t3) : 0;
    SSET(0, r < 0 ? 0xFFFFFFFFu : r > 0 ? 1u : 0u);
    NEXT; }
L_CAPSCOMP: {               /* as COMP, ignoring case (OR $20, as F-PC did) */
    t3 = POP(); t2 = POP(); t1 = S(0);
    int r = 0;
    if (t3) {
        const uint8_t *a = vm_ptr(vm, t1, t3), *b = vm_ptr(vm, t2, t3);
        for (ucell i = 0; i < t3 && !r; i++) {
            int x = a[i] | 0x20, y = b[i] | 0x20;
            r = x < y ? -1 : x > y;
        }
    }
    SSET(0, r < 0 ? 0xFFFFFFFFu : r > 0 ? 1u : 0u);
    NEXT; }
L_SKIP:                      /* ( a n c -- a' n' ) */
    t3 = POP() & 0xFF; t2 = S(0); t1 = S(1);
    while (t2 > 0 && rd8(vm, t1) == t3) { t1++; t2--; }
    SSET(1, t1); SSET(0, t2);
    NEXT;
L_SCAN:
    t3 = POP() & 0xFF; t2 = S(0); t1 = S(1);
    while (t2 > 0 && rd8(vm, t1) != t3) { t1++; t2--; }
    SSET(1, t1); SSET(0, t2);
    NEXT;
L_UPC:     t1 = S(0); if (t1 >= 'a' && t1 <= 'z') SSET(0, t1 - 32); NEXT;
L_UPPER: {
    t2 = POP(); t1 = POP();
    uint8_t *p = vm_ptr(vm, t1, t2);
    for (ucell i = 0; i < t2; i++) if (p[i] >= 'a' && p[i] <= 'z') p[i] -= 32;
    NEXT; }
L_SLASHSTRING:              /* n is clamped to len when 0 <= n, as in F-PC */
    t3 = POP();
    if ((cell)t3 >= 0 && t3 > S(0)) t3 = S(0);
    SSET(0, S(0) - t3); SSET(1, S(1) + t3);
    NEXT;
L_PLACE:        /* ( a n dest -- ) */
    t3 = POP(); t2 = POP(); t1 = POP();
    if (t2 > 255) t2 = 255;
    if (t2) memmove(vm_ptr(vm, t3 + 1, t2), vm_ptr(vm, t1, t2), t2);
    wr8(vm, t3, t2);
    NEXT;
L_PLUSPLACE:
    t3 = POP(); t2 = POP(); t1 = POP();
    {
        ucell old = rd8(vm, t3);
        if (old + t2 > 255) t2 = 255 - old;
        if (t2) memmove(vm_ptr(vm, t3 + 1 + old, t2), vm_ptr(vm, t1, t2), t2);
        wr8(vm, t3, old + t2);
    }
    NEXT;
L_ALIGNED: SSET(0, aligned(S(0))); NEXT;
}
