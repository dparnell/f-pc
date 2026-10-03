# Expected-output divergences from the 16-bit oracle

`tests/golden/<t>.KERNEL.out` is what the original F-PC 3.6 kernel printed under
DOSBox. `tests/expected/<t>.out` is what the native 32-bit port must print.
Every line that differs is listed here with its reason. A difference with no
entry here is a bug.

Reasons:
- **W**: cell width (16 → 32 bits). Wraparound, unsigned maximum, double-cell
  splitting and byte offsets all change.
- **H**: test harness. The DOSBox harness logs load errors as
  `*** ERROR at line N : ...`. Natively, the error report is the F-PC style
  `file = … at Line N` / source line / `---^-- message`.
- **S**: segment words were removed (flat memory model, kernel-design.md §2.1).

## arith
| line | oracle | port | why |
|---|---|---|---|
| add/sub/wrap | `-32768 32768 65535` | `32768 32768 4294967295` | W: `32767 1+` no longer wraps; `-1 U.` is 2³²-1 |
| mul | `24464 0` | `90000 65536` | W: `300 300 *` and `256 256 *` no longer overflow |
| um | `65534 1` | `0 4294836225` | W: `65535 65535 UM*` fits in the low cell; hi = 0 |
| um/mod | `9362 2` | `613566756 4` | W: `0 1` as a double is 2³² rather than 2¹⁶ |
| neg/abs | `-32768 -32768` | `32768 32768` | W: -32768 is no longer the most negative number |
| min/max | `65535` | `4294967295` | W |
| 2* | `-32768` | `32768` | W: `16384 2*` no longer reaches the sign bit |
| s>d | `4464` | `70000` | W: `70000. DROP` is no longer truncated |

## stack
| line | oracle | port | why |
|---|---|---|---|
| sp | `4` | `8` | W: two cells are 8 bytes |

## define
| line | oracle | port | why |
|---|---|---|---|
| create | `20 30` | `1310720 20` | W: the test uses 16-bit cell offsets (`TBL 2+`, `TBL 4 +`) |
| forget | `*** ERROR …` | F-PC-style error report | H. The test deliberately ends with an undefined word. |

## Not run natively yet
- **strings**: uses kernel words (`">$`) that arrive with the ported kernel (M2).
- **farmem**: tests `@L !L C@L CMOVEL LFILL PARAGRAPH +XSEG` (S). It stays as
  a DOSBox-only oracle; a flat-model replacement is `tests/listspace.seq` (M2).
