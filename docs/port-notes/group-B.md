# Group B port notes: screen/UI extensions (IBMCURSR .. PRINT)

Status: all 23 files are ported in place and converted to LF line endings. The CP437 bytes in
BOXTEXT, HELLO, WFL and MENUS were checked byte for byte. FL.SEQ's trailing `^Z^Z` (DOS EOF
marker) was removed.

The files were loaded on top of the real, partly ported group A files: TIMER .. CASE, LOADEXE,
DBGFIX, PATHSET, HYPER, SEARCH, LARGEST, WORDS. PASM, NEWLAB, SAVEEXE and DEBUG did not load
yet and were skipped. With that, every group B file loads. The one exception is ENVIRON.SEQ,
which stops at `BUILTIN GETENV` until the VM provides it. It was tested with a scratch copy
that stubs the two built-ins.

Tests run (batch host, results checked through a copy of VIDEO-BUF):
- **Boxes and pertype.** `box` and `.box"`, plus `\1` attributes, `\@xxyy` and `\sNN` in `."`.
- **Screens and lines.** `SAVESCR`/`RESTSCR` (including nesting and underflow), `IBM--LINE`,
  `IBM-AT`/`IBM-AT?`, `INVERT-SCREEN`.
- **Colorizer.** It gives the right colour for colon, variable, does and code words.
- **Editing and definitions.** `LINEEDITOR` with keystrokes (overwrite, insert, ^A ^S ^V,
  Enter), `MAKEDEFER`, `SWITCH`, `MARK`.
- **Sign-on and status.** `.HELLO` and `.CURFILE`. The `STATUS` line shows during loading.
- **Views, menus and file picker.** `>VIEWFILE`, `.FILES`, `DEFMENU` (draw, Enter, choose an
  item) and `<GETFILE>` (letter search, Enter, returns the full path).
- **Commands and printing.** `DIR`, `CHDIR`, `SYS`/`` ` ``, `?SYSERROR`, and
  `PFILE`/`PCLOSE` with printing to a file.

Not tested: the `K-RESIZE` paths (the batch host cannot make one), and anything that needs a
real terminal.

## Built-ins needed

| Name | Stack | Semantics | Why |
|---|---|---|---|
| `GETENV` | `( a1 n1 -- a2 n2 f1 )` | Look up the host environment variable named by the string a1,n1 (no `=`, case-sensitive). If it is set: a2,n2 is its value copied into a VM buffer (valid until the next GETENV/ENVSTRING call), and f1 is true. Otherwise `0 0 false`. | ENVIRON.SEQ (`"envfind`, `comspec@`, `path@`, `me@`). There is no DOS environment segment. |
| `ENVSTRING` | `( n1 -- a2 n2 f1 )` | The n1th (0-based) entry of the host environment (`environ[n1]`), as `NAME=value` in the same kind of VM buffer, with true. If n1 is past the end, `0 0 false`. | `.env` lists the whole environment. |

Both are declared with `BUILTIN` at the top of ENVIRON.SEQ, so **F-PC.SEQ stops loading at
ENVIRON.SEQ until the VM provides them**.

## Per file

- **IBMCURSR.** `IBM-AT` is now `AT-XY` and `IBM-AT?` is `GET-XY`. `IBM-DARK` fills VIDEO-BUF
  with `$0720` (blank, attribute 7, as the BIOS mode reset did) and homes the cursor.
  `IBM--LINE` uses `BIOS-VIDEO` AH=6 from the cursor row to `ROWS 1-`/`COLS 1-`. `LFILLW` is
  now `( addr byte-count word -- )` (flat address, no segment). `NORM-DARK` fills VIDEO-BUF.
  The rest (cursor shapes, `>IBM`, `CURSOR_POS_INIT`) is unchanged.
- **MONOCROM.** `INVERT-SCREEN` walks the attribute bytes of VIDEO-BUF.
- **COLOR.** No change. `?VMODE` is always 3, so `>COLOR` is installed.
- **BLINKER.** `blink!`, `pal!` and `border!` do nothing, and `border@` returns 0. There is no
  video hardware; the renderer treats attribute bit 7 as a bright background, which is the
  same as BLINKOFF.
- **COLORIZE.**
  - `CLR-OTHER` no longer tests for a CALL opcode (232). It is "DOES> word (`DOES?`) → does
    colour, otherwise code colour", because there are no `;CODE` words.
  - `COLORIZE` passes the code token (`cfa @`) to `DEFINITION-CLASS`, which matches DECOM.SEQ
    as ported.
- **BOXTEXT.** `(.box)` reads its in-line string directly
  (`r> dup count + aligned >r`). It used `X>"BUF`, which the native kernel lacks and nothing
  else uses. `<box>` already clipped to `COLS`/`ROWS`.
- **SAVESCR.** Rewritten.
  - **Storage.** Each saved screen is now an `ALLOCATE`d block, `[cols][rows][cells…]`, kept on
    `SVSTACK` (most recent first, at most `SVMAX` (4), limit 16). Saving more than SVMAX drops
    the oldest, as before.
  - **Restoring.** `RESTSCR`, `RECOVERSCR` and `RECOVERLINE` copy only the area the saved
    screen and the current screen share, so a resize between save and restore is safe.
  - **Edge cases and leftovers.** `RESTSCR` with nothing saved does nothing. `SVSEG` is gone;
    `SVSIZE` stays as "size at last save".
- **QVIDEO.**
  - **TYPE.** `QTYPEL` is replaced by `QTYPE ( a n )`: `(TYPE)` when printing, otherwise
    `VIDEO-TYPE`. `FAST` and `SLOW` switch the user defer `TYPE` (there is no TYPEL any more).
  - **EMIT and CR.** These still go through handle 1 (the BIOS TTY at the same cursor).
    VIDEO-TYPE leaves the cursor after the text, so `#OUT`/`#LINE` and the cursor agree.
    Known, as in the original: a TYPE clipped at the right edge leaves `#OUT` at `COLS 1-`.
- **PERTYPE.**
  - **Segment code removed.** The `SSEG`/`@L` versions are gone: `\%spaces`, `\%tenths` and
    `\%at` use `W@`, `\typeL` is removed, and `\type ( a n )` works on any address.
  - **In-line strings.** `(\.")` steps over the in-line string with `ALIGNED`.
- **HELLO.**
  - **Sign-on box.** The box is centred with `.HINDENT` (`COLS 64 - 2/`), replacing the
    `\t`/`\sNN` positions, and is the same as before on 80 columns.
  - **Start-up and MARK.** `HELLO` uses `TIB0 'TIB !` (was `SP0 @`). `MARK` remembers `XHERE`
    and restores `XDP`, with no list segments.
- **LEDIT.**
  - **Key tables.** `FUNCARRAY` puts its table in the CREATE body (`does> swap cells +
    perform`), and `LKEY!`/`LKEY@` index it with `cells`.
  - **Stripping blanks.** `strip_bl's` uses `W@`.
  - **Resize.** `DOKEY` handles `K-RESIZE` by calling the new defer **`LEDRESIZE`** (default
    `NOOP`). The loop then redraws the edit line.
- **VIEW.**
  - **Name and thread walk.** `NAME>PAD` uses a flat `CMOVE`. `VIEWFILE` walks the 64 thread
    cells (`#threads cells`, `cell +loop`, `@` for the links). `>VIEWFILE` uses `>VIEW @`.
  - **Kernel words have no file.** The kernel is loaded by the seed and has no FILES
    variables, so kernel words return an empty file name. `ChangeFilesVariables` and its
    META86 helpers (`files_set`, `1file`, `'f-pc.seq`) were removed.
- **STATUS.**
  - **Free space.** The free-space figures are now `LIMIT HERE -` and `LIST-LIMIT XHERE -`.
  - **Vocabulary stack.** It is walked with `cell+`.
  - **Resize.** The new **`STATRESIZED`** is chained into `RESIZED` and redraws the status
    line after a resize.
- **FL, NEEDS, FILSTAT, PRINT.** No code changes.
- **WFL.**
  - **Directory entries.** `>FADR ( n -- addr )` is a flat address into DIRSEG (attribute byte
    at -1, then the counted name). `FOFF+` is high-level. Everything that used `c@l`, `cmovel`,
    `countl`, `+placel` or `typeL` now uses plain memory words. The root and `..` tests use
    `W@`.
  - **Layout.** `FORGX` and `DLEN` are now VALUEs set by **`WFLGEOMETRY`**, which centres the
    window horizontally and sizes the list to `ROWS`. The window drawing is factored into
    `WFLFRAME`.
  - **Resize.** On `K-RESIZE`, `KEYTESTS` calls `WFLRESIZED`, which recovers the saved screen,
    recomputes the layout, redraws and keeps the current file visible. The window still needs
    about 78x24 to show everything.
- **ENVIRON.**
  - **Lookups.** Uses GETENV and ENVSTRING. `"envfind` is now
    `( a1 n1 -- a2 n2 f )` and returns the value instead of an offset; a trailing `=` on the
    name is ignored. It is only used inside ENVIRON and EXEC.
  - **Command spec and path.** `comspec@` tries COMSPEC and then SHELL. `PATH$` is now 256
    bytes.
  - **ME@.** It uses `$_` (set by bash and others to the program path) and otherwise leaves
    `ME$` empty, which the original allows.
  - **Handles.** The new helper `env>hcb` limits strings to `B/FILENAME 2-`.
- **EXEC.**
  - **Shelling out.** The EMS/disk swap-out (`fpc>emm`, `fpc>disk`, `fpc>out`) and the vector
    and critical-error juggling are gone. `$SYS` copies the command into `EXEC$` and calls
    `<EXTEXEC>` (SYSTEM); an empty command starts an interactive `$SHELL`.
  - **Errors.** `?SYSERROR` now aborts on shell status 127 ("Command not found") and 126 (the
    shell's DOS-error equivalents).
  - **Command buffer.** `cmdbuf` is a 256-byte buffer. It used to be `RP0 @ 100 -`, which is
    inside the return stack now.
  - **Mapping the DOS commands to the host:**

    | Command | Native behaviour |
    |---|---|
    | `DIR` | Built in, using `$dir`/FIND-FIRST: DOS wildcards, case-insensitive, 8.3 names only. The default spec is `*.*`. |
    | `CHDIR`/`CD` | DOS function 3Bh, so F-PC's own directory changes. With no argument it prints the current directory, as DOS does. |
    | `DEL` | `rm` |
    | `COPY` | `cp` |
    | `REN` | `mv` |
    | `SYS`/`` ` ``/`` `` `` | Pass the line to the shell unchanged. |
  - **Path separators.** `"syscommand` turns `\` into `/` in the user's arguments.
  - **Unchanged.** `"swapfile`/`swapfile` are kept but do nothing (there is no `EXTHNDL`).
    `initcmdpath` copies the shell path into `CMDPATH` for information only.
- **EMMPTR.** Stubbed: there is no EMS. `EMM-POINTER` still creates and chains pointers (cell
  layout), but executing one aborts with "No expanded memory available!".
  `EMM-POINTERINIT` is kept; `EMM-BYE-FUNC` is removed.
- **MENUS.**
  - **Menu layout.** `NEWMENU`'s body is `[list-address of the function xts][count][strings…]`.
    `MENULINE"` lays the xt with `X,`, and every `2* … 2+` became `cells … cell+`.
  - **Return stack.** `SAVEMENU`/`RESTMENU` use `r>`/`>r` (one return-stack cell per level).
  - **Shell errors.** `do-dos` reports the shell's 127 and 126 codes.
  - **Resize.** In the `MENU` loop, `K-RESIZE` restores the original screen, re-saves it at the
    new size and redraws the menubar.
- **MAKEDEF.**
  - **makedefer.** It swaps code tokens with `!CT`: name2 becomes `CT-NEST` with name's
    token-list body, and name becomes `CT-DODEFER` pointing at name2. The CALL/JMP patching is
    gone.
  - **Other words.** `:def?` tests for `CT-NEST`, and `xchange` is high-level.

## TODO-PORT

- VIEW: words in the kernel cannot be VIEWed, because the seed records no FILES variables for
  KERNEL*.SEQ (noted in VIEW.SEQ).
- ENVIRON: needs the `GETENV`/`ENVSTRING` built-ins (above).
- WFL and HELLO lay out for at least ~78x24 and 64 columns respectively. On smaller terminals
  they are clipped rather than reflowed.
- The `K-RESIZE` handling in LEDIT, MENUS, WFL and STATUS has not been run; it needs the tty
  host and a real resize.

## Cross-file notes

- **New hooks.**
  - `LEDRESIZE` (LEDIT, hidden): a defer run when the line editor gets `K-RESIZE`. Callers that
    draw around the edit line can `save!>` it.
  - `STATRESIZED` is in the `RESIZED` chain.
  - `WFLGEOMETRY`, `WFLFRAME` and `WFLRESIZED` (WFL, hidden/headerless).
- **Changed stack effects.**
  - `LFILLW ( addr bytes word -- )`, which had a segment before.
  - `>FADR ( n -- addr )`, which returned `seg off`.
  - `"envfind ( a n -- a' n' f )`.
  - `dir>pad ( addr -- a n )`.
  - `\typeL` is removed; use `\type ( a n )`.
  - `QTYPEL` is replaced by `QTYPE ( a n )`.
- **Assumptions about group A.**
  - `DOES? ( cfa -- pfa f )` is true for DOES> words (UTILS).
  - `DEFINITION-CLASS` takes a code token (DECOM).
  - `?DOSIO`, `>ATTRIBn`, `DOBUTTON`, `I:`, `DEFERS`, `\u` and `GETTIME`/`BUILD-HM` keep their
    original stack effects.
- **SAVESCR** no longer has `SVSEG`. Nothing outside SAVESCR used it.
- **Possible bug outside this group (seen while testing).** A FLOAD of a file given with a
  `/`-style absolute path makes LOADED, try to create a FILES variable named after the whole
  path ("Name TOO LONG"). Nested inside another FLOAD, that error printed garbage. Paths
  written with `\` work.
