# F-PC native port: status

Last updated 2026-10-04 (overnight session). Design: [kernel-design.md](kernel-design.md). Porting rules: [porting-guide.md](porting-guide.md). Per-file notes: [port-notes/](port-notes/).

## Quick start

```bash
make -C vm          # build vm/fpc (C11 + GNU computed goto; vendored sljit)
make -C vm image    # load SRC/F-PC.SEQ and save F-PC.IMG (as EXTEND.BAT made F-PC.EXE)
vm/fpc -i F-PC.IMG  # full F-PC: sign-on, status line, SED editor, debugger...
vm/fpc              # bare kernel, loaded from SRC/KERNEL.SEQ (~6 ms)
make -C vm test     # regression tests: from source, from an image, without JIT
make -C vm test-ui  # UI scenarios in a pseudo-terminal (needs: pip install pyte)
```

Running `fpc`:
- `--batch` gives plain stdin/stdout, with no screen control.
- The remaining arguments are F-PC's DOS command line, for example `vm/fpc - FLOAD MYFILE.SEQ BYE`.
- Sources are found through `FPATH`, which defaults to the installation directory and its `SRC`, `HLP` and `TOOLS`, so `fpc` works from any directory.

## Milestones

| | Status |
|---|---|
| Step 0: original build and oracle in DOSBox | Done. `KERNEL.COM` rebuilds byte-for-byte; tests have 16-bit golden output |
| Step 1: inventory | Done (`docs/inventory/`) |
| Step 2: kernel design | Done, with your decisions: 32-bit cells, flat memory, C seed, `SRC/` edited in place, terminal-size UI |
| M1: C VM and seed interpreter | Done |
| M2: kernel `.SEQ` ported, F-PC's own QUIT/FLOAD/errors | Done |
| M3: system images | Done (`SAVE-IMAGE`, `FSAVE`/`SAVE-EXE`, `fpc -i`) |
| M4: all `F-PC.SEQ` extensions | Done: all 88 files load (12,136 lines in about 0.1 s) |
| M5: terminal UI with live resize | Done. The ANSI host diffs the VM's text screen; `SIGWINCH` drives `RESIZED` and `K-RESIZE`. SED, the status line, menus and the file list follow the terminal size |
| M6: native CODE words | Done. AM assembler (`SRC/AMASM.SEQ`), interpreter, sljit JIT (about 6x faster than the interpreter) |
| M7: debugger, multitasker, SDL | Debugger done, on a VM trace hook. Multitasker (`MULTASK.SEQ`, not loaded by default) and SDL not started |

## What is verified interactively

`tests/ui/*.ui` scenarios run in a pseudo-terminal (`tools/runtests-ui.sh`) and check:
- **Sign-on and prompt.** The sign-on screen, the status line and the `ok` prompt.
- **Resize.** Shrinking and growing the terminal reflows the screen.
- **SED.** Opening a file (`FILE X` then `1 EDIT`), cursor keys, typing, F10 save with a `.BAK` backup, and the editor window resizing.
- **Debugger.** `DEBUG word`: stepping, Nest, Unnest and Continue, with the source shown in the top pane.
- **Menus and help.** The ESC menu bar with the File drop-down, and F1 hypertext help.
- **Batch mode** (checked by hand): `SEE`, `WORDS` and `DUMP`.

## Known gaps / next steps

- **TOOLS/** (66 optional add-ons, including the float packages) is not ported yet.
- **Mouse.** `MOUSE-PRESENT?` returns false. xterm mouse reporting can come next.
- **Popups that assume 80x25.** WFL, the sign-on box and some SED popups clip on smaller terminals rather than reflowing.
- **Kernel words can't be VIEWed:** the seed records no FILES variables for `KERNEL*.SEQ`.
- **`GETDISKFREE`** reports a large disk.
- **PRN.** The VM discards printer output unless `PFILE` redirects it.
- **Performance.** Primitives are C built-ins, with no top-of-stack caching yet; the JIT covers only CODE words.

## Differences from the original worth knowing

| Area | Difference |
|---|---|
| Cells | 32 bits; doubles are 64 |
| Segments | Gone: `@L`, `CMOVEL` etc. are replaced by flat addresses |
| `CMOVE` | Moves bytes (the original moved 16-bit words) |
| `K-RESIZE` | Key 130 (BIOS `$0200`), a code no real key produces |
| Handle block | `B/HCB` is 140; file names can be 127 characters |
| CODE words | Use the AM assembler (registers `R0`–`R7`, `SP`, `RP`, `IP`, `W`, `UP`), not 8086 |
| Images | `.IMG` files tied to the `fpc` build that wrote them (a checksum of built-in names) |
| DOS commands | Mapped to the host: `DIR`, `CD`, `DEL`, `COPY`, `REN`, shell escapes via `$SHELL` |
| `F-PC.SES` | New, self-checking 32-bit format |
