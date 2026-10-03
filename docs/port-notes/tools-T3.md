# TOOLS group T3 port notes: screen, windows and UI

Files: WINDOW SWINDOW SCROLL SSCROLL SMENU SMESSAGE SMACRO SELECT SPREAD COLPLAY COLOURS ANSI WYSE50 MIDNIGHT MORE LISTED FUNKEY VMOVE WINDEX.

All nineteen files were converted from CRLF to LF. Their CP437 bytes (box drawing, the `±` field markers in SELECT, the German text in COLPLAY, the `^Z`s at the end of WYSE50) are preserved byte for byte. Each header line says `F-PC native port`.

## Testing

Every file FLOADs cleanly on top of `F-PC.IMG`. The only messages are "isn't unique" warnings, which the originals also gave (see "Name clashes"). Each file was then exercised in batch mode or in a pseudo-terminal with `tools/ptytest.py`. Resize tests were run at 40x10 to 40x12, 60x16, 80x25 and 120x40.

- **WINDOW.** `demo` runs, and nested windows close back to an identical screen. This was checked byte for byte against VIDEO-BUF in batch mode. `enterwindow`/`exitwindow` route EMIT and CR into the window. A window still open across a shrink closes without damage (clipped). A window too large for the screen gets "Window won't fit".
- **SCROLL.** `SCROLL-UP` and `SCROLL-DN` on half-screen areas. Coordinates off the screen are clipped.
- **SSCROLL.** `5 sub-set sub-on words` keeps lines 0-4 in place.
- **SWINDOW.** `popup`, then a key restores the screen.
- **SMENU.** `amenu` shows the bar and the drop-down.
- **SMESSAGE.** `.hello` prints the new message.
- **SELECT.** The demo after `\S` was loaded from a scratch copy. SDEMO shows its window, the number and string fields, the arrows, Enter and the activation keys. "do window 2" opens a nested window, and ESC restores the screen exactly.
- **SPREAD.**
  - Data entry, dollar format, A)gain, R)ow and C)ol names, G)oto, M)ode, N)ew, P)ref, the arrows, PgUp/PgDn and Q)uit all work.
  - The equation `0 A + 0 B * 2. - 0 A mod 30.` gives 20, with the original precedences.
  - Live resizes at 60x16 and 120x40 lay the sheet out again (7 columns by 26 rows at 120x40).
- **COLPLAY.** `syscolors`: editing colour 1 changes only that table byte (19, and colour 2 stays 79).
- **COLOURS.** `t1` and `2 3 bigcolours`.
- **ANSI.** Under `ONLY ANSI CORE EXT ALSO ANSI CORE`, these work: `:`, `:NONAME`, UNLOOP inside DO, LSHIFT/RSHIFT, UNUSED, EVALUATE, POSTPONE, CHAR, MARKER, SM/REM, FM/MOD, `>NUMBER` and ALIGN/ALIGNED.
- **WYSE50.** `W50-IBM`: `^Z` clears the screen, `ESC a 5 R 10 C` puts the cursor at column 9, row 4, and plain characters are drawn.
- **MIDNIGHT.** `4 TOWERS`, a key aborts. `20 TOWERS` at 40x12 is limited to 5 rings.
- **MORE.** `paged words` stops each page, ESC prints "Stopped", and `normal slower slower faster normal` work.
- **LISTED.** The demo after `\s` gives the expected list output.
- **FUNKEY.** F10 runs MYFUNC, and `.fn` lists the keys.
- **VMOVE.** It moved two words, one of them at the head of its thread, from FORTH to a new vocabulary.
- **WINDEX.** `ascii Z <awords>` lists the words with their vocabulary, file, page and source line.

## Per-file changes

### WINDOW.SEQ
The CODE words are rewritten in high-level Forth:
- `chra` and `chra+` use BIOS-VIDEO AH=9.
- `rdchra` uses AH=8.
- `scrlup` uses AH=6.
- `calloc` uses ALLOCATE and returns `( addr true | 0 false )`.
- `e@` and `e!` are `+ @` and `+ !`.
- `rdcur` is GET-XY.
- `scn->buf` and `buf->scn` copy between an ALLOCATEd buffer and VIDEO-BUF through `scnmove`, clipping rows and columns to the current COLS and ROWS.

Data structures:
- The WCB fields are cells, and WCBSEG holds the WCB's address.
- The DOS `free` and `setblock` are gone. The system's `FREE ( addr -- ior )` has the same arity as the old word as it is used here.
- POSTFIX and VSEG are gone.

Size handling:
- `putch` and `drawrow` skip positions off the screen. BIOS-VIDEO clamps the cursor, so without this, off-screen writes would land on the edge.
- `openwindow` checks against `rows 1-` and `cols 1-` (it used 24 and 79).
- `fillscreen` fills a row at a time. The VM's AH=9 does not wrap.

Behaviour fixes:
- `wemit` positions the cursor (`wat`) before writing. TYPE (VIDEO-TYPE) moves the hardware cursor, which the original assumed stayed at the window cursor.
- If the screen buffer can't be allocated, `window` now unlinks and frees the WCB it had just made.
- Open windows are not re-laid out on a resize.

### SWINDOW, SMENU, SMESSAGE, SMACRO
No code changes. BOX&FILL, SAVESCR/RESTSCR and the MENUS words are already ported.

### SCROLL.SEQ
SCROLL-UP and SCROLL-DN (CODE, INT 10h AH=6/7) are now BIOS-VIDEO calls. BIOS-VIDEO clips the area to the screen. The comment explains `COLS 1- ROWS 1-`.

### SSCROLL.SEQ
`SUB-SET` clamps to `rows 2-` (it used 23).

### SELECT.SEQ
- **Window record.** The pointer fields (`wlink` and the xt and text pointers) are `cell winitem`, and the per-line pointer table is in cells (`2*` became `cells` everywhere, including the commented-out WINDSHOW).
- **Strings.** `?cs: ... \typeL` became `\type`, and `?cs: pad count prntypel` became `pad count prntype`.
- **Field padding.** The blanks after the number field use `w!`.
- **comment->PRN.** The box is on the bottom 3 lines of the screen (it used 0 21 79 23).

The mouse handlers are unchanged. MOUSE-PRESENT? is false in the VM, so they are inactive.

### SPREAD.SEQ (the spreadsheet)
- **Cell layout.** A cell is `3 cells` (the xt plus a double), and `cells 2+` became `cell+`.
- **Renamed words.** `array`/`cells` became `ss_array`/`ss_cell`, because the old names would hide the system's ARRAY and CELLS from everything loaded later.
- **Operator stack.** `op_stack` steps by `2 cells`.
- **Layout from the terminal size.**
  - `ss_layout` sets `#drows` to ROWS-10 and `#dcols` to (COLS-28)/13. These are 15 and 4 at 80x25, so the layout is identical to the original there.
  - The border, menu, status and command lines are placed from `border_row`, `menu_col` and `stat_col`.
  - Menu lines that don't fit are skipped.
  - `dis_screen` re-lays out the sheet, and the main loop redraws it on K-RESIZE.
- **Window clamping.** `ss_clamp` keeps the window and the marker inside the 26x26 sheet. The movement tests use `<` (they used `<>`).
  - Before this, G)oto or R)ow near the end could push `cell_ptr` past the array, which corrupts memory on data entry.
  - G)oto keeps the marker on the requested cell, and allows columns A-Z.
  - R)ow and C)ol redraw at the starting row or column, and the column letter is clamped.
- **Double arithmetic.** `d*`, `d/` and `dmod` are true double-by-double operations. `d/` is a 64-step unsigned division, plus signs, and division by zero gives 0. The originals took a single divisor or multiplier, so `A * B` in an equation was wrong and `mod` returned the quotient. `get#` passes `100 0` and `10 0`.
  - Literal constants in equations must be doubles (`2.`), as before.
- **Equations.** E)quation builds `: formula a[ ... ]a [ cell_ptr cell+ ] literal 2! ; last @ name> cell_ptr !` in `equ_buf` and EVALUATEs it (it built the line in TIB and called INTERPRET).

### COLPLAY.SEQ
- The colour being edited is fetched and stored with `c@` and `c!` (in `newcolset`, `<setcolor>` and the table display). `@` and `!` would now write 4 bytes into the 2-byte-per-entry COLORS table.
- The patch of `>1BGFG`..`>8BGFG` with `@>` and `!>` works unchanged, since a colon definition's body is its list address.

### COLOURS.SEQ
- A header and a note were added in the `\\` text part. Don't put a brace in that text: it would end the text.
- `bigcolours` and `.colours` split the screen at `cols 2/` and `rows 2/`.

### ANSI.SEQ
This is the ANS Forth CORE / CORE EXT compatibility package, not a terminal driver.
- `token` lays down `CT-NEST ,` and `XALIGN XHERE ,`, like the kernel's `(:)` (it made a JMP to `>NEST` and a list segment). `:` and `:NONAME` use it.
- CELL+, CELLS, ALIGN and ALIGNED alias the real words.
- LSHIFT, RSHIFT and UNLOOP (UNDO) alias VM built-ins. They were CODE words.
- EVALUATE aliases the kernel's, so `needs eval` is gone.
- MARKER steps through CONTEXT in cells.
- UNUSED is `limit here -`.

### WYSE50.SEQ
- The CR handling clamps `#LINE` at `ROWS 1-` (it used 24).
- It works as an alternative EMIT driver: `W50-IBM` installs it, and the cursor addressing and clear screen go through IBM-AT and IBM-DARK.
- TYPE stays `(TYPE)`, so escape sequences must be EMITted, as in the original.

### MIDNIGHT.SEQ
NMAX is a colon word: `COLS 3 - 6 /`, limited by `ROWS 7 -` and the 13-byte RING array. It was a VALUE computed from COLS at load time.

### MORE.SEQ
No code changes. `switch_bodies` swaps token list addresses. A note was added.

### LISTED.SEQ
List nodes are two cells: `2+` became `cell+`.

### FUNKEY.SEQ
`f#funcs` is indexed in cells. K-RESIZE and other keys pass through.

### VMOVE.SEQ
- `y@`, `y!` and `yc@` became `@`, `!` and `c@` for links and name bytes.
- The first name test uses `w@`: the count byte and the first character, masked with $7F1F as before.

### WINDEX.SEQ
- The thread tables are copied with `#threads cells`, and the vocabulary body is `voc-link #threads cells -`.
- `y@` and `yc@` became `@` and `c@`.
- The source lines shown are stripped of CR/LF by a new `-crlf`. The original used `count 2-`, which is wrong for LF-only files.
- Run ALLWORDS from the keyboard, not from inside an FLOADed file: `$file` switches the current source file, so the FLOAD would go on reading from the file WINDEX opened. The original had the same limitation.

## Name clashes (as in the originals, kept)

These files redefine system words in FORTH, so load them last or on their own:
- WINDOW: WIDTH, ATTRIB (a WCB field offset), BELL, NORMAL, REVERSE and WINDOW.
- MIDNIGHT: MOVE, TRAVERSE and RING.
- SELECT: ?UPC.
- WYSE50: ?DARK.
- COLPLAY: `>1BGFG`..`>8BGFG` in PATCH (intended).

SPREAD leaves ALGEBRA in the search order, as the original did. SPREAD's own ARRAY/CELLS clash was removed by renaming (see above).

## Built-ins needed

None.

## TODO-PORT / limitations

- **Mouse.** The mouse paths in SELECT (`wbutton`) and COLOURS (`ttbutton`) are inactive while the VM reports no mouse.
- **WINDOW.** Open windows are not re-laid out after a resize; they are only clipped.
- **SELECT and COLPLAY.** These windows have fixed positions and sizes, and are clipped on small terminals.
- **SPREAD.** A resize while a prompt inside a command is waiting for a key is taken as a key. The sheet is redrawn on the next resize, or when the command's own redraw runs.

## Kernel bug found (outside T3; not fixed here)

**FLOAD of any file larger than 16 KB corrupts the heap.**
- **Cause.** INBSEG, the file read buffer POINTER in SEQREAD.SEQ, is allocated in the saved image with 16384 bytes, but BUFSET.SEQ's `bufsize-init` sets IBLEN and IBFULL to IBLIMIT (32000) at start-up. `iblen 0 sizeof!> inbseg` in SEQINIT only changes the recorded size of an already allocated pointer, so FILLBUFF reads up to 32000 bytes into a 16384-byte block. That overwrites the next heap block's header with file text.
- **Symptom.** A later heap allocation fails. Most visibly, the next terminal resize aborts with "No memory for the screen", and SAVESCR/RESTSCR can misbehave.
- **Reproduce.** FLOAD a 20000-byte comment-only file, then walk the heap from `head-limit 15 + -16 and`: the block after the 16400-byte block has a header like $20202020.
- **Files that trigger it.** In T3, SPREAD.SEQ (31 KB) and SELECT.SEQ (17 KB) are large enough.
- **Workaround used in testing.** Run `16384 =: iblen 16384 =: ibfull` first.
- **Possible fix.** In SEQINIT (or `bufsize-init`), free or RESIZE INBSEG when the size changes (e.g. `unpointer> inbseg` before `sizeof!>`), or keep IBLIMIT and the pointer size equal.
