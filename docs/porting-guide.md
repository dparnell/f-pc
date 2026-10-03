# Porting F-PC `.SEQ` files to the native VM

This guide covers porting the high-level source in `SRC/` (and later `TOOLS/`) from the 16-bit DOS F-PC to the native 32-bit VM. The rules come from the ported kernel (`SRC/KERNEL*.SEQ`, `HANDLES.SEQ`, `SEQREAD.SEQ` and the rest); read those files for worked examples.

Background is in [kernel-design.md](kernel-design.md). The original source is at the git tag `fpc-3.6-original` (`git show fpc-3.6-original:SRC/FILE.SEQ`).

## Ground rules

- **Port in place, minimally.** Keep the structure, word names, comments and stack comments, and change only what has to change.
  - Add `F-PC native port` to the header line.
  - When something non-obvious changed, add a short comment saying what and why.
- **Do not edit `vm/`** (C), the kernel files, or `F-PC.SEQ`. If you need a new C primitive, write the word in high-level Forth if you reasonably can. Otherwise list it under "Built-ins needed" in your notes file, with its name, stack effect and exact semantics, and declare it with `BUILTIN name` where the CODE word was.
- **Leave work you can't finish as a `\ TODO-PORT:` comment** saying what's missing, and list it in your notes.
- Source files may use either CRLF or LF line endings. Keep each file's existing endings, or use LF.
- The `FILES DEFINITIONS VARIABLE X.SEQ FORTH DEFINITIONS` preamble at the top of the original files is not needed (F-PC's `FLOAD` records files itself), so delete it.

## 1. Cells are 32 bits

| Original | Port |
|---|---|
| `2+` `2-` `2*` `2/` used to step or scale **cells** (addresses of cells, cell counts) | `CELL+` `CELL-` `CELLS` `CELL /` |
| `2+` used as plain arithmetic, or as a byte offset into a string | unchanged |
| `2 ALLOT` (one cell), `n 2* ALLOT` (n cells) | `CELL ALLOT`, `n CELLS ALLOT` |
| Fixed structure offsets (`4 +` to the third cell of a body) | recompute for 4-byte cells (`2 CELLS +`) |
| `$FFFF` meaning true or all-ones, `$8000` sign-bit tests | `TRUE` / `-1`, `$80000000` (or rethink) |
| A double laid out as `VARIABLE X 2 ALLOT` | `2VARIABLE X` (the high cell is at the lower address, as in F-PC) |
| `0 $FFFF` style 16-bit range limits | reconsider; usually just remove the clamp |

- **Things that stay 16-bit.** `FLIP` `SPLIT` `JOIN` still work on the low 16 bits; they are used for key codes (scan*256+ascii), attribute bytes and cursor shapes, so keep them. `W@` and `W!` access 16 bits.
- **Doubles are 64 bits.** Most double arithmetic keeps working. Watch for code that relies on 16-bit overflow, such as `UM* DROP` to take a low half.
- **Divide by zero raises `DIV0FUNC`** ("Divide OVERFLOW error"), as before.
- **`CMOVE` is a byte-wise ascending move.** The original moved 16-bit words. Code that depended on word moves (rare) should be noted.

## 2. Memory is flat (no segments)

Every address is one cell. Code space, list space (colon-definition token lists), head space (headers) and the heap are regions of one address space, so `@ ! C@ C! CMOVE FILL` work everywhere.

**Removed words, and what to use instead:**

| Original | Port |
|---|---|
| `seg off @L` / `!L` / `C@L` / `C!L` | `addr @` / `!` / `C@` / `C!` |
| `sseg soff dseg doff n CMOVEL` / `CMOVEL>` | `src dst n CMOVE` / `CMOVE>` (or `MOVE`) |
| `seg off n c LFILL` | `addr n c FILL` |
| `seg a COUNTL` | `a COUNT` |
| `seg off len a1 PLACEL` / `+PLACEL` | `off len a1 PLACE` / `+PLACE` |
| `seg a n TYPEL` | `a n TYPE` |
| `?CS:` `?DS:` `?ES:` `SSEG` `ES0` | nothing: drop the segment value |
| `XSEG` `YSEG` `XDPSEG` `+XSEG` `PARAGRAPH` `DPARAGRAPH` `U16/` `UD16/` `CMOVE-PARS` `#PARS` | gone; rework the code with byte addresses |
| `Y@` `Y!` `YC@` `YC!` `YS:` `YCSET` | `@` `!` `C@` `C!` (nothing) `CSET` |
| `XHERE ( -- seg off )` | `XHERE ( -- addr )` |
| `EXHREAD` / `EXHWRITE ( a n hcb seg -- len )` | `HREAD` / `HWRITE ( a n hcb -- len )` |
| `ALLOC` `DEALLOC` `SETBLOCK` (DOS paragraphs) | `ALLOCATE ( n -- addr ior )` `FREE ( addr -- ior )` `RESIZE ( addr n -- addr' ior )`, in bytes |
| `name ( POINTER )` returned a paragraph | returns an **address**; sizes are in bytes |
| Video memory `$B800`/`$B000`, `VIDEO-SEG @` | `VIDEO-BUF ( -- addr )`, see §5 |

**Colon definitions and list space.**
- A colon definition's code field holds the token `CT-NEST`. Its body is **one cell, the address of its token list**: `' word >BODY @` is the list address, and the original `>BODY @ +XSEG` becomes `>BODY @`.
- The token list is a sequence of xts (cells). After `(LIT)`, `<'>`, `BRANCH`, `?BRANCH`, `(DO)`, `(LOOP)` and the like comes one in-line cell; branch targets are **absolute list addresses**.
- In-line strings after `(.")`, `(")`, `(ABORT")` and `(X")` are counted strings padded to a cell boundary.
- Bodies are no longer paragraph-aligned (`XALIGN` aligns to a cell).

**The return stack holds one cell per nesting level**: the flat IP. The original pushed ES and IP. Rewrites:

| Original | Port |
|---|---|
| `2R@ @L` (read the in-line cell after the caller's IP) | `R@ @` |
| `R> 2+ >R` (skip it) | `R> CELL+ >R` |
| `2R> ... 2>R` around in-line data | `R> ... >R` |
| Skip an in-line counted string | `R> COUNT 2DUP + ALIGNED >R` |
| A `DO` frame | 3 cells: leave-address, biased limit, biased index (`UNDO` drops it) |

## 3. Code fields are tokens

- A word's code field is **one cell**, so `>BODY` is `CELL+` and `BODY>` is `CELL-`.
- The cell holds a code token. Compare it with the constants `CT-NEST` `CT-DOVAR` `CT-DOCONST` `CT-DOVALUE` `CT-DODEFER` `CT-DOUSER` `CT-DOUSERDEFER`, for example `' X @ CT-NEST =` (is X a colon definition?).
- Every `DOES>` clause gets its own token, so "is this a VOCABULARY?" style tests need another approach (e.g. compare with the token of a known instance: `' FORTH @`).
- Gone: `@REL>ABS`, `C@ 232 =` / `233` (CALL/JMP) tests, `,CALL`, `,JUMP`, `;USES`, `;CODE`, and code-field patching with machine code.
- `!CT ( ct cfa -- )` changes a word's code token.
- Words now are `CREATE … ,` without ALIGN worries: `HEADER`/`CREATE` align `HERE` to a cell. `ALIGN` is a real word now; `EVEN` is a no-op.

## 4. CODE words

There is no 8086 assembler.
- **Rewrite a `CODE` word in high-level Forth** when it is reasonably short or not speed-critical, and keep the original comment.
- **Declare it as `BUILTIN NAME ( stack )`** when it is a performance-critical primitive or needs host access, and record the semantics in your notes.
- Machine-code tricks (patching `>NEXT`, jumping into the middle of other words, self-modifying code) need a rethink. Describe them in your notes.
- Native `CODE` words will come back later as an abstract-machine assembler (kernel-design.md §5). Don't try to anticipate it.

## 5. DOS, BIOS and hardware

DOS:
- `BDOS` `<BDOS> ( n fun -- al )`, `BDOS2 ( cx dx ax -- cx dx ax )`, `HDOS1 ( cx dx ax -- ax cf )` and `HDOS4 ( bx cx dx ax -- ax cf )` emulate INT 21h.
- They cover files (3C–43, 47 getcwd, 4E/4F find, 56 rename, 57 date), directories (39–3B), DTA (1A/2F), date/time (2A/2C), the version, and console I/O.
- DX and the other pointer registers are **flat addresses**. Paths are DOS style and are mapped to the host's files case-insensitively.
- `PDOS ( addr drive -- f )` gets the current directory.
- `FIND-FIRST ( asciiz attr -- f )`, `FIND-NEXT ( -- f )` and `DTA@`/`DTA!`/`SET-DTA` (single addresses) work with a DOS-layout DTA. Only names that fit 8.3 are found.
- `SYSTEM ( a n -- rc )` runs a host shell command, and `<EXTEXEC>` runs `EXEC$`.

Screen:
- **`VIDEO-BUF ( -- addr )`** is the screen: `COLS*ROWS` cells of (char, attribute) bytes, row stride `COLS 2*`, the same layout as `$B800`.
- **Fetch `VIDEO-BUF` every time.** It moves when the terminal is resized, and so do `COLS` and `ROWS` (VALUEs).
- Direct video writes become plain `C!`/`CMOVE`/`FILL` into it. The host redraws changed cells when the program waits for a key (or calls `REFRESH`).
- **Never assume 80x25.** Use `COLS` and `ROWS`. Code that lays out the screen should recompute on resize: add to the `RESIZED` deferred chain and/or handle the key `K-RESIZE` (255) in key loops.
- `BIOS-VIDEO ( ax bx cx dx -- ax bx cx dx )` emulates INT 10h AH = 00 01 02 03 06 07 08 09 0A 0E 0F. That's enough to translate `INT $10` sequences directly.
- Also: `AT-XY ( x y -- )`, `GET-XY ( -- x y )`, `SET-CURSOR`/`GET-CURSOR` (shape), `VIDEO-TYPE ( a n -- )` (at `#OUT`/`#LINE` with `ATTRIB`), `?VMODE` (3), `ATTRIB`.

Keyboard and other hardware:
- `BIOSKEY` and `BIOSKEY?` work as before. `SHIFT-STATE ( -- flags )` replaces reading `0:417` / `$40:17`, with the same bits as INT 16h AH=2.
- **No hardware.** `PC@ P@` return $FF and `PC! P!` do nothing. Interrupt vectors, EMS (`EMM-PRESENT?` is false), CGA snow handling, timer interrupts and the printer port are absent: stub them or drop them, and note it. `BEEP` should go through `BELL EMIT`.
- **Not provided:** the BIOS data area, the PSP and the environment segment. For the command tail use `DOS-LINE ( -- counted-string )`. For the environment, list a `GETENV` built-in in your notes if you need it.

## 6. Kernel facts you will need

| Word | Port value / behaviour |
|---|---|
| `B/HCB` | 140 |
| `HNDLOFFSET` | 136 (handle cell) |
| `>ATTRIB` | `132 +`, a cell |
| `>NAM` | `1+`; the name is a counted, NUL-terminated string |
| `B/FILENAME` | 127 (it was 64: host paths are longer) |

Use the constants and `>HNDLE`/`>ATTRIB`/`>NAM`, never literal offsets.
| `HANDLE name` | creates one |
| User variables (TOS ENTRY LINK SP0 RP0 DP OFFSET BASE HLD PRINTING) and user defers (EMIT KEY? KEY TYPE) | VM-provided |
| `UP` | a VARIABLE holding the user-area address |
| Defining words | `CONSTANT VALUE VARIABLE ARRAY DEFER CREATE DOES> VOCABULARY 2CONSTANT 2VARIABLE`, plus USER versions in the `USER` vocabulary |
| `!>` `+!>` `@>` `INCR>` `DECR>` `ON>` `OFF>` `&>` `SAVE!>` `SAVE>` `RESTORE>` | as before |
| Header | VFA cell, LFA cell, NFA (count\|$80, name, last char\|$80), CFA cell |
| `N>LINK` `L>NAME` `>VIEW` `VIEW>` | cell steps |
| `NAME>` | `1 TRAVERSE 1+ @` |
| `>NAME` | searches all vocabularies |
| Vocabulary body | 64 thread cells then a voc-link cell |
| `HASH ( str voc -- thread )` | as before |

The full list of built-ins is in `vm/prims.def` (the quoted names). Run `vm/fpc` and type `WORDS` to see what is defined.

## 7. Testing a file

- **Run the system.** `cd SRC; ../vm/fpc` gives F-PC's `ok` prompt with the ported kernel loaded. `../vm/fpc --batch - FLOAD FILE.SEQ BYE </dev/null` loads a file non-interactively and shows F-PC's error report (file, line, caret) if anything fails.
- **Load prerequisites first.** Files depend on files loaded earlier in `F-PC.SEQ`. Load those, if they are already ported, with more `FLOAD`s on the same line, for example `- FLOAD UTILS.SEQ FLOAD DECOM.SEQ BYE`. The DOS command line is limited to 127 characters, so use a small driver `.SEQ` in the scratch directory for longer lists.
- **Words from files that aren't ported yet won't exist.** That's expected; note it in your notes instead of working around it.
- **Never edit files outside your assignment**, even to make a test pass.
