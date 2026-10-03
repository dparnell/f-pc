# F-PC 3.6: Native-Code Inventory

This inventory covers every native (8086 machine-code) definition in F-PC 3.6. It is the input for the planned native port: a portable C VM kernel, with the high-level `.SEQ` source kept as the maintained artifact.

**Scope**
- (a) The kernel: `SRC/KERNEL1-4.SEQ`. META86 also loads twelve other files between the KERNEL files (`SRC/META86.SEQ:674-689`), and their code ends up in KERNEL.COM, so they are covered too. The load order is: KERNEL1, VIDEO, KERNEL2, VIDEO2, KERNEL3, EXPAND, EMMEXEC, POINTER, EQUCOLON, SAVEREST, HANDLES, SEQREAD, FPATH, DEFAULT, HCRITICA, KERNEL4.
- (b) Every file FLOADed by `SRC/F-PC.SEQ`. That is 88 files, including the `true #if ... #endif` SED-editor section (F-PC.SEQ:76-131). `MULTASK.SEQ` is commented out (F-PC.SEQ:42) and is covered only in the notes. None of these files FLOAD further files, and FL.SEQ is just two aliases.
- (c) `TOOLS/`, summarised per file.

**Method**
- Files were normalised with `tr -d '\r\032'`, which keeps line numbering. All `file:line` references are to lines in the original files.
- A comment-aware scanner found every `CODE`, `LABEL`, `;CODE`, `;USES`, `INLINE` and `END-CODE`/`C;`/`END-INLINE`. It skips `\` comments, `( )`, `.( )`, `comment: ... comment;` and string literals. Every hit was then read by hand.
- Separate greps looked for:
  - raw opcode bytes (`232`/`233`/`$E8`/`$E9` compiled with `C,`/`!`);
  - `>NEXT` patching;
  - `@REL>ABS` code-field decoding;
  - `INT`, `IN`/`OUT` and segment-register use.
- No `INLINE ... END-INLINE` and no `[ASSEMBLER]`-in-colon-definition inline assembly is *used* in any loaded file. `INLINE`/`END-INLINE` are only *defined*, at PASM.SEQ:840-844. Every `[ASSEMBLER]` occurrence in the kernel only reads a label address as a literal (KERNEL1:11,13; KERNEL4:43-44; SAVEREST:112-135).

**Classification key**

| Class | Meaning |
|---|---|
| PRIM | Pure stack, arithmetic or memory primitive. Becomes a C VM built-in. |
| RUNTIME | Inner interpreter, NEST/EXIT/DOES>, branch and loop machinery, inline-operand words. |
| SEG | Uses segment registers or far memory (including ES:IP list threading, YSEG heads, SSEG). |
| DOS | INT 21h. |
| BIOS | INT 10h/16h/17h, the BIOS data area (0:4xx / 40:xx), and the driver interrupts INT 33h (mouse) and INT 67h (EMS). |
| VIDEO | Direct writes to B800/B000 (VIDEO-SEG). |
| HW | Port I/O, interrupt vectors, ISRs. |
| SELFMOD | Patches machine code at run time. |
| PERF | CODE only for speed. It has, or could have, a high-level equivalent. |

A word can carry more than one class.

**Register conventions** (PASM.SEQ:240-241, 785-801)
- `IP`=SI (an offset into the list segment held in **ES**), `RP`=BP, data stack = the 8086 `SP`, W=AX (the CFA on entry).
- DS = CS = SS: one 64K segment holds code, variables, the user area, both stacks and TIB.
- `NEXT` = `ES: LODSW / JMP AX`.
- `1PUSH` = `PUSH AX; NEXT`, and `2PUSH` = `PUSH DX; PUSH AX; NEXT`. With `INLINE_NEXT` true (KERNEL1:5) all three are expanded inline. Otherwise they are `JMP >NEXT`, `>NEXT 1-` (APUSH) and `>NEXT 2-` (DPUSH).
- Direction flag is assumed clear.
- Words that call the BIOS save SI/BP themselves.

## 1. Summary counts

### 1a. Kernel (KERNEL.COM)

"Native defs" = CODE + LABEL (+ `;CODE`). `;USES` lines are high-level defining words that lay a machine-code CFA (`CALL`/`JMP` to a kernel label). They are not native bodies and are counted separately; see section 2c.

| File | CODE | LABEL | Native defs | `;USES` CFAs | Labels inside colon defs | Main classes |
|---|---|---|---|---|---|---|
| KERNEL1.SEQ | 149 | 14 | **163** | 0 (+4 metacompiler CFA generators) | 2 (NESTPATCH, DOESPATCH) | PERF 79, RUNTIME 45, PRIM 31, SEG 22, HW 6, SELFMOD 6, BIOS 3, DOS 1 |
| KERNEL2.SEQ | 52 | 0 | **52** | 0 | 0 | SEG 29, PERF 19, DOS 5, BIOS 3, PRIM 2, RUNTIME 1 |
| KERNEL3.SEQ | 14 | 0 | **14** | 9 | 0 | PERF 10, SEG 4, RUNTIME 4 |
| KERNEL4.SEQ | 4 | 5 | **9** | 2 | 3 (WARMBODY, COLDBODY, DIV0BODY) | RUNTIME 7, HW 5, DOS 3, SEG 3, SELFMOD 2, BIOS 1 |
| *KERNEL1-4 subtotal* | *219* | *19* | ***238*** | *11* | *5* | |
| VIDEO.SEQ | 3 | 0 | 3 | | | HW 2, BIOS 1 |
| VIDEO2.SEQ | 2 | 0 | 2 | | | VIDEO 2, HW, BIOS, SEG |
| EXPAND.SEQ | 10 | 0 | 10 | | | BIOS(EMS) 8, DOS, SEG, PERF |
| EMMEXEC.SEQ | 1 | 3 | 4 | | | DOS 4, SEG 4, BIOS(EMS), RUNTIME |
| POINTER.SEQ | 1 | 0 | 1 | | | RUNTIME, SEG |
| EQUCOLON.SEQ | 10 | 0 | 10 | | | RUNTIME 10 (inline-operand) |
| SAVEREST.SEQ | 6 | 0 | 6 | | | RUNTIME 6 (inline-operand) |
| HANDLES.SEQ | 10 | 0 | 10 | | | DOS 10, SEG 4 |
| SEQREAD.SEQ | 7 | 0 | 7 | | | PERF 6, RUNTIME 2, DOS, SEG |
| HCRITICA.SEQ | 1 | 1 | 2 | | | HW 2, DOS |
| FPATH.SEQ, DEFAULT.SEQ | 0 | 0 | 0 | | | |
| **Kernel total** | **270** | **23** | **293** | **11** | **5** | RUNTIME 77, SEG 86, PERF 115, PRIM 33, DOS 26, BIOS 18, HW 16, SELFMOD 8, VIDEO 2 |

### 1b. Files FLOADed by SRC/F-PC.SEQ

17 of the 88 files contain native code. PASM.SEQ, the run-time assembler, is listed in the table for reference but contains none. The remaining 70 files are entirely high-level: TIMER, TIMESTUF, COMMENT, UTILS, UNHEAD, BRACES, VOCABS, DEFERS, BUFSET, VALIDATE, DECOM, DUMP, CASE, SAVEEXE, PATHSET, HYPER, WORDS, MONOCROM, COLOR, COLORIZE, BOXTEXT, SAVESCR, PERTYPE, HELLO, LEDIT, VIEW, STATUS, FL, NEEDS, FILSTAT, ENVIRON, EXEC, EMMPTR, MENUS, PRINT, EDITSTUF, SEDIT2, PRTCTRL, LASERJET, PROPRINT, SEDCASE, SEDITWP, SEDJUST, SEDDRAW, SEDSORT, SEDCOPY, SEDAPND, SEDPAGE, SEDWIND, SEDCHARS, SEDSHELL, HTYPE, SEDWHELP, TOPEDIT, HELPLINK, EDITSET, SEDMENU, NEWFILE, EDITERR, BROWSEPR, MLOAD, SOUND, REF, WATCHER, WINSTACK, NEXPECT, LISTSET, MOUSEY, MACROS and SVSESDAT. Several of them still depend on code-field layout; see section 2c.

| File | CODE | LABEL | `;CODE` | Native defs | Classes |
|---|---|---|---|---|---|
| PASM.SEQ | 0 | 0 | 0 | 0 | the 8086 assembler itself: emits x86, and defines CODE/LABEL/NEXT/INLINE |
| NEWLAB.SEQ | 0 | 0 | 1 | 1 | PERF |
| LOADEXE.SEQ | 0 | 1 | 0 | 1 | RUNTIME, SEG |
| DBGFIX.SEQ | 1 | 0 | 0 | 1 | SELFMOD |
| DEBUG.SEQ | 2 | 3 | 0 | 5 | SELFMOD 3, RUNTIME 2, SEG 2 |
| SEARCH.SEQ | 1 | 0 | 0 | 1 | PERF, SEG |
| LARGEST.SEQ | 1 | 0 | 0 | 1 | PERF |
| IBMCURSR.SEQ | 5 | 0 | 0 | 5 | BIOS 4, SEG/VIDEO 1 |
| BLINKER.SEQ | 4 | 0 | 0 | 4 | BIOS 4 |
| QVIDEO.SEQ | 1 | 0 | 0 | 1 | BIOS |
| WFL.SEQ | 2 | 0 | 0 | 2 | PERF |
| MAKEDEF.SEQ | 1 | 0 | 0 | 1 | PERF (+ high-level SELFMOD `makedefer`) |
| SEDCODE.SEQ | 19 | 0 | 0 | 19 | PERF 14, SEG 6, DOS 1 |
| SEDITOR.SEQ | 1 | 0 | 0 | 1 | PERF, RUNTIME |
| PRINTING.SEQ | 1 | 0 | 0 | 1 | DOS |
| SCAN.SEQ | 3 | 0 | 0 | 3 | PERF, SEG |
| FWORDS.SEQ | 1 | 0 | 0 | 1 | PERF |
| MOUSE.SEQ | 6 | 0 | 0 | 6 | BIOS (INT 33h) 6 |
| **F-PC.SEQ total** | **49** | **4** | **1** | **54** | PERF 25, BIOS 15, SEG 14, RUNTIME 4, SELFMOD 4, DOS 2, VIDEO 1 |

### 1c. TOOLS/ (optional add-ons; details in section 5)

66 `.SEQ` files, 17,812 lines. Native code: **232 CODE (+1 TCOM-only), 30 LABEL, 1 `;CODE`, in 24 files**. Most of it is in the float packages: FFLOAT 60+10, SFLOAT1-3 52+6 (+1 `;CODE`), FLTAUX 15, NEW-WFL 24.

### 1d. Grand total

| Area | Native definitions |
|---|---|
| Kernel (KERNEL1-4 + 12 META86-loaded files) | 293 (238 in KERNEL1-4) |
| F-PC.SEQ extensions | 54 |
| **KERNEL.COM + F-PC.EXE** | **347** |
| TOOLS (optional) | 263 |


## 2. Per-file tables

Line numbers are those of the original files. The semantics column gives the stack effect where the source states one, or where it was derived by reading the code.

### 2a. Kernel files KERNEL1-4.SEQ

#### KERNEL1.SEQ (163 entries)

Also in this file:
- `LABEL NESTPATCH` (125) and `LABEL DOESPATCH` (168) are internal labels inside NEST and DODOES. They mark the `ADD AX,#imm` instructions patched at cold start.
- Lines 204-274 hold metacompiler-side CFA generators; see 2c.
- `INLINE_NEXT` is TRUE (line 5), so the kernel is built with inline NEXT.

| Word | Line(s) | Kind | Semantics | Class |
|---|---|---|---|---|
| `ORIGIN` | 29-31 | LABEL | Two `JMP` slots at 100h (cold, warm); targets patched by metacompiler (KERNEL4:274,284) | RUNTIME SELFMOD |
| `DPUSH` | 33 | LABEL | `PUSH DX` then falls into APUSH; target of `2PUSH` (`JMP >NEXT 2-`) | RUNTIME |
| `APUSH` | 34 | LABEL | `PUSH AX` then falls into >NEXT; target of `1PUSH` (`JMP >NEXT 1-`) | RUNTIME |
| `>NEXT` | 35-36 | LABEL | Inner interpreter: `ES: LODSW / JMP AX` (fetch next CFA from ES:SI list space); patched at runtime by BIOSBK/DEBUG | RUNTIME SEG |
| `ABNORM` | 76-86 | LABEL | Re-writes original >NEXT bytes (26 AD FF E0), clears BIOS break flag 0:0471, jumps to warm entry | RUNTIME SELFMOD BIOS |
| `BIOSBK` | 88-105 | LABEL | INT 1Bh Ctrl-Break ISR: reads shift state 0:0417; if BKABLE, patches >NEXT into `JMP ABNORM` | HW BIOS SELFMOD |
| `DOSBK` | 107-112 | LABEL | INT 23h (DOS ^C) handler: swallows key via INT 16h, `RETF` with CF=0 | HW BIOS |
| `NEST` | 114-131 | LABEL | Colon-def entry (reached by `JMP NEST` in CFA): push ES:IP on RP, ES = rel-paragraph at CFA+3 + XSEG (immediate patched at NESTPATCH), IP=0 | RUNTIME SEG SELFMOD |
| `EXIT` | 134-140 | CODE | ( -- ) pop IP and ES from return stack | RUNTIME SEG |
| `?EXIT` | 142-146 | CODE | ( f -- ) EXIT if f non-zero (jumps into EXIT) | RUNTIME |
| `UNNEST` | 148-154 | CODE | ( -- ) same as EXIT; compiled by `;` | RUNTIME SEG |
| `DODOES` | 156-173 | LABEL | DOES> runtime: CALLed from child CFA, then CALL in parent; push ES:IP, ES = paragraph after the CALL (+XSEG, patched at DOESPATCH), IP=0; PFA left on data stack | RUNTIME SEG SELFMOD |
| `DOCONSTANT` | 177-180 | LABEL | CFA `JMP DOCONSTANT`: push cell at CFA+3 (W=AX) | RUNTIME |
| `DOVALUE` | 182-185 | LABEL | same as DOCONSTANT, for VALUEs (body is writable via !> etc.) | RUNTIME |
| `DOUSER-VARIABLE` | 187-191 | LABEL | CALLed: push UP + offset stored in body | RUNTIME |
| `(LIT)` | 193-194 | CODE | ( -- n ) push inline cell from ES:IP | RUNTIME SEG |
| `<'>` | 196-197 | CODE | ( -- n ) same as (LIT); distinct CFA so decompiler shows ['] | RUNTIME SEG |
| `DOBEGIN` | 278-279 | CODE | ( -- ) no-op marker compiled by control structures (for decompiler) | RUNTIME |
| `DOCASE` | 281-282 | CODE | ( -- ) no-op marker compiled by control structures (for decompiler) | RUNTIME |
| `DOENDCASE` | 284-285 | CODE | ( -- ) no-op marker compiled by control structures (for decompiler) | RUNTIME |
| `DOTHEN` | 287-288 | CODE | ( -- ) no-op marker compiled by control structures (for decompiler) | RUNTIME |
| `DOAGAIN` | 290-292 | CODE | ( -- ) unconditional branch: IP = inline offset in ES list segment | RUNTIME SEG |
| `DOREPEAT` | 294-296 | CODE | ( -- ) unconditional branch: IP = inline offset in ES list segment | RUNTIME SEG |
| `?WHILE` | 298-304 | CODE | ( f -- ) branch to inline offset if f = 0, else skip it | RUNTIME SEG |
| `?UNTIL` | 306-312 | CODE | ( f -- ) branch to inline offset if f = 0, else skip it | RUNTIME SEG |
| `BRANCH` | 314-316 | CODE | ( -- ) unconditional branch: IP = inline offset in ES list segment | RUNTIME SEG |
| `DOENDOF` | 318-320 | CODE | ( -- ) unconditional branch: IP = inline offset in ES list segment | RUNTIME SEG |
| `?BRANCH` | 322-328 | CODE | ( f -- ) branch to inline offset if f = 0, else skip it | RUNTIME SEG |
| `NEXT\|` | 330-337 | CODE | ( -- ) FOR..NEXT runtime: decrement RP top, branch back while no borrow | RUNTIME SEG |
| `UNDO` | 354-356 | CODE | ( -- ) drop the 3-cell DO frame from the return stack | RUNTIME |
| `(LOOP)` | 358-365 | CODE | ( -- ) increment biased index; loop until signed overflow | RUNTIME SEG |
| `(+LOOP)` | 367-374 | CODE | ( n -- ) add n to biased index; loop until overflow | RUNTIME SEG |
| `(DO)` | 376-386 | CODE | ( l i -- ) push 3-cell frame: leave-addr (inline), limit+8000h, index-(limit+8000h) | RUNTIME SEG |
| `(?DO)` | 388-403 | CODE | ( l i -- ) like (DO) but branches past loop if l = i | RUNTIME SEG |
| `(OF)` | 405-413 | CODE | ( n1 n2 -- n1 \| ) CASE OF: if unequal branch, else drop both | RUNTIME SEG |
| `BOUNDS` | 415-417 | CODE | ( a n -- a+n a ) | PERF |
| `EXECUTE` | 433-434 | CODE | ( cfa -- ) jump to CFA (machine code) | RUNTIME |
| `PERFORM` | 436-438 | CODE | ( a -- ) @ EXECUTE | RUNTIME |
| `GOTO` | 440-449 | CODE | ( -- ) tail-call: fetch inline CFA, EXIT, jump to it | RUNTIME SEG |
| `DODEFER` | 452-454 | LABEL | CALLed from DEFER CFA: jump through cell in body | RUNTIME |
| `EXEC:` | 456-465 | CODE | ( n -- ) execute n-th inline CFA, then EXIT the caller (jump table) | RUNTIME SEG |
| `DOUSER-DEFER` | 467-470 | LABEL | CALLed from USER DEFER CFA: jump through UP+offset cell | RUNTIME |
| `GO` | 472-473 | CODE | ( addr -- ) `RET` = jump to machine code at addr popped from data stack (SP is the 8086 stack) | RUNTIME |
| `NOOP` | 475-476 | CODE | ( -- ) does nothing | PRIM |
| `PAUSE` | 478-482 | CODE | ( -- ) 3 NOPs + NEXT; multitasker patches a `JMP (PAUSE)` over the NOPs (MULTASK.SEQ:199-201) | SELFMOD |
| `I` | 484-487 | CODE | ( -- n ) index = RP[0]+RP[1] (biased frame) | RUNTIME |
| `J` | 489-492 | CODE | ( -- n ) second loop index (RP+6, RP+8) | RUNTIME |
| `K` | 494-497 | CODE | ( -- n ) third loop index (RP+12, RP+14) | RUNTIME |
| `(LEAVE)` | 499-503 | CODE | ( -- ) IP = leave-addr from frame, drop frame | RUNTIME |
| `(?LEAVE)` | 505-513 | CODE | ( f -- ) (LEAVE) if f non-zero | RUNTIME |
| `@` | 518-520 | CODE | ( addr -- n ) Fetch a 16 bit value from addr | PRIM |
| `!` | 522-524 | CODE | ( n addr -- ) Store value n into the address addr | PRIM |
| `C@` | 526-529 | CODE | ( addr -- char ) Fetch an 8 bit value from addr.  Fill high part with zeros. | PRIM |
| `C!` | 531-534 | CODE | ( char addr -- ) Store the least significant 8 bits of char at the specified addr | PRIM |
| `CMOVE` | 553-572 | CODE | ( from to n -- ) ascending byte move (word-wise when dest aligned; NOT usable as fill, see comment KERNEL1:536-551) | PRIM |
| `CMOVE>` | 574-586 | CODE | ( from to n -- ) descending byte move | PRIM |
| `PLACE` | 588-609 | CODE | ( from n to -- ) store counted string | PERF |
| `+PLACE` | 611-635 | CODE | ( from n to -- ) append to counted string | PERF |
| `SP@` | 639-641 | CODE | ( -- n ) Push the address of the top element on the parameter stack (prior to push). | PRIM |
| `SP!` | 645-647 | CODE | ( n -- ) Set the parameter stack pointer to specified value. | PRIM |
| `RP@` | 649-652 | CODE | ( -- addr ) Push the address of the top element of the return stack | PRIM |
| `RP!` | 654-655 | CODE | ( n -- ) Set the return stack pointer to n . | PRIM |
| `DROP` | 657-658 | CODE | ( n1 -- ) | PRIM |
| `DUP` | 660-663 | CODE | ( n1 -- n1 n1 ) Duplicate the top element of the stack. | PRIM |
| `SWAP` | 665-668 | CODE | ( n1 n2 -- n2 n1 ) Exchange the top two items on the stack. | PRIM |
| `OVER` | 670-674 | CODE | ( n1 n2 -- n1 n2 n1 ) Push a copy of the second stack item. | PRIM |
| `PLUCK` | 676-680 | CODE | ( n1 n2 n3 --- n1 n2 n3 n1 ) Copy the third stack item to top | PERF |
| `TUCK` | 682-685 | CODE | ( n1 n2 -- n2 n1 n2 ) Tuck the first stack element under the second. | PERF |
| `NIP` | 687-689 | CODE | ( n1 n2 -- n2 ) Delete the second stack item. | PERF |
| `ROT` | 691-694 | CODE | ( n1 n2 n3 --- n2 n3 n1 ) Rotate top three stack values, bringing the third item to the top. | PERF |
| `-ROT` | 696-698 | CODE | ( n1 n2 n3 --- n3 n1 n2 ) Inverse of ROT | PERF |
| `FLIP` | 700-702 | CODE | ( n1 -- n2 ) Exchange the high and low halves of a word | PERF |
| `SPLIT` | 704-710 | CODE | ( n1 --- n2 n3 ) Splits n1 into two bytes, low, high | PERF |
| `JOIN` | 713-717 | CODE | ( n1 n2 -- n3 ) Join bytes into one word, n2 = hi | PERF |
| `?DUP` | 719-725 | CODE | ( n1 -- [n1] n1 ) duplicate n1 if <> 0 | PERF |
| `?DROP` | 728-734 | CODE | ( n f -- n f \| f ) drop n if f = 0 | PERF |
| `R>` | 736-740 | CODE | ( -- n ) Pop an item from the return stack and push onto parameter stack. | PRIM |
| `R>DROP` | 742-744 | CODE | ( --- ) Drop an item from the return stack | PERF |
| `DUP>R` | 746-751 | CODE | ( n1 --- n1 ) Pushes a copy of the top item on parameter stack to the return stack. | PERF |
| `>R` | 753-757 | CODE | ( n -- ) Pop top of parameter stack and push value onto return stack. | PRIM |
| `2R>` | 759-764 | CODE | ( -- n1 n2 ) Pop two items from return stack onto parameter stack | PERF |
| `2>R` | 766-771 | CODE | ( n1 n2 -- ) Pop two items from parameter stack, push onto return stack. | PERF |
| `R@` | 773-776 | CODE | ( -- n ) Push a copy of top item on return stack onto parameter stack. | PRIM |
| `2R@` | 778-782 | CODE | ( -- n1 n2 ) Push a copy of the top two items on the return stack onto the parameter stack. | PERF |
| `PICK` | 784-788 | CODE | ( nm ... n2 n1 k -- nm ... n2 n1 nk ) Push a copy of the n-th item on paramter stack. | PERF |
| `RPICK` | 790-795 | CODE | ( k -- nk ) Pick a copy of the k-th item on the return stack and push onto the | PERF |
| `AND` | 797-800 | CODE | ( n1 n2 -- n3 ) Perform bit-wise logical AND of top two items. | PRIM |
| `OR` | 802-805 | CODE | ( n1 n2 -- n3 ) Perform bit-wise logical OR of top two items on parameter stack. | PRIM |
| `XOR` | 807-810 | CODE | ( n1 n2 -- n3 ) Perform bit-wise logical Exclusive OR of top two stack items. | PRIM |
| `NOT` | 812-814 | CODE | ( n -- n' ) Logically invert the bits of top stack item. | PERF |
| `CSET` | 819-822 | CODE | ( b addr -- ) Logical OR of l.s. 8 bits of "b" with byte at "addr". | PERF |
| `CRESET` | 824-828 | CODE | ( b addr -- ) Clear bits in byte at addr corresponding to "1" bits in b . | PERF |
| `CTOGGLE` | 830-833 | CODE | ( b addr -- ) Toggle bits in byte at addr corresponding to "1" bits in b . | PERF |
| `ON` | 835-837 | CODE | ( addr -- ) Set word at addr to "true" | PERF |
| `OFF` | 839-841 | CODE | ( addr -- ) Clear all bits of word at addr. | PERF |
| `-1!` | 843-845 | CODE | ( addr -- ) Same as ON | PERF |
| `0!` | 847-849 | CODE | ( addr -- ) Same as OFF | PERF |
| `INCR` | 851-853 | CODE | ( addr --- ) Increment word at addr. | PERF |
| `DECR` | 855-857 | CODE | ( addr --- ) Decrement word at addr. | PERF |
| `0DECR` | 859-864 | CODE | ( addr -- ) Decrement to zero only, not below | PERF |
| `+` | 866-868 | CODE | ( n1 n2 -- sum ) Add top two elements | PRIM |
| `NEGATE` | 870-872 | CODE | ( n -- n' ) Arithmetically negate top stack element. | PERF |
| `-` | 874-876 | CODE | ( n1 n2 -- n1-n2 ) Subtract top stack element from second | PRIM |
| `ABS` | 878-884 | CODE | ( n1 -- n2 ) Return absolute value of top stack item | PERF |
| `D+!` | 886-890 | CODE | ( d addr -- ) Add double number "d" to double value at "addr" | PERF |
| `+!` | 892-894 | CODE | ( n addr -- ) Add "n" to word at "addr" | PERF |
| `C+!` | 896-898 | CODE | ( n addr -- ) Add "n" to byte at "addr" | PERF |
| `PC@` | 905-908 | CODE | ( port# -- n ) Read 8-bit port at "port#" and push value on stack. | HW |
| `P@` | 910-913 | CODE | ( port# -- n ) Read 16-bit value at "port#" and push value on stack. | HW |
| `PC!` | 915-918 | CODE | ( n port# -- ) Write 8 bit value "n" to "port#". | HW |
| `P!` | 920-923 | CODE | ( n port# -- ) Write 16 bit value "n" to "port#". | HW |
| `PDOS` | 925-936 | CODE | ( addr drive -- f ) INT 21h/47h get current directory | DOS |
| `2*` | 940-942 | CODE | ( n -- 2*n ) Logical left shift n by 1 position. | PRIM |
| `2/` | 944-946 | CODE | ( n -- n/2 ) Arithmetic right shift of n by 1 position | PRIM |
| `U2/` | 948-950 | CODE | ( u -- u/2 ) Logical right shift of n by 1 position | PRIM |
| `U16/` | 952-956 | CODE | ( u -- u/16 ) Logical shift right by 4 bit positions. | PERF |
| `U8/` | 958-963 | CODE | ( u -- u/8 ) Logical shift right by 3 bit positions. | PERF |
| `8*` | 965-968 | CODE | ( n -- 8*n ) Logical shift left by 3 positions. | PERF |
| `1+` | 970-972 | CODE | ( n1 --- n2 ) Add 1 to top stack element | PERF |
| `2+` | 974-976 | CODE | ( n1 --- n2 ) Add 2 to top stack element | PERF |
| `1-` | 978-980 | CODE | ( n1 --- n2 ) Subtract 1 from top stack element | PERF |
| `2-` | 982-984 | CODE | ( n1 --- n2 ) Subtract 2 from top stack element | PERF |
| `UM*` | 986-989 | CODE | ( n1 n2 -- d ) Form a 32 bit product from two 16 bit unsigned numbers | PRIM |
| `*` | 991-994 | CODE | ( n1 n2 -- n3 ) Form a 16 bit product from two 16 bit numbers | PERF |
| `UM/MOD` | 1000-1009 | CODE | ( ud u -- rem quot ) returns -1,-1 on overflow instead of trapping | PRIM |
| `0=` | 1011-1013 | CODE | ( n -- f ) Return TRUE if n is zero.  Otherwise FALSE. | PRIM |
| `0<` | 1015-1018 | CODE | ( n -- f ) If n is negative, return TRUE.  Otherwise FALSE. | PRIM |
| `0>` | 1020-1028 | CODE | ( n -- f ) If n is greater than 0, return TRUE.  Otherwise FALSE. | PERF |
| `0<>` | 1030-1033 | CODE | ( n -- f ) If n is not equal to 0, return TRUE.  Otherwise FALSE. | PERF |
| `=` | 1035-1039 | CODE | ( n1 n2 -- f ) If n1 is equal to n2, return TRUE.  Otherwise FALSE. | PERF |
| `<>` | 1041-1045 | CODE | ( n1 n2 -- f ) If n1 is not equal to n2, return TRUE.  Otherwise FALSE. | PERF |
| `U<` | 1050-1054 | CODE | ( n1 n2 -- f ) If unsigned n1 is less than unsigned n2, return TRUE, otherwise FALSE. | PRIM |
| `U>` | 1056-1060 | CODE | ( n1 n2 -- f ) If unsigned n1 is greater than unsigned n2, return TRUE, otherwise FALSE. | PERF |
| `<` | 1062-1069 | CODE | ( n1 n2 -- f ) If signed n1 is less than signed n2, return TRUE, otherwise return FALSE. | PERF |
| `>` | 1071-1078 | CODE | ( n1 n2 -- f ) If signed n1 is greater than signed n2, return TRUE, otherwise FALSE. | PERF |
| `UMIN` | 1080-1087 | CODE | ( n1 n2 -- n3 ) Return smaller of n1 or n2, treated as unsigned numbers. | PERF |
| `MIN` | 1089-1096 | CODE | ( n1 n2 -- n3 ) Return smaller of n1 or n2, treated as signed numbers. | PERF |
| `MAX` | 1098-1105 | CODE | ( n1 n2 -- n3 ) Return larger of n1 or n2, treated as signed numbers. | PERF |
| `0MAX` | 1107-1116 | CODE | ( n1 -- n3 ) Return larger of n1 or ZERO, treated as signed numbers. | PERF |
| `UMAX` | 1118-1124 | CODE | ( n1 n2 -- n3 ) Return larger of n1 or n2, treated as unsigned numbers. | PERF |
| `WITHIN` | 1126-1135 | CODE | ( n lo hi -- flag ) Returns TRUE if  lo <= n < hi .  Signed comparison | PERF |
| `BETWEEN` | 1137-1146 | CODE | ( n lo hi -- flag ) Returns TRUE if  lo <= n <= hi . Signed comparison | PERF |
| `UBETWEEN` | 1148-1158 | CODE | ( n lo hi -- flag ) Returns TRUE if  lo u<= n u<= hi . UNsigned comparison | PERF |
| `2@` | 1160-1164 | CODE | ( addr -- d ) Fetch a 32 bit value from addr | PERF |
| `2!` | 1166-1168 | CODE | ( d addr -- ) Store a 32 bit value into addr | PERF |
| `2DROP` | 1170-1172 | CODE | ( d -- ) Drop two 16 bit values from stack | PERF |
| `3DROP` | 1174-1176 | CODE | ( n1 n2 n3 -- ) Drop 3 items from the stack. | PERF |
| `2DUP` | 1178-1182 | CODE | ( d -- d d ) Duplicate two top items on stack. | PERF |
| `3DUP` | 1184-1190 | CODE | ( n1 n2 n3 -- n1 n2 n3 n1 n2 n3 ) Duplicate top 3 items on stack. | PERF |
| `2SWAP` | 1192-1197 | CODE | ( d1 d2 -- d2 d1 ) Exchange top two pairs of numbers on stack. | PERF |
| `2OVER` | 1199-1204 | CODE | ( d2 d2 -- d1 d2 d1 ) Copy second pair of numbers over top pair of numbers on stack. | PERF |
| `D+` | 1206-1210 | CODE | ( d1 d2 -- dsum ) Add top two double numbers on stack | PERF |
| `DNEGATE` | 1212-1219 | CODE | ( d# -- d#' ) Negate double number on top of stack. | PERF |
| `S>D` | 1221-1224 | CODE | ( n -- d ) Convert single signed number to signed double | PERF |
| `DABS` | 1226-1238 | CODE | ( d1 -- d2 ) Replace the top double number with its absolute value. | PERF |
| `D2*` | 1240-1243 | CODE | ( d -- d*2 ) 32 bit left shift | PERF |
| `D2/` | 1245-1248 | CODE | ( d -- d/2 ) 32 bit arithmetic right shift | PERF |
| `UD16/` | 1250-1256 | CODE | ( d -- d/16 ) 32 bit UNSIGNED right shift 4 bits | PERF |
| `DU<` | 1274-1279 | CODE | ( ud1 ud2 -- Flag ) Unsigned compare double numbers.  If ud1 < ud2, return TRUE.  Else FALSE. | PERF |
| `*D` | 1303-1307 | CODE | ( n1 n2 -- d# ) Obtain the 32 bit signed product of two 16 bit numbers. | PERF |
| `/` | 1325-1335 | CODE | ( n d -- q ) floored signed division (IDIV + floor fix-up) | PERF |
| `/MOD` | 1337-1348 | CODE | ( n d -- r q ) floored | PERF |
| `*/MOD` | 1355-1367 | CODE | ( a b c -- r q ) 32-bit intermediate, floored | PERF |

#### KERNEL2.SEQ (52 entries)

| Word | Line(s) | Kind | Semantics | Class |
|---|---|---|---|---|
| `FILL` | 72-78 | CODE | ( a n c -- ) REP STOSB (temporarily ES=DS) | PRIM |
| `LFILL` | 80-86 | CODE | ( seg a n c -- ) fill in far segment | SEG |
| `COUNT` | 93-97 | CODE | ( addr -- addr+1 len ) Convert from the address of a counted string to an address and count. | PRIM |
| `LENGTH` | 99-104 | CODE | ( addr -- addr+2 len ) really word count | PERF |
| `COUNTL` | 107-114 | CODE | ( seg addr -- seg addr+1 len ) Like COUNT, but works with a LONG (seg/offset) address. | SEG |
| `UPC` | 157-163 | CODE | ( c -- C ) XLAT through ATBL (also folds >127 to ASCII) | PERF |
| `UPPER` | 165-184 | CODE | ( a n -- ) uppercase in place via ATBL | PERF |
| `?UPPERCASE` | 186-213 | CODE | ( a -- a ) uppercase counted string if CAPS on | PERF |
| `HERE` | 215-220 | CODE | ( -- a ) DP in user area | PERF |
| `PAD` | 222-227 | CODE | ( -- a ) HERE + 80 | PERF |
| `-TRAILING` | 229-248 | CODE | ( addr len -- addr len1 ) The length of string is conditionally reduced by the number of trailing | PERF |
| `COMP` | 250-267 | CODE | ( a1 a2 n -- -1\|0\|1 ) REPZ CMPSB; a2 is read via ES=SSEG | PERF SEG |
| `CAPS-COMP` | 269-289 | CODE | ( a1 a2 n -- -1\|0\|1 ) case-insensitive (OR 20h); a2 via SSEG | PERF SEG |
| `?CS:` | 295-297 | CODE | ( -- cs ) | SEG |
| `?DS:` | 299-301 | CODE | ( -- ds ) | SEG |
| `?ES:` | 303-305 | CODE | ( -- es ) current list segment of running definition | SEG |
| `@L` | 307-311 | CODE | ( seg addr -- word ) Load a 16 bit word from the specified segment and offset. | SEG |
| `C@L` | 313-317 | CODE | ( seg addr -- byte ) Load an 8 bit byte from the specified segment and offset. | SEG |
| `C!L` | 319-323 | CODE | ( byte seg adr ) Store the byte at the specified segment and offset. | SEG |
| `!L` | 325-329 | CODE | ( n seg adr -- ) Store the 16 bit word n at the specified segment and offset. | SEG |
| `<BDOS>` | 331-336 | CODE | ( dx fn -- al ) generic INT 21h (AH=fn, DX=n); default of DEFER BDOS | DOS |
| `BDOS2` | 341-346 | CODE | ( cx dx al -- cx dx ax ) INT 21h with CX | DOS |
| `BIOSKEY?` | 353-373 | CODE | ( -- f ) INT 16h/01; discards Ctrl-Break 0000 keys | BIOS |
| `BIOSKEY` | 375-385 | CODE | ( -- key ) INT 16h/00 (scan<<8\|ascii) | BIOS |
| `CMOVEL` | 412-431 | CODE | ( sseg soff dseg doff n -- ) far ascending move | SEG |
| `CMOVEL>` | 433-448 | CODE | ( sseg soff dseg doff n -- ) far descending move | SEG |
| `DOS_DEALLOC` | 504-515 | CODE | ( seg -- err ) INT 21h/49h | DOS SEG |
| `DOS_ALLOC` | 517-530 | CODE | ( paras -- max seg err ) INT 21h/48h | DOS |
| `DOS_SETBLOCK` | 532-545 | CODE | ( seg paras -- err ) INT 21h/4Ah | DOS SEG |
| `+XSEG` | 562-565 | CODE | ( relpara -- abspara ) add list-space base XSEG | SEG |
| `YHERE` | 588-591 | CODE | ( -- yoff ) head-space DP | PERF |
| `YS:` | 593-596 | CODE | ( off -- yseg off ) | SEG |
| `Y@` | 598-604 | CODE | ( addr -- n ) head-space (YSEG) access: ( a -- n ) | SEG |
| `Y!` | 606-612 | CODE | ( n addr -- ) head-space (YSEG) access: ( n a -- ) | SEG |
| `YC@` | 614-620 | CODE | ( addr -- char ) head-space (YSEG) access: ( a -- c ) | SEG |
| `YC!` | 622-628 | CODE | ( char addr -- ) head-space (YSEG) access: ( c a -- ) | SEG |
| `Y,` | 630-639 | CODE | ( n -- ) head-space (YSEG) access: ( n -- ) append cell | SEG |
| `YCSET` | 641-647 | CODE | ( byte addr -- ) head-space (YSEG) access: ( b a -- ) OR byte | SEG |
| `YHASH` | 649-667 | CODE | ( ystr voc -- thread ) vocabulary hash of a name stored in head segment | SEG |
| `XHERE` | 669-672 | CODE | ( -- seg off ) list-space DP | SEG |
| `X,` | 674-683 | CODE | ( n -- ) append cell to list space (XDPSEG:XDP) | SEG |
| `XC,` | 685-694 | CODE | ( c -- ) append byte to list space | SEG |
| `PR-STATUS` | 696-705 | CODE | ( prn -- status ) INT 17h/02 | BIOS |
| `TIB` | 861-863 | CODE | ( -- addr ) Leaves address of text input buffer. | PERF |
| `MORE?` | 866-870 | CODE | ( -- f ) >IN < #TIB | PERF |
| `DIGIT` | 913-931 | CODE | ( char base -- n f ) If the character is equivalent to a digit in the specified base, | PERF |
| `%NUMBER` | 1014-1049 | CODE | ( a -- d f ) dispatch on $ ' ^ h b prefixes/suffixes by `JMP` into the CFA of DEFERed $NUM/'NUM/^NUM/NUMH/NUMB/#NUM | PERF RUNTIME |
| `SKIP` | 1134-1147 | CODE | ( a n c -- a' n' ) REPZ SCASB with ES=SSEG | PERF SEG |
| `SCAN` | 1149-1162 | CODE | ( a n c -- a' n' ) REPNZ SCASB with ES=SSEG | PERF SEG |
| `/STRING` | 1164-1177 | CODE | ( addr len n -- addr' len' ) Index into the string by n.  Returns addr+n and len-n. | PERF |
| `SOURCE` | 1179-1184 | CODE | ( -- a n ) 'TIB #TIB | PERF |
| `WORD` | 1191-1248 | CODE | ( c -- here ) parse from TIB to counted string at HERE (+ trailing blank); sets >IN, >IN_WORD | PERF |

#### KERNEL3.SEQ (14 entries)

The 9 `;USES` CFA generators (669-778) are listed in 2c.

| Word | Line(s) | Kind | Semantics | Class |
|---|---|---|---|---|
| `TRAVERSE` | 16-26 | CODE | ( a dir -- a' ) step through name bytes in head segment until high bit set | SEG |
| `DONE?` | 35-46 | CODE | ( state -- f ) f = STATE<>n OR END?; clears END? | PERF |
| `CNSRCH` | 54-87 | CODE | ( cfa ya maxya -- nfa f ) linear scan of head segment for header whose CFA pointer = cfa (>NAME) | SEG |
| `HASH` | 137-146 | CODE | ( str voc -- thread ) ((c1*2+c2)*2+len) & (#THREADS-1) | PERF |
| `(FIND)` | 148-195 | CODE | ( str alf -- cfa 1\|-1 \| str 0 ) walk one hash chain in head segment (ES=YSEG); immediate bit 40h | SEG |
| `DROP.CONTEXT.I2*+@DUP` | 199-209 | CODE | ( a1 -- n1 ) headerless helper for %%FIND: reads DO index from RP frame, CONTEXT[i] | PERF RUNTIME |
| `PRIOR.CHECK` | 212-224 | CODE | ( n -- n f ) DUP PRIOR @ OVER PRIOR ! = | PERF |
| `OVER.SWAP.HASH.@` | 226-240 | CODE | ( str voc -- str thread@ ) inline HASH and fetch | PERF |
| `%FIND` | 254-269 | CODE | ( a -- cfa f \| a 0 ) set up stack and `JMP` to CFA of high-level %%FIND; empty string sets END? and returns NOOP | PERF RUNTIME |
| `SKIP'C'` | 273-282 | CODE | ( a -- a ) if token is 'x' form, `ADD SI,2` = skip the next compiled word in the CALLER's thread (used by DEFINED to skip ?UPPERCASE) | RUNTIME |
| `(?STACK)` | 300-322 | CODE | ( -- ) check SP vs SP0 and HERE+80/+280; `JMP` into CFA of STACKUNDER/STACKOVER/WARNOVER | PERF RUNTIME |
| `,` | 341-349 | CODE | ( n -- ) store at HERE, DP+=2 | PERF |
| `C,` | 351-359 | CODE | ( c -- ) store byte at HERE, DP+=1 | PERF |
| `SPCHECK` | 608-627 | CODE | ( -- ) abort if head space or list space (XDPSEG-XSEG) within 6 paragraphs of limit | PERF SEG |

#### KERNEL4.SEQ (9 native defs + 3 in-colon labels)

The `LABEL ... ` entries marked "in colon def" are `[ LABEL x ]` markers that name the body address of a colon definition. The 2 USER `;USES` generators (28, 36) are listed in 2c.

| Word | Line(s) | Kind | Semantics | Class |
|---|---|---|---|---|
| `WARMBODY` | 108 | LABEL (in colon def) | `[ LABEL WARMBODY ]` marks body of WARM; WORIG jumps to `WARMBODY 5 -` = its CFA (assumes 5-byte colon CFA) | RUNTIME |
| `COLDBODY` | 114 | LABEL (in colon def) | marks body of COLD; CORIG computes ES from the paragraph cell at `COLDBODY 2-` | RUNTIME |
| `RESTORE_VECTORS` | 147-164 | CODE | ( -- ) restore INT 1Bh and INT 0 vectors and DOS break flag (INT 21h/25xx, 3301) | DOS HW SEG |
| `DIV0BODY` | 188 | LABEL (in colon def) | marks body of DIVIDE0; DIV0BK jumps to `DIV0BODY 5 -` | RUNTIME |
| `DIV0BK` | 191-204 | LABEL | INT 0 (divide overflow) ISR: STI, push regs, jump into CFA of DIVIDE0 (never returns) | HW RUNTIME |
| `SETBRK` | 209-234 | LABEL | subroutine: if RESTNEXT, rewrite >NEXT bytes; hook INT 1Bh->BIOSBK, 23h->DOSBK, 0->DIV0BK; disable DOS break | HW DOS SELFMOD |
| `SAVEVECTORS` | 236-253 | LABEL | subroutine: save INT 1Bh/INT 0 vectors and DOS break flag | HW DOS SEG |
| `SET_VECTORS` | 255-259 | CODE | ( -- ) CALL SETBRK | HW |
| `@REL>ABS` | 261-267 | CODE | ( cfa -- target ) decode rel16 of the JMP/CALL in a code field: cfa + 3 + [cfa+1] | RUNTIME |
| `WORIG` | 271-277 | LABEL | warm entry stub (ORIGIN+3 patched to here): `JMP WARMBODY-5` | RUNTIME |
| `CORIG` | 279-366 | LABEL | cold entry: DS=SS=CS; copy heads to YSEG and list space to XSEG (if not already moved); patch NESTPATCH/DOESPATCH immediates with XSEG; save+set vectors; compute LIMIT/FIRST (patching CONSTANT bodies), RP0/SP0/TIB; ES:IP = COLD body; NEXT | RUNTIME SEG SELFMOD |
| `SET-CURSOR` | 466-473 | CODE | ( shape -- ) INT 10h/01 | BIOS |

### 2a'. Other kernel files loaded by META86.SEQ

FPATH.SEQ and DEFAULT.SEQ contain no native code.

#### VIDEO.SEQ (3 entries)

| Word | Line(s) | Kind | Semantics | Class |
|---|---|---|---|---|
| `?VMODE` | 19-39 | CODE | ( -- mode ) INT 10h/0Fh; reads cols/rows/cursor from BIOS data area 40:4A, 40:84, 40:60 into COLS/ROWS/CROWS | BIOS |
| `BLANK.COLOR` | 50-65 | CODE | ( -- ) on CGA: wait for retrace (port 3DAh) and disable video (3D8h) | HW |
| `SHOW.COLOR` | 67-77 | CODE | ( -- ) re-enable CGA video (port 3D8h <- 2Dh) | HW |

#### VIDEO2.SEQ (2 entries)

| Word | Line(s) | Kind | Semantics | Class |
|---|---|---|---|---|
| `VIDEO-TYPEL` | 16-137 | CODE | ( seg a n -- ) clip to ROWS/COLS, set cursor (INT 10h/02), optional CGA snow blanking (ports 3DAh/3D8h), computed `JMP CX` into a 132x unrolled LODSB/STOSW block writing char+ATTRIB to VIDEO-SEG | VIDEO HW BIOS SEG |
| `VIDEO-TYPE` | 139-141 | CODE | ( a n -- ) push CS and tail-jump into VIDEO-TYPEL CFA | VIDEO |

#### EXPAND.SEQ (10 entries)

| Word | Line(s) | Kind | Semantics | Class |
|---|---|---|---|---|
| `EMM-PRESENT?` | 30-50 | CODE | ( -- f ) INT 21h/3567 get INT 67h vector, compare driver name "EMMXXXX0" at ES:000A | DOS SEG |
| `EMM-STATUS?` | 54-60 | CODE | ( -- ) INT 67h/40 status -> EMM-STATUS (EMS driver) | BIOS |
| `EMM-PAGE-FRAME` | 69-77 | CODE | ( -- ) INT 67h/41 -> PAGE-FRAME (EMS driver) | BIOS |
| `EMM-AVAIL-PAGES` | 82-91 | CODE | ( -- n ) INT 67h/42 free pages (EMS driver) | BIOS |
| `EMM-TOTAL-PAGES` | 96-104 | CODE | ( -- n ) INT 67h/42 total pages (EMS driver) | BIOS |
| `EMM-ALLOC-PAGES` | 109-118 | CODE | ( n -- h ) INT 67h/43 (EMS driver) | BIOS |
| `EMM-MAP-PAGES` | 124-133 | CODE | ( log phys h -- ) INT 67h/44 (EMS driver) | BIOS |
| `EMM-DEALLOC-PAGES` | 140-148 | CODE | ( h -- ) INT 67h/45 (EMS driver) | BIOS |
| `EMM-GET-VERSION` | 153-163 | CODE | ( -- v ) INT 67h/46 (EMS driver) | BIOS |
| `?EMM:` | 167-169 | CODE | ( -- seg ) PAGE-FRAME @ | PERF |

#### EMMEXEC.SEQ (4 entries)

| Word | Line(s) | Kind | Semantics | Class |
|---|---|---|---|---|
| `SMALFPC` | 62-68 | LABEL | subroutine: shrink own memory block (INT 21h/4Ah) to EMMPARS before EXEC | DOS SEG |
| `EMM>FPC` | 75-116 | LABEL | subroutine: regrow block, copy program image back from EMS pages (INT 67h/44, REP MOVSW), free EMS | DOS BIOS SEG |
| `DSK>FPC` | 120-156 | LABEL | subroutine: regrow block, read program image back from swap file FPCIMAGE.$$$ in 16K chunks, close+delete | DOS SEG |
| `<EXTEXEC>` | 158-205 | CODE | ( -- rc ) swap own image out, switch SS:SP to tiny stack, save ES/SI/BP, INT 21h/4B00 EXEC COMMAND.COM, restore image and all VM registers | DOS SEG RUNTIME |

#### POINTER.SEQ (1 entry)

| Word | Line(s) | Kind | Semantics | Class |
|---|---|---|---|---|
| `PTR_OK?EXIT` | 13-24 | CODE | ( a -- a \| seg ) used after DOES> in POINTER: if @a<>0 push it and EXIT the calling DOES> body (pops IP/ES) | RUNTIME SEG |

#### EQUCOLON.SEQ (10 entries)

| Word | Line(s) | Kind | Semantics | Class |
|---|---|---|---|---|
| `%@>` | 13-18 | CODE | ( --- n1 ) Fetch from the BODY field of following definition. | RUNTIME SEG |
| `%!>` | 20-25 | CODE | ( n1 --- ) Store to BODY field of following definition. | RUNTIME SEG |
| `%U@>` | 27-34 | CODE | ( --- n1 ) Fetch the DATA field of a USER definition in user space. | RUNTIME SEG |
| `%U!>` | 36-43 | CODE | ( n1 --- ) Store into the DATA field of a USER definition in user space. | RUNTIME SEG |
| `%+!>` | 45-51 | CODE | ( n1 --- ) Increment the BODY field of the following definition by n1. | RUNTIME SEG |
| `%INCR>` | 53-58 | CODE | ( --- ) Increment the BODY field of the following definition by one. | RUNTIME SEG |
| `%DECR>` | 60-65 | CODE | ( --- ) Decrement the BODY field of the following definition by one. | RUNTIME SEG |
| `%OFF>` | 67-72 | CODE | ( --- ) Clear to zero the BODY field of following definition. | RUNTIME SEG |
| `%ON>` | 74-79 | CODE | ( --- ) Set to -1 the BODY field of following definition. | RUNTIME SEG |
| `%&>` | 81-85 | CODE | ( -- a1 ) Return the BODY a1 address of the following word in a definition. | RUNTIME SEG |

#### SAVEREST.SEQ (6 entries)

| Word | Line(s) | Kind | Semantics | Class |
|---|---|---|---|---|
| `%SAVE!>` | 34-44 | CODE | ( n1 --- ) Save the BODY contents of the following definition on the return stack | RUNTIME SEG |
| `%SAVE>` | 46-53 | CODE | ( --- ) Save the BODY contents of the following definition on the return stack. | RUNTIME SEG |
| `%RESTORE>` | 55-63 | CODE | ( --- ) Restore the BODY of the following definition from the return stack. | RUNTIME SEG |
| `%USAVE!>` | 65-76 | CODE | ( n1 --- ) Save the USER data area for the following definition on the return | RUNTIME SEG |
| `%USAVE>` | 78-88 | CODE | ( --- ) Save the USER data area for the following definition on the return | RUNTIME SEG |
| `%URESTORE>` | 90-101 | CODE | ( --- ) Restore the USER data area of the following definition from the | RUNTIME SEG |

#### HANDLES.SEQ (10 entries)

| Word | Line(s) | Kind | Semantics | Class |
|---|---|---|---|---|
| `HDOS1` | 99-112 | CODE | ( cx dx ax -- ax cf ) INT 21h | DOS |
| `HDOS3` | 114-132 | CODE | ( bx cx dx ds ax -- ax cf ) INT 21h with caller-supplied DS (far buffers) | DOS SEG |
| `HDOS4` | 134-148 | CODE | ( bx cx dx ax -- ax cf ) INT 21h | DOS |
| `MOVEPOINTER` | 150-159 | CODE | ( d handle -- ) LSEEK from start (INT 21h/4200), handle via HCB+HNDLOFFSET | DOS |
| `ENDFILE` | 161-175 | CODE | ( handle -- d ) LSEEK to end (INT 21h/4202) | DOS |
| `<HRENAME>` | 187-205 | CODE | ( hcb1 hcb2 -- ax f ) rename (INT 21h/56h), ES=DS | DOS SEG |
| `FIND-FIRST` | 306-315 | CODE | ( asciiz attr -- f ) INT 21h/4Eh | DOS |
| `FIND-NEXT` | 317-324 | CODE | ( -- f ) INT 21h/4Fh | DOS |
| `DTA@` | 331-338 | CODE | ( -- seg off ) INT 21h/2Fh | DOS SEG |
| `DTA!` | 340-347 | CODE | ( seg off -- ) INT 21h/1Ah | DOS SEG |

#### SEQREAD.SEQ (7 entries)

| Word | Line(s) | Kind | Semantics | Class |
|---|---|---|---|---|
| `CURPOINTER` | 90-101 | CODE | ( handle -- d ) LSEEK relative 0 (INT 21h/4201) | DOS |
| `GET_ALINE` | 106-145 | CODE | ( -- a ) scan INBSEG buffer for line delimiter, copy line to OUTBUF (counted), advance INSTART/INLENGTH, bump LOADLINE and 32-bit FILEPOINTER | PERF SEG |
| `?FILLBUFF` | 164-170 | CODE | ( -- ) if INLENGTH <= OBLEN `JMP` to CFA of FILLBUFF | PERF RUNTIME |
| `CRLF>BL'S` | 298-308 | CODE | ( a -- a ) replace trailing CR LF of counted string by blanks | PERF |
| `SETTIB` | 310-319 | CODE | ( a -- ) 'TIB = a+1, #TIB = count, >IN = 0 | PERF |
| `?.LOADLINE` | 327-333 | CODE | ( a -- a ) unless LISTVAR=-1, `JMP` to CFA of DEFER .LOADLINE | PERF RUNTIME |
| `LENGTH.CHECK` | 335-346 | CODE | ( a -- a f ) f = INLENGTH<>0 or count<>0 | PERF |

#### HCRITICA.SEQ (2 entries)

| Word | Line(s) | Kind | Semantics | Class |
|---|---|---|---|---|
| `CRITINT` | 61-67 | LABEL | INT 24h critical-error ISR: increments HADCRITICAL, returns AL=0 (ignore), `IRET` | HW |
| `SETCRITICAL` | 69-75 | CODE | ( -- ) install CRITINT via INT 21h/2524 | DOS HW |

### 2b. Files FLOADed by SRC/F-PC.SEQ

PASM.SEQ is the assembler. It defines CODE, LABEL, END-CODE and the NEXT macros (see 2c) but contains no native definitions of its own. In SEDCODE.SEQ and MOUSE.SEQ, `int 33` and `int 51` are decimal: INT 21h and INT 33h.

#### NEWLAB.SEQ (1 entry)

| Word | Line(s) | Kind | Semantics | Class |
|---|---|---|---|---|
| `+field (child code)` | 87-92 | ;CODE | `+field` ( n1 -- n1+off ) DOES>-style child: adds the byte offset stored in its body (assembler-vocabulary helper for local labels) | PERF |

#### LOADEXE.SEQ (1 entry)

| Word | Line(s) | Kind | Semantics | Class |
|---|---|---|---|---|
| `SEXE` | 15-93 | LABEL | .EXE entry point (header CS:IP): relocate head space (backwards) to YSEG and list space paragraph-by-paragraph to XSEG, then `JMP FAR [SUVEC]` to ORIGIN | RUNTIME SEG |

#### DBGFIX.SEQ (1 entry)

| Word | Line(s) | Kind | Semantics | Class |
|---|---|---|---|---|
| `fixinline` | 33-40 | CODE | ( a -- a ) overwrite an inline NEXT (26 AD FF E0) at a with `JMP >NEXT` (E9 rel16) | SELFMOD |

#### DEBUG.SEQ (5 entries)

| Word | Line(s) | Kind | Semantics | Class |
|---|---|---|---|---|
| `FNEXT` | 117-122 | LABEL | subroutine: restore >NEXT to `ES: LODSW / JMP AX` | SELFMOD |
| `DNEXT` | 124-126 | LABEL | copy of the normal NEXT used while trapped | RUNTIME SEG |
| `DEBNEXT` | 128-143 | LABEL | patched-in NEXT: when ES:IP reaches breakpoint (DBSEG:DBOFF) twice, restore >NEXT and jump to 'DEBUG with IP pushed | RUNTIME SEG |
| `PNEXT` | 145-150 | CODE | ( -- ) patch >NEXT to `JMP DEBNEXT` (installs single-step trap) | SELFMOD |
| `UNBUG` | 156-158 | CODE | ( -- ) CALL FNEXT | SELFMOD |

#### SEARCH.SEQ (1 entry)

| Word | Line(s) | Kind | Semantics | Class |
|---|---|---|---|---|
| `SEARCH` | 28-74 | CODE | ( sa sl da dl -- off f ) substring search, optional CAPS folding via ATBL XLAT; dest via SSEG; borrows BP (RP) as index | PERF SEG |

#### LARGEST.SEQ (1 entry)

| Word | Line(s) | Kind | Semantics | Class |
|---|---|---|---|---|
| `largest` | 3-14 | CODE | ( a n -- a' max ) find largest cell (used to walk vocabulary threads) | PERF |

#### IBMCURSR.SEQ (5 entries)

| Word | Line(s) | Kind | Semantics | Class |
|---|---|---|---|---|
| `IBM-AT` | 5-14 | CODE | ( col row -- ) INT 10h/02 set cursor | BIOS |
| `IBM-AT?` | 35-44 | CODE | ( -- col row ) INT 10h/03 | BIOS |
| `IBM-DARK` | 46-53 | CODE | ( -- ) INT 10h/0F then re-set mode (clears screen) | BIOS |
| `LFILLW` | 55-63 | CODE | ( seg a nbytes w -- ) far REP STOSW (used to clear VIDEO-SEG) | SEG VIDEO |
| `IBM--LINE` | 83-99 | CODE | ( -- ) INT 10h/06 scroll up region below cursor (clear to end of screen) | BIOS |

#### BLINKER.SEQ (4 entries)

| Word | Line(s) | Kind | Semantics | Class |
|---|---|---|---|---|
| `blink!` | 13-18 | CODE | ( f -- ) INT 10h/1003 | BIOS |
| `pal!` | 26-34 | CODE | ( color reg -- ) INT 10h/1000 | BIOS |
| `border!` | 36-41 | CODE | ( n -- ) INT 10h/1001 | BIOS |
| `border@` | 43-49 | CODE | ( -- n ) INT 10h/1008 (VGA) | BIOS |

#### QVIDEO.SEQ (1 entry)

| Word | Line(s) | Kind | Semantics | Class |
|---|---|---|---|---|
| `COLOR-EMIT` | 33-59 | CODE | ( c -- ) CR/LF via INT 10h/0E, others INT 10h/09 with ATTRIB; maintains #OUT/#LINE | BIOS |

#### WFL.SEQ (2 entries)

| Word | Line(s) | Kind | Semantics | Class |
|---|---|---|---|---|
| `foff+` | 83-87 | CODE | ( n -- n+foff ) | PERF |
| `>fadr` | 91-98 | CODE | ( n -- dirseg n*b/fnam+1 ) address of n-th filename in far directory buffer | PERF |

#### MAKEDEF.SEQ (1 entry)

| Word | Line(s) | Kind | Semantics | Class |
|---|---|---|---|---|
| `xchange` | 35-43 | CODE | ( a1 a2 -- ) swap cells | PERF |

#### SEDCODE.SEQ (19 entries)

| Word | Line(s) | Kind | Semantics | Class |
|---|---|---|---|---|
| `tl:` | 5-9 | CODE | ( a -- lseg a ) | PERF |
| `tl+` | 11-13 | CODE | ( a1 --- a2 ) a2 = a1 + 2, the list entry size in bytes | PERF |
| `tl-` | 15-17 | CODE | ( a1 --- a2 ) a2 = a1 - 2, the list entry size in bytes | PERF |
| `tl*` | 19-21 | CODE | ( n1 -- n2 ) n2 = n1 * 2, the list entry size in bytes | PERF |
| `getdiskfree` | 23-31 | CODE | ( drv -- clusters bytes/sec secs/cluster ) INT 33 (=21h)/36h | DOS |
| `>lineptr` | 33-37 | CODE | ( n -- 2n ) | PERF |
| `lineptr` | 39-43 | CODE | ( -- curline*2 ) | PERF |
| `tl:@` | 45-49 | CODE | ( a -- n ) fetch from LSEG | SEG |
| `tl:!` | 51-55 | CODE | ( n a -- ) store to LSEG | SEG |
| `tl:+!` | 57-61 | CODE | ( n a -- ) add into LSEG | SEG |
| `endtst?` | 63-75 | CODE | ( -- f ) is line pointer of CURLINE-1 below TEND | SEG PERF |
| `rmsave` | 77-89 | CODE | ( a -- a c ) fetch count byte, track max in RMMAX | PERF |
| `#lineseg` | 91-98 | CODE | ( line -- seg ) fetch line-pointer entry from LSEG | SEG |
| `clipline` | 100-115 | CODE | ( a n -- a' n' ) clip text to window offset and text width | PERF |
| `window.left` | 117-120 | CODE | ( --- n1 ) absolute left edge of window | PERF |
| `window.right` | 122-125 | CODE | ( --- n1 ) absolute right edge of window. | PERF |
| `text.width` | 127-131 | CODE | ( --- n1 ) width of text portion of window | PERF |
| `len-accum` | 133-140 | CODE | ( n -- n ) add n into 32-bit CURRENTSIZE | PERF |
| `adj_ptr_lines` | 144-158 | CODE | ( inc limit start -- ) add inc to a range of line-pointer cells in LSEG | SEG PERF |

#### SEDITOR.SEQ (1 entry)

| Word | Line(s) | Kind | Semantics | Class |
|---|---|---|---|---|
| `?page-char` | 582-592 | CODE | ( n -- ) if n mod PRTLINES = 0, `JMP` to CFA of PAGECHAR | PERF RUNTIME |

#### PRINTING.SEQ (1 entry)

| Word | Line(s) | Kind | Semantics | Class |
|---|---|---|---|---|
| `get_file_date&time` | 127-134 | CODE | ( h -- time date ) INT 21h/5700 | DOS |

#### SCAN.SEQ (3 entries)

| Word | Line(s) | Kind | Semantics | Class |
|---|---|---|---|---|
| `scanw` | 15-31 | CODE | ( a n w -- a' n' ) REPNZ SCASW, ES=SSEG | PERF SEG |
| `-scan` | 37-52 | CODE | ( a n c -- a' n' ) backwards scan (STD), ES=SSEG | PERF SEG |
| `-skip` | 57-71 | CODE | ( a n c -- a' n' ) backwards skip, ES=SSEG | PERF SEG |

#### FWORDS.SEQ (1 entry)

| Word | Line(s) | Kind | Semantics | Class |
|---|---|---|---|---|
| `searchsetup` | 58-68 | CODE | ( -- a1 n1 a2 n2 ) COUNT of SLOOK.BUF and OUTBUF | PERF |

#### MOUSE.SEQ (6 entries)

| Word | Line(s) | Kind | Semantics | Class |
|---|---|---|---|---|
| `show.ms` | 73-77 | CODE | ( -- ) INT 51 (=33h) fn 1 (mouse driver) | BIOS |
| `hide.ms` | 79-83 | CODE | ( -- ) INT 33h fn 2 | BIOS |
| `init.mouse` | 87-110 | CODE | ( -- ) INT 33h fn 0 reset, fn 14 light-pen off; sets BADMOUSE/MOUSEFLG/HAVEMOUSE | BIOS |
| `getmous` | 112-120 | CODE | ( -- x y buttons ) INT 33h fn 3 | BIOS |
| `setmous` | 122-127 | CODE | ( x y -- ) INT 33h fn 4 | BIOS |
| `mouse.scale` | 133-150 | CODE | ( -- ) INT 33h fn 7/8 set range from COLS/ROWS | BIOS |

## 2c. High-level code that writes or decodes machine-code code fields

None of these words have native bodies. They are listed because they write opcodes into code space, patch code fields (CFAs) at run time, or decode a CFA's `CALL`/`JMP rel16`. All of them break if the VM changes the 3-byte code-field layout.

**Code-field layout**

| Word type | Code field (3 bytes, then body at CFA+3) |
|---|---|
| Colon definition | `E9 rel16` (JMP NEST), followed by a cell holding the body's list-space paragraph relative to XSEG |
| CREATE / VARIABLE | `E8 rel16` (CALL >NEXT). The CALL pushes the PFA, then NEXT runs. |
| CONSTANT / VALUE | `E9` (JMP) to DOCONSTANT / DOVALUE |
| DEFER | `E8` (CALL) to DODEFER |
| USER variable / USER DEFER | `E8` (CALL) to DOUSER-VARIABLE / DOUSER-DEFER |
| DOES> child | `E8` (CALL) to code in the parent: `E8` (CALL) to DODOES, followed by a cell with the DOES> list paragraph |
| CODE word | Machine code starts at the CFA itself (PASM.SEQ:146-147: `LABEL -3 DP +!` reclaims the 3 bytes CREATE laid down) |

| File:line | Word | What it does | Class |
|---|---|---|---|
| KERNEL1.SEQ:204-207 | CONSTANT (meta) | Compiles `233` (E9) + rel to DOCONSTANT into target code space | RUNTIME |
| KERNEL1.SEQ:209-212 | VALUE (meta) | E9 + rel to DOVALUE | RUNTIME |
| KERNEL1.SEQ:215-220 | DOES> (meta) | `(;CODE)`, then `232` (E8) CALL DODOES + relative list paragraph | RUNTIME |
| KERNEL1.SEQ:270-274 | `:` (meta) | E9 JMP NEST + relative paragraph of the list body | RUNTIME |
| KERNEL3.SEQ:657-661 | ,CALL / ,JUMP | Emit `E8`/`E9` + rel16 (placeholder 0) | RUNTIME |
| KERNEL3.SEQ:668-672 | "CREATE / CREATE | `,CALL ;USES >NEXT` | RUNTIME |
| KERNEL3.SEQ:686-687 | (;USES) | Runtime: rewrites rel16 of LAST's CFA to the following label | SELFMOD |
| KERNEL3.SEQ:689-692 | (;CODE) | Runtime: forces `E8` into LAST's CFA and rewrites rel16 | SELFMOD |
| KERNEL3.SEQ:694-698 | DOES> | Compiles (;CODE), then `E8` CALL DODOES + paragraph into code space | RUNTIME |
| KERNEL3.SEQ:709-715 | ;USES / ;CODE | Compile-time halves of the above | RUNTIME |
| KERNEL3.SEQ:731-738, 745-753 | MAKEDUMMY, (:) | `,JUMP`, list paragraph, `;USES NEST` | RUNTIME |
| KERNEL3.SEQ:765-778 | CONSTANT, VALUE, VARIABLE, ARRAY, DEFER | `;USES DOCONSTANT/DOVALUE/>NEXT/>NEXT/DODEFER` | RUNTIME |
| KERNEL3.SEQ:782-798 | VOCABULARY, 2CONSTANT, 2VARIABLE | DOES> children | RUNTIME |
| KERNEL4.SEQ:23-36 | USER CREATE / USER DEFER | `;USES DOUSER-VARIABLE / DOUSER-DEFER` | RUNTIME |
| KERNEL4.SEQ:40-45 | >IS | Decodes the CFA (`DUP 1+ @ OVER >BODY +`) and compares it with the DOUSER-* label addresses | RUNTIME |
| KERNEL4.SEQ:47-50 | (IS) | Reads its inline operand with `2R@ @L` (return-stack frame is IP + ES) | RUNTIME SEG |
| KERNEL4.SEQ:499-501 | RESOLVES <VARIABLE> etc. | Metacompiler fixes up CALL/JMP targets in code space | RUNTIME |
| POINTER.SEQ:29-51 | POINTER | DOES> + PTR_OK?EXIT; `RESOLVES <POINTER>` patches the metacompiler-generated CALLs | RUNTIME |
| SAVEREST.SEQ:104-139 | SAVE!> SAVE> RESTORE> | `@REL>ABS` to tell USER variables from plain ones, then compile the % word + CFA | RUNTIME |
| PASM.SEQ:143-147, 159-164 | LABEL, CODE, END-CODE, C; | CODE = CREATE then back up 3 bytes so code starts at the CFA | RUNTIME |
| PASM.SEQ:785-801 | NEXT, 1PUSH, 2PUSH | Macros: inline `26 AD FF E0` or `JMP >NEXT` / `>NEXT-1` / `>NEXT-2` | RUNTIME |
| PASM.SEQ:840-844 | INLINE / END-INLINE | Compiles `HERE` (a code address) into the list as an anonymous xt. Defined but never used in a loaded file. | RUNTIME |
| NEWLAB.SEQ:85-92 | +field | `create dup c, ;code ...` | RUNTIME |
| DBGFIX.SEQ:23-52 | findinline, debugable | Scans the whole code segment for `26 AD FF E0` (inline NEXT) and replaces each with `JMP >NEXT` | SELFMOD |
| MAKEDEF.SEQ:12-29 | :def?, makedefer | Writes `$E8`/`$E9` and rel16 into two CFAs, turning a colon def into a DEFER, and swaps bodies | SELFMOD |
| MAKEDEF.SEQ:45-51 | switch_bodies | Swaps the list-paragraph cells of two colon defs | SELFMOD |
| DEFERS.SEQ:33-42 | DEFERS, UNDEFER | `@REL>ABS` type test; reads a colon body's first cell with `>BODY @ +XSEG 0 @L` | RUNTIME SEG |
| UTILS.SEQ:52-63 | DOES?, 'DOCOL, >.ID | Double `@REL>ABS` to detect DOES>; scans backwards for a colon CFA | RUNTIME |
| WORDS.SEQ:27-110 | word-type filters | `C@ 232/233` opcode tests and `@REL>ABS` comparisons | RUNTIME |
| COLORIZE.SEQ:31, 84 | CLR-OTHER etc. | `C@ 232 <>`, `@REL>ABS` | RUNTIME |
| DECOM.SEQ:154, 268-322 | decompiler | Opcode tests, `@REL>ABS`, (;CODE) layout, walks list space | RUNTIME SEG |
| DEBUG.SEQ:171-174, 180-414 | 'UDEFER 'DEFER 'DODOES, stepping | `@REL>ABS` classification, walks ES:IP | RUNTIME SEG |
| REF.SEQ:38-50; SEDWHELP.SEQ:166-170 | | `@REL>ABS` classification | RUNTIME |
| MENUS.SEQ:44 | | Lays a CFA into list space with `' xhere !L` (hand-compiled thread) | SEG |
| MULTASK.SEQ:169-208 (not loaded) | SLEEP, WAKE, SINGLE, MULTI | ENTRY user variable holds machine code (`CD E9` = INT E9h, or `90 E9`). SINGLE/MULTI patch `JMP` over PAUSE. MULTI writes the INT E9h vector at 0:3A4. | SELFMOD HW |

## 3. Trickiest items for the port

### 3.1 Self-modifying code

| Where | What | Why it matters |
|---|---|---|
| KERNEL1.SEQ:125-126 (NESTPATCH), 168-169 (DOESPATCH); patched at KERNEL4.SEQ:331-333 | `ADD AX, # XSEG` immediates in NEST and DODOES are overwritten at cold start with the real list-segment base | In the VM, XSEG is just a register or variable. Any code that reads these addresses (PROFILE.SEQ uses `>NEST $0E +`, `>DOES $0C +`) breaks. |
| KERNEL1.SEQ:35-36 (>NEXT) with ABNORM 76-86, BIOSBK 88-105, SETBRK KERNEL4.SEQ:209-234 | Ctrl-Break ISR overwrites the 4 bytes of `>NEXT` with `JMP ABNORM`; ABNORM and SETBRK restore `26 AD FF E0` | Asynchronous break works by corrupting the inner interpreter. The VM needs an `interrupt-pending` flag checked in NEXT (or at branches) that diverts to WARM. |
| KERNEL1.SEQ:478-482 (PAUSE) + MULTASK.SEQ:189-208 | 3 NOPs patched to `JMP (PAUSE)`; task ENTRY cells hold `INT E9h` opcodes | Make PAUSE a DEFER and implement tasks as VM register-set switches. |
| DEBUG.SEQ:117-158 (FNEXT, DNEXT, DEBNEXT, PNEXT, UNBUG) | The debugger traps by patching `>NEXT` to `JMP DEBNEXT`, which compares ES:IP to a breakpoint | Needs a VM single-step/breakpoint hook in the dispatch loop. |
| DBGFIX.SEQ:23-52 | Rewrites every inline NEXT in the code segment into `JMP >NEXT` so the patch above takes effect | Obsolete in a VM with one dispatch loop; drop it. |
| ORIGIN KERNEL1.SEQ:29-31, WORIG/CORIG KERNEL4.SEQ:271-284 | Entry JMPs patched by the metacompiler; CORIG patches the LIMIT/FIRST CONSTANT bodies (`' LIMIT 3 + AX`, 346-348) | Replace with a C loader that initialises the image header and these values. |
| KERNEL3.SEQ:686-692, MAKEDEF.SEQ:17-29, PROFILE (TOOLS) | Runtime CFA rewriting | The VM's code-field representation must stay writable with the same 3-byte semantics (see 4.2). |

### 3.2 DTC-layout dependence

- **Body = CFA+3.** `>BODY`/`BODY>` are `3 +`/`3 -` (KERNEL3.SEQ:95-105). `DOCONSTANT` reads `3[W]`; EQUCOLON/SAVEREST `%xx>` words read `3[BX]` of an inline CFA (EQUCOLON.SEQ:13-85, SAVEREST.SEQ:34-101).
- **`@REL>ABS`** (KERNEL4.SEQ:261-267), plus `C@ 232/233` tests in WORDS, COLORIZE and DECOM, classify words by the target of the CALL/JMP in their CFA. This is about 15 call sites across 9 loaded files (section 2c).
- **Hard-coded CFA arithmetic from code-segment labels:**
  - `WARMBODY 5 -` and `DIV0BODY 5 -` (KERNEL4.SEQ:202, 275) assume a 5-byte colon header: JMP + paragraph cell.
  - `COLDBODY 2-` reads the paragraph cell (KERNEL4.SEQ:361).
  - CORIG patches `' LIMIT 3 +` (KERNEL4.SEQ:346).
- **CREATE's CFA is `CALL >NEXT`** (KERNEL3.SEQ:672). The PFA is delivered by pushing the CALL's return address. DOES> children use the same trick (two nested CALLs, DODOES KERNEL1.SEQ:156-173). `GO` (KERNEL1.SEQ:472) is `RET`, which jumps to the address on the *data* stack.
- **Machine-code tail jumps into high-level CFAs** (`MOV AX, # ' word / JMP AX`):
  - %NUMBER (KERNEL2.SEQ:1014-1049);
  - %FIND (KERNEL3.SEQ:254-269);
  - (?STACK) (KERNEL3.SEQ:300-322);
  - SPCHECK (KERNEL3.SEQ:608-627);
  - ?FILLBUFF, ?.LOADLINE (SEQREAD.SEQ:164-170, 327-333);
  - ?page-char (SEDITOR.SEQ:582-592);
  - VIDEO-TYPE (VIDEO2.SEQ:139-140);
  - ?EXIT (KERNEL1.SEQ:144).

  In the C VM these become "execute xt" from a primitive, so primitives must be able to return an xt to dispatch next.
- **`SKIP'C'`** (KERNEL3.SEQ:273-282) does `ADD SI,2`, skipping the next cell of the *caller's* thread. It is used by DEFINED (KERNEL3.SEQ:284-285) to bypass `?UPPERCASE` for `'c'` literals. **`PTR_OK?EXIT`** (POINTER.SEQ:13-24) performs EXIT on behalf of the DOES> body that called it. **`EXEC:`** and **`GOTO`** (KERNEL1.SEQ:440-465) fetch an inline CFA and then EXIT.
- **Duff's-device computed jump:** VIDEO-TYPEL does `add cx, # here $06 +` / `jmp cx` into a 132x unrolled LODSB/STOSW block (VIDEO2.SEQ:85-88). The jump depends on exact instruction sizes.

### 3.3 ES:IP list-space threading and the return-stack frame

- Each colon body lives in its own paragraph-aligned chunk of a separate **list segment**. The CFA holds `JMP NEST` plus the body's paragraph relative to XSEG. NEST sets `ES = XSEG + rel` and `IP = 0` (KERNEL1.SEQ:114-131).
  - All branch targets are **offsets within that ES chunk** (`MOV ES: IP, 0 [IP]`, KERNEL1.SEQ:290-328).
  - `(DO)` stores the leave address as an offset (KERNEL1.SEQ:376-386).
- **NEST pushes two cells: ES, then IP.** So one return-stack frame is 2 cells, and a DO frame is 3 cells (leave-offset, limit+8000h, index-bias). High-level code relies on this:
  - `COMPILE` = `2R@ R> 2+ >R @L X,` (KERNEL3.SEQ:367-368);
  - `(.")`, `(")`, `X>"BUF`, `(ABORT")`, `CRASH`, `(IS)` (KERNEL3.SEQ:387-475, KERNEL4.SEQ:47-50) read inline data with `2R@ ... @L`;
  - `DROP.CONTEXT.I2*+@DUP` reads `0[RP]+2[RP]` (KERNEL3.SEQ:199-209);
  - `DBG_RDEPTH`'s magic `4 - 14 -` (DEBUG.SEQ:86-88);
  - `UNDO` (`ADD RP,#6`) and TOOLS ANSI `UNLOOP`.
- **Inline-operand words** take the next cell from ES:IP as data or an xt: (LIT), <'>, branches, (DO), (OF), GOTO, EXEC:, the 10 EQUCOLON `%xx>` words and the 6 SAVEREST words. They need a VM `fetch-inline` operation that matches the list-space address model.
- **Recommendation:** have the C VM represent IP as a **(segment, offset) pair, or a linear 20-bit address**, with the return-stack frame kept as `[IP][ES]`. Then `2R@ @L` and all of the above keep working unchanged. Flattening IP to a single linear pointer would mean rewriting COMPILE, `(.")`, `(ABORT")`, `(IS)`, X>"BUF, DEFERS, the decompiler, the debugger, and TOOLS LOCALS/CODEHIGH/MONITOR.

### 3.4 16-bit cells and segment:offset arithmetic

- **Cell = 2 bytes everywhere.** Examples: `2+`/`2-` used as CELL+, header link offsets, `PICK` = `2*` + SP, `CNHASH` = `$FE00 AND FLIP` (KERNEL3.SEQ:51-52) indexing the >NAME table by the CFA's high byte, `#OUT`/`#LINE`, and `HASH` reading two name characters with one little-endian `MOV AX,1[BX]` (KERNEL3.SEQ:139).
- **Arithmetic edge cases the C VM must reproduce exactly** (not with C int semantics):
  - `(DO)` biases with `$8000` and `(LOOP)`/`(+LOOP)` terminate on the **8086 overflow flag** (KERNEL1.SEQ:358-386);
  - `/`, `/MOD` and `*/MOD` are **floored** (KERNEL1.SEQ:1325-1367);
  - `UM/MOD` returns `-1 -1` on overflow (KERNEL1.SEQ:1000-1009);
  - an IDIV overflow or divide-by-zero traps through INT 0 into `DIV0BK` and ABORTs "Divide OVERFLOW error" (KERNEL4.SEQ:166-204). C would hit UB or SIGFPE instead.
- **Memory map:**
  - the code segment is 64K with `LIMIT = -2` (KERNEL2.SEQ:880-887);
  - the stacks and TIB sit at the top of the code segment (CORIG KERNEL4.SEQ:339-359);
  - `PAD = HERE+80` (KERNEL2.SEQ:222-227);
  - stack-overflow checks compare SP with HERE (KERNEL3.SEQ:300-322).

  Because `SP@` returns a real address that code uses as a buffer (`FEMIT` = `SP@ 1 TYPE`, KERNEL2.SEQ:734-736; `ROLL` uses `CMOVE>` on stack memory, KERNEL1.SEQ:1373-1375), **both stacks must live in VM-addressable memory** in the code ("DS") segment.
- **Far memory is everywhere:**
  - heads live in YSEG (`Y@`/`Y!`/`Y,`/`YC@`, TRAVERSE, CNSRCH, (FIND), YHASH);
  - list space lives in XSEG (`X,`/`XC,`/`XHERE`, `+XSEG`);
  - SSEG is the implicit segment of the second string for `COMP`/`CAPS-COMP`/`SKIP`/`SCAN`/`SEARCH`/`-scan`/`-skip`/`scanw` (KERNEL2.SEQ:250-289, 1134-1162; SCAN.SEQ). It is set to CS by SETYSEG (KERNEL2.SEQ:573) but can be redirected.
  - POINTER allocates far blocks in paragraphs, which `CMOVE-PARS` moves in 160-byte steps (KERNEL2.SEQ:455-486);
  - the SED editor keeps its line table in LSEG (SEDCODE.SEQ).

  The simplest faithful model is a **1 MB linear byte array addressed as `seg*16+off`**, with `?CS:`/`?DS:` returning fixed virtual segments. That makes all SEG words trivial C built-ins.
- **Startup and image layout.** CORIG (KERNEL4.SEQ:279-366) and SEXE (LOADEXE.SEQ:15-93) relocate heads and list space out of the file image into separate segments, and SAVEEXE writes an MZ header (SAVEEXE.SEQ:15-21). These become a C image loader and saver. The saved image needs three spaces (code, list, head) plus the YSTART/DPSTART/XSEGLEN/XMOVED variables.
- **CMOVE is not a byte-at-a-time move** (KERNEL1.SEQ:536-572); it moves words when the destination is aligned. Implement CMOVE as an ascending byte loop, and CMOVE> as a descending one. Do not use `memmove`: overlapping-propagation semantics differ.

### 3.5 Interrupt and vector handlers

| Handler | Vector | Location | VM replacement |
|---|---|---|---|
| BIOSBK | INT 1Bh | KERNEL1.SEQ:88-105 | SIGINT handler that sets the interrupt-pending flag |
| DOSBK | INT 23h | KERNEL1.SEQ:107-112 | none |
| DIV0BK | INT 0 | KERNEL4.SEQ:191-204 | arithmetic check in the primitives |
| CRITINT | INT 24h | HCRITICA.SEQ:61-67 | host I/O errors returned as codes |

Vectors are installed by SETBRK and SAVEVECTORS (KERNEL4.SEQ:209-253), SET_VECTORS and RESTORE_VECTORS (KERNEL4.SEQ:147-164, 255-259), and SETCRITICAL. `<EXTEXEC>` swaps SS:SP, saves the VM registers and EXECs COMMAND.COM after paging the whole image to EMS or disk (EMMEXEC.SEQ:62-205). Replace it with `system()`.

## 4. Proposed minimal primitive set for the C VM

This set comes from what the kernel's high-level code actually calls and from the runtime structures above. Everything in KERNEL1-4 that is not listed here either is already high-level or is marked PERF. PERF words can be re-expressed in Forth, although most are one-line C and worth keeping as built-ins for speed.

### 4.1 VM machine state

The VM registers are:
- `IP` = (ES, off), or a linear address;
- `SP` and `RP` (addresses inside VM memory);
- `W` (the current CFA);
- `UP` (user area);
- `XSEG`, `YSEG`, `SSEG`, `DS`/`CS` constants;
- an `interrupt-pending` flag and an optional `trace` hook in the dispatch loop.

Memory is a 1 MB byte array addressed `seg*16+off`, with 16-bit little-endian cells.

### 4.2 Code-field model (keeps all the layout-dependent Forth source working)

Keep the 3-byte CFA as `[opcode byte][rel16]` in VM memory:
- `E9 rel16` means "jump to the routine at CFA+3+rel";
- `E8 rel16` means "call it" (pushes CFA+3 as the PFA).

The targets are **virtual label addresses** for a small, fixed set of C runtime handlers. Any other target is a CODE body for the abstract register machine (sljit) or a C primitive id. With this model, `@REL>ABS`, `C@ 232 =`, `>BODY`, `(;CODE)`, `(;USES)`, MAKEDEF, DEFERS, the decompiler and the debugger run unchanged.

The required runtime handlers (each is a code-field target, not a dictionary word):

| Handler | Source | Behaviour |
|---|---|---|
| `NEST` | KERNEL1:114 | push ES, IP; ES = XSEG + [W+3]; IP = 0 |
| `DODOES` | KERNEL1:156 | two-level CALL: push the child's PFA and run the DOES> list |
| `>NEXT` (DOVAR) | KERNEL1:35 | CREATE/VARIABLE/ARRAY via `CALL >NEXT`: push the PFA |
| `DOCONSTANT`, `DOVALUE` | KERNEL1:177-185 | |
| `DODEFER`, `DOUSER-DEFER` | KERNEL1:452, 467 | |
| `DOUSER-VARIABLE` | KERNEL1:187 | |
| POINTER's DOES> code | POINTER.SEQ:36 + `RESOLVES <POINTER>` | handled by DODOES; no special handler needed |

### 4.3 Threading and control primitives (RUNTIME, must be built-ins)

`EXIT` `UNNEST` `?EXIT` `(LIT)` `<'>` `BRANCH` `?BRANCH` `DOAGAIN` `DOREPEAT` `?WHILE` `?UNTIL` `DOENDOF` `DOBEGIN` `DOTHEN` `DOCASE` `DOENDCASE` `NEXT|` `(DO)` `(?DO)` `(LOOP)` `(+LOOP)` `(LEAVE)` `(?LEAVE)` `UNDO` `I` `J` `K` `(OF)` `EXECUTE` `PERFORM` `GOTO` `EXEC:` `NOOP` `SKIP'C'` `PTR_OK?EXIT`

There are 37 of these. The no-op markers and branch aliases must stay **distinct xts** for DECOM. Add the inline-operand family `%@> %!> %U@> %U!> %+!> %INCR> %DECR> %OFF> %ON> %&> %SAVE!> %SAVE> %RESTORE> %USAVE!> %USAVE> %URESTORE>` (16). They could be written in Forth with `2R@ @L`, but they sit on hot paths (`!>`, `SAVE>` and so on are used everywhere).

`PAUSE` becomes a DEFER (default NOOP). `GO` (execute machine code at an address) maps to "run abstract-machine code at addr".

### 4.4 Core data primitives (PRIM, about 40)

| Group | Primitives |
|---|---|
| Stack | `DROP DUP SWAP OVER ROT >R R> R@ SP@ SP! RP@ RP! PICK` |
| Memory | `@ ! C@ C! +! 2@ 2!` |
| Block | `CMOVE CMOVE> FILL` |
| Arithmetic / logic | `+ - AND OR XOR NOT NEGATE 2* 2/ U2/ 0= 0< = U< < UM* UM/MOD` (UM/MOD with the -1 overflow result) |
| Far memory (SEG) | `@L !L C@L C!L CMOVEL CMOVEL> LFILL ?CS: ?DS: ?ES:` |

### 4.5 Strongly recommended built-ins (PERF in the original; cheap in C and on hot paths)

| Group | Words |
|---|---|
| Stack shuffles | `NIP TUCK -ROT PLUCK ?DUP 2DUP 2DROP 3DUP 3DROP 2SWAP 2OVER 2>R 2R> 2R@ DUP>R R>DROP RPICK` |
| Arithmetic | `1+ 1- 2+ 2- * / /MOD */MOD *D ABS MIN MAX UMIN UMAX 0MAX` |
| Comparisons | `0> 0<> <> U> > WITHIN BETWEEN UBETWEEN` |
| Double cell | `D+ DNEGATE DABS S>D D2* D2/ DU< D+!` |
| Memory updates | `ON OFF INCR DECR 0DECR C+! CSET CRESET CTOGGLE FLIP SPLIT JOIN` |
| Strings | `COUNT PLACE +PLACE UPC UPPER ?UPPERCASE -TRAILING COMP CAPS-COMP SKIP SCAN /STRING WORD DIGIT HASH (FIND) TRAVERSE CNSRCH` |
| Compiler and head space | `HERE PAD , C, X, XC, XHERE +XSEG Y@ Y! YC@ YC! Y, YCSET YHERE YS: YHASH` |
| Interpreter helpers | `SEARCH -scan -skip scanw` (for SED and the editor) |

The headerless %%FIND helpers (`DROP.CONTEXT.I2*+@DUP`, `PRIOR.CHECK`, `OVER.SWAP.HASH.@`), `%FIND`, `%NUMBER`, `(?STACK)`, `SPCHECK`, `DONE?`, `SOURCE`, `TIB`, `MORE?`, `SETTIB`, `GET_ALINE`, `CRLF>BL'S` and `LENGTH.CHECK` already have high-level equivalents in comments or are trivial. Implement them as Forth, or as C if profiling says so.

### 4.6 Host-services layer (replaces every DOS, BIOS, VIDEO and HW word)

| Area | Original words | Host-layer replacement |
|---|---|---|
| Files (16 words) | `<BDOS>`, `BDOS2`, `HDOS1`, `HDOS3`, `HDOS4`, `MOVEPOINTER`, `ENDFILE`, `CURPOINTER`, `<HRENAME>`, `FIND-FIRST`, `FIND-NEXT`, `DTA@`, `DTA!`, `PDOS`, `get_file_date&time`, `getdiskfree` | ~12 host calls: open, create, close, read, write, seek, delete, rename, dir-first/next, getcwd/chdir, file time, disk free. **Easiest path:** keep `<BDOS>`/`HDOSn` as a C INT-21h *emulator* for the subset of AH functions used. This leaves HANDLES, SEQREAD, FPATH and EXEC source untouched. |
| Memory | `DOS_ALLOC`, `DOS_DEALLOC`, `DOS_SETBLOCK` | A paragraph allocator over the 1 MB arena |
| Console input | `BIOSKEY?`, `BIOSKEY` | `KEY?`/`KEY` returning BIOS-style scan<<8\|ascii codes |
| Console output | `VIDEO-TYPEL`, `VIDEO-TYPE`, `COLOR-EMIT`, `LFILLW`, `?VMODE` (sets COLS/ROWS/CROWS) | A virtual 80xN char+attribute screen buffer at a virtual B800 segment (so high-level VIDEO-SEG code keeps working), flushed to the terminal |
| Cursor and screen | `IBM-AT`, `IBM-AT?`, `SET-CURSOR`, `IBM-DARK`, `IBM--LINE`, `blink!`, `pal!`, `border!`, `border@` | Terminal calls |
| Printer | `PR-STATUS` | `PR-STATUS` returns "ready" |
| Mouse (optional) | `show.ms`, `hide.ms`, `init.mouse`, `getmous`, `setmous`, `mouse.scale` | Terminal mouse events |
| Stubs | Ports `PC@ P@ PC! P!`, `BLANK.COLOR`, `SHOW.COLOR` | No-ops |
| Stubs | EMS: `EMM-*` | Report "not present" |
| Stubs | `SET_VECTORS`, `RESTORE_VECTORS`, `SETCRITICAL` | No-ops plus signal setup |
| Stubs | `<EXTEXEC>` | `system()` |
| Stubs | BIOS data area reads such as WINSTACK.SEQ:26 `$40 $17 C@L` (shift state) and VIEW.SEQ:42, KERNEL4.SEQ:476 `GET-CURSOR` (`0 $460 @L`) | A synthetic BIOS data area at segment 40h, kept current by the host |

### 4.7 Things that disappear

| Item | Reason |
|---|---|
| ORIGIN, APUSH, DPUSH, WORIG, CORIG, SEXE | Replaced by the C loader |
| ABNORM, BIOSBK, DOSBK, DIV0BK, CRITINT, SETBRK, SAVEVECTORS | Replaced by signals and checks in the dispatch loop |
| NESTPATCH, DOESPATCH | XSEG is a VM register |
| DBGFIX, PNEXT, FNEXT, DNEXT, DEBNEXT, UNBUG | Replaced by a VM trace hook. DEBUG.SEQ's high-level part only needs `PNEXT`/`UNBUG` to set and clear it, plus `'DEBUG`/DBSEG/DBOFF. |
| SMALFPC, EMM>FPC, DSK>FPC | Replaced by `system()` |

## 5. TOOLS/ summary (per file)

Counts come from a comment-aware token scan of the stripped copies. RS232IB counts only the F-PC (`\F`) build lines. In MONITOR and WINDOW, `16 INT`/`33 INT` are decimal: INT 10h and 21h. In this section, ASM/DIS marks assembler and disassembler files, which are inherently x86-specific.

| File | Lines | CODE | LABEL | ;CODE | Other native | Classes | Notes |
|---|---|---|---|---|---|---|---|
| ANSI.SEQ | 366 | 3 | 0 | 0 | Hand-built colon CFA: `233 c, >nest here 2+ - ,` (l.82) | PRIM, RUNTIME | LSHIFT, RSHIFT (207/229); UNLOOP `ADD RP,#6` (250) assumes the 3-cell DO frame; `token` builds JMP NEST + XSEG paragraph |
| AUTOFOR.SEQ | 85 | 0 | 0 | 0 | — | (SEG via CMOVEL) | Auto forward refs; writes headers into YSEG, patches DEFER bodies |
| AUTOLOAD.SEQ | 17 | 0 | 0 | 0 | — | — | A `coment;` typo makes the whole file a comment |
| BLKTOSEQ.SEQ | 68 | 0 | 0 | 0 | — | — | |
| BLOCK.SEQ | 229 | 5 | 0 | 0 | — | PRIM, PERF | buf#>bufaddr, >rec#s, >rec#updt, >rec#fil, chkfil (53-111) |
| CODEBUG.SEQ | 873 | 9 | 2 | 0 | Writes `$CC` (INT 3) into target code (613, 646); trap flag (527); IRET into target | HW, DOS, SELFMOD, SEG, RUNTIME, DIS | Machine-code debugger; INT1/INT3 ISRs (338, 371); vector save/set via INT 21h/25h, 35h (417-487) |
| CODEHIGH.SEQ | 55 | 1 | 1 | 0 | Assembler inside a colon def: `>pre call hdoes ... pre>` (38) | RUNTIME | LABEL hdoes forges a NEST from a CALL site (`sub ax,#3; jmp >nest`); hret `jmp ax+2` |
| COLOURS.SEQ | 72 | 0 | 0 | 0 | — | — | |
| COLPLAY.SEQ | 349 | 0 | 0 | 0 | — | — | |
| COMMAND.SEQ | 29 | 0 | 0 | 0 | — | — | Nested interpreter; `sp!`/`rp!` re-pointing |
| COMPLEX.SEQ | 105 | 0 | 0 | 0 | — | — | |
| CONSTANT.SEQ | 53 | 0 | 0 | 0 | — | — | |
| DIS8086.SEQ | 873 | 4 | 0 | 0 | — | DIS, PRIM, DOS | 8086 disassembler; #INT2@ INT 21h/35h (840); symbol table uses `>NEXT`, `' (LOOP) 5 +` (213) |
| DISASSEM.SEQ | 501 | 4 | 0 | 0 | — | DIS, PRIM | Older disassembler; kernel-layout symbols (94) |
| DMULDIV.SEQ | 234 | 2 | 0 | 0 | — | PRIM, PERF | UMD/MOD (116), UMD* (185); borrows SI/BP |
| DOSIO.SEQ | 61 | 0 | 0 | 0 | — | (DOS via bdos) | |
| EMMEXMPL.SEQ | 117 | 0 | 0 | 0 | — | (SEG, VIDEO high-level) | CMOVEL to `video-seg` (98-106) |
| EVAL.SEQ | 52 | 0 | 0 | 0 | — | — | |
| EXPANDED.SEQ | 22 | 0 | 0 | 0 | — | — | Empty stub |
| FASSEM.SEQ | 327 | 7 | 1 | 0 | Emits 8087 opcodes (17, 75) | ASM, PRIM | 8087 assembler extension; LABEL (POWER) (293) |
| FDIS86.SEQ | 235 | 0 | 0 | 0 | — | DIS | 8087 disassembly tables |
| FDISASEM.SEQ | 161 | 0 | 0 | 0 | — | DIS | |
| FFLOAT.SEQ | 2062 | 60 | 10 | 0 | `mov ax,#' FPERR / jmp ax` (179, 211...) | PRIM, RUNTIME, SEG, PERF | 8087 float with spill stack; error paths read seg/IP from RP and the xt from `ES:-2[SI]` (167-212); (FLIT) inline 8-byte literal (878) |
| FLTAUX.SEQ | 283 | 15 | 0 | 0 | — | PRIM | 8087 helpers for CODEBUG |
| FLTBUG.SEQ | 58 | 0 | 0 | 0 | — | — | |
| FORWARD.SEQ | 33 | 0 | 0 | 0 | — | — | |
| FPMATH.SEQ | 69 | 0 | 0 | 0 | — | — | |
| FUNKEY.SEQ | 70 | 0 | 0 | 0 | — | — | |
| LASERJET.SEQ | 54 | 0 | 0 | 0 | — | — | |
| LISTED.SEQ | 94 | 0 | 0 | 0 | — | — | |
| LOCALS.SEQ | 89 | 6 | 0 | 0 | Pushes a fake ES:IP frame onto RP (54-60); `jmp ' exit` (38) | RUNTIME | Depends on the NEST/EXIT frame format |
| MIDNIGHT.SEQ | 144 | 0 | 0 | 0 | — | — | |
| MONITOR.SEQ | 246 | 7 | 0 | 0 | Patches QUIT's list body at offset 22 with `!L` (218) | BIOS, SELFMOD | INT 10h words (37-95) |
| MORE.SEQ | 78 | 0 | 0 | 0 | `switch_bodies` (39-74) | SELFMOD (threaded) | |
| NEW-WFL.SEQ | 1361 | 24 | 0 | 0 | — | BIOS, DOS, SEG, PERF | Russian file selector; INT 10h/21h wrappers, far dir table |
| NEWCOM.SEQ | 36 | 1 | 0 | 0 | Writes assembled bytes to TEST.COM (25-34) | DOS, ASM | |
| OBJECT.SEQ | 199 | 1 | 0 | 0 | Method stubs: `,JUMP` + rel16 patch (92-93) | RUNTIME | `action` does `jmp ax` into the stub (30-40) |
| OVERLAY.SEQ | 215 | 0 | 0 | 0 | Loads code/list images to fixed addresses (155-158) | SEG, RUNTIME | Image-layout dependent |
| PICTURE.SEQ | 65 | 0 | 0 | 0 | — | — | |
| PROFILE.SEQ | 201 | 2 | 3 | 0 | PRPATCH writes JMPs into NEST (`>NEST $0E +`), DODOES, EXIT, UNNEST (66-77) | SELFMOD, HW, DOS, SEG, RUNTIME | MYTIMER INT 1Ch ISR histogram indexed by ES (83-96) |
| RS232IB.SEQ | 497 | 12 (+1 TCOM) | 1 | 0 | — | HW, DOS, BIOS, SEG | 8250/8259 port I/O with CLI/STI (92-178); IRQ ISR com_int (239-284); vectors 0Bh/0Ch; BIOS 40:0 |
| SALLOC.SEQ | 48 | 0 | 0 | 0 | — | (SEG) | |
| SBOOT.SEQ | 94 | 0 | 0 | 0 | — | — | |
| SCROLL.SEQ | 46 | 2 | 0 | 0 | — | BIOS | INT 10h/06, 07 |
| SELECT.SEQ | 538 | 0 | 0 | 0 | — | — | |
| SEQTOBLK.SEQ | 36 | 0 | 0 | 0 | — | — | |
| SETJMP.SEQ | 55 | 0 | 0 | 0 | — | (RUNTIME high-level) | `rp@ rp! sp@ sp!` |
| SFLOAT.SEQ | 10 | 0 | 0 | 0 | — | — | Loader |
| SFLOAT1.SEQ | 1252 | 33 | 1 | 0 | `jmp ' FPERR` (267, 571...) | PRIM, PERF | Software float |
| SFLOAT2.SEQ | 1127 | 10 | 2 | 1 | `JMP ' F+` (859) | PRIM, RUNTIME, PERF | FCONSTANT `;CODE` (22) pops the PFA pushed by the child's CALL |
| SFLOAT3.SEQ | 1248 | 9 | 3 | 0 | — | PRIM, RUNTIME, SEG | (FLIT) `LODSW ES:` inline literal (859-870) |
| SMACRO.SEQ | 135 | 0 | 0 | 0 | — | — | |
| SMENU.SEQ | 86 | 0 | 0 | 0 | — | — | |
| SMESSAGE.SEQ | 19 | 0 | 0 | 0 | — | — | |
| SPREAD.SEQ | 846 | 0 | 0 | 0 | — | — | |
| SSCROLL.SEQ | 34 | 0 | 0 | 0 | — | — | |
| SVALUES.SEQ | 47 | 0 | 0 | 0 | — | — | |
| SWINDOW.SEQ | 32 | 0 | 0 | 0 | — | — | |
| UNLINK.SEQ | 37 | 0 | 0 | 0 | Rewrites the DEFERS chain in list space via `@L`/`!L` | SELFMOD (threaded), SEG | |
| UNREF.SEQ | 100 | 1 | 0 | 0 | — | SEG | c+!L (28) |
| VMOVE.SEQ | 67 | 0 | 0 | 0 | — | — | |
| WINDEX.SEQ | 81 | 0 | 0 | 0 | — | — | |
| WINDOW.SEQ | 427 | 12 | 5 | 0 | `label saveh nop nop` code-segment variables written via `cs:` (191-232) | VIDEO, BIOS, DOS, SEG, SELFMOD | Direct copies to B000 (mono only, 152); INT 21h/48-4A |
| WYSE50.SEQ | 82 | 0 | 0 | 0 | — | — | |
| XMS.SEQ | 627 | 2 | 1 | 0 | `CALL FAR [] XMAddr` (126-127) | SEG, HW, RUNTIME | INT 2Fh/43xx driver discovery (164-193); LABEL (xmAbort) jumps into the Forth XMAbort CFA (146-155) |
| **Total (66 .SEQ)** | **17812** | **232 (+1)** | **30** | **1** | | | 24 files contain native code |

The non-`.SEQ` files are help text and documentation, not loadable code: `*.HLP`, `*.TXT`, and SFBUGS.FIX (patch notes).

**Hardest TOOLS items:**
- **PROFILE:** patches NEST, DODOES, EXIT and UNNEST at fixed byte offsets, and installs an INT 1Ch ISR.
- **CODEBUG:** INT 1/INT 3 ISRs, the trap flag, `$CC` breakpoints.
- **RS232IB:** UART/PIC port I/O plus an IRQ ISR.
- **FFLOAT/SFLOAT:** large x86/8087 bodies that read the caller's IP/ES and inline literals at ES:SI. Reimplement on C doubles, but keep the inline-literal format.
- **ANSI `token` and OBJECT:** hand-built code fields. They need a VM "make colon xt" primitive.
- **CODEHIGH and LOCALS:** forge NEST/EXIT frames.
- **WINDOW:** code-segment variables and hard-coded B000.
- **XMS:** far CALL into HIMEM.
- **MONITOR:** `!L` at a fixed offset in QUIT's body. This needs the list layout preserved exactly.
