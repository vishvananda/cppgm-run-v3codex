# PA20 Full-Stage Plan

## Stage Design and Spec Alignment

PA20 extends the retained PA19 template graph at semantic analysis; syntax is
still parsed once by PA10 and successful programs still lower directly from
typed PA12 facts to typed LowIR.  `spec.md` requires canonical constants and
specialization arguments, O(1)-average identity/cache lookup, immutable
parent-linked template environments, separate demand states, and lowering that
does not reconstruct lookup or constants from text.  PA20 therefore needs one
typed integral-constant path shared by `static_assert` and non-type arguments,
parameter records that distinguish type/value/pack slots, canonical argument
keys, and specialization selection before demand-driven replay.  No PA21
constexpr-function evaluation or PA22/PA23 SFINAE behavior is pulled forward.

## Current Failure Map

Turn-start baseline was 15/164; the completed assertion checkpoint raises the
stage to 39/164.  The dominant remaining group reaches the PA19 type-only
template-parameter guard (integral non-type ownership and argument binding).
The pack group reaches invalid placeholder identity, missing `sizeof...`
syntax, or unsupported expansion replay.  Eight successful compilations have
specialization/pack LowIR mismatches; the remainder are dependent lookup,
literal, declaration-ambiguity, early member-body validation, and stale
specialization composition cases.  These groups are owned primarily by PA12
semantic facts plus PA19 template replay; LowIR needs no new PA20 form.

## Active Checkpoint

Implement canonical fixed-arity integral non-type template parameters and
arguments for class and explicitly-argumented function templates.  Pattern
slots must distinguish type and integral-value parameters; arguments carry a
canonical type plus normalized value, and specialization keys compare those
facts without rendered strings.  Instantiation binds value slots as constant
parameter bindings in an immutable child scope, then reuses PA19 demand-driven
class/function replay.  Packs and partial specialization remain separate.

Owner/data flow: PA10 parameter/argument syntax -> PA12 typed constant facts ->
PA19 pattern and canonical specialization key -> substitution scope -> existing
typed lowering.  Per request work is O(parameters + supplied/default arguments)
with O(1)-average cache lookup; repeated complete keys must be cache hits and
must not repeat layout/member work.  Validate fixed class/function cases,
defaults, expression/enum arguments and template assertions, then full PA20,
PA1-PA19, file audit, and 1x/2x/4x distinct/repeated specialization probes.

## Performance Evidence

Seven-run release medians for 64/128/256 macro-expanded assertions: tokens
1,098/2,186/4,362; semantic nodes 453/901/1,797; edges 388/772/1,540;
conversion checks 769/1,537/3,073; peak semantic-stage bytes
147,772/279,396/542,605; semantic time 0.704/1.303/2.526 ms.  Lowered nodes stay
at three and typed LowIR storage at 1,735 bytes.  Work and storage track the
assertion expression count without retry or output growth.

## Completed Checkpoints

| Checkpoint | Result |
|---|---|
| Typed integral constants and `static_assert` | Width/signedness-aware casts, folds, literals, declaration/class/block validation, and `wchar_t`/`char16_t`/`char32_t` lowering; PA20 15 -> 39, PA1-PA19 2,013/2,013, audit pass |
