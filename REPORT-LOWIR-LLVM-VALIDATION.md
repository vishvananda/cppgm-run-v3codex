# LowIR Validation Through an Independent LLVM IR Export

Status: implementation and investigation complete; final repository gates are
recorded in section 11.

Date: 2026-08-22

## 1. Executive conclusion

The investigation found one correctness-bearing LowIR gap, one durable-boundary
mismatch, and several lower-priority precision or portability questions. It did
not find evidence that LowIR should be replaced by LLVM IR or broadly expanded
with Clang's optimization metadata.

The highest-priority issue is **volatile access**. The semantic graph knows
whether an object is volatile, but serialized LowIR load/store operations have
no volatile fact. A minimized witness emits two ordinary LowIR stores, and
`lowiropt -O2` deletes both; Clang emits `store volatile` for both accesses.
This is observable-language behavior, not an optimization preference.

The second concrete issue is **section placement**. `SymbolMetadata` carries
`section_name` and `section_segment` in the in-memory/object path, but public
LowIR text neither serializes nor parses them. That conflicts with the stated
role of LowIR text as the durable backend boundary for the later hosted/object
assignments.

The main negative conclusion is equally important: most metadata seen in
Clang's LLVM IR is not missing language information. `noundef`, `nonnull`,
`dereferenceable`, TBAA, loop metadata, `mustprogress`, unwind-table policy,
module flags, compiler identity, and O0 attributes are optimizer promises,
debug facts, target policy, or bookkeeping. Adding them to LowIR by imitation
would make the contract larger and could introduce undefined behavior when a
promise is overstated.

LowIR also intentionally has useful facts LLVM normally erases: projection
kind, reference/decay/pass origin, runtime roles, trivial-lifecycle state, and
object-emission preferences. Those should be kept.

Confidence is high for PA15 procedural lowering, the static LowIR/LLVM fact
census, and the confirmed volatile defect. Confidence is moderate for template
emission and scalar ABI conclusions. Confidence is deliberately low for class
value ABI, complete lifetime, RTTI, virtual inheritance, and EH lowering: the
independent exporter reports these as explicit coverage limits instead of
approximating a second production backend.

## 2. Instrument built for the investigation

The repository now has an experimental `cppgm++ --emit-llvm-ir -O0` mode. It
consumes `SemanticGraphView` directly and constructs a typed LLVM module; it
does not construct, parse, inspect, or translate LowIR. The only lowering
helpers shared with the production LowIR path are Itanium symbol mangling and
canonical semantic/type/layout facts established before either backend.

Implemented coverage includes:

- one LLVM module per translation unit;
- fixed x86-64 triple and Clang-matched data layout;
- typed scalar, pointer, array, struct, function, constant, operand, block,
  instruction, global, and function models;
- exact integer and `float`/`double`/`x86_fp80` constants;
- scalar globals and structured array/address initializers;
- scalar function boundaries, direct and indirect calls, references, and
  dynamic scalar allocation;
- scalar expressions, conversions, comparisons, short-circuiting,
  conditionals, assignments, pointer arithmetic, and subscripting;
- local arrays and initialization;
- `if`, loops, switch, goto/labels, PHIs, return, and unreachable; and
- conservative LLVM emission: no `inbounds`, `nsw`, `nuw`, `exact`, fast-math,
  alias, dereferenceability, or `noundef` promises without proof.

The exporter explicitly stops at unsupported class construction/value
transfer, complex lifetime, vtables, RTTI, EH, and hosted STL body lowering.
The most common limitation categories in the PA15–PA28 successful denominator
were:

| Explicit limitation | Cases |
| --- | ---: |
| constructor action | 940 |
| unresolved id expression after unsupported demand | 146 |
| unsupported global initializer | 114 |
| unsupported lvalue | 83 |
| member expression | 79 |
| initializer action | 78 |
| class value transfer | 30 |
| temporary object | 28 |
| excess scalar local initializers | 21 |
| aggregate construction action | 15 |
| try statement | 13 |
| other individually categorized limits | 43 |

These are instrument-coverage limits, not LowIR failures.

## 3. Normative specification and toolchain

The normative LLVM source was the actual versioned [LLVM 21.1 Language
Reference Manual](https://releases.llvm.org/21.1.0/docs/LangRef.html), not
Clang output treated as a specification. The fetched release copy had SHA-256
`b4c36733988f37744a5458f2fd30c68b17a09de27b833384c208f741714510aa`.
The [LLVM 21.1 release
notes](https://releases.llvm.org/21.1.0/docs/ReleaseNotes.html) copy had
SHA-256
`6ee828a8591eb4f0fd308205c7be8e2ffc4229f01e140f4d5f9b7203307948c2`.

High-risk rules were checked directly in the LangRef, including
[well-formedness and SSA dominance](https://releases.llvm.org/21.1.0/docs/LangRef.html#well-formedness),
[`alloca`](https://releases.llvm.org/21.1.0/docs/LangRef.html#alloca-instruction)
uninitialized storage,
[`load`](https://releases.llvm.org/21.1.0/docs/LangRef.html#load-instruction)
and store alignment/volatile rules,
[`getelementptr`](https://releases.llvm.org/21.1.0/docs/LangRef.html#getelementptr-instruction),
[poison values](https://releases.llvm.org/21.1.0/docs/LangRef.html#poison-values),
[`freeze`](https://releases.llvm.org/21.1.0/docs/LangRef.html#freeze-instruction),
[`call`](https://releases.llvm.org/21.1.0/docs/LangRef.html#call-instruction)
tail constraints,
[`invoke`](https://releases.llvm.org/21.1.0/docs/LangRef.html#invoke-instruction),
[`landingpad`](https://releases.llvm.org/21.1.0/docs/LangRef.html#landingpad-instruction),
and LLVM 21
[`captures(none)`](https://releases.llvm.org/21.1.0/docs/LangRef.html#captures-attr).

The fixed comparison environment was:

| Fact | Value |
| --- | --- |
| Clang/LLVM | Ubuntu Clang 21.1.8 |
| Clang target | `x86_64-pc-linux-gnu` |
| cppgm++ hosted compiler | GCC 15 / `/usr/bin/x86_64-linux-gnu-g++-15` |
| C++ library | GCC 15 libstdc++ include roots and runtime family |
| Language | `gnu++11` |
| LLVM optimization/debug | `-O0 -g0`, frontend LLVM passes disabled |
| Data layout | `e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128` |
| Standalone `llvm-as`/`opt` | Not installed at a matching version; `clang -x ir -c` is the reader/verifier gate |

Clang and cppgm++ use the same libstdc++ roots, but their predefined macro sets
are not identical. The manifest records both macro and include-path digests;
hosted conditional branches are therefore never assumed equal merely because
the library directory is equal.

Final manifest SHA-256:
`a8a238087416967406fb8c36b0821d847d095ecc90f10495f4137b7f5a6edfde`.

## 4. Method and trust gates

For each ordinary source case, the static runner produced:

1. current `cppgm++ --emit-lowir -O0` output;
2. independent `cppgm++ --emit-llvm-ir -O0` output;
3. pristine Clang LLVM IR using the pinned profile;
4. a Clang LLVM-reader/object gate for each LLVM module; and
5. deterministic JSON commands, state, inventories, artifact sizes, and
   SHA-256 digests outside the repository.

`complete` means static three-lane and two-reader completion. Runtime claims
come only from the separate behavioral runner. Expected source rejections are
excluded from the successful denominator. An exporter failure remains in that
denominator as `exporter-limitation`; an emitted module rejected by the reader
is always `llvm-invalid`.

The exporter initially exposed two systematic adapter defects:

- LLVM rejected internal/weak linkage on declarations; and
- LLVM rejected `zeroext` attached to an incompatible already-promoted type.

Both were fixed in the adapter and the entire affected census was rerun from
clean scratch. The final corpus has zero `llvm-invalid` and zero
`clang-ir-invalid` cases. The inventory was also corrected to count only
reader-verified modules; partial files from explicit failures contribute no
opcodes or attributes.

Two repeated toolchain manifests were byte-identical. Two repeated artifact
runs over scalar, class, template, exception, and hosted probes had identical
normalized file/digest lists. The behavioral triangle compared exit status,
stdout, and stderr for 14 programs, including one separately compiled two-TU
program: all 14 matched across the native LowIR path, the experimental LLVM
path, and Clang.

## 5. Corpus coverage

### PA15–PA28 mandatory census

| Assignment | Total | Expected-success denominator | Complete | Exporter limitation | Clang noncomparable | Expected rejection |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| PA15 | 119 | 116 | 115 | 1 | 0 | 3 |
| PA16 | 301 | 258 | 60 | 198 | 0 | 43 |
| PA17 | 245 | 213 | 17 | 196 | 0 | 32 |
| PA18 | 37 | 27 | 2 | 25 | 0 | 10 |
| PA19 | 305 | 285 | 115 | 170 | 0 | 20 |
| PA20 | 174 | 161 | 102 | 59 | 0 | 13 |
| PA21 | 149 | 128 | 78 | 43 | 7 | 21 |
| PA22 | 311 | 296 | 125 | 171 | 0 | 15 |
| PA23 | 413 | 396 | 177 | 217 | 2 | 17 |
| PA24 | 422 | 415 | 199 | 216 | 0 | 7 |
| PA25 | 143 | 134 | 40 | 90 | 4 | 9 |
| PA26 | 114 | 108 | 4 | 104 | 0 | 6 |
| PA27 | 97 | 93 | 32 | 61 | 0 | 4 |
| PA28 | 45 | 44 | 5 | 39 | 0 | 1 |
| **Total** | **2,875** | **2,674** | **1,071** | **1,590** | **13** | **201** |

Thus 40.1% of successful cases reached static three-way completion, 59.5%
were retained as explicit exporter limits, and 0.5% were accepted by the
course compiler but not comparable under pinned Clang. The required PA19–PA24
template census covered all 1,681 expected successes: 796 complete, 876
limited, and 9 Clang-noncomparable.

The PA15 result—115 of 116 expected successes complete, plus 14/14 behavioral
witnesses overall—provides strong trust for procedural conclusions. Coverage
falls sharply when construction and class-value semantics begin, exactly where
the plan says to stop rather than create a second general-purpose backend.

### PA30–PA36 curated object/hosted profile

The later assignment harnesses use descriptors and `.t.1`, `.t.2`, … source
files, while PA29 and most PA37/PA38 `.t` files are LowIR rather than C++.
The checked-in explicit source list prevents those files from corrupting the
C++ denominator.

Twelve curated PA30–PA36 translation units were attempted: six complete and
six explicit limits. Complete cases cover a single-TU executable, both sides
of a separate-TU call, `main(int,char**)`, dynamic `alloca`, and a PA34 hosted
builtin. The PA31 EH case and PA35/PA36 STL object-bearing bodies remain
limited. Clang's same-libstdc++ modules for those inputs are still included in
the reverse LLVM census.

### Evidence digests

| Summary | SHA-256 |
| --- | --- |
| PA15–PA18 | `75aaee5dc11444fb3d6a9059f38bc6c0a914d504a6814a1a2f4d6cee55707a59` |
| PA19–PA24 | `635f44e6a3b21aa48747bcfcc8a93db0edc499eb336bb6cd257502f5fcfc60d5` |
| PA25–PA28 | `701a5eafa95f339ccc29b73fb30f038748cc76f114996e6f70779704de815253` |
| PA30–PA36 curated | `895a86c6f2040cd301e360665d46629cb7359ec676887ab4510de02852f72010` |
| Behavioral triangle | `abdb3e5b41cd42bba753c2133edc384dcb620411d2e678a1c75eb859b0e0cdc6` |

Raw artifacts remain reproducible scratch output, not checked-in assignment
fixtures or Clang-derived oracles.

## 6. Bidirectional fact conclusions

The complete field-by-field result is
[`doc/lowir-llvm-crosswalk.md`](doc/lowir-llvm-crosswalk.md). Its main results
are summarized here.

### LowIR facts with direct or derived LLVM representations

- Scalar types, values, globals, declarations/definitions, slots, CFG,
  arithmetic, conversions, comparisons, calls, and returns map directly or by
  small lowering sequences.
- `binding=internal/strong/weak` maps conservatively to LLVM linkage, while
  exact C++ ODR linkage needs semantic emission facts.
- function effects, non-unwinding, and non-return map to LLVM memory,
  `nounwind`, and `noreturn` attributes when proven;
- indirect result, by-address, reference, and direct aggregate ABI boundaries
  map to target-classified LLVM signatures and `sret`/`byval` where applicable;
- `capture=nocapture` maps to LLVM 21 `captures(none)`;
- `copyobj`/`zeroinit` map to scalar operations or memory intrinsics;
- LowIR EH regions lower to `invoke`/`landingpad`/selector/cleanup CFG rather
  than mapping one-for-one; and
- init/fini, TLS, EH, allocation, RTTI, and other roles lower to module tables,
  runtime symbols, personalities, and object policy.

### LowIR-only facts worth keeping

- `projection=array_element/field/base_subobject/reference_field`;
- `pass=reference/decay/by_address` as source-boundary origin;
- semantic runtime roles;
- `trivial_lifecycle`;
- `object_root`, `keep_alias`, `prefer_local`, and explicit object spellings;
- byte-object size/alignment independent of an LLVM aggregate spelling; and
- explicit copies and decay markers useful to LowIR optimization/native
  lowering even when LLVM SSA would erase them.

### LLVM facts that do not belong in LowIR by default

- target triple, data layout, PIC/PIE and unwind-table policy;
- `llvm.ident` and most module flags;
- O0 `optnone`/`noinline` policy;
- TBAA, loop/access-group, profile, sanitizer, and debug graph metadata;
- `noundef`, `nonnull`, `dereferenceable`, range, alignment, and alias promises
  not already proven by a durable LowIR fact; and
- optimizer-derived `mustprogress`, `willreturn`, `norecurse`, `nofree`,
  `nosync`, and `speculatable` properties.

## 7. Observed LLVM construct census

The verifier-filtered final evidence contains 1,090 experimental modules and
2,664 Clang modules. The inventory has no unknown instruction family.

Clang's most frequent attribute-like tokens were `align` 41,331,
`noundef` 11,540, `nonnull` 8,554, `uwtable` 7,448,
`dereferenceable` 7,103, `noinline` 4,780, `optnone` 4,739,
`mustprogress` 4,737, and `nounwind` 3,448. The `align` counter includes
instruction alignment syntax as well as attributes, so it is not a count of
parameter attributes alone.

ABI-relevant observed attributes included `zeroext` 500, `signext` 83,
`sret` 139, and `byval` 4. LowIR has enough source type/layout/pass facts to
derive these at a target boundary; duplicating all of them in the language IR
is unnecessary. `captures` 267, `noalias` 299, `readonly` 202, `writeonly`
264, and `memory` 304 have partial LowIR counterparts and require guarantee-
preserving mapping.

Observed intrinsic families were:

| Intrinsic family | Count | LowIR disposition |
| --- | ---: | --- |
| `llvm.memcpy` | 501 | `copyobj` or scalar/object copy lowering |
| `llvm.memset` | 134 | `zeroinit` or scalar stores |
| `llvm.eh.typeid.for` | 42 | exception selector/type matching |
| `llvm.global_ctors` | 26 | init role and initialization ordering |
| `llvm.threadlocal.address` | 15 | TLS and wrapper facts |
| `llvm.trap` | 12 | trap builtin/runtime policy, not generic source UB |
| `llvm.is.fpclass` | 4 | hosted FP builtin lowering |
| `llvm.memmove` | 4 | overlap-capable memory operation, not ambiguous `copyobj` |
| `llvm.used` | 3 | object roots/retention |
| `llvm.fabs.f80` | 2 | hosted FP builtin lowering |
| `llvm.umul.with.overflow` | 2 | hosted overflow builtin lowering |

Every Clang module had `llvm.module.flags` and `llvm.ident`. These are strong
frequency evidence but weak evidence for a LowIR change: they are module
producer facts.

## 8. Lowering-shape comparison

For the 1,077 cases where both frontends and both LLVM readers completed, the
instruction totals were:

| Instruction | Experimental LLVM | Clang LLVM |
| --- | ---: | ---: |
| `alloca` | 1,024 | 1,918 |
| `store` | 1,215 | 1,991 |
| `load` | 894 | 951 |
| `br` | 1,136 | 303 |
| `phi` | 342 | 47 |
| `select` | 0 | 187 |
| GEP | 252 | 108 |
| `sext` | 263 | 57 |
| `call` | 562 | 608 |
| `ret` | 1,621 | 1,569 |

The dominant differences are explained lowering choices:

- Clang's pristine O0 path materializes more parameters and locals in allocas,
  hence more stores. The experimental path keeps more values in SSA.
- The experimental path lowers conditionals and short-circuiting structurally
  with branches and PHIs. Clang uses `select` where it is safe and profitable
  even before optimization passes.
- LowIR's canonical integer value conventions and explicit source conversions
  lead to more extension operations.
- GEP frequency reflects different choices about explicit address computation
  and array/pointer lowering.

These raw shape differences are not correctness findings. The 14 behavioral
triangles match, and no verifier rule prefers `select` over CFG or stack
materialization over SSA. A future optimizer can target measured costs without
changing LowIR semantics.

The comparison cannot make equivalent shape claims for constructors, complete
class values, vtables, RTTI, virtual bases, or EH because those cases are
mostly exporter limits. Clang's observed `extractvalue`/`insertvalue`,
`invoke`, `landingpad`, and `resume` counts identify the implementation work a
full LLVM backend would require; they do not establish defects in LowIR.

## 9. Findings and recommendations

### LLVM-LOWIR-001 — volatile access is lost

| Field | Result |
| --- | --- |
| Classification | LowIR spec gap and semantic-to-LowIR lowering defect |
| Disposition | **accept-now** |
| Earliest owner | PA15 lowering; PA37 exposes the optimization consequence |
| Confidence | High |

Minimized witness:

```cpp
int main() {
  volatile int x = 0;
  x = 1;
  return 0;
}
```

Clang emits initialization plus two `store volatile i32` accesses. LowIR O0
emits two ordinary stores to `$x`; LowIR O2 removes the slot and both stores.
LLVM's [volatile memory
rules](https://releases.llvm.org/21.1.0/docs/LangRef.html#volatile-memory-accesses)
make volatility part of the operation, not the pointer type.

Recommendation: add a durable volatile marker to LowIR load/store and every
atomic access form for which volatile is semantically distinct. Carry it from
the semantic graph, preserve it through parsing/serialization and PA37, and
make native/MIR lowering treat it as an observable access. Add O0 round-trip,
O1–O3 preservation, and native behavior/code-shape tests. Bulk object
operations should remain disallowed across volatile subobjects unless their
contract is explicitly extended.

### LLVM-LOWIR-002 — section placement is not serialized

| Field | Result |
| --- | --- |
| Classification | Durable LowIR boundary/spec mismatch |
| Disposition | **accept-now** or explicitly narrow the durable-boundary claim |
| Earliest owner | PA32 object data / PA34 GNU section attributes |
| Confidence | High |

`SymbolMetadata.section_name` and `.section_segment` are retained by in-memory
object preparation and direct ELF emission, but `write_symbol_metadata` emits
neither and the text parser accepts neither. An emit/parse/object round trip
therefore cannot preserve a supported section attribute.

Recommendation: add a quoted target-section metadata form to public LowIR and
round-trip it, at least for the fixed ELF section name. Keep Mach-O-style
segment/name policy target-gated. If the intended architecture is that hosted
object facts are deliberately object-only, document that exception and stop
claiming serialized LowIR is the complete durable object boundary.

### LLVM-LOWIR-003 — weak and weak-ODR precision is collapsed

| Field | Result |
| --- | --- |
| Classification | Potential optimization/object precision gap |
| Disposition | **measure-more** |
| Earliest owner | PA19 templates and PA32 weak symbols |
| Confidence | Medium |

The semantic graph distinguishes `weak_odr` from source `weak_symbol`; public
LowIR has only `binding=weak`. A future LLVM backend can conservatively map
both to `weak`, which is correct but loses ODR optimization and exact COMDAT
intent. Current ELF emission can still produce weak/coalesced objects.

Recommendation: do not change the spec immediately. First measure whether
downstream LowIR-only optimization or object emission needs the equivalence and
discardability distinction. If so, add an ODR/coalescing field separate from
symbol binding; do not overload `binding=weak` with LLVM linkage spelling.

### LLVM-LOWIR-004 — clarify `copyobj` overlap semantics

| Field | Result |
| --- | --- |
| Classification | Specification clarity |
| Disposition | **accept-now** documentation clarification |
| Earliest owner | PA16 object copying / PA34 hosted memory builtins |
| Confidence | Medium-high |

LLVM distinguishes `memcpy` from `memmove`. LowIR says `copyobj` copies exact
bytes but does not state whether overlap is allowed. Production lowering uses
it for semantic object transfers, while overlap-capable `memmove` remains a
separate hosted/runtime operation.

Recommendation: specify `copyobj` as a non-overlapping semantic object copy.
If a later optimizer needs a first-class overlap-safe operation, add
`moveobj`; do not silently map ambiguous `copyobj` to the stronger/slower
operation or to LLVM `memcpy` without a contract.

### LLVM-LOWIR-005 — `va_end` is fixed-target implicit

| Field | Result |
| --- | --- |
| Classification | Portability boundary |
| Disposition | **follow-up** only before retargeting |
| Earliest owner | PA15 variadics |
| Confidence | High for the current target |

Source `__builtin_va_end` is deliberately lowered to no LowIR operation for
the pinned x86-64 SysV ABI. LLVM provides `llvm.va_end` because this is not
portable across all targets.

Recommendation: no current x86-64 change is required. Add a first-class
`va_end` before describing LowIR as a portable multi-target boundary or adding
a target whose `va_end` has effects.

### LLVM-LOWIR-006 — per-access alignment is conservative, not missing correctness

| Field | Result |
| --- | --- |
| Classification | Optional optimization fact |
| Disposition | **keep**, then measure |
| Earliest owner | PA15 memory; packed hosted objects later |
| Confidence | High |

LowIR does not promise an alignment on every ordinary load/store. LLVM
requires an explicit alignment and makes overstatement undefined behavior. A
backend can always emit alignment one and use stronger type/layout facts only
when proven.

Recommendation: document the conservative rule. Add per-access alignment only
if measured LLVM/native performance warrants it; never derive it from value
type alone for packed or adjusted pointers.

### LLVM-LOWIR-007 — module flags and most Clang attributes are not LowIR

| Field | Result |
| --- | --- |
| Classification | Backend-only, optimizer-derived, or bookkeeping |
| Disposition | **not-lowir** / **reject** broad copying |
| Earliest owner | PA30+ object/module producer |
| Confidence | High |

The target triple, data layout, PIE/PIC, unwind tables, frame-pointer policy,
compiler identity, and most module flags belong to the LLVM/object producer.
Optional promises such as `noundef`, `dereferenceable`, `nonnull`, and
`mustprogress` can improve LLVM optimization but can also introduce poison/UB
boundaries.

Recommendation: derive these in a future LLVM backend from target, semantic,
and analysis facts. Do not serialize them into LowIR merely because Clang emits
them frequently.

### LLVM-LOWIR-008 — LowIR semantic extras should remain

| Field | Result |
| --- | --- |
| Classification | Purposeful LowIR extra |
| Disposition | **keep** |
| Earliest owner | PA13 onward |
| Confidence | High |

Projection kinds, pass origin, runtime roles, lifecycle triviality, and
object-emission preferences have no single LLVM spelling because LLVM lowers
or erases them. They remain valuable to LowIR optimization, native lowering,
and object production.

Recommendation: retain them and test their actual consumers. Absence from
LLVM is not evidence of redundancy.

### LLVM-LOWIR-009 — adapter defects were not LowIR findings

| Field | Result |
| --- | --- |
| Classification | Investigation adapter defect |
| Disposition | **not-lowir**, fixed and rerun |
| Earliest owner | Experimental exporter |
| Confidence | High |

The verifier found illegal definition-only linkage on declarations and
misplaced `zeroext`. Both errors arose after the sibling semantic boundary and
do not imply a LowIR change. They demonstrate why LLVM-reader gates are a
required part of trusting the instrument.

## 10. Ranked follow-on work

1. **Correct volatile now.** Extend the public grammar/model, semantic lowering,
   optimizer effects, MIR/native selection, and tests as one coherent change.
2. **Resolve section round-trip now.** Either serialize the supported fact or
   explicitly document the object-only exception.
3. **Clarify `copyobj` overlap now.** This can be a spec/test clarification
   without changing current lowering.
4. **Measure weak-ODR precision.** Add a LowIR field only if a consumer needs
   the distinction.
5. **Keep alignment conservative.** Measure before adding per-access metadata.
6. **Add `va_end` only with a portability requirement.** The fixed target does
   not justify immediate churn.
7. **Do not bulk-import Clang metadata.** Add individual facts only with a
   proven producer, exact LangRef preconditions, and a measured consumer.
8. **If a production LLVM backend is desired, plan it separately.** The next
   implementation tranche is class ABI/coercion, construction/lifetime,
   COMDAT/aliases/TLS, then Itanium EH/RTTI. It should not be hidden inside a
   LowIR validation cleanup.

No current LowIR fixtures, specification, or lowering were changed as part of
gathering this baseline. The report recommends changes; each accepted change
should receive its own assignment-owned implementation and validation plan.

## 11. Existing repository validation

All required repository gates passed on the final implementation:

| Gate | Result |
| --- | --- |
| `make test-pa15` | 119 / 119 passed |
| `make test-report-through-pa15` | 1,183 / 1,183 passed |
| `make test-report-through-pa38` | 5,420 / 5,420 passed |
| `make inception` | Passed; matching self-hosted `llvm_ir_model.o`, `llvm_ir_export.o`, and final `cppgm++` |
| Python syntax checks for both runners | Passed |
| LLVM reader gate over final census | Zero `llvm-invalid`; zero `clang-ir-invalid` |
| Behavioral triangle | 14 / 14 matched |
| Repeatability | Two manifests and two five-profile artifact runs identical |

## 12. Reproduction index

- Plan: [`PLAN-LOWIR-LLVM-VALIDATION.md`](PLAN-LOWIR-LLVM-VALIDATION.md)
- Protocol and exact commands:
  [`doc/lowir-llvm-investigation-protocol.md`](doc/lowir-llvm-investigation-protocol.md)
- LLVM 21.1 coverage ledger:
  [`doc/llvm-ir-21.1-coverage.md`](doc/llvm-ir-21.1-coverage.md)
- Complete bidirectional crosswalk:
  [`doc/lowir-llvm-crosswalk.md`](doc/lowir-llvm-crosswalk.md)
- Manifest generator: [`scripts/lowir_llvm_manifest.py`](scripts/lowir_llvm_manifest.py)
- Static sweep runner:
  [`scripts/run_lowir_llvm_sweep.py`](scripts/run_lowir_llvm_sweep.py)
- Behavioral runner:
  [`scripts/run_lowir_llvm_behavior.py`](scripts/run_lowir_llvm_behavior.py)
- Curated source lists: `doc/lowir-llvm-curated-sources.txt`,
  `doc/lowir-llvm-behavior-sources.txt`, and
  `doc/lowir-llvm-repeatability-sources.txt`

The generated LLVM IR, objects, executables, and JSON case artifacts remain in
scratch and are intentionally not assignment references.
