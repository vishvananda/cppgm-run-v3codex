# LowIR Section Placement Audit and Recommendation

Date: 2026-08-22

## Purpose

This report records the investigation into GNU section-placement attributes and
their loss at the serialized LowIR boundary. It complements:

- [REPORT-LOWIR-LLVM-VALIDATION.md](REPORT-LOWIR-LLVM-VALIDATION.md); and
- [REPORT-LOWIR-RECENT-ADDITIONS-AUDIT.md](REPORT-LOWIR-RECENT-ADDITIONS-AUDIT.md).

## Executive conclusion

The compiler already carries explicit section placement through its semantic,
typed lowering, in-memory LowIR, MIR, binary object, and native ELF layers. The
public LowIR text format is the missing link.

No existing textual metadata key has the correct meaning. The recommended fix
is to extend top-level LowIR symbol metadata with a dedicated `section=` key and,
if non-ELF object formats are intentionally supported, an optional
`section_segment=` key.

This is a small public-format extension, not a new semantic-model or backend
feature.

## Language status

The source construct under discussion is typically written as:

```cpp
int counter __attribute__((section("cppgmsec"))) = 42;
```

It is a GCC/Clang extension, not a standard C++11 language construct. Clang
also supports the vendor-namespaced spelling `[[gnu::section("cppgmsec")]]`,
but use of C++11 attribute syntax does not make the attribute itself part of
the C++11 standard.

The repository explicitly recognizes the GNU `section` and `__section__`
attribute names in
[dev/src/pa32_object_data_semantic.cpp](dev/src/pa32_object_data_semantic.cpp).

This support matters because the compiler has chosen to accept the extension.
Once accepted, its observable object-placement effect must either survive the
compiler pipeline or be documented as unsupported.

Reference: [Clang Attribute Reference, `section`](https://clang.llvm.org/docs/AttributeReference.html#section-declspec-allocate).

## Assignment ownership and history

PA32 is the host object/toolchain interoperability assignment. Its README says
that PA32 does not add language features; it connects existing semantic and ABI
facts to real host-compatible object files. It also states that its tests are
object-boundary checks rather than direct C++11 clause tests.

The section-placement test and its expected object inspection were added by
bulk export commit `74d80976` on 2026-08-05. Semantic implementation followed
in commit `0d3e1179` on 2026-08-13.

Therefore:

- testing the emitted ELF section in PA32 is appropriate;
- introducing semantic support for a GNU extension in PA32 is mild contract
  drift from the statement that PA32 adds no language features; and
- testing durability through LowIR belongs in PA37's object-roundtrip lane.

## Existing direct coverage

PA32 contains a dedicated two-object test:

- [pa32/tests/general/200-host-gnu-section-attribute.t.1](pa32/tests/general/200-host-gnu-section-attribute.t.1);
- [pa32/tests/general/200-host-gnu-section-attribute.t.2](pa32/tests/general/200-host-gnu-section-attribute.t.2); and
- [pa32/tests/general/200-host-gnu-section-attribute.inspect.plan](pa32/tests/general/200-host-gnu-section-attribute.inspect.plan).

The inspection plan requires:

```text
custom_section 1 cppgmsec
custom_section 2 cppgmsec
weak_symbol 2 retained_alias
```

This validates direct source-to-object section placement and weak-symbol
behavior.

It does not serialize the program to LowIR text before object emission.

## Missing roundtrip coverage

PA37's object-roundtrip harness compares object bytes produced by:

```text
source -> cppgm++ -c -> direct object
```

against:

```text
source -> --emit-lowir -O0 -> LowIR text -> cppgm++ -c -> replayed object
```

The contract is documented in [pa37/README.md](pa37/README.md), and the harness
is [pa37/scripts/run_object_lowir_roundtrip_tests.pl](pa37/scripts/run_object_lowir_roundtrip_tests.pl).

The harness is strong: it byte-compares direct and replayed objects at `-O0`,
`-O1`, `-O2`, and `-O3`. The coverage gap is its selected corpus. None of the
13 current object-roundtrip sources uses a section-placement attribute.

The existing PA32 fixture was replayed manually through the exact boundary. The
result was:

- the direct object contained `cppgmsec` and `.relacppgmsec`;
- serialized LowIR contained no section information;
- the replayed object placed the definition in `.data` with `.rela.data`; and
- the objects differed byte-for-byte.

Adding the existing PA32 fixture to the PA37 object-roundtrip corpus exposes the
failure immediately.

## Current information path

The direct in-memory compilation path is:

```text
GNU section attribute
  -> BindingRecord.object_section_name
  -> typed LowIR Symbol.section_name
  -> lowir_model::SymbolMetadata.section_name
  -> MIR global section_name
  -> custom native ELF section
```

### Semantic capture

`SemanticAnalyzer` decodes the attribute's narrow string-literal sequence and
stores it in `BindingRecord.object_section_name`:

- [dev/src/pa32_object_data_semantic.cpp](dev/src/pa32_object_data_semantic.cpp)

### Typed lowering

PA15 copies the canonical binding's section name into the typed symbol:

- [dev/src/pa15_lowering.cpp](dev/src/pa15_lowering.cpp)

### In-memory LowIR

The PA30 adapter copies the typed fact into
`lowir_model::SymbolMetadata::section_name`:

- [dev/src/pa30_lowir_adapter.cpp](dev/src/pa30_lowir_adapter.cpp)
- [dev/src/lowir_model.h](dev/src/lowir_model.h)

The in-memory metadata contains both:

```cpp
StringId section_segment;
StringId section_name;
```

### Binary object serialization

The repository's binary object representation already writes and reads both
fields:

- [dev/src/pa30_object.cpp](dev/src/pa30_object.cpp)

### Native lowering

LowIR-to-MIR conversion copies the fields into the MIR global:

- [dev/src/lowir_native_program.cpp](dev/src/lowir_native_program.cpp)

The ELF backend selects a custom data section when `section_name` is valid:

- [dev/src/lowir_native_elf.cpp](dev/src/lowir_native_elf.cpp)

This explains why direct compilation works: it hands the richer in-memory
LowIR model directly to the backend without requiring a text serialization and
parse.

## Exact serialization defect

The public text serializer's `write_symbol_metadata` emits:

- `role`;
- `linkage`;
- `binding`;
- `object`;
- `tls_for`;
- `keep_alias`;
- `prefer_local`;
- `object_root`;
- `trivial_lifecycle`;
- `force_inline`;
- `inline_hint`; and
- `no_inline`.

It does not emit `section_segment` or `section_name`:

- [dev/src/lowir_serialize.cpp](dev/src/lowir_serialize.cpp)

The text parser accepts the same closed set of symbol keys and rejects unknown
keys. It has no section-placement case:

- [dev/src/lowir_parse.cpp](dev/src/lowir_parse.cpp)

The durable LowIR specification likewise lists no section key:

- [pa13/lowir.md](pa13/lowir.md)

The precise finding is therefore not that section placement travels outside
the in-memory LowIR model. It travels outside the **public serialized LowIR
contract**.

## Why existing metadata is insufficient

The nearest existing keys have distinct meanings:

- `object=` controls the concrete object-file symbol spelling;
- `storage=readonly` is a semantic immutability guarantee;
- `storage=thread_local` is TLS storage intent;
- `binding=` controls symbol ownership/linkage strength; and
- `role=` identifies compiler/runtime purposes.

Section placement is independent of all of them. For example, a strong,
mutable, non-TLS object can still be explicitly placed in a custom section.

Overloading `object=` would mix symbol identity with storage placement.
Overloading `storage=` would mix semantic memory properties with an explicit
object-layout constraint. Neither is correct.

## LLVM IR comparison

LLVM represents section placement as a first-class property of a global or
function, not as arbitrary attached `!metadata`.

For a global:

```llvm
@section_alias = global ptr @section_alias, section "cppgmsec"
```

The LLVM Language Reference permits explicit sections on both global
declarations and definitions. If declaration and definition section
information disagree, behavior is undefined. An explicit section is retained
for targets that use it.

LLVM also allows explicit sections on function definitions. The current
repository implementation only traces variable section placement through the
PA32 object-data path, so function-section support should not be claimed merely
because the shared in-memory metadata type could hold the field.

References:

- [LLVM Language Reference, global variables](https://llvm.org/docs/LangRef.html#global-variables)
- [LLVM Language Reference, functions](https://llvm.org/docs/LangRef.html#functions)

## Recommended LowIR extension

For the current Linux x86_64 target, add a dedicated top-level symbol metadata
key:

```text
global @section_alias : ptr
    [linkage=c, binding=strong, section=cppgmsec] = addr @section_alias
```

The minimum implementation should:

1. document `section` as a global declaration/definition metadata key;
2. serialize `SymbolMetadata::section_name` as `section=...`;
3. parse it back into the existing field;
4. validate declaration/definition consistency;
5. preserve it through `lowiropt` at every level;
6. map it to LLVM's `section "name"` property; and
7. add direct and object-roundtrip tests using the existing PA32 fixture.

### Optional segment support

The model also contains `section_segment`, apparently anticipating object
formats such as Mach-O that distinguish segment and section names. If that is
an intended public capability, use two independent keys:

```text
[section_segment=__DATA, section=cppgmsec]
```

For the repository's stated Linux x86_64 scope, only `section=` is required.
`section_segment=` should not be added to the public contract without a target,
consumer, and test that require it.

## Metadata string-encoding edge case

LowIR metadata values are currently unquoted lexer tokens. The serializer emits
`key=value` directly, and the parser reads exactly one token as the value.

Consequences:

- a period in an ELF section name is representable;
- commas, spaces, `=`, `#`, brackets, and other punctuation handled by the
  lexer may not roundtrip;
- a single Mach-O spelling such as `__DATA,cppgmsec` cannot be represented as
  one current metadata value because comma is a metadata separator; and
- arbitrary LLVM section strings cannot be represented faithfully.

Two implementation strategies are possible:

1. Minimal Linux implementation: accept the currently token-safe ELF subset
   and use separate `section_segment=` and `section=` fields if segment support
   is later required.
2. Robust format implementation: add quoted and escaped metadata string values,
   then use them for section names and other future string-valued metadata.

The robust approach is preferable for a durable IR specification, but it is a
larger grammar change and should have dedicated lexer/parser compatibility
tests. The minimum section fix should not silently claim arbitrary-name support
if only token-safe names are accepted.

## Validation requirements

### Parser and serializer

- Parse and serialize `section=cppgmsec` on a global definition.
- Parse and serialize it on a global declaration if declarations are supported.
- Reject duplicate `section` keys.
- Reject the key on unsupported top-level kinds until function-section support
  exists.
- Verify `lowiropt -O0` produces the same metadata.
- Verify `-O1`, `-O2`, and `-O3` retain the placement on surviving globals.

### Object roundtrip

Reuse the PA32 section fixture in PA37's object-roundtrip lane and compare at
all four optimization levels:

```text
direct object:     cppgmsec + .relacppgmsec
roundtripped object: cppgmsec + .relacppgmsec
```

The object byte comparison should pass. Also retain explicit `readelf -SW` and
symbol-table assertions so failures explain whether the discrepancy is section
identity, relocation placement, binding, or unrelated byte layout.

### LLVM export

For a sectioned global, require emitted LLVM IR containing:

```llvm
section "cppgmsec"
```

Then run the configured LLVM verifier/assembler and compare Clang's lowering of
the same GNU attribute.

### Full repository gates

After implementation:

```sh
make test-pa13
make test-report-through-pa13
make test-pa32
make test-report-through-pa32
make test-pa37
make test-report-through-pa37
make test-report-through-pa38
make inception
```

## Recommended disposition

Add `section=` to serialized LowIR. Reuse the existing in-memory field and
backend implementation. Do not repurpose another metadata key, and do not add
`section_segment=` unless its target-specific behavior is deliberately brought
into scope.

Separately, add the existing PA32 fixture to PA37 object-roundtrip coverage so
future in-memory/text-model mismatches are caught at the boundary where they
occur.

