/* vm.h -- F-PC native VM: state, memory access, handler table.
 *
 * See docs/kernel-design.md. Cells are 32 bits; VM memory is one flat byte
 * array addressed by 32-bit offsets, little-endian on every host.
 */
#ifndef FPC_VM_H
#define FPC_VM_H

#include <stdint.h>
#include <stddef.h>
#include <setjmp.h>
#include <signal.h>

typedef int32_t  cell;
typedef uint32_t ucell;
typedef int64_t  dcell;
typedef uint64_t udcell;

#define CELL 4
#define TRUE_F  ((cell)-1)

/* ---- memory map (defaults; see kernel-design.md 2.2) ------------------ */
#define MEM_RESERVED   0x00000100u   /* 0..FF never allocated              */
#define TIB_SIZE       256
#define DSTACK_CELLS   8192
#define RSTACK_CELLS   8192
#define NTRAMP         64            /* nested C->Forth calls             */
#define USER_CELLS     64

typedef struct vm vm_t;
typedef void (*vm_cfn)(vm_t *vm);

/* ---- handler table: a code field holds an index into it --------------- */
enum { HK_PRIM = 0,   /* inline primitive in the dispatch loop (label)   */
       HK_CPRIM,      /* C function, registers synced around the call     */
       HK_DOES,       /* DOES> clause: push PFA, nest into data (list)    */
       HK_AMCODE      /* abstract-machine code (M6)                       */
};

typedef struct {
    uint8_t     kind;
    uint8_t     jit_state;  /* HK_AMCODE: 0 = not tried, 1 = tried         */
    vm_cfn      fn;       /* HK_CPRIM / HK_AMCODE                          */
    ucell       data;     /* HK_DOES: list address; HK_AMCODE: op stream   */
    const char *name;
    void       *jit;      /* HK_AMCODE: compiled code, or NULL             */
} vm_handler;

/* ---- primitive token numbers ------------------------------------------ */
enum {
#define PRIM(id, name, flags)  T_##id,
#define CPRIM(id, name, flags) T_##id,
#include "prims.def"
#undef PRIM
#undef CPRIM
    T_NBUILTIN
};

/* header flags in the prims table */
#define F_IMM     1    /* immediate                                       */
#define F_NOHEAD  2    /* code-field handler only; no dictionary entry    */

/* ---- system variables: fixed slots in VM memory ------------------------ */
/* Plain variables (one cell each); the seed gives them headers. */
#define SYSVARS(X) \
    X(STATE,"STATE") X(TOIN,">IN") X(NTIB,"#TIB") X(SPAN,"SPAN")       \
    X(DPL,"DPL") X(CSP,"CSP") X(LAST,"LAST") X(CURRENT,"CURRENT")      \
    X(VOCLINK,"VOC-LINK") X(WARNING,"WARNING") X(CAPS,"CAPS")          \
    X(LOADLINE,"LOADLINE") X(OUT,"#OUT") X(LINE,"#LINE") X(XDP,"XDP")  \
    X(YDP,"YDP") X(WIDTH,"WIDTH") X(LOADING,"LOADING")                 \
    X(DEFBASE,"DEFBASE") X(FENCE,"FENCE") X(PRIOR,"PRIOR") X(RNUM,"R#")   \
    X(TICKTIB,"'TIB") X(ENDQ,"END?") X(TOINWORD,">IN_WORD")              \
    X(ATTRIB,"ATTRIB") X(BIOSCHAR,"BIOSCHAR") X(BIOSKEYVAL,"BIOSKEYVAL")   \
    X(COLS,"COLS") X(ROWS,"ROWS") X(CROWS,"CROWS") X(UP,"UP")            \
    X(CURSOR,"CURSOR-SHAPE") X(VIDEOBUF,"VIDEO-BUF-VAR")                 \
    X(DBGON,"DBG-ON") X(DBGLO,"DBG-LO") X(DBGHI,"DBG-HI") X(TICKDEBUG,"'DEBUG") \
    X(FSP,"FSP") X(PRECISION,"PRECISION-VAR")
/* these slots are VALUEs (code field DOVALUE), the rest VARIABLEs */
#define SV_IS_VALUE(i) ((i) == SV_COLS || (i) == SV_ROWS || (i) == SV_CROWS)

enum {
#define X(id, name) SV_##id,
    SYSVARS(X)
#undef X
    SV_COUNT
};

/* user area (per task) offsets, in cells */
#define USERVARS(X) \
    X(TOS,"TOS") X(ENTRY,"ENTRY") X(LINK,"LINK") X(SP0,"SP0")          \
    X(RP0,"RP0") X(DP,"DP") X(OFFSET,"OFFSET") X(BASE,"BASE")          \
    X(HLD,"HLD") X(PRINTING,"PRINTING")

enum {
#define X(id, name) U_##id,
    USERVARS(X)
#undef X
    U_COUNT
};
/* user DEFERs follow the user variables */
#define USERDEFERS(X) X(EMIT,"EMIT") X(KEYQ,"KEY?") X(KEY,"KEY") X(TYPE,"TYPE")
enum {
    U_DEFER_BASE = U_COUNT - 1,
#define X(id, name) U_##id,
    USERDEFERS(X)
#undef X
    U_END
};

#define NVOCS     12          /* search-order depth (#VOCS)             */
#define NTHREADS  64          /* hash threads per vocabulary            */

/* ---- host interface ------------------------------------------------------ */
typedef struct host {
    void (*emit)(struct host *h, int c);
    void (*type)(struct host *h, const uint8_t *s, size_t n);
    int  (*key)(struct host *h);            /* BIOS-style code, -1 = EOF */
    int  (*keyq)(struct host *h);
    int  (*eof)(struct host *h);               /* input has ended (batch) */
    void (*flush)(struct host *h);
    /* screen: put n chars at (x,y) with attribute; move the cursor;
       tty-style output at the cursor (handles CR LF BS BEL, scrolls) */
    void (*put)(struct host *h, int x, int y, const uint8_t *s, size_t n, int attr);
    void (*gotoxy)(struct host *h, int x, int y);
    void (*tty)(struct host *h, int c, int attr);
    void (*size)(struct host *h, int *cols, int *rows);
    void (*suspend)(struct host *h, int on);   /* hand the terminal over */
    void (*refresh)(struct host *h, struct vm *vm, int full); /* draw VIDEO-BUF */
    void (*scrolled)(struct host *h, int n, int down);
    void (*cursor_shape)(struct host *h, int shape);
    int  (*shift)(struct host *h);             /* BIOS shift-state flags */
    int  (*mouse_enable)(struct host *h);      /* 1 if mouse input works */
    void (*mouse_state)(struct host *h, int *x, int *y, int *buttons);
    void *priv;
} host_t;

host_t *host_batch_new(void);
host_t *host_tty_new(void);
void    host_tty_attach(host_t *h, vm_t *vm);
host_t *host_sdl_new(void);             /* NULL if unavailable */
void    host_sdl_attach(host_t *h, vm_t *vm);

/* ---- the VM ------------------------------------------------------------- */
struct vm {
    uint8_t *mem;
    ucell    memsize;

    ucell ip, w, sp, rp;               /* VM addresses                    */
    ucell sp0, rp0;
    ucell fstack_lo, fstack_hi;        /* float stack area (float.c)      */

    ucell sysvar;                      /* address of sysvar block         */
    ucell tib;                         /* TIB address                     */
    ucell tramp;                       /* trampoline area                 */
    ucell dosbuf;                      /* DTA (128), DOS-LINE (128), FPC-HOME (256) */
    int   depth;                       /* nesting of vm_execute           */

    ucell code_base, code_end;         /* regions                         */
    ucell list_base, list_end;
    ucell head_base, head_end;
    ucell heap_base, heap_end;

    vm_handler *handlers;
    ucell       nhandlers, maxhandlers;

    volatile sig_atomic_t interrupt;
    volatile sig_atomic_t attention;   /* 2 = refresh due, 4 = resize pending */
    host_t  *host;

    jmp_buf *catch_jmp;                /* where vm_throw goes             */
    const char *throw_msg;
    char     msgbuf[160];
    int      throw_code;
    int      bye;                      /* BYE executed                    */
    int      exit_code;

    void    *seed;                     /* seed interpreter state           */
    ucell    heap_free;                /* allocator free list (dos.c)      */
    void    *dos;                      /* DOS emulation state              */
    void    *screen;                   /* virtual screen (screen.c)        */
    ucell    resized_xt;               /* RESIZED, run after a resize      */
    ucell    dbg_last;                 /* trace hook: IP just reported     */
    vm_handler *cur_handler;           /* the HK_AMCODE handler running     */
};

/* ---- memory access -------------------------------------------------------- */
void vm_bad_address(vm_t *vm, ucell a, ucell n);

static inline void vm_chk(vm_t *vm, ucell a, ucell n)
{
    if (__builtin_expect(a > vm->memsize - n || a < 4, 0))
        vm_bad_address(vm, a, n);
}
static inline ucell rd32(vm_t *vm, ucell a)
{
    vm_chk(vm, a, 4);
    const uint8_t *p = vm->mem + a;
    return (ucell)p[0] | (ucell)p[1] << 8 | (ucell)p[2] << 16 | (ucell)p[3] << 24;
}
static inline void wr32(vm_t *vm, ucell a, ucell v)
{
    vm_chk(vm, a, 4);
    uint8_t *p = vm->mem + a;
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static inline ucell rd16(vm_t *vm, ucell a)
{
    vm_chk(vm, a, 2);
    return (ucell)vm->mem[a] | (ucell)vm->mem[a + 1] << 8;
}
static inline void wr16(vm_t *vm, ucell a, ucell v)
{
    vm_chk(vm, a, 2);
    vm->mem[a] = (uint8_t)v; vm->mem[a + 1] = (uint8_t)(v >> 8);
}
static inline ucell rd8(vm_t *vm, ucell a) { vm_chk(vm, a, 1); return vm->mem[a]; }
static inline void wr8(vm_t *vm, ucell a, ucell v) { vm_chk(vm, a, 1); vm->mem[a] = (uint8_t)v; }
/* check a whole range [a, a+n) */
static inline uint8_t *vm_ptr(vm_t *vm, ucell a, ucell n)
{
    if (n == 0) return vm->mem;
    if (a < 4 || n > vm->memsize || a > vm->memsize - n) vm_bad_address(vm, a, n);
    return vm->mem + a;
}

/* system and user variable addresses */
static inline ucell sv_addr(vm_t *vm, int i) { return vm->sysvar + (ucell)i * 8 + 4; }
static inline ucell sv(vm_t *vm, int i) { return rd32(vm, sv_addr(vm, i)); }
static inline void sv_set(vm_t *vm, int i, ucell v) { wr32(vm, sv_addr(vm, i), v); }
static inline ucell vm_up(vm_t *vm) { return sv(vm, SV_UP); }   /* user area */
static inline ucell uv_addr(vm_t *vm, int i) { return vm_up(vm) + (ucell)i * CELL; }
static inline ucell uv(vm_t *vm, int i) { return rd32(vm, uv_addr(vm, i)); }
static inline void uv_set(vm_t *vm, int i, ucell v) { wr32(vm, uv_addr(vm, i), v); }

/* stacks (data stack grows down, sp -> top item) */
static inline void push(vm_t *vm, ucell v) { vm->sp -= CELL; wr32(vm, vm->sp, v); }
static inline ucell pop(vm_t *vm) { ucell v = rd32(vm, vm->sp); vm->sp += CELL; return v; }
static inline ucell top(vm_t *vm) { return rd32(vm, vm->sp); }
static inline void rpush(vm_t *vm, ucell v) { vm->rp -= CELL; wr32(vm, vm->rp, v); }
static inline ucell rpop(vm_t *vm) { ucell v = rd32(vm, vm->rp); vm->rp += CELL; return v; }
static inline void dpush(vm_t *vm, udcell d) { push(vm, (ucell)d); push(vm, (ucell)(d >> 32)); }
static inline udcell dpop(vm_t *vm) { udcell hi = pop(vm); udcell lo = pop(vm); return hi << 32 | lo; }

static inline ucell aligned(ucell a) { return (a + 3u) & ~3u; }

/* ---- API ------------------------------------------------------------------ */
vm_t *vm_new(ucell memsize, host_t *host);
void  vm_free(vm_t *vm);
void  vm_execute(vm_t *vm, ucell xt);              /* run xt to completion */
_Noreturn void vm_throw(vm_t *vm, int code, const char *fmt, ...);
ucell vm_add_handler(vm_t *vm, int kind, vm_cfn fn, ucell data, const char *name);
void  vm_type(vm_t *vm, const uint8_t *s, size_t n);   /* via TYPE defer   */
void  vm_emit(vm_t *vm, int c);                         /* via EMIT defer   */
void  vm_types(vm_t *vm, const char *s);

/* throw codes (ANS-ish where they exist) */
enum { E_ABORT = -1, E_ABORTQ = -2, E_DSTACK_OVER = -3, E_DSTACK_UNDER = -4,
       E_RSTACK_OVER = -5, E_RSTACK_UNDER = -6, E_BADADDR = -9, E_DIV0 = -10,
       E_RANGE = -11, E_UNDEFINED = -13, E_COMPILE_ONLY = -14, E_INTERRUPT = -28,
       E_FILE = -38, E_BYE = -256 };

/* image.c */
int   image_save(vm_t *vm, const char *path);
int   image_load(vm_t *vm, const char *path, char *err, size_t errsz);
void  heap_free_block(vm_t *vm, ucell a);

/* float.c */
void  float_init(vm_t *vm, ucell lo, ucell hi);

/* am.c, jit.c */
void  am_bind(vm_t *vm, vm_handler *h);
int   jit_try(vm_t *vm, vm_handler *h, ucell start);

/* screen.c */
void  screen_init(vm_t *vm);
void  screen_tty(vm_t *vm, int c);
void  screen_put(vm_t *vm, int x, int y, const uint8_t *s, int n, int attr);
void  screen_note_resize(vm_t *vm);
int   screen_check_resize(vm_t *vm);
int   screen_take_resize_key(vm_t *vm);
void  screen_unget_key(vm_t *vm, int k);
int   screen_unget_take(vm_t *vm);
void  screen_cursor(vm_t *vm, int *x, int *y);
int   screen_cols(vm_t *vm);
int   screen_rows(vm_t *vm);
ucell screen_buf(vm_t *vm);

/* C-function primitives (defined in seed.c / io.c) */
#define PRIM(id, name, flags)
#define CPRIM(id, name, flags) void p_##id(vm_t *vm);
#include "prims.def"
#undef PRIM
#undef CPRIM

#endif
