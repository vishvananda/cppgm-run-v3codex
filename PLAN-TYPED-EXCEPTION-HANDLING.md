# Plan: Typed Compiler Failures and Non-Exception Recovery

Status: in progress; E0 baseline and exception-census infrastructure complete

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
   unwinds.
5. Run fresh PA39 inception with all three job counts set to 32 and require
   every object and final compiler to match.
6. Remove temporary profiling/build roots, confirm no stale process, complete
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
| E0 | baseline and audit | generic standard categories and broad catches | report-only ceilings plus native throw census | PA1-PA39 | frozen 1; full 302; focused counts below | section baseline below | frozen/full baseline below | 5,473/5,473; output hashes below | pending | retained |

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
