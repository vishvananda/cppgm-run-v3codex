# PA36 Final Plan

## Stage Design and Spec Alignment

The PA36 production path remains the shared compiler path:

`source buffer -> streaming preprocessing sink -> compact interned token vector
-> one SyntaxArena -> canonical semantic graph and demand worklists -> typed
LowIR -> structural native adapter -> function-local MIR -> direct ELF64`

This is the staged-project form of the `spec.md` forward pipeline. The parser
owns one compact token sequence rather than successive token representations.
Syntax, substitution, lookup, and demand scratch die before the semantic graph
consumer runs. Graph lowering consumes canonical `NameId`, `TypeId`, `ScopeId`,
`EntityId`, and `BindingId` facts; it neither renders nor reparses text. Typed
and native LowIR coexist only across the one structural adapter. Native
lowering, liveness, register state, and encoding scratch are bounded to one
function, while direct ELF assembly alone retains cross-function symbols,
relocations, lifecycle arrays, and final section layout.

PA36 extends that path rather than adding a hosted backend. Resolved hosted
calls retain their canonical callable and intrinsic identities. Deduplicated
member-definition, default-constructor, and function demand queues compute the
required-definition closure. ABI lowering consumes canonical owner scopes,
types, lifecycle entry relationships, linkage, and source-declared ABI facts.
The object writer publishes the resulting weak/strong definitions and aliases;
externally owned library symbols remain unresolved with host ABI spelling.

## Performance Evidence

All measurements used the release compiler with `CPPGM_DRIVER_STATS=1` and
`/usr/bin/time`. A recursive `std::function` PA36 object compiled in 0.43 s at
23,824 KiB RSS with 1,011 specialization requests / 540 hits, 51 demand pushes,
50 demanded functions, 537 LowIR instructions, 722 MIR instructions, and 195
fixups. The virtual-base `basic_ostream` ownership fixture compiled in 1.42 s
at 65,916 KiB RSS with 351 demanded functions, 7,608 LowIR instructions, and
10,944 MIR instructions; its object publishes exactly one complete constructor.

A controlled recursive hosted-template workload at depths 4/8/16 produced:

| Depth | Template requests / hits | Demanded functions | LowIR / MIR | Fixups / object bytes | Median native lower / encode |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 4 | 755 / 387 | 46 | 427 / 594 | 183 / 269,344 | 2.102 / 1.206 ms |
| 8 | 783 / 411 | 54 | 523 / 722 | 227 / 322,240 | 2.200 / 1.280 ms |
| 16 | 839 / 459 | 70 | 715 / 978 | 315 / 431,512 | 2.671 / 1.741 ms |

Each added specialization contributes seven requests, six cache hits, two
demanded/emitted functions, 24 LowIR instructions, 32 MIR instructions, and 11
fixups. RSS remained 20,848-21,356 KiB and every linked binary passed. This is
linear work and output growth with no retry, invalidation, or allocation cliff.

The three-run heavy-regex profile was deterministic (identical object hashes).
Its median 7.29 s / 232,368 KiB run was explained by 273,855 tokens, 113,605
declarations, 203,504 indexed lookups with 360,070 scope visits, 19,904
specialization requests / 13,840 hits, 2,148 demanded functions, 45,510 LowIR
instructions, and 60,315 MIR instructions. Median phase times were 2,123.631 ms
preprocessing, 333.838 ms parsing, 3,219.484 ms semantic analysis, 431.148 ms
typed lowering, 125.028 ms adaptation, 356.161 ms native lowering, and 528.619
ms encoding. No unexplained slow phase remains.

## Architecture Review

| Checklist surface | Final disposition |
| --- | --- |
| Representation and ownership | Source, one compact token sequence, and one syntax arena feed one semantic graph. Parser/analyzer scratch dies before graph lowering; MIR is reclaimed per function. No rendered text is a production transport. |
| Identity and lookup | Names, scopes, declarations, types, specialization arguments, and callable selections use compact canonical IDs and flat/indexed tables. `std` ABI substitution is selected by canonical namespace `ScopeId`, not a rendered owner string. |
| Templates and repeated work | Template syntax is parsed once; canonical request keys expose not-started/in-progress/succeeded/failed states; dependent demand is deduplicated; non-dependent facts and retained environments are shared. |
| Lowering and backend | Selected callable, conversion, lifetime, layout, virtual-base, intrinsic, ABI-entry, and linkage facts flow directly into typed LowIR. One structural adapter feeds per-function MIR and the direct x86-64 ELF writer. |
| Allocation and scaling | Long-lived graphs use contiguous owner storage and compact IDs; temporary work uses bounded vectors/worklists. Scaling counters track demanded semantics and emitted IR linearly. Ordered maps are isolated to final EH/ELF presentation where stable table construction is required. |
| Self-containment | Compile mode invokes no host compiler, assembler, prior stage, reference binary, or answer cache. The PA36 harness uses the host toolchain only after object production for the specified final link. |

The nontrivial declaration trace is the `StreamWrapper` construction path:
interned hosted declarations and canonical class/layout facts select the
`basic_ostream` constructor, preserve its complete/base entry relationship and
virtual-base ownership, build structured Itanium ABI facts, lower constructor
and cleanup actions to typed LowIR, lower/encode the function once, and publish
one weak C1 ELF definition plus required relocations.

The demanded-template trace is the recursive `std::function<void(const
Value&)>` path. Canonical template/argument/request identities retain the
selected specializations and call conversions, a 51-push worklist closes 50
demanded bodies, and 537 LowIR / 722 MIR instructions produce 47 functions.
ELF inspection shows ABI-correct weak `_Function_handler` manager/invoke and
`std::function` lifecycle definitions with direct relocations to emitted bodies
and ordinary unresolved host runtime/library owners.

## Final Architecture Review

The final audit independently reviewed the PA36 contract, all eleven stage
commits, all 29 changed compiler source files, the production driver and phase
owners, representative typed data flow, symbol tables/relocations, controlled
scaling, and the heavy-header phase profile. Closed intrinsic handlers retain
operation identity before generic compatibility aliases; canonical `std` owner
identity survives to ABI facts; lifecycle entries carry explicit alias policy;
specialization completion and callable cleanup remain monotonic and locally
owned; reference/vbase boundaries preserve typed addresses; floating sign
classification evaluates once and extracts the format-owned sign bit.

No correctness, architecture, performance, timeout, self-containment, or
file-audit blocker remains. The file audit's 22 header-division messages are
inherited nonfatal advisories; PA36 introduces no new hard violation. The only
final-audit artifact defect was the checkpoint-only audit record, now replaced
by this PA-wide consolidation and the matching final audit.

## Checkpoint Ledger

| Checkpoint commits | Consolidated result |
| --- | --- |
| `1f918c84`-`4e3ae78d` | Closed compiler builtins retain semantic operation identity before generic hosted alias fallback; collision regression and prior builtin behavior pass. |
| `03e47d62`-`1de3710d` | Standard-library owner identity is canonical `ScopeId` data through structured ABI facts and continuous Itanium substitution state. |
| `79dce1ac` | Trivial explicit-destructor calls preserve semantic checks and object evaluation while avoiding unnecessary host calls. |
| `c2d73df6` | Hosted callable type and complete-object virtual-base boundaries are normalized before lowering. |
| `090d2d0f` | Container paths complete conversion, layout, initializer-list, constexpr, and inherited-constructor facts at their semantic owners while preserving call-live native values. |
| `5d71de98` | Complete/base/deleting lifecycle entries carry canonical ownership, weak linkage, and explicit object-publication/alias policy into ELF. |
| `60defaab` | Nested callable lookup ancestry is separated from automatic-object cleanup domains; union reference calls remain addresses. |
| `4136f859` | Tuple/forwarding reference ordering uses normalized canonical type/category facts, and complete temporary virtual bases use static projections. |
| `7fe4ff43` | f32/f64/f80 sign predicates evaluate once and lower to constant-size typed sign-field extraction. |
| Final PA-wide audit | Architecture, ownership, identity, demand, lowering, ELF publication, self-containment, scaling, and all gates are independently evidenced; the audit ledger is consolidated. |
