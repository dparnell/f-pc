# F-PC 3.6 OS / hardware interface inventory

Scope: the kernel (`SRC/META86.SEQ` → `KERNEL1.SEQ`, `VIDEO.SEQ`, `KERNEL2.SEQ`, `VIDEO2.SEQ`, `KERNEL3.SEQ`, `EXPAND.SEQ`, `EMMEXEC.SEQ`, `POINTER.SEQ`, `EQUCOLON.SEQ`, `SAVEREST.SEQ`, `HANDLES.SEQ`, `SEQREAD.SEQ`, `FPATH.SEQ`, `DEFAULT.SEQ`, `HCRITICA.SEQ`, `KERNEL4.SEQ`; META86.SEQ:674-689) plus every file FLOADed by `SRC/F-PC.SEQ`. `TOOLS/` is summarised separately in section 7.

Line numbers come from the files after `tr -d '\r\032'`, which deletes characters but not lines, so they match the originals. Several sources contain CP437 bytes (KERNEL1, SEDITOR, MENUS, WFL, NEXPECT, MACROS and others). Use `grep -a`, because plain `grep` treats those files as binary and skips them silently.

Notation: `nn` decimal, `$nn`/`nnh` hex. Numbers passed to `BDOS`/`OS2` are decimal unless written with `$`.

---

## 0. Summary

* **The OS interface is narrow and almost entirely in CODE words.** About 60 CODE words or labels contain `INT`, `IN` or `OUT`. Everything else in the system reaches the OS through about 15 Forth-visible seams: `BDOS`/`BDOS2`/`OS2`, `HDOS1`/`HDOS3`/`HDOS4`, the HCB words, `BIOSKEY`/`BIOSKEY?`, `VIDEO-TYPEL`, the `AT`/`AT?`/`DARK`/`-LINE` deferreds, `SET-CURSOR`/`GET-CURSOR`, `ALLOC`/`SETBLOCK`/`MAXBLOCK`, and `$SYS`.
* **Forth code bypasses the seams in only three ways:**
  1. It reads the BIOS data area with `@L`/`C@L` (shift state 40:17, keyboard buffer head/tail 40:1A/1C, cursor shape 40:60, and the INT 33h vector at 0:CC).
  2. It moves blocks to and from video RAM with `CMOVEL`/`C!L`, using the `VIDEO-SEG` variable.
  3. It reads the PSP at CS:002C (environment segment) and CS:0080 (command tail).

  All three keep working unchanged if the VM keeps a segmented 1 MB address space and synthesises a BDA, a PSP and an environment block, and if the virtual text buffer lives at B800:0000 (or B000:0000 in mono mode).
* **`KEY` returns one cell with value 0..255.** Plain ASCII is 1..127 and 129..255. An extended key becomes `128 OR (scan AND 127)`, so Up is 200 and F1 is 187. Ctrl-@ returns 0 (KERNEL2.SEQ:395-404).
* **Screen output is a hybrid.** `TYPE` (and therefore `SPACE`, `SPACES` and `."`) writes directly into video RAM through `VIDEO-TYPEL`, which never wraps or scrolls. `EMIT`, `CR` and `BEEP` go through a DOS `write(1, …)`, so scrolling and cursor movement after `CR` are done by the DOS CON driver and the BIOS TTY.

---

## 1. Touchpoint table

Columns:
* **Mechanism**: INT nn, port I/O, BDA (BIOS data area), IVT, PSP, VRAM (video memory), or "via X" (goes through wrapper X).
* **Port mapping**:
  * `dos(AH)`: handled by the C INT 21h function emulator (section 5.2).
  * `vscr`: virtual screen API.
  * `kbd`: keyboard event queue.
  * `vm-mem`: synthesised memory content.
  * `stub`, `no-op` or `drop`.

Dead code (inside `comment:` … `comment;` or after `\S`) is marked **DEAD**.

### 1.1 INT 21h (DOS)

| file:line | word | mechanism | function | purpose | port mapping |
|---|---|---|---|---|---|
| KERNEL2.SEQ:331-336 | `<BDOS>` (→ `DEFER BDOS` :338) | INT 21h, AH=fun, DX=n | generic | simple DOS call, returns AL | `dos(AH)` dispatcher primitive |
| KERNEL2.SEQ:341-346 | `BDOS2` / `OS2` (:348) | INT 21h, CX DX AX | generic | 3-register DOS call | `dos(AH)` dispatcher primitive |
| KERNEL2.SEQ:501 | `MEMCHK` | via BDOS | 00h terminate | abort "Insufficient Memory" | `host_exit(1)` |
| KERNEL2.SEQ:504-515 | `DOS_DEALLOC` (→ `DEALLOC` :551) | INT 21h | 49h free memory | free a DOS block | VM arena free |
| KERNEL2.SEQ:517-530 | `DOS_ALLOC` (→ `ALLOC` :552, `DOS_MAXBLOCK` :547) | INT 21h | 48h allocate memory | `-1 DOS_ALLOC` gives the largest free block | VM arena alloc / largest |
| KERNEL2.SEQ:532-545 | `DOS_SETBLOCK` (→ `SETBLOCK` :553) | INT 21h | 4Ah resize block | grow or shrink the program block; every `POINTER` uses it | VM arena resize of program block |
| KERNEL2.SEQ:557-559 | `DOSVER` | via BDOS | 30h get DOS version | needs ≥2 (SETYSEG :577), ≥3 for `ME@` | return 5.0 |
| KERNEL2.SEQ:579 | `SETYSEG` | via BDOS | 00h terminate | "Must have DOS 2.x" | `host_exit` |
| KERNEL1.SEQ:925-936 | `PDOS` | INT 21h | 47h get current directory | drive → `\path` (no leading `\`) | `host_getcwd` (DOS-style) |
| KERNEL4.SEQ:60-65 | `SELECT` (A: … F:) | via BDOS 14 | 0Eh select disk | change default drive | no-op (single virtual drive) |
| KERNEL4.SEQ:147-164 | `RESTORE_VECTORS` | INT 21h | 25h set vector (1Bh, 00h), 33h/01 set BREAK flag | restore on exit and before EXEC | no-op |
| KERNEL4.SEQ:178-183 | `BYE` | via BDOS | 00h terminate | exit to DOS | `host_exit(0)` |
| KERNEL4.SEQ:209-234 | `SETBRK` (label; `SET_VECTORS` :255) | INT 21h | 25h for 1Bh→`BIOSBK`, 23h→`DOSBK`, 00h→`DIV0BK`; 33h/01 DL=0 | install break and divide-by-zero handlers, disable DOS ^C checking | no-op; VM break flag (§4.6) |
| KERNEL4.SEQ:236-253 | `SAVEVECTORS` (label) | INT 21h | 35h get vector (1Bh, 00h); 33h/00 get BREAK | save originals | no-op |
| KERNEL4.SEQ:335-337 | `CORIG` cold entry | calls the two above | – | installs vectors at start-up | no-op |
| HCRITICA.SEQ:61-67 | `CRITINT` (label) | INT 24h handler | – | critical error → "fail", bumps `HADCRITICAL` | drop |
| HCRITICA.SEQ:69-75 | `SETCRITICAL` | INT 21h | 25h set INT 24h | called in COLD (KERNEL4.SEQ:118) and after EXEC (EXEC.SEQ:87) | no-op |
| HCRITICA.SEQ:86-117 | `savecritical`/`resetcritical` | INT 21h 35h/25h | – | **DEAD** (inside `comment:`) | – |
| HANDLES.SEQ:99-112 | `HDOS1` | INT 21h, CX DX AX | generic, returns (ax cf) | used by HCREATE/HOPEN | `dos(AH)` |
| HANDLES.SEQ:114-132 | `HDOS3` | INT 21h, BX CX DX DS AX | generic, DS override | far read/write | `dos(AH)` |
| HANDLES.SEQ:134-148 | `HDOS4` | INT 21h, BX CX DX AX | generic | close/delete/read/write | `dos(AH)` |
| HANDLES.SEQ:150-159 | `MOVEPOINTER` | INT 21h | 4200h lseek SEEK_SET | seek | `host_seek` |
| HANDLES.SEQ:161-175 | `ENDFILE` | INT 21h | 4202h lseek SEEK_END | file size / seek to EOF | `host_seek` |
| HANDLES.SEQ:187-205 | `<HRENAME>` / `HRENAME` :208 | INT 21h | 56h rename (DS:DX→ES:DI) | rename | `host_rename` |
| HANDLES.SEQ:215-223 | `HCREATE` | via HDOS1 $3C02 | 3Ch create (CX=attrib word at HCB+66) | create/truncate | `host_create` |
| HANDLES.SEQ:251-261 | `HOPEN` | via HDOS1 $3D00+mode | 3Dh open, AL=R/W-MODE (0 R, 1 W, 2 RW) | open | `host_open` |
| HANDLES.SEQ:263-269 | `HCLOSE` | via HDOS4 $3E00 | 3Eh close | close; sets HCB handle to -1 | `host_close` |
| HANDLES.SEQ:271-273 | `HDELETE` | via HDOS4 $4100 | 41h unlink | delete by name | `host_delete` |
| HANDLES.SEQ:276-278 | `EXHREAD` | via HDOS3 $3F00 | 3Fh read into seg:off | far read | `host_read` |
| HANDLES.SEQ:281-283 | `EXHWRITE` | via HDOS3 $4000 | 40h write from seg:off | far write; also the console path | `host_write` |
| HANDLES.SEQ:285-291 | `HWRITE` / `HREAD` | via HDOS4 $4000/$3F00 | 40h / 3Fh | near write/read | `host_write`/`host_read` |
| HANDLES.SEQ:306-315 | `FIND-FIRST` | INT 21h | 4Eh find first (CX=attr) | directory scan into DTA | `host_find_first` → fill DTA |
| HANDLES.SEQ:317-324 | `FIND-NEXT` | INT 21h | 4Fh find next | | `host_find_next` |
| HANDLES.SEQ:331-338 | `DTA@` | INT 21h | 2Fh get DTA | | VM keeps DTA pointer |
| HANDLES.SEQ:340-347 | `DTA!` / `SET-DTA` :349 | INT 21h | 1Ah set DTA | | VM keeps DTA pointer |
| SEQREAD.SEQ:90-101 | `CURPOINTER` | INT 21h | 4201h lseek SEEK_CUR | current position | `host_seek` |
| SEQREAD.SEQ:49-80 | `CONSOLEL` `(CONSOLE)` `PRINTL` `(PRINT)` `PRNTYPEL` `CONTYPEL` `(TYPEL)` | via EXHWRITE to `CONHNDL` (handle 1) / `PRNHNDL` (handle 4) | 40h | **console and printer output path** | handle 1 → `vscr_tty_write`; handle 4 → printer sink |
| SEQREAD.SEQ:147-162 | `FILLBUFF` | via EXHREAD | 3Fh | source line reader; turns ^Z in the last 6 bytes into blanks | `host_read` (keep the ^Z logic) |
| SEQREAD.SEQ:240-251 | `SEQINIT` | – | – | builds CON./PRN. HCBs with fixed handles 1 and 4 | keep |
| POINTER.SEQ:29-41 | `POINTER` DOES> | via SETBLOCK | 4Ah | lazily grows the program block, hands out paragraphs | VM arena |
| POINTER.SEQ:59 | `?VALID_POINTER` | via BDOS | 00h | fatal exit | `host_exit` |
| POINTER.SEQ:62-79 | `%UNPOINTER` | via SETBLOCK | 4Ah | shrink | VM arena |
| EMMEXEC.SEQ:62-68 | `SMALFPC` (label) | INT 21h | 4Ah | shrink to ~8 KB before EXEC | drop |
| EMMEXEC.SEQ:75-80 | `EMM>FPC` | INT 21h | 4Ah | re-grow after EXEC | drop |
| EMMEXEC.SEQ:120-156 | `DSK>FPC` | INT 21h | 4Ah, 4200h, 3Fh, 3Eh, 41h | reload the image from the swap file `FPCIMAGE.$$$` | drop |
| EMMEXEC.SEQ:158-205 | `<EXTEXEC>` | INT 21h | **4B00h EXEC** (`CMDPATH`=COMSPEC, `EXEC.PARAM`) | shell out | `host_system()` |
| EXPAND.SEQ:30-50 | `EMM-PRESENT?` | INT 21h | 3567h get INT 67h vector, compare "EMMXXXX0" | EMS detection | return false |
| ENVIRON.SEQ:60 | `ME@` | via DOSVER | 30h | | – |
| EXEC.SEQ:70-90 | `$SYS` | via `<EXTEXEC>` | 4Bh | the shell primitive used by `SYS` / `` ` `` / `` `` `` (:129-137), `DIR` `DEL` `CHDIR`/`CD` `COPY` `REN` (:176-198), MENUS.SEQ:83, SEDSHELL.SEQ:35 | `host_system(cmd)` |
| EXEC.SEQ:165-172 | `$DIR` | FIND-FIRST/NEXT, SET-DTA | 4Eh/4Fh/1Ah | internal directory listing (DTA +21 attr, +30 name) | `host_find_*` |
| EXEC.SEQ:33-52 | `fpc>disk` | HCREATE/EXHWRITE/HDELETE | 3Ch/40h/41h | swap the image to disk before EXEC | drop |
| TIMER.SEQ:3 | `GETDATE` | via OS2 42 | 2Ah get date → `( Y MD )` | | `host_get_date` |
| TIMER.SEQ:5 | `SETDATE` | via OS2 43 | 2Bh set date | | stub (fail) |
| TIMER.SEQ:8-10 | `<GETTIME>` (→ `DEFER GETTIME`) | via OS2 44 | 2Ch get time → `( HM Sh )` (hundredths) | all timing: `TIME-RESET`/`TIME-ELAPSED` :77-79, `SECONDS`/`TENTHS` (TIMESTUF.SEQ:11-21) | `host_get_time` |
| TIMER.SEQ:12 | `SETTIME` | via OS2 45 | 2Dh set time | | stub (fail) |
| UTILS.SEQ:36 | `DRIVE?` | via BDOS 25 | 19h get current drive | | return 2 (C:) |
| PATHSET.SEQ:10 | `?drive.extract` | via BDOS 25 | 19h | default drive for names without a drive letter | return 2 |
| PATHSET.SEQ:29 | `prepend.path` (→ `PATHSET`, :46) | via PDOS | 47h | prepends `X:\cwd\` to every HOPEN/HCREATE/HRENAME name | `host_getcwd` + DOS↔POSIX path mapping |
| SEDCODE.SEQ:23-31 | `getdiskfree` | INT 33 (=21h) | 36h get free disk space | used in SEDITOR.SEQ:316,334 (enough room to save?) | `statvfs` |
| PRINTING.SEQ:127-134 | `get_file_date&time` | INT 21h | 5700h get file date/time | footer on printouts | `fstat` → DOS packed date/time |
| PRINT.SEQ:17-39 | `pclose` / `$pfile` | HOPEN/HCREATE on PRNHNDL | 3Dh/3Ch on "PRN." | printer ↔ file redirection | PRN → printer sink |
| PRTCTRL.SEQ:51, PRINTING.SEQ:12 | printer escape output | HWRITE PRNHNDL | 40h | | printer sink |
| SAVEEXE.SEQ:102-148 | `write-exe` / `<save-exe>` | HCREATE/HWRITE/EXHWRITE | 3Ch/40h | writes an MZ `.EXE` image | replace with VM image save |
| SEDSHELL.SEQ:17-53 | `%doDOS` (Ctrl-J, Esc-F-D) | SETBLOCK + `$SYS` | 4Ah, 4Bh | shrinks the edit buffer and shells out | `host_system` |
| MENUS.SEQ:78-90 | `do-dos` | `$SYS` | 4Bh | menu "DOS shell" | `host_system` |
| EDITSTUF.SEQ:91 | `tbuf.init` | via MAXBLOCK | 48h | sizes the editor's text buffer from free DOS memory | VM arena largest |

Other HCB call sites need no change because they all go through the seam: SEDITOR.SEQ:257,356-462,1600 (open/create/rename/delete .BAK and .$$$), TOPEDIT.SEQ:22-46,141-144, SEDCOPY.SEQ:65-144, SEDAPND.SEQ:14-24, SEDWHELP.SEQ:34-337, NEWFILE.SEQ:13-21, MACROS.SEQ:75-92, SVSESDAT.SEQ:52-72, FWORDS.SEQ:72-165, FPATH.SEQ:120-141, WFL.SEQ:118-136,305, UTILS.SEQ:386-394, HELLO.SEQ:24.

### 1.2 INT 10h (video BIOS)

| file:line | word | function | purpose | port mapping |
|---|---|---|---|---|
| VIDEO.SEQ:19-39 | `?VMODE` | 0Fh get mode; also reads BDA 40:4A cols, 40:84 rows-1, 40:60 cursor end line | sets `COLS`, `ROWS` (≥25), `CROWS`, `VMODE-VAR` | `vscr_mode()` + synthesised BDA |
| VIDEO2.SEQ:41-49 | `VIDEO-TYPEL` | 02h set cursor (unless `NOSETCUR`) | moves the hardware cursor to the end of typed text | `vscr_set_cursor` |
| KERNEL4.SEQ:466-473 | `SET-CURSOR` | 01h set cursor shape (CX) | | `vscr_cursor_shape` |
| IBMCURSR.SEQ:5-14 | `IBM-AT` (→ `AT`) | 02h set cursor position | | `vscr_set_cursor` |
| IBMCURSR.SEQ:35-44 | `IBM-AT?` (→ `AT?`) | 03h read cursor position | | `vscr_get_cursor` |
| IBMCURSR.SEQ:46-53 | `IBM-DARK` | 0Fh then 00h set mode | clears the screen by re-setting the mode (used by `DARK` when `?DOSIO`) | `vscr_clear(0x07)` |
| IBMCURSR.SEQ:83-99 | `IBM--LINE` (→ `-LINE`) | 03h, 06h scroll up 1 (fill attr 07) | delete the line at the cursor | `vscr_scroll_up(region, 1, 0x07)` |
| IBMCURSR.SEQ:16-33, 116-124 | `IBM-SCR@`, `IBM-SCR!`, alternative `SET-CURSOR`/`GET-CURSOR` | 08h, 09h, 01h | | **DEAD** (commented) |
| BLINKER.SEQ:13-18 | `blink!` (`blinkoff` :20, called at HELLO.SEQ:6 and EXEC.SEQ:89) | 1003h toggle blink/intensity | attr bit 7 means bright background | `vscr_set_blink` |
| BLINKER.SEQ:26-34 | `pal!` | 1000h set palette register | | stub (or renderer palette) |
| BLINKER.SEQ:36-41 | `border!` | 1001h set overscan | | no-op |
| BLINKER.SEQ:43-49 | `border@` | 1008h read overscan | | return 0 |
| BLINKER.SEQ:56-68 | border save/restore | | | **DEAD** (after `\S` at :51) |
| QVIDEO.SEQ:33-59 | `COLOR-EMIT` | 0Eh TTY, 09h write char/attr | | **DEAD** (after `\S` at :31) |

### 1.3 INT 16h (keyboard BIOS)

| file:line | word | function | purpose | port mapping |
|---|---|---|---|---|
| KERNEL2.SEQ:353-373 | `BIOSKEY?` | 01h peek (ZF); discards AX=0 (Ctrl-Break) with 00h | default `KEY?` via `(KEY?)` :391-393; raw value goes to `BIOSCHAR` | `kbd_peek()` |
| KERNEL2.SEQ:375-385 | `BIOSKEY` | 00h read; loops on AX=0 | raw AX saved in `BIOSKEYVAL` | `kbd_read()` → AX = scan<<8 \| ascii |
| KERNEL1.SEQ:107-112 | `DOSBK` (INT 23h handler) | 00h | swallows the ^C key and continues (CLC/RETF) | drop |
| Direct BIOSKEY users | MOUSE.SEQ:292 (`mousekey`), MACROS.SEQ:169-171 (`?abortmac`), SEDITOR.SEQ:167 (`emptykbd`), UTILS.SEQ:366-367 (`?funkey` tests `BIOSKEYVAL`) | | | covered by the seam |

### 1.4 Other interrupts (mouse, EMS, printer, timer, multiplex, vectors)

| file:line | word | mechanism | function | port mapping |
|---|---|---|---|---|
| KERNEL2.SEQ:696-705 | `PR-STATUS` (→ `<?PTR.READY>` :707, `?PRINTER.READY` :711) | INT 17h | 02h printer status | return $90 (ready) |
| MOUSE.SEQ:73-77 | `show.ms` | INT 51 (=33h) | 01h show cursor | stub → later `host_mouse_*` |
| MOUSE.SEQ:79-83 | `hide.ms` | INT 33h | 02h hide | stub |
| MOUSE.SEQ:87-110 | `init.mouse` | INT 33h | 00h reset, 0Eh light-pen off | stub (AX=0 means no mouse) |
| MOUSE.SEQ:112-120 | `getmous` | INT 33h | 03h position/buttons | stub |
| MOUSE.SEQ:122-127 | `setmous` | INT 33h | 04h set position | stub |
| MOUSE.SEQ:133-150 | `mouse.scale` | INT 33h | 08h/07h set Y/X range (8 px per cell) | stub |
| MOUSE.SEQ:229-245 | `initmouse` | **IVT read** `0 204 @L` (0:00CC = INT 33h vector) | mouse driver present? | IVT holds 0 → mouse disabled |
| MOUSE.SEQ:261-302 | `mousekey?` / `mousekey` | rebinds `KEY?`/`KEY` | duplicates the `(KEY)` scan-code encoding | keep (uses `BIOSKEY`) |
| EXPAND.SEQ:54-163 | `EMM-STATUS?` (40h), `EMM-PAGE-FRAME` (41h), `EMM-AVAIL-PAGES`/`EMM-TOTAL-PAGES` (42h), `EMM-ALLOC-PAGES` (43h), `EMM-MAP-PAGES` (44h), `EMM-DEALLOC-PAGES` (45h), `EMM-GET-VERSION` (46h) | INT 67h | LIM EMS 4.0 | stub: `EMM-PRESENT?` returns false, so `EMM-INIT` (:171) leaves `EMM-STATUS` set |
| EMMEXEC.SEQ:86-115 | `EMM>FPC` | INT 67h 4400h/45h | restore the image from EMS after EXEC | drop |
| EMMPTR.SEQ:8-58 | `EMM-POINTER`, `EMM-BYE-FUNC` | via EMM-* words | EMS-backed pointers | stub (aborts "No expanded memory") or emulate later |
| EXEC.SEQ:5-29 | `emm-initstuff`, `fpc>emm` | via EMM-* | swap the image to EMS | drop |
| KERNEL1.SEQ:88-105 | `BIOSBK` (INT 1Bh handler) | IVT-installed ISR; writes BDA 0:471, reads 0:417 | Ctrl-Break: sets `BKFLAG` if Shift is held; if `BKABLE`, patches `>NEXT` with JMP `ABNORM` | VM break flag polled in NEXT (§4.6) |
| KERNEL1.SEQ:76-86 | `ABNORM` | writes 0:471; JMP to the WARM entry | break target | VM: restore NEXT → `WARM` |
| KERNEL4.SEQ:185-204 | `DIV0BK` / `DIVIDE0` | INT 00h handler | → `DIV0FUNC` (abort "Divide OVERFLOW") | VM `/`, `UM/MOD` etc. call `DIV0FUNC` on zero or overflow |
| (none) | – | INT 1Ah, INT 08h/1Ch, INT 2Fh | **not used** by the kernel or F-PC.SEQ files. Time comes only from DOS 2Ch. (PROFILE.SEQ in TOOLS hooks 1Ch; XMS.SEQ in TOOLS uses 2Fh) | – |

### 1.5 Port I/O

| file:line | word | ports | purpose | port mapping |
|---|---|---|---|---|
| KERNEL1.SEQ:905-923 | `PC@` `P@` `PC!` `P!` | any | generic 8/16-bit port access | primitives call a small `io_in`/`io_out` emulator |
| VIDEO.SEQ:50-65 | `BLANK.COLOR` | IN 3DAh (986) retrace wait, OUT 3D8h (216 in DL, DH already 3) ← 25h | CGA snow avoidance (only if `BLANKING` is on) | no-op |
| VIDEO.SEQ:67-77 | `SHOW.COLOR` | OUT 3D8h ← 2Dh | re-enable display | no-op |
| VIDEO2.SEQ:60-73, 128-136 | inside `VIDEO-TYPEL` | 3DAh / 3D8h as above | same | drop |
| SOUND.SEQ:14-27 | `note` / `tone` / `ring`; `newbeep` → `BEEP` (:39-42) | OUT 43h ← B6h, 42h ← divisor lo/hi, IN/OUT 61h bits 0-1 (speaker gate) | PC speaker tone, duration via `MS` | trap 42h/43h/61h in `io_out` → `host_tone(freq, on/off)`; initially stub, `BEEP` → BEL |

Users of the `BLANK.COLOR`/`SHOW.COLOR` pair: SAVESCR.SEQ:29-53, MONOCROM.SEQ:48-56, IBMCURSR.SEQ:77-80.

### 1.6 Direct BIOS data area / IVT reads

| file:line | word | address | meaning | port mapping |
|---|---|---|---|---|
| VIDEO.SEQ:25-29 | `?VMODE` | 40:4A, 40:84, 40:60 | columns, rows-1, cursor end scan line | synthesised BDA, kept in sync by vscr |
| KERNEL4.SEQ:475-476 | `GET-CURSOR` | `0 $460 @L` (40:60-61) | cursor shape word | BDA updated by `SET-CURSOR` |
| KERNEL1.SEQ:82-83, 92-93 | `ABNORM`, `BIOSBK` | 0:471 (break flag), 0:417 (shift flags) | | VM-internal |
| SEDITOR.SEQ:97-100 | `?capslock` `?altkey` `?ctrlkey` `?shiftkey` | `0 $417 C@L` | modifier state; used by SEDCOPY:18,38, SEDJUST:52,67, SEDSHELL:56, SEDSORT:58, SEDWHELP:280,364, NEXPECT:118,139 | BDA 40:17 written by host on every key event |
| NEXPECT.SEQ:26 | fallback `?shiftkey` | 0:417 | only if not already defined (the SEDITOR one wins) | same |
| SEDITOR.SEQ:160-169 | `emptykbd` | `0 $41A @L` / `0 $41C @L` (key buffer head/tail) when not `?DOSIO` | flush type-ahead | BDA head/tail mirror queue depth; or rewrite to `begin key? while bioskey drop repeat` |
| WINSTACK.SEQ:26, 49 | `?showstack` (`BGSTUFF`) | `$40 $17 C@L` | both Shift keys → pop-up stack display | BDA 40:17 |
| MOUSE.SEQ:231 | `initmouse` | 0:00CC (INT 33h vector) | mouse driver presence | IVT zero |
| VIEW.SEQ:42 | `ctrl` | 0:417 | **DEAD** (commented) | – |

### 1.7 Direct video memory ($B800 / $B000)

| file:line | word | access | purpose | port mapping |
|---|---|---|---|---|
| VIDEO.SEQ:44-48 | `VMODE.SET` (COLD KERNEL4.SEQ:117, COLOR.SEQ:80) | sets `VIDEO-SEG` = B000h (mode 7) or B800h | mono/colour selection, runs `INITMONO`/`INITCOLOR` | vscr at linear B8000h (or B0000h) |
| VIDEO2.SEQ:16-141 | `VIDEO-TYPEL` / `VIDEO-TYPE` | STOSW of (`ATTRIB`<<8 \| char) at `(#LINE*COLS+#OUT)*2` | **the fast TYPE path** | C primitive `vscr_type(seg,off,len)` |
| IBMCURSR.SEQ:55-63, 74-81 | `LFILLW`, `NORM-DARK` (→ `DARK`) | fills ROWS·COLS words with `ATTRIB`<<8 \| 20h | clear screen | `LFILLW` is a plain memory op; dirty-mark |
| SAVESCR.SEQ:26-55 | `savescr` `restscr` `recoverscr` `recoverline` | `CMOVEL` VRAM ↔ `svseg` (up to 4 nested screens; size ROWS·COLS·2, :10-20) | pop-ups, menus, help, editor | works unchanged on VM memory |
| MONOCROM.SEQ:47-56 | `invert-screen` | `C@L`/`C!L` on attribute bytes | `white-on-black`/`black-on-white` | unchanged |
| SAVESCR.SEQ:67-79 | entry/exit screen save | | **DEAD** (commented) | – |

### 1.8 PSP / environment / process

| file:line | word | access | purpose | port mapping |
|---|---|---|---|---|
| DEFAULT.SEQ:11-27 | `DOS-LINE` (=128), `DOS>TIB`, `HDEFAULT` (→ `DEFAULT`) | CS:0080 command tail (count byte + text) | the command line is interpreted at boot (file to open, or `-` then words) | VM writes the PSP tail from argv (≤126 chars) |
| ENVIRON.SEQ:3-4 | `evseg` | `44 @` = CS:002C environment segment | | VM builds an env block |
| ENVIRON.SEQ:6-70 | `envsize`, `"envfind`, `.env`, `comspec@`, `path@`, `me@` | `C@L`/`@L` over the env block; `ME@` reads the DOS 3+ trailer (program path after `\0\0` + word) | COMSPEC → `CMDPATH` (EXEC.SEQ:64-68) | env from `environ` + `COMSPEC=` (→ $SHELL) + argv[0] trailer |
| EXEC.SEQ:79-82 | `$sys` | `44 @` env seg into the EXEC param block; CS + `exec$` as the command tail | | `host_system` ignores the param block |
| KERNEL4.SEQ:183, KERNEL2.SEQ:501,579, POINTER.SEQ:59 | terminate | AH=00 (needs CS=PSP) | | `host_exit` |
| META86.SEQ:78 | `DOSVER` (metacompiler host side) | | | not target code |

---

## 2. The wrapper layer (seams to reimplement)

The C VM cannot execute 8086 CODE words, so every CODE word has to become a primitive anyway. The OS-touching subset is small. Re-implement these and the remaining Forth source runs unchanged:

| seam | defined | role |
|---|---|---|
| `<BDOS>` (`DEFER BDOS`), `BDOS2`, `OS2` | KERNEL2.SEQ:331-348 | generic `INT 21h`. Implement as `dos_call(AH, AL, BX, CX, DX, DS, ES)` → regs + CF. Live call sites: 00h, 0Eh, 19h, 2Ah-2Dh, 30h |
| `HDOS1`, `HDOS3`, `HDOS4` | HANDLES.SEQ:99-148 | generic `INT 21h` with a carry→flag result. Call sites: 3Ch, 3Dh, 3Eh, 3Fh, 40h, 41h |
| `MOVEPOINTER` `ENDFILE` `CURPOINTER` `<HRENAME>` `FIND-FIRST` `FIND-NEXT` `DTA@` `DTA!` `PDOS` `get_file_date&time` `getdiskfree` | HANDLES.SEQ, SEQREAD.SEQ:90, KERNEL1.SEQ:925, PRINTING.SEQ:127, SEDCODE.SEQ:23 | special-register DOS calls: 42h, 56h, 4Eh/4Fh, 2Fh/1Ah, 47h, 5700h, 36h |
| HCB words: `HOPEN` `HCREATE` `HCLOSE` `HREAD` `HWRITE` `EXHREAD` `EXHWRITE` `HDELETE` `HRENAME` | HANDLES.SEQ:208-291 | colon definitions over HDOSn. They stay Forth if `dos_call` exists |
| `DOS_ALLOC` `DOS_DEALLOC` `DOS_SETBLOCK` (deferred as `ALLOC` `DEALLOC` `SETBLOCK` `MAXBLOCK`) | KERNEL2.SEQ:504-554 | memory arena |
| `BIOSKEY?`, `BIOSKEY` | KERNEL2.SEQ:353-385 | keyboard. `(KEY?)`/`(KEY)` at :391-404 stay Forth |
| User deferreds `EMIT` `KEY?` `KEY` `TYPE` `TYPEL` | KERNEL2.SEQ:22-26; initial values KERNEL4.SEQ:382-386 (`(EMIT)` `(KEY?)` `(KEY)` `(TYPE)` `(TYPEL)`) | `TYPEL` is rebound to `QTYPEL` by `FAST` (QVIDEO.SEQ:21-29). `CONSOLE` = `(CONSOLE)` (KERNEL4.SEQ:518), `PEMIT` = `(PRINT)` (:517), `CR` = `CRLF` (:514). Also `KEYFILTER`/`BGSTUFF` (KERNEL2.SEQ:387-389) |
| `VIDEO-TYPEL` / `VIDEO-TYPE`, `?VMODE`, `BLANK.COLOR`, `SHOW.COLOR` | VIDEO2.SEQ:16-141, VIDEO.SEQ:19-77 | video primitives |
| `IBM-AT` `IBM-AT?` `IBM-DARK` `IBM--LINE` `LFILLW` `SET-CURSOR` (deferreds `AT` `AT?` `DARK` `-LINE`, UTILS.SEQ:81-96, bound by `>IBM` IBMCURSR.SEQ:101-105) | IBMCURSR.SEQ, KERNEL4.SEQ:466 | cursor and screen control |
| `blink!` `pal!` `border!` `border@` | BLINKER.SEQ:13-49 | attribute/palette |
| `PR-STATUS`, `PC@` `PC!` `P@` `P!` | KERNEL2.SEQ:696, KERNEL1.SEQ:905-923 | printer status, ports |
| Vector words: `SAVEVECTORS` `SETBRK`/`SET_VECTORS` `RESTORE_VECTORS` `SETCRITICAL`, ISRs `BIOSBK` `DOSBK` `ABNORM` `DIV0BK` `CRITINT` | KERNEL1.SEQ:76-112, KERNEL4.SEQ:147-259, HCRITICA.SEQ:61-75 | become no-ops plus the VM break and div0 mechanism |
| EMS/EXEC: `EMM-PRESENT?` + 7 `EMM-*` words, `<EXTEXEC>` (+ labels `SMALFPC` `EMM>FPC` `DSK>FPC`) | EXPAND.SEQ, EMMEXEC.SEQ | stub, and `host_system` |
| Mouse: `show.ms` `hide.ms` `init.mouse` `getmous` `setmous` `mouse.scale` | MOUSE.SEQ:73-150 | stub |

**Recommended strategy:** implement one C `dos_call()` emulator, `int10_call()`, `int16_call()`, `int33_call()`, `io_in()`/`io_out()`, and keep a segmented 1 MB VM memory with a synthesised IVT, BDA, PSP, environment and VRAM. Then every primitive listed above becomes a thin register shim. All Forth-level code, including the many call sites that pass DOS function numbers as literals (`$3D00 hdos1`, `44 OS2`, …), stays untouched.

### 2.1 Handle Control Block (HCB)

Defined in HANDLES.SEQ:16-43. `B/HCB` = 70, `HNDLOFFSET` = 68, `B/FILENAME` = 64.

```
offset  size  field        accessor
+0      1     count        (counted string length of name, max 64)
+1      65    name + NUL   >NAM      (ASCIIZ; DS:DX for DOS calls is HCB+1)
+66     2     attrib       >ATTRIB   (passed in CX to 3Ch/3Dh; 0 normal)
+68     2     DOS handle   >HNDLE    (-1 = closed; -2 = "no file, name holds path" sentinel,
                                      see KERNEL4.SEQ:63, EXEC.SEQ:185-187)
```

* `HANDLE <name>` (:31-32) allots one HCB. `CLR-HCB` (:28) erases it and sets the handle to -1.
* `">HANDLE` / `$>HANDLE` / `!HCB` (:62-74) fill the name. `!HCB` upper-cases it when `CAPS` is on. `?DEF.EXT` (:47-60) appends `.SEQ` (`DEFEXT`) if the name has no dot.
* `PATHSET` (deferred, HANDLES.SEQ:177; bound to `prepend.path`, PATHSET.SEQ:22-46) rewrites the name in place into `D:\cwd\name` before HOPEN/HCREATE/HRENAME. The result must fit in about 62 characters.
* The open mode comes from the value `R/W-MODE` (0 read, 1 write, 2 read/write; HANDLES.SEQ:225-261). It resets to `R/W-DMODE` after each `HOPEN`.
* Errors: HOPEN/HCREATE/HCLOSE/HDELETE/HRENAME return 0 or a DOS error code (2 not found, 3 path, 4 too many, 5 access, 6 bad handle, 18 no more files). HREAD/HWRITE return the byte count and store the error in `RWERR`.
* Fixed HCBs:
  * `SEQHANDLE` points into the `HNDLS` stack of 7+1 nested HCBs (SEQREAD.SEQ:39-42, `SEQUP`/`SEQDOWN` :232-366).
  * `CONHNDL` is handle 1 and `PRNHNDL` is handle 4 (SEQREAD.SEQ:46-47, 248-251).
  * Others: `CFGHNDL` (DEFAULT.SEQ:12), `ED1HNDL`/`ED2HNDL` (editor), `EXTHNDL`/`CMDPATH` (EMMEXEC.SEQ:46-47), `COMSPEC$`/`ME$` (ENVIRON.SEQ:24,56), `PATHHNDL`, `EXEHCB`.
* HCB+68 is also read from assembly: EMMEXEC.SEQ:127 (`exthndl 68 +`) and the `MOVEPOINTER`/`ENDFILE`/`CURPOINTER` primitives.

Port notes:
* Map the DOS handle number to a host fd table inside the VM. Keep 0/1/2/4 as reserved stdin, console, stderr and printer.
* Paths need a mapping layer:
  * strip a `X:` drive prefix;
  * convert `\` to `/`;
  * resolve each component case-insensitively, because names are upper-cased;
  * root everything at a configurable directory, so that DOS-visible paths stay short enough for the 64-byte field.
* The DTA layout that callers read directly is DOS's: +21 attribute, +22 time, +24 date, +26 size, +30 13-byte name. Callers: EXEC.SEQ:154-172, WFL.SEQ:112-136. `host_find_*` must fill it exactly.

---

## 3. Keyboard

### 3.1 Encoding (`(KEY)`, KERNEL2.SEQ:395-404)

```
BIOSKEY ( AX = scan<<8 | ascii, AX=0 i.e. Ctrl-Break is skipped, :382-383 )
DUP 127 AND 0= IF          \ ascii byte is 00h (or 80h!)
    FLIP DUP 3 = IF DROP 0 \ scan 03 = Ctrl-@/Ctrl-2 -> NUL
    ELSE 127 AND 128 OR    \ extended: 128 | (scan & 7Fh)
    THEN
ELSE 255 AND THEN          \ ordinary char 1..255
KEYFILTER
```

So **an extended key is the single value `128 + (scan mod 128)`**, not a 0 prefix and not 256+scan. Values 128-255 are shared by extended keys and CP437 characters 80h-FFh typed with Alt-numpad. Two oddities follow:
* ASCII 80h (`Ç`) is misread as an extended key.
* Scan codes ≥ 80h wrap around: Alt-9 (scan 80h) becomes 128, and Ctrl-PgUp (scan 84h) becomes 132.

The same algorithm is duplicated in `mousekey` (MOUSE.SEQ:292-298).

The raw AX stays available in `BIOSKEYVAL` (KERNEL2.SEQ:351). `?FUNKEY` (UTILS.SEQ:366-367) uses it to tell an extended key from a high-ASCII character. `MACROS.SEQ:169-172` tests `BIOSKEY` AND $FF for ESC.

Because INT 16h AH=00 is used (not AH=10h), E0-prefixed grey keys arrive as their standard codes, and **F11/F12 are never seen**.

Ctrl-C is not a signal: DOS break checking is turned off (KERNEL4.SEQ:227-229), so Ctrl-C arrives as key 3 (for example `TILLKEY`, TIMESTUF.SEQ:58).

Values in use:

| key | value | scan | | key | value | scan |
|---|---|---|---|---|---|---|
| F1-F10 | 187-196 | 3Bh-44h | | Home/Up/PgUp | 199/200/201 | 47h/48h/49h |
| Shift-F1-F10 | 212-221 | 54h-5Dh | | Left/Right | 203/205 | 4Bh/4Dh |
| Ctrl-F1-F10 | 222-231 | 5Eh-67h | | End/Down/PgDn | 207/208/209 | 4Fh/50h/51h |
| Alt-F1-F10 | 232-241 (Alt-F10 = 241, SEDITOR.SEQ:394) | 68h-71h | | Ins/Del | 210/211 | 52h/53h |
| Alt-Q…P | 144-153 | 10h-19h | | Ctrl-Left/Right | 243/244 | 73h/74h |
| Alt-A…L | 158-166 | 1Eh-26h | | Ctrl-End/PgDn/Home | 245/246/247 | 75h/76h/77h |
| Alt-Z…M | 172-178 | 2Ch-32h | | Alt-1…8 | 248-255 | 78h-7Fh |
| Shift-Tab | 143 | 0Fh | | Alt-9/0/-/= | 128/129/130/131 | 80h-83h (wrapped) |
| Ctrl-Backspace | 127 (ASCII 7Fh) | 0Eh | | Ctrl-PgUp | 132 | 84h (wrapped) |

### 3.2 Tables that depend on this encoding

* **SED editor**
  * `s^tbl` (SEDIT2.SEQ:44-53) handles control characters 0-31.
  * `sfuntbl` (SEDIT2.SEQ:57+) is indexed by `key-127`: index 0 is Ctrl-Backspace (127), then keys 128-255.
  * `ctlset` / `fnset` (SEDIT2.SEQ:98-118) patch those tables.
  * EDITSET.SEQ:18-140+ assigns keys, for example `199 fnset shoml`, `187 fnset helpF1`, `241`/`196` exit.
  * `skeyfilter` (SEDITOR.SEQ:390-395) maps ESC, Alt-F10 (241) and F10 (196) to Enter. It is installed into `KEYFILTER` at SEDIT2.SEQ:179.
* **Line editor:** `keyfuncs1`/`keyfuncs2` are 128-entry arrays for keys 128-255 (LEDIT.SEQ:259-295). They are filled by `lkey!` at LEDIT.SEQ:361-389 (199, 200, 201, 203, 205, 207-211, 243, 244, 158), and control characters go through `?control` (LEDIT.SEQ:243-257).
* **Kernel `EXPECT`:** `NORM-KEYTABLE` (KERNEL2.SEQ:823-833) handles control characters only.
* **Other code testing key literals:** MENUS.SEQ:246-283 (199/207/205/203/200/208), WFL.SEQ:334-341 & 420, HELPLINK.SEQ (F1 = 187, `helpkey` EDITSET.SEQ:117), MACROS.SEQ (`macbase` key range), SEDITOR.SEQ:1440-1459 (key help iterates `i 127 and`).
* **Modifier state** comes from the BDA, not from KEY (§1.6), for Shift-selection in SEDCOPY/NEXPECT/SEDWHELP and for WINSTACK's two-Shift trigger. The host must keep 40:17 current.

**Port:** translate host key events (SDL keysym, or a decoded ANSI/xterm escape sequence) into a BIOS INT 16h AX value using the standard set-1 BIOS tables, including the Shift/Ctrl/Alt variants. Queue them in a 16-entry ring and mirror it into BDA 40:1A/1C. `BIOSKEY` returns AX, and the Forth `(KEY)` does the rest, so every table above keeps working.

With an ANSI terminal, Shift-arrows and Alt-letters only come through as xterm modifier sequences or ESC-prefix sequences, and a bare Shift press is invisible. SDL gives full fidelity.

Ctrl-Break (AX=0000) should set the break flag (§4.6) instead of being queued.

---

## 4. Video

### 4.1 Output paths

* **`TYPE`** → `(TYPE)` (SEQREAD.SEQ:79) → `TYPEL`. After `FAST` (QVIDEO.SEQ:21-29, the default; it also sets `?DOSIO` false) this is `QTYPEL` → `VIDEO-TYPEL` (VIDEO2.SEQ:16), unless `PRINTING` is set.
  * Writes at row `#LINE` (clipped to ROWS-1) and column `#OUT`.
  * Each cell gets the low byte of `ATTRIB`.
  * Clips at the right edge: there is **no wrap and no scroll**, and the hard limit is 132 columns.
  * Advances `#OUT` and moves the hardware cursor through INT 10h AH=2, unless `NOSETCUR` is on.
  * `SLOW` switches `TYPEL` to `(TYPEL)`, which is DOS write to handle 1.
* **`EMIT`** → `(EMIT)` (KERNEL2.SEQ:720) → `CONSOLE` = `(CONSOLE)` → `CONSOLEL` → `EXHWRITE` of 1 byte to handle 1. This is always DOS output, through the CON driver and the BIOS TTY:
  * `CR` = `CRLF` (KERNEL2.SEQ:727-732) emits 13 and 10, so scrolling at the bottom line is done by the BIOS, which fills the new line with the attribute of the cell at the cursor.
  * `BEEP` = `%BEEP` emits BEL (:761-764) unless SOUND.SEQ rebinds it.
  * Backspaces are emitted in `BS-IN`/`DEL-IN`.
  * TTY output replaces the character only; **the cell keeps its existing attribute**.
* **`#OUT`/`#LINE`** are Forth's own idea of the cursor. `CURSOR_POS_INIT` (IBMCURSR.SEQ:107-112, installed as `CURSORSET`, called by SETYSEG and after `$SYS`) resynchronises them from INT 10h AH=3.

### 4.2 Cursor

* `AT` (UTILS.SEQ:81-84) → `IBM-AT`, which also stores `#LINE`/`#OUT`.
* `AT?` → `IBM-AT?`.
* Shape: `SET-CURSOR` (INT 10h AH=1) and `GET-CURSOR` (reads BDA 40:60).
* `CURSOR-OFF`/`CURSOR-ON` (IBMCURSR.SEQ:126-128) set or clear bit 2000h.
* `NORM-`/`BIG-`/`MED-CURSOR` (:132-145) are computed from `CROWS` (40:60).
* `SAVECURSOR`/`RESTCURSOR` (UTILS.SEQ:98-110) save and restore attribute, shape and position.

### 4.3 Attributes

* `ATTRIB` (VIDEO.SEQ:11) is a word variable; only the low byte is used.
* MONOCROM.SEQ:7-43 defines `NORMVAL` (07) and `REVVAL` (78) and the `>NORM`/`>REV` deferreds (UTILS.SEQ:204-205). `>ATTRIB1`…`>ATTRIB8` are bound by `>MONO` (MDA codes: 01 underline, 7F bright, 79, 8F, F0).
* COLOR.SEQ:24-78 binds `>COLOR` from the `COLORS` array of background/foreground pairs, using `>FG`/`>BG`.
* `>LCD` (:84-89) is an alternative.
* `blinkoff` runs at start-up (HELLO.SEQ:6), so bit 7 means **bright background**, not blink.
* Embedded attribute escapes in strings (`\1`…`\8`, `\0`, `\r`, `\d`, `\n`, `\s<n>`) are interpreted by PERTYPE.SEQ through `AT` and `>ATTRIBn`.
* The renderer must draw the CP437 glyph set; HELLO.SEQ uses box-drawing bytes.

### 4.4 Screen clear, scroll, save/restore

* `DARK` = `NORM-DARK` (IBMCURSR.SEQ:74-81).
  * With `?DOSIO` it uses `IBM-DARK` (mode re-set, attribute 07).
  * Otherwise `LFILLW` writes `ATTRIB` + space over ROWS·COLS cells.
  * Either way it finishes with `0 0 AT`.
* `-LINE` = BIOS scroll-up of the region from the cursor row down, attribute 07.
* `SAVESCR`/`RESTSCR`/`RECOVERSCR`/`RECOVERLINE` (SAVESCR.SEQ) copy raw VRAM into a `POINTER` buffer of up to 4 screens, limited to under 64 KB. These are used by menus, help, WINSTACK and SED.

### 4.5 Mono vs colour detection

* `VMODE.SET` (VIDEO.SEQ:44-48) runs at COLD (KERNEL4.SEQ:117) and again at COLOR.SEQ:80.
* If INT 10h AH=0Fh returns mode 7, it selects `VIDEO-SEG`=B000h and runs `INITMONO`; otherwise B800h and `INITCOLOR`.
* MONOCROM binds both to `>MONO` and COLOR rebinds `INITCOLOR` to `>COLOR`.
* `COLS`/`ROWS` come from BDA 40:4A/40:84, so EGA/VGA 43/50-line modes work. `ROWS` is forced to at least 25.
* CGA snow suppression (`BLANKING`, ports 3DAh/3D8h) only runs in colour mode with `BLANKON`.

### 4.6 Break and divide-by-zero (not video, but part of the console runtime)

* INT 1Bh → `BIOSBK` patches the inner interpreter `>NEXT` (`LODSW ES:` / `JMP AX` = 26ADh/E0FFh) into a JMP to `ABNORM`. `ABNORM` restores it and jumps to the WARM entry, where `WARMFUNC` aborts "Warm Start".
* It does this only if `BKABLE` is set. Holding Shift sets `BKFLAG`.
* **Port:** have the host set a `break_pending` flag (SDL Ctrl-Break/Pause, or SIGINT/SIGQUIT if the terminal keeps ISIG for one chosen key). The VM checks the flag every N instructions or in NEXT, honours `BKABLE`, writes `BKFLAG` from the host shift state, and jumps to WARM.
* Divide by zero is raised by the C `/MOD`/`UM/MOD` primitives through `DIV0FUNC`.

---

## 5. Proposed C host interface

The design has two layers:
* **VM layer:** portable C. It owns the 1 MB segmented memory, the vscreen at B800:0, the synthesised IVT/BDA/PSP/env, the DOS/BIOS function emulators, and the fd table.
* **Host layer:** platform-specific. It only does I/O.

### 5.1 Host calls

```c
/* ---- lifecycle / process ---- */
int   host_init(int argc, char **argv);              /* parse args, open terminal/SDL */
void  host_exit(int code);                           /* DOS 00h/4Ch, BYE */
int   host_cmdline(char *buf, int max);              /* -> PSP:80 tail (DEFAULT.SEQ) */
int   host_environ(char *buf, int max);              /* KEY=VAL\0...\0\0 + argv0 trailer; COMSPEC=$SHELL */
int   host_system(const char *cmd);                  /* $SYS / 4B00h; suspend+restore terminal; returns exit code */

/* ---- console rendering (vscreen lives in VM memory; host only draws) ---- */
int   host_screen_open(int *cols, int *rows, int *mono);          /* requested 80x25 default */
void  host_screen_present(const uint16_t *cells, int cols, int rows,
                          const uint8_t *dirty_rows);              /* char|attr<<8, CP437 */
void  host_cursor(int col, int row, uint16_t shape);              /* shape bit 0x2000 = hidden */
void  host_set_blink(int blink_enabled);                           /* INT10 1003h: bit7 blink vs bright bg */
void  host_set_palette(int reg, int color);                        /* INT10 1000h (optional) */
void  host_beep(void);                                             /* BEL / %BEEP */
void  host_tone(unsigned freq_hz, int on);                         /* PIT ch2 + port 61h emulation (later) */

/* ---- keyboard ---- */
int      host_key_poll(void);           /* pump events; nonzero if a key is queued */
uint16_t host_key_get(int wait);        /* BIOS AX = scan<<8|ascii; 0 if none and !wait */
uint8_t  host_shift_flags(void);        /* BDA 40:17 bits: RShift1 LShift2 Ctrl4 Alt8 Scroll10 Num20 Caps40 */
int      host_break_pending(void);      /* Ctrl-Break; read-and-clear */

/* ---- mouse (phase 2) ---- */
int   host_mouse_get(int *col, int *row, int *buttons);   /* INT33 03h, in character cells */
void  host_mouse_set(int col, int row);                    /* INT33 04h */
void  host_mouse_show(int visible);                        /* INT33 01h/02h */

/* ---- files (paths are DOS-form; host maps drive, '\\', case) ---- */
int   host_open(const char *dospath, int mode);            /* 3Dh: 0 R,1 W,2 RW -> fd | -doserr */
int   host_create(const char *dospath, int attr);          /* 3Ch */
int   host_close(int fd);                                  /* 3Eh */
long  host_read(int fd, void *buf, unsigned n);            /* 3Fh  (fd 0 = stdin) */
long  host_write(int fd, const void *buf, unsigned n);     /* 40h  (fd 1 routed to vscr TTY by VM) */
long  host_seek(int fd, long off, int whence);             /* 42h */
int   host_delete(const char *dospath);                    /* 41h */
int   host_rename(const char *from, const char *to);       /* 56h */
int   host_getcwd(int drive, char *buf, int max);          /* 47h: "DIR\\SUB" (no drive, no lead '\\') */
int   host_chdir(const char *dospath);                     /* for CD (instead of shelling out) */
int   host_get_drive(void);                                /* 19h -> 2 */
int   host_set_drive(int drive);                           /* 0Eh -> no-op, returns #drives */
struct host_dta { uint8_t attr; uint16_t time, date; uint32_t size; char name[13]; void *priv; };
int   host_find_first(const char *dospattern, int attr, struct host_dta *d);   /* 4Eh -> 0 | 18 */
int   host_find_next(struct host_dta *d);                                       /* 4Fh */
int   host_file_datetime(int fd, uint16_t *dos_time, uint16_t *dos_date);      /* 5700h */
int   host_disk_free(int drive, uint16_t *avail_clusters,
                     uint16_t *bytes_per_sector, uint16_t *sectors_per_cluster); /* 36h */

/* ---- printer ---- */
long  host_printer_write(const void *buf, unsigned n);    /* handle 4 / "PRN" -> file or lpr */
int   host_printer_status(void);                          /* INT17 02h -> 0x90 */

/* ---- time ---- */
void     host_get_date(int *year, int *month, int *day, int *dow);    /* 2Ah */
void     host_get_time(int *h, int *m, int *s, int *hundredths);      /* 2Ch */
void     host_sleep_ms(unsigned ms);                                  /* replace calibrated MS */
uint32_t host_ticks_ms(void);                                         /* for PAUSE/idle, render pacing */
```

That is about 40 calls. With `host_screen_*`/`host_key_*` behind one backend switch, you can ship an ANSI/termios backend first and add SDL later.

### 5.2 VM-side emulators (C, portable) that call the host

* **`dos_call` (INT 21h):** 00, 0E, 19, 1A, 25/35 (store only), 2A-2D, 2F, 30, 33, 36, 3C-42, 47, 48, 49, 4A, 4B (→ `host_system`), 4E, 4F, 56, 5700. Unknown functions log a message and set CF. TOOLS would add 07, 09, 3A, 3B, 43, 4C.
* **`int10_call`:** 00 (clear), 01, 02, 03, 06/07 (scroll), 0F (returns 3 or 7), 1000/1001/1003/1008. Every call updates BDA 40:4A/40:50/40:60/40:84.
* **`int16_call`:** 00, 01.
* **`int17_call`:** 02.
* **`int33_call`:** 0, 1, 2, 3, 4, 7, 8, 0Eh. Phase 1 leaves the IVT entry 0:CC at zero.
* **`int67`:** absent. The 0:019C vector is zero and there is no "EMMXXXX0" device header, so `EMM-PRESENT?` returns false. Simpler still: make the primitive return 0.
* **`io_in`/`io_out`:**
  * 3DAh reads 09h, so retrace loops finish immediately.
  * 3D8h is ignored.
  * 42h, 43h and 61h drive `host_tone`.
  * Anything else reads FFh and is logged.
* **vscreen:** a cell array at linear B8000h (or B0000h for mode 7).
  * Every segmented store (`!L`, `C!L`, `CMOVEL`, `CMOVEL>`, `LFILLW`, and `VIDEO-TYPEL`) dirty-marks any range it touches inside the window.
  * Handle 1 (TTY) writes handle 7, 8, 10 and 13, scroll with the cursor-cell attribute, and replace the character only.
  * Present the screen before any blocking `KEY` and at most 60 times a second from `PAUSE`/`KEY?`.

### 5.3 Drop or stub

| item | where | decision |
|---|---|---|
| EMS (INT 67h) incl. `EMM-POINTER`, image swapping to EMS | EXPAND.SEQ, EMMEXEC.SEQ, EMMPTR.SEQ, EXEC.SEQ:5-29 | stub: not present. Optionally emulate later as plain memory |
| Swap image to disk before EXEC (`FPCIMAGE.$$$`) | EMMEXEC.SEQ:120-156, EXEC.SEQ:33-62 | drop (`USE-DISK` off) |
| EXEC 4Bh memory shrink/regrow dance | EMMEXEC.SEQ:62-205, SEDSHELL.SEQ:17-53 | replace with `host_system`. `SETBLOCK` shrink/regrow becomes a trivial success |
| `SYS DIR`/`DEL`/`COPY`/`REN`/`CHDIR` via COMMAND.COM | EXEC.SEQ:176-198 | keep `host_system`. `CHDIR` should call `host_chdir`, because a child shell's cd does not persist |
| Interrupt vector save/restore (1Bh, 23h, 24h, 00h), BREAK flag | KERNEL4.SEQ:147-259, HCRITICA.SEQ | no-op. Break and div0 move into the VM |
| Printer port INT 17h, PRN device | KERNEL2.SEQ:696, PRINT/PRTCTRL/PRINTING/LASERJET/PROPRINT | stub: status ready, output to file |
| PC speaker (PIT/61h) | SOUND.SEQ | phase 1: `BEEP` → `host_beep`. Phase 2: tone |
| CGA snow blanking | VIDEO.SEQ:50-77, VIDEO2.SEQ:60-73 | drop |
| Palette, border, blink | BLINKER.SEQ | blink → renderer flag; palette/border → no-op |
| Mouse INT 33h | MOUSE.SEQ, MOUSEY.SEQ | phase 1: no mouse (IVT zero). Phase 2: `host_mouse_*` |
| Set date/time 2Bh/2Dh | TIMER.SEQ:5,12 | return failure |
| Drive select 0Eh / `A:`…`F:` | KERNEL4.SEQ:60-73 | no-op |
| Calibrated busy-wait `MS` + `bufsize-init` | BUFSET.SEQ:5-30 | replace `MS` with `host_sleep_ms`. Keep `IBLEN` at its default |
| `SAVE-EXE` MZ writer / LOADEXE relocator | SAVEEXE.SEQ, LOADEXE.SEQ | replace with a VM image format |
| Text-file conventions | SEQREAD.SEQ:147-162, :298-308 | lines split on LF; trailing CRLF → blanks; ^Z → blank. LF-only files leave a stray LF in the TIB: either normalise in `host_read` for `.SEQ` or patch `CRLF>BL'S` to also accept bare LF |

---

## 6. Risks and oddities found

1. **`EMIT` and `TYPE` take different routes** (DOS TTY vs direct VRAM), and they keep each other in step only through the hardware cursor. The vscreen must use one cursor for both. Otherwise `CR` after `TYPE` lands in the wrong place.
2. **`?DOSIO` defaults to TRUE** (UTILS.SEQ:22) until `FAST` (QVIDEO.SEQ:29) clears it. The editor branches on it (SEDITOR.SEQ:161,183,578,618,643; MENUS.SEQ:154,168), for example to avoid writing the bottom-right cell in DOS mode.
3. **The key encoding wraps scan codes ≥ 80h and collides with high ASCII.** Keep the encoding bit-exact for compatibility. Do not "fix" it to 256+scan, because the tables in §3.2 are hard-coded.
4. **CS = PSP is assumed.** The program uses `44 @`, `128 ...`, and AH=00 terminate. The VM must place a PSP at offset 0 of the code segment and start the dictionary at 100h (KERNEL1.SEQ:18, `256 DP-T !`).
5. **`DOS-LINE` (PSP:80) is also the default DTA.** `HDEFAULT` reads it before any `FIND-FIRST`, which matters only if the VM puts the default DTA there.
6. **Names are upper-cased and limited to 64 bytes**, and `prepend.path` builds absolute `D:\...` names. Path mapping must be case-insensitive and rooted.
7. **`emptykbd` reads the BDA key-buffer pointers directly** (SEDITOR.SEQ:165-166). The BDA mirror of queue depth must be maintained, or that word must be patched.

---

## 7. TOOLS/ (optional add-ons) — files with OS touchpoints

| file | touchpoints |
|---|---|
| BLKTOSEQ.SEQ | HCB file I/O only (HOPEN/HCREATE/HREAD/HWRITE): portable through the seam |
| BLOCK.SEQ | virtual block system using HREAD/HWRITE: portable through the seam |
| CODEBUG.SEQ | INT 21h 35h/25h on vectors 00h, 01h, 03h (single-step/breakpoint debugger for CODE words): drop (no 8086) |
| COLOURS.SEQ / COLPLAY.SEQ | write `ATTRIB` only (colour demos): portable |
| DIS8086.SEQ | INT 21h 35h (`#INT2@`, read any vector) + HCB output: stub (IVT read) |
| DMULDIV.SEQ | INT 0 commented out: none |
| DOSIO.SEQ | `BDOS 7` (direct console input, no echo) as `KEY`; `BDOS 0`: map 07h to `kbd_read` low byte |
| EMMEXMPL.SEQ | EMS example using EMM-* words + `VIDEO-SEG`: drop with EMS |
| EXPANDED.SEQ | stub note only ("now built into the kernel"): none |
| MONITOR.SEQ | postfix `16 INT` = INT 10h 00, 03, 06, 07, 08, 09 (mode set, scroll, read/write char+attr): needs `int10_call` 06-09 |
| NEW-WFL.SEQ | INT 10h 06/07/09, INT 21h 0Eh, 19h, 1Ah, 2Fh, 3Ah (rmdir), 3Bh (chdir), 41h, 43h (chmod), 47h, 4Eh/4Fh, 56h: extended file picker; needs extra `dos_call` functions |
| NEWCOM.SEQ | assembles .COM files; INT 21h 00h in generated code + HCB writes: drop (target is 8086) |
| OVERLAY.SEQ | overlays via HCB EXHREAD/EXHWRITE: drop or re-design (no overlays in VM) |
| PROFILE.SEQ | hooks INT 1Ch timer tick (35h/25h 1Ch) + HCB output: needs a periodic VM callback, or drop |
| RS232IB.SEQ | 8250 UART + 8259 PIC port I/O, IRQ vector install (25h/35h 0Bh/0Ch): drop (or map to a host serial API) |
| SCROLL.SEQ | INT 10h 06h/07h window scroll: `int10_call` 06/07 |
| SEQTOBLK.SEQ | HCB I/O: portable |
| WINDOW.SEQ | postfix `16 INT` (INT 10h 03, 06, 09) and `33 INT` (INT 21h 48h/49h/4Ah memory; values passed as decimal 72/73/74): `int10_call` + VM arena |
| WYSE50.SEQ | Wyse-50 escape decoder on `EMIT` (uses AT/DARK): portable |
| XMS.SEQ | INT 2Fh 4300h/4310h XMS driver detection + far call: stub (no XMS) |
