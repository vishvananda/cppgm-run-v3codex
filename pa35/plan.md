# PA35 Plan

## Stage Design and Spec Alignment

PA35 keeps source -> streaming preprocessing/post-tokenization -> integrated
syntax/semantics -> typed LowIR -> native ELF. Relevant `spec.md` requirements
are canonical identity and phase flow (§§2, 6), demand-owned specialization
(§4), and bounded, observable heavy-header work (§9). Retained syntax is shared;
concrete type, lookup, conversion, and lowering facts belong to their canonical
owner in the shared front end.

## Current Failure Map

PA35 is 116/128 with 12 failures. The complete set groups by shared owner and
diagnostic: retained-call viability 4; hosted vector builtin lookup 2;
list/constructor selection 2; local/class lookup and lowering 2; bound storage
lowering 1; and reactive register allocation 1.

## Active Checkpoint

**Specialization-owned retained-call replay.** Per `spec.md` §§2, 4-6, retained
syntax is shared but lookup and conversion facts are owned by the active
specialization. Data flows retained callee NodeId -> recorded function/template
set -> active owner and substituted arguments -> validated/rebuilt candidates ->
overload result and demand. PA19 retained-template validation owns fact reuse;
PA12 call analysis consumes the canonical candidate view. Expected work is
O(retained candidates + indexed active-owner candidates + deduction depth), with
rebuilds cached per callee/owner specialization and no global declaration scan.
Validate all four retained-call failures, owner-reentry and genuine no-viable
negatives, 8/16 replayed specialization calls, PA35, PA1-34, and audit.

## Performance Evidence

Eight/sixteen nested unevaluated calls used 8/16 indexed candidates, 16/32
overload candidates, 44/92 conversion checks, 16/32 specialization requests,
and 8/16 cache hits. Demand-worklist pushes and function emissions stayed zero.
Peak stage storage was 203,057/342,069 bytes; five-run median analysis was
1.46/2.48 ms and elapsed time 2.40/4.01 ms, showing bounded, near-linear
signature resolution without body demand.

## Completed Checkpoints

| Checkpoint | Result | Validation |
|---|---|---|
| Hosted front-end ingress | PA35 6 -> 15/103 | focused utility/probes; PA1-34 and audit pass |
| Hosted attributed/template syntax | shared syntax barriers removed; PA35 15/103 | decltype-shift; parser scaling linear |
| Canonical using-function merge | 35 duplicate-import barriers removed; PA35 15/103 | repeated/distinct imports; scaling linear |
| Hosted class-template registration | 49 registration barriers removed; PA35 15/103 | attributed partial identity; scaling linear |
| Dependent nested-type lookup | prior 55-case barrier removed; PA35 15 -> 17/103 | focused shape replay and allocator cases advance |
| Dependent call/type disambiguation | dependent calls remain calls; scalar cast set completed | PA35 17 -> 18/103; qualified-base regressions pass |
| Explicit-instantiation operator identity | 36 routing barriers removed; PA35 18/103 | operator and genuine template-id targets distinct |
| Constexpr declaration/completion | 16 owner-literal barriers removed; PA35 18/103 | member/base/object negatives remain rejected |
| Retained template-default access | 19 access barriers removed; PA35 18/103 | member/friend defaults; external private alias rejected |
| Canonical retained declarations | tag/control/function/exception facts merge; PA35 18 -> 19/103 | 16 barriers advance; PA1-34, audit, scaling pass |
| Qualified nested-member replay | canonical current specialization, definition parameters, retained operator call, constexpr string array, and local-class access | PA35 19 -> 21/103; PA1-34 4756/4756; audit/focused/scaling pass |
| Demand-safe declarations and function-local lowering | incomplete parameter ABI deferred; nested cleanup bounded; shared node/binding slots reset per function; direct class calls retained as objects | PA35 21 -> 36/103; PA1-34 4756/4756; focused/scaling/audit pass |
| Target-typed casts and bounded hosted shapes | braced casts, specialization-local constexpr, symbolic packs, runtime atomic order, and cv-overloaded special members | PA35 36 -> 41/103; PA1-34 4756/4756; behavior/scaling/audit pass |
| Retained current-class ownership | canonical owner/member view, enclosing packs, and member-template result calls; audited pack facts use one direct/per-scope index | PA35 41 -> 48/103 compile (48/104 tracked); PA1-34 4756/4756; retained-pack probes linear; file audit pass |
| Mandatory static-assert evaluation roots | assertion-local suppression, compact provenance with error-only rendering, and compile-path constexpr counters | PA35 48 -> 53/103; five pass and 16 advance; PA1-34 4756/4756; focused/scaling/audit pass |
| Canonical completed-type trait demand | complete canonical type-edge readiness; specialization-owned member access; lazy defaulted-destructor nonthrowing/boundary facts | PA35 handout 53 -> 60/103 plus composite course regression; all 13 barriers advance; PA1-34 4756/4756; scaling/file audit pass |
| Canonical qualified/inherited type identity | selected partial names, enclosing packs, identity-only friends, and function references keep canonical ownership | PA35 handout 61 -> 64/104 (65/105 total); 13 barriers removed/advanced; PA1-34 4756/4756; scaling/audit pass |
| Canonical retained exception equivalence | special-member kind, structural overload shape, ordinal parameter mapping, and deferred exception state select one retained declaration | PA35 65/105 -> 68/107; eight barriers removed, one handout plus two regressions pass; PA1-34 4756/4756; scaling/audit pass |
| Parameter-owned exception evaluation | declarators carry parameter/`this` scope into ordinary and deferred `noexcept` evaluation | five handout cases advance from unknown names to incomplete construction; direct positive/negative probes and 8/16 scaling pass |
| Demand-safe unevaluated construction | member typing skips constexpr address/layout work in unevaluated operands; dependent function-template specs rebuild canonical parameter scopes | PA35 68/107 -> 70/108 (one handout plus one regression); four cases advance; PA1-34 4756/4756; scaling/audit pass |
| Canonical specialization completion re-entry | synthetic initializer-list layout and declaration replay remain distinct; only canonical in-progress replay crosses the duplicate guard | PA35 70/108 -> 73/109 (two handout plus one regression); PA1-34 4756/4756; 8/16 scaling and audit pass |
| Canonical explicit class target routing | distinct `_Float128`/`__float128` identities and generic type/value/template argument routing remove all seven barriers | PA35 73/109 -> 77/111 (75/109 existing); two handouts pass, five advance, two regressions pass; PA1-34 4756/4756; scaling/audit pass |
| Retained class/declaration convergence | injected class tags merge through the class-tag index; stale specialization-owned call facts rebuild in the active scope | PA35 77/111 -> 82/113 (80/111 existing); map/codecvt/wide-string and two regressions pass; PA1-34 4756/4756; scaling/audit pass |
| Specialization-local construction convergence | class references remain references; static downcasts complete concrete targets; fixed cv-reference patterns order over forwarding packs; direct-member calls expand packs | PA35 82/113 -> 86/117; six construction and three pack-call barriers advance, four regressions pass; PA1-34 4756/4756; scaling/audit pass |
| Explicit-id pack partition convergence | explicit function-template prefixes wait for argument-aware deduction, preserving canonical trailing-pack partitions | PA35 86/117 -> 91/119; three handout and two regressions pass; direct piecewise construction advances to native transport; PA1-34 4756/4756; scaling/audit pass |
| Native scalar aggregate transport | object copy/load/store values stay address-backed until existing ABI call/return chunking; MIR uses bounded byte copies | PA35 91/119 -> 98/120; six handout plus one regression pass; 1/16-byte executable probe, PA1-34 4756/4756, linear MIR scaling, and audit pass |
| Empty variadic-tail ownership | fixed-only variadic invocations bind a zero-length slice at the raw-token end instead of indexing an absent parsed range | PA35 98/120 -> 99/121; two regex crashes advance under ASan to retained `_CharT`; direct regression and 8/16 scaling pass; PA1-34/audit pass |
| Template-argument function-type disambiguation | `bool(T)` retains a function type for an unadorned active type parameter while non-type and qualified-value forms remain expressions | PA35 99/121 -> 102/123; regex iterator passes and member-call advances; positive/negative plus PA24 guards, 8/16 scaling, PA1-34 4756/4756, and audit pass |
| Canonical function-specialization request ownership | result-type re-entry is retryable substitution failure; successful specialization publishes once | PA35 102/123 -> 105/124; four crashes removed, two handouts plus one regression pass and two advance; 8/16 scaling, PA1-34 4756/4756, and audit pass |
| Reference-preserving traits and conversions | direct traits retain reference wrappers; reference casts/ties, transitive anonymous aliases, and class-array member actions use canonical destinations | PA35 105/124 -> 111/126; four handouts plus two regressions pass, regex advances; 8/16 scaling, PA1-34 4756/4756, and audit pass |
| Unevaluated retained-call demand | pending nested calls remain signature-only in `decltype`/`noexcept`; dynamic `typeid` still promotes its operand after evaluatedness is known | PA35 111/126 -> 116/128; three handouts plus positive/negative regressions pass; zero 8/16 body demand, PA1-34 4756/4756, and audit pass |
