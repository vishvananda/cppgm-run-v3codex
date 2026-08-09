# PA19 Final Audit

## Findings

1. Supported template-ids were still reparsed from rendered source spellings in
   class/function/type/value, using, retained-call, and special-member paths.
   This made presentation text a semantic input and left qualified operator
   terminals vulnerable to incorrect identity.
2. Class specialization completion copied the complete, growing template
   pattern. Function and class pattern vectors could also move while nested
   semantic replay published another pattern. The combination created avoidable
   quadratic work and unsafe re-entrant references.
3. Function-template defaults were parsed but discarded. Omitted class and
   function defaults also lacked a request-key alias to the completed canonical
   key, so repeated omitted-argument uses could replay default substitution.
4. Template instantiation and syntax orchestration had outgrown their owning
   source files and two call-resolution functions exceeded the architecture
   audit limits.
5. The first structured-name integration exposed older-stage boundary
   assumptions: PA10 non-type argument lists were being reparsed as PA19 type
   trees, PA11 treated hidden name facts as expression children, and PA12 could
   bypass access or ambiguity guards on the new path.

All findings are closed. No residual correctness, architecture, performance,
self-containment, or file-audit blocker remains.

## Changes

- PA10 now retains hidden structured-name facts below declarations,
  declarators, id/member expressions, using targets, constructor initializers,
  class/base names, and dependent type names. Components carry shared interned
  IDs and template arguments retain their parsed `type-id` trees. Operator and
  destructor terminals preserve canonical identity; qualified operator
  extraction uses the final `::operator` component.
- PA12/PA19 resolve those facts directly and build every supported template
  argument through `BuildTypeId`. Textual template/type parsers, substring
  dependence scans, and the class-specialization spelling fallback were
  removed. Retained validation now walks syntax IDs.
- Function and class template patterns use stable `deque` ownership; retained
  member definitions are stable as well. Completion borrows the published
  pattern and snapshots only the mutable specialization sequence needed by a
  forward-definition upgrade.
- Function defaults are retained and merged across compatible declarations.
  Class/function instantiation evaluates omitted defaults in parent-linked
  parameter scopes, canonicalizes the completed argument vector, and aliases
  the incomplete request key to that specialization.
- Syntax orchestration moved to `pa10_syntax_driver.cpp`; function-template
  lookup/binding/upgrade/instantiation moved to
  `pa19_function_template_instantiation.cpp`. Both modules are in the
  `cppgm++` source set. Oversized call paths were compacted without changing
  ownership or introducing helper fragments.
- Cross-stage compatibility is explicit: structured argument retention stops
  when a PA10 template-id contains a non-type argument, PA11 skips hidden
  structured-name children in first-language-child queries, and PA12 applies
  the same access checks and callable prefilters to structured and legacy paths.

## Performance Evidence

| Probe | Counters and seven-run median |
|---|---|
| Distinct specializations, 64/128/256 | Requests 64/128/256; layouts and member visits 128/256/512; semantic nodes 136/264/520; peak bytes 1.185/2.366/4.729 MB; semantic 4.447/8.731/18.085 ms; lowering 0.353/0.581/1.162 ms. |
| Repeated specialization, 128/256/512 | Hits 127/255/511; layouts/member visits remain 2; semantic nodes 264/520/1,032; peak bytes 0.201/0.376/0.725 MB; semantic 1.113/1.944/3.767 ms; lowering 0.538/1.019/2.058 ms. |
| Repeated omitted defaults, 128 | Function and class requests each have 127 cache hits; the class case performs one layout. |

The distinct case scales with required specialization/layout output. The
repeated case demonstrates O(1)-average completed-key lookup and no repeated
layout/member replay. Telemetry accounts for the observed slopes; no
unexplained slow path required a sampling profile.

## Validation

- `make test-pa19`: pass, 300/300 (293 handout plus 7 course tests).
- `make test-report-through-pa19`: pass, 2,013/2,013 and 19/19 stages.
- `perl scripts/cppgm_file_audit.pl --stage pa19 --paths dev/src`: pass with
  11 advisory pre-existing header-ownership warnings.
- Focused audit probes: dependent function-template defaults lower correctly;
  repeated class/function omitted defaults produce 128 requests and 127 hits.
- Changed-source scans: no host/reference invocation, filename/test branch,
  cached output, hosted-library shortcut, textual LowIR transport, or
  whole-program retry.

## Checkpoint Audit Ledger

| Checkpoint group | Independent audit disposition | Evidence |
|---|---|---|
| Core specialization (`0797d80f`-`020b715c`) | Pass after repair | Stable pattern ownership, canonical request/completed keys, dependent defaults, one retained body |
| Lookup and replay (`b346cab8`-`6da6811e`) | Pass after repair | Structured qualified identities, indexed owners/candidates, syntax-ID dependence, selected semantic facts |
| Explicit/enum/declaration checkpoints (`5ca3aed9`-`d1c73c33`) | Pass | Prior audit corrections remain monotonic and fully covered |
| Identity/callable/scalar checkpoints (`497c5381`-`74a0fccd`) | Pass | Local canonical identity, retained callable sets, local conversion keys, no registry-dependent lowering |
| Demand/default/allocation tail (`729f6952`-`63423959`) | Pass after repair | Omitted-default cache aliases and allocation binding demand flow directly to typed LowIR |
| Final PA-wide audit | Pass | Required file audit and full 19-stage report clean; no open finding |
