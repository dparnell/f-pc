/* jit.c -- compile abstract-machine (AM) code to host code with sljit.
 *
 * A CODE word's op stream (am.c) is compiled the first time the word runs.
 * The AM registers live in a frame in memory; every memory access is masked
 * into VM memory (whose size is a power of two), so JIT code cannot reach
 * host memory even when a CODE word is wrong. Division, the long multiply,
 * CMOVE and shifts call small C helpers with the interpreter's exact
 * semantics. Backward branches poll for Control-C and screen refresh.
 * Where code generation is not possible (W^X policy, unsupported CPU, big
 * endian host) or FPC_NOJIT is set, the interpreter runs the ops instead.
 */
#define SLJIT_CONFIG_AUTO 1
#include "sljit/sljitLir.h"

#include "vm.h"

#include <stdlib.h>
#include <string.h>

enum {
#define AMOP(name, imm) AM_##name,
#include "amops.def"
#undef AMOP
    AM_NOPS
};
static const unsigned char has_imm[AM_NOPS] = {
#define AMOP(name, imm) imm,
#include "amops.def"
#undef AMOP
};

typedef struct {
    uint32_t r[16];             /* AM registers; 8 SP 9 RP 10 IP 11 W 12 UP */
    vm_t *vm;
    uint8_t *mem;
} jframe;

typedef void (*jitfn)(jframe *f);

#define R_SP 8
#define R_RP 9
#define R_IP 10
#define R_W  11
#define R_UP 12
#define NREG 13
#define MAXOPS 65536

/* ---- helpers called from generated code ------------------------------------ */
static void sync_out(jframe *f)
{
    f->vm->sp = f->r[R_SP]; f->vm->rp = f->r[R_RP]; f->vm->ip = f->r[R_IP];
}

static void SLJIT_FUNC h_op(jframe *f, sljit_uw insn)      /* the ops done in C */
{
    unsigned op = insn & 0xFF, a = (insn >> 8) & 0xFF, b = (insn >> 16) & 0xFF, c = (unsigned)(insn >> 24) & 0xFF;
    uint32_t *r = f->r;
    vm_t *vm = f->vm;
    switch (op) {
    case AM_SHL: if (a != R_UP) r[a] = r[c] >= 32 ? 0 : r[b] << r[c]; break;
    case AM_SHR: if (a != R_UP) r[a] = r[c] >= 32 ? 0 : r[b] >> r[c]; break;
    case AM_SAR: if (a != R_UP) r[a] = (uint32_t)((int32_t)r[b] >> (r[c] >= 32 ? 31 : r[c])); break;
    case AM_UMULH: if (a != R_UP) r[a] = (uint32_t)(((uint64_t)r[b] * r[c]) >> 32); break;
    case AM_DIVMOD: {
        int32_t n = (int32_t)r[b], d = (int32_t)r[c];
        if (d == 0 || (n == INT32_MIN && d == -1)) { sync_out(f); vm_throw(vm, E_DIV0, "Division by zero or overflow"); }
        int32_t q = n / d, m = n % d;
        if (m != 0 && ((m < 0) != (d < 0))) { q--; m += d; }
        if (b != R_UP) r[b] = (uint32_t)m;
        if (a != R_UP) r[a] = (uint32_t)q;
        break; }
    case AM_CMOVE:
        if (r[c]) {
            sync_out(f);
            uint8_t *d = vm_ptr(vm, r[b], r[c]), *s = vm_ptr(vm, r[a], r[c]);
            for (uint32_t i = 0; i < r[c]; i++) d[i] = s[i];
        }
        break;
    }
}

static void SLJIT_FUNC h_poll(jframe *f)
{
    vm_t *vm = f->vm;
    if (vm->interrupt) { vm->interrupt = 0; sync_out(f); vm_throw(vm, E_INTERRUPT, "Interrupted"); }
    if ((vm->attention & 2) && vm->host->refresh) { vm->attention &= ~2; vm->host->refresh(vm->host, vm, 0); }
}

/* ---- compilation ---------------------------------------------------------- */
#define REG(k) SLJIT_MEM1(SLJIT_S0), (sljit_sw)offsetof(jframe, r[k])

static int cmp_u32(const void *x, const void *y)
{
    uint32_t a = *(const uint32_t *)x, b = *(const uint32_t *)y;
    return a < b ? -1 : a > b;
}

/* find the extent of the op stream and its branch targets */
static int scan(vm_t *vm, ucell start, ucell *end, uint32_t **targets, int *ntargets)
{
    uint32_t *t = NULL; int nt = 0, cap = 0;
    ucell pc = start, maxt = start;
    for (int n = 0; n < MAXOPS; n++) {
        if (pc + 4 > vm->memsize) break;
        ucell insn = rd32(vm, pc);
        unsigned op = insn & 0xFF;
        if (op >= AM_NOPS || ((insn >> 8) & 0xFF) >= NREG || ((insn >> 16) & 0xFF) >= NREG || (insn >> 24) >= NREG) break;
        ucell imm = has_imm[op] ? rd32(vm, pc + 4) : 0;
        ucell next = pc + (has_imm[op] ? 8 : 4);
        if (op >= AM_BR && op <= AM_BGEU) {
            if (nt == cap) { cap = cap ? cap * 2 : 16; t = realloc(t, (size_t)cap * sizeof *t); }
            t[nt++] = imm;
            if (imm > maxt) maxt = imm;
        }
        if ((op == AM_NEXT || op == AM_BR) && next > maxt) {
            *end = next;
            qsort(t, (size_t)nt, sizeof *t, cmp_u32);
            *targets = t; *ntargets = nt;
            for (int i = 0; i < nt; i++) if (t[i] < start || t[i] >= next) { free(t); return -1; }
            return 0;
        }
        pc = next;
    }
    free(t);
    return -1;
}

static int is_target(uint32_t *t, int nt, ucell a)
{
    return bsearch(&a, t, (size_t)nt, sizeof *t, cmp_u32) != NULL;
}

static void *compile(vm_t *vm, ucell start)
{
    ucell end; uint32_t *tg; int nt;
    if (scan(vm, start, &end, &tg, &nt) != 0) return NULL;
    struct sljit_compiler *C = sljit_create_compiler(NULL);
    if (!C) { free(tg); return NULL; }

    struct sljit_label **labels = calloc((size_t)nt + 1, sizeof *labels);
    struct pend { struct sljit_jump *j; ucell target; } *pend = calloc((size_t)nt + 1, sizeof *pend);
    int np = 0;
    const sljit_s32 MASK = (sljit_s32)(vm->memsize - 1);

    sljit_emit_enter(C, 0, SLJIT_ARGS1V(P), 4, 2, 0);
    sljit_emit_op1(C, SLJIT_MOV_P, SLJIT_S1, 0, SLJIT_MEM1(SLJIT_S0), (sljit_sw)offsetof(jframe, mem));

    /* R2 = (r[b] + imm) & MASK, zero extended: an address in VM memory */
#define ADDR(b, imm) do { \
        sljit_emit_op2(C, SLJIT_ADD32, SLJIT_R2, 0, REG(b), SLJIT_IMM, (sljit_sw)(sljit_s32)(imm)); \
        sljit_emit_op2(C, SLJIT_AND32, SLJIT_R2, 0, SLJIT_R2, 0, SLJIT_IMM, MASK); \
        sljit_emit_op1(C, SLJIT_MOV_U32, SLJIT_R2, 0, SLJIT_R2, 0); } while (0)
#define STORE_A(src) do { if (a != R_UP) sljit_emit_op1(C, SLJIT_MOV32, REG(a), src, 0); } while (0)

    for (ucell pc = start; pc < end; ) {
        if (is_target(tg, nt, pc)) {
            struct sljit_label *l = sljit_emit_label(C);
            for (int i = 0; i < nt; i++) if (tg[i] == pc) labels[i] = l;
        }
        ucell insn = rd32(vm, pc);
        unsigned op = insn & 0xFF, a = (insn >> 8) & 0xFF, b = (insn >> 16) & 0xFF, c = insn >> 24;
        ucell imm = has_imm[op] ? rd32(vm, pc + 4) : 0;
        ucell here = pc;
        pc += has_imm[op] ? 8 : 4;
        sljit_s32 op2 = -1;
        switch (op) {
        case AM_NEXT: sljit_emit_return_void(C); break;
        case AM_LI:   if (a != R_UP) sljit_emit_op1(C, SLJIT_MOV32, REG(a), SLJIT_IMM, (sljit_sw)(sljit_s32)imm); break;
        case AM_MOV:  sljit_emit_op1(C, SLJIT_MOV32, SLJIT_R0, 0, REG(b)); STORE_A(SLJIT_R0); break;
        case AM_ADD:  op2 = SLJIT_ADD32; break;
        case AM_SUB:  op2 = SLJIT_SUB32; break;
        case AM_AND:  op2 = SLJIT_AND32; break;
        case AM_OR:   op2 = SLJIT_OR32; break;
        case AM_XOR:  op2 = SLJIT_XOR32; break;
        case AM_MUL:  op2 = SLJIT_MUL32; break;
        case AM_ADDI:
            sljit_emit_op2(C, SLJIT_ADD32, SLJIT_R0, 0, REG(b), SLJIT_IMM, (sljit_sw)(sljit_s32)imm);
            STORE_A(SLJIT_R0); break;
        case AM_NOT:
            sljit_emit_op2(C, SLJIT_XOR32, SLJIT_R0, 0, REG(b), SLJIT_IMM, -1);
            STORE_A(SLJIT_R0); break;
        case AM_NEG:
            sljit_emit_op2(C, SLJIT_SUB32, SLJIT_R0, 0, SLJIT_IMM, 0, REG(b));
            STORE_A(SLJIT_R0); break;
        case AM_LD:  ADDR(b, imm); sljit_emit_op1(C, SLJIT_MOV32, SLJIT_R0, 0, SLJIT_MEM2(SLJIT_S1, SLJIT_R2), 0); STORE_A(SLJIT_R0); break;
        case AM_LDB: ADDR(b, imm); sljit_emit_op1(C, SLJIT_MOV32_U8, SLJIT_R0, 0, SLJIT_MEM2(SLJIT_S1, SLJIT_R2), 0); STORE_A(SLJIT_R0); break;
        case AM_LDW: ADDR(b, imm); sljit_emit_op1(C, SLJIT_MOV32_U16, SLJIT_R0, 0, SLJIT_MEM2(SLJIT_S1, SLJIT_R2), 0); STORE_A(SLJIT_R0); break;
        case AM_ST:  ADDR(b, imm); sljit_emit_op1(C, SLJIT_MOV32, SLJIT_R0, 0, REG(a));
                     sljit_emit_op1(C, SLJIT_MOV32, SLJIT_MEM2(SLJIT_S1, SLJIT_R2), 0, SLJIT_R0, 0); break;
        case AM_STB: ADDR(b, imm); sljit_emit_op1(C, SLJIT_MOV32, SLJIT_R0, 0, REG(a));
                     sljit_emit_op1(C, SLJIT_MOV32_U8, SLJIT_MEM2(SLJIT_S1, SLJIT_R2), 0, SLJIT_R0, 0); break;
        case AM_STW: ADDR(b, imm); sljit_emit_op1(C, SLJIT_MOV32, SLJIT_R0, 0, REG(a));
                     sljit_emit_op1(C, SLJIT_MOV32_U16, SLJIT_MEM2(SLJIT_S1, SLJIT_R2), 0, SLJIT_R0, 0); break;
        case AM_PUSH: case AM_RPUSH: {
            int sp = op == AM_PUSH ? R_SP : R_RP;
            sljit_emit_op1(C, SLJIT_MOV32, SLJIT_R0, 0, REG(a));
            sljit_emit_op2(C, SLJIT_SUB32, REG(sp), REG(sp), SLJIT_IMM, 4);
            ADDR(sp, 0);
            sljit_emit_op1(C, SLJIT_MOV32, SLJIT_MEM2(SLJIT_S1, SLJIT_R2), 0, SLJIT_R0, 0);
            break; }
        case AM_POP: case AM_RPOP: {
            int sp = op == AM_POP ? R_SP : R_RP;
            ADDR(sp, 0);
            sljit_emit_op1(C, SLJIT_MOV32, SLJIT_R0, 0, SLJIT_MEM2(SLJIT_S1, SLJIT_R2), 0);
            sljit_emit_op2(C, SLJIT_ADD32, REG(sp), REG(sp), SLJIT_IMM, 4);
            STORE_A(SLJIT_R0);
            break; }
        case AM_BR: case AM_BZ: case AM_BNZ: case AM_BEQ: case AM_BNE:
        case AM_BLT: case AM_BGE: case AM_BLTU: case AM_BGEU: {
            if (imm <= here) {                          /* backward: poll */
                sljit_emit_op1(C, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S0), (sljit_sw)offsetof(jframe, vm));
                sljit_emit_op1(C, SLJIT_MOV32, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_R0), (sljit_sw)offsetof(vm_t, interrupt));
                sljit_emit_op2(C, SLJIT_OR32, SLJIT_R1, 0, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_R0), (sljit_sw)offsetof(vm_t, attention));
                struct sljit_jump *quiet = sljit_emit_cmp(C, SLJIT_EQUAL | SLJIT_32, SLJIT_R1, 0, SLJIT_IMM, 0);
                sljit_emit_op1(C, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_S0, 0);
                sljit_emit_icall(C, SLJIT_CALL, SLJIT_ARGS1V(P), SLJIT_IMM, SLJIT_FUNC_ADDR(h_poll));
                sljit_set_label(quiet, sljit_emit_label(C));
            }
            struct sljit_jump *j;
            if (op == AM_BR) j = sljit_emit_jump(C, SLJIT_JUMP);
            else if (op == AM_BZ || op == AM_BNZ)
                j = sljit_emit_cmp(C, (op == AM_BZ ? SLJIT_EQUAL : SLJIT_NOT_EQUAL) | SLJIT_32, REG(a), SLJIT_IMM, 0);
            else {
                static const sljit_s32 cond[] = { SLJIT_EQUAL, SLJIT_NOT_EQUAL, SLJIT_SIG_LESS,
                                                  SLJIT_SIG_GREATER_EQUAL, SLJIT_LESS, SLJIT_GREATER_EQUAL };
                sljit_emit_op1(C, SLJIT_MOV32, SLJIT_R0, 0, REG(a));
                sljit_emit_op1(C, SLJIT_MOV32, SLJIT_R1, 0, REG(b));
                j = sljit_emit_cmp(C, cond[op - AM_BEQ] | SLJIT_32, SLJIT_R0, 0, SLJIT_R1, 0);
            }
            pend[np].j = j; pend[np].target = imm; np++;
            break; }
        default:                                        /* done in C */
            sljit_emit_op1(C, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_S0, 0);
            sljit_emit_op1(C, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, (sljit_sw)insn);
            sljit_emit_icall(C, SLJIT_CALL, SLJIT_ARGS2V(P, W), SLJIT_IMM, SLJIT_FUNC_ADDR(h_op));
            break;
        }
        if (op2 >= 0) {
            sljit_emit_op2(C, op2, SLJIT_R0, 0, REG(b), REG(c));
            STORE_A(SLJIT_R0);
        }
    }
    for (int i = 0; i < np; i++)
        for (int k = 0; k < nt; k++)
            if (tg[k] == pend[i].target && labels[k]) { sljit_set_label(pend[i].j, labels[k]); break; }

    void *code = sljit_get_compiler_error(C) ? NULL : sljit_generate_code(C, 0, NULL);
    sljit_free_compiler(C);
    free(labels); free(pend); free(tg);
    return code;
}

/* ---- running ------------------------------------------------------------------ */
static int jit_enabled(void)
{
    static int on = -1;
    if (on < 0) {
        uint32_t one = 1;
        on = !getenv("FPC_NOJIT") && *(uint8_t *)&one == 1;
    }
    return on;
}

static void run(vm_t *vm, jitfn fn)
{
    jframe f;
    memset(&f, 0, sizeof f);
    f.r[R_SP] = vm->sp; f.r[R_RP] = vm->rp; f.r[R_IP] = vm->ip; f.r[R_W] = vm->w; f.r[R_UP] = vm_up(vm);
    f.vm = vm; f.mem = vm->mem;
    fn(&f);
    vm->sp = f.r[R_SP]; vm->rp = f.r[R_RP]; vm->ip = f.r[R_IP];
}

static void jit_run(vm_t *vm) { run(vm, (jitfn)vm->cur_handler->jit); }

/* try to compile the handler's op stream; returns 1 if it now runs native */
int jit_try(vm_t *vm, vm_handler *h, ucell start)
{
    if (h->jit_state) return h->jit != NULL;
    h->jit_state = 1;
    if (!jit_enabled() || (vm->memsize & (vm->memsize - 1))) return 0;
    h->jit = compile(vm, start);
    if (!h->jit) return 0;
    h->fn = jit_run;
    return 1;
}

void p_JITQ(vm_t *vm)           /* ( -- f ) is the JIT in use? */
{
    push(vm, jit_enabled() ? TRUE_F : 0);
}
