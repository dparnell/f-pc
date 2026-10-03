# Group C port notes: SED core (EDITSTUF, SEDCODE, SEDITOR, SEDIT2)

Status: all four files are ported in place. They compile cleanly with a scratch
stub prelude standing in for the group A/B files (UTILS, COMMENT, DEFERS,
BOXTEXT, SAVESCR, LEDIT, TIMER, ...), plus the real CASE.SEQ.

Tests run against the stubs:
- **File round trip.** Read a 5000-line file, then: move with `to.line` and `backto.line`;
  edit lines through `putline` (making a line longer, shorter and empty); `ins.linelist`;
  `linedelete`; `?appendline`; `<shom>`; `write.file`. The output matches the expected file
  byte for byte, and `CURRENTSIZE` equals the file size.
- **Invariants.** Checked after every step: `TEND` = line `CURLINE`'s address, `TOFF` <= `TEND`,
  and the end sentinel is in place.
- **Other operations.** The line delete buffer, `#deletelines`, the handle stack
  (push, pop and `hrotate`), `curline+` and `curline-` over thousands of lines, and
  `ctlset`/`fnset` table patching.
- **Display.** `showscreen` drew the window into VIDEO-BUF correctly, and `sresize` re-laid it out.

## 1. SED memory: the new data structures and API (other files depend on this)

All editor memory is one heap block, `BASESEG` (a POINTER). `TBUF.INIT` (`EDINIT`) allocates
it and cuts it into these parts, each cell aligned:

| value | was | now |
|---|---|---|
| `HSEG` | segment | address of the handle stack: `MAXH+1` entries of `B/HSTK` = `B/HCB 4 CELLS +` bytes (an HCB, then cells LOADLINE SCREENCHAR ?BROWSE LINESAVE) |
| `LSEG` | segment | address of the **line table**: `MAXLINES+4` cells |
| `DSEG` | segment | address of the line delete buffer: `(MAXDLINE+1)*MXLLN` bytes. `LDEL.BUF` is an offset into it (`dseg ldel.buf +`). |
| `WSEG` | segment | address of the write buffer (`WRITELIM 256 +` bytes) |
| `TSEGB` | segment | address of the **text buffer** |
| `#EDSEGS` | paragraphs | text buffer size in **bytes** (= `EDBUFSIZE`, new VALUE, default $100000) |
| `MAXSEGS` | paragraphs | total bytes in BASESEG |
| `MAXLINES` | | `#EDSEGS 16 /` (65536 by default) |
| `TOFF` `TEND` | segments | **addresses** (see below) |

To edit bigger files, set `EDBUFSIZE` before the first edit, or after `unpointer> baseseg off> tsegb`.

**Text buffer (a gap buffer).**
- **Line format.** Each line is a counted string: a count byte (1..255), then the text **including its CR LF**.
- **Alignment.** A line starts on a cell boundary and occupies `count 1+ ALIGNED` bytes.
  The minimum is one cell (`2 c, 13 c, 10 c,` + pad).
  - **Rule for ported code:** the original `c@l 1+ PARAGRAPH` becomes `c@ 1+ ALIGNED`.
  - "One paragraph" (the `1-` on a segment) becomes `CELL -`.
  - A paragraph *count* times 16 is now just a byte count.
- **Layout.**
  - Lines `0..CURLINE-1` are packed upward from `TSEGB`. `TOFF` is the first free byte after them.
  - Lines `CURLINE..LASTLINE` are packed against the top, and the last line ends at `TSEGB #EDSEGS +`.
  - `TEND` = address of line `CURLINE`.
  - The gap is `TOFF..TEND`. `?FULL` is `tend toff - $1000 <` (4096 bytes; was $100 paragraphs).

**Line table.**
- Entry n (a cell) holds the address of line n's count byte.
- Entry `LASTLINE+1` = `TSEGB #EDSEGS +`, the end sentinel.
- For n >= CURLINE, line n's storage is `[entry n, entry n+1)`.

**Table API (SEDCODE.SEQ).** "Line table addresses" are now **absolute addresses of table cells**:

| word | stack | meaning |
|---|---|---|
| `>LINEPTR` | ( n -- a ) | `n CELLS LSEG +` |
| `LINEPTR` | ( -- a ) | `CURLINE >LINEPTR` |
| `TL:@` `TL:!` `TL:+!` | | aliases of `@ ! +!` |
| `TL:` | ( a -- a ) | does nothing (was `( a -- lseg a )`) |
| `TL+` `TL-` `TL*` | | aliases of `CELL+ CELL- CELLS` |
| `#LINESEG` | ( n -- a ) | address of line n (its count byte); was its segment |
| `#LINESEGINFO` | ( n -- a len ) | **was ( n -- seg 1 len )**: one item fewer. a = text start, len includes CR LF |
| `LINESEGINFO` | ( -- a len ) | the same for CURLINE (**was seg a len**) |
| `LINEBUF:` | ( -- a ) | **was ( -- seg a )** |
| `ADJ_PTR_LINES` | ( n1 n2 n3 -- ) | add n1 (bytes) to entries n3..n2-1; does nothing if n3 >= n2 |
| `ENDTST?` | ( -- f ) | `entry[CURLINE-1] TEND U<`, and (new) `CURLINE < MAXLINES-4` |
| `RMSAVE` | ( a -- a c ) | c = `a C@`; RMMAX = max(RMMAX, c-2) |
| `CLIPLINE` | ( a n -- a+winoff n' ) | n' = n-WINOFF, clamped to 0..TEXT.WIDTH (signed) |
| `LEN-ACCUM` | ( n -- n ) | `n 0 CURRENTSIZE D+!` |
| `WINDOW.LEFT` `WINDOW.RIGHT` `TEXT.WIDTH` | | `first.textcol 1-`, `last.textcol 1+`, `last-first+1` |
| `GETDISKFREE` | ( drv -- clusters b/sec sec/clus ) | stub: `65535 512 64` |

Porting patterns for the add-on files:
- `tl: dup tl+ tl: n cmovel>` → `lineptr dup tl+ n cmove>`. The pairs disappear, and a `2SWAP` of two seg:off pairs becomes `SWAP`.
- Long moves inside the text buffer:
  - moving lines down (to lower addresses) uses `CMOVE`;
  - moving lines up uses `CMOVE>`;
  - `CMOVE-PARS` / `CMOVE-PARS>` become `CMOVE` / `CMOVE>` with byte counts.
- **CR LF tests are 16-bit:** use `crlfval ... W!` / `W@`, and the same for `BLBL`. `@`/`!` would touch 4 bytes.
- `SSEG` is gone. `#lineseg =: sseg 1 @> sseg 0 c@l` → `#lineseginfo`, and `SEARCH` takes two flat strings.
- `TYPEL ( seg a n )` is gone:
  - SED's line display deferred word `SLTYPEL` is now `( a n -- )`, defaulting to `TYPE`;
  - `EXSLTYPEL ( a n -- )` expands tabs;
  - `SDISP` uses `TYPE`.
- `0 $417 c@l` → `SHIFT-STATE` (`?capslock ?altkey ?ctrlkey ?shiftkey` are defined here).

Other in-file changes to the data handling:
- `#DELETELINES` set `TEND` from entry `n` (`r@ tl* tl:@`), a latent bug that SEDCOPY's following `LINEDELETE` hid. It now uses entry `CURLINE+n`.
- `?APPENDLINE` now also resets `TEND` (it moved the current line down one cell without doing so).
- `HROTATE` exits if there is no handle stack.

## 2. Screen size and resize

- **Initial size.**
  - `LAST.TEXTLINE` / `LAST.TEXTCOL` start as `rows 2-` / `cols 2-`, both at load time and in `SEGINIT` (INITSTUFF).
  - New VALUEs `SEDROWS` and `SEDCOLS` (EDITSTUF) record the size the window was laid out for.
- **`%SEDLAYOUT`** (deferred as `SEDLAYOUT`, so SEDWIND can lay out split windows):
  - moves the bottom and right edges by the change in ROWS and COLS, so a full-screen window stays full and a smaller one keeps its margins;
  - clamps the window to the screen (first.textline 1..rows-4, last.textline <= rows-2, last.textcol <= cols, minimum 8 columns);
  - clamps SCREENLINE.
- **Resize handling.**
  - `SED-RESIZED` is added to the `RESIZED` chain with DEFERS. It only sets `SRESIZED`.
  - `SRESIZE` = `off> sresized sedlayout dark on> ?border showscreen`.
  - `<REEDIT>` calls `SEDLAYOUT` on entry. Each pass of its key loop runs `SRESIZE` if `SRESIZED` is set.
  - `K-RESIZE` (255) from `KEY` is not passed to `DOACHAR`; it sets `SRESIZED`. 255 was Alt-8 (`ecmdtgl`), which is now unreachable by key.
- **Other changes.**
  - `^CC` ignores K-RESIZE.
  - `EMPTYKBD` drains all typeahead with `KEY? BIOSKEY` (the BIOS buffer depth can't be read). A resize drained there is not lost, because `SRESIZED` is already set.
  - `-S` now types in 132-byte chunks, so wide screens work.
  - The status line's "F10"/mouse field is at `window.right 8 -` / `6 -` (was 71/73).
  - `?SOFTERROR`'s box is centred on COLS.

## 3. Per-file changes

- **EDITSTUF.SEQ.**
  - The buffer values are documented as addresses and byte counts.
  - `TBUF.INIT` is rewritten: no MAXBLOCK; sizes come from `EDBUFSIZE`.
  - `B/HSTK` = `b/hcb 4 cells +`.
  - `ED1>HSTACK`, `HSTACK>ED1` and `HROTATE` use flat addresses.
  - The window values follow ROWS/COLS.
  - The commented-out `HSWAP` is left in segment form.
- **SEDCODE.SEQ.** All 19 CODE words are now high-level Forth or ALIASes; a header comment documents the layout.
- **SEDITOR.SEQ.**
  - Changes listed in §1 and §2: shift keys, EMPTYKBD, -S, PLACELINE, ?0FIX, READ.OPENFILE, ?ENOUGHDISK, LINEWRITE/FLUSHWRITE (`HWRITE`), GETLINE/PUTLINE, TOLINE±, CURLINE±, SINIT, INS.LINELIST, ?APPENDLINE, LINE>LDEL.BUF, LDEL>LINEBUF, #DELETELINES, LINEDELETE, APPENDLINE, #LINELOOK, LOOK.TILL/LOOK.BACK/<SLOOKER> (SSEG gone), SGETL, SDISP, SLTYPE, EXSLTYPEL.
  - `?PAGE-CHAR` is now high level (`0 prtlines um/mod drop 0= if pagechar then`).
  - SEDLAYOUT and SRESIZE are added at the end.
- **SEDIT2.SEQ.**
  - `CTLSET`, `FNSET`, `?.S^TBL` and `?.SFUNTBL` index EXEC: tables as `' tbl >BODY @ n CELLS + CELL+`.
  - `?EXP_TYPE_SET` uses `TYPE`.
  - The `<REEDIT>` key loop handles resize. The key tables are unchanged.

## 4. Built-ins needed

**None required:** every former CODE word is Forth now.

Optional speed candidates, if profiling ever shows a need:
- `ADJ_PTR_LINES ( n1 n2 n3 -- )`: add n1 to the cells at `LSEG + i*4` for i in [n3, n2), with no effect when n3 >= n2. It is a loop over the line table, run once per long jump.
- A host `DISKFREE` built-in to replace the `GETDISKFREE` stub. DOS 36h is not emulated.

## 5. TODO-PORT

- `GETDISKFREE` is a stub that reports a large disk, so ?DISKFULL and ?ENOUGHDISK never warn (SEDCODE.SEQ).
- The pop-up boxes (BOX&FILL with the AT positions inside them), `<STATFUNC>`'s columns 30/45/59, and `.PARTIAL`/`UPDT`/`SLOON` and similar boxes still assume a screen of at least 80x25 (comment at the top of SEDITOR.SEQ). Only the edit window follows COLS/ROWS.
- Split windows (`SPLITWIND?`, SEDWIND.SEQ) are not considered by `%SEDLAYOUT`. SEDWIND should re-vector `SEDLAYOUT`.

## 6. Cross-file notes

**Prerequisites these files use** (from other groups). The names and stack effects are assumed unchanged:
- UTILS: `=: ALIAS ?DOSIO AT DARK -LINE SP>COL SAVECURSOR RESTCURSOR MOUSEFLG DOBUTTON SAVESTATE RESTORESTATE >NORM >REV >ATTRIBn .MEM U<= <= >= ...`
- COMMENT: `COMMENT:`
- DEFERS: `DEFERS`
- CASE
- SEARCH: `SEARCH ( a1 n1 a2 n2 -- off f )`, now on flat addresses
- IBMCURSR: `CURSOR-OFF/ON BIG-CURSOR NORM-CURSOR`
- BOXTEXT: `BOX&FILL BCR .BOX" TX TY`
- SAVESCR: `SAVESCR RESTSCR`
- PERTYPE: `\TYPE`
- LEDIT: `LINEEDITOR AUTOCLEAR STRIPPING_BL'S`
- TIMER/TIMESTUF: `TENTHS SECONDS GETTIME T>B B>T FORM-TIME TTIME TIME-ELAPSED TIME-RESET *D`
- HANDLES: `PATHSET`

Notes for the add-on files:
- **TOPEDIT / HTYPE / BROWSEPR.** They switched `TYPEL` between `(TYPEL)`, `HYPERTYPEL` and the QVIDEO type. With TYPEL gone:
  - switch `TYPE`, or `SLTYPEL ( a n -- )` for the line display only;
  - `HYPERTYPEL` should become `( a n -- )`;
  - `SDISP` types the current line with `TYPE`.
- **TOPEDIT.** Its cleanup (`unpointer> baseseg off> tsegb off> lseg ...`) still works.
- **SEDSHELL.SEQ:23-50.** The `c@l 1+ paragraph` / `16 *` / `toff 100 +` arithmetic becomes bytes:
  - use `c@ 1+ aligned`;
  - a byte count needs no `16 *`;
  - `100` paragraphs of gap margin is about `$640` bytes (or use `$1000` to match ?FULL).
- **SEDSORT / PARSORT / PRINTING.** `#lineseginfo` returns `( a len )`: drop the seg and use `CMOVE`.
- **SEDITWP.** `curline 1+ #lineseg 0 c@l` → `curline 1+ #lineseg c@`.
- **SEDCOPY.** `#DELETELINES` and `LINEWRITE`/`FLUSHWRITE` are unchanged in use. `WBLEN` is a byte offset into `WSEG`.
- **EDITSET.** To patch the key tables, use `CTLSET`/`FNSET` or the `' tbl >body @ n cells + cell+` layout.
- **SEDCHARS / MOUSEY.** `extcharseg +xseg` is a separate issue; it is not SED's data.
- **SEDWIND.** It changes `FIRST/LAST.TEXTLINE/TEXTCOL`. Keep `SEDROWS`/`SEDCOLS` in sync if it sets absolute sizes, and re-vector `SEDLAYOUT` for split windows.
