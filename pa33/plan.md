# PA33 Implementation Plan

## Stage Design and Spec Alignment

PA33 extends the PA32 path `semantic entity/callable IDs -> typed LowIR ABI
artifacts -> per-function MIR -> direct ELF` with host-runtime facts for vtables,
VTTs, RTTI, casts, covariant thunks, and EH. It must preserve one owner for each
selected declaration, layout, ABI entry point, and emission unit (`spec.md` §§2,
4, 6-10); names are rendered only at ABI publication, and object writing does
not recover semantic facts from strings.

## Current Failure Map

Current result: **85/94 PA33 tests pass**, improved from the 78/94 turn
baseline (and 59/94 stage-start baseline); PA1-PA32 pass (4291/4291).

| Owner / shared behavior | Count | Failing cases |
| --- | ---: | --- |
| Finalized covariant/VTT layout | 3 | covariant layout-finalization symbols, self-covariant adjustment, VTT base copy |
| Remaining ABI name publication | 1 | nested-lambda owner identity |
| Remaining type/name frontend | 1 | unnamed local-class constructor |
| Polymorphic ODR ownership | 1 | duplicate inline-header polymorphic class |
| Static/TLS lifecycle | 3 | local static dtor, local thread-local dtor, TLS wrapper access |

## Active Checkpoint

**Next — finalized covariant/VTT layout.** Unify the remaining covariant symbol,
return-adjustment, and VTT copy failures at the finalized class-layout boundary.

- Spec alignment (§§2, 4, 6-9): canonical entities and final overriders own
  covariant paths, layout owns concrete offsets, and publication consumes those
  facts for one thunk/vtable/VTT entry without reparsing ABI names.
- Owner/data flow: class completion + base paths -> final overrider/covariant
  result path -> finalized this/result adjustments -> thunk and vtable
  publication; constructor mode + base edge -> VTT slice -> canonical C1/C2
  copy actions. Indexed lookups remain O(1), and total work is O(classes + base
  edges + virtual slots + VTT entries), with one emitted thunk per adjustment
  key.
- Validation: the three grouped fixtures, nearby covariance and virtual-base
  lifecycle tests, full PA33/prior reports, file audit, and a widening/deep
  inheritance sample.

## Performance Evidence

The virtual-base widening series remained proportional:

| Virtual bases | Tokens | Layout visits/facts | RTTI edges | Vtable rows | Semantic ms | Lowering ms |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 4 | 103 | 4/4 | 4 | 12 | 0.62 | 0.61 |
| 8 | 163 | 8/8 | 8 | 24 | 0.81 | 0.92 |
| 16 | 283 | 16/16 | 16 | 48 | 1.15 | 1.44 |
| 32 | 523 | 32/32 | 32 | 96 | 2.03 | 2.74 |

An 8x wider graph produced exactly 8x layout visits, RTTI dependency edges,
and vtable rows; semantic and lowering time grew 3.27x and 4.49x.

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
| PA33 virtual-inheritance RTTI/cast checkpoint | Pass — host-object VMI prefix rows, nonlinear casts, polymorphic assignment, forwarded virtual-base arguments, and explicit-instantiation constructor ownership are canonical; PA33 78→85, PA1-PA32 4291/4291, file audit pass. |
