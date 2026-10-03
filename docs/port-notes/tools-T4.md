# TOOLS group T4 port notes (files, I/O, misc)

Files: NEW-WFL BLOCK BLKTOSEQ SEQTOBLK SBOOT DOSIO RS232IB XMS EXPANDED EMMEXMPL LASERJET (the TOOLS copy).

All eleven files were converted from CRLF to LF. CP437 bytes are preserved: the box drawing in NEW-WFL, the German text in XMS and the ESC bytes in the LASERJET strings. The only CP437 text dropped is the Cyrillic comments on 8086 instruction lines inside the NEW-WFL CODE words that were rewritten.

## Testing

Each file was FLOADed on `F-PC.IMG`, and its words were run from driver files in the scratch area. The scratch area is `t4/`, and the UI runs used `tools/ptytest.py` with pyte installed into the scratch area.

- **BLOCK.** On a host file: BUFFER, UPDATE, FLUSH, EMPTY-BUFFERS and BLOCK, including LRU eviction with 3 buffers.
  - LIST, LOAD and THRU work, including `\` and `( )` comments, and a colon definition loaded from a block.
  - Data survives a close and reopen of the file.
  - A block past the end of the file gives "Error reading block".
- **SEQTOBLK, then BLOCK, then BLKTOSEQ round trip.**
  - A 25-line .SEQ file becomes a 3-block .SCR file.
  - `1 2 THRU` compiles and runs it.
  - Converting it back gives the original lines. The only extra lines are the block structure that SEQTOBLK adds: the `\` line 0 and the blank line 15 of each block.
  - BLKTOSEQ on a `.BLK` file pairs each block with its shadow block as `comment:` blocks.
- **NEW-WFL.**
  - The low-level words, tested in batch: N>TYPE, Z>COUNT, COUNT>Z, +FILE/@FILE, +CATALOG/@CATALOG, @NAME, SYS-DEL (file and directory), SYS-REN, SET-PATH and INIT-PATH-BUF.
  - The whole selector, in a pty at 80x25:
    - The listing, arrows and scrolling, and PgDn.
    - Letter search.
    - A resize to 120x40 (5 columns of 34 names) and to 70x20.
    - Enter returns the full path.
  - Not exercised in the pty: delete, rename, path and mask edit. The routines they call were tested in batch.
- **DOSIO.** Commands piped on stdin are interpreted, and the end of input leaves F-PC.
- **SBOOT.** It writes MYBOOT.IMG. `fpc -i MYBOOT.IMG` runs MYDEFAULT: it prints its message, waits for a key, and leaves.
- **XMS.** XMINIT, XMAVAIL and XMRESULT work. `=XMOVLEN` and `=XMOVSRC` lay out the parameter block byte-for-byte as before. `XMSVERSION` aborts with "XMM not installed".
- **EMMEXMPL.** EMM-SAMPLE stops with "No Expanded memory available!".
- **RS232IB.** BAUD, COM1:, COM-IN, COM-OUT, COM-STAT, COM-CNT, `.BUF` and ?REST_COM1: all run without hardware.
- **EXPANDED and LASERJET.** They load. LASERJET selects the LaserJet strings.

## Per-file changes

### BLOCK.SEQ
- The CODE words are now high-level Forth: BUF#>BUFADDR, >REC#S, >REC#UPDT, >REC#FIL and CHKFIL.
- The arrays hold cells (BUFLEN is `#buffers cells`).
- BUBBLEUP and ?GOTREC step in cells, using SCANW from SRC/SCAN.SEQ, which now scans cells.
- `.BLOCKS` steps in cells and prints 7 wide.
- `PREFIX` is removed.
- READ_BLOCK marks the buffer empty before it aborts, so a half-read block isn't kept.
- **New: SCR, LIST, LOAD and THRU** for blocks.
  - LOAD interprets one 64-character line at a time, with `'TIB`, `#TIB` and `>IN` pointed at the buffer and BLK set. It saves and restores all of these.
  - **These hide F-PC's line-based LIST and LOAD** once BLOCK.SEQ is loaded.
- As before, BLOCKHANDLE defaults to SEQHANDLE. To use your own handle, define B/BUF, #BUFFERS and BLOCKHANDLE first; the header comment has an example.
- **Open block files with READ-WRITE.** The default HOPEN mode is read-only, so a FLUSH to a file opened without it fails with "Error writing block".

### BLKTOSEQ.SEQ
No code change. It works as-is on host files.

### SEQTOBLK.SEQ
- **New helper `-crlf`.** It strips the trailing CR and/or LF from a line. The original did `2-`, which drops a real character on LF-only files.
- CONV prompts for a file name with QUERY and opens it with `$HOPEN` on SEQHANDLE, as the original did. Run it from the prompt, not from inside an FLOADed file.

### SBOOT.SEQ
- `FSAVE MYBOOT.EXE` becomes `FSAVE MYBOOT`, which writes MYBOOT.IMG; run it with `fpc -i MYBOOT.IMG`.
- A comment explains that SETSEG and VMODE.SET are host work now. The INITSTUFF, BOOT and DEFAULT chain is unchanged.

### DOSIO.SEQ
- No code change; a note was added.
- BDOS function 7 reads the host's stdin, and end of input leaves F-PC. Function 0 ends the program.

### RS232IB.SEQ
These are documented stubs that keep the original logic.
- The CODE words are now Forth that talks to the 8250 and the 8259 through PC@ and PC!: COM-INIT, RS232_INTOFF, COM-OUT, COM-IN and COM-STAT.
- In the VM, PC@ reads $FF and PC! does nothing. So COM-OUT discards characters, and COM-IN and COM-STAT return $FF after the poll.
- RS232_BASE points to a table of the standard port addresses ($3F8, $2F8), because there is no BIOS data area.
- The interrupt handler COM_INT is kept as a Forth word, but nothing calls it. Because of that, COM-CNT stays 0, and TERMINAL and TOSERIAL never receive anything.
- The vector words are no-ops: SET_COMn:, REST_COMn: and SAVE_COMn:. SAVE_COMn: records a dummy vector so that ?REST_COMn: still runs RS232_INTOFF at BYE.
- INT3 is a no-op.
- **SER_TYPEL now takes ( a n )** and TOSERIAL installs it in TYPE; TYPEL is gone.

### XMS.SEQ
- The no-XMM behaviour of the original is kept:
  - CallXM is now `DROP XMAbort`, which aborts with "XMM not installed".
  - XMInit sets XMAvail false and XMResult 0.
- The `(xmAbort)` label and the 80286 code are gone.
- `=XMOVLEN`, `=XMOVSRC` and `=XMOVDEST` store 16-bit halves with `W!`, in the same byte layout as the original `sp@ … cmove`.
- In XMMove and XMOV, `?CS:` becomes 0.
- **Bug fix:** UMBAlloc did `XMResult @` (XMResult is a VALUE).

### EXPANDED.SEQ
Comment only. The EMM words are the kernel stubs in SRC/EXPAND.SEQ: EMM-PRESENT? is false.

### EMMEXMPL.SEQ
- `LFILLW` becomes a `w!` loop over `?emm: $4000`.
- The screen copies use `video-buf` with `cols rows * 2*` bytes, and CMOVE in place of CMOVEL.
- It compiles, and at run time it stops at step 1.

### LASERJET.SEQ
Header comment only. It loads and runs as-is (TELETYPE and `=:` exist).

### NEW-WFL.SEQ
**CODE words**, now high-level Forth:

| Word | Now |
|---|---|
| DRAW-LINE | Writes into VIDEO-BUF at #OUT/#LINE, clipped |
| ?4+ | Forth |
| SCR-UP, SCR-DOWN | BIOS-VIDEO 06h/07h over the name area |
| CURR-PATH | PDOS |
| SET-PATH | HDOS1 3Bh |
| CHANGE-DRV | BDOS 0Eh |
| CURR-DRV | BDOS 19h |
| COUNT>Z, Z>COUNT | Forth |
| N>TYPE | Builds the 14-character display string in N>TBUF |
| FIRST-FIND, NEXT-FIND | FIND-FIRST and FIND-NEXT. NEXT-FIND leaves an error code under true, as before |
| SYS-DEL | HDOS1 41h/3Ah |
| SYS-REN | `<HRENAME>` with the ASCIIZ addresses minus 1 |
| DROP-ATR | HDOS1 4301h; the VM ignores it |
| N>AT | Forth |
| +FILE, @FILE, +CATALOG, @CATALOG | Use DIR-SEG plus an offset |
| @LONG | Forth |

- NEW-WFL's own DTA@ and DTA! (segment and offset) are removed, so the kernel's flat-address versions are used.
- **Geometry.** These VALUEs are set by NWFL-GEOMETRY from COLS and ROWS:

  | Value | Was |
  |---|---|
  | `#FCOLS` = (COLS-29)/17, at least 3 | 3 |
  | `#FROWS` = ROWS-6 | 19 |
  | `#FPAGE` | 57 |
  | `PANEL-X` | 52 |
  | `RIGHT-X` | 78 |
  | `LIST-BOTTOM`, `SEP-Y`, `STAT-Y`, `BOT-Y` | rows 21–24 |

  - Every 19/56/57/52/23 in the paging and marker words uses these values.
  - DRAW-SCREEN and STRING-SCREEN draw any number of columns.
  - Help lines that don't fit are skipped (`?HELP`), and extra rows get blank panel lines.
  - MARKER-UP and MARKER-DOWN redraw the scrolled line with REDRAW-ROW.
- **Resize.** KEY-SELECT calls NWFL-RESIZE when the size has changed (`?RESIZED`) or on K-RESIZE. NWFL-RESIZE lays the window out again and keeps the current name. `.TIME-KEY` also stops waiting on a size change.
- **Pop-up dialogs keep their 80x25 positions.** They need at least 76x17.
- **Buffers.** Path buffers are PBUF-LONG (256) bytes; they were 70. PATH-LONG is 56 (was 38). MAXFILES is 2000 (was 500). New `ROOM?` stops the name list from overwriting itself; the original didn't check.
- **Fetches.** `@ $2E2E =` and the `':' '\' join` compares now use `W@`. `init-path-buf` uses `W!`. The attribs save area is now 5 cells. `funcmouse` indexes the table by cells.
- `.TIME-KEY` calls `REFRESH 10 MS` while it waits, so the screen is shown and the loop doesn't spin.
- **Long names.** `.AT-PATH` and `AT-NAME` show the tail of a path that is too long (TYPE-TAIL).
- **Bug fix:** SCOOPS-DIR no longer reads the DTA when FIND-FIRST finds nothing.

## Built-ins needed

None.

## TODO-PORT and problems found (outside these files)

- **Kernel bug: heap corruption after FLOADing any file over 16 KB.** This needs fixing outside T4.
  - INBSEG, the read buffer, is a 16400-byte heap block, allocated at IBLEN 16384.
  - BUFSET.SEQ's BUFSIZE-INIT then sets IBLEN and IBFULL to IBLIMIT = 32000 without resizing INBSEG.
  - FILLBUFF then reads up to 32000 bytes into it and overwrites the next heap block's header. Every later ALLOCATE fails (ior -59), and POINTERs return 0.
  - To reproduce: FLOAD a 20 KB file, then `100 allocate . .`.
  - It hits NEW-WFL directly: the file is 50 KB, so after it loads, DIR-SEG can't be allocated and GETFILE says "Insufficient memory".
  - Fix (SRC/BUFSET.SEQ or SEQREAD.SEQ, not mine): resize INBSEG when IBLEN/IBFULL change (`sizeof!>` plus RESIZE), or allocate IBLIMIT up front.
  - Tests here used `16384 =: ibfull` before FLOAD as a workaround.
- **VM: INT 21h AH=47h** (PDOS, CURR-PATH) cuts the current directory at 63 characters, so NEW-WFL shows and returns a wrong path in deeper directories.
- **VM: INT 21h AX=4301h** (set attributes) is ignored, so DROP-ATR can't clear read-only.
- **Kernel: KEY? doesn't report a pending K-RESIZE.** BIOSKEY? does, but KEY? (MOUSEKEY? → (KEY?)) doesn't, so a loop that polls KEY? doesn't see a resize until a real key arrives. NEW-WFL works around it by comparing COLS and ROWS.
- **RS232IB.** A real serial line would need host built-ins. For example, COM-OPEN ( a n -- f ) to open a host tty or file path, plus non-blocking COM-OUT, COM-IN and COM-STAT on it. This is not done.
- **XMS, EXPANDED and EMMEXMPL** are permanent "not available" stubs.
