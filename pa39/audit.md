# PA39 Inception Audit

This audit treats the checked-in PA39 stage, not the prior green run, as the
review subject.  A requirement is closed only by current source/build evidence
and a fresh canonical check where state can affect the result.

## Audit Plan

1. Establish provenance: review the PA39 stage commit against `spec.md`, the
   PA39 contract, fixed source sets, Makefile generation graph, changed source,
   and every added reducer.  Confirm each reducer is owned by the earliest PA
   whose contract covers the defect.
2. Trace representative nontrivial declarations and demanded templates through
   source storage, preprocessing, integrated syntax/semantic construction,
   typed LowIR, per-function MIR, and direct ELF emission.  At each boundary,
   record representation ownership, canonical identity, lookup/demand behavior,
   lifetime, and whether any textual round trip or whole-program retry exists.
3. Audit the PA39 build graph for a fixed source manifest, immediate-generation
   compiler use, identical self/inception flags and generated configuration,
   deterministic object/link ordering, per-object and final byte comparison,
   and the absence of a restored/cached/host-produced success path in canonical
   targets.
4. Search implementation and build changes for PA39/test/source-name branches,
   reference or host-compiler delegation, serialized LowIR/MIR transport,
   unstable iteration, global rescans, unbounded fixed points, disabled work,
   weakened checks, and timeout/RSS changes.  Trace each candidate to its owner
   and either prove it is diagnostic/adaptor-only or fix it with an earliest-PA
   regression.
5. Compare host-seeded and self-built compilation of the same source set using
   elapsed time, CPU time, and peak RSS.  Check object counts and bytes, and
   repeat representative byte comparisons to distinguish implementation cost
   from layer divergence or nondeterminism.
6. Run the file audit, host PA1--PA38 report, self PA1--PA10 ladder, pptoken
   inception compare, and full compiler inception compare.  Record exact
   commands and outcomes only after the architecture review is closed.

## Findings

1. The PA39 stage is one commit, `d994aab5` (`Implement PA39 reproducible
   inception`), on the audited PA38 parent `28bebf67`.  The canonical PA39
   graph takes the per-tool manifests from `dev/frontend_source_sets.mk`, uses
   the same generated host configuration and compile flags for both compiler
   generations, compiles inception with the immediately preceding
   `cppgm++-self`, compares each object, links in manifest order, and compares
   the final executable.  The restored-self targets are explicitly diagnostic
   and are not prerequisites of either canonical compare.
2. The production path remains source/tokens -> compact syntax and canonical
   semantic IDs -> typed LowIR -> bounded function MIR -> direct ELF.  The
   textual LowIR parser and serializer are tool adapters, not production
   transport.  The host compiler is used only to link already-produced object
   files and to derive the fixed builtin configuration; no implementation path
   shells out to a host/reference compiler, consumes a reference answer, or
   recognizes PA39, a test filename, or compiler-source spelling.
3. The stage's `InternalIdentityClassifier` was an architecture defect.  PA15
   lowering reconstructed linkage by recursively walking types/entities,
   recognized anonymous namespaces through the rendered string `<unnamed>`,
   and treated a recursion edge as false in a way that made a cached answer
   traversal-order dependent.  Linkage is a semantic fact and must be
   published once for lowering.  A clean rebuild also exposed the related
   directionality rule: an internal alias may consume the identity of its
   canonical declaration, but it must not make that external canonical
   declaration internal.  Treating every unnamed class as internal similarly
   misclassified typedef-named classes.
4. Two other identity/order defects were present in PA39 changes.  Cached
   lambda rebinding converted interned `NameId` values back to strings and
   re-entered spelling lookup.  Native register reclamation selected the first
   eligible value from an `unordered_map`, allowing an unspecified iteration
   order to affect emitted code.
5. PA10's late class-name preindex scanned nested inline bodies repeatedly.
   Deeply nested class/member input could therefore produce quadratic parser
   work during self-compilation.  A translation-unit brace index permits each
   nested body to be skipped in constant time while preserving source-order
   discovery at the current class level.
6. Searches of the PA39 build/source changes found no disabled semantic work,
   whole-program retry loop, PA39-only compiler branch, source-name shortcut,
   cached-output success path, changed timeout/RSS threshold, or relaxed byte
   comparison.  The inliner instruction budget is a bounded optimization
   policy with telemetry, not a correctness skip.  The file audit's 23
   advisory header-division warnings predate this cleanup; no new substantial
   implementation was left in a header and there are no fatal findings.
7. All 49 stage reducers are in the earliest assignment whose observable
   contract owns the defect:

   - PA10 parser/name facts: `known-function-name-with-type-marker`,
     `late-nested-class-type-in-inline-member`,
     `parameter-shadows-template-comparison`,
     `parameter-shadows-template-strict-comparison`,
     `typedef-shadows-outer-value-in-member-function`, and
     `value-and-call-condition`.
   - PA12 lexical/semantic identity, lookup, access, and calls:
     `310-raw-string-underscore-delimiter`,
     `conditional-const-enum-preserves-type`,
     `private-nested-out-of-class-constructor-definition`,
     `private-nested-out-of-class-member-definition`,
     `qualified-base-data-member-hides-function`, and
     `reference-parameter-to-value-call`.
   - PA15 typed lowering/symbol identity:
     `extern-const-definition-retains-external-linkage`,
     `in-class-defaulted-assignment-is-weak`,
     `scalar-literal-reference-temporary-lowering`,
     `scalar-new-value-initialization-lowering`, and
     `static-member-callee-declaration`.
   - PA16 object lifetime/operator lowering and emission order:
     `100-user-destructor-anonymous-union-storage`,
     `310-enum-pseudo-destructor-call`,
     `array-member-empty-paren-mem-initializer`, and
     `floating-intrinsic-emission-order`.
   - PA19--PA21 templates and constant semantics:
     `local-type-namespace-template-specialization-abi` (PA19),
     `braced-constructor-function-pack` (PA20), and
     `address-of-incomplete-reference` plus
     `noexcept-in-suppressed-specialization` (PA21).
   - PA25 lambda identity/access: `cached-lambda-rebinds-automatic-capture`,
     `lambda-captures-this-for-out-of-class-member-call`,
     `lambda-implicitly-captures-this-for-member-call`, and
     `lambda-inherits-friend-class-access`; PA26 owns
     `noreturn-direct-object-fallback`.
   - PA29 register/value lowering: `branch-local-parameter-spill`,
     `call-result-branch-across-call`,
     `copied-compare-result-across-call`,
     `indirect-second-load-direct-compare-pressure`,
     `loop-invariant-parameter-across-call`,
     `loop-invariant-temporary-home`, `o2-one-past-local-callee-save`,
     `remapped-zero-index-parameter`, `stack-argument-register-clobber`,
     `store-frame-address-value`, and
     `wide-parameter-register-preservation`.
   - PA30 native EH/import/linkage/lifetime:
     `300-runtime-eh-handler-preserves-this`,
     `300-runtime-imported-function-address`,
     `300-runtime-left-nested-short-circuit-temporary-lifetime`,
     `300-runtime-stack-argument-call-unwind-cleanup`, and
     `310-anonymous-template-specialization-linkage`; PA31 owns
     `310-shared-conditional-cleanup-resume`, PA32 owns
     `200-host-extern-template-vtable-reference`, and PA37 owns
     `o2/escaped-slot-store-after-address`.

## Changes

- Added a typed, inherited `internal_linkage` scope fact.  Namespace analysis
  establishes it from syntax identity, and semantic/template consumers query
  the fact without inspecting namespace spelling.
- Replaced PA15's lowering-time classifier with the PA19-owned
  `PublishInternalIdentityFacts` semantic pass.  It uses dense stable IDs and a
  monotonic dependency worklist over types, entities, template arguments, and
  bindings.  Canonical facts flow from canonical declaration to alias/redecl,
  never backward; cycles converge without traversal-order caching.
- Kept PA15 lowering as a direct consumer of the published binding fact and
  removed `pa15_internal_identity.{h,cpp}`.  Registered the new semantic owner
  in the compiler source manifest.
- Replaced both lambda spelling round trips with canonical one-component
  `NamePath` lookup.
- Added PA10-owned brace matching and made class-name preindexing jump over
  nested bodies.  The implementation is in a small `.cpp`, and its source-set
  ownership is explicit.
- Made dead parameter-register reclamation inspect source parameter order,
  eliminating output-affecting `unordered_map` traversal.

## Performance Evidence

`/usr/bin/time -v` measured the same full source set after the semantic and
native fixes.  The only subsequent source change was extracting the 35-line
brace matcher into its own PA10 translation unit for stable ownership and the
file-audit line limit; the final 138-object canonical compare below covers that
split.

| Compilation | Compiler | Elapsed | User + system CPU | Peak RSS |
| --- | --- | ---: | ---: | ---: |
| Full `cppgm++-self` | host-seeded `dev/cppgm++` | 1:19.23 | 1192.22 s | 1,605,056 KiB |
| Full `cppgm++-inception` | `cppgm++-self` | 11:01.66 | 5059.99 s | 1,605,084 KiB |
| `lowir_native.cpp` object | host-seeded `dev/cppgm++` | 7.34 s | not sampled | 313,244 KiB |
| `lowir_native.cpp` object | `cppgm++-self` | 44.07 s | not sampled | 333,004 KiB |

The full self-built generation is 8.35x slower by elapsed time and 4.24x by
aggregate CPU, but its peak RSS differs by only 28 KiB (less than 0.01%).  The
representative largest backend source is 6.00x slower with 6.3% more peak RSS.
This supports a slower self-built code generator, not a self-only memory-growth
or repeated-work divergence.  No timeout, retry, job-count reduction, or OOM
exception was used to obtain a pass; the checked-in 900/3600-second per-object
limits and eight-job inception cap were unchanged.

The first clean audit build intentionally invalidated reused artifacts and
failed at link, exposing the alias-direction defect above.  That failed run is
not counted as performance success.  After correction, the canonical source
set contains 138 objects totaling 1,450,630,680 bytes; all 138 host-seeded and
self-built objects compare equal.  Both final compiler binaries are
1,373,767,208 bytes with SHA-256
`f72db73da0001824a038db60f3b55ce2120abe6ed643809c910e577c35f8a1b3`.
Both pptoken binaries are 13,050,072 bytes with SHA-256
`32d5c883906786787516986e4891a2bedbffe3cf7fe27e5d9c39f40412d7383c`.

## Validation

- `perl scripts/cppgm_file_audit.pl --stage pa39 --paths dev/src`: pass, zero
  fatal findings (23 advisory warnings).
- `make test-report-through-pa38`: pass, 5,138/5,138.
- `make -C pa39 test-through-pa10 CXX=../dev/cppgm++ CPPGM_HOST_CXX=g++`:
  pass for every self-built PA1--PA10 checkpoint.
- `make -C pa39 compare-pptoken-inception CXX=../dev/cppgm++
  CPPGM_HOST_CXX=g++`: pass with matching entry/shared objects and
  `MATCH pptoken`.
- `make -C pa39 compare-cppgm++-inception CXX=../dev/cppgm++
  CPPGM_HOST_CXX=g++`: pass with all 138 objects matching and `MATCH cppgm++`.
- Focused ownership checks also passed: PA10 157/157 plus 6/6 course, PA19
  293/293 plus 8/8 course, PA22 308/308 plus 2/2 course, and PA30 88/88 plus
  9/9 course.
