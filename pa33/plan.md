# PA33 Implementation Plan

## Stage Design and Spec Alignment

PA33 extends the PA32 path `semantic entity/callable IDs -> typed LowIR ABI
artifacts -> per-function MIR -> direct ELF` with host-runtime facts for vtables,
VTTs, RTTI, casts, covariant thunks, and EH. It must preserve one owner for each
selected declaration, layout, ABI entry point, and emission unit (`spec.md` §§2,
4, 6-10); names are rendered only at ABI publication, and object writing does
not recover semantic facts from strings.

## Current Failure Map

Current result: **73/94 PA33 tests pass**, improved from the 71/94 turn
baseline (and 59/94 stage-start baseline); PA1-PA32 pass (4291/4291).

| Owner / shared behavior | Count | Failing cases |
| --- | ---: | --- |
| Remaining ABI name publication | 2 | covariant layout-finalization symbols; nested-lambda owner identity |
| Remaining type/name frontend | 1 | unnamed local-class constructor |
| Host EH semantic/lowering regions | 6 | dynamic exception specification, noexcept termination, rethrow outer cleanup, two switch/catch cases, out-of-line virtual-base RTTI catch |
| Polymorphic ODR ownership | 1 | duplicate inline-header polymorphic class |
| Virtual-inheritance RTTI/casts | 3 | lazy-template cross-cast, virtual-inheritance cast-to-void, typed cross-cast |
| Virtual-base/covariant runtime layout | 5 | external vbase reference, forwarded-reference condition, self-covariant result, virtual-base return, VTT base copy |
| Static/TLS lifecycle | 3 | local static dtor, local thread-local dtor, TLS wrapper access |

## Active Checkpoint

**Queued — host EH dispatch and cleanup regions.** Group the six remaining EH
failures at the semantic-action/LowIR-region boundary: typed and repeated-base
catch selection, switch exits, rethrow cleanup, termination policy, and
out-of-line virtual-base RTTI ownership.

- Spec alignment (§§4, 6-9): semantic EH actions own cleanup and handler facts;
  lowering publishes one typed region graph; MIR consumes explicit landing-pad
  edges without symbol-text recovery. Formation should be O(actions + region
  edges), with indexed RTTI/base lookup rather than rescanning declarations.
- Validation: the six grouped fixtures, prior PA26-P32 EH coverage, full PA33
  and PA1-P32 reports, file audit, and an increasing nested-region probe.

## Performance Evidence

The generated transform/layout series remained proportional:

| Cases | Source bytes | Identity requests | Syntax visits | Alias expansions | Semantic ms |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 32 | 6,360 | 64 | 992 | 64 | 9.38 |
| 64 | 12,409 | 128 | 1,984 | 128 | 17.79 |
| 128 | 24,621 | 256 | 3,968 | 256 | 35.52 |
| 256 | 49,325 | 512 | 7,936 | 512 | 69.23 |

Doubling cases exactly doubled identity work and alias expansion; 7.76x source
growth produced 7.38x semantic time. A runtime probe also covered cv/reference,
array, and function decay plus `sizeof(char)`/`sizeof(long)` specializations.

## Completed Checkpoints

| Checkpoint | Result |
| --- | --- |
| PA33 ABI-tag publication checkpoint | Pass — C1/C2, D1/D2, RTTI name/object, and vtable symbols carry canonical tags; PA33 59→62, PA1-PA32 4291/4291, file audit pass. |
| PA33 dependent callable-type recipe checkpoint | Pass — owner-prefix, qualified-member-owner, template-template, and local-result RTTI names are canonical; PA33 62→66, PA1-PA32 4291/4291, file audit pass. |
| PA33 dependent expression recipe checkpoint | Pass — alias-expanded `decltype`, template-id, unary, call, and non-type parameter expressions retain canonical ABI structure; PA33 66→68, PA1-PA32 4291/4291, file audit pass. |
| PA33 stack/SysV vararg intrinsic checkpoint | Pass — typed alloca and scalar varargs cover register-save/overflow paths with dynamic-frame restoration; PA33 68→71, PA1-PA32 4291/4291, file audit pass. |
| PA33 dependent transform/layout checkpoint | Pass — `__decay` is a semantic and retained ABI type node; ordinary alias parameters publish source recipes, and dependent class-layout constants replay after substitution; PA33 71→73, PA1-PA32 4291/4291, file audit pass. |
