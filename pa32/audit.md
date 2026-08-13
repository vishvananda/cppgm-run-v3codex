# PA32 Final Audit

Scope: PA32 full stage, from PA31 baseline `617abe30` through `a20d0480`,
plus the final audit repair. The review independently read `spec.md`, the PA32
contract, all 20 stage commits, the complete changed-source set, representative
handout/course tests, prior-stage architecture records, and the production
source-to-ELF path.

## Checkpoint Audit Ledger

| Checkpoint | Result |
| --- | --- |
| Host ABI roots and lifecycle demand (`1137edc6`–`f5e9e5f4`) | Pass: canonical object spellings, weak roots, demand pruning, and lifecycle aliases remain typed and single-owner. |
| Dependent result, substitution, and NTTP facts (`8670a6bd`–`f83e81ad`) | Pass: recipes retain canonical parameter/member/modifier identity and source type/value facts. |
| Structured results and audit (`f642998a`–`8094903f`) | Pass: compact framing and transactional publication have complete keys and no text recovery path. |
| Callable/member facts and audit (`45e35717`–`a631e4d6`) | Pass: typed callable/member terminals flow directly into ABI facts and object symbols. |
| External data addressing and audit (`0d3e1179`–`a41f3384`) | Pass: local/preemptible address choice survives through MIR and typed fixups. |
| ELF sections/TLS and audit (`b206d7c2`–`10b241de`) | Pass: section, TLS, wrapper, label, and relocation ownership is indexed and deterministic. |
| Virtual inheritance (`e51dcbea`) | Pass: complete/base entry points, support symbols, and COMDAT ownership are explicit. |
| Lifecycle/template preemption (`aed869d8`) | Pass: canonical peers and specialization suppression are demand-owned. |
| Dependent ABI ownership (`f0d4a536`) | Pass after final repair: shared type children are now classified and encoded once per complete identity. |
| Anonymous ownership and audit (`03c66170`–`a8c3b663`) | Pass: local ordinals, projected storage, union/default selection, and reverse indexes retain canonical owners. |
| Callable lifecycle boundary (`2ba6c588`) | Pass: member-pointer calls and direct ELF init/fini arrays preserve typed ownership. |
| Exception cleanup graph (`a20d0480`) | Pass: goto exits, argument temporaries, member/delegating construction, cleanup-first dispatch, and resume edges are explicit. |

## Findings

One release-blocking architecture/performance defect was found. A canonical
type DAG such as `Pair<T,T>` nested repeatedly was expanded as a tree in four
places:

1. local/anonymous-context classification revisited both equal children;
2. dependent-template-shape classification did the same;
3. host semantic shells recursively rendered the complete specialization type
   as their internal name; and
4. `AbiFactBuilder` rebuilt and copied an equivalent ABI type-fact subtree for
   every repeated occurrence.

This violated the specification's canonical-identity, no-rendered-hot-key, and
O(input + output) requirements. It was not visible in the functional suite:
depth 16 already reached 498,988 KiB RSS, and depth 20 exceeded 20 seconds while
approaching 7.9 GiB.

No additional correctness, lifetime, lookup, lowering, backend, object-format,
self-containment, or placement blocker was found. The 21 file-audit warnings
are unchanged inherited header-division advisories, not PA32 failures.

## Changes

- `pa32_template_preemption_semantic.cpp`: replaced recursive per-argument
  local-context expansion with one iterative, validated, visit-once traversal
  over all argument roots. Visit state includes the type table's maximum valid
  ID, and unresolved dependent placeholders do not invent concrete ownership.
- `pa19_template_semantic.cpp`: made dependent-shape traversal iterative and
  canonical; host-object specialization shells now use compact
  pattern/argument/partition IDs instead of recursively rendered types. Staged
  non-host presentation output is unchanged.
- `pa15_lowering_abi.cpp`: interned ABI type-argument facts in a geometric
  open-addressed table keyed by `TypeId`, canonical function `BindingId`, and
  recipe ID; also made dependent-parameter discovery visit-once.
- Added
  `cppgm.tests/course/pa32/200-canonical-template-type-dag-scaling.t`, a
  depth-24 compile/link/run regression that would exceed the old build budget.

The repair is at the shared ownership points rather than at the observed
timeout. Canonical specialization lookup still uses
`TemplateSpecializationKey`; compact shell names are presentation/storage
labels only; final Itanium spelling is still derived from typed ABI facts.

## Performance Evidence

The initial profile was dominated by ABI fact movement, copy/destruction, and
argument-reference resolution. After ABI interning, the exposed semantic
profile was dominated by range interning/moves and recursive specialization
name/dependent-shape work. Both causes were repaired before remeasurement.

| DAG depth | Declarations | Template requests | Semantic peak bytes | Semantic + lowering ms | Object bytes | RSS KiB |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 16 | 111 | 19 | 149,152 | 1.211 | 4,424 | 8,040 |
| 32 | 207 | 35 | 282,992 | 2.017 | 4,936 | 8,468 |
| 64 | 399 | 67 | 554,256 | 3.152 | 6,072 | 8,772 |
| 128 | 783 | 131 | 1,095,846 | 5.991 | 8,376 | 9,632 |
| 256 | 1,551 | 259 | 2,180,262 | 12.446 | 12,984 | 11,128 |

Across 16x depth, semantic work/storage and output remain proportional; native
work is constant at 2 functions, 4 LowIR instructions, 9 MIR instructions, and
3 fixups. The depth-16 generated weak name is byte-for-byte the same raw symbol
as `g++`; the object links and runs. Stats-on/off objects compare identical.

## Validation

- Focused canonical-DAG regression: 1/1 pass.
- `make test-pa32`: 133/133 handout and 8/8 course tests pass.
- `perl scripts/cppgm_file_audit.pl --stage pa32 --paths dev/src`: pass with
  21 inherited warnings.
- Process trace of `cppgm++ -c`: one `execve`, no child process.
- Host inspection: ELF64 `ET_REL`, weak template symbol, COMDAT group,
  relocations, `.eh_frame`, and compatibility payload present.
- `make test-report-through-pa32`: 4,291/4,291 tests and 32/32 stages
  pass; all tracked stages pass.
