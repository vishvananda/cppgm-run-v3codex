# Plan: Typed Compiler Failures and Non-Exception Recovery

Status: complete through E10 at `377775bb`; E9's cumulative self-build
regression has been corrected, and final correctness, performance, and 32-way
inception gates are recorded below

Date: 2026-09-01

## Objective

Give every compiler-controlled failure an explicit semantic disposition.  A
failure must be represented by either:

- a project-owned exception type when stack unwinding is the appropriate
  exceptional path; or
- an allocation-free status/result when failure is an expected alternative in
  successful compiler control flow.

No compiler decision may depend on `std::logic_error` versus
`std::runtime_error`, a catch-all that discards the exception, or diagnostic
message text.  Standard-library failures such as `std::bad_alloc` may still
originate outside compiler code, but internal boundaries must either let them
propagate or translate only the narrow standard exception documented for the
operation.

This is both a correctness and a performance program.  Merely replacing every
generic throw with a different class is insufficient: speculative parsing and
constant evaluation currently have recovery paths that can execute while
compiling a valid program.  Those paths must be counted on representative
workloads and changed to explicit status/result flow when they are frequent or
when their exception regions measurably harm hot code shape.  Conversely, a
cold malformed-input or invariant path must not be expanded into checks on
every successful operation merely to eliminate a throw.

The work must preserve:

- all student-facing behavior, exit status, and serialized syntax, semantic,
  LowIR, MIR, object, and executable contracts;
- the distinction between recoverable candidate failure, an ordinary source
  diagnostic, resource exhaustion, and a compiler invariant failure;
- the current PA38 file audit at zero fatal findings with no new advisory
  warning;
- current O1 and O3 generated-compiler performance, including the fully
  optimized GCC and Clang best-case controls; and
- exact 32-way inception at the final checkpoint.

## Governing rules

1. “Typed exception” means a project-owned type or compact typed payload whose
   category is sufficient for every catch-site policy decision.  It does not
   mean one RTTI class per message or per throw statement.
2. Diagnostic text is presentation only.  It may be printed by a terminal
   tool boundary, but it must never be compared, searched, parsed, or used to
   choose recovery behavior.
3. Expected alternatives in successful control flow are not exceptional.
   Parser no-match, failed speculative grammar alternatives, non-constant
   evaluation, and substitution failure use status/result flow if dynamic
   measurement shows they occur on valid workloads.
4. User input errors, I/O failures, resource limits, and internal invariants
   are different dispositions.  Do not preserve the current accidental
   `runtime_error`/`logic_error` split as the new taxonomy.
5. `std::bad_alloc` must propagate as allocation failure.  Never translate it
   into malformed input, a syntax miss, SFINAE, or a non-constant result.
   The two explicit `std::bad_alloc` throws in the LowIR pass allocator are
   required allocator-protocol behavior and remain standard typed failures.
   Catch `std::invalid_argument` and `std::out_of_range` only immediately
   around standard numeric conversions that document those failures.
6. A terminal `main` may retain a final `catch (const std::exception&)` solely
   to print an unexpected failure and return nonzero.  Internal policy code may
   not catch `std::exception`, `std::runtime_error`, or `std::logic_error`.
7. A cleanup-only `catch (...) { ...; throw; }` is not type-based routing, but
   it remains a large audit and code-shape surface.  Replace it with an
   existing or narrow RAII guard when that is clearer and performance-neutral.
   Any retained cleanup/rethrow site must be explicitly allowlisted; no
   catch-all may convert an unknown exception to `false`, invalid, deferred,
   or another ordinary result.
8. Keep the taxonomy compact.  Do not add hundreds of subclasses, virtual
   policy methods, dynamically allocated payload graphs, or a formatting
   framework.  Carry enums/IDs for machine decisions and construct detailed
   strings only on an actual error path.
9. This plan does not authorize a LowIR, MIR, ABI-fact, compiler-object, or
   command-line contract change.  Renaming or refining the C++ exception used
   by a parser changes no PA13 LowIR contract; changing accepted input or
   emitted output requires a separately justified earliest-owned behavior
   increment.
10. Course tests describe behavior a student can implement from the owning PA
    README.  They must not inspect production source, exception class names,
    exact diagnostic text, or a complete program fixture merely to prove an
    internal type migration.
11. One disposition family or bounded catch-site conversion per commit.  Run
    its fast checks before committing; run cumulative checks and push after no
    more than three retained commits, and immediately after a high-risk
    parser, constexpr, template, LowIR, optimizer, or native milestone.
12. Every inception command uses outer, inner, and object parallelism of 32.
    Before timings, verify that no stale Cachegrind, Valgrind, `perf record`,
    benchmark, or inception process remains.

## Starting checkpoint

The planning checkpoint is `f2d7a737` (`Align frontend source-set helper
names`).  The worktree is clean and synchronized with `origin/v3opt`.

The current architecture audits pass:

- `make audit-compiler-layout`: 475 production files, no legacy source;
- `make audit-frontend-source-sets`: 14 tools, 50 responsibility sets, and
  231 production sources;
- `make audit-lowir-contract`: 127 ledger rows, 102 retained rows, 140 enum
  values, 23 metadata keys, and 21 roles; and
- `perl scripts/cppgm_file_audit.pl --stage pa38 --paths dev`: zero fatal
  findings and the established 32 advisory warnings.

The built `dev/cppgm++` gives this initial exception/code-size guard:

| ELF section | Bytes |
| --- | ---: |
| `.text` | 6,623,398 |
| `.rodata` | 214,195 |
| `.eh_frame_hdr` | 50,820 |
| `.eh_frame` | 322,880 |
| `.gcc_except_table` | 164,929 |

Refresh the compiler and these numbers in E0.  They are a planning census,
not a permanent performance baseline.

## Frozen exception census

The production tree currently contains 2,895 explicit generic throws in 218
files:

| Area | `logic_error` throws | `runtime_error` throws | Existing typed throws |
| --- | ---: | ---: | ---: |
| ABI adapters and mangling | 61 | 1 | 0 |
| compiler-object serialization | 4 | 41 | 0 |
| CY86 frontend/backend and LowIR conversion | 26 | 13 | 0 |
| executable/driver sources | 52 | 52 | 0 |
| typed source-to-LowIR lowering | 379 | 125 | 0 |
| LowIR model, I/O, and support | 86 | 12 | 126 `lowir_model::ParseError` |
| namespace initialization | 14 | 136 | 0 |
| namespace semantics | 3 | 43 | 0 |
| native lowering and object emission | 248 | 80 | 0 |
| preprocessing | 44 | 76 | 0 |
| recognition | 9 | 3 | 0 |
| semantic analysis | 482 | 884 | 1 `HardSemanticError` |
| shared support | 1 | 1 | 0 |
| syntax | 7 | 12 | 89 `throw Error(...)` and 43 extension equivalents |
| **Total generic** | **1,416** | **1,479** | - |

Two additional explicit `std::bad_alloc` throws implement overflow/failure at
the LowIR optimizer's allocator boundary.  They are deliberate standard typed
exceptions rather than unclassified compiler failures and are retained.

The three apparent project exception types do not yet form a sound taxonomy:

- `lowir_model::ParseError` derives from `std::runtime_error`, but its 126
  throws mix malformed textual LowIR, failed output open/write operations,
  invalid compact identities, and internal consistency failures.
- `HardSemanticError` derives from `std::logic_error` so it bypasses a catch of
  `std::runtime_error`.  Its inheritance is being used as an undocumented
  routing bit rather than expressing a semantic disposition.
- `NotImplementedException` has three top-level catches but no producer.  It
  and `CPPGM_EXIT_NOT_IMPLEMENTED` are dead candidates unless an assignment
  contract proves otherwise.

The original planning search honored `dev/.gitignore`, whose `cy86` binary
entry also hides the tracked `dev/src/cy86/` and `dev/src/lowir/cy86/` source
directories from an ordinary recursive `rg`.  It also counted only same-line
throw expressions.  E0's filesystem walk found the omitted 39 CY86 throws,
one multiline semantic throw, four additional files, and one cleanup catch.
The corrected baseline above is authoritative.  E0 also records two generic-
return helpers (`missing_option_argument` and `ParserCursor::Error`), rather
than treating only the parser helper as part of the migration.

Catch-site census:

| Shape | Count | Current role and risk |
| --- | ---: | --- |
| `catch (...)` | 59 | 53 restore state/cleanup and rethrow; 6 swallow every failure as an ordinary invalid result |
| internal `catch (const std::runtime_error&)` | 4 | two speculative syntax paths, token-paste translation, and exception-specification state routing |
| internal `catch (const std::exception&)` | 3 | ABI numeric conversion and ABI line-context translation; these also catch `bad_alloc` |
| terminal generic standard catches | 15 | last-resort diagnostic/exit adapters in staged tools |
| terminal `NotImplementedException` catches | 3 | dead special-exit adapters unless a producer is found |

The six swallowing catch-all sites are:

- two scalar conversion/update paths in
  `semantic/analysis/analyzer.cpp`; and
- four constexpr construction/function-evaluation paths in
  `semantic/constants/scalar_evaluator.cpp`.

The other catch-all sites currently restore counters, contexts, scratch
storage, request states, or arena ownership and then rethrow.  They are not
classification sites, but their manual cleanup makes it difficult to prove
that a new typed failure cannot be lost.

There are 19 calls to `.what()`: 18 terminal diagnostic prints and one ABI
fact parser that prepends a line number.  The source audit found **no**
comparison, substring search, regex, or other branch on `.what()` or exception
message text.  The migration therefore does not need to preserve an existing
string-keyed policy, but the new architecture audit must prevent one from
appearing.

## Hot-control-flow finding

There is already proof that at least one exception is used as control flow on
a valid workload.  A retained Callgrind run of the successful frozen
`dev/src/preprocess/preprocessor.cpp` compile records:

- one call from `ParserCursor::Expect` through `__cxa_throw`;
- roughly 34,900 instructions attributed below `__cxa_throw`; and
- a total run of 3,705,297,621 instruction references.

That single unwind is below 0.002% of the run, so it is evidence of a real
valid-input throw, not evidence that exceptions currently dominate frozen-TU
time.  It does, however, invalidate the assumption that every compiler error
path is cold.  The two speculative parser catches deliberately use exceptions
to abandon an abstract declarator or parameter clause, and their broad type
can also absorb an unrelated runtime/resource failure.

The six constexpr catch-all sites are the second likely valid-input family.
“This expression is not a constant” is common, nonfatal compiler information,
especially during templates and constant-expression probing.  The retained
frozen profile did not show a second throw, so their dynamic importance is not
yet established; the full all-source workload and targeted PA21/PA23 corpora
must be counted before choosing the API boundary.

Most ABI, LowIR parse, command-line, token-paste rejection, and native
invariant throws are negative-input or compiler-failure paths and are expected
to remain cold.  For these, typed exceptions normally preserve the best hot
path.  Status-return conversion is justified only by dynamic use on successful
inputs or a repeated native code-shape win.

Even an unthrown exception can affect performance by adding landing pads,
LSDA/typeinfo, cleanup edges, and cold code to a hot function.  Prior optimizer
work also showed that exception-bearing call shape can affect inlining and
hot/cold layout.  E0 and every hot-flow conversion therefore measure both
dynamic throws and static `.text`, `.gcc_except_table`, `.eh_frame`, typeinfo,
and hot function size.  A source-level removal is not a win if it merely
replaces unwinding with more work on every successful call.

## Target failure model

Use the smallest taxonomy that supports actual handling policy.  The starting
design to validate in E1 is:

| Type/disposition | Meaning | Catch policy |
| --- | --- | --- |
| `CompilerError` | common terminal presentation base only | terminal adapters may print it; internal code does not recover on the base |
| `InvocationError` | invalid option, missing argument, unsupported driver mode | main/driver converts to nonzero exit |
| `InputOutputError` | source/object/LowIR open, read, write, rename, or link transport failure | operation boundary may add typed path/operation context, then rethrow |
| `SourceError` plus phase enum | lexical, preprocessing, recognition, or other non-semantic source rejection | corresponding staged or integrated terminal boundary |
| `SyntaxError` | committed malformed syntax with token/location facts | syntax terminal; never used for ordinary no-match after E2 |
| `SemanticError` | ordinary hard source-program rejection | semantic terminal or an explicitly documented semantic boundary |
| `HardSemanticError` | failure that must bypass candidate/substitution recovery | sibling of `SemanticError`, not a subclass caught as ordinary recovery |
| `SerializedInputError` plus format enum | malformed ABI facts, LowIR, or compiler-object input | only the relevant adapter boundary |
| `ResourceLimitError` | compiler-owned checked count/size limit | terminal; never converted to syntax miss/SFINAE/not-constant |
| `InternalCompilerError` plus domain/code | broken compiler invariant or impossible state | terminal; never recovered as invalid user input |

`std::bad_alloc` remains a standard allocation failure rather than being
wrapped at thousands of allocation sites.  A final terminal standard catch
can still report it.  Similarly, the standard exceptions thrown directly by
`stoll`/`stoull` are caught narrowly and translated into
`SerializedInputError`; unexpected exceptions propagate.

Machine-relevant context belongs in small enums and integer IDs.  Path,
token, and line data may be retained for deferred formatting, but constructors
must not allocate or concatenate a detailed diagnostic on a recoverable hot
miss.  Before adding a new subclass, demonstrate a catch site that needs to
distinguish it; otherwise add a compact domain/code to an existing type.

## Non-exception result model

Do not introduce one generic `Result<T>` framework across the compiler.  Use
owner-local result shapes with complete state transitions:

1. Syntax speculation distinguishes `matched`, `no_match`, and
   `committed_error`.  `no_match` rolls back without constructing text or
   unwinding.  `committed_error` carries enough token/location data to create
   a `SyntaxError` only when the caller cannot try another grammar arm.
2. Constant evaluation distinguishes at least `value`, `not_constant`, and
   `hard_error`.  Ordinary evaluation rules return `not_constant`; malformed
   compiler state, resource limits, and allocation failures propagate.  Reuse
   the existing `ConstexprFlow`/value carriers where possible rather than
   wrapping every scalar in a heap-bearing object.
3. Template substitution continues to use the existing explicit candidate
   failure state where it is already correct.  A hard semantic failure must
   not be encoded by choosing a different generic standard exception base.
4. A status conversion owns rollback/finalization.  Use a narrow scope guard
   for mutation that must be undone on every return, and commit the guard on a
   successful result.  Do not duplicate manual restoration across each arm.

The final choice for each family is data-driven:

| Observation | Required disposition |
| --- | --- |
| no throws on successful frozen/full/targeted corpora and no hot code-shape penalty | retain a typed cold exception |
| throws occur as an expected successful alternative | convert that recovery boundary to status/result |
| no dynamic throw, but removal repeatedly shrinks a hot body/EH table and improves native timing | retain the measured non-exception form |
| status form adds hot branches/code and has no repeatable benefit | revert it; keep the typed cold exception |

## Coverage audit and assignment ownership

Internal exception class names are not student-facing requirements.  Before
each migration, identify behavior coverage for every disposition that the old
catch could conflate:

| Family | Earliest behavior owner and downstream checks |
| --- | --- |
| tokenization and literal rejection | PA1/PA2 invalid-token behavior and exit-status rules |
| preprocessing and token paste | PA3-PA5 inactive/invalid directives, macro expansion, placemarkers, and invalid multi-token paste |
| recognition | PA6 grammar acceptance and rejection |
| speculative and committed syntax | PA10 abstract declarators, parameter clauses, casts/templates, malformed declarations, and positive ambiguity cases |
| semantic source rejection | PA11/PA12 positive and negative semantic cases |
| textual LowIR | PA13 malformed structure/metadata plus valid parse/serialize behavior |
| ABI fact input | PA14 malformed numeric values, duplicate IDs, valid graph/mangle behavior |
| lowering invariants and calls | PA15 through the first assignment owning the relevant language feature |
| constexpr/non-constant flow | PA21 positive constant evaluation, invalid required constants, and valid non-constant contexts |
| template/SFINAE recovery | PA19/PA22/PA23 selected and discarded candidates, dependent exception specifications, and hard-error cases |
| source-language exceptions | PA26 and PA31/PA33 runtime behavior; ensure compiler implementation failures are not confused with generated-program EH |
| driver, I/O, object, and link flow | PA30/PA31/PA36 valid compile/link behavior and negative exit status |
| optimizer validation | PA37 malformed LowIR, exact relevant structural properties, and generated behavior |
| native validation | PA38 malformed/unsupported MIR and object plus generated behavior |
| integrated compiler | through-PA38 report and PA39 32-way inception |

Use existing coverage when it exercises the relevant behavior.  If a broad
catch has hidden a real observable bug, first add a high-level requirement to
the earliest owning README and a focused course test that distinguishes the
behaviors by output structure, exit status, or generated behavior.  Never add
a fixture that recognizes the compiler source program, checks the exception
class/message, or exact-matches a complete LowIR/MIR program for this purpose.

The exception architecture itself is enforced by a developer audit, not a
course test.  Add `scripts/audit_compiler_exceptions.py` (or the existing
repository script language if reuse is clearer) and a root
`audit-compiler-exceptions` target.  The final audit must report:

- zero explicit compiler `throw std::logic_error` or
  `throw std::runtime_error` sites;
- zero helpers that construct/return those generic types for later throwing;
- zero internal catches of `std::exception`, `std::runtime_error`, or
  `std::logic_error` used for policy;
- zero catch-all sites that swallow, classify, or return an ordinary result;
- zero `.what()` comparisons/searches outside terminal presentation; and
- an explicit, reviewed list of standard-library translation sites,
  terminal fallback catches, and cleanup/rethrow catch-alls that remain.

This source audit validates an internal architecture rule and is not a
student implementation oracle.  It may inspect tokens/source patterns; course
tests may not.

## Performance protocol

### Metrics and lanes

Freeze a fresh same-revision baseline in E0.  At each performance checkpoint,
all producers compile the exact same candidate source and use the same fixed
workload.  Record compiler hashes, output hashes/manifests, lane order, wall,
aggregate CPU, peak RSS, and ELF section sizes.

Use these lanes:

1. Fast native frozen-TU guard at requested `-O0`, produced by current
   self-O1, self-O3, GCC-O3, and Clang-O3 compilers.  The fully optimized host
   lanes protect the approximately five-second best case as well as self-host
   performance.
2. Same-source 32-way full compiler builds at requested O1 and O3, comparing
   self-produced compilers with GCC- and Clang-produced controls.  Report both
   absolute time and normalized self/host ratios; a source change that makes
   every producer slower is not hidden as a relative win.
3. Targeted successful PA10, PA21, and PA23 batches for parser, constexpr, and
   substitution changes.  Record dynamic throw counts and time separately;
   these are diagnosis lanes, not substitutes for the full workload.
4. Exact 32-way inception at high-risk milestones and final closure.

Native interleaved timing is the normal oracle.  Use at least B/A/A/B for a
candidate expected to be close, repeat when aggregate CPU differs by less than
1%, pin the serial frozen lane when practical, and do not mix compile caches or
object roots.  Cachegrind is not an incremental gate; use Callgrind only after
native results are ambiguous and a deterministic instruction/call breakdown
would change the decision.

### Dynamic exception census

E0 builds a temporary instrumented compiler that intercepts `__cxa_throw` and
counts exception type plus a symbolizable caller address without printing per
throw.  Run it over:

- the frozen `preprocessor.cpp` compile;
- a clean all-32 full compiler source build;
- successful parser ambiguity/abstract-declarator cases;
- successful and rejected constexpr cases; and
- successful template deduction/SFINAE cases.

Aggregate once at process exit.  Instrumentation overhead is irrelevant to
timing, but the input/output behavior must remain exact.  Remove the wrapper
and rebuild normally before measuring.  After E1 introduces project types,
repeat the census by typed category.  Add narrow owner-local counters only
where return-address attribution cannot distinguish a recovery family.

### Retention thresholds

- A pure typed cold-path migration must be neutral: no repeatable increase
  greater than 0.5% in aggregate CPU or normalized full-build ratio, and no
  unexplained increase in fully optimized GCC/Clang frozen time.
- Treat changes within the established approximately 1% layout/noise band as
  close.  Repeat them and inspect section/hot-function movement rather than
  claiming a win or loss from one lane.
- `.gcc_except_table`, `.eh_frame`, RTTI/typeinfo count, total `.text`, and the
  affected hot-function sizes may not grow cumulatively without a measured,
  recorded benefit.  If many leaf subclasses increase RTTI/LSDA, collapse
  them to a compact typed domain/code.
- A valid-input status conversion must reduce dynamic throws in its target
  family and either improve repeatable native performance or reduce a proven
  hot EH/code-shape obstacle without regressing the full workload.
- Outputs and manifests from baseline/candidate producers on the same source
  must be exact for an internal-only change.  Investigate any difference; do
  not regenerate fixtures or edit compiler source inputs to conceal it.
- Reject and revert a candidate that repeatedly exceeds a 1% performance loss
  or changes behavior without a separately approved correctness fix.

## Execution program

### E0. Freeze correctness, size, timing, and throw-frequency baselines

1. Save the complete throw/catch/`.what()` census and exception section sizes.
2. Add the report-only form of `audit-compiler-exceptions` with an initial
   reviewed allowlist; do not enable a failing gate until migrations begin.
3. Run root through-PA38 and the existing architecture/file/LowIR audits.
4. Record fast frozen-TU and same-source full O1/O3 self/GCC/Clang baselines,
   including fully optimized GCC/Clang producers.
5. Run the temporary native exception census over frozen, full, PA10, PA21,
   and PA23 successful workloads.  Record type/caller counts in the execution
   ledger and remove all instrumentation.
6. Confirm no stale profiler/build process and remove completed temporary
   object roots after recording hashes/results.

Exit: one reproducible correctness/performance/size baseline and a dynamic
answer for which exceptions occur during successful compilation.

### E1. Establish the compact taxonomy and terminal boundaries

1. Replace `support/exceptions.h` with the compact project taxonomy after
   validating which distinctions have actual internal catch policy.
2. Give types compact phase/format/domain/code payloads.  Keep message
   formatting lazy/cold where the hot census warrants it.
3. Update staged and integrated `main` functions to catch `CompilerError`
   before a final standard fallback.  Preserve existing nonzero exits; remove
   the dead not-implemented exit only after assignment coverage confirms it is
   unobservable.
4. Add compile-time relationship checks for the critical sibling types: a
   hard semantic error must not be caught as ordinary semantic recovery, and
   internal/resource errors must not derive from recoverable syntax or
   constexpr categories.
5. Enable audit failures for message matching and new generic catches/throws,
   while grandfathering the remaining migration inventory by exact site.

Fast verification: build every affected staged tool, representative positive
and negative exit-status cases, architecture audit, file audit, and
`git diff --check`.

Exit: new code cannot add generic or string-keyed routing, terminal behavior
is unchanged, and the taxonomy has bounded RTTI/section cost.

### E2. Remove exception control flow from syntax speculation where justified

1. First make committed parser failures `SyntaxError` and narrow the two
   speculative catches so unrelated resource/internal failures propagate.
2. Count the abstract-declarator and parameter-clause miss/retry paths on the
   full and targeted PA10 corpus.
3. If a miss occurs on valid programs, introduce the owner-local
   `matched`/`no_match`/`committed_error` result.  Roll back through a compact
   mark guard and defer diagnostic construction until a committed error.
4. Do not convert every `Expect` to a status.  Terminal committed grammar
   errors remain typed exceptions unless code-shape measurement proves a
   broader conversion valuable.
5. Verify that arena nodes/edges/tokens, parser position, string ownership,
   and syntax output are exact after every retry.

Fast verification: focused PA10 positive ambiguity, abstract declarator,
parameter, cast/template, and malformed syntax tests; then
`make test-report-through-pa10`, exception audit, file audit, frozen native
throw count, and frozen self/GCC/Clang guard.

Exit: no broad runtime catch can turn resource/invariant failure into a parse
miss, and dynamically expected parser alternatives do not unwind.

### E3. Make constexpr failure explicit and preserve hard failures

Perform this in small owner slices rather than rewriting the evaluator at
once:

1. Classify every throw reachable by the six swallowing catch-all sites as
   `not_constant`, ordinary required-constant diagnostic, resource limit, or
   internal invariant.
2. Narrow the catch sites immediately so `bad_alloc`, resource failures, and
   internal errors cannot become `false`/`CONSTEXPR_FLOW_INVALID`.
3. Reuse/extend `ConstexprFlow` and scalar/object result carriers to return
   expected `not_constant` outcomes without exceptions.  Start with the
   highest-count scalar conversion/binary-operation family, measure, and then
   migrate constructor and function evaluation.
4. Replace manual scratch/context restoration with scoped guards where this
   prevents duplicated return cleanup and is performance-neutral.
5. Preserve required-constant diagnostics, discarded/non-evaluated behavior,
   storage identity, depth limits, object/member state, and memoized call
   states exactly.

Fast verification: focused PA21 positive, non-constant, invalid-required,
overflow/division, constructor, recursion/depth, and object cases plus PA23
dependent constexpr/SFINAE cases; through-PA21 or through-PA23 report as
appropriate; throw census; exception/file audits; frozen and full native
guard after each retained slice.

Exit: no catch-all converts an unknown exception into constant-evaluation
failure, and every dynamically common non-constant result is exception-free.

### E4. Correct semantic recovery and exception-specification routing

1. Preserve the existing explicit candidate-substitution status path; do not
   replace it with exceptions.
2. Convert ordinary semantic diagnostics and invariant checks to their typed
   dispositions in bounded semantic responsibility groups.
3. Replace `EnsureFunctionExceptionSpecification`'s
   `runtime_error`/catch-all inheritance trick with explicit ordinary-failed,
   hard/deferred, resource, and internal outcomes.  State transitions must be
   chosen from type/status, never message or generic standard base.
4. Audit every cleanup/rethrow catch in semantic analysis.  Move simple depth,
   context, stack, and scratch restoration to narrow RAII guards; explicitly
   allowlist the residual sites whose transactional state needs a catch.
5. Remove `HardSemanticError`'s dependency on `logic_error` inheritance while
   preserving static-assert and template-candidate behavior.

Suggested commit slices: declarations/lookup/types; expressions and overload
resolution; initialization/lifetime; templates/SFINAE; exception
specifications; remaining semantic invariants.

Fast verification follows the earliest owning PA for each slice, with PA12,
PA21, and PA23 cumulative checks at their milestones.  Run a full O1/O3
performance guard after templates/constexpr and a 32-way inception checkpoint
after the semantic family closes.

Exit: semantic recovery is explicit, hard failures cannot be swallowed as
candidate failure, and remaining semantic catch-alls only clean up and rethrow.

### E5. Type preprocessing, ABI adapters, and numeric translations

1. Make generated-token cardinality and token-paste rejection a preprocessing
   type (or direct status inside the paste helper), and remove the broad
   runtime translation.  Invalid paste remains an input error; allocation and
   invariant failures propagate.
2. Introduce `SerializedInputError(ABI_FACT, code, context)` for the PA14
   adapter.  `require` constructs it directly.
3. Catch only `std::invalid_argument` and `std::out_of_range` around
   `stoll`/`stoull`.  Add line number as typed context while retaining the
   original typed error; never catch/translate `bad_alloc`.
4. Migrate lexical, preprocessing, and recognition generic throws by behavior
   category without changing valid token streams or negative exit status.

Fast verification: PA1-PA6 positive/negative suites, focused token-paste
tests, PA14 valid/malformed/duplicate/numeric tests, through-PA14 report,
exception/file audits, and frozen fully optimized host guard.

Exit: early frontend and ABI adapters contain no generic policy catches or
throws, and malformed input cannot conceal allocation/internal failure.

### E6. Split LowIR and compiler-object failure categories

1. Classify every existing `lowir_model::ParseError` and generic LowIR throw
   as malformed serialized input, I/O, resource limit, or internal invariant.
2. Replace the overbroad parse type with the appropriate compact type/code.
   Preserve a compatibility alias only if a real staged C++ source contract
   requires it; PA13 diagnostic spelling is not such a contract.
3. Apply the same classification to compiler-object read/write/join paths.
4. Verify that integrated typed LowIR does not acquire a private exception
   field or bypass.  This is C++ control-flow cleanup, not a LowIR addition.
5. Keep parser/serializer context cold and avoid retaining full diagnostic
   strings inside successful LowIR objects.

Fast verification: PA13 malformed and valid structural behavior, PA14 ABI
consumer checks, PA30 object/link cases, PA37 malformed optimizer inputs,
PA38 native inputs, LowIR contract audit, exception audit, and exact
same-source outputs.

Exit: malformed external LowIR/object data, transport failures, limits, and
internal corruption are distinct and no public IR contract changed.

### E7. Migrate lowering, optimizer, native, driver, and remaining tools

Work by responsibility-named source group:

1. invocation, path, subprocess, compiler-driver, and link errors;
2. lowering user diagnostics versus typed-model invariants;
3. optimizer malformed-input guards, resource limits, and pass invariants;
4. native MIR validation, unsupported target representation, allocation/
   range limits, fixup/relocation invariants, and object I/O; and
5. remaining staged executable/model helpers.

Keep negative-only throws cold.  If a status conversion is proposed in a hot
loop, require E0-style dynamic evidence and an isolated native A/B.  Do not
turn every `require`/bounds check into a returned branch that callers must test
on successful operations.

Fast verification: the owning PA suite and through-target report for each
slice, PA37/PA38 structural and behavioral checks for backend work, exception
and file audits, exact output hashes, and frozen timing.  Run full O1/O3
same-source controls at the optimizer/native milestones.

Exit: every remaining compiler-controlled throw has a project-owned semantic
type, and backend hot paths have no unmeasured status overhead.

### E8. Close cleanup catches and enforce the architecture audit

1. Re-run the 59-site catch-all inventory.  There must be zero swallowing or
   classification catch-alls.
2. Replace straightforward manual depth/context/vector cleanup with RAII and
   record any code-size/performance-sensitive rejection.
3. For each residual cleanup/rethrow catch-all, document the exact state it
   restores and add it to the narrow audit allowlist.  It must end in `throw;`
   on every arm.
4. Remove dead `NotImplementedException` handling if E1 found no assignment
   owner; otherwise give its real producer a non-generic project disposition.
5. Turn `audit-compiler-exceptions` into a required root audit and run it as
   part of the cumulative report path.
6. Recount types, generic throws/catches, `.what()` uses, cleanup sites, RTTI,
   and EH sections and record the before/after table.

Exit: the audit is green without a broad migration allowlist and all remaining
catch-all/standard catches are reviewed terminal or cleanup boundaries.

### E9. Final correctness, performance, and inception closure

1. Run `make test-report-through-pa38` and every compiler architecture, source
   set, file, LowIR-contract, and exception audit.
2. Run focused invalid-input and successful recovery suites across PA1-PA38.
3. Run final native frozen and full O1/O3 self/GCC/Clang interleaved controls,
   including GCC-O3 and Clang-O3 best-case frozen lanes.
4. Re-run the native exception census without profiler distortion and confirm
   that no dynamically expected parser/constexpr/substitution alternative
   unwinds.  Correlate the census with per-stage/per-translation-unit timing
   so a zero-throw result does not conceal exception checks, cleanup regions,
   or typed helper code in hot ordinary control flow.
5. For every dynamically repeated exception, or any hot owner whose ordinary
   path regresses after its error-policy change, prototype an owner-local
   status/result alternative.  Retain non-exception flow when it is faster
   without weakening the typed terminal failure; otherwise record the A/B
   rejection and keep the cold exception boundary.  In particular, re-audit
   tokenizer, parser, constexpr/substitution, optimizer eligibility, and
   native selector/allocation paths rather than inferring cost from throw
   counts alone.  Inspect the successful side of each proposed result API as
   well: an out-parameter, default construction, move assignment, or repeated
   status branch is not an improvement merely because it avoids unwinding.
6. Run fresh PA39 inception with all three job counts set to 32 and require
   every object and final compiler to match.
7. Remove temporary profiling/build roots, confirm no stale process, complete
   the execution ledger, commit, and push.

Exit: all acceptance criteria below are satisfied at one pushed commit.

## Verification cadence

For every retained increment:

1. build the affected tools;
2. run focused positive and negative tests for every changed disposition;
3. run `make test-paN` and `make test-report-through-paN` for its owning PA;
4. run `make audit-compiler-exceptions`, the PA38 file audit, and
   `git diff --check`;
5. run the fast frozen native guard if exception construction, catch regions,
   hot parser/semantic/optimizer code, headers, or linked layout changed;
6. commit the independently verified increment; and
7. push with a cumulative report after no more than three retained commits.

Run the corrected full O1/O3 same-source oracle after E2, each major E3/E4
slice, E6, and optimizer/native portions of E7.  Run 32-way inception after
the semantic close, after backend close, and at final closure.  A rejected
candidate is reverted before beginning the next increment and recorded with
its correctness, size, and timing evidence.

## Acceptance criteria

The plan is complete only when:

1. every explicit compiler-controlled throw uses a project-owned typed
   disposition, except the two allocator-protocol `std::bad_alloc` throws; no
   raw `logic_error`/`runtime_error` throw or constructor helper remains;
2. no internal policy catches a generic standard exception and no catch-all
   swallows or classifies an unknown failure;
3. no code branches on exception message text;
4. `bad_alloc`, internal invariants, I/O/resource failures, hard semantic
   errors, ordinary source diagnostics, substitution failure, non-constant
   evaluation, and parser no-match cannot be confused;
5. every recovery path observed on successful workloads is status/result
   based, or has a recorded measurement showing that the typed cold exception
   is the faster/smaller representation;
6. no student-facing output, exit-status, LowIR, MIR, object, or behavior
   contract changes except separately documented correctness fixes with
   earliest-owned coverage;
7. the exception, layout, source-set, LowIR-contract, and PA38 file audits pass
   with zero new warning;
8. root through-PA38 passes and final 32-way PA39 inception is object- and
   compiler-exact;
9. self O1/O3, normalized GCC/Clang ratios, peak RSS, and fully optimized
   GCC/Clang frozen times have no repeatable regression beyond the declared
   thresholds; and
10. final `.text`, `.gcc_except_table`, `.eh_frame`, RTTI/typeinfo, dynamic
    throw counts, tests, timings, hashes, commit IDs, and rejected candidates
    are recorded in the ledger and pushed.

## Execution ledger template

Append one row for each retained or rejected increment:

| ID | Family/sites | Old ambiguity | New type/status | Earliest coverage | Dynamic throws before/after | Text/EH/RTTI delta | Fast/full timing | Correctness and hashes | Commit | Result |
| --- | --- | --- | --- | --- | ---: | --- | --- | --- | --- | --- |
| E0 | baseline and audit | generic standard categories and broad catches | report-only ceilings plus native throw census | PA1-PA39 | frozen 1; full 302; focused counts below | section baseline below | frozen/full baseline below | 5,473/5,473; output hashes below | `8b2216ff` | retained |
| E1 | taxonomy and terminal boundaries | standard bases and dead not-implemented exit route | compact disposition/domain types; typed terminal adapter before fallback | staged tools and PA10-PA38 integrated driver | valid-input throws unchanged; generic runtime sites -20 | +384 text, -4 exception table, +88 unwind | frozen wall 0.520/0.520 s | through-PA23 3,139/3,139; focused later suites pass | `e9bde193` | retained |
| E2 | syntax speculation | runtime exception used for type-id/parameter-clause retry | 8-byte matched/no-match/committed-error result; committed `SyntaxError` | PA10 dependent logical template argument and malformed syntax | frozen 1->0; full syntax 231->0; full total 302->71 | +640 text, -76 exception table, +136 unwind | frozen 0.520/0.520 s; full O1/O3 neutral below | through-PA10 586/586; 222 O1/O3 objects exact | `9109b2d9` | retained |
| E3a | constexpr constructor initializer access | catch-all converted a lost class context into ordinary non-constant flow | constructor class/function context spans arguments and base/member initializers | PA21 protected base construction in a required constant expression | full 71->0; all events were three valid lookup tables in `lowir/io/parse.cpp` | text/rodata/unwind unchanged; exception table -8 | frozen neutral; full O1 +0.34% CPU, O3 -0.24% CPU | through-PA21 2,416/2,416; PA23 414/414; 222 O1/O3 objects exact | `ee6d0cb8` | retained |
| E3b | scalar conversion and compound update | two catch-alls swallowed arithmetic failure, invariant failure, allocation, or any future type alike | recover only `SemanticError`; scalar invariants use `InternalCompilerError`; shared cold throw helpers | PA21 scalar/constexpr evaluation, including rejected required constants | successful full 0->0; catch-all sites 59->57 | -512 text, +32 rodata, -352 exception table, +296 unwind | frozen neutral; full O1 -0.23% CPU, O3 -0.31% CPU | PA21 150/150; 222 O1/O3 objects exact | `421d5c1c` | retained |
| E3c | constructor/function constexpr probe boundaries and selection | four catch-alls converted every unknown failure to non-constant; constructor selection was untyped | recover only `SemanticError`; overload failures are semantic and conversion-table exhaustion is resource | PA17 nonconstant class-array initialization; PA21 ordinary dynamic fallback after a failed constant probe | successful full 0->0; targeted fallback has one typed semantic throw; catch-all sites 57->53 | +128 text, +32 rodata, -48 exception table, +112 unwind | frozen neutral; full O1 -0.32% CPU, O3 -0.71% CPU | through-PA21 2,417/2,417; PA21 151/151; 222 O1/O3 objects exact | `4e933284` | retained |
| E4a | declarations, lookup, and semantic index tables | source diagnostics, resource ceilings, and invariants shared generic standard bases | `SemanticError`, semantic-domain `ResourceLimitError`, and `InternalCompilerError`; cold shared throw helpers | PA12 declarations and expressions, with cumulative PA21/PA23 recovery coverage | successful full remains 0; generic logic/runtime sites -43/-160 | -10,368 text, -32 rodata, -1,972 exception table, -32 unwind | frozen 0.520/0.520 s; full O1 -0.46% CPU, O3 -0.07% CPU | through-PA12 842/842; through-PA23 3,142/3,142; 222 O1/O3 objects exact | `5d25136f` | retained |
| E4b | expressions, calls, and overload resolution | expression rejection and failed overloads shared generic bases with representation limits and invariants | ordinary diagnostics use `SemanticError`; candidate failure stays status-based; limits and invariants bypass recovery | PA12 expressions/intrinsics; PA23 substitution and overload recovery | successful full remains 0; generic logic/runtime sites -28/-113 | -8,256 text, -1,328 exception table, +208 unwind | frozen 0.525/0.520 s; full O1 -0.08% CPU, O3 -0.45% CPU | PA12 184/184; PA23 414/414; through-PA23 3,142/3,142; 222 O1/O3 objects exact | `0bda33df` | retained |
| E4c | initialization and lifetime | construction/destruction diagnostics, capacity ceilings, and synthesized-state contradictions shared generic bases | typed semantic/resource/internal dispositions; substitution and constexpr status remain explicit | PA17 initialization/lifetime, PA21 constexpr, PA23 templates | successful full remains 0; generic logic/runtime sites -71/-125 | -14,336 text, -2,132 exception table, +200 unwind | frozen neutral; repeated full O1 -0.10% CPU, O3 +0.08% CPU | PA17 247/247; PA21 151/151; PA23 414/414; through-PA23 3,142/3,142; 222 O1/O3 objects exact | `e743a077` | retained |
| E4d | template arguments, deduction, placeholders, validation, and identity support | template diagnostics and retained-state invariants shared generic bases | terminal source diagnostics typed; candidate substitution remains explicit status; limits/invariants bypass it | PA23 template deduction/substitution and retained templates | successful full remains 0; generic logic/runtime sites -86/-125 | -768 text, -1,820 exception table, +168 unwind | frozen neutral; repeated full O1 -0.10% CPU, O3 -0.05% CPU | PA23 414/414; through-PA23 3,142/3,142; 222 O1/O3 objects exact | `63354ba4` | retained |
| E4e | class/function template formation and exception-specification cache | terminal template diagnostics and cache policy shared generic bases; runtime base selected permanent failure | typed semantic/resource/internal failures; only ordinary semantic disposition is cached failed, all other unwinds remain deferred | PA23 class/function templates and deferred exception-specification demand | successful full remains 0; generic logic/runtime sites -81/-73; internal runtime catches -1 | -11,840 text, -1,460 exception table, -376 unwind | frozen -1.0% user; repeated full O1 -0.14% CPU, O3 -0.17% CPU | PA23 414/414; through-PA23 3,142/3,142; 222 O1/O3 objects exact | `e9e719ee` | retained |
| E4f | constant evaluation, constexpr objects/addresses, and alignment | source rejection, representation limits, and evaluator invariants shared generic bases | typed semantic/resource/internal failures; non-constant probe results and cleanup/rethrow guards remain status-based | PA21 constant expressions, constexpr initialization, and required-constant diagnostics | successful full remains 0; generic logic/runtime sites -48/-27 | -2,112 text, -1,068 exception table, -216 unwind | frozen neutral; full deferred to next periodic semantic checkpoint | PA21 151/151; through-PA21 2,417/2,417; frozen object exact | `56214019` | retained |
| E4g | class layout, inheritance, virtual dispatch, RTTI, and object attributes | object diagnostics, ABI/resource ceilings, and graph invariants shared generic bases | typed semantic/resource/internal failures; cleanup catches still rethrow | PA17 object model plus cumulative PA18-PA21 inheritance/RTTI behavior | successful full remains 0; generic logic/runtime sites -39/-57 | +1,856 text, +32 rodata, -920 exception table, +336 unwind | frozen neutral; clean mirrored full O1 -0.16% CPU, full O3 -0.34% CPU | PA17 247/247; through-PA21 2,417/2,417; 222 O1/O3 objects exact | `64bfa60b` | retained |
| E4h | language/host extensions, builtins, range-for, source exceptions, and ABI tags | extension diagnostics, limits, and retained syntax invariants shared generic bases | typed semantic/resource/internal failures; candidate and cleanup status flow unchanged | PA23 builtin/template behavior and PA26 source-language exceptions | successful full remains 0; generic logic/runtime sites -44/-129 | -11,264 text, -1,856 exception table, +264 unwind | frozen neutral; full covered by adjacent periodic checkpoints | PA23 414/414; PA26 114/114; through-PA26 3,821/3,821; frozen object exact | `c3f1bf9d` | retained |
| E4i | semantic graph/model and source presentation | model diagnostics, capacity, and identity invariants shared generic bases | typed semantic/resource/internal failures; semantic generic-throw census reaches zero | PA12 graph/type model and cumulative PA23 presentation behavior | successful full remains 0; semantic generic logic/runtime sites -36/-41 | -19,136 text, -4,424 exception table, -696 unwind | frozen exact tie; clean mirrored full O1 -0.09% CPU, O3 -0.52% CPU | PA12 184/184; PA23 414/414; through-PA26 3,821/3,821; 32-way inception exact | `3bc47eb5` | retained |
| E4j | semantic state restoration | 49 semantic catch-alls manually restored counters, values, or container depth | 39 simple regions use scoped restoration; 10 transactional/scratch cleanup-and-rethrow regions remain explicit | PA12, PA17, PA21, PA23, PA26 and cumulative semantic behavior | successful full remains 0; catch-all sites -39 | -896 text, -840 exception table, neutral unwind | frozen -1.04% user; 32-way full O1 -0.04% CPU, O3 -0.09% CPU | PA12 184/184; PA17 247/247; PA21 151/151; PA23 414/414; PA26 114/114; through-PA26 3,821/3,821; 222 O1/O3 objects and inception exact | `7f46e61e` | retained |
| E5a | PA14 ABI fact adapter | broad standard catches translated numeric, allocation, encoder, and I/O failures alike | coded `SerializedInputError` with line context; only invalid/range numeric exceptions translate; typed I/O/internal propagation | PA14 normalized/malformed facts and cumulative through-PA14 behavior | successful full remains 0; generic logic sites -24; internal standard catches -3 | +192 text, +4 exception table, +104 unwind | frozen -1.04% user | PA14 117/117; through-PA14 1,081/1,081; frozen object exact | `538b79f7` | retained |
| E5b | lexical input and preprocessing-token paste | generic lexical failures plus broad runtime translation for generated-token cardinality | lexical source/internal types; direct one-token status; preprocessing source failure; explicit cursor inlining boundary | PA1 source tokenization and PA4 valid/invalid paste behavior | successful full remains 0; generic logic/runtime sites -5/-20; internal runtime catches -1 | -16,000 text, -64 rodata, -2,104 exception table, -280 unwind | frozen -6.19% user; full O1 -1.91% CPU, O3 -2.34% CPU | PA1 53/53; PA4 75/75; through-PA4 174/174; 222 O1/O3 objects exact | `71d6deba` | retained |
| E5c | post-tokenization, recognition, and hosted intrinsic registry | API/invariant, embedded-grammar, resource-limit, and invalid-token failures shared generic bases | lexical/recognition internal, recognition resource, and lexical source dispositions; explicit shared string-flush boundary | PA2 literal/token behavior, PA5 preprocessing, PA6 invalid-token and grammar acceptance | successful frozen remains 0; generic logic/runtime sites -26/-3 | -640 text, +96 rodata, -200 exception table, -48 unwind | frozen user median 0.450/0.450 s; paired +0.55% noise | PA2 26/26; PA5 70/70; PA6 48/48; through-PA6 292/292; frozen objects exact | `8311aeb4` | retained |
| E5d | PA3-PA5 macro processor | source, invocation, transport, resource, and state-machine failures shared generic bases | preprocessing source/I/O/resource/internal dispositions and driver invocation type; expected probes/invocation alternatives remain status flow | PA3 controlling expressions, PA4 macro replacement, PA5 directives/includes | successful frozen remains 0; generic logic/runtime sites -22/-56 | -4,800 text, +96 rodata, -708 exception table, +136 unwind | frozen paired user -0.56%; full O1 -0.85%, O3 -0.29% CPU | PA3 20/20; PA4 75/75; PA5 70/70; PA6 48/48; through-PA14 1,082/1,082; 222 O1/O3 objects and 32-way inception exact | `7cf04f4b` | retained |
| E6a | LowIR text, model identity, serialization, and PA13 CY86 adapter | one parse type mixed malformed input, I/O, limits, and invariants; model helpers used generic bases | format/domain-specific serialized-input, I/O, resource, invocation, and internal failures; valid flow unchanged | PA13 malformed/valid text, identity, role, metadata, phi, and adapter behavior | successful frozen remains 0; 126 `ParseError` sites removed; generic logic/runtime sites -41/-9 | -14,528 text, -64 rodata, +56 EH header, -168 unwind, -2,536 exception table | frozen user -1.12%; full O1 -0.39%, O3 -0.26% CPU | PA13 122/122; through-PA13 965/965; LowIR contract unchanged; 222 O1/O3 objects exact | `23294aa9` | retained |
| E6b | compiler-object serialization, ELF import, and join | malformed bytes, I/O, size ceilings, link conflicts, and invariants shared generic bases | compiler-object serialized-input/I/O/resource/internal types; no-input and target mismatch are invocation failures; probes stay status-based | PA30 separate/direct/mixed compilation, helper ELF import, duplicate/missing/unresolved link behavior | successful frozen remains 0; generic logic/runtime sites -4/-41 | -5,568 text, +128 rodata, -72 EH header, -480 unwind, -2,060 exception table | frozen 0.450/0.450 s; full O1 -0.23%, O3 neutral CPU | PA30 100/100; through-PA30 4,355/4,355; malformed/I/O fail; 222 O1/O3 objects exact | `fd1c98ff` | retained |
| E6c | integrated typed-LowIR adapter | lowering-model identity, bounds, operation, CFG, EH, and presentation invariants used a generic logic base inside adapter loops | LowIR-domain internal failures through the existing shared cold boundary; successful checks unchanged | PA15 source-to-LowIR plus PA37 optimizer and PA38 native consumers | successful frozen remains 0; generic logic sites -23 | -1,280 text, -16 EH header, -72 unwind, -180 exception table | frozen 0.450/0.450 s; full O1 -0.39%, O3 -0.09% CPU | PA15 121/121; PA37 190/190; PA38 45/45; through-PA38 5,477/5,477; 222 O1/O3 objects and 32-way inception exact | `9ab20e02` | retained |
| E7a | LowIR analyses and optimizer invariants | invocation rejection, call-graph/CFG/SSA corruption, and inliner shape contradictions shared generic logic/runtime bases in hot optimizer owners | invalid inline overrides use typed invocation failure; true optimizer invariants use one cold optimizer-domain boundary; ordinary optimization rejection remains status flow | PA37 invocation, structural, optimization-level, inlining, specialization, and generated-behavior controls | successful frozen remains 0; generic logic/runtime sites -23/-3 | -2,048 text, +32 rodata, +16 EH header, -8 unwind, -240 exception table | frozen 0.450/0.450 s and paired -0.55%; full O1 -0.18%, repeated O3 +0.39% CPU (neutral) | PA37 190/190; through-PA37 5,432/5,432; audits pass; frozen and 222 O1/O3 objects exact | `7e9450a0` | retained |
| E7b | integrated compiler driver | invocation, source-token, file/library transport, compiler-object rejection, and unreachable driver states shared generic bases; missing-option helper returned a generic exception object | cold driver invocation/I/O/source/internal boundaries plus compiler-object input type; file-kind and library-path probes remain non-exception status flow | PA30 object/compile/link failures and PA36 hosted compile/link behavior | successful frozen remains 0; generic logic/runtime sites -23/-22; generic return helper -1 | -3,968 text, +32 rodata, +40 EH header, +176 unwind, -212 exception table | frozen 0.450/0.450 s; paired +0.56% (neutral) | PA30 179/179; PA36 1/1; through-PA36 5,242/5,242; invalid option/missing input/unwritable output fail; audits and frozen object exact | `181b1270` | retained |
| E7c | lowering core, typed IR, reachability, identity, rendering, and presentation | source/transport failures, fixed-ID ceilings, and graph/identity/render contradictions shared generic bases in hot LowIR construction owners | typed lowering invocation/I/O/source/resource/internal failures through one cold boundary; successful construction checks unchanged | PA15 typed source-to-LowIR structure and cumulative LowIR contracts | successful frozen remains 0; generic logic/runtime sites -55/-22 | -832 text, +32 rodata, +152 EH header, +336 unwind, -3,680 exception table | cumulative frozen baseline/candidate user 0.445/0.450 s; paired +0.01% (neutral) | PA15 121/121; through-PA15 1,203/1,203; audits and frozen object exact | `071ecc3a` | retained |
| E7d | lowering ABI fact construction and symbol mangling | semantic recipe/identity invariants, checked cache capacity, and unsupported source representation shared generic bases in a per-symbol hot owner | lowering-domain internal/resource/source failures through the existing cold boundary; optional owner/substitution probes remain Boolean | PA14 ABI spelling plus PA15 source-to-LowIR ABI metadata | successful frozen remains 0; generic logic/runtime sites -68/-5 | -1,920 text, -32 rodata, -16 EH header, -136 unwind, -764 exception table | cumulative frozen 0.450/0.450 s; paired neutral | PA14 117/117; PA15 121/121; through-PA15 1,203/1,203; frozen object exact | `567b2fde` | retained |
| E7e | call, constructor, special-member, intrinsic, and value-boundary lowering | call/cleanup graph contradictions, unsupported object source shapes, and fixed argument/extent limits shared generic bases | lowering internal/source/resource types through the cold boundary; builtin/call eligibility stays Boolean and cleanup routing unchanged | PA15 calls, PA17 object construction/lifetime, and PA26 destruction/EH behavior | successful frozen remains 0; generic logic/runtime sites -65/-10 | -5,056 text, neutral rodata, -72 EH header, -352 unwind, -876 exception table | cumulative frozen 0.445/0.445 s; paired -0.54% (neutral) | PA15 121/121; PA17 247/247; PA26 114/114; through-PA26 3,822/3,822; frozen object exact | `6fb546a1` | retained |
| E7f | constants, control, expression, and extension lowering | CFG/EH contradictions, unsupported source shapes, and fixed graph/count limits shared generic bases | lowering internal/source/resource types through the cold boundary; semantic checkpoint, operator, and initializer eligibility remains ordinary flow | PA15 control/expression lowering, PA16 lifetime/goto policy, PA23 extensions, and PA26 EH behavior | successful frozen remains 0; generic logic/runtime sites -53/-42 | -11,136 text, +32 rodata, +40 EH header, +56 unwind, -1,456 exception table | cumulative frozen 0.450/0.450 s; paired +1.67% (noise) | PA15 121/121; PA16 58/58; PA23 414/414; PA26 114/114; through-PA26 3,822/3,822; frozen object exact | `61011cc8` | retained |
| E7g | object initialization, arrays, temporaries, and cleanup lowering | semantic-dump contradictions, unsupported initializer shapes, and fixed cleanup/extent identities shared generic bases | lowering internal/source/resource types through the cold boundary; optional initialization and cleanup decisions remain ordinary flow | PA15 initialization, PA17 object lifetime, and PA26 cleanup/EH behavior | successful frozen remains 0; generic logic/runtime sites -57/-28 | -6,528 text, +32 rodata, neutral EH header, -144 unwind, -916 exception table | cumulative frozen 0.450/0.450 s; paired +0.56% (neutral) | PA15 121/121; PA17 247/247; PA26 114/114; through-PA26 3,822/3,822; frozen object exact | `a184f038` | retained |
| E7h | storage, static lifetime, RTTI, member-pointer, and thunk lowering | emitted-symbol/storage/RTTI facts and semantic action shapes shared generic bases | lowering internal/source types through the cold boundary; storage lookup and object-model applicability remain ordinary flow | PA15 storage, PA16 static lifetime, PA18 polymorphism, and PA22 member pointers | successful frozen remains 0; generic logic/runtime sites -22/-9 | -5,760 text, neutral rodata, -16 EH header, -128 unwind, -692 exception table | cumulative frozen user 0.460/0.450 s; paired -1.09% (neutral) | PA15 121/121; PA16 300/300; PA18 37/37; PA22 311/311; through-PA26 3,822/3,822; frozen object exact | `fb0bb947` | retained |
| E7i | polymorphic layout, construction VTT, and virtual-base lowering | vtable/VTT/RTTI graph contradictions and finite slot ceilings shared generic bases | lowering internal/resource types through the cold boundary; hierarchy traversal and optional ABI facts remain ordinary flow | PA18 polymorphism and PA22 object-model behavior | successful frozen remains 0; generic logic/runtime sites -41/-2 | -4,608 text, neutral rodata, -16 EH header, -104 unwind, -404 exception table | cumulative frozen 0.450/0.450 s; paired neutral | PA18 37/37; PA22 311/311; through-PA26 3,822/3,822; frozen object exact | `b1aafa26` | retained |
| E7j | force-inline lowering transform | cloned LowIR identity/CFG contradictions and finite temp/slot/block ceilings shared generic bases | lowering internal/resource types through the cold boundary; candidate selection and recursive rejection remain ordinary flow | PA37 forced-inlining structure and cumulative optimizer behavior | successful frozen remains 0; generic logic/runtime sites -18/-7; lowering generic census reaches zero | -128 text, neutral rodata/EH header/unwind, +12 exception table | cumulative frozen 0.450/0.450 s; paired neutral; full O1 +0.53%, O3 -0.17% CPU | PA37 190/190; through-PA37 5,432/5,432; 222 O1/O3 objects and final binaries exact | `c8f7b6f5` | retained |
| E7k | native driver, MIR model/optimizer, allocation, layout, and frame planning | invocation/I/O, malformed LowIR facts, resource ceilings, and allocation/MIR invariants shared generic bases | native invocation/I/O/LowIR-input/resource/internal types through one cold boundary; allocation decisions remain ordinary flow | PA38 native structural and generated-behavior surface | successful frozen remains 0; generic logic/runtime sites -23/-10 | -128 text, neutral rodata, +40 EH header, -160 unwind, -600 exception table | cumulative frozen 0.450/0.450 s; paired neutral | PA38 45/45; through-PA38 5,477/5,477; frozen object exact | `808ea131` | retained |
| E7l | native instruction, operand, ABI, intrinsic, phi, and wide-value lowering | malformed LowIR combinations, unsupported target forms, allocation limits, and lowering invariants shared generic bases in hot selectors | native LowIR-input/source/resource/internal types through the cold boundary; selection and allocation alternatives remain ordinary flow | PA38 native structural and generated-behavior surface | successful frozen remains 0; generic logic/runtime sites -15/-40 | -320 text, +192 rodata, neutral EH header, -320 unwind, -1,096 exception table | cumulative frozen 0.450/0.450 s; paired neutral | PA38 45/45; through-PA38 5,477/5,477; frozen object exact | `aac2b352` | retained |
| E7m | native host-EH analysis/LSDA and x86 encoding | MIR/region/encoding contradictions and one EH range ceiling shared generic logic base in hot backend owners | centralized native internal/resource/source cold boundary; encoder availability and EH traversal remain ordinary flow | PA38 native EH, encoding, structural, and generated behavior | successful frozen remains 0; generic logic sites -62 | -768 text, -64 rodata, -8 EH header, -360 unwind, -988 exception table | cumulative frozen user 0.440/0.445 s; paired +0.56% (neutral) | PA38 45/45; through-PA38 5,477/5,477; frozen object exact | `c87b815a` | retained |
| E7n | native code buffer, relocations, fixups, labels, and ELF string table | symbol/label/relocation invariants, unresolved source symbols, and finite offset/identity ranges shared generic bases | native internal/source/resource types through the centralized cold boundary; fixup resolution and branch relaxation remain ordinary flow | PA38 native object, relocation, encoding, and generated behavior | successful frozen remains 0; generic logic/runtime sites -50/-19 | -2,368 text, neutral rodata, -24 EH header, -672 unwind, -1,064 exception table | cumulative frozen 0.450/0.450 s; paired neutral | PA38 45/45; through-PA38 5,477/5,477; frozen object exact | `afcdcde4` | retained |
| E7o | ELF section, symbol, relocation, COMDAT, and host-object layout | object-layout identities/invariants, undefined alias input, and 16-bit section ceiling shared generic bases | native internal/source/resource types through the centralized cold boundary; section/symbol lookup remains ordinary flow | PA38 native object, EH relocation, COMDAT, and generated behavior | successful frozen remains 0; generic logic/runtime sites -43/-2 | -256 text, neutral rodata, -16 EH header, -192 unwind, -456 exception table | cumulative frozen user 0.450/0.455 s; paired +1.67% (noise) | PA38 45/45; through-PA38 5,477/5,477; frozen object exact | `41736c1d` | retained |
| E7p | final MIR-to-x86 and executable/object writer | encoder/MIR contradictions, unsupported opcode/source entry, target invocation, and output I/O shared generic bases | native internal/source/invocation/I/O types through the centralized cold boundary; opcode dispatch remains ordinary flow | PA38 native executable/object, encoding, and generated behavior | successful frozen remains 0; generic logic/runtime sites -55/-9; native generic census reaches zero | -6,272 text, neutral rodata, -96 EH header, -632 unwind, -1,172 exception table | frozen paired +0.56%; isolated full O1 +0.08%, O3 +0.01% CPU | PA38 45/45; through-PA38 5,477/5,477; 223 O1/O3 objects and final binaries exact | `c1c2e8ca` | retained |
| E7q | PA7 namespace semantics/parser | source, syntax, semantic, resource, and namespace-model invariant failures shared generic bases | typed lexical/syntax/semantic/resource/internal exits; speculative declarator and declaration probes remain Boolean | PA7 namespace declaration, lookup, declarator, and invalid-input exit behavior | no recovery catch or valid-input unwind; generic logic/runtime sites -3/-43 | nsdecl: -1,408 text, +32 rodata, +64 EH header, +264 unwind, -535 exception table; integrated compiler exact | not linked into integrated compiler; no timing exposure | PA7 43/43; through-PA7 335/335; audits pass; integrated compiler binary exact | `a9f4b15a` | retained |
| E7r | PA8 namespace initialization/parser/image model | syntax and semantic rejection, image I/O, capacity limits, and model/image invariants shared generic bases | typed lexical/syntax/semantic/I/O/resource/internal exits; parser probes remain Boolean | PA8 namespace initialization, semantic rejection, linking, and mock-image behavior | no recovery catch or valid-input unwind; generic logic/runtime sites -14/-136 | nsinit: -13,888 text, +32 rodata, +112 EH header, -48 unwind, -2,030 exception table; integrated compiler exact | not linked into integrated compiler; no timing exposure | PA8 67/67; through-PA8 402/402; audits pass; integrated compiler binary exact | `091d3579` | retained |
| E7s | integrated syntax arena, attributes, name facts, brace matches, and spelling interning | syntax/resource/invariant failures shared generic bases in hot parser support | typed lexical/syntax/resource/internal cold exits; attribute and name/declarator probes remain status flow | PA10 syntax structure, attributes, templates, ambiguity, and malformed-input behavior | successful frozen remains 0; generic logic/runtime sites -8/-13 | -704 text, neutral rodata, +64 EH header, +152 unwind, -296 exception table | frozen user medians 0.460/0.455 s; mean +0.83%, one-tick noise | PA10 165/165; through-PA10 587/587; frozen object exact; audits pass | `f8ae46f1` | retained |
| E7t | ABI model, vocabulary, presentation, and mangler | ABI fact/model/encoding/resource/I/O failures shared generic bases; two catch-alls restored active graph cases | centralized ABI fact-input/internal/resource/I/O exits; scoped graph-case cleanup; ordinary cache/substitution lookup remains status flow | PA14 valid, malformed, numeric, model, and mangling behavior | successful frozen remains 0; generic logic/runtime -37/-1; catch-all -2 | +2,176 text, neutral rodata, +40 EH header, -288 unwind, -924 exception table | 16-run frozen mean exactly neutral at 0.45125/0.45125 s | PA14 117/117; through-PA14 1,082/1,082; frozen objects exact; audits pass | `b4433caf` | retained |
| E7u | PA9 CY86 frontend, backend, model, and ELF writer | CY86 source rejection, capacities, output I/O, and backend/model invariants shared generic bases | centralized CY86 source/resource/I/O/internal exits; parsing/opcode lookup stays status flow; one transactional cleanup/rethrow retained for E8 review | PA9 valid/invalid CY86 and generated native behavior | no recovery catch or valid-input unwind; generic logic/runtime -25/-13 | cy86: +3,328 text, neutral rodata, +72 EH header, +384 unwind, -1,265 exception table; integrated compiler exact | not linked into integrated compiler; no timing exposure | PA9 20/20; through-PA9 422/422; audits pass; integrated compiler binary exact | `9b2da99f` | retained |
| E7v | remaining staged executable adapters | invocation, output I/O, and one invalid phase-7 token used generic terminal throws | driver/optimizer/native invocation, driver I/O, and lexical source dispositions; terminal presentation unchanged | PA3-PA9, PA13/14, PA37/38 staged behavior | generic logic/runtime -29/-10; repository generic census reaches zero | eleven staged binaries aggregate: +5,152 text, -52 rodata, +208 EH header, +1,016 unwind, +140 exception table; integrated exact | adapters not on integrated hot path; no timing exposure | through-PA38 5,477/5,477; all 11 invalid invocations fail; audits pass; integrated compiler exact | `a78bb76a` | retained |
| E8 | semantic and allocator rollback; architecture enforcement | twelve catch-alls duplicated cleanup or assigned failure/cache states around unknown exceptions | scoped cleanup/commit owns scratch, contexts, cache state, optimizer storage, and CY86 table rollback; typed semantic exception-specification policy remains explicit | PA9, PA12, PA21, PA23, PA26, PA37 and cumulative behavior | successful frozen remains 0; catch-all sites 12->0 | -256 text, neutral rodata/EH header, -16 unwind, -292 exception table; exception RTTI remains 20 symbols | frozen baseline/candidate user means 0.4525/0.4550 s across semantic slice and 0.4525/0.4500 s across allocator slice (timer noise) | through-PA38 5,477/5,477; focused suites and audits pass; file audit zero-fatal/32 warnings; frozen object exact | `282c12e9` | retained |
| E9 | exception declaration/linkage consolidation and final hot-flow audit | the complete taxonomy and inline constructors crossed most throw-helper headers; zero dynamic throws did not explain a preprocessing code-shape regression | lightweight throw declarations, explicit full-type includes, one out-of-line taxonomy owner, centralized domain helpers, and a host-neutral cursor boundary; expected alternatives remain status flow and cold failures remain typed exceptions | PA1/PA4 lexical and paste behavior; PA13/14/30/37/38 serialized, optimizer, object, and native behavior; cumulative PA1-PA38 | frozen 0->0; final 221-source census 0; 226 typed throws and zero generic/catch-all/message-policy sites | host compiler -10,048 text, -64 rodata, -400 EH header, -3,392 unwind, -604 exception table; 20 RTTI symbols unchanged | E8/current frozen user 0.784/0.766 s; E8/current full-O3 compile CPU 736.12/736.25 s (neutral); final self/GCC/Clang controls below | 5,477/5,477; zero-fatal/32-warning file audit; frozen hash `8545fec6...`; 223-object inception and final hash `4398825a...` exact | `44fc67de` | retained |
| E10 | fixed lexical lookahead invariant failures | the string-only internal-error constructor forced either an opaque noinline helper call or expanded string/EH setup; the helper form prevented ordinary loop and terminal-query simplification even though it was never called | a compact `const char*` internal-error constructor keeps the failure typed while three fixed-queue invariants throw it directly; other lexical failures retain the shared cold helper | PA1/PA4 tokenizer and paste behavior plus the exception audit; cumulative PA1-PA38 | successful full remains 0; typed direct throws +3 and helper calls -3 | self O3 -688 text, +144 unwind, -384 exception table; no taxonomy or LowIR contract change | same-source full O1 -2.75% CPU and O3 -2.24%; normalized controls below | 5,477/5,477; 223 O1/O3 objects exact in baseline/candidate runs; O3 32-way inception hash `ed507c8e...` exact | `377775bb` | retained |

For status conversions, also record the result-state truth table and rollback
owner.  For retained catch-alls, record the exact cleanup invariant and why an
RAII conversion was rejected.  A row saying only “tests pass” or “typed” is
not sufficient evidence.

### E0 execution record

E0 added `audit-compiler-exceptions` and a repeatable native
`__cxa_throw` census.  The audit walks the filesystem rather than honoring
ignore patterns.  This exposed the `dev/.gitignore` `cy86` entry hiding tracked
source from the planning `rg` census and froze the corrected totals recorded
above: 1,416 logic throws, 1,479 runtime throws, 218 files, two generic-return
helpers, 59 catch-all sites, four internal runtime catches, three internal
standard catches, and zero exception-message policy sites.  The audit passes
at these E0 ceilings and will be ratcheted down by later phases.

Root `make -j32 test-report-through-pa38` passes 5,473/5,473.  The layout,
frontend-source-set, LowIR-contract, and PA38 file audits also pass; the file
audit remains zero-fatal with the established 32 warnings.  The unchanged
GCC-O3 `dev/cppgm++` is `0f6e3861...` and gives the planning section baseline:
6,623,398 `.text`, 214,195 `.rodata`, 50,820 `.eh_frame_hdr`, 322,880
`.eh_frame`, and 164,929 `.gcc_except_table` bytes.

The native throw census produces no per-event I/O and aggregates by mangled
type and ASLR-relative caller PC at process exit.  Its E0 successful-workload
results are:

| Workload | Successful cases/TUs | Dynamic throws | Attribution |
| --- | ---: | ---: | --- |
| frozen `preprocessor.cpp` | 1 | 1 | `ParserCursor::Expect` |
| full O1 compiler source | 222 | 302 | 231 `ParserCursor::Expect`; 71 `SelectConstructor` “inaccessible constructor” |
| PA10 positive syntax | 157 | 0 | none |
| PA21 positive constexpr | 128 | 0 | none |
| PA23 positive template/SFINAE | 397 | 3 | two `ParserCursor::Expect`; one `ParseParameterClause` |

The 71 constructor throws are recovered while compiling valid compiler source,
so E3's constexpr/status work is justified even though the smaller positive
PA21 corpus does not trigger it.  The rejected PA21 and PA23 batches contained
21 and 17 cases and produced exactly one terminal exception per case,
including one `HardSemanticError` in each batch.  Those negative-path events
are not status-conversion targets.

Three position-varied frozen requested-O0 samples per producer are output-
exact at object `8545fec6...` and 98,736 bytes:

| Producer | Median wall | Median user | Producer hash |
| --- | ---: | ---: | --- |
| self O1 | 0.98 s | 0.93 s | `747d6051...` |
| self O3 | 0.76 s | 0.71 s | `5846b8b0...` |
| GCC O3 | 0.52 s | 0.48 s | `470b9adf...` |
| Clang O3 | 0.62 s | 0.58 s | `acdfaa1a...` |

Fresh one-lane, all-32 full controls provide the E0 orientation baseline.
Later retention decisions still use interleaved B/A/A/B comparisons rather
than treating these single lanes as sub-percent evidence:

| Requested workload / producer | Wall | Aggregate CPU | Ratio versus host | Output compiler |
| --- | ---: | ---: | ---: | --- |
| O1 / self O1 | 31.94 s | 880.04 s | 1.50945x GCC; 1.46514x Clang | `747d6051...` |
| O1 / GCC O1 | 21.16 s | 560.27 s | control | `747d6051...` |
| O1 / Clang O1 | 21.80 s | 579.48 s | control | `747d6051...` |
| O3 / self O3 | 26.84 s | 723.79 s | 1.47392x GCC; 1.32022x Clang | `5846b8b0...` |
| O3 / GCC O3 | 18.21 s | 468.25 s | control | `5846b8b0...` |
| O3 / Clang O3 | 20.33 s | 530.96 s | control | `5846b8b0...` |

The corresponding aggregate-CPU self/host ratios are 1.57074x/1.51867x at
O1 and 1.54573x/1.36317x at O3.  Every fixed workload contains 222 objects;
all three producers at a requested level reproduce the same final compiler.
The census preload is diagnostic-only and is absent from every timing lane and
production binary.

### E1 execution record

E1 introduces one compact `CompilerError` base carrying disposition, domain,
and a 16-bit code, plus only the policy-level subclasses named in the target
model.  `SemanticError` and `HardSemanticError` are siblings; resource and
internal failures cannot derive from syntax recovery.  Compile-time assertions
freeze those relationships.  `HardSemanticError` no longer relies on
`logic_error` inheritance.  The project base derives directly from
`std::exception`, so neither legacy `runtime_error` nor `logic_error` recovery
can accidentally absorb a typed compiler failure.

All 15 staged/integrated terminal standard fallbacks now have a preceding
`CompilerError` adapter.  The three dead `NotImplementedException` catches,
their unused type, and exit-86 constant are removed.  The exception audit now
fails on any reintroduction of that route or any terminal standard fallback
without a typed adapter, while retaining the E0 generic migration ceilings.
The file audit exposed five copies of the staged source loader and timestamp
option builder after the terminal includes changed.  Those copies now have one
`preprocess/tool_support` owner used by the five tools.  Its four environment
failure sites are typed at that boundary, reducing the generic runtime census
from 1,479 to 1,459 without changing successful control flow.

Every affected tool builds.  Focused PA1, PA10, PA14, PA21, PA23, PA37, and
PA38 suites pass 53/53, 164/164, 117/117, 149/149, 485/485, 17/17, and 83/83;
the cumulative through-PA23 report passes 3,139/3,139.  Architecture and file
audits remain clean at their established counts.

A fresh GCC-O3 source-matched comparison against the E0 commit changes
`.text` 6,623,462 -> 6,623,846 bytes, `.rodata` 214,208 -> 214,176,
`.eh_frame_hdr` 50,820 -> 50,844, `.eh_frame` 322,880 -> 322,968, and
`.gcc_except_table` 164,928 -> 164,924.  The project typeinfo population stays
at two: `CompilerError` replaces dead `NotImplementedException`.  Eight ABBA
frozen samples give identical 0.520-second wall and 0.475-second user medians,
with exact 98,736-byte object `8545fec6...`; paired wall and user ratios favor
the candidate by 0.48% and 1.04%, both within this lane's timer granularity.
The taxonomy is therefore retained as a neutral correctness boundary, not
claimed as a performance win.

### E2 execution record

E2 changes every committed parser failure produced by `ParserCursor::Error`
from a generic runtime error to `SyntaxError`.  The two syntax catches are
removed rather than merely narrowed.  The exception audit ratchets its generic
return-helper count from two to one and its internal runtime catches from four
to two; neither remaining catch is in syntax.

The measured retry is owned by the parameter-clause/type-id parser and now
uses one register-sized `ParserAttempt`:

| State | Representation | State and rollback owner |
| --- | --- | --- |
| matched | syntax node plus `PARSER_MATCHED` | declarator attaches the completed clause |
| no match | `kNoNode` plus `PARSER_NO_MATCH` | caller retains the pre-probe position |
| committed error | `kNoNode` plus a compact expected-token/parameter/default code | ordinary caller constructs `SyntaxError`; speculative declarator rolls back its parameter mark and outer declarator mark before returning no-match |

Diagnostic strings are selected only after a committed result.  The carrier
is statically fixed at eight bytes; an earlier padded node/status/string form
was tightened before retention because its first two full-source screens were
about 0.25% higher in aggregate CPU despite removing unwinds.

The native census proves the control-flow change.  Frozen `preprocessor.cpp`
falls from one parser throw to zero.  The complete 222-object O1 workload falls
from 302 throws to 71: all 231 `ParserCursor::Expect` events disappear and only
the already identified constructor-selection family remains.  A malformed
PA10 parameter-list case still exits through exactly one `SyntaxError`.

PA10 now documents dependent qualified operands in parenthesized logical
template arguments and owns a behavioral AST fixture for that syntax.  The E1
compiler accepts the fixture while performing one speculative throw; E2 emits
the same AST without throwing.  This is grammar/output coverage that a student
can implement from the handout, not inspection of exception classes or source
text.  Focused PA10, PA21, and PA23 suites pass 165/165, 149/149, and 414/414;
the cumulative through-PA10 report passes 586/586.  All architecture audits
pass and the file audit remains zero-fatal with 32 established warnings.

Against the E1 GCC-O3 producer, E2 changes `.text` 6,623,846 -> 6,624,486,
`.rodata` 214,176 -> 214,208, `.eh_frame_hdr` 50,844 -> 50,876,
`.eh_frame` 322,968 -> 323,104, and `.gcc_except_table` 164,924 -> 164,848.
Three frozen ABBA blocks remain exact at object `8545fec6...`; baseline and
candidate wall medians are both 0.520 seconds and user medians are 0.480 and
0.475 seconds.  The paired wall movement is +0.97%, within the serial timer's
single-tick band.

Fresh 32-way final-carrier full controls reproduce all 222 objects at each
level.  O1 baseline/candidate are 18.89/18.41 seconds wall and 517.06/513.97
seconds aggregate CPU; O3 is 18.43/18.31 wall and 516.90/517.63 aggregate CPU.
Thus the only unfavorable full metric is +0.14% O3 CPU, while O1 CPU improves
0.60%; neither level shows a material regression.  E2 is retained for typed
committed failures and elimination of proven successful-control-flow unwinds,
not claimed as a broad throughput optimization.

### E3a execution record

The remaining 71 successful-workload exceptions all came from one translation
unit, `lowir/io/parse.cpp`.  The optional
`CPPGM_EXCEPTION_CENSUS_TAG` field now identifies the source workload in each
process-level census record, which localized every event to the three static
`std::pair<const char*, enum>` lookup tables for operations, LowIR types, and
symbol roles.  A debug stack showed that constant initialization entered the
selected `std::pair` constructor, then tried to construct its internal base
while `current_class_context_` was `kNoEntity`.  `SelectConstructor` therefore
reported an inaccessible constructor, and
`EvaluateConstexprConstructorInitializers`' catch-all silently classified the
analyzer's lost context as an ordinary non-constant result.

Constructor arguments and base/member initializers are evaluated in the
constructor's class and function context.  E3a now installs that context before
invocation arguments and the initializer plan, retaining it through the body,
and restores the outer context at the existing single cleanup point.  This is
a semantic correction rather than a source-specific bypass.  PA21 documents
the rule and adds a behavioral protected-base constexpr fixture: the E2
compiler rejects its `static_assert`, while E3a accepts it and emits the
reviewed ordinary `main` LowIR.  No test inspects exception classes, messages,
or production source.

The full 220 shared-source O1 census drops from 71 throws to zero.  The affected
`lowir/io/parse.cpp` object remains exact at `1fcf8c53...`, and source-matched
O1 and O3 controls reproduce all 222 objects.  The GCC-O3 compiler has identical
`.text`, `.rodata`, `.eh_frame_hdr`, and `.eh_frame`; only
`.gcc_except_table` falls from 164,848 to 164,840 bytes.  Eight frozen samples
remain exact at `8545fec6...`, with 0.520-second wall and 0.480-second user
medians for both compilers.

The 32-way full B/A/A/B guard is neutral.  At requested O1, baseline/candidate
average wall is 19.560/20.025 seconds and aggregate CPU is 486.785/488.420
seconds (+0.34% CPU; wall includes one candidate outlier).  At requested O3,
wall is 19.685/19.690 seconds and CPU is 491.040/489.855 seconds (-0.24%).
PA21 passes 150/150, through-PA21 passes 2,416/2,416, and PA23 passes 414/414;
the architecture audits remain clean and the PA38 file audit retains exactly
32 warnings.  All six swallowing constexpr sites, including this now-cold
initializer site, still require explicit classification/status work before E3
closes.

### E3b execution record

The two scalar catches in compound assignment were not observed in the frozen,
full-source, PA21-positive, or PA23-positive successful censuses.  Adding a
status branch to every constant scalar conversion or arithmetic operation is
therefore not justified by the hot-control-flow rule.  E3b instead makes the
cold distinction explicit: range, overflow, zero-divisor, and invalid-shift
outcomes are `SemanticError`; impossible scalar kinds, widths, normalized
forms, and operator dispatches are `InternalCompilerError`.  The assignment
sites catch only `SemanticError` and convert that known arithmetic outcome to a
non-constant local.  Allocation, internal, hard semantic, and resource failures
now propagate.  This removes two swallowing catch-alls and ratchets the audit
from 59 to 57; generic logic/runtime throws fall by 6/25 and the generic-file
census falls from 218 to 217.

Direct construction of the project error at every throw site was screened and
rejected before retention: it added 4,480 bytes of text and 244 bytes of
exception table.  Two shared cold, no-inline helpers preserve the typed
disposition and lazy error construction while merging the repeated cold path.
Against E3a the final carrier changes `.text` 6,624,486 -> 6,623,974,
`.rodata` 214,208 -> 214,240, `.eh_frame_hdr` 50,876 -> 50,956,
`.eh_frame` 323,104 -> 323,400, and `.gcc_except_table` 164,840 ->
164,488.  No new RTTI type is introduced.

Eight frozen samples are exact at `8545fec6...` and remain in the same
0.52-0.53-second timer band.  The 32-way full B/A/A/B guard reproduces all 222
objects.  Requested O1 baseline/candidate average wall is 19.585/19.435
seconds and aggregate CPU is 488.415/487.280 seconds (-0.23%); requested O3
wall is 19.905/19.830 seconds and CPU is 492.930/491.390 seconds (-0.31%).
PA21 passes 150/150 and all architecture audits pass at the ratcheted ceilings.
Four swallowing constexpr construction/function-evaluation catches remain.

### E3c execution record

The four constructor-plan, constructor-evaluation, constructor-body, and
constexpr-function catches now recover only `SemanticError`.  `bad_alloc`,
`InternalCompilerError`, `ResourceLimitError`, `HardSemanticError`, and any
remaining untyped failure propagate instead of becoming `false` or
`CONSTEXPR_FLOW_INVALID`.  The first focused run passed, while the required
cumulative report exposed PA17's ordinary global class-array initialization:
its non-required constant probe legitimately finds no viable default
constructor and must fall back to dynamic initialization.  Constructor deleted,
explicit-copy, inaccessible, no-viable, and ambiguous outcomes are therefore
typed as `SemanticError`; conversion-table exhaustion is
`ResourceLimitError`.  PA17 then passes 247/247 and through-PA21 passes
2,417/2,417.  The exception audit ratchets from 57 to 53 catch-alls and from
1,434 to 1,425 generic runtime throws; all 53 remaining catch-alls are
cleanup-and-rethrow rather than result classification.  The full
successful-source census remains at zero throws.

PA21 now states and behaviorally tests the complementary language rule: a
failed constant-initialization probe for an ordinary namespace object falls
back to dynamic initialization, while a required constant context is rejected.
The fixture uses a valid C++11 `constexpr` division whose zero-divisor call is
not required to be constant.  It emits the expected dynamic initializer, and
the census observes exactly one `SemanticError` recovered by the function
probe.  This deliberately cold invalid-constant case does not justify a status
branch on every constexpr expression; the measured successful compiler and
PA21-positive workloads have no such unwind.

Against E3b, the final GCC-O3 carrier changes `.text` 6,623,974 -> 6,624,102,
`.rodata` 214,240 -> 214,272, `.eh_frame_hdr` 50,956 -> 50,980,
`.eh_frame` 323,400 -> 323,512, and `.gcc_except_table` 164,488 -> 164,440.
Eight frozen samples remain exact at `8545fec6...` and 0.52-0.53 seconds.  The
32-way full comparison reproduces all 222 objects.  Requested O1 B/A/A/B
baseline/candidate wall is 20.235/19.500 seconds and aggregate CPU is
488.970/487.410 seconds (-0.32%; the first baseline wall lane is noisy).  O3
was repeated because its first block straddled a load change; across all eight
interleaved lanes baseline/candidate wall is 19.992/19.985 seconds and CPU is
497.553/494.015 seconds (-0.71%).  PA21 passes 151/151 and PA23 passes 414/414.
E3 therefore closes with zero swallowing catch-all sites and zero dynamically
common valid-input unwinds.

### E4a execution record

The first semantic slice covers declaration analysis, name-path parsing,
switch-entry validation, and the compact semantic index tables.  It converts
160 generic runtime throws and 43 generic logic throws.  Ordinary rejected
programs now use `SemanticError`; checked table cardinality and layout overflow
use semantic-domain `ResourceLimitError`; and broken identities, impossible
ownership, invalid compiler-produced alignment, and missing cached facts use
`InternalCompilerError`.  The explicit candidate-substitution status path is
unchanged.  In particular, no catch was broadened: the previously established
constexpr probes can recover only the newly classified ordinary semantic
diagnostics, while resource and internal failures bypass them.

Cold noinline string overloads centralize construction for the semantic and
internal types instead of repeating exception setup at each concatenated
diagnostic.  The helper dependency remains explicit in the implementation
files rather than leaking through the semantic model header.  The exception
audit ratchets from 1,410/1,425 generic logic/runtime throws in 217 files to
1,367/1,265 in 206 files.  Catch and message-policy counts are unchanged.

PA12 passes 184/184, through-PA12 passes 842/842, and the cumulative
through-PA23 report passes 3,142/3,142.  The PA38 file audit retains its
established 32 warnings and no fatal finding.  Against `4e933284`, the matched
GCC-O3 carrier changes `.text` 6,624,102 -> 6,613,734, `.rodata`
214,272 -> 214,240, `.eh_frame_hdr` 50,980 -> 51,020, `.eh_frame`
323,512 -> 323,440, and `.gcc_except_table` 164,440 -> 162,468.  The resource
helper takes literal text directly, avoiding a redundant allocation while
reporting resource exhaustion.  Twelve final frozen samples are exact at
`8545fec6...`; both wall and user medians are 0.520 and 0.480 seconds.

The source-matched 32-way full guard reproduces all 222 objects.  Requested
O1 baseline/candidate aggregate CPU averages 493.355/491.080 seconds (-0.46%);
requested O3 averages 498.850/498.525 seconds (-0.07%).  Wall time rose with
machine load across each block, so aggregate CPU and the interleaved ordering
are the retention signal.  The final refinement reclassified three impossible
states and moved includes from the model header to direct owners.  Those cold
classification changes do not alter a successful control-flow path; the final
resource-helper refinement accounts for the additional size reduction above.

### E4b execution record

Expression construction, builtin calls, member pointers, unary/binary
operators, conversion functions, and overload resolution now use the typed
semantic taxonomy.  The slice converts 113 generic runtime throws and 28
generic logic throws.  Candidate type formation, overload failure, and
expression failure keep their existing explicit substitution status whenever
a candidate owner is active; only the non-candidate terminal arm constructs a
`SemanticError`.  This preserves the no-unwind SFINAE path established before
the exception migration.

Checked overload/callable table products, retained string-literal storage, and
member-pointer representation bounds use semantic-domain
`ResourceLimitError`.  Missing selected facts, invalid cached identities, and
impossible builtin/member state use `InternalCompilerError`; ordinary invalid
operands, inaccessible/deleted selections, and ambiguous overloads remain
source diagnostics.  The four expression catch-alls are unchanged
cleanup-and-rethrow regions; none classifies or swallows a typed failure.

PA12 passes 184/184, PA23 passes 414/414, and through-PA23 passes
3,142/3,142.  The exception audit ratchets to 1,339 generic logic throws and
1,152 generic runtime throws in 199 files.  Against `5d25136f`, `.text`
changes 6,613,734 -> 6,605,478, `.rodata` stays 214,240,
`.eh_frame_hdr` 51,020 -> 51,068, `.eh_frame` 323,440 -> 323,600, and
`.gcc_except_table` 162,468 -> 161,140.  Twelve frozen outputs are exact at
`8545fec6...`; baseline/candidate wall medians are 0.525/0.520 seconds and
both user medians are 0.480 seconds.

The source-matched 32-way full guard reproduces all 222 requested-O1 and O3
objects.  O1 baseline/candidate aggregate CPU averages 493.625/493.235 seconds
(-0.08%); O3 averages 493.680/491.460 seconds (-0.45%).  The slice is therefore
neutral-to-favorable in both dynamic lanes while removing another 9,584 bytes
of text and exception-table data.

### E4c execution record

Initialization, aggregate/list initialization, constructor/destructor
lifetime, local statics, and special-member analysis now distinguish ordinary
source rejection from resource ceilings and compiler-state contradictions.
The slice converts 71 generic logic throws and 125 generic runtime throws.
Candidate construction and the previously narrowed constexpr recovery paths
remain unchanged; resource and internal siblings continue to bypass ordinary
semantic recovery.

Exception-type storage, local-static ordinals, empty-constructor dependency
indices, aggregate-helper identity/prefix bounds, and compile-time array
allocation representation limits use semantic-domain `ResourceLimitError`.
Conflicting synthesized helper values and duplicate namespace backing
destructors are internal invariants.  Invalid initialization, unavailable or
inaccessible special members, and non-destructible source types remain
`SemanticError`.  The two catch-alls in this group only restore an evaluation
depth or class context and rethrow; they do not classify a failure.

PA17 passes 247/247, PA21 passes 151/151, PA23 passes 414/414, and the
cumulative through-PA23 report passes 3,142/3,142.  The exception audit
ratchets to 1,268 generic logic throws and 1,027 generic runtime throws in 189
files.  Against `0bda33df`, `.text` changes 6,605,478 -> 6,591,142,
`.rodata` stays 214,240, `.eh_frame_hdr` 51,068 -> 51,116, `.eh_frame`
323,600 -> 323,752, and `.gcc_except_table` 161,140 -> 159,008.  Twelve frozen
outputs are exact at `8545fec6...` and remain within the 0.52-second timer
band.

Because the first full block was within 1%, a second mirrored B/A/A/B block
was run.  All 222 requested-O1 and O3 objects match in both blocks.  Across
eight lanes per optimization, O1 baseline/candidate aggregate CPU averages
489.830/489.330 seconds (-0.10%), while O3 averages 494.320/494.698 seconds
(+0.08%).  This is neutral under the pure typed-migration guard and removes
another 16,468 bytes of text and exception-table data.

### E4d execution record

Template arguments, deduction, aliases/lambdas, placeholders, pack handling,
retained-template validation, ABI-result identity, and supporting indices now
use typed terminal failures.  The slice converts 86 generic logic throws and
125 generic runtime throws.  All active candidate-substitution paths continue
to record and return their explicit owner-local status; no generic or typed
exception is introduced as the successful SFINAE mechanism.

Template/partial/proxy/ABI identity counts, parameter ordinals, zero-cardinality
alignment representation, and static-member definition indices use
semantic-domain `ResourceLimitError`.  Retained syntax and invalid explicit
specializations use `SemanticError`, while impossible stored ranges, owner
indices, and cached identity shapes use `InternalCompilerError`.  Existing
catch-alls in this group restore candidate stacks, contexts, or scratch state
and rethrow; none returns an ordinary substitution result from an exception.

PA23 passes 414/414 and through-PA23 passes 3,142/3,142.  The exception audit
ratchets to 1,182 generic logic throws and 902 generic runtime throws in 175
files.  Against `e743a077`, `.text` changes 6,591,142 -> 6,590,374,
`.rodata` stays 214,240, `.eh_frame_hdr` 51,116 -> 51,180, `.eh_frame`
323,752 -> 323,856, and `.gcc_except_table` 159,008 -> 157,188.  Frozen output
is exact at `8545fec6...`; user-time medians are both 0.480 seconds.

The first full block was within 1%, so a mirrored second block was run.  All
222 requested-O1 and O3 objects match in all lanes.  Across eight lanes per
optimization, O1 baseline/candidate aggregate CPU averages 488.695/488.200
seconds (-0.10%); O3 averages 493.078/492.855 seconds (-0.05%).  The template
infrastructure migration is dynamically neutral and removes another 2,588
bytes of text and exception-table data.

### E4e execution record

Class and function template formation now separate terminal source rejection,
semantic-domain representation ceilings, and retained-state contradictions.
The slice converts 81 generic logic throws and 73 generic runtime throws.  It
does not change candidate substitution: expected discarded alternatives still
use the existing owner-local status instead of throwing.

Dependent function exception specifications no longer use
`std::runtime_error` inheritance as cache policy.  The typed handler caches
only `SEMANTIC` failures as permanently failed.  Hard-semantic diagnostics,
resource exhaustion, internal failures, and other project dispositions restore
the retryable deferred state and rethrow.  The remaining catch-all solely
restores that state for allocation or foreign exceptions and rethrows; it does
not convert an exception into a semantic result.  The PA23 handout already
describes typed substitution status and deferred instantiation, and
`300-function-template-exception-demand.t` behaviorally checks lazy demand,
successful specialization reuse, and recursive class completion without
depending on compiler source or diagnostic text.

PA23 passes 414/414 and through-PA23 passes 3,142/3,142.  All architecture
audits pass, including a ratchet to 1,101 generic logic throws, 829 generic
runtime throws in 173 files, and one remaining internal runtime catch.  The
PA38 file audit remains at zero fatal findings and 32 established warnings.
Against `63354ba4`, `.text` changes 6,590,374 -> 6,578,534, `.rodata` stays
214,240, `.eh_frame_hdr` 51,180 -> 51,156, `.eh_frame` 323,856 -> 323,504,
and `.gcc_except_table` 157,188 -> 155,728.  Twelve frozen objects remain exact
at `8545fec6...`; baseline/candidate user medians are 0.480/0.475 seconds.

The initial full block was within 1%, so a mirrored B/A/A/B block was run.
All 222 requested-O1 and O3 objects match in all lanes.  Across eight lanes
per optimization, O1 baseline/candidate aggregate CPU averages
488.263/487.565 seconds (-0.14%), and O3 averages 492.643/491.805 seconds
(-0.17%).  The result is dynamically neutral and favorable in code size.

### E4f execution record

The remaining generic failures in constant evaluation, constexpr object and
address storage, static constant recipes, and alignment analysis now use typed
terminal outcomes.  The slice converts 48 generic logic throws and 27 generic
runtime throws.  Invalid source constants and required-constant diagnostics
use `SemanticError`; address/object/floating/static fact capacity uses
semantic-domain `ResourceLimitError`; impossible stored identities, scratch
ranges, and evaluation-stack states use `InternalCompilerError`.

Expected non-constant probes remain explicit boolean/result flow.  The seven
catch-alls in this slice restore scratch arenas, depth counters, or suppression
state and rethrow; none classifies an unknown exception as non-constant.  They
remain cleanup candidates for E8.  PA21's handout describes the constant-result
model, and its behavioral fixtures cover constant success, dynamic fallback,
and required-constant rejection without inspecting diagnostic text or compiler
implementation.

PA21 passes 151/151 and through-PA21 passes 2,417/2,417.  The exception audit
ratchets to 1,053 generic logic throws and 802 generic runtime throws in 168
files.  Against `e9e719ee`, `.text` changes 6,578,534 -> 6,576,422,
`.rodata` stays 214,240, `.eh_frame_hdr` 51,156 -> 51,204, `.eh_frame`
323,504 -> 323,240, and `.gcc_except_table` 155,728 -> 154,660.  Twelve frozen
objects remain exact at `8545fec6...`; baseline/candidate user medians are
0.475/0.470 seconds and paired timing is neutral.  The next combined semantic
checkpoint owns the periodic full O1/O3 measurement.

### E4g execution record

Class layout, inheritance paths, polymorphic views, virtual-base indexing,
RTTI, casts, and object-section attributes now separate source diagnostics,
semantic/ABI representation ceilings, and retained graph contradictions.  The
slice converts 39 generic logic throws and 57 generic runtime throws.  Layout,
path, virtual-slot, and runtime-offset capacity use semantic-domain
`ResourceLimitError`; invalid C++ casts, access, overrides, RTTI use, and object
attributes use `SemanticError`; impossible cached identities and graph shapes
use `InternalCompilerError`.

The existing catch-alls in inheritance/RTTI paths only restore current-class
or conditionally-evaluated context and rethrow.  They do not select a semantic
fallback.  PA17 passes 247/247 and through-PA21 passes 2,417/2,417, covering
layout and inheritance behavior through later RTTI/constant-expression uses.
The exception audit ratchets to 1,014 generic logic throws and 745 generic
runtime throws in 156 files.

Against `56214019`, `.text` changes 6,576,422 -> 6,578,278, `.rodata`
214,240 -> 214,272, `.eh_frame_hdr` 51,204 -> 51,284, `.eh_frame`
323,240 -> 323,496, and `.gcc_except_table` 154,660 -> 153,740.  Frozen output
remains exact at `8545fec6...`; baseline/candidate user medians are
0.480/0.475 seconds.

The periodic full checkpoint used an A/B/B/A block and a mirrored B/A/A/B
block.  Every one of the 222 requested-O1 and O3 objects is exact.  The first
O1 baseline lane was an isolated 512.89-second aggregate-CPU load outlier; its
paired baseline returned to 487.30 seconds and the outlier is not treated as a
candidate win.  The clean mirrored O1 block averages baseline/candidate
490.350/489.570 seconds (-0.16%).  Across all eight O3 lanes the averages are
494.693/493.008 seconds (-0.34%).  The code-shape tradeoff is therefore retained
as dynamically neutral.

### E4h execution record

The remaining extension-layer failures in ABI tags, compiler/host builtins,
numeric and pack extensions, range-for, lambdas, GNU asm, control regions, and
source-language exception analysis now use project-owned types.  The slice
converts 44 generic logic throws and 129 generic runtime throws.  Invalid
extension syntax and source-language constraints use `SemanticError`; ABI-tag,
exception-context, capture-fact, range hidden-object, vector-width, and
generated-pack capacity use semantic-domain `ResourceLimitError`; impossible
retained syntax and context-stack states use `InternalCompilerError`.

Candidate substitution and dependent builtin results continue to use their
existing explicit status forms.  None of these terminal throws selects a
successful builtin or template alternative, and existing cleanup catches only
restore context and rethrow.  PA23 passes 414/414, PA26 passes 114/114, and
through-PA26 passes 3,821/3,821.  The exception audit ratchets to 970 generic
logic throws and 616 generic runtime throws in 142 files.

Against `64bfa60b`, `.text` changes 6,578,278 -> 6,567,014, `.rodata` stays
214,272, `.eh_frame_hdr` 51,284 -> 51,364, `.eh_frame` 323,496 -> 323,680,
and `.gcc_except_table` 153,740 -> 151,884.  Twelve frozen objects remain exact
at `8545fec6...`.  One first-position baseline warm-up measured 0.510 seconds;
the remaining baseline/candidate medians are 0.480/0.475 seconds and do not
show a credible regression.  Full timing is bracketed by the adjacent E4g
periodic checkpoint and the next combined semantic checkpoint.

### E4i execution record

The final semantic-model and presentation slice converts all 36 remaining
generic logic throws and 41 remaining generic runtime throws in the semantic
tree.  Graph and canonical-storage ceilings use semantic-domain
`ResourceLimitError`; invalid type construction, binding, inheritance, and
lookup use `SemanticError`; retained-identity and graph contradictions use
`InternalCompilerError`.  This brings the semantic generic-throw census to
zero without changing candidate/substitution status flow or cleanup catches.

PA12 passes 184/184, PA23 passes 414/414, and through-PA26 passes
3,821/3,821.  A 32-way inception run reproduces the exact final compiler.
The exception audit ratchets to 934 generic logic throws and 575 generic
runtime throws in 136 files.  Against `c3f1bf9d`, `.text` changes
6,567,014 -> 6,547,878, `.rodata` remains 214,272, `.eh_frame_hdr`
51,364 -> 51,444, `.eh_frame` 323,680 -> 322,904, and
`.gcc_except_table` 151,884 -> 147,460.

Twelve frozen objects remain exact at `8545fec6...`; both compiler user-time
medians are 0.480 seconds.  A first full block contained one isolated O1
candidate spike and one O3 baseline spike, so the clean mirrored block is the
retention signal.  All 222 objects match in every lane.  Requested-O1
baseline/candidate aggregate CPU averages 490.950/490.530 seconds (-0.09%);
requested-O3 averages 496.425/493.835 seconds (-0.52%).  The slice is retained
as a code-size and exception-policy improvement with no measured throughput
regression.

### E4j execution record

Thirty-nine of the 49 semantic catch-alls only restored a counter, scalar or
pointer-valued context, or candidate-stack depth before rethrowing.  They now
use three small C++11 scope guards: `ScopedCounterIncrement`,
`ScopedValueRestore`, and `ScopedContainerPush`.  Normal return, early return,
and exceptional exit consequently share one restoration path, without
catching or inspecting an exception.  The guards are active only around the
same analysis calls as the former increments and assignments.

The ten retained semantic catch-alls own transactions that are not equivalent
to a single value restoration:

- the three scalar-evaluator sites restore paired constexpr node/edge scratch
  arenas and their derived object map before rethrowing;
- lambda-capture analysis records a failed cache state, restores several
  parallel table bounds, and balances recursive depth;
- alias, class-shape, function-default, exception-specification, and
  placeholder-body sites commit distinct success, expected-failure, deferred,
  or hard-failure cache states before rethrowing; and
- placeholder variable formation invokes a multi-part context-release
  operation shared with normal exit.

These are explicit rollback/state-machine boundaries, not generic failure
classification.  They remain for the E8 residual allowlist rather than being
hidden behind a scope guard that cannot represent their commit semantics.  The
architecture audit ratchets from 53 to 14 catch-all sites repository-wide:
the ten above plus four non-semantic sites.  Generic-throw counts remain
934 logic and 575 runtime throws in 136 files.

PA12 passes 184/184, PA17 247/247, PA21 151/151, PA23 414/414, PA26
114/114, and through-PA26 passes 3,821/3,821.  Against `3bc47eb5`, `.text`
changes 6,547,878 -> 6,546,982, `.rodata` stays 214,272,
`.eh_frame_hdr` stays 51,444, `.eh_frame` stays 322,904, and
`.gcc_except_table` falls 147,460 -> 146,620.  Twelve frozen objects remain
exact at `8545fec6...`; baseline/candidate user medians are 0.480/0.475
seconds.

The 32-way A/B/B/A full controls reproduce all 222 objects and final compiler
hashes in every lane.  Requested-O1 baseline/candidate aggregate CPU averages
512.060/511.855 seconds (-0.04%); requested-O3 averages
518.865/518.385 seconds (-0.09%).  The state-restoration cleanup is therefore
dynamically neutral while reducing duplicated cleanup code and exception
metadata.  The closing 32-way inception run also matches every object and the
final `cppgm++` binary exactly.

### E5a execution record

The PA14 line-oriented adapter now constructs coded `SerializedInputError`
failures directly.  Its record-validation helper uses the ABI-fact format and
an invalid-record code.  Signed and unsigned decimal conversion catch only
`std::invalid_argument` and `std::out_of_range`, mapping them to distinct
invalid-number and number-out-of-range codes.  The line wrapper catches only
`SerializedInputError`, adds the line number, and preserves that code.  It no
longer translates allocation, encoder, internal, or I/O failures.

Case flushing and mangling were moved outside the parse-error translation
region, so a failure while consuming the preceding case cannot be mislabeled
as a syntax error on the next `case` line.  Stream and file failures use
ABI-domain `InputOutputError`; impossible canonical-serializer states use
ABI-domain `InternalCompilerError`.  A nonthrowing terminal-vocabulary lookup
lets the adapter reject unknown terminal words without catching an internal
standard exception; the existing integrated convenience API retains its
original behavior pending the ABI-core migration.

The audit ratchets from 934 to 910 generic logic throws and from 136 to 135
generic-throw files.  All three broad internal standard catches disappear;
the four reported standard translations are precisely the signed/unsigned
invalid/range catches above.  PA14 passes 117/117 and through-PA14 passes
1,081/1,081, including malformed and negative-index fact coverage described
by the PA14 handout.

Against `7f46e61e`, `.text` changes 6,546,982 -> 6,547,174, `.rodata`
stays 214,272, `.eh_frame_hdr` 51,444 -> 51,452, `.eh_frame`
322,904 -> 323,000, and `.gcc_except_table` 146,620 -> 146,624.  Twelve
frozen outputs remain exact at `8545fec6...`; baseline/candidate user medians
are 0.480/0.475 seconds.  The small typed-context cost is retained with no
measured throughput regression.

### E5b execution record

The phase-1 tokenizer now distinguishes invalid source characters, UTF-8,
UCNs, comments, headers, literals, and escapes (`SourceError`, lexical domain)
from impossible queue, Unicode-output, hexadecimal, and raw-mode states
(`InternalCompilerError`, lexical domain).  Macro paste no longer catches all
`runtime_error` failures.  Its collector returns an explicit boolean for the
expected “exactly one preprocessing token” decision; the terminal arm reports
a preprocessing-domain source failure.  Allocation and tokenizer-internal
failures propagate.  PA4 now states and behaviorally checks the one-token paste
rule rather than inspecting exception text or implementation source.

The first mechanically typed build was rejected after twelve frozen lanes
showed a repeatable 25% user-time regression.  Stage telemetry localized the
entire increase to preprocessing (about 331 ms -> 453 ms); parsing, semantics,
and lowering were unchanged.  Binary inspection found that changing the cold
queue throw bodies had altered GCC's inlining decision: the full physical and
translation cursor pipeline was duplicated into each lexer lookahead clone,
growing `Lexer::Peek` from roughly 200 bytes to 7.8 KiB and `Lexer::Run` from
5.5 KiB to 9.2 KiB.  This was code-layout/inlining interference, not exception
execution—the valid workload still throws zero times.

The retained version explicitly preserves the established `PhysicalCursor`
and `TranslationCursor` call boundaries with `noinline`; a screened attempt to
also prevent `Lexer::Peek` inlining regressed 18% and was rejected.  Four final
ABBA blocks reverse the original result: frozen baseline/candidate user medians
are 0.485/0.455 seconds (-6.19%), with exact object hash `8545fec6...`.
The 32-way full A/B/B/A controls reproduce all 222 objects and final compiler
hashes.  Requested-O1 aggregate CPU averages 514.790/504.940 seconds (-1.91%);
requested-O3 averages 518.780/506.635 seconds (-2.34%).

The exception audit ratchets by 5 generic logic throws, 20 generic runtime
throws, one generic-throw file, and the last internal runtime catch.  Against
`538b79f7`, `.text` changes 6,547,174 -> 6,531,174, `.rodata`
214,272 -> 214,208, `.eh_frame_hdr` 51,452 -> 51,420, `.eh_frame`
323,000 -> 322,752, and `.gcc_except_table` 146,624 -> 144,520.
PA1 passes 53/53, PA4 passes 75/75, and through-PA4 passes 174/174.

### E5c execution record

Post-tokenization now reports impossible enum values, fixed-storage overflow,
and missing output destinations as lexical-domain internal compiler failures.
Recognition distinguishes an invalid phase-7 token (lexical source failure),
checked identifier/token ceilings (recognition resource limits), and defects
in the embedded PA6 grammar or its EBNF reader (recognition internal failures).
Invalid hosted-intrinsic enum values are project-owned internal failures.  The
normal literal-decoding, grammar no-match, and identifier lookup paths remain
boolean/status flow; no successful alternative was converted to an exception.

The first typed build grew `.text` by 17,344 bytes even though none of these
exceptions executed.  The smaller cold throw bodies caused GCC to inline the
roughly 1 KiB pending-string flush state machine into nearly every token
callback.  The retained version marks that shared state machine `noinline`,
which restores a single copy and makes the boundary independent of cold error
details.  This is the same unthrown-exception/inliner interference class found
in E5b, not dynamic unwind cost.

Against `71d6deba`, `.text` changes 6,531,174 -> 6,530,534,
`.rodata` 214,208 -> 214,304, `.eh_frame_hdr` 51,420 -> 51,428,
`.eh_frame` 322,752 -> 322,696, and `.gcc_except_table` 144,520 ->
144,320.  Four exact ABBA blocks tie at a 0.450/0.450-second user median;
the paired +0.55% difference is below timer resolution and accompanied by
identical frozen objects.  PA2 passes 26/26, PA5 70/70, PA6 48/48, and the
cumulative through-PA6 report passes 292/292.  The exception audit ratchets by
26 generic logic throws, three generic runtime throws, and three generic-throw
files.

### E5d execution record

The macro processor now distinguishes committed preprocessing-source errors,
command-line macro invocation errors, include/source I/O, checked identifier,
paint, and include-depth resource ceilings, and internal paint/rescan/storage
invariants.  Existing successful-control-flow alternatives remain explicit:
include and builtin probes return `bool`, a name that is not a function-like
macro invocation returns an invocation-state enum, an invalid controlling
expression is reported only after its evaluator returns `valid == false`, and
token-paste cardinality retains E5b's direct status.  Filesystem absence during
an include probe remains `false`; only a committed include or source read
becomes an I/O exception.

The central `Drain` routine grew by roughly 2.8 KiB after GCC reconsidered
several small helper inlines, while the linked compiler shrank overall.  This
was measured rather than inferred: the frozen valid compile records zero
throws and four exact ABBA blocks tie at a 0.450-second user median (paired
-0.56%).  Full 32-way A/B/B/A builds reproduce all 222 object and final-binary
hashes.  O1 aggregate CPU averages 460.670/456.745 seconds (-0.85%); O3
averages 467.690/466.320 seconds (-0.29%).  The larger rescan symbol therefore
does not conceal a measured hot-path regression.

Against `8311aeb4`, `.text` changes 6,530,534 -> 6,525,734,
`.rodata` 214,304 -> 214,400, `.eh_frame_hdr` 51,428 -> 51,492,
`.eh_frame` 322,696 -> 322,768, and `.gcc_except_table` 144,320 ->
143,612.  The exception audit ratchets by 22 generic logic throws, 56 generic
runtime throws, and one generic-throw file.  The PA3-PA6 focused suites pass,
through-PA14 passes 1,082/1,082, all architecture audits pass, and exact PA39
inception succeeds with outer, inner, and object parallelism set to 32.

E5 now has zero generic policy throws or catches in lexical preprocessing,
post-tokenization, macro processing, and recognition, and zero in the PA14 ABI
fact adapters.  Four generic throws intentionally remain in the ABI mangling
core identity owner; they are not fact-adapter translations and move with the
remaining compiler core in E7.

### E6a execution record

The PA13 LowIR text/model slice removes the overbroad
`lowir_model::ParseError` type and classifies all 126 of its producers.
Malformed textual structure, references, metadata, and unsupported PA13 input
now use LowIR-format `SerializedInputError`.  Source/output transport failures
use LowIR-domain `InputOutputError`, an empty path list is an invocation error,
checked generated identity ceilings are LowIR resource limits, and compact
identity or layout contradictions are LowIR internal failures.  The typed
integrated model gains no field or serialized fact; cold, non-inlined throw
helpers live beside the in-memory model and retain diagnostic strings only in
the exceptional object.

No successful alternative was converted to an exception or status.  The
instrumented frozen compile records zero throws, so status propagation would
add successful-path branches without removing dynamic unwind.  The exception
audit instead falls by 41 generic logic throws, nine runtime throws, and four
generic-throw files, to 816/487/126.  PA13 passes 122/122 and through-PA13
passes 965/965; the 127-row LowIR contract audit is unchanged.

Against `7cf04f4b`, `.text` changes 6,525,734 -> 6,511,206, `.rodata`
214,400 -> 214,336, `.eh_frame_hdr` 51,492 -> 51,548, `.eh_frame`
322,768 -> 322,600, and `.gcc_except_table` 143,612 -> 141,076.  Four frozen
A/B/B/A blocks reproduce object hash `8545fec6...` and move the user median
from 0.450 to 0.440 seconds (paired -1.12%).  The 32-way full controls
reproduce all 222 objects and final compilers: O1 aggregate CPU averages
503.41/501.47 seconds (-0.39%) and O3 averages 507.59/506.28 seconds
(-0.26%).  A formatting-only consolidation of E5d's typed macro throw calls
also repairs its two inherited file-audit failures; rebuilding after that
consolidation produces the exact same candidate compiler, and the audit is
back to the established 32 advisory warnings with no fatal finding.

### E6b execution record

Compiler-object transport now distinguishes malformed private-object and ELF
bytes from stream failures, checked representation ceilings, link invocation
errors, and impossible serializer/linker state.  The private-object reader
translates only a nested LowIR `SerializedInputError` at the enclosing format
boundary; LowIR resource/internal failures and allocation failures propagate.
ELF open/read failures are I/O, unsupported or inconsistent section, symbol,
and relocation records are compiler-object input failures, and host-size
overflow is a resource limit.  Duplicate definitions and conflicting aliases
are rejected as incompatible compiler-object inputs; no inputs and target
mismatch are invalid link invocations.

Normal control flow is unchanged.  `IsObject` and payload discovery remain
nonthrowing probes, and successful serialization, import, and linking add no
returned status branch.  The instrumented frozen compile remains at zero
throws.  The generic audit falls by four logic throws, 41 runtime throws, and
three files, to 812/446/123.  A generated private object truncated to 17 bytes
and an unwritable output path both fail without relying on diagnostic text.
PA30 passes 100/100 and through-PA30 passes 4,355/4,355.

Against `23294aa9`, `.text` changes 6,511,206 -> 6,505,638, `.rodata`
214,336 -> 214,464, `.eh_frame_hdr` 51,548 -> 51,476, `.eh_frame`
322,600 -> 322,120, and `.gcc_except_table` 141,076 -> 139,016.  Four frozen
A/B/B/A blocks reproduce object hash `8545fec6...` and tie at a 0.450-second
user median; the paired -1.66% result is below timer resolution and recorded as
neutral.  The final 32-way full controls reproduce all 222 objects and final
compilers: O1 aggregate CPU averages 501.17/500.02 seconds (-0.23%) and O3
averages 503.76/503.78 seconds (neutral).

### E6c execution record

The in-memory source-to-LowIR adapter's 23 remaining generic checks are all
producer-owned invariants: typed operation/type enums, dense result and
operand identities, call/EH/switch side-table spans, block targets and order,
parameter origins, and presentation retention.  They do not parse external
LowIR and therefore use LowIR-domain `InternalCompilerError`, not
`SerializedInputError`.  The existing shared cold, non-inlined LowIR helper
keeps exception construction out of the adapter loops; every successful bounds
or identity check is unchanged.

The instrumented frozen compile remains at zero throws.  Generic logic throws
fall by 23 and the adapter leaves the generic-file inventory, bringing the
audit to 789/446/122.  Against `fd1c98ff`, `.text` changes 6,505,638 ->
6,504,358, `.rodata` remains 214,464, `.eh_frame_hdr` 51,476 -> 51,460,
`.eh_frame` 322,120 -> 322,048, and `.gcc_except_table` 139,016 -> 138,836.
Four frozen A/B/B/A blocks reproduce object hash `8545fec6...` and tie at a
0.450-second user median; the paired +0.56% fluctuation is below resolution.
The 32-way full controls reproduce all 222 objects and final compilers: O1
aggregate CPU averages 500.80/498.84 seconds (-0.39%) and O3 averages
504.93/504.48 seconds (-0.09%).

PA15 passes 121/121, PA37 190/190, PA38 45/45, and through-PA38 passes
5,477/5,477.  The LowIR contract audit remains exactly 127/102/140/23/21, and
the closing inception run uses 32-way outer, inner, and object parallelism and
matches every object plus the final `cppgm++`.  E6 therefore changes no LowIR
field or serialized fact while separating malformed LowIR/compiler objects,
transport failures, limits, link invocation failures, and internal typed-model
corruption.

### E7a execution record

The first E7 slice covers the shared CFG, phi-edge, reachability, direct-call,
inlining, force-inlining, slot-promotion, and interprocedural-specialization
owners.  Its 26 generic failures are not ordinary optimizer rejection: four
validate the optional `--inline-limit` invocation and the other 22 diagnose
producer-owned LowIR/optimizer contradictions after the PA13 parser has
already validated external LowIR.  Invalid overrides now throw
`InvocationError`; the invariant arms use one cold, non-inlined
optimizer-domain `InternalCompilerError` boundary.  The existing Boolean and
eligibility returns continue to represent "cannot optimize" without an
exception, and the two deliberate allocator-protocol `std::bad_alloc` throws
remain unchanged.

The successful frozen compiler records zero throws.  Generic logic/runtime
sites fall by 23/3 and eight files leave the generic inventory, bringing the
audit to 766/443/114.  Against `9ab20e02`, `.text` changes 6,504,358 ->
6,502,310, `.rodata` 214,464 -> 214,496, `.eh_frame_hdr` 51,460 -> 51,476,
`.eh_frame` 322,048 -> 322,040, and `.gcc_except_table` 138,836 -> 138,596.
Four frozen A/B/B/A blocks reproduce object hash `8545fec6...` and tie at a
0.450-second user median; paired candidate time is -0.55%, below resolution.

PA37 passes 190/190 and through-PA37 passes 5,432/5,432.  Layout, source-set,
LowIR-contract, and exception audits pass.  The 32-way full O1 control
reproduces all 222 objects and its final compiler, with baseline/candidate
aggregate CPU averages 498.86/497.96 seconds (-0.18%).  Two O3 blocks also
reproduce all 222 objects and their final compiler; their combined averages
are 504.03/505.99 seconds (+0.39%), while the repeat alone is +0.13%.  This is
timer noise below the 1% rejection threshold, not evidence for adding returned
status checks to successful optimizer control flow.

### E7b execution record

The integrated `cppgm++` driver now separates invalid options/mode
combinations, source-token rejection, source/output/library transport,
malformed compiler-object input, and impossible dispatch states.  Its
missing-option helper no longer returns a `logic_error` for a caller to throw;
it enters the cold typed invocation boundary directly.  Unsupported target and
language selections are invocation errors, source/output/library failures are
driver-domain I/O errors, an invalid phase-7 token is a lexical source error,
and an object-looking input rejected by the link path is compiler-object input.
Unknown enum/query dispatch remains an internal driver failure.

The ordinary path adds no status propagation.  File-kind checks and candidate
library-path probes remain Boolean; only exhaustion of all requested library
paths fails.  The successful frozen compile records zero throws.  Generic
logic/runtime sites fall by 23/22, the integrated driver leaves the generic
file inventory, and the generic returned-exception helper reaches zero,
bringing the audit to 743/421/113/0.  Relative to E7a, `.text` changes
6,502,310 -> 6,498,342, `.rodata` 214,496 -> 214,528, `.eh_frame_hdr`
51,476 -> 51,516, `.eh_frame` 322,040 -> 322,216, and
`.gcc_except_table` 138,596 -> 138,384.

PA30 passes 179/179, PA36 passes 1/1, and through-PA36 passes 5,242/5,242.
Invalid optimization selection, a missing source, and an unwritable object
destination all fail by exit status without any diagnostic-text oracle.  The
layout, source-set, LowIR-contract, exception, and PA38 file audits pass with
the established 32 file warnings.  Four frozen A/B/B/A blocks against the E6c
baseline reproduce object hash `8545fec6...`; both user medians are 0.450
seconds and paired candidate time is +0.56%, below resolution.  E7a was
independently neutral before this combined guard, so no full-build checkpoint
is added between the plan's optimizer and native milestones.

### E7c execution record

The foundational lowering slice covers typed IR construction and rendering,
semantic-graph reachability, source-type conversion, emission identities,
local presentation, and compact identity/sequence support.  Cross-source type
or definition conflicts and a value-returning body with no return are lowering
source failures.  Empty API input is invocation, output-stream failure is I/O,
and fixed-width symbol/value/slot/presentation/identity ceilings are resource
limits.  The remaining enum, identity, graph, CFG, ABI, and presentation checks
are producer-owned lowering invariants.  An unsupported `_BitInt` width is a
representation limit; an unresolved dependent type reaching emission is an
internal phase-order failure.

No site represents an expected “cannot lower this optimization” alternative,
so successful control flow keeps its existing checks and enters one cold,
non-inlined typed boundary only on failure.  The successful frozen compile
records zero throws.  Generic logic/runtime sites fall by 55/22 and eleven
files leave the generic inventory, bringing the audit to 688/399/102.  Against
E7b, `.text` changes 6,498,342 -> 6,497,510, `.rodata` 214,528 -> 214,560,
`.eh_frame_hdr` 51,516 -> 51,668, `.eh_frame` 322,216 -> 322,552, and
`.gcc_except_table` 138,384 -> 134,704.

PA15 passes 121/121 and through-PA15 passes 1,203/1,203.  Four frozen A/B/B/A
blocks against the preserved E6c baseline also cover the independently neutral
E7a/E7b prefix, reproduce object hash `8545fec6...`, and give baseline/candidate
user medians 0.445/0.450 seconds with a paired +0.01% result.  This is neutral,
while the exception-table reduction confirms that the cold boundary removes
EH duplication from the hot lowering owners without returned-status overhead.

### E7d execution record

ABI lowering builds typed facts and symbol identities for nearly every emitted
entity, but its generic failures are not search misses.  All 68 logic sites
guard retained semantic paths, template recipes, context ownership, argument
ranges, and typed ABI node invariants.  Of five runtime sites, the type-fact
cache ceiling is a lowering resource limit, dependent/non-fundamental ABI
states are internal phase/type contradictions, and unsupported complex or
remaining ABI source types are terminal lowering source failures.  Existing
Boolean probes for builtin types, class-template owners, substitutions, and
optional contexts remain non-exceptional.

The successful frozen compile records zero throws.  The generic audit falls by
68 logic and five runtime sites, with the ABI mangling owner leaving the
generic-file inventory, to 620/394/101.  Against E7c, `.text` changes
6,497,510 -> 6,495,590, `.rodata` 214,560 -> 214,528, `.eh_frame_hdr`
51,668 -> 51,652, `.eh_frame` 322,552 -> 322,416, and
`.gcc_except_table` 134,704 -> 133,940.

PA14 passes 117/117, PA15 passes 121/121, and through-PA15 passes
1,203/1,203.  Four cumulative frozen A/B/B/A blocks reproduce object hash
`8545fec6...`; baseline and candidate user medians tie at 0.450 seconds and the
paired ratio is exactly neutral.  The smaller hot owner and EH tables therefore
retain the typed cold form without adding a status result to every mangling
call.

### E7e execution record

Call lowering has no recovery catch around its generic failures.  Its ordinary
builtin recognition, callee selection, elision, and optional cleanup decisions
already use Boolean or typed graph state and remain unchanged.  The migrated
sites guard intrinsic arity/type facts, call-argument side-table consistency,
constructor/destructor actions and emitted bindings, synthesized special-member
steps, and value-boundary identities.  Those are internal lowering failures;
call-vector and signed LowIR extent ceilings are resource limits, while an
unsupported atomic object width, brace-less array construction shape, or
unbounded-array destruction is a terminal lowering source failure.

The successful frozen compile records zero throws.  Generic logic/runtime
sites fall by 65/10 and all five call-owner headers leave the generic inventory,
bringing the audit to 555/384/96.  Against E7d, `.text` changes 6,495,590 ->
6,490,534, `.rodata` remains 214,528, `.eh_frame_hdr` 51,652 -> 51,580,
`.eh_frame` 322,416 -> 322,064, and `.gcc_except_table` 133,940 -> 133,064.

PA15 passes 121/121, PA17 247/247, PA26 114/114, and through-PA26 passes
3,822/3,822.  Four cumulative frozen A/B/B/A blocks reproduce object hash
`8545fec6...`; baseline and candidate user medians both measure 0.445 seconds
and paired candidate time is -0.54%, below resolution.  No exception-to-status
conversion is justified because these failures are neither caught nor observed
on successful compilation, while the shared cold boundary materially reduces
the hot template owners and EH metadata.

### E7f execution record

Constant, control-flow, expression, and extension lowering likewise has no
recovery catch around its generic failures.  Expected checkpoint availability,
operator applicability, initializer progress, and optional semantic forms keep
their existing Boolean or typed-state paths.  The converted terminal failures
instead distinguish graph, region, identity, and exception-routing
contradictions as internal; automatic-template, block, incoming-edge, cleanup,
and switch-case ceilings as resource limits; and unsupported source-level
operators, conversions, bit-fields, initializers, and checkpoint contexts as
source failures.

The successful frozen compile records zero throws.  Generic logic/runtime
sites fall by 53/42 and fourteen owners leave the generic inventory, bringing
the audit to 502/342/82.  Against E7e, `.text` changes 6,490,534 -> 6,479,398,
`.rodata` 214,528 -> 214,560, `.eh_frame_hdr` 51,580 -> 51,620, `.eh_frame`
322,064 -> 322,120, and `.gcc_except_table` 133,064 -> 131,608.

PA15 passes 121/121, PA16 58/58, PA23 414/414, PA26 114/114, and through-PA26
passes 3,822/3,822.  Four cumulative frozen A/B/B/A blocks reproduce object
hash `8545fec6...`; baseline and candidate user medians both measure 0.450
seconds.  The paired +1.67% result is one scheduler tick at this sample size
and is treated as neutral alongside the exact output, zero successful-path
throws, and substantial code/EH-table reduction.  Replacing these terminal
failures with returned status would add successful-path plumbing without a
recovering consumer.

### E7g execution record

Object initialization, array construction, temporary lifetime, and cleanup
continuation lowering contains no recovery catch around its generic failures.
Optional initialization, elision, branch-cleanup, and continuation decisions
already use normal Boolean, identity, or operand state.  Semantic-dump and
cleanup-graph contradictions now use the lowering internal type; unsupported
bracing and initializer shapes use the lowering source type; and cleanup
identity or array-extent exhaustion uses the lowering resource type.  The one
combined constructor-array check was split so malformed graph shape and
arithmetic extent overflow no longer share a disposition.

The successful frozen compile records zero throws.  Generic logic/runtime
sites fall by 57/28 and all six owners leave the generic inventory, bringing
the audit to 445/314/76.  Against E7f, `.text` changes 6,479,398 -> 6,472,870,
`.rodata` 214,560 -> 214,592, `.eh_frame_hdr` remains 51,620, `.eh_frame`
322,120 -> 321,976, and `.gcc_except_table` 131,608 -> 130,692.

PA15 passes 121/121, PA17 247/247, PA26 114/114, and through-PA26 passes
3,822/3,822.  Four cumulative frozen A/B/B/A blocks reproduce object hash
`8545fec6...`; baseline and candidate user medians both measure 0.450 seconds
and the paired candidate time is +0.56%, below timer resolution.  Returning
status from these hot lowering templates would add checks on successful
construction even though no local consumer recovers from the failures.

### E7h execution record

Storage access, static lifetime, RTTI, member-pointer, vptr, and thunk lowering
has no recovery catch around the remaining generic failures.  Normal storage
lookup, static-initialization state, and object-model applicability continue to
use returned operands, sentinels, or branches.  Missing emitted symbols,
malformed semantic actions, contradictory storage facts, and impossible RTTI
or thunk shapes now use the lowering internal type.  Invalid string-literal
input and a source expression outside the supported scalar-storage surface use
the lowering source type.

The successful frozen compile records zero throws.  Generic logic/runtime
sites fall by 22/9 and all eleven owners leave the generic inventory, bringing
the audit to 423/305/65.  Against E7g, `.text` changes 6,472,870 -> 6,467,110,
`.rodata` remains 214,592, `.eh_frame_hdr` 51,620 -> 51,604, `.eh_frame`
321,976 -> 321,848, and `.gcc_except_table` 130,692 -> 130,000.

PA15 passes 121/121, PA16 300/300, PA18 37/37, PA22 311/311, and through-PA26
passes 3,822/3,822.  Four cumulative frozen A/B/B/A blocks reproduce object
hash `8545fec6...`; baseline/candidate user medians are 0.460/0.450 seconds and
paired candidate time is -1.09%, below timer resolution.  The cold typed
boundary reduces the linked success-path footprint without adding a returned
status check to these frequently instantiated lowering helpers.

### E7i execution record

Polymorphic-layout, construction-VTT, and virtual-base lowering has no recovery
catch around its generic failures.  Ordinary hierarchy traversal, optional
virtual-base lookup, and ABI-fact discovery retain their existing Boolean and
sentinel paths.  Contradictory vtable, RTTI, thunk, construction-tree, symbol,
and virtual-base facts now use the lowering internal type; the 32-bit host
vtable and adjusted-slot ceilings use the lowering resource type.

The successful frozen compile records zero throws.  Generic logic/runtime
sites fall by 41/2 and both owners leave the generic inventory, bringing the
audit to 382/303/63.  Against E7h, `.text` changes 6,467,110 -> 6,462,502,
`.rodata` remains 214,592, `.eh_frame_hdr` 51,604 -> 51,588, `.eh_frame`
321,848 -> 321,744, and `.gcc_except_table` 130,000 -> 129,596.

PA18 passes 37/37, PA22 311/311, and through-PA26 passes 3,822/3,822.  Four
cumulative frozen A/B/B/A blocks reproduce object hash `8545fec6...`;
baseline/candidate user medians both measure 0.450 seconds and paired candidate
time is exactly neutral.  These are terminal graph-contract checks, so a
returned failure channel would burden successful vtable construction without
a recovery owner.

### E7j execution record

The force-inline lowering transform has no recovery catch around its generic
failures.  Candidate selection, recursive-call rejection, reachability, and
non-applicable inlining already use ordinary indices, Boolean state, or
sentinels.  Contradictory symbols, dense identities, call ranges, CFG targets,
returns, and block ordering now use the lowering internal type; exhausted
temporary, slot, and block identities use the lowering resource type.

The successful frozen compile records zero throws.  Generic logic/runtime
sites fall by 18/7 and the final lowering owner leaves the generic inventory,
bringing the repository audit to 364/296/62 and the lowering generic census to
zero.  Against E7i, `.text` changes 6,462,502 -> 6,462,374; `.rodata` remains
214,592; `.eh_frame_hdr` remains 51,588; `.eh_frame` remains 321,744; and
`.gcc_except_table` changes 129,596 -> 129,608.

PA37 passes 190/190 and through-PA37 passes 5,432/5,432.  Four cumulative
frozen A/B/B/A blocks reproduce object hash `8545fec6...`; baseline/candidate
user medians both measure 0.450 seconds and paired candidate time is exactly
neutral.  A 32-way full self-build against the E6c baseline reproduces all 222
objects and final binaries at both requested levels.  Aggregate CPU is
500.825/503.500 seconds at O1 (+0.53%, neutral, with one slower candidate run)
and 508.005/507.145 seconds at O3 (-0.17%).  The absence of a recovery consumer
and the exact full outputs favor the typed terminal boundary over adding
success-path status plumbing.

### E7k execution record

Native driver setup, MIR presentation/optimization, register allocation,
data-layout, and frame planning have no recovery catches around their generic
failures.  Allocation replay and spill choices remain ordinary recorded or
Boolean decisions.  Unsupported target and optimization-level requests now
use invocation failure; MIR output uses native I/O failure; contradictory TLS
and variadic LowIR facts use serialized-LowIR failure; finite register, frame,
and size ceilings use native resource failure; and allocation/MIR/frame
contradictions use native internal failure through a namespace-qualified cold
boundary shared by the backend's otherwise separate namespaces.

The successful frozen compile records zero throws.  Generic logic/runtime
sites fall by 23/10 and all eleven owners leave the generic inventory, bringing
the audit to 341/286/51.  Against E7j, `.text` changes 6,462,374 -> 6,462,246;
`.rodata` remains 214,592; `.eh_frame_hdr` 51,588 -> 51,628; `.eh_frame`
321,744 -> 321,584; and `.gcc_except_table` 129,608 -> 129,008.

PA38 passes 45/45 and through-PA38 passes 5,477/5,477.  Four cumulative frozen
A/B/B/A blocks reproduce object hash `8545fec6...`; baseline/candidate user
medians both measure 0.450 seconds and paired candidate time is exactly
neutral.  The checks are terminal while allocation's expected choices remain
non-exception flow, so no hot status conversion is warranted.

### E7l execution record

Native instruction, operand, ABI, intrinsic, phi, and wide-value lowering has
no recovery catch around its generic failures.  Selector applicability,
register choice, pressure-home fallback, and instruction dispatch retain their
ordinary Boolean, opcode, and operand paths.  Malformed operand/type and
metadata combinations now use serialized-LowIR failure; target forms that the
backend cannot represent use native source failure; register exhaustion uses
native resource failure; and contradictory value locations, move cycles,
frame homes, and MIR state use native internal failure.

The successful frozen compile records zero throws.  Generic logic/runtime
sites fall by 15/40 and all nine owners leave the generic inventory, bringing
the audit to 326/246/42.  Against E7k, `.text` changes 6,462,246 -> 6,461,926;
`.rodata` 214,592 -> 214,784; `.eh_frame_hdr` remains 51,628; `.eh_frame`
321,584 -> 321,264; and `.gcc_except_table` 129,008 -> 127,912.

PA38 passes 45/45 and through-PA38 passes 5,477/5,477.  Four cumulative frozen
A/B/B/A blocks reproduce object hash `8545fec6...`; baseline/candidate user
medians both measure 0.450 seconds and paired candidate time is exactly
neutral.  Since expected selector/allocation alternatives did not unwind
before and remain ordinary flow, replacing terminal checks with returned
status would only add hot caller plumbing.

### E7m execution record

Native host-EH analysis, LSDA construction, and x86 encoding have no recovery
catches around their generic failures.  EH traversal, immediate-encoding
availability, address folding, and copy-shape selection keep their existing
ordinary branches.  Contradictory MIR regions, operands, addresses, ranges,
and encoder inputs now use native internal failure; EH range overflow uses the
native resource type; and unsupported source global-data forms use the native
source type.

The first header-inline version duplicated typed construction across backend
translation units and grew `.text` by 3,072 bytes, so it was rejected before
validation.  Moving the native boundary into `native/errors.cpp` and wiring it
into both native source sets amortizes that cost across all backend owners.
The retained form removes 62 generic logic sites and all nine owners from the
inventory, bringing the audit to 264/246/33.  Against E7l, `.text` changes
6,461,926 -> 6,461,158, `.rodata` 214,784 -> 214,720, `.eh_frame_hdr` 51,628 ->
51,620, `.eh_frame` 321,264 -> 320,904, and `.gcc_except_table` 127,912 ->
126,924.

PA38 passes 45/45 and through-PA38 passes 5,477/5,477.  Four cumulative frozen
A/B/B/A blocks reproduce object hash `8545fec6...`; baseline/candidate user
medians are 0.440/0.445 seconds and paired candidate time is +0.56%, below
timer resolution.  Centralizing the failure-only code improves the linked
footprint without adding a status edge to encoder or EH success paths.

### E7n execution record

Native code-buffer, relocation, label, fixup, and ELF string-table owners have
no recovery catches around their generic failures.  Fixup lookup, known-label
tests, branch relaxation, and symbol publication retain their normal lookup
and Boolean paths.  Contradictory label, identity, patch, and relaxation state
now uses native internal failure; duplicate/unresolved source symbol requests
use native source failure; and label, displacement, address, and 32-bit table
ceilings use native resource failure.

The successful frozen compile records zero throws.  Generic logic/runtime
sites fall by 50/19 and all three owners leave the generic inventory, bringing
the audit to 214/227/30.  Against E7m, `.text` changes 6,461,158 -> 6,458,790;
`.rodata` remains 214,720; `.eh_frame_hdr` 51,620 -> 51,596; `.eh_frame`
320,904 -> 320,232; and `.gcc_except_table` 126,924 -> 125,860.

PA38 passes 45/45 and through-PA38 passes 5,477/5,477.  Four cumulative frozen
A/B/B/A blocks reproduce object hash `8545fec6...`; baseline/candidate user
medians both measure 0.450 seconds and paired candidate time is exactly
neutral.  The centralized cold boundary reduces both hot-owner code and EH
metadata while successful fixup/relaxation flow remains unchanged.

### E7o execution record

ELF section, symbol, relocation, COMDAT, and host-object layout contains no
recovery catch around its generic failures.  Section placement, symbol lookup,
weak grouping, and relocation selection retain their normal table and branch
flow.  Contradictory section/symbol identities, object bounds, EH tables, and
relocation envelopes now use native internal failure; an undefined source
alias uses native source failure; and the 16-bit ELF section ceiling uses
native resource failure.

The successful frozen compile records zero throws.  Generic logic/runtime
sites fall by 43/2 and the owner leaves the generic inventory, bringing the
audit to 171/225/29.  Against E7n, `.text` changes 6,458,790 -> 6,458,534;
`.rodata` remains 214,720; `.eh_frame_hdr` 51,596 -> 51,580; `.eh_frame`
320,232 -> 320,040; and `.gcc_except_table` 125,860 -> 125,404.

PA38 passes 45/45 and through-PA38 passes 5,477/5,477.  Four cumulative frozen
A/B/B/A blocks reproduce object hash `8545fec6...`; baseline/candidate user
medians are 0.450/0.455 seconds and paired candidate time is +1.67%, one timer
tick at this sample size.  Exact output plus lower code/EH footprint and the
surrounding neutral native slices classify that result as noise.

### E7p execution record

The final MIR-to-x86 and executable/object writer has no recovery catch around
its generic failures.  Opcode dispatch, function-context selection, and output
construction retain their ordinary switch and branch flow.  Contradictory MIR
operands, function context, TLS facts, and object identities now use native
internal failure; an unsupported MIR opcode or missing startup entry uses
native source failure; target mismatch uses invocation failure; and native
executable/object creation, writing, and permission updates use native I/O
failure.

The successful frozen compile records zero throws.  Generic logic/runtime
sites fall by 55/9 and the final native owner leaves the inventory, bringing
the audit to 116/216/28 and the native generic census to zero.  Against E7o,
`.text` changes 6,458,534 -> 6,452,262; `.rodata` remains 214,720;
`.eh_frame_hdr` 51,580 -> 51,484; `.eh_frame` 320,040 -> 319,408; and
`.gcc_except_table` 125,404 -> 124,232.

PA38 passes 45/45 and through-PA38 passes 5,477/5,477.  Four cumulative frozen
A/B/B/A blocks reproduce object hash `8545fec6...`; baseline/candidate user
medians are 0.445/0.450 seconds and paired candidate time is +0.56%, below
timer resolution.

The first cumulative E6c/current 32-way O1 windows reported +1.59% while O3
reported +0.38%, so the O1 result was investigated rather than retained at
face value.  Per-translation-unit 32-way accounting localizes no native
regression: lowering-complete E7j/current aggregate CPU is 478.48/478.37
seconds, and native-owner CPU is 84.30/83.79 seconds.  Direct full A/B/B/A
comparison against E7j reproduces all 223 objects and final binaries and
measures 506.67/507.06 seconds at O1 (+0.08%) and 511.56/511.59 seconds at O3
(+0.01%).  An E6c/E7j per-unit control measures 476.16/478.48 seconds (+0.49%),
consistent with the earlier lowering milestone, while contemporaneous full
windows show isolated multi-second scheduler outliers under elevated host
load.  The native phase is therefore retained as neutral; expected backend
alternatives remain non-exception flow and no hot status conversion is
justified.

### E7q execution record

The PA7 namespace semantic/parser owner contains no recovery catch around any
of its 46 generic failures.  Its speculative declaration, type-id, and
declarator alternatives already return Boolean miss/success results, so valid
parser control flow remains exception-free.  The migrated sites are terminal
lexical/syntax diagnostics, ordinary namespace/type semantic rejection,
finite identifier/type/namespace/memo ceilings, or canonical/declarator-frame
invariants; they now use the corresponding typed disposition through cold
helpers.  Converting these terminal checks to returned status would add
successful-path propagation without removing a measured unwind.

The generic audit falls by 3 logic and 43 runtime sites, to 113/173/27.  PA7
passes 43/43 and through-PA7 passes 335/335; the exception and frontend source
set audits pass.  Against E7p the staged `nsdecl` `.text` changes 323,718 ->
322,310, `.rodata` 15,520 -> 15,552, `.eh_frame_hdr` 3,892 -> 3,956,
`.eh_frame` 21,316 -> 21,580, and `.gcc_except_table` 6,101 -> 5,566.  This
owner is not part of the integrated `cppgm++` source set, and rebuilding both
revisions produces the exact same integrated compiler hash
`4438331165dbb...`; therefore it has no frozen/full timing exposure.

### E7r execution record

The PA8 namespace-initialization driver, parser, semantic/image model, and
writer likewise contain no recovery catch around their generic failures.
Committed grammar failures use `SyntaxError`; source semantic rejection uses
`SemanticError`; the image writer has a distinct semantic-domain I/O exit;
finite token/model/image ceilings use `ResourceLimitError`; and impossible
model/image states use `InternalCompilerError`.  Speculative declarator and
declaration parsing continues to return Boolean miss/success, so no expected
control-flow exception or hot status-conversion opportunity exists.

The generic audit falls by 14 logic and 136 runtime sites, to 99/37/24.  PA8
passes 67/67 and through-PA8 passes 402/402; the exception audit passes.
Against E7q the staged `nsinit` `.text` changes 353,990 -> 340,102, `.rodata`
22,891 -> 22,923, `.eh_frame_hdr` 5,156 -> 5,268, `.eh_frame` 27,196 ->
27,148, and `.gcc_except_table` 7,919 -> 5,889.  These owners are not part of
the integrated source set, and the integrated compiler remains exact at
`4438331165dbb...`, so there is no frozen/full timing exposure.

### E7s execution record

The remaining integrated syntax-support failures have no recovery catches.
Malformed attributes are committed syntax errors; token/node/edge/fact and
intern-table ceilings are resource failures; invalid rollback, lookahead,
packed-token, and destination states are internal failures; and invalid
phase-7 tokens retain lexical source disposition.  Attribute recognition,
name lookahead, declarator alternatives, and arena rollback remain explicit
Boolean/checkpoint flow.  A successful instrumented frozen compile records
zero throws, so replacing the terminal guards with propagated status would
only add hot successful-path work.

The generic audit falls by 8 logic and 13 runtime sites, to 91/24/19.  PA10
passes 165/165 and through-PA10 passes 587/587.  Against E7r, integrated
`.text` changes 6,452,262 -> 6,451,558; `.rodata` remains 214,720;
`.eh_frame_hdr` 51,484 -> 51,548; `.eh_frame` 319,408 -> 319,560; and
`.gcc_except_table` 124,232 -> 123,936.  Sixteen pinned interleaved frozen
runs all reproduce object hash `8545fec6...`; baseline/candidate user medians
are 0.460/0.455 seconds and means 0.451/0.455 seconds (+0.83%), a one-timer-
tick distribution recorded as neutral.

### E7t execution record

The remaining ABI model, vocabulary, presentation, and encoder failures are
terminal fact/model validation, finite-cache limits, output I/O, or internal
encoding contradictions.  They now use centralized ABI-domain fact-input,
resource, I/O, and internal cold exits.  Successful substitution lookup,
resolved-type caching, and encoding dispatch retain their existing Boolean,
lookup, and switch flow; no exception is used as an ordinary miss.

The only EH regions in this family were two catch-all cleanup/rethrow blocks
around each context mangle.  A `FactGraphCaseScope` now restores the exact
definition/type/argument/expression/context scratch state on every exit.  It
removes both catch-all sites and avoids placing generic cleanup policy around
the hot mangle body.  The first typed form measured +2,304 bytes of text; the
scoped cleanup recovers 128 bytes of text and 68 bytes of exception data.

The generic audit falls by 37 logic and one runtime site, to 54/23/14, and the
catch-all inventory falls from 14 to 12.  PA14 passes 117/117 and through-PA14
passes 1,082/1,082.  Against E7s, `.text` changes 6,451,558 -> 6,453,734;
`.rodata` remains 214,720; `.eh_frame_hdr` 51,548 -> 51,588; `.eh_frame`
319,560 -> 319,272; and `.gcc_except_table` 123,936 -> 123,012.  Sixteen
pinned interleaved frozen runs reproduce object hash `8545fec6...` and give
identical baseline/candidate mean user time of 0.45125 seconds.  The small text
cost is therefore retained for the required typed separation and lower EH
footprint, with no measured hot-path regression.

### E7u execution record

The PA9 CY86 frontend, backend, program model, and ELF writer now distinguish
ill-formed CY86 source, fixed identifier/literal/opcode limits, output I/O,
and impossible parser/backend/image states through a centralized CY86-domain
cold boundary.  Token/operand/opcode recognition and lookup remain explicit
ordinary flow.  Backend-only 80-bit/operand contradictions are internal
rather than source recovery because the frontend has already validated those
forms.

The identifier table retains one cleanup/rethrow catch-all: after a hash-map
entry is inserted, failure to grow the parallel pointer vector must erase that
exact entry before rethrowing or the two structures diverge.  It never returns
an ordinary result or classifies the exception and remains for explicit E8
allowlist review.

The generic audit falls by 25 logic and 13 runtime sites, to 29/10/11.  PA9
passes 20/20 and through-PA9 passes 422/422.  Against E7t, staged `cy86`
`.text` changes 356,230 -> 359,558; `.rodata` remains 16,424;
`.eh_frame_hdr` 4,436 -> 4,508; `.eh_frame` 23,980 -> 24,364; and
`.gcc_except_table` 6,423 -> 5,158.  CY86 is not in the integrated compiler
source set, and the integrated binary remains exact at `44f180930596...`, so
the staged typed-boundary size tradeoff has no frozen/full timing exposure.

### E7v execution record

The final generic throws were confined to eleven staged executable adapters.
Invocation parsing now uses the appropriate driver, optimizer, or native
invocation boundary; output creation/writing uses typed driver I/O; and the
preprocessor output sink's invalid phase-7 token uses lexical source failure.
Terminal `CompilerError` presentation and exit status are unchanged.  These
adapters are not linked into the integrated compiler hot path.

The generic audit falls by the final 29 logic and 10 runtime sites, reaching
zero generic throws and zero generic-throw files.  Each changed executable's
invalid-invocation path returns failure.  A clean supported
`make test-report-through-pa38` passes 5,477/5,477; exception and source-set
audits pass.  (A preceding attempt to request eleven root `test-paN` targets
in one `make` invocation was discarded because their independent sub-builds
raced on shared `.tmp` dependency/config paths; it was not a test failure.)

Across the eleven staged binaries, `.text` changes 5,249,682 -> 5,254,834,
`.rodata` 214,310 -> 214,258, `.eh_frame_hdr` 52,820 -> 53,028, `.eh_frame`
304,416 -> 305,432, and `.gcc_except_table` 103,862 -> 104,002.  An initially
added ABI I/O overload also moved the integrated binary by 128 bytes; routing
that wrapper-owned operation through the existing driver I/O boundary restores
the exact E7t integrated compiler hash `44f180930596...`.  The staged size cost
therefore has no frozen/full compile-time exposure.

### E8 execution record

The final twelve catch-alls did not implement ordinary successful control
flow.  Ten were semantic rollback/state transactions: three paired constexpr
scratch marks, lambda-capture bounds/depth/failure state, placeholder context
and body state, alias hard-failure state, class partial-shape reset, function
default-request failure, and exception-specification deferral.  The other two
owned allocations between insertion into parallel containers.  All now use a
small scoped cleanup/commit owner.  The only remaining internal policy catch
is typed: dependent exception specifications cache `SemanticError` as a stable
failure and leave every other project disposition retryable.  Parser,
substitution, optimizer eligibility, lookup, and cache-hit alternatives remain
explicit Boolean/result flow.

This answers the hot-control-flow concern directly.  The successful frozen
census is zero throws, as was the most recent full-source census before this
cleanup-only slice.  Therefore none of these paths was repeatedly throwing as
a valid-input alternative.  The optimizer arena catch did put a landing pad
around a hot allocation owner, however; scoped allocation ownership removes
it and changes the integrated compiler by -384 bytes of text, -16 bytes of
unwind data, and -60 bytes of exception table relative to the semantic-only
candidate.  The analogous staged CY86 change is text/unwind neutral and saves
32 bytes of exception table.  Returned allocation status was rejected because
`operator new` and container growth already throw by protocol and translating
them would add a catch plus successful-path status plumbing.

Across all E8 changes, the integrated compiler moves from 6,453,734 to
6,453,478 bytes of `.text`; `.rodata` remains 214,720; `.eh_frame_hdr` remains
51,588; `.eh_frame` changes 319,272 -> 319,256; and `.gcc_except_table`
changes 123,012 -> 122,720.  The ten project exception types retain exactly 20
typeinfo/typeinfo-name symbols, and the source still contains 19 C++-language
RTTI operator sites unrelated to error policy.  Eight-run interleaved frozen
checks for each of the semantic and allocator slices reproduce the exact
98,736-byte object hash `8545fec6...`; one-tick user-time movement changes
direction between the two slices and is neutral.

The E8 audit now rejects every non-project explicit throw except the two
allocator-protocol `std::bad_alloc` sites, every generic logic/runtime throw,
every catch-all, internal generic standard catch, message-based policy, and
untyped terminal fallback.  It exact-counts the 15 reviewed executable
fallback catches and four `stoll`/`stoull` translations.  The final inventory
is 226 explicit typed throw sites and zero generic throw files.  Root
`test-report-nobuild` depends on this audit, so every cumulative through-target
report enforces the architecture rather than relying on a separately invoked
check.  PA9 passes 20/20, PA12 184/184, PA21 151/151, PA23 414/414, PA26
114/114, PA37 190/190, PA38 45/45, through-PA26 passes 3,822/3,822, and
through-PA38 passes 5,477/5,477.  The layout, rename-manifest, source-set,
semantic/lowering/native owner, LowIR-contract, and exception audits pass.
The PA39 file audit initially exposed the ABI guard's one-line destructor as a
scanner ambiguity and two native owners three/six lines over the hard limit;
inert formatting makes the destructor structurally visible and returns both
owners to exactly 3,000 lines.  The resulting compiler binary is byte-exact,
and the audit closes zero-fatal with the established 32 warnings.  Final full
performance, census, and inception gates are recorded in E9.

### E9 execution record

The final linkage audit separated the cheap throwing interface from the full
exception representation.  `support/exceptions.h` is now a 34-line
declaration-only helper surface, `support/exception_types.h` owns the compact
taxonomy, and `support/exceptions.cpp` owns constructors, accessors, terminal
presentation, and the shared domain throw helpers.  Domain error headers are
declaration-only; six redundant implementation owners were removed.  LowIR
identity failure helpers moved from inline model-header definitions to the
existing identity owner.  Every tool links the single support owner through
its responsibility source set, while only 32 source files include the full
taxonomy.  The exception audit enforces both properties in addition to the E8
zero-generic policy.

The final successful frozen compile and a fresh 221-source native census both
record zero calls to `__cxa_throw`.  Software `perf` sampling and per-phase
stats nevertheless found ordinary-flow interference in preprocessing.  The
historical fast tokenizer generated a 47-byte cached `Lexer::Peek(0)` query
and a separate 72-byte fill path; the first consolidated form generated a
259-byte combined specialization.  This was not the cost of throwing an
exception.  It was a source-call/inlining interaction around error-capable
fixed-queue operations and cursor boundaries.  Retaining a conditional
`PhysicalCursor::Next` boundary lets the self compiler choose its better
shape while GCC and Clang see the original `noinline` source and therefore
preserve their established code generation.

The required non-exception prototypes were measured rather than inferred.
Guard-proven unchecked queue operations reduced static instructions but were
neutral/slower (preprocess medians 487.3 versus 485.8 ms).  A general explicit
fast/slow `Peek` split regressed to 540.5 versus 486.7 ms; an offset-zero
wrapper regressed to 497.8 versus 487.7 ms; direct offset-zero calls regressed
to 514.4 versus 485.2 ms; and direct typed throws regressed to 630.0 versus
485.7 ms while greatly expanding text and EH data.  Branch hints, throw-owner
link position, returned token-paste objects, and cold typed paste helpers were
also neutral or worse.  All prototypes preserved the exact frozen object and
were removed.  The result-side audit is therefore conclusive for this phase:
adding unchecked APIs, out-parameters, moves, or repeated status branches does
not improve successful control flow merely by avoiding a cold throw.  The
remaining compact-query opportunity belongs to a future PA37 optimizer change
with structural and behavioral coverage, not a tokenizer workaround.

The final GCC-O3 compiler is `f70915c8...`.  Relative to E8 its sections move
as follows: `.text` 6,453,478 -> 6,443,430, `.rodata` 214,720 -> 214,656,
`.eh_frame_hdr` 51,588 -> 51,188, `.eh_frame` 319,256 -> 315,864, and
`.gcc_except_table` 122,720 -> 122,116.  The ten project types still contribute
exactly 20 typeinfo/typeinfo-name symbols.  An exact E8 replay compiling the
same current frozen source gives mean user time 0.784 seconds versus 0.766 for
E9 (-2.3%).  A same-current-source 223-unit O3 replay is neutral at 736.12
versus 736.25 aggregate compile CPU seconds.  This distinguishes the retained
change from the rejected source splits above.

Final eight-run frozen controls, all producing `8545fec6...`, are:

| Producer / requested level | Mean wall | Mean user | Median wall | Median user |
| --- | ---: | ---: | ---: | ---: |
| self O1 / O0 | 0.9712 s | 0.9300 s | 0.970 s | 0.930 s |
| self O3 / O0 | 0.7862 s | 0.7412 s | 0.780 s | 0.740 s |
| GCC O1 / O0 | 0.6025 s | 0.5587 s | 0.600 s | 0.555 s |
| GCC O3 / O0 | 0.4938 s | 0.4512 s | 0.490 s | 0.450 s |
| Clang O1 / O0 | 0.5875 s | 0.5438 s | 0.590 s | 0.540 s |
| Clang O3 / O0 | 0.5200 s | 0.4775 s | 0.520 s | 0.480 s |

Fresh, serially ordered 32-way full-build controls give self/GCC/Clang wall
times of 33.01/22.09/20.31 seconds at O1 and 29.44/18.76/18.29 seconds at O3.
Their user times are 887.17/552.03/522.95 and 736.25/455.72/469.91 seconds,
respectively.  The user-time self/GCC and self/Clang ratios are therefore
1.607x/1.696x at O1 and 1.616x/1.567x at O3.  These ratios are recorded rather
than hidden: header thinning helps GCC and Clang more than the current self
compiler.  The same-source E8 replay above shows no absolute self regression;
recovering the host-relative difference requires the future compact-query
optimizer work, not reintroducing duplicated exception definitions.

Root `make -j32 test-report-through-pa38` passes 5,477/5,477.  The exception,
layout, rename-manifest, frontend-source-set, semantic/lowering/native-owner,
and LowIR-contract audits pass.  The PA38 file audit remains zero-fatal with
the established 32 warnings.  The final native census is empty.  Fresh
32-way inception builds 223 objects in each generation; explicit object
comparison finds zero mismatches and both final compilers have SHA-256
`4398825aed4f...`.  No Cachegrind, Valgrind, or owned `perf record` process was
left running at closure.

### E10 regression correction

The E9 comparison established that its final consolidation was neutral against
E8, but it did not answer whether the complete typed-exception program was
neutral against the pre-conversion compiler.  A later identical-current-source
replay against E0 found a real cumulative regression: E9 was 1.30% slower at
O1 and 1.87% slower at O3.  Phase isolation assigned almost all of it to E5b's
lexical conversion.  A frozen tokenizer profile then showed the concrete
failure: the formerly small cached `Lexer::Peek(0)` path remained combined
with its fill path, and the O1 loop retained two cold invariant-helper arms.

The typed helper itself never executes on the successful workload.  Its call
shape nevertheless hid the fixed queue's non-returning failure from the
ordinary inliner and cleanup passes.  At O1 this kept redundant queue-size
loads and branches in the loop.  At O3 it prevented the stable-prefix query
pass from extracting the slow fill suffix.  The earlier E9 direct-throw
prototype was not a counterexample: `InternalCompilerError` then accepted only
`std::string`, so every template instantiation expanded string construction
and six additional EH blocks.  That version made the queue operation too large
to inline and was correctly rejected.

E10 gives `InternalCompilerError` a declaration-only `const char*` overload
whose implementation remains in the existing exception owner.  The three
fixed-queue invariant arms can therefore throw the required lexical-domain
typed exception directly without constructing a temporary string in the
tokenizer translation unit.  The queue operation stays four blocks, the O1
`Lexer::Peek` loop returns from eleven blocks to nine, and O3 again produces a
three-block cached query plus a separate noinline fill function.  All other
lexical error sites continue to use the shared cold helper.

A proposed generic loop-body reload forwarder also recovered the O1 shape, but
it was not retained.  It required a new optimizer rule and public PA37
coverage, while it still could not recover the O3 split from the opaque helper
shape.  The compact typed interface fixes the actual regression without a
LowIR contract or optimizer-policy addition.

Two serially ordered, all-32 runs per side on the same current 223-source tree
give:

| Requested level | E9 user seconds | E10 user seconds | Change | E0 replay | E10 versus E0 |
| --- | ---: | ---: | ---: | ---: | ---: |
| O1 | 887.455 | 863.035 | -2.75% | 872.945 | -1.14% |
| O3 | 733.805 | 717.400 | -2.24% | 722.705 | -0.73% |

Every baseline/candidate run produced the same 223 object files and final
compiler at its requested level.  The O3 tokenizer microbenchmark moves by
-2.59% user time; O1 is below that short lane's 10 ms resolution.  Current
host-produced controls and normalized aggregate-CPU ratios are:

| Requested level | E10 self | GCC control | Clang control | self/GCC | self/Clang |
| --- | ---: | ---: | ---: | ---: | ---: |
| O1 | 863.035 s | 550.94 s | 523.21 s | 1.566x | 1.650x |
| O3 | 717.400 s | 456.71 s | 469.68 s | 1.571x | 1.527x |

The three-way control also reproduced an existing host-producer presentation
difference in six objects: the Clang-built compiler selects different internal
string-record ordinals while self and GCC are exact.  Recompiling the untouched
E9 tokenizer reproduces the same difference, so E10 did not introduce it and
retention is based on each producer's deterministic output plus exact
self/inception output, not a false claim of new cross-producer identity.

The exception audit accepts 229 typed throw sites and remains at zero generic
throws, catch-alls, internal standard catches, message policy, and untyped
terminal fallback.  A direct constructor probe verifies disposition, lexical
domain, code, and message preservation.  No course fixture inspects this source
shape: it is an internal performance repair, not a new student-visible
language or IR contract.  Existing PA1/PA4 behavior, the architecture audit,
and cumulative `make -j32 test-report-through-pa38` pass 5,477/5,477.  Fresh
O3 inception uses 32 workers at every level, builds 223 objects, and matches
the restored self compiler exactly at SHA-256 `ed507c8ede83...`.

## Initial code map

| Concern | Primary owners |
| --- | --- |
| taxonomy and terminal presentation | `dev/src/support/exceptions.h`, staged tool `main` files, `dev/cppgm++.cpp` |
| syntax errors and speculative recovery | `dev/src/syntax/parser/cursor.h`, `dev/src/syntax/parser/parser.cpp`, syntax extensions |
| semantic recovery and cleanup | `dev/src/semantic/analysis/`, `constants/`, `templates/`, `expressions/`, `declarations/`, `object_model/`, `lifetime/` |
| token paste and early frontend | `dev/src/preprocess/`, tokenizer/post-tokenizer/recognizer owners |
| ABI fact input | `dev/src/abi/itanium/abi_mangle_parse.cpp` |
| LowIR input/model/output | `dev/src/lowir/io/`, `dev/src/lowir/model/`, LowIR tool adapters |
| compiler-object transport | compiler-object I/O/join owners and PA30 driver paths |
| lowering/optimizer/native | `dev/src/lowering/`, `dev/src/lowir/optimize/`, `dev/src/native/` |
| architecture enforcement | new exception audit script and root make audit wiring |

If a new `dev/src/*.cpp` owner is necessary, add it to every applicable tool
list in `dev/frontend_source_sets.mk`.  Prefer a compact header-only taxonomy
only if it does not grow code/RTTI across staged tools; otherwise use the
existing support ownership and measure the linked result.
