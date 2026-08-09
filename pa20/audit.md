# PA20 Final Audit

## Findings

The audit started from clean commit `40fc166d`, the reported 2,177/2,177 and
20/20 baseline, and a passing reused file audit. The complete specification,
assignment contract, twelve stage commits, cumulative source delta, tests,
current plan, and primary log were reviewed independently. Four findings were
hidden by the clean functional baseline:

1. PA10 discarded the phase-7 type and decoded value of scalar literals. PA12
   reparsed presentation spelling and treated unsuffixed integers as suffix-only
   types. This miscompiled `0xffffffff`, `2147483648`, and translated wide
   character values.
2. Integral constant evaluation used an incomplete mixed signed/unsigned
   conversion rule, eagerly folded unselected logical/conditional operands, and
   accepted signed add/subtract/multiply/divide/modulo/unary/shift overflow.
3. Five dependent qualified class/value paths reparsed rendered payload text
   even though PA19 had introduced structured name identities.
4. The fixes pushed `pa10_syntax.cpp` and `pa12_semantic.cpp` over the
   required 3,000-line ownership limit.

All findings are closed. Fresh scale counters found no performance blocker.

## Changes

- `SyntaxToken` remains 8 bytes and now packs a 24-bit scalar-fact index beside
  its token kind. A dense 16-byte `SyntaxLiteralFact` retains the phase-7
  `FundamentalType`, decoded value, and validity; PA10 literal nodes borrow
  that fact by compact ID. PA12 maps it directly to the canonical semantic type
  and value. String and user-defined literal paths retain their existing syntax
  behavior.
- Usual arithmetic conversions now follow rank, signedness, representability,
  and unsigned-counterpart rules. Constant folding rejects signed overflow and
  invalid shifts at the operand width while preserving unsigned wrap.
  Logical/conditional analysis still type-checks both operands but suppresses
  constant evaluation and constexpr-call interpretation in the unselected arm.
- Decltype-qualified value/type syntax now retains structured interned name
  components. Class-template declaration/member replay and PA20 argument lookup
  consume those IDs instead of reparsing payload text.
- Unary/binary operator analysis moved to
  `pa12_semantic_operators.cpp`, registered in the compiler source set.
  `pa10_syntax.cpp` is 2,991 lines and `pa12_semantic.cpp` is 2,750 lines.
- Eight course regressions cover retained literal typing/value, mixed
  conversions, short-circuit selection, and every repaired signed-overflow
  family.

## Performance Evidence

| Probe | Fresh seven-run evidence |
|---|---|
| 128/256/512 integral assertions | nodes 1,669/3,333/6,661; peak 551,252/1,088,724/2,165,716 bytes; semantic 2.561/5.072/10.145 ms |
| 16/32/64 specialization keys called twice | requests 128/256/512; hits 96/192/384; demands 16/32/64; peak 379,669/753,245/1,500,397 bytes; semantic 2.006/3.693/7.273 ms |
| 16/32/64 type-pack relay | nodes 79/143/271; lookups 67/115/211; output 3,921/7,609/14,985 bytes; semantic 0.544/0.781/1.198 ms |

Every measured counter is fixed or proportional to input/output. No unexplained
superlinear path remained, so no sampling profile was required.

## Validation

- `make test-pa20`: pass, 164/164 handout and 8/8 course audit tests.
- `perl scripts/cppgm_file_audit.pl --stage pa20 --paths dev/src`: pass; no
  fatal findings (advisories are recorded in the plan).
- `make test-report-through-pa20`: pass, 2,185/2,185 tests and 20/20 stages.
- Focused host comparison confirmed the two literal-selection failures before
  repair; all eight accept/reject regressions pass after repair.
- Source scan: no compiler host/reference invocation, cached answer,
  test/ref-name branch, textual LowIR transport, or whole-program retry.

## Checkpoint Audit Ledger

| Checkpoint group | Audit disposition | Evidence |
|---|---|---|
| `c74ce9d5` constant assertions | Pass after repair | retained typed literals, complete conversion/selection/overflow checks |
| `c7783d8d` integral NTTPs | Pass after repair | canonical typed value arguments and width normalization |
| `6d3d2a75`-`80cef651` packs | Pass | canonical offsets, overlay element scopes, linear relay evidence |
| `e744d35c` base packs | Pass | ordered identities and typed base layout/lowering |
| `d8e5ca44`-`dee259a3` dependent facts | Pass after repair | structured decltype-qualified identity, no PA20 payload reparse |
| `c8745d36` specialized demand | Pass | stable retained ownership and monotonic deduplicated worklists |
| `10b67478` literals | Pass after repair | phase-7 scalar fact ownership joins retained literal dispatch |
| `7f74da10` target conversion | Pass | selected callable identity reaches typed LowIR |
| `40fc166d` closure | Pass after repair | explicit/late specialization retained; file ownership restored |
| Final PA-wide audit | Pass | focused tests, file audit, and 2,185-test through-stage report pass |
