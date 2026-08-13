# PA33 Implementation Plan

## Stage Design and Spec Alignment

PA33 extends the PA32 path `semantic entity/callable IDs -> typed LowIR ABI
artifacts -> per-function MIR -> direct ELF` with host-runtime facts for vtables,
VTTs, RTTI, casts, covariant thunks, and EH. It must preserve one owner for each
selected declaration, layout, ABI entry point, and emission unit (`spec.md` §§2,
4, 6-10); names are rendered only at ABI publication, and object writing does
not recover semantic facts from strings.

## Current Failure Map

Current result: **78/94 PA33 tests pass**, improved from the 76/94 turn
baseline (and 59/94 stage-start baseline); PA1-PA32 pass (4291/4291).

| Owner / shared behavior | Count | Failing cases |
| --- | ---: | --- |
| Remaining ABI name publication | 2 | covariant layout-finalization symbols; nested-lambda owner identity |
| Remaining type/name frontend | 1 | unnamed local-class constructor |
| Remaining host EH RTTI | 1 | out-of-line virtual-base RTTI catch |
| Polymorphic ODR ownership | 1 | duplicate inline-header polymorphic class |
| Virtual-inheritance RTTI/casts | 3 | lazy-template cross-cast, virtual-inheritance cast-to-void, typed cross-cast |
| Virtual-base/covariant runtime layout | 5 | external vbase reference, forwarded-reference condition, self-covariant result, virtual-base return, VTT base copy |
| Static/TLS lifecycle | 3 | local static dtor, local thread-local dtor, TLS wrapper access |

## Active Checkpoint

**Next — virtual-inheritance RTTI and casts.** Unify the three virtual-base
`dynamic_cast` failures and the out-of-line virtual-base RTTI catch at the
canonical class-layout/RTTI publication boundary.

- Spec alignment (§§2, 4, 6-9): canonical class entities and finalized virtual
  base paths must own one RTTI graph; EH and cast lowering consume those IDs,
  and ABI publication emits complete, coalescible host objects without
  reconstructing relationships from names.
- Owner/data flow: instantiated class/layout facts -> canonical RTTI demand and
  base graph -> typed catch/cast operands -> Itanium RTTI objects and runtime
  calls. Complexity should remain O(classes + base edges + demand sites), with
  indexed identity lookup at each use.
- Validation: all four virtual-inheritance RTTI/EH fixtures, nearby nonvirtual
  and external RTTI cases, full PA33/prior reports, file audit, and a widening
  virtual-base graph series.

## Performance Evidence

The constrained-function boundary series remained proportional:

| Functions | Tokens | LowIR instructions | EH states/edges/calls | Lowering ms | Native lower ms |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 16 | 182 | 194 | 16/32/16 | 0.30 | 0.38 |
| 32 | 358 | 386 | 32/64/32 | 0.46 | 0.66 |
| 64 | 710 | 770 | 64/128/64 | 0.64 | 1.16 |
| 128 | 1,414 | 1,538 | 128/256/128 | 1.12 | 2.16 |

Doubling functions exactly doubled EH states, edges, and protected calls; 7.77x
token growth produced 7.93x LowIR and 5.69x native-lowering time.

## Completed Checkpoints

| Checkpoint | Result |
| --- | --- |
| PA33 ABI-tag publication checkpoint | Pass — C1/C2, D1/D2, RTTI name/object, and vtable symbols carry canonical tags; PA33 59→62, PA1-PA32 4291/4291, file audit pass. |
| PA33 dependent callable-type recipe checkpoint | Pass — owner-prefix, qualified-member-owner, template-template, and local-result RTTI names are canonical; PA33 62→66, PA1-PA32 4291/4291, file audit pass. |
| PA33 dependent expression recipe checkpoint | Pass — alias-expanded `decltype`, template-id, unary, call, and non-type parameter expressions retain canonical ABI structure; PA33 66→68, PA1-PA32 4291/4291, file audit pass. |
| PA33 stack/SysV vararg intrinsic checkpoint | Pass — typed alloca and scalar varargs cover register-save/overflow paths with dynamic-frame restoration; PA33 68→71, PA1-PA32 4291/4291, file audit pass. |
| PA33 dependent transform/layout checkpoint | Pass — `__decay` is a semantic and retained ABI type node; ordinary alias parameters publish source recipes, and dependent class-layout constants replay after substitution; PA33 71→73, PA1-PA32 4291/4291, file audit pass. |
| PA33 EH nonlocal-transfer checkpoint | Pass — guarded rethrow owns its cleanup suffix, structured targets close crossed EH regions, and unreachable source-order jumps do not create reachable fallthrough; PA33 73→76, PA1-PA32 4291/4291, file audit pass. |
| PA33 exception-specification escape-policy checkpoint | Pass — canonical dynamic-spec type slices lower to LSDA filters/`unexpected`, escaping `noexcept` definitions terminate after cleanup, and boundary demand stays definition-local; PA33 76→78, PA1-PA32 4291/4291, file audit pass. |
