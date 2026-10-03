# F-PC 3.6, native

This is Tom Zimmer's F-PC 3.6 Forth, ported from 16-bit DOS to a portable C virtual machine with 32-bit cells. F-PC's own high-level `.SEQ` source is the system and is maintained as such: the kernel, the SED editor, the debugger, the menus and the hypertext help. Only the 8086, DOS and BIOS layer underneath was replaced.

```bash
make -C vm                 # build vm/fpc (C11, GNU computed goto; SDL2 optional)
make -C vm image           # build the full system image F-PC.IMG from SRC/F-PC.SEQ
./f-pc                     # run it (checks the VM and image are built; same as vm/fpc -i F-PC.IMG)
vm/fpc -i F-PC.IMG         # run it in the terminal (adapts to the terminal size)
vm/fpc --sdl -i F-PC.IMG   # ... or in an SDL window
vm/fpc - FLOAD MY.SEQ BYE  # scripted: the rest of the line is F-PC's command line
make -C vm test            # regression tests; make -C vm test-ui needs pip install pyte
make -C vm test-tools      # load each TOOLS/*.SEQ add-on
make -C vm install PREFIX=~/.local   # then: f-pc (full system), fpc (bare kernel); make -C vm uninstall
```

Configuration:
- `F-PC.CFG` is Forth that runs at every start. It sets colours, `.BAK` backups (`BACKUPON`), `FPATH` and so on. The bare kernel reads `KERNEL.CFG` instead.
- The copies next to the image (in the source tree, or `PREFIX/lib/fpc` when installed) are the defaults.
- Your own copy goes in `~/.config/f-pc/` (or `$XDG_CONFIG_HOME/f-pc/`) and is used instead. `EDIT-CONFIG` copies the default there the first time and opens it in the editor.
- The hypertext index `HYPER.NDX` works the same way: `FLOAD INDEX.SEQ BINDEX` writes your own index there.

Things to try:
- `WORDS`, and `SEE word` (the decompiler).
- `DEBUG word`, then run the word (the single-stepping debugger).
- `FILE name` then `1 EDIT` (SED, the editor); `VIEW word` opens a word's source.
- F1 (hypertext help), ESC (the menu bar).
- The add-ons in `TOOLS/`, e.g. `FLOAD FFLOAT.SEQ` (floating point), `FLOAD SPREAD.SEQ` (a spreadsheet), `FLOAD NEW-WFL.SEQ` (a file browser), `FLOAD DISASSEM.SEQ` then `SEE` a CODE word.

How it works:
- **The VM.** Built-ins are C; the inner interpreter uses computed goto; memory is flat and bounds-checked; images can be saved.
- **CODE words.** These are written for a small abstract register machine (`SRC/AMASM.SEQ`) and compiled to host code with sljit, with an interpreter as the fallback.
- **The host interface.** DOS calls are emulated over POSIX. The screen is a virtual PC text screen, rendered with ANSI or SDL. Keys arrive as BIOS scan codes.

Documentation:
- [docs/STATUS.md](docs/STATUS.md): current state and known gaps.
- [docs/kernel-design.md](docs/kernel-design.md): the design.
- [docs/porting-guide.md](docs/porting-guide.md): how `.SEQ` files are ported.
- [docs/inventory/](docs/inventory/): the analysis of the original.
- [tests/expected/DIVERGENCES.md](tests/expected/DIVERGENCES.md): where behaviour deliberately differs from the 16-bit original.

The unmodified original is at the git tag `fpc-3.6-original`. `tools/dosbox/` rebuilds it in DOSBox and records its behaviour as the test oracle.

The DOS executables (`F-PC.EXE`, `KERNEL.COM`, `INSTALL.COM`), the `.BAT` build scripts and the NEWZ utilities are no longer in the tree; they are kept at that tag, which the DOSBox scripts use. The manual in `HLP/` still describes them: where it says `EXTEND`, use `make -C vm image`, and for `F` or `F-PC` use `vm/fpc -i F-PC.IMG` (or `f-pc` once installed).
