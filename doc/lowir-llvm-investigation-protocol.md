# LowIR/LLVM Investigation Protocol

Status: finalized for the reported investigation

This document fixes the commands, evidence format, corpus denominator, and
scratch policy used by `PLAN-LOWIR-LLVM-VALIDATION.md`. It is operational
documentation for the investigation; assignment fixtures remain the compiler
oracle.

## Normative inputs

The LLVM language source is the [LLVM 21.1 Language Reference
Manual](https://releases.llvm.org/21.1.0/docs/LangRef.html), supplemented by
the [LLVM 21.1 release
notes](https://releases.llvm.org/21.1.0/docs/ReleaseNotes.html). A copy fetched
from the versioned release site on 2026-08-22 had these SHA-256 digests:

```text
b4c36733988f37744a5458f2fd30c68b17a09de27b833384c208f741714510aa  LangRef.html
6ee828a8591eb4f0fd308205c7be8e2ffc4229f01e140f4d5f9b7203307948c2  ReleaseNotes.html
```

The investigation has directly reviewed, rather than inferred from Clang,
the following high-risk rules so far:

- well-formedness is stricter than textual parsing and includes SSA dominance;
- an `alloca` creates uninitialized storage of a sized type;
- overstating load/store alignment is undefined behavior, while alignment one
  is conservative;
- GEP performs address calculation only, its first index walks the pointer,
  and structure indices have additional constant/type restrictions;
- poison becomes immediate undefined behavior in branch conditions, memory
  addresses, divisors, callees, and `noundef` boundaries;
- `freeze` chooses one stable arbitrary value for `undef` or poison and does
  not make a frozen pointer dereferenceable;
- `tail` is only a hint, while `musttail` imposes correctness constraints;
- an `invoke` unwind destination begins with a `landingpad` as its first
  non-PHI instruction on the Itanium path;
- a landing pad needs at least one clause or the `cleanup` flag; and
- LLVM 21 spells a non-capturing pointer contract as `captures(none)`.

The Itanium C++ ABI and x86-64 System V psABI, linked from the plan, govern
facts which LLVM deliberately leaves target-specific.

## Pinned commands

Generate the machine-readable environment record outside the repository:

```sh
scripts/lowir_llvm_manifest.py --output "$scratch/toolchain.json"
```

Generate the three primary representations for one translation unit:

```sh
dev/cppgm++ --emit-lowir -O0 source.cpp -o "$case/subject.lowir"
dev/cppgm++ --emit-llvm-ir -O0 source.cpp -o "$case/ours.ll"
clang++ -std=gnu++11 -stdlib=libstdc++ -O0 -g0 \
  -S -emit-llvm -Xclang -disable-llvm-passes \
  source.cpp -o "$case/clang.ll"
```

The LLVM-reader validity gate is deliberately a compilation, not a syntax
grep:

```sh
clang -x ir -c "$case/ours.ll" -o "$case/ours.o"
clang -x ir -c "$case/clang.ll" -o "$case/clang.o"
```

Run an assignment range or the explicit later-assignment source list with:

```sh
scripts/run_lowir_llvm_sweep.py --output "$scratch/run" \
  --pa-from 19 --pa-through 24 --quiet
scripts/run_lowir_llvm_sweep.py --output "$scratch/hosted" \
  --source-list doc/lowir-llvm-curated-sources.txt --quiet
```

Run the behavioral triangle with:

```sh
scripts/run_lowir_llvm_behavior.py \
  --source-list doc/lowir-llvm-behavior-sources.txt \
  --output "$scratch/behavior"
```

Each non-comment line in the behavior list is one program. Multiple
whitespace-separated paths on one line are emitted independently and linked
together.

The installed toolchain has no version-matched `llvm-as` or `opt`. If those
tools become available, their major/minor version must match before adding
`llvm-as` and `opt -passes=verify` as additional gates.

Clang may select compiler-specific preprocessor branches. Hosted comparisons
therefore retain both macro/include digests from `toolchain.json`; equal
libstdc++ include roots alone do not prove equal preprocessed tokens.

## Corpus denominator

The runner enumerates `.t` source cases under both `paN/tests/` and
`cppgm.tests/course/paN/`, sorted by repository-relative path. A checked-in
`.ref.exit_status` of `EXIT_SUCCESS` puts the source in the successful-case
denominator. Expected rejection cases are counted separately and are never
reported as missing LLVM output.

Each successful source has one of these comparison states:

| State | Meaning |
| --- | --- |
| `complete` | LowIR, our LLVM, Clang LLVM, and both LLVM object gates succeeded; this is static completeness, not by itself a runtime claim |
| `exporter-limitation` | Existing LowIR succeeded and our adapter failed explicitly |
| `clang-noncomparable` | Existing compiler accepted an extension/input which pinned Clang rejected |
| `subject-failure` | Current LowIR path failed despite a successful checked-in oracle |
| `llvm-invalid` | Our adapter emitted text which the pinned LLVM reader rejected |
| `clang-ir-invalid` | The Clang-produced module failed its own reader gate; this is a toolchain fault |

`exporter-limitation` is coverage, not success. `llvm-invalid` is always an
adapter defect and must never be relabeled as an unsupported source feature.
Failures are categorized by a stable diagnostic prefix; the full stderr and
digest are retained.

The mandatory sweep is PA15 through PA28, including a complete PA19 through
PA24 template census. PA30 through PA36 use curated same-libstdc++ hosted and
multi-translation-unit representatives because many later harness `.t` files
are not standalone source translation units.

For PA30-PA36 descriptor-driven suites, `--source-list` names the actual
`.t.1`, `.t.2`, and later translation units. A descriptor oracle value of `0`
is normalized to `EXIT_SUCCESS`. PA29, most PA37 cases, and PA38 use LowIR as
their `.t` input, so those files are not miscounted as C++ sources.

## Scratch-output policy

All generated source-independent artifacts go under an explicit path supplied
to the runner, normally a fresh directory below `/tmp`. The runner refuses the
repository root and refuses to place output inside any `paN/`,
`cppgm.tests/`, `dev/`, or `doc/` directory. It may replace only a run
directory which it created and whose marker declares the expected schema.

No generated `.ll`, `.o`, executable, stdout, or Clang-derived `.ref` belongs
in assignment fixtures. Checked-in files are limited to the exporter,
reproduction/summary scripts, protocol, ledgers, crosswalk, minimized human
witnesses where necessary, and the final report.

## Artifact schema

One run has this layout:

```text
scratch/
  RUN-MARKER.json
  toolchain.json
  corpus.json
  summary.json
  cases/<stable-case-id>/
    case.json
    subject.lowir
    subject.stderr
    ours.ll
    ours.stderr
    ours.verify.stderr
    ours.o
    clang.ll
    clang.stderr
    clang.verify.stderr
    clang.o
```

Behavior runs use a parallel schema with one `case.json` per program, emitted
LLVM per translation unit, three linked executables, captured stdout/stderr,
exit status, timeout state, and SHA-256 digests. Observable behavior matches
only when all three lanes have the same status, stdout, and stderr.

Absent artifacts are represented in `case.json`; empty placeholder files are
not used. JSON is UTF-8, sorted by key, indented by two spaces, and terminated
by one newline. Paths inside JSON are repository-relative or artifact-relative
and never depend on the absolute scratch directory. Every produced artifact
records SHA-256, byte size, command, and exit status. Timing is optional and
is excluded from reproducibility digests.

`corpus.json` records enumeration and denominator decisions.
`summary.json` records counts by PA, state, diagnostic category, instruction,
attribute, intrinsic, and metadata family. A repeated run on the same revision
and toolchain must produce the same corpus and content digests.

The checked-in repeatability selection is
`doc/lowir-llvm-repeatability-sources.txt`. It contains scalar, class,
template, exception, and hosted probes. Two runs must have identical normalized
artifact digest lists, including records for explicit exporter limitations.

## Finding schema

Material differences receive an ID of the form `LLVM-LOWIR-NNN`. A finding
record has these fields:

| Field | Meaning |
| --- | --- |
| `id`, `title`, `status` | Stable identity and open/confirmed/rejected state |
| `classification` | spec gap, lowering defect, semantic gap, backend-only, optional optimization, LowIR-purposeful-extra, Clang choice, or adapter defect |
| `disposition` | accept-now, follow-up, measure-more, keep, reject, or not-lowir |
| `earliest_pa` | Earliest assignment owning the relevant contract |
| `witnesses` | Minimized source and full-corpus case IDs |
| `llvm_rule`, `abi_rule` | Direct section URLs and summarized preconditions |
| `semantic_fact` | The canonical fact available before either IR |
| `lowir_fact`, `clang_fact`, `ours_fact` | Normalized three-way evidence |
| `behavior` | Compile/link/run and observable-result evidence |
| `frequency` | Denominator and affected count |
| `recommendation`, `impact` | Proposed owner and expected spec/test/reference effects |
| `confidence`, `open_questions` | Evidence strength and remaining uncertainty |

Raw textual differences without a semantic or ABI consequence remain census
observations and do not receive a recommendation ID.

## Independence ledger

The adapter currently shares only these facts/helpers with production
lowering:

| Shared item | Disposition | Reason |
| --- | --- | --- |
| `SemanticGraphView`, `Program`, `DumpArena` | semantic fact | Canonical input intentionally common to both sibling consumers |
| canonical bindings, types, layout, constants, and value categories | semantic fact | Established before either low-level representation |
| `MangleFunction` and `MangleVariable` | ABI fact | Itanium symbol identity is not a LowIR lowering choice |
| x86-64 triple and probed data layout | ABI/target fact | Frozen by the comparison profile and checked against Clang |

The adapter does not construct a typed LowIR program, parse serialized LowIR,
call LowIR optimizers, or translate LowIR operations. Its LLVM module model,
SSA/CFG construction, storage/value split, and renderer are exporter-only.
