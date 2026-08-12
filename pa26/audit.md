# PA26 Audit

## Current Checkpoint Review

The class exception-object checkpoint (`336f0c80` plus this audit follow-up)
passes its bounded architecture review. The reviewed increment is selected
class copy/move construction into runtime exception storage, destructor
transfer to `__cxa_throw`, canonical RTTI/typed-handler routing, polymorphic
copy chains, and retirement of direct throw-statement temporaries. Remaining
conditional/logical cleanup failures are the next checkpoint, not part of this
landed increment.

The audit removed one checkpoint-owned semantic shortcut. Throw analysis was
changing synthesized copy/move constructors of polymorphic classes from
trivial to nontrivial and rebuilding their storage prefixes only when such a
class was thrown. Constructor triviality now becomes false at its canonical
class-special-member owner, after polymorphism is complete and before ABI and
construction facts are published. Throw analysis only consumes the selected
constructor and destructor bindings. This also exposed and fixed a PA16
boundary bug: a projected derived-to-base reference is lowered as a projection
before the call-result ABI path is considered, and only an actual typed call
node may enter indirect-result lowering.

The correctness witness was `base copy(derived)` for a polymorphic base. Before
the repair it emitted `copyobj`, incorrectly retaining the derived vptr in the
new base object; afterward it calls the selected base copy constructor, whose
typed body installs the base vptr. The complete durable path is class
polymorphism -> PA12 class-special-member facts and construction action ->
throw-owned `TypeId`/constructor/destructor identities and cleanup suffix ->
PA18 demanded RTTI/runtime symbols -> per-function PA16/PA17/PA26 typed LowIR.
No stage uses throw-triggered fact mutation, lookup recovery, rendered text as
semantic identity, test/source-name branching, external compilation, or a
reference-binary fallback.

Representative current-binary runs with 16/32/64/128 typed class throws and
one live guard per handler recorded 371/675/1,283/2,499 semantic nodes,
148/276/532/1,044 blocks, 799/1,471/2,815/5,503 instructions, and
169,362/297,474/553,698/1,066,220 typed bytes. Special-member subobject visits
stayed at 10 and demand pushes/emissions at 10/9 for every size. Semantic and
lowering time grew from 1.08/0.62 ms to 5.69/2.60 ms, supporting linear work in
source throw sites and produced LowIR with constructor demand computed once.

Final validation preserves both baselines: the six focused checkpoint fixtures
pass, the PA18 projection regression passes, PA1-PA25 pass 3,607/3,607, and
PA26 remains 86/110 with the same 24 failures. The required PA26 report was run
and preserves that checkpoint baseline; the file audit passes with the same 19
inherited division warnings. No relevant file-audit, timeout, shortcut,
correctness, or performance issue remains in this checkpoint's ownership path.

## Checkpoint Audit Ledger

| Checkpoint | Audit result |
|---|---|
| Canonical RTTI demand and query lowering (`9eb277da`, audit follow-up) | Pass: evaluated demand, reachable collection, complete canonical RTTI categories, cast legality, linear counters, baseline and earlier stages preserved. |
| Lexical unwind snapshots and handler continuation (`e05062b1`, audit follow-up) | Pass: complete bounded owners, ordered handler exit, non-duplicated typed suffixes, linear counters, and both baselines preserved. |
| Class exception objects and typed-handler routing (`336f0c80`, audit follow-up) | Pass: canonical special-member facts, direct typed construction/destructor transfer, projection-safe call ABI, linear evidence, and both baselines preserved. |
