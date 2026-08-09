# PA21 Implementation Plan

## Stage Design and Spec Alignment

PA21 extends the canonical semantic graph with typed constexpr scalar/object/address
facts, complete invocation keys, allocation-relative subobject identity, and explicit
runtime-demand edges. Declaration checks, template arguments, `static_assert`, static
initialization, and `noexcept` consume those facts without conflating evaluation with
emission. This follows `spec.md` §§1–6 and 8–10: canonical identity, indexed lookup,
phase-owned facts, bounded traversal, demand-driven completion, and fact-driven lowering.

Function-local statics are declaration-owned static-duration actions, not automatic
slots. Their flow is syntax token range -> canonical binding/function owner -> typed
initializer/destructor fact -> global storage and optional guard -> first-use CFG. Constant
scalars/classes/addresses use static data where representable; dynamic objects use one
guarded initializer. Template-owned instances use weak ABI-stable source identity. The
lowerer retains O(1) binding-to-action/storage maps and O(initializer size) action walks,
preserving PA22+ room for richer destruction and thread-safe guard policy.

## Current Failure Map

No PA21 failures remain: 137/137 pass, improved from 120/137. The original set was grouped
under local-static ownership/guards (12), runtime class/global materialization (4), and
constexpr declaration suitability (1). The fixes now live at declaration, evaluator,
demand, ABI, static-initializer, and lowering boundaries; PA16–PA20 focused regression
reports also pass after narrowing constructor demand to empty runtime arguments and
constexpr polymorphic bases.

## Active Checkpoint

**Final verification and handoff.** The implementation checkpoint is complete. Validation
is the exact PA21 report, PA1–20 through-report, PA21 file audit, diff hygiene, and a clean
cohesive commit. Expected validation work is linear in test count; no semantic rescans are
introduced.

## Performance Evidence

- Dynamic local-static arrays of width 16/32/64/128 produced 67/99/163/291 LowIR lines,
  exactly `2N + 35`; peak RSS stayed 5,944/5,864/5,936/5,956 KiB. This confirms linear
  element lowering with fixed guard/control overhead.
- Earlier ODR-used static constexpr class arrays at width 16/32/64/128 used 33/65/129/257
  initializer visits (`2N + 1`), one dependency edge, two demand pushes, one emitted
  function, and one global.
- Reusing one compile-time-only static constant 1/2/4/8 times kept initializer visits and
  call requests at one, with zero dependency demand or helper emission.

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Integral scalar invocation | Recursive/defaulted/template calls, locals, mutation, control flow, declaration checks, canonical demand | PA1–20 clean; PA21 41/129 baseline family; linear scaling |
| Floating scalar widening | Typed literals/conversions, mixed arithmetic, typed call keys/results | PA21 42→49/130; prior stages and audit clean |
| Aggregate/array values | Structural interning, nested/string init, projection, ODR rematerialization | PA21 49→56/130; linear element scaling |
| Constructor/member invocation | Constructor frames, member initializers, receiver calls, runtime/static boundaries | PA21 56→67/130; linear field scaling |
| Canonical addresses/calls | Binding/local/string/function addresses, pointer operations, indirect calls | PA21 67→76/130; linear pointer-walk scaling |
| Class-valued conversions | Object return/call facts, temporary identity, demand separation, escape checks | PA21 76→80/131; bounded repeated-call work |
| Base-subobject completion | Ordered base facts, adjusted receiver/address identity, delegated/base initialization | PA21 80→88/133; linear inheritance-depth scaling |
| Callable/contextual conversion | Call operators, overloaded operators, arrow chains, parser rollback, cv ordering | PA21 88→101/134; bounded lookup/parser work |
| `noexcept` facts | Fold-suppressed selected calls/lifetimes, contextual bool, dependent specialization | PA21 101→109/135; linear action walks |
| Qualified static constants | Canonical recipes, ODR storage demand, typed rematerialization and dependency ownership | PA21 109→120/137; fixed repeated-use work |
| Local statics and final boundaries | Static-duration actions, guards, weak template identity, class/reference/array init, literal-type checks, vptr and empty-value materialization | PA21 120→137/137; PA16–PA20 focused reports and file audit clean |
