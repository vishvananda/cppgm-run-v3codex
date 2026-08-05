# PA9 Final Audit Record

## Stage Design and Spec Alignment

`cy86` reads one immutable source buffer at a time and reuses the PA5
preprocessor/post-token cursor.  Post-tokens flow directly into a
semicolon-bounded CY86 parser; identifiers are interned on arrival, each
distinct opcode is classified once, and statements retain compact opcode,
operand, label, and literal-slice identities.  One compilation-owned byte
arena owns literal payloads.  Forward labels are tracked by dense identity plus
a set containing only unresolved names.

After final semantic checks, parser scratch is destroyed.  The backend lowers
each typed statement once into x86-64 bytes and address fixups, records label
addresses in a dense identity vector, patches each fixup once, releases fixup
and semantic storage, and writes a fixed ELF header followed by the one code
buffer.  There is no textual IR, assembly, external compiler/assembler,
previous stage executable, reference binary, or cached-answer path.

This is the PA9 adaptation of `spec.md` §§1–2 and §§6–10: CY86 has no
templates, overloads, classes, functions, or dependency scheduler, so those
checklist surfaces are not present.  Its typed statement model is the canonical
semantic/LowIR boundary, and each fixed-size CY86 operation is selected and
encoded directly rather than requiring a separate general machine IR.

## Findings

| Finding | Final disposition |
|---|---|
| Generic conversion recognition admitted opcodes absent from `cy86-opcode.desc` | Restricted recognition to the 170 declared opcodes; added a rejecting regression |
| Operand validation rejected legal raw literals, and widening only handled selected literal classes | Applied the specified truncate/sign-extend/zero-extend rule uniformly through 80 bits |
| Floating equality treated unordered x87 comparisons as equal | Masked parity for `feq`; NaN matrix now matches the reference |
| Statement parsing copied a suffix and then deep-copied the typed statement | Replaced by a bounded one-token checkpoint and move-only commit |
| Scalar literals allocated individually and every operand embedded several vector objects | Moved payloads to one offset-addressed compilation arena; operand size fell from 248 to 124 bytes |
| Opcode spellings were reparsed and the full descriptor copied into every statement | Cached classification by interned ID and retained one descriptor per distinct opcode; statements use 16-bit IDs |
| Label references, label addresses, fixups, parser scratch, semantic storage, and the final ELF image had avoidable duplicate ownership | Deduplicated unresolved labels, used dense address IDs, released phase-local state promptly, and streamed header plus code without a second image buffer |
| Backend entry selection looked up the rendered spelling `"start"` | Frontend records the selected start-label identity; lowering performs no name lookup |

No finding remains open.

## Changes

- Added exact opcode-table recognition, general literal-width conversion, and
  IEEE unordered handling for floating equality.
- Added regressions for an unsupported conversion and for raw float/address
  widening plus NaN equality; reference fixtures were generated through the
  documented `ref-test` target.
- Consolidated literals, opcode descriptors, label state, and backend fixups
  under explicit compilation/phase owners and exposed PA9-specific counters
  and per-phase timers through `CPPGM_FRONTEND_STATS=1`.
- Replaced the full ELF-image copy with direct header/content output and kept
  executable production entirely in-process.

## Performance Evidence

Final representative measurements (`CPPGM_FRONTEND_STATS=1`, `/usr/bin/time`):

| Workload | Tokens / statements / operands | Fixups / content | Frontend / lower / write | Max RSS |
|---|---:|---:|---:|---:|
| noop | 5 / 1 / 3 | 0 / 28 B | 0.98 / 0.003 / 0.063 ms | 6.8 MiB |
| integer calculator | 5,513 / 884 / 1,925 | 961 / 22,414 B | 7.21 / 0.18 / 0.12 ms | 7.3 MiB |
| floating calculator | 2,702 / 448 / 998 | 376 / 10,337 B | 3.98 / 0.10 / 0.07 ms | 7.3 MiB |

A generated label/fixup workload measured 10k, 20k, and 40k statements:
60.7, 131.5, and 272.6 ms elapsed with 9.3, 15.0, and 25.8 MiB RSS.
Tokens, operands, labels, fixups, code bytes, time, and memory all scale
linearly.  Against the pre-arena audit build, the 40k case improved from
294.2 ms / 37.7 MiB to 272.6 ms / 25.8 MiB.  A 40k
telemetry comparison was 0.27 s both disabled and enabled; enabled telemetry
added about 0.3 MiB.  No unexplained slow path remained to profile.

## Architecture Review

- Representation/ownership: one source buffer per active file, bounded
  preprocessor and statement scratch, one typed program, one literal arena,
  and one output byte buffer.  Borrowed callback bytes are copied only into the
  arena when semantics must retain them.  Parser state dies before lowering;
  fixups and semantic storage die before file writing.
- Identity/lookup: identifiers and opcodes have stable compact IDs; repeated
  opcode/register classification and label equality are O(1) average.  Dense
  vectors own definition/address facts, while hashing is confined to spelling
  interning and the currently unresolved label set.  Output order follows
  source vectors, not ordered semantic maps.
- Lowering/backend: typed operands and recorded opcode/start identities drive
  lowering directly.  Each statement and fixup is visited once; there is no
  whole-program retry, serializer/parser round trip, fallback lookup, external
  assembler, or repeated validator.
- Allocation/scaling: vectors grow geometrically, scalar literals do not
  allocate per node, and no hot node uses shared ownership.  Observable counts
  and the 1x/2x/4x workload show linear parsing, selection, fixup, and writing.
- Self-containment: a generated noop is an ELF64 `EXEC` with one `PT_LOAD`, no
  section table, and `ldd` reports `not a dynamic executable`.

## Final Architecture Review

The final source-to-ELF trace satisfies every PA9-applicable item in the
`spec.md` checklist.  The complete 170-opcode descriptor corpus compiles on one
unreachable-path smoke program and the generated executable exits zero.  The
PA9 suite also passes under AddressSanitizer plus UndefinedBehaviorSanitizer.
No correctness, self-containment, timeout, file-audit, architecture, ownership,
or performance blocker remains.

## Validation

- `perl scripts/cppgm_file_audit.pl --stage pa9 --paths dev/src` — pass, 35 files.
- `make test-report-through-pa9` — pass, 419/419 tests and 9/9 stages.
- PA9 sanitizer run — pass, 20/20 tests.
- Full opcode emission smoke — 170/170 declared opcodes accepted; executable exit 0.
- ELF inspection — direct static ELF64 executable, one load segment, no dynamic dependencies.

## Checkpoint Ledger

| Checkpoint | Result | Evidence |
|---|---|---|
| Original PA9 implementation | Complete | 18/18 PA9 and 417/417 through-stage baseline |
| Independent spec/README/source/commit reconstruction | Complete | End-to-end ownership, identity, lowering, ELF, and self-containment trace above |
| Correctness hardening | Complete | Exact conversions, general raw widths, NaN equality, two new course regressions |
| Architecture and ownership consolidation | Complete | Typed IDs, literal arena, bounded parser checkpoint, dense labels, released phase state, streamed ELF |
| Scaling and telemetry audit | Complete | Representative and 10k/20k/40k evidence above; no superlinear counter or timing growth |
| Final gates | Complete | File audit pass; through-PA9 419/419; sanitizer and ELF checks pass |
