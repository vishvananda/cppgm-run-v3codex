# PA25 Checkpoint Audit

## Current Checkpoint Review

Checkpoint `cec97359` (selected class conversions and typed value boundaries)
passes after audit repair. The landed increment adds one-class conditional
construction, conversion-to-lvalue-reference increment, conversion-function
using lookup through aliases, and direct transfer for the supported small
derived parameter boundary.

The audit found and repaired three defects on that ownership path. PA10 first
scanned conversion-type syntax as a name and PA12/PA19 later reconstructed the
using target with `rfind` and string slices. Conversion targets are now retained
as one parsed `conversion-type-id`; PA19 builds one canonical `TypeId`, visits
only the named base conversion set, and matches by identity. The derived direct
parameter exception was also recomputed in PA17 from size, inheritance, and
lifecycle properties. PA12 special-member completion now publishes a distinct
`indirect_class_parameter_abi` fact beside the class transfer facts, while PA17
consumes it directly for both function headers and calls. Finally, built-in
increment candidate formation admitted `const T&` conversion results, so a
nonmodifiable alternative could suppress a valid `T&`; those results are now
filtered before target selection.

The demanded-template trace is source bytes -> one PA10 conversion target ->
canonical `AtomicBase<char>` and `char` identities -> indexed conversion members
of that base chain -> selected conversion binding and object projection -> one
demanded member specialization -> direct typed call lowering. The derived-value
trace is completed class layout/special members -> one published parameter ABI
fact -> constructor-selected staging object -> matching caller/callee boundary
metadata -> typed `obj<4x4>` LowIR. Conditional construction similarly retains
the selected constructor on its arm; lowering performs no lookup, type-spelling
recovery, syntax replay, or LowIR text round trip.

For 16/64/256 conversion-function candidates resolved by one canonical-target
using-declaration, tokens were 211/787/3,091, declarations 69/213/789, lookup
queries 73/217/793, signature probes 82/226/802, access checks 25/73/265, and
peak semantic storage 102,152/333,672/1,319,100 bytes. Five-run median semantic
time was 0.437/1.118/3.783 ms and lowering time 0.033/0.034/0.052 ms, with no
emitted functions. Work and storage follow the actual declaration/candidate set
without a whole-program or quadratic trend.

No relevant source/test shortcut, global retry, lowering-time semantic search,
textual transport, timeout-adjacent behavior, or unresolved checkpoint-owned
correctness, performance, or file-audit issue remains. The landed 83/128 PA25
baseline is intact and both audit regressions pass, producing 85/130 with the
same 45 pre-existing failures. Focused checkpoint and neighboring cases pass
10/10, PA10 passes 157/157, PA1-24 pass 3,471/3,471, and file audit passes with
15 nonfatal inherited division advisories. Both reducers produce accepted
LowIR; the modifiable-reference reducer also executes through the secondary
CY86 path with status zero.

## Checkpoint Audit Ledger

| Checkpoint | Audit disposition |
|---|---|
| Ordinary placeholder results (`583b174a`) | Pass after cv-reference, runtime-demand, direct-ownership, and retained-body copy repairs; shipped baseline and all earlier stages preserved; linear scaling and file audit verified. |
| Range-for statements (`b985f854`) | Pass after single-parse dispatch, category-correct one-time range binding, and condition/iteration cleanup repairs; 67/128 PA25 and 3,471/3,471 earlier tests preserved; linear scaling and file audit verified. |
| Selected class conversions (`cec97359`) | Pass after single-parse canonical conversion targets, semantic-owned parameter ABI, and modifiable-reference filtering; 85/130 PA25 and 3,471/3,471 earlier tests preserved; linear scaling and file audit verified. |
