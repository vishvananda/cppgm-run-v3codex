# PA19 Full-Stage Plan

## Stage Design and Spec Alignment

PA19 is a staged source-to-LowIR compiler. The assignment-mandated PA10 syntax
boundary owns one compact token vector and one syntax arena for the current
translation unit. PA12 builds one canonical semantic program during the arena
callback; PA15-PA18 consume that graph directly into one typed LowIR program,
after which the syntax and semantic owners are released. No syntax or LowIR
text is used as an in-process transport, and no later owner retains a pointer
into an earlier phase. Native machine IR and direct ELF emission belong to
later assignments and are therefore outside this audit boundary.

Identifiers, scopes, declarations, types, specialization arguments, layouts,
selected conversions, lifetime actions, ABI entries, and emission units use
interned IDs or compact typed records. PA19 retains each template body once,
uses parent-linked parameter scopes and canonical `(pattern, TypeId...)`
specialization keys, completes only demanded definitions/layouts, and lowers
the selected semantic facts without lookup replay. Supported template-ids are
retained as semantic-only PA10 `structured-type-name` nodes whose components
carry interned IDs and whose arguments remain ordinary `type-id` trees. The
public PA10 rendering contract is unchanged.

Representative end-to-end trace:

```text
source nested template-id/new-expression
  -> retained structured name and type-id arguments
  -> canonical specialization shell and completed inherited base
  -> indexed lookup selects the allocation binding and queues demand
  -> semantic new-expression records symbol/conversion/lifetime facts
  -> typed LowIR emits the direct allocation call and construction
  -> final LowIR renderer writes the PA19 presentation view
```

This adapts `spec.md` to PA19's explicit PA10 boundary and LowIR-only endpoint
without weakening its canonical-identity, demand, typed-lowering,
self-containment, or scaling requirements.

## Performance Evidence

Seven-run medians were collected from the release compiler with
`CPPGM_FRONTEND_STATS=1` after the audit changes.

| Probe | 1x / 2x / 4x evidence |
|---|---|
| Distinct class specializations (64/128/256) | Tokens 1,051/2,075/4,123; requests 64/128/256 with no hits; semantic nodes 136/264/520; layouts and member visits 128/256/512; peak bytes 1,184,574/2,366,134/4,729,398; median semantic time 4.447/8.731/18.085 ms and lowering 0.353/0.581/1.162 ms. |
| Repeated specialization (128/256/512) | Requests 128/256/512 with 127/255/511 hits; layouts and member visits stay 2; semantic nodes 264/520/1,032; peak bytes 201,151/375,743/724,927; median semantic time 1.113/1.944/3.767 ms and lowering 0.538/1.019/2.058 ms. |
| Omitted defaults, 128 uses | Function and class probes each report 128 requests and 127 hits; the class probe performs one layout. A dependent function default (`U = T`) also lowers successfully through canonical `TypeId` replay. |

Required work, storage, and time track input/output growth. Repeated uses hit
the canonical cache and do not repeat layout/member work. Counters explain the
largest path, so no unexplained slow path remained for sampling-profiler
escalation.

## Architecture Review

| Checklist area | Independent result |
|---|---|
| Representation and ownership | Pass. One PA10 arena is borrowed only for semantic construction; the semantic graph is borrowed only for direct typed lowering; textual forms are terminal views. |
| Identity and lookup | Pass after repair. Supported template arguments and qualified components use retained nodes and interned IDs; scope/function/template indexes restrict visits to legal owners and candidates. |
| Templates and repeated work | Pass after repair. Stable pattern owners, canonical complete keys, parent-linked scopes, monotonic completion states, and a deduplicated demand worklist avoid global retries and repeated specialization work. |
| Lowering | Pass. Selected declarations, conversions, layouts, lifetimes, ABI facts, and emission demand cross the boundary as typed facts. PA19 performs no name reconstruction or LowIR reparse. |
| Allocation and scaling | Pass after repair. Re-entrant template publication cannot move pattern owners, growing specialization sequences are not copied per completion, and measured work is proportional to demanded facts and emitted IR. |
| Self-containment | Pass. Changed production sources contain no host/reference compiler invocation, test/file recognizer, cached answer, or hosted-library special case. |

## Final Architecture Review

The audit found four blockers and closed all four: textual reconstruction of
supported template-ids, unstable/quadratic pattern ownership during re-entrant
completion, discarded function-template defaults plus uncached omitted-default
requests, and source/function ownership above file-audit limits. The structured
path review also corrected qualified operator terminal identity. Full-stage
validation then closed one compatibility group in the shared boundary:
non-type PA10 argument forms remain presentation-only, PA11 skips hidden name
facts where it expects a language child, and PA12 preserves access and
declaration/call ambiguity checks. Expected
overload rejection remains rank/result based; exceptions are reserved for the
selected final error path. No whole-program retry, global cache invalidation,
text round trip, lowering fallback, or unexplained nonlinear counter remains.

Final exit state: PA19 300/300, PA1-PA19 2,013/2,013, all 19 tracked stages
passing, and the required file audit passing with 11 advisory pre-existing
header-ownership warnings.

## Checkpoint Ledger

| Stage commits reviewed | Scope | Final disposition |
|---|---|---|
| `0797d80f`, `dc67fb1e`, `020b715c` | Function demand, class specialization, retained members | Pass after stable-owner/default-cache audit repairs |
| `b346cab8` through `6da6811e` | ADL, target context, qualified/namespace replay, ambiguity, dependent types, demand, validation, calls and value boundaries | Pass after canonical structured-name propagation |
| `5ca3aed9` through `d1c73c33` | Explicit instantiation, enum competition, declaration-owned replay and checkpoint audits | Pass; prior corrections preserved |
| `497c5381`, `266ca0e1`, `28035796`, `74a0fccd` | Local identity, callable replay, scalar/control conversions | Pass; canonical identity and local conversion keys preserved |
| `729f6952`, `b1989cc0`, `51d82f40`, `63423959` | Emission demand, default/base replay, extension retention, allocation lowering | Pass after end-to-end allocation and omitted-default audit |
| Final full-stage audit | All changed production paths and stage history | Pass; no open finding |
