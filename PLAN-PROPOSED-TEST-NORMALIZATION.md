# Plan: Normalize Proposed Tests into the Course Suites

Status: complete and validated; prerequisite for `PLAN-O0-CLANG-PARITY.md`

Date: 2026-08-20

## Objective

Remove `proposed/` as a third, non-running test location before beginning the
next LowIR/MIR fixture migrations.  Every retained regression should have one
unambiguous status:

- an active course test under `cppgm.tests/course/paN/` at its earliest owner;
- an existing assignment fixture updated in place when that exact fixture
  already owned the behavior;
- a deliberately ill-formed active negative test when rejection is part of
  the assignment contract; or
- no test file when the item is only a deferred idea or an internal
  implementation-shape observation.

Reference disagreement is not a reason to keep a test outside the active
suite.  When the course deliberately adopts a new semantic, LowIR, MIR, or
object contract, update the authoritative reference and regenerate the active
fixture.  Do not preserve the old output as a second oracle.

After this work, `proposed/` should have no tracked files and should disappear
from the tree.

## Repository findings

The current directory contains 36 test anchors across PA11, PA12, PA15, PA16,
PA25, PA26, PA29, PA31, PA32, and PA37, plus one PA31 white-box C++ unit and
ten explanatory README files.

Git history shows that none of the current anchors was moved verbatim out of a
tracked active suite.  They were all added directly under `proposed/`.
However, two PA11 candidates preserve ill-formed fragments that were removed
from existing PA11 assignment fixtures.  Their valid positive fixtures should
remain corrected, while the rejected forms become separate active negative
course tests.

The current proposed files fall into four groups:

1. Standard or LowIR programs that should become active course regressions.
2. Deliberately ill-formed C++ programs that should become active negative
   course regressions.
3. Representation/performance cases whose transformations are not themselves
   public oracles, but whose execution still guards the safety of an active
   backend path.  These belong in a course behavior lane.
4. Nonconforming source, white-box implementation units, or unimplemented
   design experiments.  These must be rewritten, converted to public
   end-to-end coverage, or removed rather than given another test directory.

PA29 currently has an additional presentation inconsistency:

- all 88 assignment-local behavior tests produce `.my.mir`, but only four
  retain a checked-in `.ref.mir`;
- those four reference MIR files are currently compared exactly because the
  behavior lane is routed through the generic MIR comparator, contrary to the
  PA29 README's behavior-only rule;
- 42 of the 46 flat course anchors have no checked-in reference MIR; and
- the other four course anchors intentionally use exact MIR and were described
  by the earlier placement plan as course-exact fixtures.

The normalization should preserve reference MIR for students without turning
it into an oracle in the behavior lane.

## Permanent placement policy

### New tests

All new regressions go under `cppgm.tests/course/paN/` at the earliest PA that
owns the behavior.  Do not add another `proposed/`, `candidate/`, or
`maintainer-tests/` tree.

The assignment-local `paN/tests/` trees remain the original handout suites.
Edit an existing local input or reference in place only when correcting that
fixture or deliberately migrating its established contract.  Additional
coverage goes in the course tree.

### Reference disagreements

If a new course requirement intentionally differs from the current pinned
reference:

1. approve the new public contract;
2. update the owning student README and scaffold when applicable;
3. select and build a clean detached local reference implementation at the
   approved contract commit;
4. regenerate the course reducer and affected existing fixtures through the
   documented `ref-test` target, passing that binary with `REF_TEST_APP`; and
5. review the complete fixture census before committing.

Never copy `.my` output from the implementation under test into `.ref`, and
never keep the same regression under `proposed/` merely because the pinned
bundle still represents an earlier official export.  A typical regeneration
command is:

```sh
make -C paN ref-test \
  REF_TEST_APP=/absolute/path/to/clean-snapshot/dev/<tool> \
  TEST=course/paN/<test>.t
```

The local reference binary must come from a clean, named commit that contains
the approved contract, and regeneration must still go through the owning
assignment's `ref-test` runner.  Do not change `reference-binaries/manifest.tsv`
or publish a replacement downloadable bundle for an implementation-branch
fixture migration.  The pinned bundle changes only when `cppgm-extended` is
officially updated and its export workflow publishes the corresponding
reference binaries.

### Invalid and undefined source

An ill-formed program belongs in the active course suite when the owning PA is
required to reject that construct.  Give it a `-bad` name and a failure-status
oracle.  It is not an `undefined/` test and should not be described as a valid
positive example.

Source that declares an implementation-reserved identifier does not establish
a portable student requirement.  Rewrite it using ordinary identifiers when
the underlying behavior is observable in a conforming program.  Otherwise
remove the source test and cover only the nearest standard-observable
invariant.

### Internal and performance-only properties

A source or LowIR program that exercises an active native transformation may
be an active behavior regression even when native instruction bytes are not a
course oracle.  The behavior case proves that the transformation is safe;
benchmark telemetry proves that it remains effective.

For PA29, a behavior test still retains the authoritative `.ref.mir` as an
informational artifact.  The harness must generate and require that file but
must not compare it with `.my.mir`.  Students can inspect or diff the two while
using program execution as the grading oracle.

Do not retain a test whose only assertion is an unobservable private data
structure.  Either expose a stable public inspection fact owned by an
assignment, replace the case with end-to-end behavior, or delete the test.
Deferred, unimplemented ideas belong in a durable design plan without a test
anchor.  A new active course reducer is added when implementation begins.

## Detailed disposition

### PA11: promote the two corrected-away forms as negative tests

Keep the corrected positive assignment fixtures in place:

- `pa11/tests/spec/200-enum-scoped.t` must continue comparing scoped
  enumerators with scoped enumerators; and
- `pa11/tests/spec/200-namespace-anonymous-union-injected-members.t` must
  retain the required namespace-scope `static` specifier.

Move the rejected forms into the active course suite, rename them as negative
tests, and generate failure references:

- `namespace-nonstatic-anonymous-union.t` becomes a PA11 `-bad` course test;
- `scoped-enum-integral-comparison.t` becomes a PA11 `-bad` course test.

GCC and Clang both reject these inputs under `-std=c++11 -pedantic-errors`.
Add the two rejection requirements to the relevant PA11 normative feature
text.  No student scaffold change is needed.

### PA12: remove reserved-identifier collisions and replace the active cases

`local-type-generated-identity-not-in-lookup.t` declares `__local_type1`, an
identifier reserved to the implementation.  Its disagreement with the old
reference is not observable for a conforming source program.  Do not promote
this input.

The same problem exists in two active assignment-local tests introduced with
the underlying fix:

- `210-generated-anonymous-enum-name-not-visible-bad.t`; and
- `210-user-type-shares-generated-anonymous-enum-name.t`.

Remove those reserved-spelling fixtures and replace them with a PA12 course
reducer that uses only standard identifiers and creates multiple anonymous
local or namespace types through ordinary declarators.  Its semantic dump
must prove that the compiler gives the distinct anonymous entities distinct
typed identities and resolves their declared objects and enumerators
correctly.  Do not attempt to make a conforming source name a compiler-private
identity.

Keep the implementation invariant that generated identities never enter
ordinary lookup, but describe it only as a PA12 `Design Notes` recommendation;
it is not a source-level grading rule by itself.

### PA15: rewrite and promote generated-name collision coverage

Rewrite `reserved-pattern-local-names.t` so every source identifier is
standard-conforming.  Retain ordinary names such as `t2`, `t3`, `t5`, and
`t7`, which still collide with the LowIR temporary presentation family.
Remove spellings containing a double underscore, including
`retmerge__1` and `__force_inline_slot_2`.

Promote the rewritten reducer to PA15 course coverage.  The active requirement
is that source locals retain distinct LowIR identities and generated
temporaries do not collide with them.  The relaxed LowIR comparator should not
turn one particular unused ordinal-selection algorithm into a normative
requirement.  PA15's main contract states the uniqueness requirement; an
efficient typed reservation strategy belongs in `Design Notes`.

### PA16, PA25, and PA26: promote valid source regressions

Promote these inputs directly to their existing earliest owners:

- PA16: `local-class-default-member-enclosing-constant.t`;
- PA25: `lambda-local-static-per-specialization.t`; and
- PA26: `200-constructor-unwind-shares-generated-suffix.t`.

All three are accepted by GCC and Clang under strict C++11.  Regenerate their
LowIR references using the updated authoritative implementation.  The PA25
fixture adopts the current compact typed lambda/local-static identity as the
course presentation.  The PA26 fixture becomes part of the cleanup-sharing
contract and is migrated again, if necessary, by the later cleanup-DAG phase.

Update the PA16, PA25, and PA26 normative text only where the current contract
does not already state the behavior.  Keep identity-table and cleanup-table
implementation advice under `Design Notes`.

### PA29: promote all 25 anchors and normalize both course test shapes

Every PA29 candidate is valid LowIR and exercises an implemented backend
path.  Move all 25 anchors to `cppgm.tests/course/pa29/`.  Do not require one
oracle strength for all of them.

Use structural MIR plus execution for the public MIR-placement cases:

- `direct-call-result-consumers.t`;
- `direct-return-placement.t`;
- `discarded-slots-do-not-reserve-frame.t`;
- `incoming-parameter-emitted-clobbers.t`;
- `index-address-placement.t`;
- `indexed-memory-addressing.t`;
- `nonadjacent-object-result-frame-placement.t`;
- `representation-preserving-copy-placement.t`;
- `single-block-call-argument-coalescing.t`;
- `typed-i128-high-word.t`; and
- `unused-index-address-elision.t`.

Use behavior grading, with informational reference MIR, for cases whose
important optimization happens below public MIR or whose safe allocation has
multiple valid shapes:

- `constant-byte-store-coalescing.t`;
- `dead-address-copy-index-load-folding.t`;
- `dead-address-copy-index-store-folding.t`;
- `dead-address-copy-load-folding.t`;
- `dead-address-copy-store-folding.t`;
- `dead-address-load-folding.t`;
- `dead-address-store-folding.t`;
- `dead-copy-store-folding.t`;
- `flag-safe-zero-materialization.t`;
- `i128-multiply-caller-saved-clobber.t`;
- `narrow-zero-extension-encoding.t`;
- `redundant-u32-normalization-encoding.t`;
- `transient-scratch-address-folding.t`; and
- `zero-compare-test-encoding.t`.

The behavior cases are still valuable: they execute the exact patterns on
which the encoder peepholes operate and catch unsafe folding or clobbering.
They do not assert native byte counts or require the student's MIR to match
the reference.  Continue measuring instruction and object-size effects
through the frozen benchmark and typed telemetry.

Give the course tree the same explicit test shapes as the assignment-local
suite:

```text
cppgm.tests/course/pa29/
  strict/
  structural/
  behavior/
```

Normalize the existing flat course anchors as follows:

- move the four existing course-exact cases into `strict/` with their current
  exact MIR oracle: `copied-compare-result-across-call`,
  `fallthrough-jump-encoding`, `immediate-move-encoding-boundaries`, and
  `scalar-copy-location-sharing`;
- move the other 42 existing course anchors into `behavior/`; and
- generate and check in authoritative informational `.ref.mir` files for all
  42 rather than omitting MIR from the handout.

Place the eleven promoted public-placement candidates under `structural/`
with both raw `.ref.mir` and canonical `.ref.cmir`.  Place the fourteen
behavior candidates under `behavior/` with informational `.ref.mir` plus the
normal execution references.

Add a distinct `mir_behavior_t` comparison mode:

- require compiler and program outcomes to match;
- on successful reference compilation, require both reference and generated
  MIR files to exist;
- retain the reference MIR for inspection but do not compare its contents;
- keep strict comparison in `strict/`; and
- keep canonical structural comparison in `structural/`.

Route both assignment-local and course `behavior/` directories through that
mode, including single-file `make check TEST=...` invocations.  Backfill
informational `.ref.mir` for the 84 assignment-local behavior tests that lack
one.  The four assignment-local behavior tests that already retain MIR must
stop comparing it exactly; focused strict/structural tests elsewhere continue
to own the relevant public placement rules.

Update the PA29 Testing section to explain that all successful tests expose
reference MIR, while only strict and structural lanes grade its shape.
`ref-test-pa29` already requests MIR from the reference compiler; extend its
post-processing to create `.cmir` only for structural directories and retain
raw MIR in all three directories.

### PA31: promote end-to-end safety, remove the white-box unit

Move `adjacent-lsda-call-site-coalescing.t` and its `.t.1` source into the PA31
course suite.  Expand the active source family so runtime coverage includes:

- adjacent protected calls with the same cleanup;
- an intervening unprotected potentially throwing call;
- different cleanup landing continuations; and
- different action/catch continuations.

These cases ensure that LSDA coalescing never changes destructor, catch, or
resume behavior.  They should not require a particular LSDA byte layout or
call-site count from students.

Delete `lsda-call-site-coalescing-unit.cpp`.  It includes private backend
headers and asserts the current `HostFunctionLayout` representation, so it is
not an appropriate course test.  The frozen benchmark's LSDA counters retain
the performance signal; the course source retains the correctness signal.

### PA32: promote retained demand pruning, remove the deferred experiment

Promote `unreachable-internal-function-pruning.t`, its source sidecar, and its
inspection expectation into the PA32 course suite.  Update PA32's normative
object-demand contract to require omission of an unreferenced
translation-unit-local function while retaining an address-used control.
Regenerate the inspect oracle from the updated authoritative reference.

Do not promote `post-optional-inline-weak-pruning.t`.  The optimization is not
implemented: its prototype broke approximately 50 PA32/PA33 cases, and its
cross-object ownership rule remains unresolved.  Move the design question and
reducer description into `PLAN-OBJECT-DEMAND.md`, then delete the anchor,
source, and inspect expectation.  If that optimization is later approved, add
a new active course test at its actual optimization/object owner; do not
resurrect a proposed fixture.

### PA37: make the inliner budget an active, documented course requirement

Move and rename `380-inline-growth-budget.t` under
`cppgm.tests/course/pa37/o1/`.  Update the PA37 contract to require one bounded
whole-caller growth budget so repeated individually profitable calls cannot
produce unbounded compile work or output growth.

The fixture should test the documented deterministic budget rather than the
old reference's unlimited per-site behavior.  Regenerate its LowIR reference
from the updated authoritative optimizer.  Put the recommended compact
counter/cost implementation under `Design Notes`; do not include benchmark or
migration history in the student README.

## Implementation sequence

### N0: establish policy and harness support

1. Add the permanent placement rules to `TESTING_AND_REFERENCES.md`.
2. Add PA29's explicit behavior comparator and focused harness self-tests.
3. Split the flat PA29 course suite into strict, structural, and behavior
   directories, retaining the four current exact comparisons.
4. Backfill informational MIR for local and course behavior tests and verify
   that changing only an informational MIR file does not fail the behavior
   lane.
5. Build a clean detached local reference binary at the approved contract
   commit and pass it to `ref-test`; leave the pinned bundle unchanged until
   the same contract is officially exported from `cppgm-extended`.

Commit this harness/policy change separately.

### N1: normalize PA11 through PA26 source tests

1. Add the two PA11 negative course tests without reverting the corrected
   positive assignment inputs.
2. remove the reserved-identifier PA12 proposed input and replace the related
   active reserved-spelling fixtures with standard-conforming identity
   coverage;
3. rewrite and promote the PA15 collision reducer; and
4. promote the PA16, PA25, and PA26 reducers.

Regenerate references at each earliest owner and run the complete through-PA
report before committing that owner.  These may be separate commits if their
authoritative reference updates are independent.

### N2: normalize the PA29 backend corpus

1. Add the eleven structural MIR reducers with `.ref.mir` and `.ref.cmir`.
2. Add the fourteen behavior reducers with informational `.ref.mir` and no
   MIR comparison.
3. Regenerate all references through `ref-test-pa29`, passing the clean local
   `lowir2native` reference binary with `REF_TEST_APP`.
4. Run PA29 alone, the PA29 through-report, PA38's O1/O2 fixture census, and
   the full report.

Commit PA29 as one fixture/harness migration so no intermediate commit has
half-active course coverage.

### N3: normalize PA31, PA32, and PA37

1. Replace the PA31 white-box test with the expanded active course runtime
   family.
2. Promote the retained PA32 internal-demand inspection case.
3. move the deferred post-inline weak-pruning evidence into its design plan
   and delete the test files; and
4. promote the PA37 function-wide inliner-budget reducer with its README
   contract.

Validate and commit each owning PA independently.

### N4: remove the obsolete tree and close the audit

1. Delete every `proposed/paN/README.md` after its inventory has been handled.
2. Confirm `git ls-files proposed` returns no paths.
3. Search all plans and handouts for instructions to put tests under
   `proposed/` and replace them with the active-course policy.
4. Add a completion entry to this plan recording every promoted, rewritten,
   and deleted anchor and the clean local reference commit used.  Record a
   bundle version only when this work coincides with an official
   `cppgm-extended` export.

## Reference and validation gates

For each owning PA:

1. Generate references only through the documented `ref-test` workflow, using
   `REF_TEST_APP` to select a clean detached local binary at the approved
   contract commit.  Do not update the pinned bundle for branch-local work.
2. Inspect every changed input, `.ref`, exit-status, MIR, canonical MIR, and
   object-inspection sidecar.
3. Run `make test-paN`.
4. Run `make test-report-through-paN`.
5. Use `make test-report ACTIVE_TEST_REPORT_PAS='...'` while iterating so the
   entire failure set is collected at once.

After all moves:

```sh
make test-report
perl scripts/cppgm_file_audit.pl --stage pa39 --paths dev
```

The file audit must have zero fatal findings.  Run `make test-strict` only when
the optional byte-exact witness surface is changed; the production `cppgm++`
driver accepts but does not implement witness logging, so that reference-
maintainer lane is not a product gate for this migration.  Inception is not
required for a test-location-only normalization, but any compiler change
needed to satisfy a newly approved semantic contract follows that compiler
change's normal PA39 validation requirements.

No test run should read anchors from `proposed/`; deletion of that tree is part
of the success condition, not a harness change that silently stops running
tests.

## Completion ledger

| Owner | Proposed anchors | Destination | Special handling | Status/commit |
| --- | ---: | --- | --- | --- |
| PA11 | 2 | course negative tests | retain corrected positive local fixtures | complete; local reference snapshot `25208d8d` |
| PA12 | 1 | no direct promotion; replacement course reducer | remove three reserved-spelling inputs including two active local cases | complete; local reference snapshot `25208d8d` |
| PA15 | 1 | course LowIR | rewrite double-underscore source names | complete; local reference snapshot `25208d8d` |
| PA16 | 1 | course LowIR | authoritative ref update | complete; local reference snapshot `25208d8d` |
| PA25 | 1 | course LowIR | adopt typed lambda/local-static identity | complete; local reference snapshot `25208d8d` |
| PA26 | 1 | course LowIR | cleanup-sharing contract | complete; local reference snapshot `25208d8d` |
| PA29 | 25 | 11 structural plus 14 behavior course tests | three course lanes; informational MIR in behavior | complete; local reference snapshot `bc9a2062` |
| PA31 | 1 | course runtime family | delete private-header unit; existing `340` plus new `360`-`380` cover every safety boundary | complete; local reference snapshot `a810c60e` |
| PA32 | 2 | one course inspect; one design-plan-only deletion | weak pruning remains deferred | complete; local reference snapshot `a810c60e` |
| PA37 | 1 | course O1 | documented whole-caller budget | complete; local reference snapshot `a810c60e` |

### Completed anchor inventory

Normalization landed in six independently validated changesets:

- `25208d8d` established the permanent policy and PA29 strict, structural,
  and behavior lanes.  It also made reference MIR mandatory but informational
  for behavior tests and backfilled the assignment-local and course behavior
  corpus.
- `ac1c39a2` normalized PA11 through PA26.  It activated the PA11 negative
  tests `namespace-nonstatic-anonymous-union-bad` and
  `scoped-enum-integral-comparison-bad`; deleted the reserved-identifier PA12
  candidate and the two related assignment-local reserved-spelling fixtures;
  replaced them with `distinct-anonymous-type-identities`; rewrote and
  activated PA15 `generated-temporary-name-collision`; and activated PA16
  `local-class-default-member-enclosing-constant`, PA25
  `lambda-local-static-per-specialization`, and PA26
  `200-constructor-unwind-shares-generated-suffix`.
- `bc9a2062` corrected unsigned multiply serialization exposed by the PA29
  i128 reducer.  `a43d36ee` then activated all eleven PA29 structural anchors:
  `direct-call-result-consumers`, `direct-return-placement`,
  `discarded-slots-do-not-reserve-frame`,
  `incoming-parameter-emitted-clobbers`, `index-address-placement`,
  `indexed-memory-addressing`,
  `nonadjacent-object-result-frame-placement`,
  `representation-preserving-copy-placement`,
  `single-block-call-argument-coalescing`, `typed-i128-high-word`, and
  `unused-index-address-elision`.  The same changeset activated all fourteen
  PA29 behavior anchors: `constant-byte-store-coalescing`,
  `dead-address-copy-index-load-folding`,
  `dead-address-copy-index-store-folding`,
  `dead-address-copy-load-folding`, `dead-address-copy-store-folding`,
  `dead-address-load-folding`, `dead-address-store-folding`,
  `dead-copy-store-folding`, `flag-safe-zero-materialization`,
  `i128-multiply-caller-saved-clobber`, `narrow-zero-extension-encoding`,
  `redundant-u32-normalization-encoding`,
  `transient-scratch-address-folding`, and `zero-compare-test-encoding`.
- `a810c60e` implemented the public PA32 requirement exposed by
  `unreachable-internal-function-pruning`, using typed object roots without
  adding serialized LowIR metadata.
- `42eae2a9` activated PA31
  `360-adjacent-lsda-call-site-coalescing`, added the public end-to-end
  boundary cases `370-lsda-distinct-cleanup-landings` and
  `380-lsda-distinct-catch-actions`, and deleted the private-header
  `lsda-call-site-coalescing-unit.cpp`.  It activated PA32
  `unreachable-internal-function-pruning`, deleted the unimplemented
  `post-optional-inline-weak-pruning` anchor after moving its design evidence
  to `PLAN-OBJECT-DEMAND.md`, and activated the renamed PA37 O1 fixture
  `390-inline-growth-budget`.

References were generated only through the owning PA `ref-test` workflows,
with `REF_TEST_APP` selecting clean detached local binaries at snapshots
`25208d8d`, `bc9a2062`, and `a810c60e` as the implementation advanced.  No
`.my` output was promoted to a reference.  The pinned downloadable reference
bundle was deliberately left unchanged; it will move only with an official
`cppgm-extended` update and export.  The former directory now has no tracked
files and has been removed from the working tree; active plans and handouts
contain no instruction to recreate it.

Final validation on 2026-08-20 passed the full report at 5,263/5,263 and the
PA39 file audit with zero fatal findings and 31 warnings.  Because two compiler
fixes were required, the PA39 inception lane was also run: every object and the
final compiler matched, in 4:19.41 wall with 225,024 KiB peak RSS.  The optional
strict witness command was audited separately: all 415 reported failures were
missing `.my.witness` outputs, with no compared-output mismatch, because the
production driver has never emitted the reference compiler's optional witness
log.  No normalized fixture touches that surface.

## Completion criteria

Normalization is complete only when:

- `proposed/` contains no tracked files;
- all retained new regressions live under `cppgm.tests/course/paN/`;
- existing assignment fixtures changed by an approved contract have been
  regenerated in place rather than duplicated;
- the PA11 rejected forms are active negative tests, not positive examples;
- no active PA12/PA15 source relies on a double-underscore identifier to test
  compiler-generated identity;
- PA29 course tests live in strict, structural, or behavior lanes, with
  informational reference MIR retained in behavior;
- no course test includes private compiler headers solely to assert an
  internal representation;
- deferred weak pruning is documented without a dormant test anchor;
- student READMEs contain current requirements and non-normative Design Notes,
  with no migration history;
- every reference came from the authoritative workflow; and
- the full report and zero-fatal file audit pass; and
- when compiler changes were required, PA39 inception matches every object and
  the final compiler.
