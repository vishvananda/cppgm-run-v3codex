# PA21 Checkpoint Audit

## Current Checkpoint Review

The landed `9db9e273` increment raised the combined PA21 report from 109/135 to
118/135 by completing qualified static constants, incomplete-array
redeclarations, typed constant storage, and ODR rematerialization. The audit
found that scalar initializers used a different constant-expression path from
array/class initializers and could eagerly demand constexpr helpers; address and
constructor paths had the same premature-demand leak. The canonical initializer
graph also used parallel owner arrays, sorting for deduplication, and no direct
work counters. Finally, an out-of-class definition could incorrectly add a
second initializer. These violated N3485 9.4.2's static-member definition rule
and `spec.md` §§2,4–6,8–10 requirements for one typed owner, demand-separated
completion, bounded indexed work, and observable performance.

The repaired path analyzes every in-class static constant initializer in the
same constant-required recipe context. Canonical member identity owns one
immutable initializer fact containing its typed recipe and O(1)-deduplicated
canonical callable edges; already-folded scalar/address leaves prune their
evaluation-only call trees. Lookup and constant consumers reuse the fact without
emission, while namespace definition or ODR storage demand explicitly consumes
the retained edges before typed rematerialization. Function-definition
provenance preserves the established PA20 emission of an explicitly
out-of-class static constexpr member without reintroducing demand for in-class
or free compile-time-only helpers. The namespace definition validator now
rejects a second initializer. Lowering consumes binding/type/value facts only;
no path reconstructs semantics from names, invokes an external tool, or
recognizes a test.

For ODR-used 16/32/64/128-element constexpr class arrays, semantic nodes were
92/156/284/540 and initializer visits were exactly 33/65/129/257, with one
dependency edge at every width. Demand pushes (2), demanded function emissions
(1), globals (1), and LowIR instructions (20) stayed fixed; typed storage grew
8,364/9,260/11,052/14,636 bytes. For 1/2/4/8 compile-time-only uses of one scalar
member, semantic nodes were 11/14/20/32 while initializer visits (1), constexpr
call requests (1), typed storage (1,735 bytes), and LowIR instructions (1)
stayed fixed, with zero dependency edges, demand pushes, or demanded functions.
The owned walk is therefore linear in initializer structure and repeated
constant consumers do not replay or emit its helper.

Validation passes both audit regressions for a combined 120/137 while retaining
the exact 17 pre-existing PA21 failures and exceeding the 118/135 turn-start
baseline. The landed nine owned and eight nearby probes remain passing,
PA1–PA20 passes 2,185/2,185, and the PA21 file audit passes with the same 12
header-division advisories.

## Checkpoint Audit Ledger

| Checkpoint | Audit disposition | Evidence |
| --- | --- | --- |
| `dd3dd301` integral scalar invocation and demand | Pass after ownership repair | stack/scratch ownership, namespace-mutation rejection, fixed canonical graph, linear counters, PA1–20 clean, PA21 baseline preserved |
| `d4d44664` class-valued calls and conversions | Pass after completion-boundary repair | complete typed call keys/object results, transitive local-address escape rejection, constant repeated-call work, PA1–20 and checkpoint baselines preserved |
| `44134d03` base-subobject completion | Pass after active-subobject ownership repair | complete/active IDs and adjusted addresses through projection, calls, references, and caches; linear depth counters; PA1–20 and checkpoint baselines preserved |
| `149f92db` callable and contextual conversions | Pass after parser/call ownership repair | exact rollback journal, shared recursive-arrow owner, semantic class initialization, retained surrogate conversions, cached address results; PA1–20 and checkpoint baselines preserved |
| `49e62fbb` canonical exception and `noexcept` facts | Pass after conversion/lifetime ownership repair | shared contextual-bool fact, compile-time-only demand, temporary-destructor coverage, linear action counter; PA1–20 and 108/134 baseline preserved |
| `9db9e273` qualified static constant storage | Pass after definition/demand ownership repair | canonical recipe/dependency fact, no reinitializer, compile-time-only scalar/address/object demand suppression, O(1) deduplication and counters; PA1–20 and 118/135 baseline preserved |
