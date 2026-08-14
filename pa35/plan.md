# PA35 Final Plan

## Stage Design and Spec Alignment

The PA35 production path is:

`source bytes -> streamed preprocessing -> compact tokens/SyntaxArena -> canonical
semantic graph -> typed LowIR -> native LowIR adapter -> function-local MIR ->
direct ELF64`

This is the staged-project adaptation of the integrated front end required by
`spec.md`. PA10 owns one interned token sequence and syntax arena; PA12 consumes
that arena once and retains only the parsed template nodes required by later
demand. Parser, lookup, substitution, and demand scratch are destroyed before
the graph consumer runs. Semantic identity uses compact `NameId`, `TypeId`,
`ScopeId`, `EntityId`, `BindingId`, canonical argument-list, and partition IDs.
Specialization requests are keyed by template, arguments, and partition and
publish monotonic not-started/in-progress/succeeded/failed states.

Function facts are also reached by stable canonical `BindingId`. Function-body
analysis does not retain a vector element reference across class completion,
which may append implicit special members; it re-acquires facts by identity and
snapshots body-role scalars before nested semantic work.

Typed lowering consumes selected facts directly. Hosted vector calls now carry
their registry enum from semantic analysis instead of recovering it from a
callee spelling. The one native adapter copies typed structures without a text
round trip. MIR, liveness, register state, and encoding scratch are owned and
released per function; the ELF writer emits machine code, symbols, relocations,
and the required compiler-object payload directly. There is no hosted-only
backend route or host-compiler fallback.

At the front-end boundary, source, compact tokens, syntax, and the semantic
graph coexist only while the graph is built. At the lowering boundary the
released syntax storage is replaced by the graph plus accumulating typed
LowIR. During adaptation, typed and native LowIR coexist once; during native
emission, only one function's MIR is additionally live. Retained syntax names
the semantic graph owner, and no later phase pins parser scratch.

## Performance Evidence

All measurements used the release compiler, `CPPGM_DRIVER_STATS=1`, and
`/usr/bin/time`; times are elapsed milliseconds.

| Workload | Semantic work | Phase evidence | Peak RSS |
| --- | --- | --- | --- |
| Capturing `std::function<int()>` | 40 demanded functions, 725 template requests / 363 hits, 343 LowIR / 472 MIR instructions | preprocess 199.711, parse 20.110, semantic 95.461, typed lowering 3.397, native lowering 1.377; wall 0.32 s | 20,608 KiB |
| `std::map` piecewise subscript | 123 demanded functions, 3,111 requests / 2,064 hits, 1,438 LowIR / 1,893 MIR | preprocess 358.470, parse 41.037, semantic 275.119, typed lowering 18.253, native lowering 6.238; wall 0.72 s | 39,056 KiB |
| `std::regex` | 2,165 demanded functions, 19,954 requests / 13,869 hits, 45,534 LowIR / 60,375 MIR | 3-run medians: preprocess 1,033.882, parse 150.939, semantic 1,589.734, typed lowering 261.858, adapt 59.638, native lowering 181.515, encode 194.324; wall 3.70 s | 225,796 KiB |

The dependent composite-template-shape workload at 8/16 families produced
67/123 template requests, 28/52 cache hits, 101/189 semantic nodes, and
551/967 declarations. Five-run median semantic time was 3.264/4.965 ms and
front-end time was 6.134/8.012 ms. The work grew below the doubled input and
showed no retry, allocation, or lookup cliff. The prior 8/16/32-value extended
call probe produced 97/139/223 LowIR and 111/153/237 MIR instructions; median
native lowering was 0.435/0.548/0.763 ms and encoding was
0.168/0.246/0.452 ms.

## Architecture Review

| Checklist surface | Final disposition |
| --- | --- |
| Representation and ownership | One compact syntax owner feeds one canonical graph; phase scratch and function MIR have explicit destruction boundaries; there is no rendered-text transport. |
| Identity and lookup | Hot semantic keys are compact canonical IDs in indexed/open-addressed tables. Vector intrinsic identity is now a typed enum through lowering; spelling lookup remains only at front-end recognition. |
| Templates and repeated work | Bodies and retained dependent nodes are shared; environments are parent-linked overlays; complete request keys cache success and failure; worklists publish only monotonic facts. |
| Lowering and backend | Typed facts flow through one structural adapter to per-function MIR and direct x86-64 ELF. No ordinary-path whole-program validator, serializer round trip, semantic lookup, or external compiler exists. |
| Allocation and scaling | Arena/vector storage replaces hot per-node ownership, visit sets bound shape traversal, and counters plus 8/16 scaling show work tracking semantic input and emitted output. |

Representative traces close both paths requested by `spec.md`. A hosted vector
declaration/call moves from registry spelling recognition to canonical function
and vector types, a `DumpNode` intrinsic enum, typed object operations, MIR lane
stores/extracts, and ELF symbols with no external builtin relocation. A
capturing `std::function` moves from retained template syntax through a
canonical specialization request/state/cache, overload and lifetime facts, a
40-item demanded-function closure, 343 typed LowIR instructions, 36 emitted
functions, and direct weak ELF handler/manager/invoke symbols and relocations.

## Final Architecture Review

The final audit found three cross-phase issues and one source-division issue and
closed each at its ownership boundary. Vector lowering reconstructed a semantic
operation from a callee name; the semantic node now carries canonical intrinsic
identity. Ordinary
function analysis retained a `FunctionInfo` vector reference while class
completion could append implicit functions; stable `BindingId` re-acquisition
now prevents that use-after-free. Front-end telemetry omitted parser time and
finalized total elapsed time after typed lowering; parser time is now explicit
and semantic publication occurs before the graph consumer. The resulting
heavy-header profiles account for every major phase. Condition analysis now has
a responsibility-named source unit, leaving the core semantic unit below the
file-audit limit. No unexplained slow path remains.

No correctness, architecture, performance, self-containment, or file-audit
blocker remains. File audit's 22 header-division notices are inherited
advisories, not PA35 ownership violations.

## Checkpoint Ledger

| Checkpoint commits | Consolidated result |
| --- | --- |
| `24f026c6`-`391abff0` | Hosted ingress, retained declarations, nested replay, and demand-safe function-local lowering established the front-end ownership path. |
| `ab8d37e6`-`56367510` | Canonical class/pack ownership, static assertions, and completed-type demand established bounded retained-template identity and readiness. |
| `82d69bfd`-`a3b832f3` | Qualified identity, retained exceptions, parameter-owned `noexcept`, and initializer-list definition completion converged canonical declarations. |
| `1b9b4f5e`-`17c73b33` | Explicit class routing, specialization ownership/local construction, and explicit-id packs completed argument-aware request partitioning. |
| `c2d511c6`-`cb741298` | Native object transport, empty variadics, function-type arguments, reentrant requests, and reference boundaries preserved typed values. |
| `f1a47ab8`-`fb626bb3` | Unevaluated demand, specialization-owned calls, noreturn flow, and callable lifetime chains bounded demanded-body emission. |
| `bc32a966`-`470c9aff` | Addressable statics, vector builtins, using imports, braced specialization, and unsigned shifts completed hosted semantic coverage. |
| `ca8f9e5e`-`9009b251` | Alias/cleanup convergence and call-input retirement removed the final functional and register-pressure barriers. |
| Final PA-wide audit | Intrinsic identity remains typed through lowering, function facts are re-acquired by canonical ID across growing work, condition analysis has bounded source ownership, and telemetry separates every major phase. |
