# F-PC native port: kernel design

Status: accepted with revisions (2026-10-03: flat memory, C seed). Inputs: [inventory/code-words.md](inventory/code-words.md),
[inventory/memory-and-cells.md](inventory/memory-and-cells.md), [inventory/os-interface.md](inventory/os-interface.md),
and the DOSBox oracle (`tools/dosbox/`, `tests/golden/`).

## 0. Goals and non-goals

**Goals**

- A real port. F-PC's high-level `.SEQ` source stays the maintained artifact. The 8086, DOS and BIOS layer underneath is replaced.
- **32-bit cells** (decided). Doubles become 64-bit.
- A portable C VM (C11 plus the GCC/Clang computed-goto extension, with a `switch` fallback). All VM state lives in one struct passed by pointer.
- Native `CODE` words go through a new postfix assembler for a small abstract register machine (AM). It is JIT-compiled by sljit, and there is an AM interpreter for when JIT isn't allowed (W^X policies, iOS).
- Images never contain machine code.
- The original's behaviour stays checkable against the DOSBox oracle. Where results differ, the difference must come from cell width and be written down.

**Non-goals (for now)**

- Running unported 16-bit source unchanged. That was the 16-bit-VM option, which was rejected.
- The 8086 tooling: PASM, the disassemblers, the 8087 float packages, EMS/XMS, overlays, interrupt hooks, RS-232. These are dropped or later re-done against the AM or C.
- Byte-identical memory layout with the original.

## 1. Cells, numbers, arithmetic

| | Rule |
|---|---|
| Cell | 32 bits, two's complement, **little-endian in VM memory on every host**. The C side accesses memory through `le32`/`le16` helpers (`memcpy` plus a byte swap on big-endian hosts), so images are portable. Unaligned access is allowed. |
| Double | 64 bits, stored F-PC style: high cell on top of the stack, and in memory `2!` puts the high cell at the lower address. |
| Char | 1 byte, CP437 inside the VM. Mapping to and from UTF-8 happens only in the host terminal layer. |
| Overflow | All cell arithmetic wraps modulo 2³². The C code does it in `uint32_t`, never in signed `int`, to avoid UB. |
| Division | Keep F-PC's **floored** `/ MOD /MOD */ */MOD` (the golden shows `-7 2 /` = -4). Add `SM/REM` and `FM/MOD` as built-ins. |
| `UM/MOD` overflow | Returns `-1 -1`, as the original does. |
| Divide by zero | Raises the VM error that the original reached through INT 0: it runs `DIV0FUNC` and aborts with "Divide OVERFLOW error". It is never a host SIGFPE. |
| `DO`/`LOOP` | Same termination rule as the original: the loop exits when the index crosses the boundary between limit-1 and limit. It's implemented with the usual `$80000000` bias and an overflow test. |
| `TRUE` | -1. `0 NOT` = -1 is kept: F-PC's `NOT` is bitwise invert. |
| `CMOVE` | Ascending **byte** copy. This **deliberately differs** from the original, which copies 16-bit words: the golden `strings` test shows `aabbdd` where we will give `aaaaaa`. `CMOVE>` is a descending byte copy. Neither uses `memmove`. |

New words for cell-size-neutral source: `CELL` (4), `CELL+`, `CELL-`, `CELLS`, `CELL/`, `HCELL` (2). §8 covers how existing `2+`/`2*` uses are converted.

## 2. Memory model

### 2.1 Flat addresses (decided)

All VM memory is one byte array, `vm->mem[0 .. memsize)`. A VM address is a 32-bit offset into it, never a host pointer, so images stay relocatable and every access can be bounds-checked.

**Every address is a single cell.** `@ ! C@ C! CMOVE FILL` work on any region.

**The segment vocabulary is removed and its uses are rewritten.** Removed words:
- `@L !L C@L C!L CMOVEL CMOVEL> LFILL COUNTL PLACEL +PLACEL`;
- `?CS: ?DS: ?ES: SSEG ES0 XSEG YSEG XDPSEG +XSEG PARAGRAPH DPARAGRAPH U16/ UD16/ CMOVE-PARS`;
- `Y@ Y! YC@ YC! YS: XS:`.

How uses change:
- **Seg:off pairs become one address.** For example `TYPEL` is replaced by `TYPE`, and `EXHREAD ( a n h seg -- )` becomes `HREAD ( a n h -- )`.
- **Code that reads head space** uses `@`/`C@` directly.

The inventory puts this at about 355 kernel lines and 503 extension lines (memory-and-cells §1.4), and they are reviewed together with the cell-size edits (§8). The other segment idioms change like this:

| Idiom | Becomes |
|---|---|
| Colon body = relative list paragraph (`XHERE PARAGRAPH + DUP XDPSEG ! XSEG @ - ,`) | Body cell = flat address of the token list (`XHERE ,`); no alignment padding |
| `>BODY @ +XSEG` readers (DECOM, DEBUG, DEFERS, DUMP, REF, LEDIT, MENUS, SEDIT2, SEDCHARS, MOUSEY) | `>BODY @` |
| POINTER heap of paragraphs | Byte-addressed blocks. `%UNPOINTER` compaction keeps working with `CMOVE`; sizes are in bytes. |
| SED line-per-paragraph storage | Byte addresses, with each line cell-aligned |
| `VIDEO-SEG` + `CMOVEL` screen save/restore | `VIDEO-BUF` (an address) + `CMOVE` |
| Command tail at `CS:80`, environment at `CS:2C` | Host words `CMDTAIL ( -- a n )` and `GETENV ( a n -- a' n' f )` |
| BIOS data area peeks (`0:417`, `$40:xx`, `0:460`) | Host words (`SHIFT-STATE`, `CURSOR-SHAPE@`, …) |

### 2.2 Regions

F-PC's three dictionary spaces stay as **logical regions inside the one address space**, each with its own allocation pointer:

```
0x00000000  reserved, zero, never allocated (so 0 is never a valid address or xt)
0x00000100  CODE/DATA   HERE  ,  C,  ALLOT   bodies, variables, user area, TIB, stacks,
                                             FIRST/LIMIT buffers
LISTBASE    LIST        XHERE X, XC,         colon-definition token lists
HEADBASE    HEAD        YHERE Y, (=, C,)     headers + >NAME hash table
HEAPBASE    HEAP        ALLOC/FREE/RESIZE    POINTER blocks, editor buffers, screen buffer
```

The separation is worth keeping:
- **Headers stay in their own region.** `BEHEAD`, headerless definitions and `TURNKEY` drop heads simply by dropping that region.
- **Data and code stay distinct.** The decompiler and the size statistics rely on token lists (`X,`) being separate from data (`,`).
- **Per-region overflow checks** (`SPCHECK`) keep working.

Region bases and sizes are image-header parameters; the defaults are 4 MB code, 4 MB list, 2 MB head and 16 MB heap. The stacks stay in VM memory, because F-PC uses `SP@` as a buffer address (`FEMIT`, `ROLL`).

## 3. Execution model

### 3.1 Code fields and code tokens

A word's **code field is one cell holding a code token**, `ct`. Code tokens are indices into the VM's handler table:

```c
typedef struct {
    uint8_t   kind;      /* BUILTIN, RUNTIME (NEST/DOVAR/...), AMCODE, CFUNC */
    uint16_t  label;     /* computed-goto slot for BUILTIN/RUNTIME */
    vm_cfn    fn;        /* C or JIT function for AMCODE/CFUNC: int fn(vm_t *vm) */
    uint32_t  src;       /* AMCODE: VM address of the word's AM op stream */
    const char *name;    /* stable name, used to rebind on image load */
} vm_handler;
```

Why tokens rather than host pointers or 8086-style `E8/E9 rel16`:

- Host pointers are 64 bits and must not be saved in images.
- A token is one cell. It can be compared with `=`, and the image loader can rebind it.
- C functions and JIT output are first-class: any word's code field can name an `AMCODE` or `CFUNC` handler, which meets the "code field can point at a C function" goal.

Effect on the source:

- `>BODY` = `CELL+` and `BODY>` = `CELL-`.
- Code that sniffs code fields with `@REL>ABS` or `C@ 232 =` (about 15 sites in DECOM, DEBUG, WORDS, MAKEDEF, COLORIZE, DEFERS, UTILS and REF) is rewritten in terms of `@CT ( cfa -- ct )`, `!CT ( ct cfa -- )` and constants such as `CT-NEST`, `CT-DOVAR` and `CT-DODOES`.

### 3.2 Runtime handlers (code-field targets)

| Handler | Body layout | Action |
|---|---|---|
| `NEST` | cell: address of the token list | push IP; IP = body cell |
| `DOVAR` | data | push PFA (replaces `CALL >NEXT`) |
| `DOCONST`, `DOVALUE` | cell | push the cell |
| `DODEFER` | xt cell | execute it |
| `DOUSER-VAR`, `DOUSER-DEFER` | user-area offset | UP-relative |
| `DODOES` | cell: address of the DOES> token list; then data | push PFA (the second body cell); NEST into the DOES> list |
| `DOCODE` | AM op stream | run via JIT or the AM interpreter (§5) |

- **A colon body is one cell: the flat address of its token list** in the LIST region. The `>BODY @ +XSEG` readers become `>BODY @` (§2.1).
- **DOES> no longer uses the two-level CALL stub.** Instead the child's code field is `DODOES`, its first body cell names the DOES> list, and its data follows. `(;CODE)`/`(;USES)` become `!CT` on the latest word.

### 3.3 Threading, IP and the return stack

- **Threading.** A list body is a sequence of xts (cells). The inner loop is:

  ```
  w = rd32(ip); ip += 4; ct = rd32(w); dispatch handler[ct]
  ```

  `BUILTIN` and `RUNTIME` handlers are reached through `goto *labels[h.label]`. `AMCODE` and `CFUNC` handlers are called as `h.fn(vm)` after the cached registers are flushed to the struct.
- **IP is a flat address.** Return-stack frames are **one cell**, where the original used two (ES, IP).
- **Inline operands** follow the token: literals, branch targets and the xts read by `<'>`, `%xx>`, `GOTO` and `EXEC:`.
- **Branch targets are flat addresses.**
  - `>MARK`, `>RESOLVE`, `<MARK` and `<RESOLVE` use `XHERE ( -- addr )`, which now returns a single address.
  - The DO frame becomes `[leave-addr][limit-bias][index-bias]`, three cells.
- **Inline-data sites are edited by hand.** About 50 high-level places read inline data from the return stack. The rewrites are `2R@ @L` → `R@ @`, `R> 2+ >R` → `R> CELL+ >R` and `2R>` → `R>`. The affected words include COMPILE, `(")`, `(.")`, `(ABORT")`, `CRASH`, `(IS)`, `X>"BUF`, DEBUG, MACROS, NEWLAB, WFL, BROWSEPR and TIMESTUF. They are listed in memory-and-cells §1.2, and a search for `2+` would not find them, so each one is checked by hand.

### 3.4 VM state

```c
typedef struct vm {
    uint8_t  *mem;  uint32_t memsize;
    uint32_t ip, w, sp, rp, up;        /* sp/rp/up are VM addresses */
    uint32_t sp0, rp0;
    volatile sig_atomic_t interrupt;   /* Ctrl-Break / SIGINT */
    uint32_t trace_xt;                 /* debugger hook, 0 = off */
    vm_handler *handlers; uint32_t nhandlers;
    struct host *host;                 /* console/files/time, §6 */
    jmp_buf  *abort_jmp;               /* VM errors -> ABORT path */
} vm_t;
```

- The dispatch loop keeps `ip`, `sp` and `rp` in locals and writes them back around C, JIT and host calls.
- Top-of-stack caching can come later. The decompiler and debugger only see VM memory, so caching is safe.

### 3.5 What replaces the original's self-modifying code

| Original | Port |
|---|---|
| NESTPATCH/DOESPATCH (XSEG patched into code at cold start) | Not needed: bodies hold flat addresses |
| Ctrl-Break overwriting `>NEXT` (BIOSBK/ABNORM/SETBRK) | A SIGINT handler sets `vm->interrupt`. It is tested in `NEST`, `BRANCH`, `?BRANCH`, `(LOOP)` and `(+LOOP)`, so tight loops still break out, and it vectors to WARM through the existing high-level path. |
| Debugger patching `>NEXT` (DEBUG.SEQ FNEXT/DNEXT/PNEXT, DBGFIX) | `vm->trace_xt`. When non-zero, a second dispatch loop calls it before each token, with IP available. DBGFIX is dropped. DEBUG.SEQ keeps its high-level UI. |
| `PAUSE` NOP patch (MULTASK) | `PAUSE` becomes a DEFER (default `NOOP`). Tasks are user areas plus saved sp/rp/ip, switched by a built-in. |
| CORIG/WORIG/ORIGIN/SEXE start-up relocation | C image loader (§7) |
| Machine-code tail-jumps into high-level xts (%NUMBER, %FIND, (?STACK), SPCHECK, ?FILLBUFF …) | Built-ins can return "execute this xt next". Most of these words are simply high-level in the port. |

## 4. Kernel source and bootstrap

### 4.1 Source tree

- Tag the import commit `fpc-3.6-original`. The DOSBox build and test tools check out that tag, so the oracle never changes.
- Port the `.SEQ` files **in place** under `SRC/`. `git diff fpc-3.6-original -- SRC/` is then the complete, reviewable record of the port.
- C sources go in a new `vm/` directory. `TOOLS/` is ported on demand.

### 4.2 What happens to the kernel's native code

The kernel has 293 native words, about half its lines.

| Class (inventory) | Count | Port |
|---|---|---|
| PRIM, RUNTIME | ~110 | C built-ins (inventory §4.3–4.4) |
| PERF | ~115 | C built-ins where hot or trivial (inventory §4.5). Otherwise high-level Forth, often from the original's own comments. |
| SEG | ~86 | Mostly gone (§2.1). What remains (string compare and search, header traversal) becomes ordinary flat C built-ins. |
| DOS, BIOS, VIDEO, HW | ~60 | C built-ins at the existing seams, calling the host (§6) |
| SELFMOD and start-up | ~25 | Removed (§3.5) |

- The kernel files no longer contain `CODE` bodies. Each C built-in is declared in the `.SEQ` file where its CODE word used to be, as a one-line declaration: `BUILTIN DUP  ( n -- n n )`.
- `BUILTIN` creates a header whose code field is the built-in's token, looked up by name. Loading fails if the VM doesn't provide it.
- The kernel source therefore still documents its complete word set in the original order.

### 4.3 Bootstrap: a C seed instead of META86

`META86.SEQ` generates 8086 code and 16-bit images, and needs a running F-PC to run it. Both make it unsuitable as the bootstrap path. Instead:

1. **The seed** (`vm/seed.c`, roughly 1–1.5k lines) creates headers for all built-ins and provides a minimal outer interpreter and compiler. That covers `: ; IMMEDIATE CREATE DOES> VARIABLE CONSTANT DEFER IS`, the control-flow words, `."`, `"`, `\`, `(`, `FLOAD`, numbers, and the metacompiler spellings the kernel uses (`T:`, `FORWARD:`, `IN-META` and so on, reduced to no-ops or aliases). The seed's own `ok` prompt is milestone M1.
2. The seed `FLOAD`s the ported kernel files in META86's order: KERNEL1, VIDEO, KERNEL2, VIDEO2, KERNEL3, EXPAND, EMMEXEC, POINTER, EQUCOLON, SAVEREST, HANDLES, SEQREAD, FPATH, DEFAULT, HCRITICA, KERNEL4. As the kernel defines its own `:`, `IF`, `INTERPRET` and the rest, later definitions shadow the seed's.
3. The seed then executes the kernel's `COLD`, and **F-PC's own `QUIT` takes over**.
4. `FLOAD F-PC.SEQ` adds the extensions, and `SAVE-IMAGE` writes `fpc.img`. Normal start-up loads that image and never runs the seed.

`META86.SEQ` stays in the tree as a historical record, unported. A self-hosted `META32` (the port's own kernel metacompiling itself) is a possible later step (§11).

## 5. Native code: the abstract machine (AM) and assembler

- **Registers.**
  - VM registers: `IP SP RP W UP`.
  - Six scratch registers `R0–R5`.
  - `T`, a cached-TOS view that only exists inside a CODE body.
- **Operations.** Load and store (cell, half and byte, flat addresses); `add sub and or xor shl shr sar mul divmod`; compare-and-branch to local labels; push and pop on either stack; `NEXT`; `EXECUTE`; `HOST <n>`. AM code addresses VM memory only, so it can't escape the sandbox, and every access is bounds-checked in the interpreter.
- **Encoding.** A `CODE` word compiles an **AM op stream into its own body**; the code field is `DOCODE`. The op stream is ordinary VM data, so it is saved in images (§7).
- **Execution.**
  - The first time a word runs, or at image load, sljit compiles the stream into a `vm_cfn`, and the handler entry is switched from interpret to JIT.
  - Where JIT isn't available or isn't allowed, the AM interpreter runs the stream directly, with identical semantics.
  - A test mode runs both and compares the results.
- **Syntax.** A new vocabulary `AM` with postfix syntax modelled on PASM's style. For example, `CODE 2DUP  SP 4 [] R0 LD,  SP 0 [] R1 LD,  R0 PUSH,  R1 PUSH,  NEXT,  END-CODE`. The exact syntax gets its own short doc before M6.
- **Scope.** The kernel uses no AM code; everything there is a C built-in. The AM is for extensions (the 54 extension CODE words, mostly SED's SEDCODE helpers, and `TOOLS/`) and for user code.

## 6. Host interface

`struct host` is a table of function pointers (inventory os-interface §5.1 is the starting list, about 40 calls). There are three implementations:

- **`host_tty`**: ANSI terminal plus raw keyboard.
- **`host_sdl`**: an SDL window with a CP437 font. Optional, added later.
- **`host_batch`**: plain stdin/stdout, with no screen and no cursor addressing. It is used for tests and scripts.

The default is `host_tty` when stdout is a terminal and `host_batch` otherwise. This is what lets the regression suite run natively without DOSBox.

| Area | Seam words (unchanged names) | Implementation |
|---|---|---|
| Files | `<BDOS>` `BDOS2` `HDOS1/3/4` `MOVEPOINTER` `ENDFILE` `CURPOINTER` `<HRENAME>` `FIND-FIRST/NEXT` `DTA@/!` | C built-ins that **dispatch on the DOS function numbers the call sites already pass** (`$3D00 HDOS1` and so on). About 30 INT 21h functions are emulated over POSIX. Paths are mapped by dropping the drive letter, translating `\`→`/` and matching names case-insensitively. Handles are host fds. The HCB layout is widened to cell-sized fields, with all access going through `>HNDLE`, `>ATTRIB` and `>NAM`. |
| Memory | `DOS_ALLOC` `DOS_DEALLOC` `DOS_SETBLOCK` `DOS_MAXBLOCK` | Replaced by `ALLOCATE`/`FREE`/`RESIZE` (byte addresses) over the HEAP region; callers are updated |
| Keyboard | `BIOSKEY?` `BIOSKEY` | The host returns BIOS-style `scan<<8 \| ascii` values, so `(KEY)`'s `128 OR scan` encoding and every key table stay as they are. Escape sequences are decoded into scan codes in the host. A new `SHIFT-STATE` word replaces reads of `0:417`. |
| Screen | `VIDEO-TYPEL` `VIDEO-TYPE` `?VMODE` `IBM-AT` `IBM-AT?` `SET-CURSOR` `IBM-DARK` `IBM--LINE` | A virtual text screen (COLS×ROWS cells of char+attr) held **in VM memory** at `VIDEO-BUF`. There is a single cursor shared by the `TYPE` path (direct write, clipped) and the `EMIT`/`CR` path (TTY: wraps and scrolls). The host diff-renders on `KEY`/`KEY?`/idle. |
| Time | DOS `2Ch`/`2Ah` via `<BDOS>`; `MS` busy loop | Host clock; `MS` = `host_sleep_ms` |
| Break | INT 1Bh/23h | SIGINT → `vm->interrupt` (§3.5) |
| Process | `<EXTEXEC>` and the shell/EXEC words | `host_system()`. EMS image swapping is dropped. |
| Stubbed | EMS, printer (PRN → file or stdout), speaker ports (`BEEP` → `\a`), CGA snow ports, interrupt vectors, critical-error handler, mouse (until the SDL/xterm mouse is done) | |

## 7. Images

`SAVE-IMAGE <name>` replaces `SAVE-EXE`, LOADEXE and SAVEEXE. Format version 1:

```
header   magic "FPCIMG\0\1", cell size (4), endianness (LE), region table
         (base address + used length for CODE, LIST, HEAD, HEAP), entry xts
         (COLD, WARM), VM register seeds (sp0, rp0, up)
handlers table of (token, kind, name, src): one row per non-built-in token
         plus the names of all built-in tokens used, for validation
regions  raw bytes of each region's used part
```

- **No machine code and no host pointers.** On load:
  - built-in tokens are matched to the running VM by name, and loading fails if one is missing;
  - each `AMCODE` entry is re-JIT-compiled from its `src` op stream, or is left to the interpreter.
- **Turnkey programs.** `TURNKEY` writes an image without heads. Optionally the image can be appended to a copy of the `fpc` binary, which finds it at start-up.

## 8. Porting the high-level source to 32-bit cells

The process is mechanical where it can be and reviewed by hand everywhere else:

1. **Lint script.** `tools/cellscan` flags segment words (§2.1) and cell-size idioms per line: `2+ 2- 2* 2/` near addresses, `2 ALLOT`, `3 +`, `>BODY` arithmetic, `232`/`233`, `$FFFF`, `$8000`, `FLIP`, `SPLIT`, `JOIN`, `2R@ @L`, `R> 2+ >R`, and fixed structure offsets. Its output is the work list. A file counts as ported when every hit is either fixed or annotated `\ cell-ok`.
2. **Fixed structures** move to cell fields: the user area, the POINTER body, the HCB, the header (VFA, LFA and CFA-pointer become cells), the vocabulary threads, and `FILEPOINTER`, which becomes a `2VARIABLE`.
3. **Head space.** `CNHASH` becomes `CFA 9 RSHIFT CELLS`, with the >NAME table sized from the CODE region. `CNSRCH`, `(FIND)`, `HASH`, `YHASH` and `TRAVERSE` become C built-ins with no 16-bit tricks.
4. **Doubles.** Mostly unchanged in meaning. Only code that relies on 16-bit overflow (`UM* DROP` idioms, `$FFFF 0 DMIN`) needs review.
5. **Order.** Kernel first (M2), then the extensions in `F-PC.SEQ` order, deferring SED and the screen UI to M5.

## 9. Testing

- **Oracle.** `tests/golden/<t>.KERNEL.out` holds 16-bit results recorded from the original in DOSBox, using `tools/dosbox/runtests.sh`.
- **Port expectation.** `tests/expected/<t>.out` holds what the 32-bit port must print. It starts as a copy of the golden output, and every changed line gets a short comment entry in `tests/expected/DIVERGENCES.md` explaining why (cell width, `CMOVE` semantics, …). A differing line with no entry fails review.
- **Native runner.** `make test` runs `vm/fpc --batch` on each test and diffs against `tests/expected/`.
- **New tests should be cell-width-neutral where possible**, using `CELL`, `TRUE` and so on, so that one golden serves both builds.
- **Further suites.** An adapted Hayes core test is added once the kernel is up, and an AM-interpreter-versus-JIT cross-check comes with M6.

## 10. Milestones

| | Deliverable | Done when |
|---|---|---|
| M1 | `vm/`: memory, built-ins, dispatch loop, seed interpreter, `host_batch`, `FLOAD` | `ok` prompt; `arith`, `stack` and `control` tests pass against `tests/expected` |
| M2 | Ported kernel `.SEQ` loads on the seed; F-PC's QUIT runs; DOS-function file I/O | all `tests/*.seq` pass; `FLOAD` nests; errors report file and line as the original does |
| M3 | `SAVE-IMAGE` and image start-up | cold start from the image is under 50 ms; round-trip test |
| M4 | Non-screen extensions: WORDS, DECOM/SEE, DUMP, VOCABS, NEEDS, DEFERS, REF … | each extension file loads; decompiler tests pass |
| M5 | `host_tty` virtual screen and keyboard; LEDIT/NEXPECT, status line, menus, SED | SED edits and saves a file; scripted keyboard tests pass |
| M6 | AM assembler, interpreter, sljit backend; SEDCODE ported to AM | JIT and interpreter cross-check passes; works with JIT disabled |
| M7 | Trace-hook debugger, multitasker, `host_sdl` | DEBUG steps a word |

## 11. Decisions and deferred items

**Decided (2026-10-03)**

- 32-bit cells.
- Flat memory with logical regions (§2). The segment words are removed rather than emulated.
- C seed bootstrap (§4.3). META86 is not ported.
- `SRC/` is edited in place, freely. The original lives at the `fpc-3.6-original` tag and elsewhere. It is used only as the DOSBox oracle, not preserved in the working tree.

**Open**

- **`CMOVE`** changes to byte semantics (§1). If any extension turns out to depend on word-wise moves, it gets an explicit `WMOVE` instead.
- **Case sensitivity of file names** on case-sensitive hosts. The plan is a case-insensitive lookup when opening; creating a file uses the case as written.
- **A self-hosted metacompiler** (the 32-bit kernel building itself) is optional, after M3.
