/* am.c -- the abstract machine (AM) for native CODE words.
 *
 * A CODE word's body is an op stream for a small register machine
 * (kernel-design.md 5). Each op is one cell: op | a<<8 | b<<16 | c<<24,
 * followed by one immediate cell for the ops marked IMM. Registers:
 *   0-7  R0-R7    general purpose
 *   8    SP       data stack pointer (cell address of the top item)
 *   9    RP       return stack pointer
 *   10   IP       Forth instruction pointer
 *   11   W        code field address of the word being executed
 *   12   UP       user area pointer (read only)
 * The op stream is plain VM data, so it is saved in images; it is run by
 * this interpreter, or compiled to host code by a JIT where allowed.
 */
#include "vm.h"

#include <string.h>

enum {
#define AMOP(name, imm) AM_##name,
#include "amops.def"
#undef AMOP
    AM_NOPS
};

static const unsigned char am_has_imm[AM_NOPS] = {
#define AMOP(name, imm) imm,
#include "amops.def"
#undef AMOP
};

#define NREG 13
enum { R_SP = 8, R_RP = 9, R_IP = 10, R_W = 11, R_UP = 12 };

/* run the op stream at pc for the word whose code field is vm->w */
static void am_run(vm_t *vm, ucell pc)
{
    ucell r[NREG] = { 0 };
    r[R_SP] = vm->sp; r[R_RP] = vm->rp; r[R_IP] = vm->ip; r[R_W] = vm->w; r[R_UP] = vm_up(vm);
    for (long steps = 0;; steps++) {
        if ((steps & 0xFFFFF) == 0xFFFFF && (vm->interrupt | vm->attention)) {
            vm->sp = r[R_SP]; vm->rp = r[R_RP]; vm->ip = r[R_IP];
            if (vm->interrupt) { vm->interrupt = 0; vm_throw(vm, E_INTERRUPT, "Interrupted"); }
            if ((vm->attention & 2) && vm->host->refresh) { vm->attention &= ~2; vm->host->refresh(vm->host, vm, 0); }
        }
        ucell insn = rd32(vm, pc);
        unsigned op = insn & 0xFF, a = (insn >> 8) & 0xFF, b = (insn >> 16) & 0xFF, c = insn >> 24;
        if (op >= AM_NOPS || a >= NREG || b >= NREG || c >= NREG) {
            vm->sp = r[R_SP]; vm->rp = r[R_RP]; vm->ip = r[R_IP];
            vm_throw(vm, E_BADADDR, "Bad AM instruction at $%X", pc);
        }
        ucell imm = am_has_imm[op] ? rd32(vm, pc + 4) : 0;
        pc += am_has_imm[op] ? 8 : 4;
#define RA r[a]
#define RB r[b]
#define RC r[c]
#define SET(x) do { if (a != R_UP) r[a] = (x); } while (0)
        switch (op) {
        case AM_NEXT:
            vm->sp = r[R_SP]; vm->rp = r[R_RP]; vm->ip = r[R_IP];
            return;
        case AM_LI:    SET(imm); break;
        case AM_MOV:   SET(RB); break;
        case AM_ADD:   SET(RB + RC); break;
        case AM_SUB:   SET(RB - RC); break;
        case AM_AND:   SET(RB & RC); break;
        case AM_OR:    SET(RB | RC); break;
        case AM_XOR:   SET(RB ^ RC); break;
        case AM_MUL:   SET(RB * RC); break;
        case AM_SHL:   SET(RC >= 32 ? 0 : RB << RC); break;
        case AM_SHR:   SET(RC >= 32 ? 0 : RB >> RC); break;
        case AM_SAR:   SET((ucell)((cell)RB >> (RC >= 32 ? 31 : RC))); break;
        case AM_ADDI:  SET(RB + imm); break;
        case AM_NOT:   SET(~RB); break;
        case AM_NEG:   SET(0u - RB); break;
        case AM_LD:    SET(rd32(vm, RB + imm)); break;
        case AM_LDB:   SET(rd8(vm, RB + imm)); break;
        case AM_LDW:   SET(rd16(vm, RB + imm)); break;
        case AM_ST:    wr32(vm, RB + imm, RA); break;
        case AM_STB:   wr8(vm, RB + imm, RA); break;
        case AM_STW:   wr16(vm, RB + imm, RA); break;
        case AM_PUSH:  r[R_SP] -= 4; wr32(vm, r[R_SP], RA); break;
        case AM_POP:   { ucell v = rd32(vm, r[R_SP]); r[R_SP] += 4; SET(v); break; }
        case AM_RPUSH: r[R_RP] -= 4; wr32(vm, r[R_RP], RA); break;
        case AM_RPOP:  { ucell v = rd32(vm, r[R_RP]); r[R_RP] += 4; SET(v); break; }
        case AM_BR:    pc = imm; break;
        case AM_BZ:    if (RA == 0) pc = imm; break;
        case AM_BNZ:   if (RA != 0) pc = imm; break;
        case AM_BEQ:   if (RA == RB) pc = imm; break;
        case AM_BNE:   if (RA != RB) pc = imm; break;
        case AM_BLT:   if ((cell)RA < (cell)RB) pc = imm; break;
        case AM_BGE:   if ((cell)RA >= (cell)RB) pc = imm; break;
        case AM_BLTU:  if (RA < RB) pc = imm; break;
        case AM_BGEU:  if (RA >= RB) pc = imm; break;
        case AM_DIVMOD: {               /* a = b / c, b = b mod c (floored) */
            cell n = (cell)RB, d = (cell)RC;
            if (d == 0 || (n == INT32_MIN && d == -1)) {
                vm->sp = r[R_SP]; vm->rp = r[R_RP]; vm->ip = r[R_IP];
                vm_throw(vm, E_DIV0, "Division by zero or overflow");
            }
            cell q = n / d, m = n % d;
            if (m != 0 && ((m < 0) != (d < 0))) { q--; m += d; }
            if (b != R_UP) r[b] = (ucell)m;
            SET((ucell)q);
            break; }
        case AM_UMULH: SET((ucell)(((udcell)RB * RC) >> 32)); break;
        case AM_CMOVE: {                /* move RC bytes from RA to RB, ascending */
            if (RC) {
                uint8_t *d = vm_ptr(vm, RB, RC), *s = vm_ptr(vm, RA, RC);
                for (ucell i = 0; i < RC; i++) d[i] = s[i];
            }
            break; }
        default:
            vm->sp = r[R_SP]; vm->rp = r[R_RP]; vm->ip = r[R_IP];
            vm_throw(vm, E_BADADDR, "Bad AM instruction at $%X", pc);
        }
    }
}

/* the handler for every CODE word: its ops follow the code field. The
 * first run tries the JIT; if that works, the handler's fn is replaced. */
static void am_code(vm_t *vm)
{
    vm_handler *h = vm->cur_handler;
    if (h && !h->jit_state && jit_try(vm, h, vm->w + 4)) { h->fn(vm); return; }
    am_run(vm, vm->w + 4);
}

/* the handler for ;CODE runtimes: ops are at the handler's data */
static void am_does(vm_t *vm)
{
    ucell ct = rd32(vm, vm->w);
    vm_handler *h = &vm->handlers[ct];
    if (!h->jit_state && jit_try(vm, h, h->data)) { h->fn(vm); return; }
    am_run(vm, h->data);
}

void am_bind(vm_t *vm, vm_handler *h)      /* after an image load */
{
    h->fn = h->data ? am_does : am_code;
}

/* NEW-AMCODE ( ops-addr | 0 -- ct ): a code token for a CODE word (0: the
 * ops follow the code field) or a ;CODE runtime (ops at ops-addr) */
void p_NEWAMCODE(vm_t *vm)
{
    ucell data = pop(vm);
    push(vm, vm_add_handler(vm, HK_AMCODE, data ? am_does : am_code, data, NULL));
}
