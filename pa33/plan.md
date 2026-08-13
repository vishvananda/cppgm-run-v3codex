# PA33 Implementation Plan

## Stage Design and Spec Alignment

PA33 extends the PA32 path `semantic entity/callable IDs -> typed LowIR ABI
artifacts -> per-function MIR -> direct ELF` with host-runtime facts for vtables,
VTTs, RTTI, casts, covariant thunks, and EH. It must preserve one owner for each
selected declaration, layout, ABI entry point, and emission unit (`spec.md` §§2,
4, 6-10); names are rendered only at ABI publication, and object writing does
not recover semantic facts from strings.

## Current Failure Map

Current result: **91/94 PA33 tests pass**, improved from the 88/94 turn
baseline (and 59/94 stage-start baseline); PA1-PA32 pass (4291/4291).

| Owner / shared behavior | Count | Failing cases |
| --- | ---: | --- |
| Remaining ABI name publication | 1 | nested-lambda owner identity |
| Remaining type/name frontend | 1 | unnamed local-class constructor |
| Polymorphic ODR ownership | 1 | duplicate inline-header polymorphic class |

## Active Checkpoint

**Next — remaining host identity/ODR publication.** Resolve the final three
cases at the canonical semantic-entity to host-ABI publication boundary.

- Spec alignment (§§2, 4, 6-10): local/lambda identities and polymorphic ODR
  owners remain canonical semantic facts; ABI names and weak ownership are
  published once without reconstructing owners from rendered strings.
- Owner/data flow: declaration merge -> local/lambda or polymorphic entity ->
  canonical owner/demand fact -> mangled symbol and weak definition. Indexed
  lookup remains O(1), with O(declarations + demanded artifacts) total work.
- Validation: the three remaining fixtures, adjacent local/lambda/template and
  duplicate-header cases, full PA33/prior reports, file audit, and a widening
  duplicate-declaration sample.

## Performance Evidence

Host-object static TLS declarations and reads remained proportional:

| TLS entities | Wrappers | Access calls | LowIR instructions | Lowering ms |
| ---: | ---: | ---: | ---: | ---: |
| 16 | 16 | 16 | 48 | 0.221 |
| 32 | 32 | 32 | 96 | 0.315 |
| 64 | 64 | 64 | 192 | 0.484 |

A 4x wider set produced exactly 4x wrappers, calls, and instructions; lowering
time grew 2.19x with symbol-indexed access lookup.

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
| PA33 finalized covariant/VTT checkpoint | Pass — finalized fixed/virtual result paths publish keyed Itanium covariant thunks with null-preserving adjustment, and synthesized C2 copies forward VTT/virtual-base arguments; PA33 85→88, PA1-PA32 4291/4291, file audit pass. |
| PA33 static/TLS lifecycle checkpoint | Pass — nested callback declarators retain function-pointer types; host-object lowering emits explicit TLS access/initialization wrappers and registers process/thread teardown after successful construction; PA33 88→91, PA1-PA32 4291/4291, file audit pass. |
