# PA34 Final Audit

## Findings

The PA-wide audit covered `spec.md`, the PA34 contract, all 30 stage commits,
the 124-file stage diff, the current architecture plan, relevant tests, and the
full primary test log. One architecture/performance defect was found:

- Source compilation performed always-inline expansion after adapting canonical
  typed IDs into string-keyed native LowIR. The pass copied the entire program
  before proving work existed, and the native session invoked it a second time.
  This introduced duplicate ownership, name reconstruction, and avoidable
  whole-program work on the ordinary source-to-object path.

No additional correctness, architecture, performance, self-containment, or
file-audit blocker remained after the repair. Static review found no test-name,
source-path, reference-binary, host-compiler, assembler, or textual transport
fallback in runtime compilation. The build-time hosted compiler probe is the
PA34-contract configuration step and does not participate in source output.

## Changes

- Added `pa15_force_inline`, a typed-LowIR transform keyed by dense `SymbolId`,
  `TempId`, `SlotId`, and `BlockId` identities.
- Added Tarjan SCC classification so direct and mutual recursion remain calls,
  while acyclic dependencies expand in dependency-first order.
- Preserved call operands/references, switch slices, multi-return CFG,
  continuation blocks, local slots, and presentation-only unique labels without
  cloning the complete program.
- Moved the transform before native adaptation and removed the driver-level
  string-keyed rewrite. `--emit-lowir` and `-c` now consume the same rewritten
  typed program.
- Added counters for candidates, recursive candidates, call probes, expanded
  calls, added blocks, and cloned instructions.
- Kept the textual-LowIR compatibility adapter for explicit LowIR tools, but it
  now proves that a forced definition exists before allocating a program copy.

## Representative End-to-End Traces

An attributed function follows syntax attribute recognition -> canonical
`BindingRecord::force_inline` -> typed `Symbol::force_inline` -> typed-ID SCC and
CFG expansion -> one native adaptation -> per-function MIR lowering/encoding ->
ELF `.text`, symbols, and relocations. A control-flow probe with nested calls,
branches, a switch, multiple returns, and recursion produced 184 LowIR and 319
MIR instructions, an 89,624-byte ELF64 relocatable, and a successful linked run.
Three candidates caused three expansions; one recursive candidate remained a
call. The rendered LowIR had the same result and no consumed `force_inline`
metadata.

A demanded `read_box<N>` function template follows one retained parsed pattern
-> canonical `(pattern, argument, partition)` key -> specialization request
state/cache -> one demand worklist entry -> one semantic function emission ->
typed symbol/function -> MIR -> ELF. At width 8, eight demands emitted eight
weak Itanium-ABI symbols; repeated calls hit the specialization cache and ELF
contains two relocations to each specialization.

## Performance Evidence

| Workload | Evidence |
| --- | --- |
| Preprocess 1/8/64 | 82/208/1,216 post-tokens; 22/148/1,156 expansions; peak rescan 18/42/46; 2.325/2.189/2.640 ms; stable ~7 MiB RSS |
| Templates 1/8/64 | 10/80/640 requests; 8/64/512 hits; exactly 1/8/64 demand pushes and emissions; 44/233/1,745 nodes; 0.683/1.745/11.670 ms semantic time |
| Typed inline 1/8/64 | 1/8/64 candidates and calls; 36/246/1,926 probes; 3/24/192 new blocks; 20/139/1,091 final instructions; 0.250/0.609/4.554 ms lowering plus adaptation |
| Inline 64 before/after | 10.789 -> 4.554 ms lowering plus adaptation (57.8% lower); 18,020 -> 13,332 KiB RSS (26.0% lower) |
| No-inline 64 | Zero candidates/probes/calls/blocks/clones; 1.749 ms lowering plus adaptation; 10,144 KiB RSS |

The counters explain all observed growth: preprocessing follows produced
expansions, template work is constant per demanded specialization, and inline
work follows input plus explicitly expanded CFG. No unexplained superlinear
path remained.

## Validation

- Complex typed-inline compile/link/run probes: pass; direct and mutual
  recursive calls preserved (2/2 mutual candidates classified, zero expansion).
- `--emit-lowir` representativeness probe: pass.
- `make -C pa34 check TEST=tests/run/800-always-inline-codegen-run.t`: 1/1.
- `make test-pa34`: 369/369.
- `perl scripts/cppgm_file_audit.pl --stage pa34 --paths dev/src`: pass with
  22 inherited nonfatal header-division warnings.
- `make test-report-through-pa34`: 4,756/4,756 tests and 34/34 tracked stages.
- `git diff --check`: pass.

## Checkpoint Audit Ledger

| Stage commits | Audit disposition |
| --- | --- |
| `17cba749`–`12f77862` | Hosted preprocessing, queries, and annotations pass ownership, streaming, and registry review. |
| `256ec8ed`–`6e49ff4c` | Integer/memory/atomic/floating builtins pass typed semantic/effect/lowering review. |
| `1534ea89`–`71c6a55f` | Layout/asm/vector/block-pointer surfaces pass identity, ABI, and structured-lowering review. |
| `db73c970`–`91a7f96f` | Lambda/numeric/complex surfaces pass retained-demand, canonical-type, and ABI review. |
| `9ac4482f`–`dc9a6846` | Trait surfaces pass after the landed assignment-owner audit; specialization caches and shared overload facts are retained. |
| `e3a3bb7c`–`9b251086` | Pack, aggregate, and compiler-function surfaces pass bounded replay and typed lowering review. |
| `92d88ba6`–`e733c57c` | Hosted runtime/configuration/declarations pass canonical declaration and self-containment review. |
| `c6e310d3`–`2dd03fc5` | Selection, type formation, and dependent callable replay pass demand/state/key review. |
| `91d55cb8`–`c73800f6` | Declaration/wide/runtime actions pass after moving force-inline ownership to typed LowIR. |
| `c6db9b6d` | Source/ABI identity passes canonical owner and presentation-only rendering review. |
| Final PA34 audit | Pass: one blocker closed; file audit and cumulative functional gates clean. |
