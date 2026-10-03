# F-PC 3.6 memory model and cell-size inventory

Purpose: this inventory covers the memory-model and cell-size facts behind the C-VM port decision (flat address space; 16-bit cells first, or 32-bit cells directly).
All `file:line` references point to the CRLF/^Z-stripped sources (line numbers match the originals). The scan tooling is described in the Method section at the end. Counts come from a token scan with comments removed, so they are **approximate and lean high**. The cited lines were checked by hand.

Build sets used throughout:

| set | files |
|---|---|
| **kernel** (→ KERNEL.COM) | `SRC/META86.SEQ`, plus the files it FLOADs (`META86.SEQ:674-689`): KERNEL1, VIDEO, KERNEL2, VIDEO2, KERNEL3, EXPAND, EMMEXEC, POINTER, EQUCOLON, SAVEREST, HANDLES, SEQREAD, FPATH, DEFAULT, HCRITICA, KERNEL4 |
| **ext** (→ F-PC.EXE) | the 88 files FLOADed by `SRC/F-PC.SEQ` (TIMER … SVSESDAT) |
| **unl** | SRC files in neither build: F-PC.SEQ, F-PCH.SEQ (alternate load list), INDEX, MULTASK, PARSORT, SAVESYS, SIZES, XEXPECT |
| **tools** | `TOOLS/*.SEQ` (78 files) |

Note: KERNEL.COM is more than KERNEL1-4. VIDEO, POINTER, HANDLES, SEQREAD and the other files above are compiled into it as well. META86.SEQ is host-side metacompiler code that runs *on F-PC*. Its `-T`/`-X`/`-Y` words (`META86.SEQ:166-227`) manipulate the target image through `@L`/`!L`, so they count as segment users too.

Size of the problem (non-blank, non-comment lines):

| set | total lines | inside CODE/LABEL bodies (x86 asm) | high-level |
|---|---|---|---|
| kernel | 4,914 | 2,486 (295 CODE/LABEL words) | 2,428 |
| ext | 10,881 | 564 (54 CODE words, plus the PASM assembler itself) | 10,317 |
| unl | 788 | 40 | 748 |
| tools | 13,176 | 5,456 (264 CODE words; FFLOAT alone has 70) | 7,720 |

Every CODE word must be rewritten as a C primitive (or dropped), **whatever cell size is chosen**. Half of the kernel's lines are 8086 assembler.

---

## 1. Segment words

### 1.1 Physical memory layout (as set up by COLD)

`CORIG` (`KERNEL4.SEQ:279-366`) runs with CS=DS=SS and builds this layout:

```
CS:0000 ............ code space: #CODESEGS paras (KERNEL2.SEQ:488, $1000 = 64K)
                     data stack, TIB, return stack and FIRST/LIMIT sit at the top of
                     this 64K (KERNEL4.SEQ:339-359)
+#CODESEGS ......... overlay space: #OVSEGS paras (KERNEL2.SEQ:494, default 0)
+#OVSEGS ........... LIST space  = XSEG:  #LISTSEGS paras (KERNEL2.SEQ:489, $2000 = 128K)
+#LISTSEGS ......... HEAD space  = YSEG:  #HEADSEGS paras (KERNEL2.SEQ:490, $1000 = 64K)
CS+#PARS ........... POINTER heap: SETBLOCK grows the DOS block (POINTER.SEQ:37-41)
```

- On cold start, list and head images are copied out of the .COM file. The list comes from `DPSTART`, with length `XSEGLEN*16` (`KERNEL4.SEQ:306-326`). The head comes from `YSTART`, with length `YDP` (`:288-304`).
- `XSEG` and `YSEG` are plain variables in code space (`KERNEL1.SEQ:69-70`) that hold **absolute paragraph numbers**.
- COLD **self-modifies** NEST and DODOES so that they add XSEG as an immediate (`KERNEL4.SEQ:331-333`, patching `KERNEL1.SEQ:125-126` and `:168-169`).
- List space is larger than 64K (128K by default). That is why bodies store paragraph numbers instead of offsets.

### 1.2 Definitions: words that take or return a segment or a seg:off pair

**Far-memory primitives (seg:off on the stack).** Defined in KERNEL2 unless noted.

| word | stack | def |
|---|---|---|
| `@L` `!L` `C@L` `C!L` | `( seg off -- n )` etc. | KERNEL2.SEQ:307, 325, 313, 319 |
| `CMOVEL` `CMOVEL>` | `( sseg soff dseg doff n -- )` | KERNEL2.SEQ:412, 433 |
| `LFILL` | `( seg off n c -- )` | KERNEL2.SEQ:80 |
| `LFILLW` | `( seg off bytes word -- )` | IBMCURSR.SEQ:55 (ext) |
| `COUNTL` | `( seg a -- seg a+1 n )` | KERNEL2.SEQ:107 |
| `PLACEL` `+PLACEL` | `( seg off n a -- )` | KERNEL3.SEQ:28, 32 |
| `TYPEL` (deferred) / `(TYPEL)` `CONSOLEL` `PRINTL` `PRNTYPEL` `CONTYPEL` | `( seg a n -- )` | KERNEL2.SEQ:26 (user DEFER); SEQREAD.SEQ:49-73 |
| `VIDEO-TYPEL` | `( seg a n -- )` | VIDEO2.SEQ:16 |
| `QTYPEL` `\typeL` `hypertypeL` `prtypeL` `exsltypel` | `( seg a n -- )` | QVIDEO:15, PERTYPE:76, HTYPE:3, BROWSEPR:80, SEDITOR:596 (ext) |
| `EXHREAD` `EXHWRITE` | `( a n handle seg -- len )` | HANDLES.SEQ:276, 281 |
| `DTA@` `DTA!` | `( -- seg off )` | HANDLES.SEQ:331, 340 |
| `?CS:` `?DS:` `?ES:` | `( -- seg )` | KERNEL2.SEQ:295, 299, 303 |
| `SSEG` (var: segment used by COMP/CAPS-COMP/SEARCH) | | KERNEL2.SEQ:58, used at :256, :273 |
| `ES0` (user var: initial ES) | | KERNEL2.SEQ:13 |
| `c+!L` `e@` `e!` `calloc` `free` | | TOOLS/UNREF:28, TOOLS/WINDOW:84-124 |

**List space (XSEG).**

| word | meaning | def |
|---|---|---|
| `XSEG` | absolute para of list base | KERNEL1.SEQ:69 |
| `XDPSEG` `XDP` | list HERE as seg:off | KERNEL2.SEQ:52, 51 |
| `XHERE` | `( -- seg off )` | KERNEL2.SEQ:669 |
| `X,` `XC,` | append to list (DS:=XDPSEG) | KERNEL2.SEQ:674, 685 |
| `+XSEG` | `( relpara -- abspara )` | KERNEL2.SEQ:562 |
| `XSEGLEN` `XMOVED` `DPSTART` | image relocation bookkeeping | KERNEL2.SEQ:56, 57, 55 |
| `XEVEN` `XALIGN` `X,"` `X>"BUF` | list-space strings | KERNEL3.SEQ:407-436 |
| `SETYSEG` | init XDPSEG from XSEGLEN | KERNEL2.SEQ:571 |
| `SPCHECK` | head/list overflow test in paragraphs | KERNEL3.SEQ:608 |
| meta: `XS:` `HERE-X` `,-X` `C,-X` `@-X` `!-X` `PARAGRAPH-X` `>XREL` `DPSEG-X` `SVXSEG` | | META86.SEQ:185-223 |
| `tl:` `tl:@` `tl:!` `tl:+!` (SED line-pointer table segment) | | SEDCODE.SEQ:5-61 (ext) |

**Head space (YSEG).**

| word | def |
|---|---|
| `YSEG` | KERNEL1.SEQ:70 |
| `YDP` `YSTART` | KERNEL2.SEQ:53, 54 |
| `YHERE` `YS:` `Y@` `Y!` `YC@` `YC!` `Y,` `YCSET` `YHASH` | KERNEL2.SEQ:588-667 |
| `TRAVERSE` (ES:=YSEG) | KERNEL3.SEQ:16 |
| `CNHASH` `CNSRCH` | KERNEL3.SEQ:51, 54 (meta CNHASH META86.SEQ:229) |
| `N>LINK` `L>NAME` `NAME>` `LINK>` `>NAME` `>LINK` `>VIEW` `VIEW>` | KERNEL3.SEQ:89-124 |
| `(FIND)` (ES:=YSEG) | KERNEL3.SEQ:148 |
| `<"HEADER>` `,VIEW` `HIDE` `REVEAL` | KERNEL3.SEQ:638, 595, 680, 683 |
| meta: `YS:` `@-Y` `!-Y` `,-Y` `C,-Y` `CSET-Y` `HERE-Y` `SVYSEG` `HEADER` | META86.SEQ:207-227, 385 |

**DOS memory and paragraph math.**

| word | def |
|---|---|
| `DOS_ALLOC` `DOS_DEALLOC` `DOS_SETBLOCK` `DOS_MAXBLOCK` (INT 21h 48/49/4A) | KERNEL2.SEQ:517, 504, 532, 547 |
| `ALLOC` `DEALLOC` `SETBLOCK` `MAXBLOCK` (DEFERs) | KERNEL2.SEQ:551-554 |
| `PARAGRAPH` `DPARAGRAPH` `U16/` `UD16/` | KERNEL1.SEQ:1381, 1385, 952, 1250 |
| `CMOVE-PARS` `CMOVE-PARS>` | KERNEL2.SEQ:455, 470 |
| `#CODESEGS` `#LISTSEGS` `#HEADSEGS` `#OVSEGS` `#OVBYTES` | KERNEL2.SEQ:488-495 |
| `#PARS` `PHEAD` | KERNEL2.SEQ:60, 59 |
| `POINTER` (child returns a **paragraph**) `PTR_OK?EXIT` `%UNPOINTER` `UNPOINTER>` `0POINTERS` `SIZEOF!>` `SIZEOF@>` | POINTER.SEQ:29-113 (meta POINTER META86.SEQ:564) |
| `EMM-POINTER` | EMMPTR.SEQ:8 (ext) |
| `evseg` (environment segment) | ENVIRON.SEQ:3 (ext) |
| POINTER instances: `F-PC` KERNEL2:569, `INBSEG` SEQREAD:22, `baseseg` EDITSTUF:11, `macseg` MACROS:39, `xbseg` NEXPECT:34, `svseg` SAVESCR:5; tools: `myseg` SALLOC:16, `PROF_SEG` PROFILE:80, `refed` UNREF:33, `xtestbuf` XMS:576, `dir-seg` NEW-WFL:67 | |

**Video and BIOS.**

| word | def |
|---|---|
| `VIDEO-SEG` (var; `$B000` mono / `$B800` color) | VIDEO.SEQ:12, set at :44-48 |
| `?VMODE` (reads `$40:4A`, `$40:84`, `$40:60`) | VIDEO.SEQ:19-39 |
| `GET-CURSOR` (`0 $460 @L`) | KERNEL4.SEQ:475-476 |
| `vseg` (`B000 constant`) | TOOLS/WINDOW.SEQ:152 |

**Code-field and return-stack words that know about seg:off.** `NEST` and `DODOES` (`KERNEL1.SEQ:114`, `:156`) push **ES and IP** onto the return stack, so every nesting level costs 2 cells. Code that reads inline data therefore uses `2R@ @L` / `R> 2+ >R`. Examples: `COMPILE` `KERNEL3.SEQ:367-368`, `(")` `:419-420`, `%(.")` `:425-426`, `(ABORT")` `:470-475`, `X>"BUF` `:413-417`, `CRASH` `:387-388`, `(;USES)`/`(;CODE)` `:686-692`, `(IS)` `KERNEL4.SEQ:47-50`. A scan of `2R@ @L|2R>|R> 2+ >R` finds about **50 sites**: kernel 15, ext 27, unl 1, tools 7. The ext sites are in DEBUG, MACROS, NEWLAB, WFL, BROWSEPR, TIMESTUF and VALIDATE.

### 1.3 Header layout and how the head, code and list spaces link

**Header (head space, YSEG).** Built by `<"HEADER>` `KERNEL3.SEQ:638-653` and by the meta `HEADER` `META86.SEQ:385-398`.

```
 YSEG:0000-00FF   >NAME hash table: 128 cells, index = (CFA and $FE00) FLIP, i.e. CFA/512*2
                  (KERNEL3.SEQ:51-52; seeded KERNEL1.SEQ:40-42; DP-Y starts at 256 META86.SEQ:208)
 header n:
   +0  VFA  2 bytes  LOADLINE (,VIEW KERNEL3.SEQ:595)
   +2  LFA  2 bytes  head-offset of previous LFA in this vocab thread
                     (thread cell in code space points at the LFA: KERNEL3.SEQ:646)
   +4  NFA  1 byte   count|$80 (delimiter) |$40 (IMMEDIATE, KERNEL3.SEQ:371), len<=31
       name chars, last char |$80 (KERNEL3.SEQ:650)
   +k  CFA ptr 2 bytes  code-space address of the code field (KERNEL3.SEQ:651)
```

- `N>LINK` = `2-` and `L>NAME` = `2+` (`KERNEL3.SEQ:89-93`). `>VIEW` = `>LINK 2-` and `VIEW>` = `2+ LINK>` (`:120-124`).
- `NAME>` = `1 TRAVERSE 1+ Y@` (`:98-99`): it walks to the high-bit-terminated last char, then fetches the CFA cell.
- `>NAME` (`:112-115`) looks up two consecutive hash cells and linear-scans the headers between them with `CNSRCH`. `CNSRCH` (`:54-87`) hardcodes `add bx,#4` (skip VFA+LFA), `and ax,#31` and `add bx,#6` (CFA+VFA+LFA of the next header). This depends on headers being **contiguous and in CFA order**, and on code space being 64K (128 buckets × 512 bytes).
- `(FIND)` (`:148-195`) compares count and first char as **one 16-bit word** under mask `$7F3F` (`:160-162`). It picks up the CFA at `1 [BX]` after the last char (`:173`). `HASH` (`:137-146`) and `YHASH` (`KERNEL2.SEQ:649-667`) read two chars as a word at `1 [BX]`. Both are little-endian and 16-bit assumptions, but they live inside CODE.

**Code field (code space).** Every word starts with a 3-byte 8086 instruction.

| kind | code field | body | defined |
|---|---|---|---|
| colon | `E9 rel16` → NEST | **1 cell = list paragraph relative to XSEG** | `(:)` KERNEL3.SEQ:745-753; meta `:` KERNEL1.SEQ:270-274 |
| CREATE/VARIABLE | `E8 rel16` → `>NEXT` (CALL pushes PFA) | data | KERNEL3.SEQ:657-672, 771 |
| CONSTANT/VALUE | `E9 rel16` → DOCONSTANT/DOVALUE (read `3 [BX]`) | 1 cell | KERNEL3.SEQ:765-769; KERNEL1.SEQ:177-185 |
| DEFER | `E8` → DODEFER | cfa cell | KERNEL3.SEQ:777 |
| DOES> child | `E8 rel16` → stub in code space; the stub is `E8 rel16 → DODOES` + **list paragraph** | | KERNEL3.SEQ:694-698; KERNEL1.SEQ:156-173, 215-220 |
| USER VARIABLE/DEFER | `E8` → DOUSER-*; body = offset into user area | | KERNEL4.SEQ:23-36; KERNEL1.SEQ:187-191 |
| POINTER | `E8` → DOES; body `[para][link][size-paras][verify]` | | POINTER.SEQ:26-41 |

- `>BODY` = `3 +` and `BODY>` = `3 -` (`KERNEL3.SEQ:95-106`; meta `BODY_SIZE` `META86.SEQ:147`).
- The rel16 target is decoded with `DUP 1+ @ OVER >BODY +`. See `>IS` `KERNEL4.SEQ:42`, `(FRGET)` `KERNEL3.SEQ:461-462` (test for "is this a colon def"), `@REL>ABS` `KERNEL4.SEQ:261-267` and `DBGFIX.SEQ:28`.
- It is re-encoded with `3 + - R> 1+ !` (`KERNEL3.SEQ:687, 692`). `232`/`233` (CALL/JMP opcodes) appear about 45 times: kernel 16, ext 15, unl 3, tools 11.

**Colon definition (list space).** `XHERE PARAGRAPH + DUP XDPSEG ! XSEG @ - , XDP OFF` (`KERNEL3.SEQ:748-751`):

- Each colon body starts on a **fresh paragraph** (offset 0). Up to 15 bytes of padding are wasted per definition.
- The **relative** paragraph number is stored in the code-space body cell (`>BODY @`).
- The list itself is a sequence of 16-bit CFAs (`X,`) at `XSEG+rel : 0..`, ending with `UNNEST`. Inline literals and branch targets are 16-bit **offsets within that paragraph** (`>MARK`/`>RESOLVE` `KERNEL3.SEQ:490-500`, `BRANCH` `KERNEL1.SEQ:314-316` does `MOV IP, ES:0[IP]`).
- `NEST` (`KERNEL1.SEQ:114-131`) does `MOV AX,3[DI]; ADD AX,#XSEG; MOV ES,AX; SUB IP,IP`. A colon body therefore cannot exceed 64K, and a list "address" is always a (para, offset) pair.
- The same idiom is copied outside the kernel. See Section 2.

### 1.4 Use counts per file

Columns (token counts on comment-stripped lines; one line may count in several columns):

- **a** list-space words (XSEG family, `X,`, `XHERE`, `+XSEG`, meta `-X` words, SED `tl:`)
- **b** head-space words (Y family, header navigation `>NAME` `N>LINK` …, meta `-Y` words)
- **c** video (`VIDEO-SEG`, `$B800`/`$B000`, `VIDEO-TYPEL`, `QTYPEL`, `vseg`)
- **d** DOS allocation and paragraph math (`ALLOC`/`DEALLOC`/`SETBLOCK`/`MAXBLOCK`, `PARAGRAPH`, `POINTER` and its instances, `#…SEGS`, `#PARS`, `U16/`, `CMOVE-PARS`)
- **e** BIOS data area or interrupt vectors (lines with `0|$40 <off> @L/C@L`, asm `MOV` of `$41x-$49x`, `MOV AX,#$40`, `INT 21h` fn `$25xx`/`$35xx`). This regex has a few false positives (EXPAND.SEQ:55 is an EMM function code).
- **f** "other": far-memory primitive tokens on lines that matched none of a–e. Usually a segment held in a local VALUE (e.g. SED `tsegb`/`wseg`/`lseg`, NEWLAB `labseg`, `?CS:`, `SSEG`, `INBSEG`-style buffers).
- **far** all far-primitive tokens (`@L !L C@L C!L CMOVEL CMOVEL> LFILL COUNTL *TYPEL PLACEL ?CS: ?DS: SSEG EXHREAD EXHWRITE DTA@ DTA!` …), whatever their category
- **asmseg** asm lines touching segment registers (`ES:`/`CS:`/`DS:` overrides, `MOV DS|ES|SS,`, `PUSH/POP DS|ES`). In TOOLS this count includes the assembler and disassembler tables themselves.

*Kernel (META86 + files it FLOADs → KERNEL.COM)*

| file | a | b | c | d | e | f | far | asmseg |
|---|---|---|---|---|---|---|---|---|
| SRC/META86.SEQ | 70 | 58 | · | 8 | · | 24 | 50 | 6 |
| SRC/KERNEL1.SEQ | 42 | 7 | · | 6 | 3 | 4 | 4 | 44 |
| SRC/VIDEO.SEQ | · | · | 4 | · | 1 | · | · | 2 |
| SRC/KERNEL2.SEQ | 24 | 23 | · | 34 | · | 23 | 24 | 73 |
| SRC/VIDEO2.SEQ | · | · | 3 | · | · | · | · | 6 |
| SRC/KERNEL3.SEQ | 57 | 63 | · | 5 | · | 9 | 23 | 21 |
| SRC/EXPAND.SEQ | · | · | · | · | 1 | · | · | 2 |
| SRC/EMMEXEC.SEQ | · | · | · | 5 | · | 15 | 17 | 28 |
| SRC/POINTER.SEQ | 3 | 3 | · | 32 | · | · | 4 | 5 |
| SRC/EQUCOLON.SEQ | 8 | · | · | · | · | · | · | 10 |
| SRC/SAVEREST.SEQ | 3 | · | · | · | · | · | · | 6 |
| SRC/HANDLES.SEQ | · | · | · | · | · | 6 | 6 | 10 |
| SRC/SEQREAD.SEQ | · | · | · | 11 | · | 17 | 21 | 9 |
| SRC/HCRITICA.SEQ | · | · | · | · | 1 | 1 | 1 | 3 |
| SRC/KERNEL4.SEQ | 21 | 15 | · | 9 | 8 | 6 | 10 | 18 |
| **total** | **228** | **169** | **7** | **110** | **14** | **105** | **160** | **243** |

*Extensions (FLOADed by F-PC.SEQ → F-PC.EXE)*

| file | a | b | c | d | e | f | far | asmseg |
|---|---|---|---|---|---|---|---|---|
| SRC/BOXTEXT.SEQ | 2 | · | · | · | · | · | · | · |
| SRC/BROWSEPR.SEQ | · | · | 4 | · | · | 5 | 9 | 3 |
| SRC/COLORIZE.SEQ | · | 1 | · | · | · | · | · | · |
| SRC/DEBUG.SEQ | 2 | · | · | · | · | 2 | 2 | 1 |
| SRC/DECOM.SEQ | 1 | 7 | · | · | · | 6 | 6 | 1 |
| SRC/DEFERS.SEQ | 2 | · | · | · | · | · | 1 | · |
| SRC/DUMP.SEQ | 1 | 1 | · | · | · | 2 | 2 | 1 |
| SRC/EDITSET.SEQ | · | · | · | 2 | · | · | · | · |
| SRC/EDITSTUF.SEQ | 3 | 13 | · | 20 | · | 8 | 14 | 2 |
| SRC/EMMPTR.SEQ | · | · | · | 2 | · | · | · | · |
| SRC/ENVIRON.SEQ | · | · | · | 12 | · | 1 | 12 | 1 |
| SRC/EXEC.SEQ | · | · | · | 3 | · | 5 | 5 | 3 |
| SRC/FWORDS.SEQ | · | · | · | · | · | 1 | 1 | · |
| SRC/HELLO.SEQ | 6 | 1 | · | 3 | · | · | · | · |
| SRC/HTYPE.SEQ | · | · | · | · | · | 12 | 12 | · |
| SRC/IBMCURSR.SEQ | · | · | 1 | · | · | 2 | 2 | 2 |
| SRC/LEDIT.SEQ | 8 | · | · | 1 | · | · | 3 | · |
| SRC/LISTSET.SEQ | 2 | · | · | 4 | · | · | · | · |
| SRC/LOADEXE.SEQ | 5 | 3 | · | 5 | · | · | · | 9 |
| SRC/MACROS.SEQ | · | · | · | 11 | · | 7 | 13 | · |
| SRC/MENUS.SEQ | 7 | · | · | 3 | · | · | 2 | · |
| SRC/MONOCROM.SEQ | · | · | 1 | · | · | 3 | 3 | · |
| SRC/MOUSE.SEQ | · | · | · | · | 1 | · | 1 | · |
| SRC/MOUSEY.SEQ | 1 | · | · | · | · | · | 1 | · |
| SRC/NEWLAB.SEQ | · | · | · | 2 | · | 34 | 34 | · |
| SRC/NEXPECT.SEQ | · | · | · | 18 | 1 | · | 28 | 8 |
| SRC/PASM.SEQ | 1 | · | · | · | · | 1 | 1 | 7 |
| SRC/PERTYPE.SEQ | 1 | · | · | · | · | 21 | 23 | 2 |
| SRC/PRINTING.SEQ | · | · | · | · | · | 3 | 3 | 1 |
| SRC/QVIDEO.SEQ | · | · | 3 | · | · | 3 | 4 | · |
| SRC/REF.SEQ | 1 | 4 | · | · | · | 2 | 3 | 1 |
| SRC/SAVEEXE.SEQ | 13 | 10 | · | 7 | · | 2 | 3 | · |
| SRC/SAVESCR.SEQ | · | · | 4 | 15 | · | · | 6 | · |
| SRC/SCAN.SEQ | · | · | · | · | · | 3 | 3 | 7 |
| SRC/SEARCH.SEQ | · | · | · | · | · | 1 | 1 | 5 |
| SRC/SEDCHARS.SEQ | 29 | · | · | 1 | · | 4 | 6 | · |
| SRC/SEDCODE.SEQ | 8 | · | · | · | · | · | · | 12 |
| SRC/SEDIT2.SEQ | 4 | 2 | · | · | · | 4 | 8 | · |
| SRC/SEDITOR.SEQ | 6 | · | · | 10 | 6 | 63 | 72 | 10 |
| SRC/SEDITWP.SEQ | · | · | · | · | · | 2 | 2 | · |
| SRC/SEDJUST.SEQ | · | · | · | 1 | · | · | · | · |
| SRC/SEDMENU.SEQ | · | · | · | 2 | · | · | · | · |
| SRC/SEDPAGE.SEQ | · | · | · | 1 | · | · | · | · |
| SRC/SEDSHELL.SEQ | · | · | · | 7 | · | 5 | 9 | 3 |
| SRC/SEDSORT.SEQ | · | · | · | 1 | · | 2 | 2 | 1 |
| SRC/SEDWHELP.SEQ | · | 3 | · | · | · | 10 | 11 | · |
| SRC/STATUS.SEQ | 2 | 3 | · | 1 | · | · | · | · |
| SRC/SVSESDAT.SEQ | · | · | · | 2 | · | 3 | 3 | 1 |
| SRC/TOPEDIT.SEQ | 1 | · | · | 2 | · | 16 | 16 | · |
| SRC/UNHEAD.SEQ | · | 10 | · | 2 | · | · | · | · |
| SRC/UTILS.SEQ | 5 | 14 | · | 9 | · | · | · | · |
| SRC/VALIDATE.SEQ | · | · | · | 3 | · | · | · | · |
| SRC/VIEW.SEQ | · | 16 | · | · | · | · | 3 | 1 |
| SRC/VOCABS.SEQ | · | 3 | · | · | · | · | · | · |
| SRC/WATCHER.SEQ | · | · | · | · | · | 2 | 2 | 2 |
| SRC/WFL.SEQ | 2 | · | · | 2 | · | 22 | 24 | 5 |
| SRC/WINSTACK.SEQ | · | · | · | · | 2 | · | 2 | · |
| SRC/WORDS.SEQ | · | 6 | · | · | · | · | 2 | 1 |
| **total** | **113** | **97** | **13** | **152** | **10** | **257** | **360** | **90** |

*SRC files not in either build (F-PC.SEQ/F-PCH.SEQ load lists, INDEX, MULTASK, PARSORT, SAVESYS, SIZES, XEXPECT)*

| file | a | b | c | d | e | f | far | asmseg |
|---|---|---|---|---|---|---|---|---|
| SRC/F-PC.SEQ | · | 1 | · | 2 | · | · | · | · |
| SRC/F-PCH.SEQ | · | 1 | · | 2 | · | · | · | · |
| SRC/MULTASK.SEQ | 5 | 1 | · | 2 | · | 1 | 1 | 3 |
| SRC/PARSORT.SEQ | · | · | · | 1 | · | · | · | · |
| SRC/SAVESYS.SEQ | 14 | 5 | · | 5 | · | · | 4 | 2 |
| SRC/SIZES.SEQ | 7 | · | · | 3 | · | 12 | 12 | · |
| SRC/XEXPECT.SEQ | · | · | · | 8 | · | · | 8 | 3 |
| **total** | **26** | **8** | **0** | **23** | **0** | **13** | **25** | **8** |

*TOOLS/ (optional add-ons)*

| file | a | b | c | d | e | f | far | asmseg |
|---|---|---|---|---|---|---|---|---|
| TOOLS/ANSI.SEQ | 7 | 1 | · | 6 | · | · | · | · |
| TOOLS/AUTOFOR.SEQ | 2 | 22 | · | · | · | · | 3 | 1 |
| TOOLS/CODEBUG.SEQ | 1 | 3 | · | 3 | 7 | 10 | 10 | 43 |
| TOOLS/CODEHIGH.SEQ | 4 | · | · | 1 | · | · | · | 1 |
| TOOLS/COMPLEX.SEQ | · | 1 | · | · | · | · | · | · |
| TOOLS/DIS8086.SEQ | · | 16 | · | · | · | 6 | 6 | 6 |
| TOOLS/DISASSEM.SEQ | · | 3 | · | · | · | 5 | 5 | 3 |
| TOOLS/EMMEXMPL.SEQ | · | · | 3 | · | · | 1 | 4 | · |
| TOOLS/EVAL.SEQ | 2 | · | · | · | · | · | · | · |
| TOOLS/FASSEM.SEQ | · | · | · | · | · | 1 | 1 | 4 |
| TOOLS/FFLOAT.SEQ | 2 | 4 | · | · | · | · | 1 | 36 |
| TOOLS/FUNKEY.SEQ | · | 1 | · | · | · | · | · | · |
| TOOLS/LISTED.SEQ | · | 2 | · | · | · | · | · | · |
| TOOLS/LOCALS.SEQ | 1 | · | · | · | · | · | · | · |
| TOOLS/MIDNIGHT.SEQ | · | 2 | · | · | · | · | · | · |
| TOOLS/MONITOR.SEQ | 1 | · | · | · | · | · | 1 | · |
| TOOLS/MORE.SEQ | · | · | · | 3 | · | · | · | · |
| TOOLS/NEW-WFL.SEQ | · | · | · | 9 | · | 6 | 6 | 50 |
| TOOLS/OBJECT.SEQ | 4 | 2 | · | 1 | · | · | · | · |
| TOOLS/OVERLAY.SEQ | 6 | · | · | 14 | · | 1 | 3 | · |
| TOOLS/PICTURE.SEQ | · | · | · | 1 | · | · | · | · |
| TOOLS/PROFILE.SEQ | · | 2 | · | 15 | 3 | 10 | 17 | 21 |
| TOOLS/RS232IB.SEQ | · | · | · | · | 6 | 5 | 5 | 29 |
| TOOLS/SALLOC.SEQ | · | · | · | 10 | · | 5 | 5 | 2 |
| TOOLS/SELECT.SEQ | · | · | · | · | · | 6 | 6 | 3 |
| TOOLS/SETJMP.SEQ | · | · | · | 2 | · | · | · | · |
| TOOLS/SFLOAT1.SEQ | · | 5 | · | · | · | · | · | · |
| TOOLS/SFLOAT2.SEQ | · | 7 | · | · | · | · | · | 7 |
| TOOLS/SFLOAT3.SEQ | 4 | 12 | · | · | · | · | · | 3 |
| TOOLS/SPREAD.SEQ | 1 | 1 | · | · | · | · | · | · |
| TOOLS/UNLINK.SEQ | 4 | · | · | · | · | · | 4 | · |
| TOOLS/UNREF.SEQ | · | 4 | · | 12 | · | 3 | 13 | 2 |
| TOOLS/VMOVE.SEQ | · | 10 | · | · | · | · | · | · |
| TOOLS/WINDEX.SEQ | · | 12 | · | · | · | · | · | · |
| TOOLS/WINDOW.SEQ | · | · | 4 | 8 | · | 15 | 16 | 13 |
| TOOLS/XMS.SEQ | · | 4 | · | 11 | · | 2 | 3 | 13 |
| **total** | **39** | **114** | **7** | **96** | **16** | **76** | **109** | **237** |

**Group totals (a / b / c / d / e / f):**

| set | a list | b head | c video | d DOS/para | e BIOS/IVT | f other | far tokens | asm seg-reg lines |
|---|---|---|---|---|---|---|---|---|
| kernel | 228 | 169 | 7 | 110 | 14 | 105 | 160 | 243 |
| ext | 113 | 97 | 13 | 152 | 10 | 257 | 360 | 90 |
| unl | 26 | 8 | 0 | 23 | 0 | 13 | 25 | 8 |
| tools | 39 | 114 | 7 | 96 | 16 | 76 | 109 | 237 |

Notable users by pattern:

- **(a) list space.** Outside the kernel these are: HELLO.SEQ:54-57 (a defining word that builds its own list body), LEDIT.SEQ:262-289, MENUS.SEQ:22, 44-45, 138, SEDIT2.SEQ:104-143 (key tables in list space holding **absolute** segments), SEDCHARS.SEQ:12-73 and MOUSEY.SEQ:131 (`extcharseg +xseg`), SAVEEXE.SEQ:74-165, LOADEXE.SEQ:83 and SEDCODE.SEQ (`tl:`). Tools: UNLINK, OVERLAY, CODEHIGH, OBJECT, ANSI:83, LOCALS:55, MONITOR:218.
- **(b) head space.** UTILS (`YHERE`, `>NAME`), VIEW, WORDS, UNHEAD, DECOM, REF, SAVEEXE. In TOOLS: AUTOFOR.SEQ:56-62 (a copy of `<"HEADER>`), WINDEX, VMOVE, DIS8086 and SFLOAT*. In SFLOAT* this is mostly `LAST @ NAME>`.
- **(c) video.** VIDEO.SEQ:44-48, VIDEO2.SEQ:75, SAVESCR.SEQ:30-52, MONOCROM.SEQ:49, IBMCURSR.SEQ:78, QVIDEO, BROWSEPR. Tools: EMMEXMPL.SEQ:98-106 and WINDOW.SEQ:152-221.
- **(d) DOS buffers.** POINTER.SEQ (heap manager), SEQREAD.SEQ:22, 114-158 (`INBSEG` file buffer), EDITSTUF.SEQ:100-124 (SED carves one POINTER into hseg/lseg/dseg/wseg/tsegb by paragraph), SAVESCR, MACROS, NEXPECT, ENVIRON.SEQ:3-7, EXEC/SEDSHELL (shell-out memory shrink), UTILS.SEQ:300-335 (memory report). Tools: WINDOW, SALLOC, PROFILE, UNREF, XMS, OVERLAY.
- **(e) BIOS data area / IVT.** KERNEL1.SEQ:82-93 (`0:$471` break flag, `0:$417` shift state), VIDEO.SEQ:25-29, KERNEL4.SEQ:147-163, 221-253 (INT 21h 25/35 vectors), KERNEL4.SEQ:476 (`0 $460 @L`), SEDITOR.SEQ:97-100, 165-166 (`0 $417 c@l`, `0 $41A/$41C @L` keyboard buffer), WINSTACK.SEQ:26, 49 (`$40 $17 c@L`), MOUSE.SEQ:231 (`0 204 @L`, INT 33h vector). Tools: RS232IB (COM IRQ vectors), CODEBUG and PROFILE (INT 3 and timer).
- **(f) other.** SEDITOR.SEQ is the largest consumer (72 far tokens). Each SED text line is stored **at its own paragraph**: the line table holds 16-bit segment numbers (`placeline` SEDITOR.SEQ:236-246; `#lineseg` SEDCODE.SEQ:91). Others: NEWLAB (label tables in `labseg`), PERTYPE/HTYPE/TOPEDIT (`TYPEL` users), WFL (directory buffer).

---

## 2. Paragraph arithmetic and relative-segment storage (breaks in a flat model)

These sites convert between bytes and paragraphs, or store segment numbers *as data*. In a flat model with byte addresses they all need rework. In a 16-bit VM that **emulates real-mode addressing** (phys = seg*16 + off), they work unchanged.

**Primitives and constants**

- `PARAGRAPH` = `15 + U16/` (KERNEL1.SEQ:1381-1383). `DPARAGRAPH` (:1385-1388). `U16/` (:952). `UD16/` (:1250). Meta `PARAGRAPH-X` (META86.SEQ:189). Meta `POINTER` does `15. D+ D2/ D2/ D2/ D2/` (META86.SEQ:570-571).
- `+XSEG` (KERNEL2.SEQ:562-565). `>XREL` = `SWAP SEG-X @ - 16 * +` (META86.SEQ:190-191).
- `CMOVE-PARS[>]` move in 10-paragraph/160-byte chunks using `10 10 D+` on seg:seg pairs (KERNEL2.SEQ:455-486).
- `#CODESEGS/#LISTSEGS/#HEADSEGS/#OVSEGS` sizes are in paragraphs (KERNEL2.SEQ:488-495). SETYSEG sums them (`:581-582`).
- `SPCHECK` shifts `YDP` right 4 and computes `XDPSEG - XSEG` (KERNEL3.SEQ:608-627).
- COLD shifts `XSEGLEN` left 4 and `#CODESEGS-1` left 4 (KERNEL4.SEQ:313-317, 339-345), adds `#…SEGS` to CS (`:291-293`, `:310-311`) and computes `COLDBODY 2- @ + XSEG → ES` (`:361-363`).
- Self-patching `ADD AX,#XSEG` in NEST/DODOES (KERNEL1.SEQ:125-126, 168-169; patched KERNEL4.SEQ:332-333).

**Stored relative paragraph numbers (the colon-body idiom `XHERE PARAGRAPH + DUP XDPSEG ! XSEG @ - ,`)**

- Kernel: `(:)` KERNEL3.SEQ:748-750, `MAKEDUMMY` :733-735, `DOES>` :697-698. Meta: `:` KERNEL1.SEQ:272-273, `DOES>` :219, `T:` META86.SEQ:468, `SVXSEG` :222, `SIZE-SET` :293.
- Ext: HELLO.SEQ:54-56, LEDIT.SEQ:262, MENUS.SEQ:22, SEDCHARS.SEQ:12 (the data block is built as a pseudo-body). Unl: SAVESYS.SEQ:23-37, SIZES.SEQ:40, 52. Tools: ANSI.SEQ:83, CODEHIGH.SEQ:35-38, OBJECT.SEQ:94-96.

**Readers of `>BODY @ +XSEG` (relative para → absolute segment)**

- Kernel: `(FRGET)` KERNEL3.SEQ:463.
- Meta: `SWITCH` META86.SEQ:162, `T:` DOES> :470.
- Ext: DEFERS.SEQ:42, DUMP.SEQ:61, DEBUG.SEQ:247 (and `seg>cfa` :192-193 inverts it), DECOM.SEQ:223, REF.SEQ:39, LEDIT.SEQ:265, 283, 289, MENUS.SEQ:138, SEDIT2.SEQ:104-143, SEDCHARS.SEQ:15-73, MOUSEY.SEQ:131.
- Tools: UNLINK.SEQ:22-32, MONITOR.SEQ:218 (`XSEG @ +`), CODEBUG.SEQ:595, OVERLAY.SEQ:90.

**Stored or computed list-length in paragraphs**

- `XSEGLEN` (KERNEL4.SEQ:541-549), SAVEEXE.SEQ:74-165 (`XDPSEG @ XSEG @ -`, `100 * +XSEG` for 4K chunks), LISTSET.SEQ:11, STATUS.SEQ:11, UTILS.SEQ:33-34, 270-279, SAVESYS.SEQ:48.
- MULTASK.SEQ:125, 149, 236 (task ES stored relative to XSEG).

**Paragraph-granular heaps**

- POINTER body stores a **physical paragraph** and a size in paragraphs (POINTER.SEQ:26-41). `%UNPOINTER` compacts the heap with `CMOVE-PARS` and adjusts the other pointers (:62-79). `%SIZEOF@` = `@ 16 *D` (:107).
- EDITSTUF.SEQ:106-124 splits `baseseg` into sub-segments by `PARAGRAPH`.
- SED lines are paragraph-aligned: SEDITOR.SEQ:242-245, 506, 542-550, 566 and SEDSHELL.SEQ:32-49 (`c@l 1+ paragraph +`).
- UNHEAD.SEQ:38, 48 (`#headsegs 16 um* drop`). ENVIRON.SEQ:7 (`?cs: evseg - 16 *`). EXEC.SEQ:71. UTILS.SEQ:300-335.
- Tools: OVERLAY.SEQ:99-157 (`#ovsegs 16 *`), NEW-WFL.SEQ:66, 125, WINDOW.SEQ:84-93 (own INT 21h alloc).

Counts from the paragraph scan (lines; the asm `SHL/SHR …,#1` column also catches non-paragraph shifts):

| set | PARAGRAPH/DPARAGRAPH | +XSEG | `XSEG @ ±`/>XREL | `16 *`/`16 *D`/`16 UM*` | U16/,UD16/ | asm `XSEG`/`YSEG`/`XDPSEG` operand or patch |
|---|---|---|---|---|---|---|
| kernel | 14 | 5 | 12 | 7 | 5 | 23 |
| ext | 30 (38 raw; the rest are "paragraph" in SED help strings) | 21 | 11 | 15 | 0 | 1 (LOADEXE.SEQ:83) |
| unl | 5 | 0 | 4 | 1 | 0 | 2 (MULTASK.SEQ:125, 149) |
| tools | 4 (9 raw) | 5 | 5 | 9 (2 are colour math) | 0 | 1 (LOCALS.SEQ:55; 14 raw hits are `PATCH` noise) |

---

## 3. Cell-size assumptions

### 3.1 Counts

Columns:

- **cell2** tokens `2+ 2- 2* 2/ U2/` (many are cell steps, many are not, e.g. screen coordinates)
- **lit2** `2 + | 2 - | 2 * | 2 +! | 2 ALLOT | 2 XDP +!`
- **alloc2** `2 ALLOT`
- **ffff8000** literals `$FFFF $8000 $7FFF $FF00 65535 32768 65536.`
- **flipsplit** `FLIP SPLIT JOIN` (a few hits are inside SED help strings)
- **dbl** double-cell words (`2@ 2! D+ D- S>D *D UM/MOD MU/MOD D< D= D0= DABS 2CONSTANT 2VARIABLE D. UD.R` …)
- **um** lines using `UM* UM/MOD U*D *D`
- **cellword** `CELL CELLS CELL+`
- **par16** `16 *`/`U16/`/asm shift-by-4

*Kernel (META86 + files it FLOADs → KERNEL.COM)*

| file | cell2 | lit2 | alloc2 | ffff8000 | flipsplit | dbl | um | cellword | par16 |
|---|---|---|---|---|---|---|---|---|---|
| SRC/META86.SEQ | 16 | 2 | 2 | 3 | 1 | 5 | · | · | 3 |
| SRC/KERNEL1.SEQ | 19 | · | · | 3 | 3 | 32 | 7 | · | 4 |
| SRC/KERNEL2.SEQ | 4 | · | · | · | 3 | 13 | 3 | · | 2 |
| SRC/KERNEL3.SEQ | 17 | · | · | · | 2 | 4 | · | · | · |
| SRC/EXPAND.SEQ | · | · | · | 1 | · | · | · | · | · |
| SRC/EMMEXEC.SEQ | · | · | · | 1 | · | · | · | · | · |
| SRC/POINTER.SEQ | 9 | · | · | 1 | · | 4 | 1 | · | 1 |
| SRC/SEQREAD.SEQ | 1 | 1 | 1 | · | · | 4 | · | · | · |
| SRC/FPATH.SEQ | 1 | · | · | · | · | 5 | · | · | · |
| SRC/KERNEL4.SEQ | 8 | 1 | 1 | · | · | · | · | · | 2 |
| **total** | **75** | **4** | **4** | **9** | **9** | **67** | **11** | **0** | **12** |

*Extensions (FLOADed by F-PC.SEQ → F-PC.EXE)*

| file | cell2 | lit2 | alloc2 | ffff8000 | flipsplit | dbl | um | cellword | par16 |
|---|---|---|---|---|---|---|---|---|---|
| SRC/BOXTEXT.SEQ | 3 | · | · | · | · | · | · | · | · |
| SRC/BROWSEPR.SEQ | · | · | · | · | · | 16 | · | · | · |
| SRC/BUFSET.SEQ | · | · | · | · | · | 2 | 1 | · | · |
| SRC/COLOR.SEQ | 4 | · | · | · | · | · | · | · | 1 |
| SRC/DEBUG.SEQ | 5 | · | · | · | · | · | · | · | · |
| SRC/DECOM.SEQ | 18 | · | · | · | · | · | · | · | · |
| SRC/EDITERR.SEQ | 1 | · | · | · | · | · | · | · | · |
| SRC/EDITSET.SEQ | · | · | · | · | 2 | · | · | · | · |
| SRC/EDITSTUF.SEQ | 9 | · | · | · | · | 3 | 1 | · | 1 |
| SRC/EMMPTR.SEQ | 2 | · | · | 1 | · | 1 | · | · | · |
| SRC/ENVIRON.SEQ | · | · | · | · | · | · | · | · | 1 |
| SRC/EXEC.SEQ | 2 | 1 | · | · | · | · | · | · | · |
| SRC/FWORDS.SEQ | 2 | · | · | · | · | 7 | · | · | · |
| SRC/HELLO.SEQ | 2 | · | · | · | · | 3 | · | · | · |
| SRC/HYPER.SEQ | 1 | · | · | · | · | · | · | · | · |
| SRC/IBMCURSR.SEQ | 2 | · | · | · | 3 | · | · | · | · |
| SRC/LEDIT.SEQ | 4 | · | · | · | · | · | · | · | · |
| SRC/LOADEXE.SEQ | 3 | · | · | · | · | · | · | · | · |
| SRC/MACROS.SEQ | 2 | · | · | · | · | 1 | 1 | · | · |
| SRC/MENUS.SEQ | 12 | 1 | · | · | · | 2 | · | · | · |
| SRC/MLOAD.SEQ | 1 | · | · | · | · | · | · | · | · |
| SRC/MONOCROM.SEQ | 1 | · | · | · | · | · | · | · | · |
| SRC/MOUSE.SEQ | 2 | · | · | · | 1 | 1 | · | · | · |
| SRC/MOUSEY.SEQ | 7 | · | · | · | · | 1 | · | · | · |
| SRC/NEWLAB.SEQ | 4 | · | · | · | · | · | 2 | · | · |
| SRC/NEXPECT.SEQ | 4 | · | · | · | · | 4 | · | · | · |
| SRC/PASM.SEQ | 10 | · | · | · | · | 10 | · | · | · |
| SRC/PATHSET.SEQ | 6 | · | · | · | · | · | · | · | · |
| SRC/PERTYPE.SEQ | 2 | · | · | · | 1 | · | · | · | · |
| SRC/PRINTING.SEQ | 7 | · | · | · | · | 9 | 3 | · | · |
| SRC/PRTCTRL.SEQ | 1 | · | · | · | · | · | · | · | · |
| SRC/REF.SEQ | 4 | · | · | · | · | · | · | · | · |
| SRC/SAVEEXE.SEQ | 5 | · | · | 1 | · | · | · | · | · |
| SRC/SAVESCR.SEQ | 3 | · | · | 1 | · | 2 | 2 | · | · |
| SRC/SEARCH.SEQ | · | · | · | 1 | · | · | · | · | · |
| SRC/SEDCHARS.SEQ | 4 | · | · | · | · | 1 | · | · | · |
| SRC/SEDCOPY.SEQ | 1 | · | · | · | · | 1 | · | · | · |
| SRC/SEDIT2.SEQ | 8 | · | · | · | · | · | · | · | · |
| SRC/SEDITOR.SEQ | 14 | 2 | · | · | · | 23 | 4 | · | 1 |
| SRC/SEDITWP.SEQ | 2 | 1 | · | · | 1 | · | · | · | · |
| SRC/SEDMENU.SEQ | 1 | · | · | · | 4 | · | · | · | · |
| SRC/SEDPAGE.SEQ | 1 | · | · | · | · | · | · | · | · |
| SRC/SEDSHELL.SEQ | · | · | · | · | · | · | · | · | 1 |
| SRC/SEDSORT.SEQ | 1 | · | · | · | · | · | · | · | · |
| SRC/SEDWHELP.SEQ | 6 | · | · | · | · | 2 | · | · | · |
| SRC/SEDWIND.SEQ | 10 | · | · | · | · | · | · | · | · |
| SRC/SOUND.SEQ | · | · | · | · | · | 1 | 1 | · | · |
| SRC/STATUS.SEQ | 4 | · | · | · | · | 3 | 3 | · | 1 |
| SRC/SVSESDAT.SEQ | 2 | · | · | · | · | 3 | · | · | · |
| SRC/TIMER.SEQ | 1 | · | · | · | 7 | 13 | 5 | · | · |
| SRC/TIMESTUF.SEQ | · | · | · | · | · | 8 | 2 | · | · |
| SRC/UNHEAD.SEQ | 2 | · | · | · | · | · | 2 | · | 2 |
| SRC/UTILS.SEQ | 13 | · | · | · | · | 24 | 10 | · | 9 |
| SRC/VALIDATE.SEQ | · | · | · | · | 2 | 6 | 2 | · | · |
| SRC/VIEW.SEQ | 2 | · | · | · | · | · | · | · | · |
| SRC/VOCABS.SEQ | 13 | · | · | · | · | · | · | · | · |
| SRC/WFL.SEQ | 9 | · | · | · | · | 16 | 1 | · | · |
| SRC/WINSTACK.SEQ | 1 | · | · | · | · | · | · | · | · |
| SRC/WORDS.SEQ | 3 | · | · | · | · | · | · | · | · |
| **total** | **227** | **5** | **0** | **4** | **21** | **163** | **40** | **0** | **17** |

*SRC files not in either build (F-PC.SEQ/F-PCH.SEQ load lists, INDEX, MULTASK, PARSORT, SAVESYS, SIZES, XEXPECT)*

| file | cell2 | lit2 | alloc2 | ffff8000 | flipsplit | dbl | um | cellword | par16 |
|---|---|---|---|---|---|---|---|---|---|
| SRC/INDEX.SEQ | 1 | · | · | · | · | 1 | · | · | · |
| SRC/MULTASK.SEQ | 7 | 1 | 1 | · | · | 1 | 1 | · | · |
| SRC/PARSORT.SEQ | 1 | · | · | · | · | · | · | · | · |
| SRC/SAVESYS.SEQ | · | · | · | · | · | 4 | 1 | · | 1 |
| SRC/SIZES.SEQ | 2 | · | · | 2 | · | · | · | · | · |
| SRC/XEXPECT.SEQ | · | · | · | · | · | 1 | · | · | · |
| **total** | **11** | **1** | **1** | **2** | **0** | **7** | **2** | **0** | **1** |

*TOOLS/ (optional add-ons)*

| file | cell2 | lit2 | alloc2 | ffff8000 | flipsplit | dbl | um | cellword | par16 |
|---|---|---|---|---|---|---|---|---|---|
| TOOLS/ANSI.SEQ | 10 | · | · | · | · | 13 | 5 | 2 | · |
| TOOLS/AUTOFOR.SEQ | 2 | · | · | · | · | · | · | · | · |
| TOOLS/BLKTOSEQ.SEQ | 1 | · | · | · | · | 1 | 2 | · | · |
| TOOLS/BLOCK.SEQ | 6 | · | · | · | · | 2 | 1 | · | · |
| TOOLS/CODEBUG.SEQ | 14 | 1 | · | · | · | 3 | · | · | · |
| TOOLS/COLOURS.SEQ | · | · | · | · | · | · | · | · | 2 |
| TOOLS/COLPLAY.SEQ | 3 | · | · | · | · | 13 | · | · | 1 |
| TOOLS/DIS8086.SEQ | 8 | · | · | · | · | · | · | · | · |
| TOOLS/DISASSEM.SEQ | 10 | · | · | · | · | 4 | · | · | · |
| TOOLS/EMMEXMPL.SEQ | · | · | · | · | 1 | · | · | · | · |
| TOOLS/FASSEM.SEQ | 4 | · | · | · | · | · | · | · | · |
| TOOLS/FDIS86.SEQ | 2 | · | · | · | · | · | · | · | · |
| TOOLS/FDISASEM.SEQ | 2 | · | · | · | · | · | · | · | · |
| TOOLS/FFLOAT.SEQ | 14 | 2 | · | 2 | 1 | 9 | · | · | · |
| TOOLS/FLTAUX.SEQ | 1 | 1 | · | 1 | · | 5 | · | · | · |
| TOOLS/FUNKEY.SEQ | 3 | · | · | · | · | · | · | · | · |
| TOOLS/LISTED.SEQ | 3 | · | · | · | · | · | · | · | · |
| TOOLS/MIDNIGHT.SEQ | 1 | · | · | · | · | · | · | · | · |
| TOOLS/MONITOR.SEQ | 1 | · | · | · | · | · | · | · | · |
| TOOLS/MORE.SEQ | 2 | · | · | · | · | · | · | · | · |
| TOOLS/NEW-WFL.SEQ | 3 | 3 | · | · | 1 | 1 | 1 | · | 2 |
| TOOLS/OBJECT.SEQ | 4 | · | · | · | · | · | · | · | · |
| TOOLS/OVERLAY.SEQ | 2 | · | · | · | · | 3 | · | · | 4 |
| TOOLS/PICTURE.SEQ | · | · | · | · | · | 1 | · | · | · |
| TOOLS/PROFILE.SEQ | 2 | · | · | 1 | · | · | · | · | · |
| TOOLS/RS232IB.SEQ | 4 | · | · | 2 | · | 11 | 1 | · | · |
| TOOLS/SALLOC.SEQ | · | · | · | · | · | 1 | · | · | · |
| TOOLS/SELECT.SEQ | 19 | · | · | · | · | 1 | · | · | 1 |
| TOOLS/SEQTOBLK.SEQ | 1 | · | · | · | · | · | · | · | · |
| TOOLS/SETJMP.SEQ | 3 | · | · | · | · | · | · | · | · |
| TOOLS/SFLOAT1.SEQ | 1 | · | · | · | · | · | · | · | · |
| TOOLS/SFLOAT2.SEQ | 1 | · | · | · | · | 3 | · | · | · |
| TOOLS/SFLOAT3.SEQ | 1 | · | · | · | · | 5 | 3 | · | · |
| TOOLS/SPREAD.SEQ | 7 | · | · | · | · | 14 | 2 | 10 | · |
| TOOLS/SVALUES.SEQ | · | · | · | · | · | 2 | · | · | · |
| TOOLS/UNREF.SEQ | 2 | · | · | 7 | · | · | · | · | · |
| TOOLS/VMOVE.SEQ | 2 | · | · | · | · | · | · | · | · |
| TOOLS/WINDEX.SEQ | 7 | · | · | · | · | · | · | · | · |
| TOOLS/WINDOW.SEQ | 9 | · | · | · | · | · | · | · | · |
| TOOLS/XMS.SEQ | 5 | · | · | 2 | · | 1 | · | · | · |
| **total** | **160** | **7** | **0** | **15** | **3** | **93** | **15** | **12** | **10** |

**Group totals:**

| set | cell2 | lit2 | 2 ALLOT | $FFFF/$8000… | FLIP/SPLIT/JOIN | double words | UM*/UM/MOD lines | CELL words |
|---|---|---|---|---|---|---|---|---|
| kernel | 75 | 4 | 4 | 9 | 9 | 67 | 11 | 0 |
| ext | 227 | 5 | 0 | 4 | 21 | 163 | 40 | 0 |
| unl | 11 | 1 | 1 | 2 | 0 | 7 | 2 | 0 |
| tools | 160 | 7 | 0 | 15 | 3 | 93 | 15 | 12 (ANSI.SEQ:163-164 aliases `CELL+`=`2+`, `CELLS`=`2*`; SPREAD has its own `cells`) |

`CELL`/`CELLS` are not used anywhere in SRC. Every cell step in the core is spelled `2+`, `2*` or `2 ALLOT`, or is hidden in asm.

### 3.2 Representative assumptions (by kind)

**Address and cell stepping (`2+`/`2-`/`2*`)**

- Header navigation: `N>LINK 2-`, `L>NAME 2+`, `>VIEW 2-`, `VIEW> 2+` (KERNEL3.SEQ:89-124). `>NAME`: `2+ Y@` (:114).
- Vocabulary threads: `#THREADS 2* -` (KERNEL3.SEQ:459), `HASH` `SHL AX,#1` (:145), meta `HASH … 2* +` (META86.SEQ:383), `NYTH 512 / 2*` (META86.SEQ:110), KERNEL1.SEQ:51-55.
- Search order: `#VOCS 2*` (KERNEL2.SEQ:39). VOCABS.SEQ:3-19 (`CONTEXT DUP 2+ … #VOCS 2- 2* CMOVE>`).
- Stack depth and ROLL: `DEPTH … 2/` (KERNEL4.SEQ:393), `ROLL … 1+ 2* CMOVE>` (KERNEL1.SEQ:1375). `UNDO` `ADD RP,#6` = 3 cells (KERNEL1.SEQ:355).
- Inline-data skipping: `R> 2+ >R` (KERNEL3.SEQ:368, 420; KERNEL4.SEQ:50).
- List-space comma: `X,` adds 2 to XDP (KERNEL2.SEQ:682). `2 xdp +!` (MENUS.SEQ:45). `LOOP` resolves with `2DUP 2+` (KERNEL3.SEQ:563).
- SED line table: `tl+`/`tl-`/`tl*` = ±2/×2 (SEDCODE.SEQ:11-21), `>lineptr` `shl ax,#1` (:33-43).

**Fixed structure layouts**

- User area: fixed 2-byte slots, TOS/ENTRY/LINK/ES0/SP0/RP0/DP/… (KERNEL4.SEQ:370-389; KERNEL2.SEQ:10-26). USER `VARIABLE` = `CREATE 2 ALLOT` (KERNEL4.SEQ:32). Meta: META86.SEQ:538, 545.
- POINTER body `+0 para, +2 link, +4 size, +6 verify` with `4 +`/`2-`/`2+` offsets (POINTER.SEQ:26-107).
- Handle control block: `B/HCB`=70, `HNDLOFFSET`=68, `>ATTRIB 66 +`, `>NAM 1+` (HANDLES.SEQ:16-28). The handle cell sits at a hardcoded byte offset.
- `FILEPOINTER` = `VARIABLE … 2 allot` (a hand-made double) (SEQREAD.SEQ:43). Also `exec.param 2 + !` (EXEC.SEQ:81) and `DSBUF 2+`/`4 +` (UTILS.SEQ:270-279).
- Vocabulary = `#THREADS` cells followed by the voc-link (KERNEL3.SEQ:782-785).

**Code-field offsets (3-byte CALL/JMP + rel16)**

- `>BODY 3 +` and `BODY> 3 -` (KERNEL3.SEQ:95-106). `BODY_SIZE` (META86.SEQ:147). `DOCONSTANT` `3 [BX]` (KERNEL1.SEQ:179). `NEST` `3 [DI]` (:120).
- `1+ @ OVER >BODY +` decode (KERNEL4.SEQ:42; KERNEL3.SEQ:461-462).
- `3 + - R> 1+ !` re-encode (KERNEL3.SEQ:687, 692). `,CALL`/`,JUMP` = `232/233 C, 0 HERE 2+ - ,` (KERNEL3.SEQ:657-661).
- Outside the kernel: PROFILE.SEQ:72-74, MULTASK.SEQ:198, DIS8086.SEQ:213/DISASSEM.SEQ:94-95 (`' (LOOP) 5 +`), DECOM/DEBUG/MAKEDEF/WORDS (`232`/`233` tests).

**16-bit width, sign bit and wraparound**

- `(DO)`/`(?DO)` bias the index by `$8000` so that overflow ends the loop (KERNEL1.SEQ:381, 398). `(LOOP)` tests OV (:358-374).
- `CNHASH` = `$0FE00 AND FLIP` assumes 16-bit CFAs and 64K code space (KERNEL3.SEQ:52; META86.SEQ:229). `(FIND)` mask `$7F3F` (KERNEL3.SEQ:162).
- `$FFFF 0 DMIN DROP` clamps to 16 bits (POINTER.SEQ:37). `65535. svsize um/mod` (SAVESCR.SEQ:13). `65536. d>` limit (EMMPTR.SEQ:11).
- META86 `$FFF0 CODEBYTES` (comment: "MUST BE less than $FFFF for math in KERNEL4", META86.SEQ:107). TOOLS/UNREF.SEQ:33-61 (`65536. pointer`, two `$8000` halves).
- `$FFFF` as true: KERNEL1.SEQ:96, EXPAND.SEQ:24, EMMEXEC.SEQ:34.

**Byte order and half-cells**

- `FLIP`/`SPLIT`/`JOIN` (KERNEL1.SEQ:700-717). Uses: cursor shape `CROWS DUP 1- FLIP +` (IBMCURSR.SEQ:133, 139), attribute-in-high-byte for `LFILLW` (IBMCURSR.SEQ:79; EMMEXMPL.SEQ:85), key codes `FLIP DUP 3 =` (KERNEL2.SEQ:399; MOUSE.SEQ:293), BCD/time packing (TIMER.SEQ:25-69; VALIDATE.SEQ:14-15; PERTYPE.SEQ:32).
- Word compares of two packed chars: `ASCII ' FLIP 3 +` (KERNEL3.SEQ:277), KERNEL2.SEQ:1025-1029.
- `C@`/`C!` on the low byte of a cell: `>NEXT` restore writes `$AD26`/`$E0FF` words (KERNEL1.SEQ:76-79). DBGFIX.SEQ:28 matches `$FFAD`/`$E0`.

**Doubles**

- Doubles are pervasive (≈330 tokens) but mostly *semantic*: 32-bit byte counts, file sizes and times. File positions are doubles passed to `MOVEPOINTER` (HANDLES.SEQ:150-170, CODE; split into CX:DX), `SEEK` (SEQREAD.SEQ:287) and `0.0 seqhandle movepointer` (FPATH.SEQ:120; FWORDS.SEQ:72-124; SEDCOPY.SEQ:66-120).
- With 32-bit cells, doubles become 64-bit and almost all of this keeps working. Exceptions are code that relies on `UM*`/`*D` overflowing 16 bits or on `DROP` of a known-zero high half: UNHEAD.SEQ:38 `16 um* drop`, POINTER.SEQ:37, `DPARAGRAPH … DROP` (KERNEL1.SEQ:1388).
- There is no `D>S` in the core (PASM's `D>S` is an assembler word).

### 3.3 Edit estimate

A line was counted as needing review if it contains a segment word or a cell-size idiom (`2+ 2- 2* 2/ FLIP SPLIT JOIN CNHASH >BODY BODY>`, `2 ALLOT`, `232/233`, `[345] [+-]`, `$FFFF/$8000`). CODE bodies are excluded because they get rewritten in C anyway.

| set | high-level lines | lines w/ segment words | lines w/ cell idioms | union (upper bound) |
|---|---|---|---|---|
| kernel | 2,428 | 355 | 128 | **444** (~18%) |
| ext | 10,317 | 503 | 363 | **809** (~8%) |
| unl | 748 | 48 | 16 | 60 |
| tools | 7,720 | 226 | 222 | 424 |

Interpretation:

- **16-bit-cell VM with emulated real-mode memory** (a 1 MB byte array, `phys = (seg<<4)+off`, seg:off kept as 2 cells, XSEG/YSEG/POINTER/ALLOC implemented by a tiny paragraph allocator in the VM):
  - High-level edits: **≈0–30 lines**. Only the BIOS/IVT and video sites in (e)/(c) need the VM to fake `0040:xxxx` and a `B800` text buffer, or need small rewrites. The `>NEXT` byte-patching tricks (KERNEL1.SEQ:76-101, DBGFIX, DEBUG.SEQ:148, PROFILE, MULTASK `PAUSE`) also need rework.
  - The work is the **~350 kernel CODE/LABEL words plus the 54 ext CODE words** re-done as C primitives, a DOS/BIOS service shim (INT 21h file/memory/vector calls), and the 3-byte code-field convention (the VM can dispatch on the `E8/E9` target address, so `>BODY 3 +` keeps working).
  - PASM, DEBUG's machine-code stepping, the TOOLS disassemblers/assemblers and FFLOAT (8087) are x86-specific in either design.
- **32-bit cells, flat byte addresses:**
  - Kernel high-level: essentially all of the 444 flagged lines plus META86. The metacompiler (`-T/-X/-Y`, `,-T 2 ALLOT-T`, paragraph-relative bodies) must be redesigned, so in practice the whole kernel's high-level half (~2,400 lines) is rewritten or re-derived.
  - Ext: ~800 flagged lines across ~60 files. The heavy ones are SEDITOR/SEDCODE/EDITSTUF/SEDIT2/SEDSHELL (line-per-paragraph storage), DEBUG/DECOM/SAVEEXE/LOADEXE/UTILS (image layout), NEWLAB, WFL, PERTYPE/HTYPE (TYPEL), LEDIT/MENUS/SEDCHARS (list-space tables) and POINTER/SAVESCR/NEXPECT/MACROS (heap).
  - Tools: ~420 lines, plus whatever is x86-specific anyway.
  - The return-stack frame shrinks from ES+IP to IP. That touches all ~50 `2R@ @L`/`R> 2+ >R` inline-data sites, and those are *not* caught by a simple cell-width search.

---

## 4. Recommendation: build a 16-bit-cell VM with real-mode address emulation first, then port to 32-bit

**Evidence for doing 16-bit first:**

1. **Most of the cost doesn't depend on cell size.** About 2,500 kernel lines (51%) are 8086 assembler in ~350 CODE/LABEL words. They must become C primitives under either option. A 16-bit VM lets that be the *only* large job.
2. **Segments are structural, not incidental.**
   - Colon bodies are reached through a relative paragraph number stored in the code field (`KERNEL3.SEQ:748-750`, `KERNEL1.SEQ:120-128`). The return stack holds ES:IP pairs, and ~50 sites depend on that. Heads are reached through a 16-bit-indexed CFA hash (`KERNEL3.SEQ:51-115`).
   - The editor stores every text line as a paragraph (`SEDITOR.SEQ:236-246`). The heap manager compacts by paragraphs (`POINTER.SEQ:62-79`).
   - ~860 high-level kernel and ext lines touch segment words (355 kernel + 503 ext). Emulating `seg*16+off` keeps all of them byte-for-byte correct. A flat 32-bit design has to touch every one.
3. **List space is already over 64K** (128K default, `KERNEL2.SEQ:489`). A 16-bit *flat* VM is therefore not an option: the 16-bit VM must keep segment:offset addressing. This is cheap. `@L` becomes `mem[(seg<<4)+off]`, and NEST becomes `ES = body + XSEG; IP = 0`.
4. **It gives a testable baseline.** The original runs under DOSBox. With identical cell width and memory layout, behaviour, `.S` output, `DUMP`s and saved images can be diffed directly. It may also be possible to bootstrap from the existing `KERNEL.COM`/`F-PC.EXE` images by mapping each CODE word's CFA to a C primitive. This would avoid running META86 before a VM exists. It is an unverified idea, and `HEADERLESS` CODE words such as `KERNEL3.SEQ:197-242` would need the metacompiler's LABELS map.
5. **The 32-bit port is a mechanical follow-on.** Section 3 is the checklist: no `CELL` vocabulary exists, so every `2+`/`2*`/`2 ALLOT`/`3 +`/`$8000`/`FLIP` must be reviewed (~1,250 kernel+ext lines). Doing it on a working VM, where the metacompiler can be changed and immediately rebuilt, is much safer than doing it blind together with the C rewrite.

**Against (and why it is acceptable):** the 64K code-space limit, `CNHASH` and 64K-per-colon-body limits stay. The VM needs a small DOS/BIOS shim covering INT 21h file, memory and vector calls, `0040:` keyboard/video variables and a `B800` text page. That is roughly 40 call sites (Section 1.4, columns e and c: 24 BIOS/IVT and 20 video tokens in kernel+ext). This is throw-away work if the long-term target is a modern 32-bit Forth.

**If the goal is not running existing F-PC code** (i.e. a new 32-bit Forth that only borrows F-PC's design), going straight to 32-bit is reasonable. Treat the kernel as a rewrite rather than a port, because META86 and ~18% of the kernel's high-level lines are layout-bound. Port only the extensions you need: SED, DEBUG/DECOM and SAVEEXE are the expensive ones.

---

## Method

- Sources were stripped of CR and ^Z and scanned with a Python tokenizer. It drops `\` line comments, `( … )` comments and `comment: … comment;` blocks, and upper-cases tokens.
- Category membership is by word lists (Section 1.4). Lines with a far primitive but no a–e indicator go to (f).
- "CODE-body lines" means the lines from `CODE`/`LABEL` up to `END-CODE`/`C;`.
- Token counts over-approximate: `2+` used for screen columns and `3 +` for box drawing are included. The `codefld` (`3 +`/`5 +`) column was too noisy to tabulate, so code-field uses are listed by hand in 3.2. Strings inside `."`/`"` are not stripped, which causes a few FLIP/SPLIT/PARAGRAPH hits in SED help text.
