# Group D1: SED add-on files

Files: PRTCTRL PRINTING LASERJET PROPRINT SEDCASE SEDITWP SEDJUST SEDDRAW SEDSORT SEDCOPY SEDAPND SEDPAGE SEDWIND SEDCHARS SEDSHELL HTYPE SEDWHELP TOPEDIT HELPLINK EDITSET SEDMENU NEWFILE EDITERR BROWSEPR MLOAD (all `.SEQ` in `SRC/`).

Every file has `F-PC native port` on its header line. Files with no other changes: LASERJET, PROPRINT, SEDCASE, SEDJUST, SEDCOPY, SEDAPND, SEDPAGE, HELPLINK, SEDMENU, NEWFILE, MLOAD.

## Status

**Load test.** All 25 files load in order on top of a driver with the following loaded:
- the files from F-PC.SEQ up to PRINT.SEQ that load today;
- group C's EDITSTUF, SEDCODE, SEDITOR and SEDIT2, with only `SEARCH` stubbed.

The only words still stubbed come from unported group A/B files:
- MENUS.SEQ: `newmenu menuline" newmenubar +," endmenu menu savemenu restmenu mbutton menubar menulist mline mcolumn doother mcol`
- WFL.SEQ / EXEC.SEQ / hyper.seq: `autoclear dirspec$ hfind tx ty doLF`
- defers from files not loaded: `editfile dofhelp clearmem dolisting makefile`

**Run tests.** Checked by running:
- the PRTFUNC `%B` toggling;
- `EXTCHAR@` over the new table (row 1 col 2 = 10, row 4 col 0 = $80, last = $FF, 20 rows);
- `get_file_date&time` on an existing and a missing file;
- the PRTMENU item walk (`>pitem`);
- `HYPERTYPE` on strings with a hyperchar and a hyperdest.

Everything else is desk-checked only.

## Per-file changes

**PRTCTRL**
- `PRTFUNC` now stores its escape char as a cell (`create ,`), so the three xts that follow are cell aligned.
- The DOES> part uses `cell+` and `cells` (`r@ cell+ perform abs 1+ cells r@ + cell+ perform`).
- A header comment explains where printer output goes (see "Printing" below).

**PRINTING**
- `get_file_date&time` was a CODE word (INT 21h/57h). None of the seams takes BX and returns CX/DX, so it is now Forth with the signature `( hcb -- time date )`:
  - it does FIND-FIRST on the handle's name into a private DTA (`fdta`) and reads the DOS time at +22 and the date at +24;
  - a name that isn't found (including one that doesn't fit 8.3) gives 0 0.
- `setfile_date&time` no longer opens the file.
- PRTMENU records are `x , y , ," text" var ,`. The walks use `2 cells +` and `cell+` where they had `4 +` and `2+`. The strings are unaligned and `,` doesn't align, so `count +` still lands on the cell.
- `.header` reads line 0 with `0 #lineseg count`, matching group C's `#LINESEG ( n -- addr )`.
- New `presize`: K-RESIZE in the Alt-P menu calls `sresize`, then redraws the form.

**SEDITWP**
- `#lineseg 0 c@l` becomes `#lineseg c@` (×2).

**SEDDRAW**
- `drawinit` is split into `drawhelp`.
- New `drawresize` (`sresize` plus the pen state and help line).
- The key loop handles K-RESIZE.

**SEDSORT**
- The `#lineseginfo` copy is now `( a n )`: `i #lineseginfo 2- sort2.buf swap colsave 15 + min cmove`.

**SEDWIND**
- The adjust-window loop handles K-RESIZE (`sedlayout dark`, then the existing redraw).
- Unzoom and unsplit call `sedlayout` after restoring the saved geometry, which clamps it to the current screen.
- New `wind-layout`, installed into group C's `SEDLAYOUT` defer. It runs `%sedlayout`, then for a split window:
  - clamps `splitline#` to the screen;
  - gives the current half (`topwind?`) the rows on its side of the split.

**SEDCHARS**
- The character table was a paragraph segment built with `X,"` and read with `C@L`/`TYPEL`. It is now `create extchars` with plain `,"` strings.
  - The LF patch is now `here ," ..." 10 swap 6 + c!`.
  - `extchar@` walks it with `count +`.
- The box drawing is split into `.charbox`. The row strings are shown with `VIDEO-TYPE`, which writes control chars raw as VIDEO-TYPEL did.
- K-RESIZE in the selector does `restscr charresize savescr .charbox`.
  - `charresize` is a new defer, set to `sresize` only when the editor is loaded, because SEDCHARS can load without it.
- The source keeps its literal CR, TAB and control bytes inside the strings, and the current reader loads them correctly.

**SEDSHELL**
- `%doDOS` no longer moves the last line or uses SETBLOCK. It is now `putline >norm command.buf count SYSTEM` followed by `cursorset getline`.
- An exit status of 127 shows "Couldn't find the command". An empty line starts `$SHELL`, which the VM's SYSTEM handles.
- It calls SYSTEM directly rather than EXEC.SEQ's `$SYS`, because `$SYS` returns DOS error codes (2, 8), not an exit status.

**HTYPE**
- `hypertypeL ( seg a n )` becomes `hypertype ( a n )`.
- `normtype` (a VALUE) is the TYPE in force at load time. It replaces `defers typeL`.

**SEDWHELP**
- `$2020 ... !` becomes `w!` (×2). The 32-bit `!` would write 2 bytes past HELPBUF.
- `worddefer`'s `@rel>abs` code-field tests become `@ ct-dodefer =` (→ `>body @`) and `@ ct-douserdefer =` (→ `>is @`).
- `flipfiles` reads the flat handle stack: `hseg b/hstk * +`, `count type`, `b/hcb + @` for the saved LOADLINE, matching group C's EDITSTUF layout.
- TYPEL switching is converted; see "TYPEL" below.

**TOPEDIT**
- `$2020 ... w!`.
- TYPEL switching is converted (×3 places); see "TYPEL" below.
- `unedit` also clears `hseg` and `wseg`, which are now addresses into BASESEG.

**EDITSET**
- Comment only: `255 fnset ecmdtgl` (Alt-8) can't be reached, because 255 is K-RESIZE and `<REEDIT>` takes it first. ECMDTGL is still available from the Help menu.

**EDITERR**
- The error arrows are centred with `cols 2/ 10 -` instead of a literal 30.

**BROWSEPR**
- TYPE is redirected where TYPEL was: `prtypeL` becomes `prtype ( a n )` using `PRNTYPE`.
- The redirection is `['] prtype save!> type`.
- `outfix`, the error path, restores the previous TYPE from the new value `oldtype`.
- The byte counter is typed with `VIDEO-TYPE` instead of QTYPEL.

## TYPEL

The kernel no longer has TYPEL, and TYPE does all typing. The original had TYPE → (TYPE) → TYPEL, so swapping TYPEL changed every TYPE. The port therefore swaps TYPE itself:

| Original | Port |
|---|---|
| `['] hypertypeL is typeL` | `['] hypertype is type` |
| `['] (typeL) is typeL` | `['] (type) is type` |
| `(lit) defers typeL is typeL` | `normtype is type` |

Group C's `sltypel` defaults to TYPE, so browse mode's hypertext display works through this.

## Printing

There is no printer port. PRNHNDL is DOS handle 4 (PRN), which the VM discards, and `?PRINTER.READY` is always true. To get printed output, do one of these:
- run `PFILE name` (PRINT.SEQ) before printing;
- in SED, use `S` in the Alt-P menu to choose a file. `pmenu` closes it with PCLOSE afterwards.

`>B` / `>BROWSER` (BROWSEPR) prints into BROWSE.PRN and opens it in the browser.

## Built-ins needed

None.

## TODO-PORT

None in the files. Open points:
- **Fixed popup positions.** Popups (box&fill dialogs, menus) keep their coordinates, which need a screen of at least 80x25. Key loops redraw on K-RESIZE: Alt-P menu, line drawing, character selector, window adjust. Single-key prompts just treat 255 as "other key" and the editor redraws afterwards (group C's SRESIZED).
- **8.3 limit on the printed file date.** It comes from FIND-FIRST. A `FILE-DATE ( hcb -- time date )` built-in (fstat) would remove the limit, if wanted.
- **Shell output and the "Press a key" prompt.** SEDSHELL's `docompile` does `savescr dark`, runs the command, then on the virtual screen shows "Press a KEY to return". Whether the host keeps the command's output visible until that key is up to the host's SYSTEM suspend and resume. The original showed both on one screen.

## Cross-file notes

- **Group C.** I rely on:
  - `#LINESEG ( n -- addr )` and `#LINESEGINFO ( n -- a len )`;
  - the `SEDLAYOUT` defer and `%SEDLAYOUT`, with SEDWIND installing `wind-layout`;
  - `SRESIZE` and `SRESIZED`;
  - the handle stack layout (HCB, then LOADLINE SCREENCHAR ?BROWSE LINESAVE as cells).
- **Group B.** QVIDEO should keep `?DOSIO` (UTILS has it) and `(TYPE)`. HTYPE captures TYPE at load time (after QVIDEO's FAST), as the original's DEFERS did.
- **Integrator, about FLOAD from long paths.** A nested `FLOAD` from a driver in the long scratchpad path failed with "Invalid code field at $1290C". Loading the same driver from a short path (`/tmp/claude-1000/d1t`) worked. `FLOAD .../stubs.seq` and `.../autostub.seq` also failed oddly (the error was just the file name, with context FILES), while `zz.seq` and `as.seq` worked. Probably file-name handling in FLOAD / the FILES vocabulary.
