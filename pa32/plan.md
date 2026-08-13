# PA32 Final Audit Plan

## Stage Design and Spec Alignment

PA32 owns the production path
`source/options -> canonical semantic IDs -> typed PA15 LowIR -> direct native
adapter -> per-function MIR/encoding -> sections/symbols/relocations -> ELF64
relocatable`. It adds host ABI spelling and binding, COMDAT/weak roots, imported
GOT/TLS addressing, custom sections, init/fini ownership, and host exception
metadata without replacing the earlier semantic, lifetime, or class-value
pipeline.

A nontrivial object declaration is resolved to one semantic `BindingId` with
typed linkage, storage, section, TLS, address-binding, initializer, and lifetime
facts. PA15 publishes those facts on one `SymbolId`; the adapter maps them to
typed global/function metadata; native selection emits the appropriate direct,
GOTPCRELX, or TPOFF32 operation; and the ELF writer owns the final section,
symbol, and relocation. A demanded function template is keyed by canonical
pattern and argument-list IDs, emitted once, represented by one canonical
`SymbolIdentity`, converted to typed ABI facts, mangled once, and carried as a
weak object symbol through LowIR, MIR, and COMDAT publication.

This aligns with `spec.md` sections 2 and 4-10. Semantic equality and template
requests use compact IDs; demand is separate from ownership; lowering consumes
selected declarations, layouts, lifetime actions, callable facts, and ABI roles
directly; machine code and ELF are emitted without assembly or a child
toolchain. The binary `.cppgm_object` section remains only for PA30's cumulative
internal-link contract. It is generated after typed adaptation and is not
parsed, validated, or used by the PA32 host-object path.

## Performance Evidence

The audit generated a repeated-child canonical type DAG: `T0=int`,
`T(n)=Pair<T(n-1),T(n-1)>`, demanded through `probe<Tn>`. The original path
re-expanded shared semantic and ABI structure: depth 16 took 1.40 s and 498,988
KiB RSS; depth 20 did not finish in 20 s and approached 7.9 GiB. The first
profile concentrated work in ABI fact copies, destruction, and argument
resolution. After ABI interning exposed the remaining front-end cost, a second
profile attributed 22.09% to range interning, 13.00% to `memmove`, 11.34% to
`ClassTemplateSpecializationName`, 9.88% to dependent-shape traversal, and
5.52% to name-path parsing.

Final `CPPGM_DRIVER_STATS=1` measurements:

| Depth | Source bytes | Declarations | Template requests | Semantic peak bytes | Semantic / lowering ms | Object bytes | Max RSS KiB |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 16 | 562 | 111 | 19 | 149,152 | 0.769 / 0.442 | 4,424 | 8,040 |
| 32 | 1,010 | 207 | 35 | 282,992 | 1.122 / 0.895 | 4,936 | 8,468 |
| 64 | 1,906 | 399 | 67 | 554,256 | 1.786 / 1.366 | 6,072 | 8,772 |
| 128 | 3,784 | 783 | 131 | 1,095,846 | 3.326 / 2.665 | 8,376 | 9,632 |
| 256 | 7,752 | 1,551 | 259 | 2,180,262 | 7.606 / 4.840 | 12,984 | 11,128 |

The 16x series has proportional declarations, requests, storage, times, and
output. Native work remains exactly 2 functions, 4 LowIR instructions, 9 MIR
instructions, and 3 fixups, so the counters distinguish semantic/ABI growth
from backend work. The depth-16 weak symbol exactly matches the host compiler,
links with `g++`, and runs with exit 0.

## Architecture Review

- Representation and ownership: preprocessing/syntax/semantic state is
  front-end-owned and dies when typed LowIR is returned. The adapter creates a
  self-contained native LowIR with no semantic back-pointers. The backend holds
  one function's MIR while encoding it, then retains only code/data, compact
  function layouts, relocations, and the final ELF image. There is no textual
  LowIR or assembly representation. The PA30 binary compatibility payload is
  the one intentional overlap with native LowIR.
- Identity and lookup: types, entities, bindings, argument lists,
  specialization requests, symbol identities, and cleanup states are compact
  IDs. Host-only class-specialization storage names now derive from canonical
  pattern/argument IDs instead of recursively rendered type text. Mangled
  strings and section names are output facts, not semantic lookup keys.
- Templates and repeated work: request tables carry in-progress/success/failure
  state; substitutions publish transactionally; dependent recipes and selected
  declarations are retained as typed facts; no translation-unit retry exists.
  Shared type DAGs are traversed once per identity, and repeated ABI type
  arguments reuse one fact under the complete type/function/recipe key.
- Lowering and backend: semantic choices flow directly into typed LowIR.
  `ProgramLoweringSession::lower_function` lowers and encodes each emission
  unit once. TU-wide work is limited to linkage, symbols, data sections,
  lifecycle arrays, EH tables, relocation layout, and deterministic final
  ordering. ELF bytes are written directly.
- Allocation and scaling: the repaired traversals are iterative with compact
  visited state; the ABI fact cache is a geometrically grown open-addressed
  table. No repaired hot path allocates a node per edge, recursively destroys a
  duplicated fact tree, globally invalidates caches, or rescans the TU.
- Self-containment and determinism: process tracing records only the requested
  `cppgm++` `execve`; production source contains no compiler, assembler, or
  reference-binary launch. Stats-on and stats-off objects are byte-identical.

## Final Architecture Review

The audit found one cross-owner defect with four symptoms. Canonical nested
template types were recursively expanded for local-context classification,
dependent-shape classification, semantic presentation names, and every ABI
argument occurrence. A DAG was therefore treated as a tree, and recursively
rendered text became a production semantic name.

The fix closes the entire ownership path: semantic classifiers are iterative
visit-once traversals with the type table's inclusive dense-ID bound; host
specialization shells use compact names formed from canonical IDs while staged
textual output remains unchanged; ABI type facts are
interned by `(TypeId, canonical function BindingId, recipe ID)`; and dependent
parameter detection also uses visited IDs. The new depth-24 regression guards
the complete source-to-host-link path. Exact host mangling, direct ELF shape,
byte determinism, no-child-process tracing, PA32 behavior, cumulative behavior,
and file placement are all validated. No correctness, architecture,
performance, self-containment, or file-audit blocker remains.

## Checkpoint Ledger

| Commit | Independent audit result |
| --- | --- |
| `1137edc6` | Pass — canonical host ABI symbol roots and weak ODR ownership. |
| `f5e9e5f4` | Pass — demand-owned lifecycle emission. |
| `8670a6bd` | Pass — parameter-rooted dependent-result recipes. |
| `a402b2cb` | Pass — canonical standard-template substitutions. |
| `f83e81ad` | Pass — dependent NTTP source type/value preservation. |
| `f642998a`, `8094903f` | Pass — structured dependent results and transactional publication. |
| `45e35717`, `a631e4d6` | Pass — canonical callable/member ABI facts. |
| `0d3e1179`, `a41f3384` | Pass — external object-data identity and address ownership. |
| `b206d7c2`, `10b241de` | Pass — typed sections, TLS, labels, and relocations. |
| `e51dcbea` | Pass — virtual-inheritance ABI artifacts and support ownership. |
| `aed869d8` | Pass — lifecycle entries and canonical template preemption. |
| `f0d4a536` | Pass after final audit repair — dependent ABI ownership is now visit-once. |
| `03c66170`, `a8c3b663` | Pass — anonymous/local entity and projected-storage ownership. |
| `2ba6c588` | Pass — host callable/member-pointer and init/fini boundaries. |
| `a20d0480` | Pass — explicit cleanup graph, protected exits, temporaries, and constructor unwind. |
