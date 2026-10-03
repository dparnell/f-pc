# Port notes: group A

Files, in load order: TIMER TIMESTUF COMMENT UTILS UNHEAD BRACES VOCABS DEFERS BUFSET VALIDATE DECOM DUMP CASE PATHSET HYPER SEARCH LARGEST WORDS.

**Status:** all 18 files load cleanly, in `F-PC.SEQ` order and with `F-PC.SEQ`'s in-between lines (`0COMPILER`, `WARNING OFF`, `HWORDS-`). The other groups' files in between (PASM, NEWLAB, LOADEXE, SAVEEXE, DBGFIX, DEBUG) were left out. Each file keeps its CRLF endings, and DUMP.SEQ keeps its CP437 box characters.

Tested in batch mode:

- `SEE` on every word in FORTH, HIDDEN, ROOT, USER and FILES (1275 words) with no errors.
- `SEE` on IF/ELSE, DO/LOOP, ?DO/+LOOP, BEGIN/WHILE/REPEAT, UNTIL, CASE/OF/IF-OF, FOR/NEXT, `."` `"` `ABORT"`, `[']`, `IS`, `%!>`, `EXEC:`, DOES> defining words and their children, CONSTANT/VALUE/VARIABLE/USER/DEFER/USER DEFER, built-ins and vocabularies.
- `DUMP`, `WORDS` (plain, a substring, `THESE`, `VALUE.*`, `TOTAL.*`, `CODE.*`), and `ONLY` `ALSO` `PREVIOUS` `ORDER` `VOCS`.
- `DEFERS`/`UNDEFER`, including a user defer (EMIT).
- `COMMENT:` `/*` `#IF/#ELSE/#THEN` `.COMMENT:`, `\\ ... { }` files, and `\U` `\FPC` `\TCOM`.
- `SEARCH`, `LARGEST`, `HFIND`/`VFIND`.
- `.TIME` `.DATE` (all three formats), `TIMER`, `MS`, `SECONDS`, `TENTHS`, `.COMPSTAT`.
- `.FREE` `.USED` `USED` `.MEM` `ALIAS` `AT` `DARK` `PAGE`.
- Headerless words plus `BEHEAD` under `HWORDS+`.
- `PATHSET`, with FLOAD from the current directory, a subdirectory (`sub\X.SEQ` and `sub/X.SEQ`), and `..\SRC\`.

## Per file

- **TIMER**: `STIME` and `TTIME` are 2VARIABLEs. `B>T` stores into TTIME at 4-byte cell offsets, so `TTIME 2@` still gives `( HM Sh )`. The VM refuses SETDATE/SETTIME (DOS 2Bh/2Dh), so they print "Invalid".
- **TIMESTUF**: `.COMPSTAT`'s 16-bit range juggling is replaced by `lines*6000/centiseconds`. SECONDS and TENTHS still busy-wait on the clock (TODO-PORT below).
- **COMMENT, BRACES, CASE**: unchanged apart from the header line.
- **UTILS**:
  - `YCOUNT` uses `C@`.
  - `.FREE` reports `LIMIT`/`LIST-LIMIT`/`HEAD-LIMIT` minus `HERE`/`XHERE`/`YHERE`.
  - `DSBUF`, `!USED` and `.USED` use cells, with the region bases as initial values.
  - `SAVECURSOR` and `RESTCURSOR` use `R>`/`>R` (one cell per return frame).
  - `ALIAS` uses `!`; `>NAME.ID` uses `C@`.
  - `!TBL+` and `@TBL+` step by a cell.
  - `.MEM` walks the POINTER chain with the new body layout. The "DOS memory available/total" lines are gone (there is no heap-free query), and the EMM branch is cell-converted but never runs.
  - New words: `FIRST-DOES-CT` (= `' FORTH @`, the first non-built-in token), `LIST>CFA ( list-addr -- cfa|0 )` with helpers `(L>C)` and `L>C-BEST`. `'DOCOL` is now `CT-NEST`.
  - `DOES? ( cfa -- pfa f )` is true for tokens at or above FIRST-DOES-CT. Its stack effect is the same as before, so COLORIZE.SEQ can keep using it.
  - `>.ID ( list-addr -- )` names the colon definition whose token list holds that address.
- **UNHEAD**: ported faithfully.
  - `Y@`/`Y!` become `@`/`!`, and threads are stepped by `CELLS`.
  - Internal heads are built down from `HEAD-LIMIT 128 -`. The 128 bytes leave room so SPCHECK doesn't trip.
  - The head size is `count(≤WIDTH) + 13`, aligned down to a cell, with an "Out of HEAD memory" check against `YHERE`.
  - `HWORDS-` in F-PC.SEQ disables it, as in the original.
- **VOCABS**:
  - `2+`/`2*` on CONTEXT become `CELL+`/`CELLS`.
  - **New:** the seed left primitive `ONLY` `ALSO` `PREVIOUS` headers in FORTH, which shadowed ROOT's versions, so ROOT was never put into CONTEXT. VOCABS now ALIASes FORTH's `ONLY` `ALSO` `PREVIOUS` to the ROOT words.
- **DEFERS**: a user defer is detected with `@ CT-DOUSERDEFER =`, and colon definitions with `CT-NEST`. UNDEFER reads the first token with `>BODY @ @`. Note that the comment's `UNDEFER JUNK2` is wrong in the original too: the syntax is `UNDEFER <deferword>`.
- **BUFSET**:
  - `bufsize-init` no longer times a PAUSE loop at every cold start. It sets `IBLEN`/`IBFULL` to `IBLIMIT`, and the old code is kept inside `comment:`.
  - `MS` waits on the centisecond clock (new helper `CS-NOW`) and handles midnight.
  - `FUDGE` is kept but unused.
- **VALIDATE**: the body stays commented out with `{ }`; only the header line changed.
- **DECOM**: rewritten for the new list layout.
  - Instruction pointers are **absolute list addresses**: `DECOMSEG` holds the list address and `DECOMSEG@` is `@`.
  - Operands are cells. In-line strings are counted and padded to a cell (`."X$" ( a -- a' )`).
  - `(")` now has an in-line string; it used to have a pointer.
  - `(DOES>)` prints `DOES>` and skips its token cell. It is class 9, replacing `(;CODE)`.
  - `NEXT|` (FOR...NEXT) is class 13, replacing `(;USES)`.
  - The definition class is chosen by code token (`CT-NEST` ... `CT-DOVALUE`). Built-ins print "is a built-in primitive (in the VM)".
  - DOES> children print `name <definer> DOES> <clause> ;`. `DOES-LIST ( ct -- list|0 )` finds a clause by scanning list space down from XHERE for the `(DOES>) ct` pair. The seed's VOCABULARY token (FORTH, ASSEMBLER) prints "is a VOCABULARY".
  - `.PFA ( cfa -- )` keeps its stack effect and calls the new `.LIST ( list-addr -- )`.
  - The decompile loop also stops at XHERE.
  - **Seed shadows:** the kernel's first definitions were compiled with the seed's built-in `(")` `(.")` `(ABORT")`, which the kernel later redefines under the same names. `FIND-SHADOWS` (run at load) records such pairs, and `CANON` maps an xt to its current version before classification.
- **DUMP**:
  - Flat addresses are shown as 8 hex digits under the header column "ADDRESS".
  - **`LDUMP` is now `( addr len -- )`**; it was `( seg addr len -- )`.
  - `YDUMP` and `XDUMP` are now plain dumps (`XDUMP` was `( list-seg n -- )`).
  - `DUMPSEG` is kept but unused, and `%DUMPC@` is `C@`.
- **PATHSET**:
  - `prepend.path` builds `C:\<cwd>\name` from PDOS. Lengths come from `B/FILENAME`, and the PDOS string length comes from `SCAN`.
  - Names starting with `/` count as absolute, like those starting with `\`.
  - **Behaviour change:** if the result wouldn't fit, or the cwd is 63+ characters (PDOS truncates at 63), the name is left relative and the flag is **false**, because the host resolves it against the same directory. The original returned true, which made HOPEN fail.
  - It now always returns false for a non-empty name.
- **HYPER**: `OVER.SWAP.HASH.@ ( a1 voc -- a1 lfa )` was a kernel CODE word. It is now high-level and defined here, since nothing else defines it. Threads step by `CELLS`, and `VFIND` pads with `$20202020`.
- **SEARCH**: the 8086 CODE word is now high-level Forth, comparing at each position with `COMPARE`, which follows CAPS. The stack effect and results are unchanged: an empty pattern gives `0 true`, and a buffer shorter than the pattern gives `dcnt false`.
- **LARGEST**: high-level now, over cells compared unsigned.
- **WORDS**:
  - `?INNAME` copies the name with `CMOVE`.
  - Types are recognised by `CT-xxx` tokens.
  - "CODE" words are built-ins: tokens below `FIRST-DOES-CT` other than the CT runtimes.
  - Threads are stepped by `CELLS`, and `Y@` is `@`.

## Built-ins needed

None are needed for loading. One is recommended:

- **`MS ( n -- )`**: sleep n milliseconds through the host (`host_sleep_ms`, kernel-design.md §6). If it is added, delete the Forth `MS` in BUFSET.SEQ. SECONDS and TENTHS could then sleep in slices instead of spinning. The current Forth MS busy-waits, calling PAUSE, at centisecond resolution.

## TODO-PORT

- TIMESTUF and BUFSET: busy-wait timing (see MS above).
- UTILS `DOES?`: AM `CODE` words will also get tokens above FIRST-DOES-CT. Distinguish them from DOES> tokens once the AM exists.
- UTILS `.MEM`: the EMMPTR chain layout, if expanded memory ever returns.

## For other groups and the integrator

- **DEBUG.SEQ** (DECOM's client): `PFASAV` and the IPs it compares are now **absolute list addresses**, not offsets from DBSEG. `DECOMSEG` holds the current list address. Use `.LIST ( list-addr -- )` or `.PFA ( cfa -- )`.
- **Changed stack effects:** `LDUMP ( addr len -- )`, `XDUMP ( addr len -- )`, `."X$" ( a -- a' )`.
- **New words others may use:** `FIRST-DOES-CT`, `LIST>CFA`, `DOES-LIST`, `CANON`, `CS-NOW`.
- **Seed leftovers in FORTH:**
  - The seed's `ONLY`/`ALSO`/`PREVIOUS` headers shadow ROOT's; worked around in VOCABS.
  - The seed's `(")` `(.")` `(ABORT")` are still compiled into early kernel words; handled in DECOM.
  - Consider giving these seed helpers no heads once the kernel defines its own.
- **Batch host:** `KEY?` is always true in `host_batch`. As a result `SEE` stops after the first token, `DUMP` after one line, and `WORDS` pauses or reads stdin, all in `--batch`. Tests can run `' FALSE IS KEY?` first, but then an error in QUIT waits forever. Consider having batch `KEY?` return false.
- **PATHSET** depends on PDOS truncating at 63 characters (dos.c 47h). If that limit is raised, change the `64 <` guard in `prepend.path`.
