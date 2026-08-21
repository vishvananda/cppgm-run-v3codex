# Inliner Feedback: Why Further Policy Work Is Stalled

Date: 2026-08-21

Scope: read-only analysis of the inliner plateau recorded in
`PLAN-OPT-PASS-IMPROVEMENTS.md` through R10i (`eb77898c`).  No source,
fixture, or plan change is made by this document.  Diagnostic artifacts are
`--stats` frozen compiles at O1 and O3 plus a relocation-level call census on
the resulting objects.

## Summary

The inliner itself is no longer the constraint.  Its scheduling, budgets, and
cascade behavior were validated by R10i-a, and the R10i-b sweep proved that
raising size and budget caps alone does not help.  What blocks further
progress is that the hottest call population in the program is categorically
ineligible for inlining, and the passes that would harvest inlining's benefit
are disabled in exactly the functions that matter.  Both walls are EH-related,
and one of them is self-inflicted:

1. Destructor bodies carry provably dead `eh_cleanup` regions that make every
   destructor callee `inline_reject_callee_eh`, and the existing EH strip
   never fires on them because it requires an explicit `unwind=no` boundary
   rather than the already-computed inferred no-unwind facts.
2. Every constructor/destructor base entry is blanket-marked `no_inline` by
   `pa15_lowering_abi.cpp`, a pre-inliner workaround the census then
   mislabels as "explicit-no-inline."
3. Memory GVN, PRE, and backend edge-register planning skip EH-bearing
   functions wholesale (and O1 entirely), so inlining into those callers
   relocates memory traffic instead of removing it.  This is why the R10i-b
   broader-policy sweep measured no runtime win; that null result was
   measuring the wrong variable and should not be treated as final evidence
   that broader inlining is unprofitable.

The R10 series has been optimizing around this wall (called-once cascades,
budget sweeps, recursive censuses) rather than through it.

## Evidence

### Half of all remaining calls are constructor/destructor calls

A relocation census of the frozen O3 object finds 13,410 static calls, of
which 6,861 (51%) target constructor or destructor entries.  The single
most-called body is `~basic_string()` with 1,586 call sites, followed by
`~shared_ptr<cpp_decl::Type>` (742) and `~ExprInfo` (646).  GCC's entire
`basic_string` call family at O1 is about 886 calls; one cppgm++ destructor
exceeds it.  `_Unwind_Resume` remains at 517 calls versus GCC 273 / Clang 213.

This population is invisible to the plan's retained-definition census:
`~basic_string` is one discardable definition there, but 1,586 call sites at
runtime.  The census metric systematically hides multi-use hot callees.

### The rejections that block those calls do not hold up

The top O3 rejection classes are `inline_reject_callee_eh` (4,487),
`inline_reject_landing` (3,592), and `inline_reject_no_inline` (2,337).

**Dead EH regions in destructors.**  The `--emit-lowir` body of
`~basic_string` D1 is three blocks: an `eh_cleanup` region guarding a single
call to `_M_dispose`, a landing block calling the empty `~_Alloc_hider`, and
a return.  The guarded chain bottoms out at `operator delete`, which is
already declared `unwind=no` in LowIR, so the existing bottom-up
`infer_no_unwind` analysis (`dev/src/lowir_inline_o1.cpp:554`) can prove the
region can never be entered.  But:

- `strip_explicit_no_unwind_eh` (`dev/src/lowir_inline_o1.cpp:778`) only
  strips EH when the function's own serialized boundary is `unwind=no`; the
  inferred facts feed inline eligibility only, never region removal.
- `infer_no_unwind` conservatively marks any function *containing* EH as
  unsafe, so `~basic_string` can never be inferred nonthrowing while its dead
  region exists, the region is never stripped, and every one of its 1,586
  calls is rejected as `callee_eh`.  The containment test and the strip gate
  form a cycle that no current pass breaks.

This one pattern — destructor bodies wrapping member-cleanup regions around
nonthrowing calls — plausibly accounts for most of the 4,487 `callee_eh`
rejections and the 299 EH-bearing discardable definitions.  The upstream
cause is that the frontend does not implement the C++11 implicit-`noexcept`
destructor rule, so every destructor gets full cleanup regions; GCC emits
`~basic_string` with no EH at all.

**The 317 "explicit-no-inline" definitions are not explicit.**  The frozen
source contains zero `noinline` attributes.  They come from
`dev/src/pa15_lowering_abi.cpp:110`, which blanket-marks every
constructor/destructor base entry `no_inline` during host object emission.
The line predates the entire inliner effort (introduced by `f3ac28a4`,
preserved by `1085fcfb`) and conflates "the C2/D2 definition must be retained
for the alias contract" — already guaranteed by object roots and
reachability — with "call sites must not inline it."  Probe LowIR shows base
entries carrying `force_inline=yes, no_inline=yes` simultaneously, and
`no_inline` wins (`dev/src/lowir_inline_o1.cpp:631`).  The R10i census tables
describe these as explicit-no-inline definitions, which closed off the
investigation.

**Landing rejections are mostly cleanup-chain destructor calls.**  The 3,592
`landing` rejections are member-destructor calls inside cleanup blocks —
exactly the calls that would collapse cleanup chains (the empty
`~_Alloc_hider` in `~basic_string`'s landing block).  Once dead regions are
stripped, most of this population disappears with the blocks that contain it.

### Successful inlining cannot be harvested where it lands

- Memory GVN and PRE skip whole functions containing any EH instruction
  (`exceptional_function`, `dev/src/lowir_memory_gvn.cpp:637`); 610 functions
  are skipped at O3.  `licm_loads_hoisted=0` and `pre_inserted_expressions=0`
  on the frozen workload: the memory-value passes are effectively inert on
  real code.
- Backend edge-register retention returns false for
  `optimization_level < 2 || function_has_eh`
  (`dev/src/lowir_native_location_planning.cpp:136`).  Only 439 edge values
  program-wide keep a register at O3; zero at O1.  Call-crossing XMM values
  always take the frame path.
- Of 119,641 MIR instructions at O3, roughly 72,000 are data movement:
  30,173 scalar-temporary loads/stores (more than the 24,944 at O1 — inlining
  creates temporaries that all get frame homes), 23,623 call-boundary moves,
  and 16,695 address materializations.
- At O1 there is no slot-to-phi promotion, no memory GVN, no PRE, no
  small-object promotion, and no location planning at all
  (`promote_slot_runs=0`, `memory_gvn_runs=0`, `pre_runs=0`), yet the recent
  R10 acceptance measurements are exact O1-built self compilers.

The Rank 8 profile already said this (`Lexer::Peek`: 617 bytes with a
144-byte frame versus GCC's 259), and it explains the R10i-b result: inlining
a callee into a frame-bound, GVN-skipped caller relocates memory traffic
instead of removing it.

## What is missing, in leverage order

1. **Region-granular EH stripping from inferred no-unwind facts.**  Delete
   any `eh_cleanup`/`eh_try` region whose guarded instructions are all
   provably no-unwind, inside the same callee-first cleanup-coupled traversal
   R10i-a built, republishing shape facts before callers are visited.  This
   breaks the containment cycle and cascades: `~basic_string` becomes an
   EH-free body of about six instructions — inlinable at all 1,586 sites
   under the existing 6/128 policy — after which its callers (`~ExprInfo`,
   `~shared_ptr`) become inferrably nonthrowing in turn.  No new analysis is
   required; the `no_unwind_` bitmap already contains everything needed.
2. **Delete the blanket base-entry `no_inline`**
   (`dev/src/pa15_lowering_abi.cpp:110`) and verify retention still holds via
   the existing root machinery.  This is the same "linkage fact is not an
   inlining fact" split R10a and R10b already performed for other
   conflations.
3. **Allow inlining EH-free callees in landing blocks**, so cleanup chains of
   empty destructors collapse instead of remaining opaque calls.
4. **Lift the whole-function EH skips** in memory GVN/PRE and the
   `function_has_eh` test in location planning — region-aware, or simply
   remeasured after stripping shrinks the EH population — and reconsider what
   value work O1 gets, since O1 currently has none.
5. **Re-run the R10i-b policy sweep and the retained census after 1–4.**
   Both were conducted against the EH wall; the sweep's rejection of broader
   budgets and the census's category labels are artifacts of that wall.
   Adopt a call-site-frequency-weighted census (or a perf profile of the
   generated compiler) as the acceptance signal alongside the definition
   census.
6. **Only then evaluate true EH-region grafting** — inlining callees whose
   cleanup regions are genuinely live — because the residual population may
   be small once dead regions are gone.

The implicit-noexcept-destructor frontend rule is the semantically right
upstream fix, but it interacts with the terminate-boundary machinery (which
would add `eh_try` wrappers around newly noexcept bodies).  The
optimizer-side strip in item 1 achieves the same result on this workload
without touching frontend exception semantics, so it should be sequenced
first.

## Reproduction

```sh
cd ~/cppgm-extended-pa39-source-layout
dev/cppgm++ -std=gnu++11 -O3 --stats -I dev/src -c \
  benchmarks/self_compile/stable/semantic_overload.cpp -o frozen-o3.o
# rejection and skip counters: inline_reject_*, memory_gvn_eh_skips,
# planned_edge_register_retains, movement_*_instructions

objdump -dr frozen-o3.o | grep -A1 call | grep R_X86_64 | \
  awk '{print $NF}' | sed 's/[-+].*//' | sort | uniq -c | sort -rn | head
# call census; ctor/dtor entries match (C1|C2|D1|D2)E

dev/cppgm++ --emit-lowir -O1 probe.cpp -o probe.lowir
# probe.cpp: a function constructing and destroying a std::string; shows the
# dead cleanup region in ~basic_string D1 and force_inline=yes,no_inline=yes
# on constructor base entries
```
