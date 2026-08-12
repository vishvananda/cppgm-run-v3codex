# PA27 Final Audit

## Checkpoint Ledger

| Area | Independent conclusion | Status |
|---|---|---|
| Requirements | PA27 requires nonvirtual multi-base layout/access/lifecycle, member-pointer target plus adjustment, target-based bool semantics, ambiguity rejection, and single-vptr `dynamic_cast<void*>`. | closed |
| Base graph | The old nonlinear query enumerated inheritance paths and could accept an ambiguous base when callers omitted an ambiguity output. | fixed |
| Member pointers | Base-to-derived conversion previously relabeled the type without retaining a later-base adjustment; calls/data access could address the wrong subobject. | fixed |
| Access | Several consumers followed only the primary base, and the first audit repair omitted selected edges for an all-public path to a protected member. | fixed |
| Lifecycle | Inherited constructors assumed the primary base and skipped construction of the other direct bases. | fixed |
| Templates/ADL | Conversion-function template and associated-entity discovery followed only the primary base. | fixed |
| Performance | Path enumeration was exponential; eager repeated-base detection was superlinear on unique fan-in chains. | fixed |
| File ownership | Audit changes crossed line/function limits in legacy owners. | fixed |
| Self-containment | No compiler/reference shell-out, expected-output lookup, filename shortcut, or test-source hardcoding was found in the compiler path. | pass |

## Final Findings

No open correctness, architecture, performance, self-containment, or fatal
file-audit findings remain for PA27. Checked-fixture compatibility intentionally
keeps zero-offset repeated empty bases and the dormant RTTI fallback block; the
active required behavior is deterministic and does not depend on the fallback.

## Changes

- Added a versioned flat base-path cache and iterative DAG memoization with
  capped multiplicity, selected edge ordinals, offset, public-path, and
  ambiguity facts; exposed work counters.
- Rejected ambiguous base projections/member-pointer applications and made
  protected/friend access traverse the actual selected base path.
- Preserved base offsets through member-pointer constants and runtime
  conversions; adjusted data access and member-function `this` before use;
  kept null conversion target-based.
- Fixed inherited-constructor ownership/order for a non-primary base and kept
  all other direct-base initialization actions.
- Extended protected-object, associated-entity, and conversion-function
  template traversal across the full nonvirtual base DAG.
- Added a compile-fail regression for ambiguous member-pointer object
  application and isolated base-path ownership in a PA27 source module.

## Performance Evidence

The final repeated-diamond witness doubles depth from 128 to 1,024 while edge
visits track 899 to 7,171 and semantic time tracks 12.3 to 102.8 ms. The unique
fan-in witness doubles from 2,048 to 8,192 layers while queries, semantic time,
and storage approximately double each step (24,576 to 98,304 queries; 133.2 to
600.7 ms; 33.8 to 135.3 MB). No unexplained superlinear path remains in the
measured PA27 ownership surface.

## Validation

- `perl scripts/cppgm_file_audit.pl --stage pa27 --paths dev/src`: pass, no
  fatal issues (20 inherited advisory warnings).
- `make test-pa27`: pass, 97/97.
- `make test-report-through-pa27`: pass, 3,814/3,814 tests and 27/27 stages.
- `git diff --check`: pass.
