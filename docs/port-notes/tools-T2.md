# TOOLS group T2 port notes (developer tools)

Files: DIS8086 DISASSEM FDIS86 FDISASEM FASSEM CODEBUG CODEHIGH MONITOR PROFILE LOCALS OBJECT UNREF UNLINK FORWARD SETJMP OVERLAY EVAL NEWCOM COMMAND AUTOLOAD AUTOFOR CONSTANT SVALUES SALLOC PICTURE.

All 25 files were converted from CRLF to LF. The `^Z^Z` end markers in COMMAND, FORWARD and FDISASEM are kept. OBJECT.SEQ's CP437 box characters (in the demo) are preserved byte for byte. The one CP437 byte in CODEBUG.SEQ was in an 8086-specific comment that was removed with its code.

## Testing

Every file FLOADs cleanly on its own on top of `F-PC.IMG`. A combined load (FDIS86, CODEBUG, CODEHIGH, PROFILE, LOCALS, SETJMP, EVAL, OVERLAY, UNLINK, FORWARD, PICTURE) also works. The test drivers are in the scratch area (`t2/T*.SEQ`), not in the repo. Each file was exercised as described under its heading below. Interactive tools (CODEBUG, COMMAND, MONITOR) were driven through `--batch` with keys piped on stdin.

## The abstract-machine disassembler (DISASSEM, DIS8086, FDIS86, FDISASEM)

**DISASSEM.SEQ** is rewritten as a disassembler for abstract-machine CODE words. It decodes the op stream (vm/amops.def: `op | a<<8 | b<<16 | c<<24`, plus an immediate cell for LI, ADDI, the loads and stores, and the branches) and prints it in AMASM syntax, ready to reassemble:
- Branch targets become local labels (`n L:`).
- An `LI,` of a named word's code field prints as `' NAME`.
- Large immediates print in `$` hex.
- A cell that is not a valid op prints as `$xxxxxxxx ,  \ not an op`.
- A CODEHIGH `>H ... H>` group is shown as `>H words H>`, and the listing carries on after it.

**Where a listing ends.** It uses the JIT's rule: stop after a `NEXT,` or `BR,` that is past every branch target seen so far. It never goes past the next word's code field (found by scanning every vocabulary) or HERE.

**Words:**
- `SEE` is hooked through `(SEE)`. CODE words show `CODE name ... END-CODE`. A word made by a `;CODE` defining word shows the defining word's `;CODE` part. The `;CODE` defining word itself is decompiled by DECOM with UNNEST temporarily in place of its `(DOES>)`, then its `;CODE` part is listed. Everything else goes to DECOM as before. Before this, DECOM listed CODE words as "DOES> word (clause not found)", and ;CODE children as garbage DOES> clauses.
- `DIS ( | name -- )` gives the same listing with addresses and op cells.
- `ADIS ( addr -- )` lists ops from any address.
- The old names are kept: `DISASSEM ( addr -- )`, `DIS: ( | name -- )` and `UN: ( | name -- )`.
- The internals are in the `DISASSEMBLER` vocabulary, which CODEBUG reuses.
- The 8086 relocation words (RELOC, HOMESEG, (T@)…), the CODE words 2/S, 2*S and SEXT, and the private DUMP are gone.

**How kinds of word are told apart, in Forth** (UTILS.SEQ's DOES? TODO-PORT):
- **CODE word:** its token is ≥ FIRST-DOES-CT, and no `(DOES>) ct` pair for it exists in list space.
- **;CODE child:** the pair exists, and the cell after it is XHERE or the start of a colon definition's list. (A DOES> clause continues in the same list; nothing follows a ;CODE.) The ops are at the defining word's CFA + 8.
- This is a heuristic. It is exact for ordinary code. A headerless colon word that follows a ;CODE definition in list space would make the ;CODE look like a DOES>.

**DIS8086.SEQ** is now a thin wrapper. It loads DISASSEM.SEQ and keeps these names:
- the `DIS8086` vocabulary (now empty)
- `SEEN ( cfa -- )`
- `DM ( a -- a' )`: a display-memory loop that stops on ESC, with `DM-LINE`
- `#INT2@`: returns `0 0`
- `IDIS`: aborts "No interrupt vectors on the VM"

**Changed: `DIS` was `( a -- )` in DIS8086.** It is `( | name -- )` everywhere now; `ADIS` is the address form. DIS8086's private redefinition of `SEE` is dropped, because `(SEE)` is hooked.

**FDIS86.SEQ / FDISASEM.SEQ** were the 8087 opcode extensions. They are now documented stubs that only `NEEDS DIS8086.SEQ` / `NEEDS DISASSEM.SEQ`. Floating point is VM built-ins: there is no 8087 code to disassemble.

**FASSEM.SEQ** was the 8087 assembler. It is now a documented stub that points to SRC/AMASM.SEQ and the float built-ins, and defines nothing. FFLOAT.SEQ (another group) already emulates the few FASSEM words it used (FPCW> >FPCW FPSW> INITFP {.} (TAG)).

## CODEBUG.SEQ: abstract-machine code debugger

The INT 1 / INT 3 machinery is replaced by **an AM simulator in Forth**. `AM-OP`/`AM-STEP` follow `am_run` in vm/am.c: SET ignores UP, the shift clamps match, DIVMOD is floored (`FM/MOD`) with the remainder written before the quotient, and CMOVE is ascending bytes. The display uses DISASSEM's `.INSN`.

**Breaking and tracing:**
- `BREAKAT name` (alias `XX`) and `ABREAKAT ( cfa -- )` save the word's code token and `!CT` it to a DOES> token (`BREAK-TOKEN`).
- When the word next runs, its clause restores the token (one shot, as before) and steps the word with the caller's IP. It then returns to whatever IP the word left.
- `TRACE name` is `' DUP ABREAKAT EXECUTE`, as in the original.
- `UNBREAK` and `.BREAK` work as before, and UNBREAK is in BYEFUNC.

**Stacks.**
- The data stack is copied into a private 256-cell stack and copied back at the word's `NEXT,`.
- The return stack is a private stack that starts empty. Whatever the word leaves there is pushed onto the real return stack under the resume IP, so a CODEHIGH `>H` works.
- A word that pops more than it pushed is reported as not followable.
- Pushing beyond either private stack aborts.

**Keys:**
- SPACE / Enter: step.
- ESC / D: run to the end.
- G: go till a hex address.
- R: `QUERY INTERPRET`, with `=R0`…`=R7 =SP =RP =IP =W` in FORTH.
- A: ABORT.
- F1 / `?`: help.

**Display.** Plain lines: R0–R7, SP RP IP W UP, the top 8 private stack items, and the next op with its address. This works in `--batch`. The boxed screen layout, the up/down highlight and the 8087 status (FLT_DEBUG, `S` significant digits) are dropped.

**Tested:** stepping a counting loop with labels; R; G; ESC; BREAKAT on a word that reads an in-line literal through IP (the changed IP is honoured); TRACE and BREAKAT on a word with `>H ... H>`.

## CODEHIGH.SEQ

`>H` assembles `IP RPUSH,  IP <XHERE> LI,  NEXT,` and compiles the high-level words that follow into list space.

`H>` ends the list with a nameless code field, `HERE X,` and then `HERE CELL+ NEW-AMCODE ,`. That field has its own ;CODE-style code token, whose ops start with `IP RPOP,`, and assembly continues after it.

**Limits (documented in the file):**
- W is that field (not the CODE word's CFA) after H>.
- Labels must not be branched to across a group, because each part is a separate JIT op stream.

Tested with one group, two groups, and from a colon word.

## PROFILE.SEQ

There is no timer interrupt and no NEXT to patch, so the profiler uses the debugger trace hook (DBG-ON/DBG-LO/DBG-HI/'DEBUG):
- `'DEBUG` is `PROF-TICK`, an AM CODE word. It adds 1 to a count cell for the IP (one cell per cell of list space, LIMIT..XHERE at PROF_START) and re-arms DBG-ON.
- So the counts are **exact counts of tokens executed** per colon definition, not clock samples.
- Time in primitives and CODE words still goes to the calling colon definition, as before.
- A DOES> clause counts towards the defining word.
- Words compiled after PROF_START are not counted.

The report groups counts with `IP>CFA` (and `LIST>CFA` for DOES> clauses), in dictionary order, with percentages and a total:
- `saveprof` writes it to PROF.TXT (CRLF lines).
- New: `.PROF` shows it on screen, and `PROF-MIN` sets the smallest count shown.

`PROF`, `PROF_START`, `PROF_FINISH`, `PROF_ON`/`PROF_OFF`, `SAVE&SET_VECS`/`REST_VECS` (which save and restore the hook) and `UNPROF_EXITFUNC` keep their names.

The internals ESSEG, PRNEST, PREXIT, PRPATCH, MYTIMER and TIMERSAVE are gone. **Don't run DEBUG while profiling: they share the hook.**

Tested: 1000 calls of a 100-iteration loop give exactly 306000 tokens, and a DOES> clause is counted. PROF.TXT was checked.

## Other files

### LOCALS.SEQ
- The CODE words became colon words.
- `LOCALS` slips `' 0LOCALS >BODY @ CELL+` (the IP of `(0LOCALS)`) under its own return address, so the caller's `;` runs `(0LOCALS)` and then returns.
- Locals are cells. The count cell holds bytes (n+1 cells).
- The local stack is 512 cells, and running out of it aborts (new).
- Tested: LOCALSWAP, the sample findXinString, and nesting.

### OBJECT.SEQ
- Offsets are now in cells. The method's code is a nameless colon definition: `CT-NEST ,  XALIGN XHERE ,` (it was `,JUMP` to NEST and a list paragraph).
- ACTION uses the high-level version: the AM has no EXECUTE.
- The demo after `\s` has its offsets updated (16/20/24/28). The "Type 140 load" message is reworded.
- Tested: the whole demo (vehicle, automobile, racer and slow-poke), `.sons`, `.methods` and `.one`.

### UNREF.SEQ
- REFED is a POINTER of `LIMIT 4 /` bytes, indexed by CFA/4. Counts saturate at 255 (they used to wrap).
- `C+!L` (CODE) became `REF+!`.
- Tested: .unref and .usage.

### UNLINK.SEQ
- `>IS` replaces the user-defer special case. A colon definition is recognised by `@ CT-NEST =`. The chain link is `>BODY @ @`.
- Tested: unlinking from the head, the middle and the tail, and from KEY? (a user defer).

### FORWARD.SEQ
- `ALIGN` comes before `HERE >R :`, because HEADER aligns.
- Tested.

### SETJMP.SEQ
- The buffer is 3 cells; frames are one cell.
- Tested: longjmp from 3 levels down, including from inside a colon word.

### OVERLAY.SEQ
There are no overlays on the VM (flat memory, no #OVSEGS/#OVBYTES). The names are kept so that programs still run:
- STARTOV and ENDOV just bracket normal definitions, and no file is written.
- ENTRYPOINT and ENTRYNAME make words that execute directly and set ?OVFLAG.
- CLEAROV does nothing.

Tested with the file's own example.

### EVAL.SEQ
- MACRO and MACRO: lay an in-line counted string after `(")` in list space, with a new helper `x$,`. They used to point to a code-space string.
- Tested: eval, both macro forms, and use inside a definition.

### COMMAND.SEQ
- The nested area is laid out as a 400-byte return stack, then a TIB sized from COLS (QUERY reads COLS characters), then a marker cell and a saved-SP cell at the new SP0, then the new data stack.
- Tested: a nested session returns with the program's stack intact.
- After an error inside COMMAND, F-PC's error recovery took over in batch mode. RET was not fully verified there.

### NEWCOM.SEQ
- A .COM file can't be made from AM code.
- DOCOM (which saves a range of memory to a file) is kept and tested. The 8086 example moved below `\S`, so it is no longer run.

### AUTOFOR.SEQ
- The resolver now hooks the deferred `"HEADER` and then calls the kernel's `<"HEADER>`. It used to replace HEADER with its own copy of the segmented header builder.
- DEFERs are detected by `@ CT-DODEFER =`.
- `ALIGN` comes before `HERE X,`.
- AUTOFOFF restores `(])` and `<"HEADER>`.
- Tested: a forward reference is created and then resolved.

### AUTOLOAD.SEQ
- The misspelled `coment;` meant the whole file was a comment. That is fixed.
- Note: the image's HDEFAULT already loads F-PC.CFG, so with this file loaded it is read twice.

### SALLOC.SEQ
- The far-memory words became CMOVE, COUNT and PLACE on the POINTER's address.
- Tested.

### CONSTANT, SVALUES, PICTURE
- Unchanged apart from the header line.
- Tested.

### MONITOR.SEQ
- The INT 10h CODE words call `BIOS-VIDEO` (SETMODE, scrollup/scrolldown via `(scroll)`, rdchar, chars). `location` uses AH=3. `charemit` is `1 chars` plus a cursor step.
- `crtwidth` is COLS and `bottom` is ROWS-1 (it was 80/24).
- Table entries are 2 cells.
- QUIT's QUERY is found by scanning the list of QUIT's current action and replacing it with IQUERY (`patch-quit`). It used to patch list offset 22.
- The redefined DUMP shows 8-digit addresses.
- Tested: location, charemit, chars and rdchar round trips, and the patched QUIT reading lines through ACCEPT/acceptline.

## Built-ins needed

None are required. One would make the disassembler's tests exact:
- `CT-KIND ( ct -- n )`: the handler kind of a code token (built-in, DOES>, AM CODE with ops after the CFA, or AM ;CODE), and
- `CT-DATA ( ct -- addr )`: its data (the DOES> list or the ;CODE ops address).

The heuristics above (DOES-PAIR, ;CODE-PAIR?, CFA+8) would then become exact. DOES? in UTILS.SEQ has the same TODO-PORT.

## TODO-PORT
- None left as `\ TODO-PORT:` comments in these files.
- Limitations are listed above: the disassembler heuristics; CODEBUG not following a CODE word that pops the caller's return stack; PROFILE counting tokens rather than time; and RET after an error in COMMAND, which is not fully verified.
