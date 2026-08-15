# PA36 Final Audit

## Findings

1. The implementation review found no remaining correctness, architecture,
   performance, self-containment, timeout, or file-audit blocker. All PA36
   behavior stays on the shared canonical semantic -> typed LowIR ->
   function-local MIR -> direct ELF path.
2. The existing audit record was incomplete: it stopped after the second of
   eleven PA36 commits and therefore did not substantiate the final 80/80 stage
   or the later callable, container, lifecycle, tuple, and floating-point
   ownership changes.
3. The 22 file-audit header-division diagnostics are inherited warnings rather
   than hard failures. The PA36 changes add no oversized source, unregistered
   source unit, generated artifact, or forbidden dependency.
4. Controlled scaling and the heavy-regex profile show no repeated
   specialization, whole-program retry, global invalidation, backend rescan,
   allocation cliff, or unexplained slow phase.

## Changes

- Closed builtin dispatch now gives semantic intrinsic handlers precedence over
  the generic `__builtin_x -> x` compatibility alias; operation identity remains
  typed through lowering. Floating `signbit` lowers by evaluating once and
  extracting the f32/f64/f80 format sign field.
- The global standard namespace is designated once by canonical `ScopeId`.
  Hosted traits, initializer-list recognition, nested class owners, mangling,
  and lifecycle entries consume canonical scope/type/entry facts rather than
  rediscovering semantic identity from rendered strings.
- Class and function specialization paths now complete inherited constructors,
  provisional initializer-list layout, conversion candidates, enum constant
  roots, reference ordering, and virtual-base projections at their owning
  semantic fact records.
- Callable cleanup domains are independent of lexical lookup ancestry, union
  reference values remain addresses, and native forwarding preserves values
  across calls according to liveness.
- Lifecycle object publication now carries explicit internal-alias policy and
  weak deleting-entry ownership through typed LowIR metadata and the ELF symbol
  table.
- The checkpoint-only PA36 documents are replaced by a PA-wide final plan,
  architecture review, performance record, validation record, and consolidated
  ledger.

## Architecture Trace

For the nontrivial declaration trace, the `StreamWrapper` fixture enters as an
immutable source buffer and one compact interned token/syntax owner. Canonical
scope, entity, type, layout, selected-constructor, cleanup, virtual-base, and
linkage facts select `std::basic_ostream<char>::basic_ostream(basic_streambuf*)`.
The mangler consumes the standard namespace `ScopeId`, structured owner type,
template arguments, and lifecycle entry identity; typed lowering emits the
constructor and cleanup calls; one function-local MIR is lowered and encoded;
the ELF writer publishes one weak complete C1 entry with the required host ABI
relocations. It does not publish a duplicate C2 object symbol.

For the demanded-template trace, recursive `std::function<void(const Value&)>`
uses canonical template, argument-list, partition, binding, conversion, and
request-state identities. The monotonic member/default/function worklists close
at 51 pushes and 50 demanded bodies. Graph lowering emits 537 typed LowIR
instructions, the native path emits 722 MIR instructions in 47 functions, and
ELF inspection finds weak ABI-correct `_Function_handler` manager/invoke and
`std::function` constructor/destructor definitions plus direct relocations.

At the representation boundaries, syntax and semantic construction overlap
only while the graph is built; analyzer/parser/token scratch is destroyed before
the graph consumer. The graph and accumulating typed LowIR overlap during one
consumer call, typed/native LowIR overlap across one structural adapter, and
only one function's MIR/encoding scratch is live during native emission. The
embedded compiler-object payload is a post-lowering binary section for staged
linking, not a text transport or an input to object generation. No later phase
retains parser scratch and no production phase renders structured data merely
to parse it again.

## Performance Evidence

The recursive `std::function` trace compiled in 0.43 s at 23,824 KiB RSS with
1,011 specialization requests / 540 cache hits, 51 demand pushes, 50 demanded
functions, 537 LowIR instructions, 722 MIR instructions, 195 fixups, and a
356,824-byte object. The virtual-base `basic_ostream` trace compiled in 1.42 s
at 65,916 KiB RSS with 351 demanded functions, 7,608 LowIR instructions, 10,944
MIR instructions, and one published complete constructor.

The depth-4/8/16 recursive hosted-template scale produced 755/783/839 template
requests, 46/54/70 demanded functions, 427/523/715 LowIR instructions,
594/722/978 MIR instructions, 183/227/315 fixups, and
269,344/322,240/431,512-byte objects. Thus each added specialization contributes
constant semantic and backend work. RSS stayed within 20,848-21,356 KiB; all
three linked programs passed.

The three-run regex profile produced identical object hashes. Median wall/RSS
was 7.29 s / 232,368 KiB. Median phase time was 2,123.631 ms preprocessing,
333.838 ms parsing, 3,219.484 ms semantic analysis, 431.148 ms typed lowering,
125.028 ms adaptation, 356.161 ms native lowering, and 528.619 ms encoding.
Those costs correspond to 273,855 tokens, 113,605 declarations, 203,504 lookup
queries / 360,070 scope visits, 19,904 specialization requests / 13,840 hits,
2,148 demanded functions, 45,510 LowIR instructions, and 60,315 MIR
instructions. The counters explain the slow phases and show bounded work.

## Validation

- Recursive `std::function` ELF inspection: expected weak manager, invoke,
  constructor, destructor, and direct relocation identities are present.
- `basic_ostream` constructor ownership inspection: focused PA36 test passes
  and exactly one complete constructor is published.
- Controlled depth 4/8/16 objects: all host-link and execute successfully.
- Heavy-regex determinism: three objects have the same SHA-256 hash.
- PA36 file audit: passes with 22 inherited nonfatal warnings.
- Required PA1-PA36 report: 4,987/4,987 tests and 36/36 stages pass.

## Checkpoint Audit Ledger

| Checkpoint commits | Audit disposition |
| --- | --- |
| `1f918c84`-`4e3ae78d` | Pass: closed intrinsic identity precedes generic aliases; typed dispatch, collision regression, and prior-stage behavior are preserved. |
| `03e47d62`-`1de3710d` | Pass: canonical `std` namespace identity feeds structured ABI facts without a lowering-time rendered-owner comparison. |
| `79dce1ac` | Pass: trivial explicit destruction retains semantic checks/evaluation and removes only the unnecessary emitted call. |
| `c2d73df6` | Pass: hosted callable and complete-object virtual-base facts cross semantic/lowering boundaries as canonical types and offsets. |
| `090d2d0f` | Pass: container specialization completion is monotonic and owner-local; inherited constructors and call-live values remain typed. |
| `5d71de98` | Pass: lifecycle entry identity, weak ownership, object aliases, and publication policy flow through typed metadata to ELF. |
| `60defaab` | Pass: callable cleanup lifetime is isolated from lexical lookup, and union references are not rematerialized as values. |
| `4136f859` | Pass: tuple reference ranking and virtual-base projection consume normalized category/type/layout facts with bounded work. |
| `7fe4ff43` | Pass: floating sign classification is single-evaluation, width-owned, constant-space typed lowering. |
| Final PA-wide audit | Pass: all checklist surfaces, representative traces, scaling, self-containment, file audit, cumulative tests, commit state, and clean worktree are covered. |
