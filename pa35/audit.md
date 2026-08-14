# PA35 Checkpoint Audit

## Current Checkpoint Review

The audit covered checkpoint commit `e3f0f029`, its parser, semantic, driver,
and plan changes, `spec.md`, the PA35 contract, focused assertion/constexpr
tests, and the primary failure log. The increment correctly treats a demanded
static assertion as an independent required-evaluation root while restoring
the caller's discarded or unevaluated suppression state. It advances the PA35
compile report from 48 to 53/103: five cases pass and 16 reach distinct later
owners, with no earlier-assignment regression.

The complete ownership path is source-token provenance -> one retained
static-assert syntax node and compact token range -> declaration or template
specialization demand -> assertion-local suppression scope -> typed expression
and contextual-bool analysis -> canonical selected function binding ->
TU-owned constexpr call fact -> assertion result. The call key contains the
canonical function specialization, receiver object/address identities, and
typed scalar/object/address arguments; its states distinguish in-progress,
success, and expected failure. Successful assertions produce no runtime LowIR;
the compile path merely reports the existing request/cache/step counters.

One checkpoint-level `spec.md` §2/§9 defect was found and repaired: source
filename/line/column text was eagerly assembled for every successful assertion.
The retained node still owns only compact token provenance, and presentation
text is now rendered solely on an assertion-owned error path. A failing probe
still reports its exact line and column; focused PA10/20 syntax and deferral
checks, three PA21 constexpr positive/negative checks, and the five new PA35
passes are clean.

For 16/32 independent `std::pair` trait families, calls are 160/320, cache hits
64/128, evaluator steps 305/609, and template requests 4,676/9,028. Semantic
time was 212/431 ms, semantic peak storage 30.8/58.5 MB, wall time 0.34/0.59 s,
and peak RSS 32,092/56,772 KiB. The stable counters show linear
assertion/evaluator work and reuse of completed call facts; successful
assertions perform no diagnostic rendering. The PA35 compile report preserves
53/103 passes (53/104 tracked); PA1-34 passes 4,756/4,756, and the file audit
passes with 22 inherited nonfatal header-division warnings. No relevant
checkpoint-owned correctness, performance, shortcut, timeout, ownership, or
file-audit issue remains.

## Checkpoint Audit Ledger

| Checkpoint commits | Audit disposition |
| --- | --- |
| `ab8d37e6` | Pass after consolidating retained-pack publication and lookup into one canonical direct/per-scope index; correctness baseline preserved. |
| `e3f0f029` + audit fix | Pass after making source-location rendering error-only; mandatory evaluation, canonical cache ownership, and the 53/103 baseline are preserved. |
