# LowIR Contract Minimization Baseline

Analysis baseline: `4060618a` (the following plan-only commit does not change
compiler sources).

The baseline was rechecked on 2026-08-27 before implementation:

- PA13: 109/109;
- PA15: 121/121;
- PA29: 291/291;
- PA37: 187/187;
- PA38: 46/46;
- through PA38: 5,453/5,453;
- PA38 file audit: passed with the 36 established warnings; and
- 32-way inception: every object and the final compiler matched.

No Cachegrind, Valgrind, perf, detached benchmark, or earlier inception process
was active before the baseline runs.

## Public token census

Across assignment-local and shared course tests at the baseline:

- `select` occurs four times in two handwritten inputs and three times in one
  generated reference; there is no source or optimizer producer;
- `trivial_lifecycle=yes` occurs 224 times in checked-in references and zero
  times in handwritten input, while production has only its force-inline
  policy consumer;
- `arity=prototype_relaxed` occurs in one handwritten input and no generated
  reference;
- `section_segment` occurs in no input or reference and has no producer or
  target consumer; and
- `eh_type`, `eh_call_unexpected`, `eh_current_exception_type`, `eh_top`,
  `eh_value`, and `eh_unhandled` occur in no input or generated reference.

## Performance oracle

The compiler source is byte-identical to the final P32-L119 source measured in
`PLAN-HOT-LOOP-RESIDENCY.md`.  Its clean O1/all-32 medians are:

| Builder | Wall | User | System | Aggregate CPU |
| --- | ---: | ---: | ---: | ---: |
| self O1 | 32.01 s | 871.02 s | 49.65 s | 919.50 s |
| GCC O1 | 21.68 s | 545.02 s | 44.91 s | 589.93 s |
| Clang O1 | 21.61 s | 562.13 s | 44.76 s | 606.73 s |

The binding same-source ratios are 1.476x versus GCC and 1.481x versus Clang
by wall time.  Final acceptance compares a newly built self compiler with GCC-
and Clang-built compilers made from the same final sources; self time alone is
only a screen.

The exact retained O1 self compiler has SHA-256
`153f48841b6870e07adeb8fd0726657017932c5ba671febd039efe0175bb0289`.

## Durable-object controls

PA37's object-roundtrip lane is the baseline equality oracle.  Before a phase
claims completion, direct source emission and serialized LowIR replay must
remain byte-identical for the owning reducers:

| Fact | Baseline control | Direct/replay O1 SHA-256 |
| --- | --- | --- |
| lifecycle inline/root policy | `course/pa37/object-roundtrip/490-lifecycle-base-entry-inline.cpp` | `7f0844bffb1e05090f3b948ea9309eecae5412bcc298d8ff07dd5e4fb95a90b9` |
| exception/unwind boundary | `course/pa37/object-roundtrip/470-inferred-no-unwind-object-eh.cpp` | `373d7b7027a65945000ff74e978db1654b5dbefe9d868d80a89b013c486cf5d6` |
| reference/by-address ABI | `tests/object-roundtrip/100-lowir-object-boundary.cpp` | `80b150f82a0f3563bc8bf42e57409b1eb64208c4911b7749713a1ec4d93dadfa` |
| debug locations | `course/pa37/object-roundtrip/410-debug-local-literal-canonicalization.cpp` in debuginfo mode | `c532272962c33e6d1dfefd591f92e9888c095d6499bf324b2b5ffd4cda724c1a` |
| explicit section placement | missing at baseline; L6 must add the PA32 reducer before this fact is durable | missing |
| unreachable control flow | missing at baseline; L4 must add direct-versus-serialized coverage with the terminator | missing |

The last two missing rows are known baseline contract gaps, not passing
evidence.  L4 and L6 cannot close until their object relationships are covered.
