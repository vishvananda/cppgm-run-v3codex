# PA33 Implementation Plan

## Stage Design and Spec Alignment

PA33 extends the PA32 path `semantic entity/callable IDs -> typed LowIR ABI
artifacts -> per-function MIR -> direct ELF` with host-runtime facts for vtables,
VTTs, RTTI, casts, covariant thunks, and EH. It must preserve one owner for each
selected declaration, layout, ABI entry point, and emission unit (`spec.md` §§2,
4, 6-10); names are rendered only at ABI publication, and object writing does
not recover semantic facts from strings.

## Current Failure Map

Current result: **76/94 PA33 tests pass**, improved from the 73/94 turn
baseline (and 59/94 stage-start baseline); PA1-PA32 pass (4291/4291).

| Owner / shared behavior | Count | Failing cases |
| --- | ---: | --- |
| Remaining ABI name publication | 2 | covariant layout-finalization symbols; nested-lambda owner identity |
| Remaining type/name frontend | 1 | unnamed local-class constructor |
| Host EH escape policy / RTTI | 3 | dynamic exception specification, noexcept termination, out-of-line virtual-base RTTI catch |
| Polymorphic ODR ownership | 1 | duplicate inline-header polymorphic class |
| Virtual-inheritance RTTI/casts | 3 | lazy-template cross-cast, virtual-inheritance cast-to-void, typed cross-cast |
| Virtual-base/covariant runtime layout | 5 | external vbase reference, forwarded-reference condition, self-covariant result, virtual-base return, VTT base copy |
| Static/TLS lifecycle | 3 | local static dtor, local thread-local dtor, TLS wrapper access |

## Active Checkpoint

**Queued — exception-specification escape policy.** Complete the paired
`noexcept` termination and dynamic-exception-specification `unexpected` paths
at the function-boundary EH policy owner.

- Spec alignment (§§2, 4, 6-9): canonical function facts own the fixed policy
  and allowed-type sequence; LowIR publishes an explicit escape boundary; the
  native action table lowers terminate/unexpected edges without redoing lookup
  or parsing names. Formation is O(throwing edges + allowed types), with one
  indexed policy lookup per function/call boundary.
- Validation: both PA33 policy fixtures, prior exception-specification and EH
  suites, full reports, file audit, and an increasing protected-call series.

## Performance Evidence

The generated EH transfer series remained proportional:

| Cases | Source bytes | Lexical/unwind visits | Dispatch entries | Selectors | Semantic ms | Lowering ms |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 16 | 3,496 | 16/16 | 16 | 32 | 2.02 | 0.84 |
| 32 | 6,920 | 32/32 | 32 | 64 | 3.47 | 1.57 |
| 64 | 13,768 | 64/64 | 64 | 128 | 6.84 | 2.97 |
| 128 | 27,609 | 128/128 | 128 | 256 | 13.17 | 5.66 |

Doubling cases exactly doubled cleanup work, dispatch entries, and selector
assignments; 7.90x source growth produced 6.52x semantic and 6.74x lowering
time. A runtime probe covered guarded rethrow, catch-to-break/continue, break
from a try body, and all-returning switches.

## Completed Checkpoints

| Checkpoint | Result |
| --- | --- |
| PA33 ABI-tag publication checkpoint | Pass — C1/C2, D1/D2, RTTI name/object, and vtable symbols carry canonical tags; PA33 59→62, PA1-PA32 4291/4291, file audit pass. |
| PA33 dependent callable-type recipe checkpoint | Pass — owner-prefix, qualified-member-owner, template-template, and local-result RTTI names are canonical; PA33 62→66, PA1-PA32 4291/4291, file audit pass. |
| PA33 dependent expression recipe checkpoint | Pass — alias-expanded `decltype`, template-id, unary, call, and non-type parameter expressions retain canonical ABI structure; PA33 66→68, PA1-PA32 4291/4291, file audit pass. |
| PA33 stack/SysV vararg intrinsic checkpoint | Pass — typed alloca and scalar varargs cover register-save/overflow paths with dynamic-frame restoration; PA33 68→71, PA1-PA32 4291/4291, file audit pass. |
| PA33 dependent transform/layout checkpoint | Pass — `__decay` is a semantic and retained ABI type node; ordinary alias parameters publish source recipes, and dependent class-layout constants replay after substitution; PA33 71→73, PA1-PA32 4291/4291, file audit pass. |
| PA33 EH nonlocal-transfer checkpoint | Pass — guarded rethrow owns its cleanup suffix, structured targets close crossed EH regions, and unreachable source-order jumps do not create reachable fallthrough; PA33 73→76, PA1-PA32 4291/4291, file audit pass. |
