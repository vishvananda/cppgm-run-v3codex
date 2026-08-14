# PA35 Final Audit

## Findings

1. Hosted vector calls were recognized semantically by a registry, but call
   lowering discarded that identity and looked the operation up again from the
   callee spelling. This was a `spec.md` identity/lowering fallback defect even
   though the focused tests passed.
2. Ordinary function analysis held a `FunctionInfo&` while parameter lifetime
   completion could instantiate a class and append implicit special members.
   Reallocation left the reference dangling; the first final report exposed it
   as a false function-try-block diagnosis in a PA22 friend-constructor case.
3. Semantic telemetry did not expose parser time, and total front-end elapsed
   time was published after the graph consumer had performed typed lowering.
   This made heavy-header phase attribution overlap and prevented a precise
   slow-path audit.
4. The function-fact repair pushed `pa12_semantic.cpp` above the file audit's
   3,000-line hard limit, exposing an existing catch-all source boundary.
5. No further correctness, representation, ownership, lookup, template replay,
   backend, allocation, self-containment, or scaling defect was found. The 22
   file-audit header-division diagnostics are inherited nonfatal advisories.

## Changes

- Added a `hosted_vector_intrinsic` enum to semantic call nodes, published it at
  semantic recognition, and consumed it directly in typed call lowering.
  `FindVectorIntrinsic` is now confined to the semantic front end.
- Replaced the long-lived mutable function-fact reference in ordinary function
  analysis with canonical `BindingId` re-acquisition. Parameter names are read
  before each potentially growing lifetime-completion call, and function body,
  friend, and try-body facts are copied before nested semantic analysis.
- Added parser time to semantic statistics, aggregated preprocessing/parser
  time for multi-source compilation, and published front-end elapsed time
  immediately after semantic analysis and scratch destruction.
- Extended driver statistics to report preprocessing, parsing, semantic,
  front-end, typed-lowering, adaptation, native-lowering, encoding, and output
  phases independently.
- Moved condition analysis into `pa12_condition_semantic.cpp` and registered it
  in the compiler source set, reducing `pa12_semantic.cpp` to 2,918 lines.
- Replaced the checkpoint-only PA35 documents with this PA-wide architecture
  review and consolidated checkpoint ledger.

## Architecture Trace

For the nontrivial declaration trace, fixed-width hosted vector declarations
are interned into canonical names and types; the selected operation is stored
as a semantic enum, lowered to typed eight-byte object construction/extraction,
converted to function-local MIR, encoded once, and emitted as direct ELF64.
The inspected object contained `make_v8qi`, `make_v4hi`, `make_v2si`, and
`extract_high` definitions and no external vector-builtin relocation.

For the demanded-template trace, a capturing `std::function<int()>` begins in
the streamed include/token path, retains one parsed template pattern, and uses
canonical template/argument/partition IDs plus request states to select and
cache specializations. Lookup, overload, exception, and lifetime facts feed a
deduplicated 40-function demand closure, then 343 typed LowIR and 472 MIR
instructions produce 36 ELF functions with the expected weak manager, invoke,
and handler symbols and direct relocations.

The representation audit found no text render/reparse cycle. Syntax and parser
scratch die before typed lowering; graph ownership outlives only its consumer;
typed and native LowIR coexist at the single structural adapter; native MIR and
encoding scratch are reclaimed per function. Canonical ID tables and parent
overlays avoid rendered signatures and copied environments. Specialization
state caches both success and failure by a complete key, and shape traversal
uses a flat visit-once set. The ordinary object route has no host compiler,
reference binary, source test path, or hosted-only backend shortcut.

## Performance Evidence

Release measurements with `CPPGM_DRIVER_STATS=1` separated all major phases:

| Workload | Template/demand work | Timing and storage |
| --- | --- | --- |
| Capturing `std::function<int()>` | 725 requests / 363 hits; 40 demanded; 343 LowIR / 472 MIR | preprocess 199.711 ms, parse 20.110 ms, semantic 95.461 ms, typed lower 3.397 ms, wall 0.32 s, RSS 20,608 KiB |
| `std::map` subscript | 3,111 / 2,064; 123 demanded; 1,438 / 1,893 | preprocess 358.470 ms, parse 41.037 ms, semantic 275.119 ms, typed lower 18.253 ms, wall 0.72 s, RSS 39,056 KiB |
| `std::regex` | 19,954 / 13,869; 2,165 demanded; 45,534 / 60,375 | 3-run median front end 2,809.182 ms, typed lower 261.858 ms, adapt 59.638 ms, native lower 181.515 ms, encode 194.324 ms; wall 3.70 s, RSS 225,796 KiB |

The 8/16-family dependent composite workload increased semantic nodes
101->189, declarations 551->967, requests 67->123, and median semantic time
3.264->4.965 ms. The 8/16/32-value call workload increased LowIR
97/139/223, MIR 111/153/237, and median native lowering
0.435/0.548/0.763 ms. Both traces scale with input and produced output; no
global retry, repeated specialization, allocation cliff, or unexplained phase
remains.

## Validation

- Hosted vector positive, arity-negative, and type-negative focused tests:
  3/3 passing after the identity change.
- PA22 friend-constructor reproducer: passing normally and under Valgrind with
  no invalid read after the function-fact ownership repair.
- Vector ELF inspection: direct definitions and only unwind-frame relocations;
  no external builtin call.
- PA35 final file audit: pass with 22 inherited advisory warnings.
- PA1-PA35 required report: 4,907/4,907 tests and 35/35 stages passing.

## Checkpoint Audit Ledger

| Checkpoint group | Audit disposition |
| --- | --- |
| Hosted ingress through demand-safe lowering (`24f026c6`-`391abff0`) | Pass: one staged front-end ownership path, retained syntax only where demanded, and bounded function-local lowering. |
| Canonical packs, assertions, and type demand (`ab8d37e6`-`56367510`) | Pass: complete canonical keys, visit-once type edges, monotonic completion, and error-only provenance rendering. |
| Qualified/exception/init-list ownership (`82d69bfd`-`a3b832f3`) | Pass: canonical declarations and parameter/class-owned delayed facts feed lowering without recovery. |
| Explicit target and specialization convergence (`1b9b4f5e`-`17c73b33`) | Pass: argument-aware routing and partition identity bound specialization replay. |
| Typed native transport through references (`c2d511c6`-`cb741298`) | Pass: typed values remain structured across ABI boundaries without host-tool or text fallback. |
| Demand, retained calls, control, and lifetime (`f1a47ab8`-`fb626bb3`) | Pass: deduplicated worklists and callable-owned cleanup bound body emission. |
| Statics through unsigned shifts (`bc32a966`-`470c9aff`) | Pass after final vector identity repair: typed hosted operations, canonical imports/list targets, and width-correct constants. |
| Alias cleanup and call pressure (`ca8f9e5e`-`9009b251`) | Pass: canonical alias convergence, enclosing cleanup ownership, and liveness-based input retirement. |
| Final PA-wide audit | Pass: typed intrinsic identity and stable function-fact ownership are preserved, condition semantics have a bounded source owner, all checklist surfaces are traced, and telemetry supplies phase-complete performance evidence. |
