# Group D2 port notes

Files: SOUND SCAN REF FWORDS WINSTACK NEXPECT LISTSET MOUSE MOUSEY MACROS SVSESDAT, plus XEXPECT (not loaded by default).

All twelve files were converted from CRLF to LF. CP437 bytes (the box in REF.SEQ, the rule in MACROS.SEQ, and the control characters in NEXPECT, WINSTACK and MACROS strings) are preserved byte for byte.

## Testing

The files were tested on a build of the committed HEAD (`vm/` and kernel). The working tree's kernel was mid-change at the time and would not load on the old `vm/fpc` binary. The earlier extension files are not ported yet, so the test driver loads a stub file in place of the words they provide (COMMENT, BRACES, `=:`, `I:`, DEFERS, LEDIT, menu and SED words, and so on). The stub file is in the scratch area, not in `SRC/`.

- **Every file compiles against the stubs.** MOUSE was compiled from a copy with the five `BUILTIN` lines replaced by stub definitions, because the VM doesn't provide those built-ins yet.
- **Tested at run time:**
  - SCAN: `scanw`, `-scan` and `-skip`, including the not-found and zero-length cases.
  - REF: `REF AA` finds colon words, including one with `."`, `DO` and literals, and a DEFER that uses it.
  - FWORDS: `searchfile` on a file mixing CRLF and LF lines prints the matching lines without their line ends.
  - SVSESDAT: values saved at `BYEFUNC` are restored by `SESDAT-INIT` in a fresh run, and a 4-byte "old 16-bit" file or a wrong-length file is ignored.
  - MACROS: `savemacs`/`loadmacs` round trip, and `MS:` addressing.
  - MOUSE and MACROS together, with no mouse: `KEY` goes through `mackey` → `mousekey` and returns keys exactly as `(KEY)` does.
- **Desk-checked only:** WINSTACK, NEXPECT, XEXPECT and MOUSEY. They need the real LEDIT, SAVESCR, menu and SED words.

## Per-file changes

### SOUND.SEQ
There is no speaker (`PC!` does nothing).
- `NOTE ( freq ms -- )` now rings the terminal bell once with `%BEEP`, unless freq is 0 (a rest), and then waits `ms` with `MS`.
- `TONE`, `RING` and `NEWBEEP`/`BEEP` keep their names and take as long as they did before.
- The port constants are kept for compatibility.

### SCAN.SEQ
The three CODE words are rewritten in high-level Forth.
- **`SCANW` now scans CELLS** (it scanned 16-bit words). Both of its users scan cell arrays after the port: REF scans token lists, and TOOLS/BLOCK.SEQ scans `rec#use`/`rec#s`. Its length and result are cell counts.
- `-SCAN` and `-SKIP` work backwards from `addr`, as the 8086 code did: the bytes examined are `addr`, `addr-1`, … `addr-len+1`. If nothing is found they return `addr-len 0`.

### REF.SEQ
- `R.NAME` recognises the kind of word from its code token: `@ CT-NEST =`, `@ CT-DODEFER =` and `@ CT-DOUSERDEFER =`.
- It scans the token list at `>BODY @`, in cells, up to the first `UNNEST`. The cap is $140 cells, or $40 cells if no `UNNEST` is found, as before.
- The `colseg`/`sseg` juggling is removed.
- Thread copies use `#THREADS CELLS`, and `Y@` is replaced by `@`.
- As in the original, an in-line literal equal to the xt being searched for counts as a reference.

### FWORDS.SEQ
- The CODE word `searchsetup` becomes `slook.buf count outbuf count`.
- **New headerless helper `-eol ( a n -- a n' )`.** It strips trailing CR and LF characters. Before, the code dropped 2 bytes (`2-`), which is wrong for LF-only host files.
- `.firstline` uses `cols 1-` and `cols 20 -` in place of 79 and 60.
- `fallof` assumes `>FADR ( n -- addr )` and `DIR>PAD ( addr -- a n )` (see the cross-file notes).
- File sizes are still doubles (`ENDFILE`, `bytes_srch D+!`).

### WINSTACK.SEQ
- `$40 $17 c@L` is replaced by `SHIFT-STATE`.
- The wait for the shift keys to be released now calls `REFRESH 10 MS`, so the pop-up is drawn and the loop doesn't spin.
- Screen save and restore are left to `SAVESCR`/`RESTSCR` (SAVESCR.SEQ, which uses `VIDEO-BUF`).
- The depth window is clipped by `ROWS`, as before.
- On a host that can't see the shift keys, `SHIFT-STATE` returns 0 and the window never appears.

### NEXPECT.SEQ
- `>XBUF` now returns the **address** of the current history line (`xbseg +`). All `CMOVEL`/`C@L`/`COUNTL TYPEL` calls became plain `CMOVE`/`COUNT TYPE`.
- `XBINIT` uses `ERASE`.
- The fallback `?SHIFTKEY` uses `SHIFT-STATE`.
- **K-RESIZE.** While NEW-EXPECT runs, it installs `xresize` in LEDIT key table 1 at `K-RESIZE` (using `LKEY@`/`LKEY!`, saving the old entry and restoring it before returning).
  - If the history box is up, `xresize` closes it (`nquit`), because the box was laid out for the old size, then runs the old entry. The next Up or Down arrow redraws the box at the new size.
  - LEDIT itself redraws the edit line.
- The key encoding is unchanged (ASCII, or 128 + scan code).

### XEXPECT.SEQ
Has the same `>XBUF`/`CMOVE` changes as NEXPECT, and no box. Load XEXPECT or NEXPECT, not both.

### LISTSET.SEQ
List space is a fixed VM region (it ends at `LIST-LIMIT`).
- `LISTSET ( paragraphs -- )` now only reports the free list space (`LIST-LIMIT XHERE -`) and whether it covers the request, then calls `BYE` as before. It no longer re-saves the system.
- **`#LISTSEGS` is defined here as a dummy VALUE** (`\- #listsegs 0 value #listsegs`), only if nothing defined it earlier. F-PC.SEQ runs `7400 =: #listsegs` after this file, and the ported kernel doesn't define the word.

### MOUSE.SEQ
- **The six INT 33h CODE words are rebuilt on five new built-ins** (listed under "Built-ins needed"):
  - `show.ms` → `MOUSE-SHOW`
  - `hide.ms` → `MOUSE-HIDE`
  - `init.mouse` → `MOUSE-PRESENT?`, which sets `badmouse`, `mouseflg` and `havemouse`
  - `getmous` → `MOUSE@ 3 AND`
  - `setmous` → `MOUSE!`
  - `mouse.scale` → an empty word
- **Positions are now in character cells.** The `U8/` and `8*` pixel scaling in `mousexy` and `mousexy!` is gone, and the results are still clamped to `COLS`/`ROWS`.
- `initmouse` tests `MOUSE-PRESENT?` instead of the INT 51 vector.
- `track-menu` now reads `mcol cells menulist + @ cell+ c@`.
- **With no mouse,** `mousekey?` and `mousekey` behave exactly like `(KEY?)` and `(KEY)`. That includes `K-RESIZE`: BIOS $FF00 becomes 255.

### MOUSEY.SEQ
- `extcharseg +xseg 0 c@L` becomes `extchars c@` (SEDCHARS.SEQ is already ported to `EXTCHARS`).
- Everything else is unchanged.
- A `TODO-PORT` is added for the fixed hit-test columns and rows.

### MACROS.SEQ
- `macseg` is a POINTER, so it is now an address. `MS: ( off -- addr )` is now `macseg +`.
- `EXHREAD`/`EXHWRITE` are replaced by `HREAD`/`HWRITE`, and `LFILL` by `ERASE`.
- `viewmacs` uses `TYPE`.
- **The F-PC.MAC format is unchanged** (8 × 128 bytes of key codes).
- **`?domac` and `?repmac` exit from MACKEY with `R>DROP`.** It was `2R> 2DROP`; the return stack now holds one cell per nesting level.
- The macro terminator store `0 … !L` became `W!`, so it still writes 2 bytes.
- **K-RESIZE / Alt-8 clash.** `MACKEY` passes a terminal resize straight through: `?resizekey` checks for 255 with `BIOSKEYVAL @ $FF00 =`. A resize is never recorded and never taken as Alt-8, which also encodes as 255 (see the cross-file notes).

### SVSESDAT.SEQ
- **New F-PC.SES format.** It has a 16-byte header: magic `"FS32"` ($32335346), `SESSTAMP` (a double, 2 cells) and the total data byte count. The raw bytes of each range follow.
- **Old or mismatched files are ignored, never half-loaded.** The file is read only if the magic, the stamp and the total match, and the file length is exactly 16 + total. An old 16-bit file fails these checks.
- **Range entries are 4 cells:** link, base-cfa, addr, size. The base-cfa returns a base address: a POINTER, or `SESABS` (returns 0) for code-space data, in place of `?CS:`.
- If a POINTER has no memory, nothing is written at `BYE`, and nothing is read at start-up.
- The example registrations use `CELL` for `NORMVAL`, `REVVAL` and `ATTRIB` (VALUEs and variables are 4 bytes now).

## Built-ins needed

These are for MOUSE.SEQ. They are declared with `BUILTIN`, so **F-PC.SEQ stops at MOUSE.SEQ until the VM provides them.** Trivial versions are enough to start with: `MOUSE-PRESENT?` → 0, `MOUSE@` → 0 0 0, and the others as no-ops.

| Name | Stack | Semantics |
|---|---|---|
| `MOUSE-PRESENT?` | `( -- f )` | TRUE (-1) if the host can deliver mouse input (an SDL window, or a terminal with xterm mouse reporting, which the host may switch on at this call). Otherwise 0. Replaces INT 33h AX=0. |
| `MOUSE@` | `( -- x y buttons )` | The last known pointer position in **character cells**, with 0 ≤ x < COLS and 0 ≤ y < ROWS, plus the buttons held now: bit 0 left, bit 1 right, bit 2 middle. It must not block. It may process pending host input, but must not consume keystrokes; mouse escape reports must not reach BIOSKEY. With no mouse it returns 0 0 0. Replaces INT 33h AX=3 (which returned pixels). |
| `MOUSE!` | `( x y -- )` | Moves the pointer to cell (x, y), clamped. A host that can't move the pointer just records the position, so that `MOUSE@` returns it until the mouse moves. Replaces INT 33h AX=4. |
| `MOUSE-SHOW` | `( -- )` | Shows the host's mouse cursor. Idempotent: it sets a flag, it is not a counter. It is called on every key wait, so it must be cheap. Replaces AX=1. |
| `MOUSE-HIDE` | `( -- )` | Hides the mouse cursor. Idempotent. Replaces AX=2. |

## TODO-PORT

- **MOUSEY.SEQ:** the button drivers hit-test fixed screen positions of other modules' layouts: the SED status line (F10 at columns 73–77, scroll areas), the print dialog, the browse prompt and the WFL window. If those modules move to layouts relative to `COLS`/`ROWS`, these tests must follow.

## Cross-file notes

- **K-RESIZE (255) has the same F-PC key code as Alt-8** (BIOS $7F00 → 128 + $7F = 255). Code that cares has to check `BIOSKEYVAL @ $FF00 =`, as MACROS does. The kernel or the VM may want a different resize code.
- **FWORDS assumes WFL.SEQ's port gives `>FADR ( n -- addr )` and `DIR>PAD ( addr -- a n )`**, with the counted 8.3 name at `addr`.
- **MOUSE assumes the MENUS.SEQ port lays out a menu as** `[cell: list address][count byte][strings]`, with MENULIST a cell array, so the item count is at `@ cell+ c@`.
- **REF needs from the WORDS.SEQ/LARGEST.SEQ port:** `LARGEST ( a n -- a' link )` over a cell array of thread heads, `.VYET`, `VADDR` and `TOTALWORDS`.
- **The DEFERS port must handle user defers.** MOUSE and MACROS do `DEFERS KEY?` and `DEFERS KEY`, so DEFERS must use `>IS`, not `>BODY`.
- **NEXPECT and XEXPECT need from the LEDIT port:** `LKEY@`/`LKEY!`, `<LEDIT>` leaving a flag, `EDITBUF` as a counted buffer at a plain address, and K-RESIZE (255 → table entry 127) dispatched through `?func`.
- **SVSESDAT** needs `GETTIME ( -- d )` from TIMER/TIMESTUF, and registers `xbseg` from NEXPECT.
- **`#LISTSEGS`:** STATUS.SEQ and SAVESYS.SEQ also use it in their originals. Its owner should define it properly in the kernel or F-PC.SEQ. LISTSET's `\-` definition steps aside if it already exists.
- **Interpret-mode `"` compiles in the bare kernel.** UTILS presumably makes it state-smart. This doesn't affect these files (they use it only inside definitions).
- **VM (not touched):** `FLOAD` of a path longer than about 64 characters corrupts the HCB name and loops. This may be fixed by the new 127-byte `B/FILENAME`.
