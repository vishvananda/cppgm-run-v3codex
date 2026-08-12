# PA26 Audit

## Current Checkpoint Review

The lexical unwind snapshot checkpoint (`e05062b1` plus this audit follow-up)
passes its bounded architecture review. The review covered only the landed
increment: typed live-action snapshots, may-throw call boundaries, dispatch
reuse, nested handler continuation, and catch-miss cleanup. Class exception
construction and the remaining branch-temporary cases stay outside this
checkpoint.

The audit closed four checkpoint-owned defects. Snapshot publication had been
wired only through expression statements, leaving no-temporary conditions,
loop expressions, scalar returns, and automatic scalar initializers without a
lexical cleanup frontier. Those owners now stage the same typed action suffix;
initializer staging occurs before destination lifetime registration so a
failed initializer cannot destroy an unconstructed destination. Existing
temporary-cleanup owners remain authoritative, and nested-template detection
now requests staging without appending a duplicate action sequence.

An exception escaping a handler also closed the catch before destroying
handler-local objects. PA12 now segments the action suffix with explicit typed
handler-exit markers; PA15 lowers each marker after local destruction and
before outer destruction. Marker bits participate in dispatch-cache identity,
so distinct ordered suffixes cannot alias. Catch-all try regions no longer
publish impossible catch-miss suffixes, eliminating quadratic action growth in
nested catch-all input. Both exception scope vectors are included in semantic
side-storage telemetry.

The durable ownership path is source expression -> canonical PA12 expression
and scope-lifetime facts -> statement-owned ordered destructor-action suffix ->
per-function PA15 cleanup manager and compact dispatch cache -> typed LowIR EH
regions. Lowering consumes `TypeId`, `BindingId`, object identity, action flags,
and handler context directly; it performs no lookup recovery, string parsing,
test-name branching, external compilation, or reference-binary fallback.

Representative release runs with one live guard and 32/64/128/256 repeated
calls recorded 32/64/128/256 unwind actions, 32/64/128/256 cache probes,
31/63/127/255 hits, and exactly one dispatch entry. Instructions grew
104/200/392/776 and typed storage 20,069/35,429/66,149/127,589 bytes. A second
run over 8/16/32/64 nested catch-all handlers recorded exactly 8/16/32/64
unwind scope and action visits; instructions grew 254/502/998/1,990 and typed
storage 60,815/114,685/222,431/437,919 bytes. The counters support linear work
in both action sequences and produced LowIR.

Validation preserves both baselines: PA1-PA25 pass 3,607/3,607 and PA26 remains
80/110. Nine focused checked fixtures pass, synthesized condition, return,
declaration, and handler-order traces contain the required cleanup ordering,
and the PA26 file audit passes with 19 inherited division warnings. The two
shared-dispatch callee failures reproduce unchanged at the parent checkpoint
and remain outside this increment.

## Checkpoint Audit Ledger

| Checkpoint | Audit result |
|---|---|
| Canonical RTTI demand and query lowering (`9eb277da`, audit follow-up) | Pass: evaluated demand, reachable collection, complete canonical RTTI categories, cast legality, linear counters, baseline and earlier stages preserved. |
| Lexical unwind snapshots and handler continuation (`e05062b1`, audit follow-up) | Pass: complete bounded owners, ordered handler exit, non-duplicated typed suffixes, linear counters, and both baselines preserved. |
