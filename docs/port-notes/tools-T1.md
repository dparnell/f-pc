# TOOLS group T1 port notes: floating point

Files: FFLOAT, FLTAUX, FLTBUG, FPMATH, COMPLEX, DMULDIV, SFLOAT, SFLOAT1, SFLOAT2, SFLOAT3. The
.HLP and .TXT files and SFBUGS.FIX are documentation and are unchanged; FFLOAT.HLP still documents
the 8087 assembler words of FASSEM.SEQ, which do not exist in the port.

Status: every file loads and was exercised. Line endings: FFLOAT, FLTAUX, SFLOAT1, SFLOAT2 and
SFLOAT3 were rewritten and are LF now. FLTBUG, FPMATH, COMPLEX, DMULDIV and SFLOAT.SEQ keep CRLF
(DMULDIV keeps its trailing `^Z^Z`). None of these files has CP437 bytes.

**FFLOAT and SFLOAT are alternatives.** Load one or the other, as before: both define `F+ F@ F.`
and the rest. FPMATH and COMPLEX work on top of either.

Tests:
- **`tests/float.seq`** exercises FFLOAT: input, `FLOATS`/`DOUBLES`, arithmetic, `E.` `F.` `F.R`
  `E.R`, specials (infinity, NaN), stack words, compares, rounding, conversions, powers, trig in
  radians and `DEGREES`, exp/log, hyperbolics, `FVARIABLE` `FCONSTANT` `FLITERAL` `FARRAY`, `FMOD`
  `FMAG` `FEXAM` `.FS` `FR.`, and the `FPERR` messages.
  Run it with `cd tests && timeout 20 ../vm/fpc --batch -i ../F-PC.IMG - FLOAD float.seq BYE </dev/null`.
  No expected-output file was recorded.
- **Checked by hand against Python:**
  - SFLOAT bit patterns (`FPOP` of `F+ F* F/ FSQRT FLOAT FLN F/LN2 F/PREM TINTA`...).
  - SFLOAT transcendental values.
  - COMPLEX on both packages.
  - FPMATH on both packages.
  - DMULDIV (`UMD*`, `UMD/MOD` including the overflow case, `D*`).
  - FLTAUX `.NPUSTK` `H.NPUSTK` `(E.R)`.
- **Not tested:** FLTBUG's debugger keys (`.` `I` `<` `>`) need an interactive `DEBUG` session.
  The file loads and hooks `extra-commands`.

## Built-ins needed

None are required. Nothing is declared with `BUILTIN`. Two would be nice to have:

| Name | Stack | Semantics | Why |
|---|---|---|---|
| `FPSW>` (or a new name such as `FEXCEPT@`) | `( -- n )` | Read and clear the host FP exception flags (`fetestexcept`/`feclearexcept`). Return them in 8087 status-word bits: 1 invalid, 4 zero-divide, 8 overflow, $10 underflow, $20 inexact. | FFLOAT's `?FSTACK` reported these sticky flags after each line (TODO-PORT in FFLOAT.SEQ). Today FFLOAT's `FPSW>` returns only the stack TOP field. |
| decompiler support for `(FLIT)` | | `SEE` should show the in-line float after `(FLIT)`: 2 cells for the VM's built-in, 3 cells for SFLOAT's own `(FLIT)`. | `SEE` currently prints the cells as `NO-NAME`s. Cosmetic. |

## FFLOAT.SEQ (8087 package, rewritten on the VM float core)

**Representation.** Floats are the VM's IEEE doubles: 8 bytes, on the VM float stack (256
deep).
- The virtual stack is gone, along with `FVBOS`/`FVTOS` and all the `(nVLOAD)`/`(nVEMPTY)`
  labels.
- `FSP0` is the VM `FSP` value when the stack is empty. `FSTACK-SIZE` is 256.
- `1VLOAD` is a no-op.
- `NEEDS FASSEM.SEQ` was dropped. The few FASSEM words FFLOAT used are emulated at the top:
  - `{.}`.
  - `INITFP`.
  - `FPCW>` and `>FPCW`: a variable holding a control word, default $037F. Its RC bits *are*
    honoured by `FRNDINT`, so `FIX INT RND>+INF RND>-INF (ROUND) FP>DI FP>QI` work as before.
  - `FPSW>`: gives only the TOP field.

**Words.**
- **Built-ins used as they are.** FFLOAT does not redefine these: `F+ F- F* F/ FABS FNEGATE
  FSQRT FLOG FLN FDUP FDROP FSWAP FOVER FROT F0= F0< F= F< FMIN FMAX F@ F! FCLEAR FDEPTH (FLIT)`.
- **CODE words rewritten in Forth:** `OR! CLEARFP FEXAM PI F1.0 F0.0 F\- 1/F F2* F2/ F2**N* FLOAT
  FP>DI FP>QI QI>FP FNSWAP F-ROT FNIP FTUCK FPICK [RVS0] [RVSR] F**+N FPARSE FRNDFRC F10.0 FMUL10
  (FADDI) QNEGATE R>BCD! FIXBCD`.
  - `FEXAM` decodes the IEEE bits into the same FXAM C3/C2/C1/C0 codes, so `E.`, `F.R` and
    `F.SPECIAL` are unchanged apart from the 32-bit `INDEFINITE` test.
- **The 8087 kernels**:
  - `[SIN] [COS] [TAN] (FALN) [FALOG] FATANA` call the built-ins.
  - FFLOAT's high-level range reduction, `DEGREES`/`RADIANS` handling and argument checks
    (`FEXP` > 699, `FALOG` > 304, `FASIN`/`FACOS` range) are kept. So `FSIN FCOS FTAN FATAN FASIN
    FACOS FEXP FALOG` are redefined over the built-ins ("isn't unique" is silenced).
  - Multiples of 45 degrees still give exact 0 and 1.
- **Hyperbolics** (`(FTANH) (FSINH) (FASINH1) (FACOSH) FATANH`).
  - The 8087 used F2XM1 and FYL2XP1 here. These are now `FEXPM1` and `FLN1P` (new helper words,
    using Kahan's formulas), so they stay accurate near 0.
  - `FATANH` of ±1 gives an infinity, and |r| > 1 calls `FPERR` $10.
- **`F**`** is the built-in pow. FFLOAT's `FSWAP FLOG F* FALOG` lost digits.
- **`F**+N`** of 0 gives 1.0; the CODE returned r unchanged.
- **Number input.** `FNUMBER` keeps FFLOAT's rules: decimal base; an `E`, or `FLOATS` and an
  embedded `.`; a trailing `.` makes a double.
  - The conversion is the VM's `>FLOAT` (correctly rounded), through `(>FLOAT)`. It replaces
    `(MANTISSA) (EXP) FLOAT FALOG F*`, which rounded several times.
  - A bare `E` / `E+` / `E-` exponent still means 0, and a leading `+` is still invalid.
  - `(MANTISSA) (EXP) QCONVERT QFLOAT` are kept and work, with "q" being a 64-bit double.
- **Output.** `F. E. F.R E.R .FS` are unchanged. They work on `FLOAT-BCD`, which keeps the
  8087 packed-BCD layout: 18 digits, least significant byte first, sign byte at +9.
  - `R>BCD!` takes its digits from the VM's `(F.)`. That is C's `%g`, correctly rounded, at the
    needed number of digits (`PRECISION-VAR` is saved and restored).
  - Scaling by 10^n in double precision rounded twice and showed noise digits.
  - `E.` prints the 17 significant digits of the double, so `0.1 E.` shows
    `.10000000000000001E+00`. That is the true value of the double; the 8087 showed `...000`
    because it worked in 80 bits.
- **Doubles are 64 bits.**
  - `FLOAT`, `FIX`, `INT`, `RND>±INF`, `ROUND` and `FMAG` take or give 64-bit doubles (they
    were 32).
  - `FP>DI` now returns a double (no `SWAP` in `(ROUND)`).
  - Out-of-range conversions (|r| ≥ 2^63) are undefined, as with the built-in `F>D`.
- **Structures.**
  - `(F2.0)` and `F.25` were rewritten as REAL*8 values in 32-bit cells. (`F.25` was a broken
    temp-real anyway.)
  - `FARRAY` skips its count cell with `CELL+`.
  - `.NAMES` no longer subtracts 3 from n1. The `FPERR` callers (`FEXP`, `FACOS`, `FATANH`) pass
    a CFA for it.
- **`FLOATS` (`-- `)** keeps its FFLOAT meaning and hides the VM's ANS `FLOATS ( n -- n*8 )`.
  `FLOAT+` is still there.
- **`?FSTACK`** checks the soft `FSTACK-SIZE` limit and then calls `(?STACK)`. The VM itself
  reports an empty or full float stack ("Float stack empty"). Exception flags are a TODO-PORT
  (see Built-ins).
- **`FR`, `FFILL` and the FSAVE image** are gone. `FR.` shows the emulated control and status
  words, then each float-stack item in hex with its tag (`(TAG)` now classifies a number).
- **`<.FSTAT>`** puts `{n}` at `COLS 6 -` (it was column 74).

## FLTAUX.SEQ

The `(xxx)` words existed to read the 8087 without disturbing FFLOAT's virtual stack. There is
nothing to disturb now, so:
- They are thin wrappers on FFLOAT's words: `(FDROP)`=`FDROP`, `(FEXAM)`=`FEXAM`,
  `(R>BCD!)` = FFLOAT's `R>BCD!` with `(#BCD)` digits, copied to `FLT-BCD`, and so on.
- `NEEDS FFLOAT.SEQ` replaces `NEEDS FASSEM.SEQ`.
- `FR1` and `NPSTK` are gone. `NPU-DEPTH` is `FDEPTH`.
- `.NPUSTK` copies items with `FPICK`, deepest shown first, as before.
- `.NPUHEX` shows 8 bytes, not 10. `H.NPUSTK` addresses the VM stack (`FSP @ n 8 * +`).
- `(F.SPECIAL)` uses the 32-bit `INDEFINITE` test.
- `(FP>DI)` and `(RND>-INF)` use 64-bit doubles.

## FLTBUG.SEQ

- `NEEDS FASSEM.SEQ` was dropped.
- The help text says "floating point stack" and "emulated 8087 Control and Status words".
- Otherwise unchanged. `I` shows the emulated `FPCW>`/`FPSW>`.

## FPMATH.SEQ

Only the header was changed (port tag, and "needs FFLOAT or SFLOAT"). It runs on both.

## COMPLEX.SEQ

The file was written for SFLOAT: it uses `FPOP FPUSH FPCOPY FPSEXP` and `FDUP0<`.
- When `FPOP` is undefined (FFLOAT is loaded), a `#IF` block defines those words for IEEE
  doubles:
  - n has the SFLOAT layout: sign $8000, 15-bit exponent with bias $3FFF, sign-extended. So
    `n 1-` halves the number, and `FPSEXP 7FFF AND` compares magnitudes.
  - d holds the 53-bit significand with its leading 1, so d = 0 means zero, as in SFLOAT.
  - `FPOP`/`FPUSH` lose nothing.
- `FPERR` takes 3 arguments in FFLOAT and 2 in SFLOAT, so `Z/` calls a new `ZPERR` that adapts.
- Bug fix: `TOPOLAR` was `F2DUP FATAN FPOP ZMAG FPUSH`, which took the arctan of y instead of
  y/x and left an extra number. It is now `F2DUP FSWAP F/ FATAN ...`. It is still quadrant-naive
  (x < 0) and divides by x, like the original intent.

## DMULDIV.SEQ

`UMD/MOD ( uquad uddiv -- udmod udquot )` and `UMD* ( ud1 ud2 -- qprod )` were 8086 CODE. They
are now high level on 32-bit cells: a double is 64 bits, and a quad is four cells (128 bits) with
the most significant cell on top.
- `UMD*` uses four `UM*` partial products.
- `UMD/MOD` is restoring division, one bit per step, 64 steps. On overflow it still returns
  quotient `-1 -1` with the low double of the dividend.
- The assembler-label words were only ever inside `comment:` blocks and are untouched.

## SFLOAT.SEQ, SFLOAT1/2/3.SEQ (software floating point)

**This is still a software float with its own stack and format,** so its high-level code
works unchanged. That code adjusts exponents with `n FSP @ +!`, compares `FPSEXP` with hex
exponents, uses `FPOP ... 401F =`, and has tables of 6-byte numbers.

**Representation (unchanged in memory).** Each number is 6 bytes:
- +0: sign ($8000) and a 15-bit exponent with bias $3FFF.
- +2: the high half of the 32-bit significand.
- +4: the low half.

The significand has its point before the leading bit, and a clear leading bit means zero.
Fields are accessed with `W@`/`W!`.

Changes that follow from 32-bit cells:
- **`FPOP FPUSH FPCOPY FPFRACT FPSEXP`** still use three cells `( lo hi sx )` holding the 16-bit
  fields. lo and hi are 0..$FFFF. sx and `FPSEXP` are **sign-extended** (`SX>N`), so
  `FPSEXP 0<` and `FPOP ... 0<` still test the sign. `FPUSH` keeps the low 16 bits of each cell,
  so every hex constant (`DAA2 C90F 4001 FPUSH`) is unchanged.
- **`-400E` replaces `BFF2`** in `FEXP` and `FEXP-1`, because it is compared with the
  sign-extended `FPSEXP`.
- **`n FSP @ +!` became `n FSP @ W+!`** (21 places in SFLOAT3). A 32-bit `+!` carries into the
  significand when the exponent field wraps. For example, `-4` added to a zero with sx 0 made a
  huge number and broke `FATAN 1.0`.
- **Tables use `W,`** (`1GEXPTAB TANTAB ATANTAB`). `FCONSTANT` uses `DOES>` and keeps 3 cells.
  `FVARIABLE` is unchanged (12 bytes ≥ 6).
- **The SFLOAT `FSP` hides the VM's `FSP`** (as the SFLOAT stack always did).

**CODE words rewritten in Forth** (all 8086 code is gone; the labels `DENORM RENORM FLN+ Y**2
FRACT* GEXP1-` and the `LOGTAB1`/`LOGTAB2` tables went with it):
- **Arithmetic: `F+ F- F\- F* F/ FSQRT FLOAT TFLOAT`.**
  - The 32-bit significand is one cell. Products and quotients use `UM*`/`UM/MOD`, and the
    extended significand is a 64-bit double.
  - Rounding is to nearest even through `(RPACK)`, with a sticky bit from `(DRS)`. So results
    should match the CODE bit for bit.
  - Overflow gives the largest number and `FPERR` 2 (1 for divide by zero); underflow gives 0,
    as before.
  - `FSQRT` is an integer square root of 64 bits.
- **Stack and test words:** `FDUP FDROP F2DROP FNIP FOVER FSWAP FROT F-ROT FPICK FNSWAP F0< F0>
  F0= FDUP0< FDUP0= F= F2DUP> F2DUP< FABS FNEGATE AND! OR! F@ F!`. `F2DUP>`/`F2DUP<` leave both
  numbers, as the CODE did.
- **Conversions:**
  - `DINTABS` now gives a 64-bit double. Its flag is 0, $7FFF if it needs all 64 bits, or
    negative on overflow.
  - `TINTA` gives a triple of three 32-bit cells (96 bits).
  - `FINT`, `FNORMALIZE`, `>INTFRACT`: the integer part is a 32-bit cell (it was 16).
  - `D2**N` gives a 64-bit double. `UMT*` and `TS+` work on 32-bit cells.
  - The `TCONVERT` overflow guard became `1000000 U<` (hex).
- **`F/PREM`.**
  - The quotient is computed exactly by long division, with the CODE's unnormalized convention:
    q in the significand with exponent $401F when it fits 32 bits (GTAN1 tests `401F =`),
    otherwise its top 32 bits. The remainder is exact.
  - A zero numerator gives quotient `0 401F`.
  - A quotient over 64 bits gives r1/r2 with remainder 0 instead of an overflow error, so `F.`
    of 1E20 prints in E format as designed.
- **`(FLIT)`** reads the three in-line cells with `R>`. `FLITERAL` is unchanged.
- **`F/LN2`** is `F*` by the 48-bit constant 1/ln2 (`3B29 B8AA 4000`).
- **On the VM's native doubles**, through the helpers `SF>NF`/`NF>SF` in SFLOAT1 (HIDDEN),
  rounded back to 48 bits: `FLN` (`NF-LN`), `GEXP1` (= (e^x−1)/x via Kahan's expm1),
  `AUXTAN` (tan x − x) and `AUXATAN` (x − atan x). The error cases of `FLN` are kept. These
  helpers are compiled before SFLOAT redefines `F*` and friends, so they bind to the built-ins.
- **Removed:** `PREFIX`, `GLOBAL_REF`/`LOCAL_REF` (in SFLOAT.SEQ) and the assembler label
  machinery.
- **Search order.** Inside a colon definition `:` replaces the top of the search order, so the
  new public words that use HIDDEN helpers are compiled under `ALSO HIDDEN ALSO`.
- **`<.FSTAT>`** uses `COLS 6 -`.

**Bugs fixed in the original high-level code:**
- `FACOSH` took `sqrt(1−x²)`, which raised an FSQRT error for every valid x. It now takes
  `sqrt(x²−1)`.
- `FATANH` computed `ln((1+x)/(x−1))` and then did `1 -` on the *data* stack. It now computes
  `ln((1+x)/(1−x))` and halves with `-1 FSP @ W+!`.

**Accuracy.** SFLOAT has about 9.6 digits as before. `F.` with 10 places shows noise in the last
digit or two, as the original did.

## TODO-PORT

- FFLOAT `?FSTACK`: floating point exception flags (needs the optional `FPSW>` built-in above).
