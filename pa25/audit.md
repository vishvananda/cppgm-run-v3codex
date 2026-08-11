# PA25 Checkpoint Audit

## Current Checkpoint Review

Checkpoint `60cd11b4` (ordinary captureless call-operator formation) passes
after audit repair. The landed increment keys each closure by canonical
enclosing function and retained syntax, publishes its entity and call operator
before implicit-result body analysis, retains canonical parameter/default and
result facts, and lowers the selected member call and retained body directly.
The lambda expression is a typed prvalue initializer; empty closure storage is
owned by the ordinary class-initialization path.

The audit found two correctness defects in retained call-operator facts. First,
only expanded `ParameterInfo` records carried a function-parameter-pack name.
An empty expansion therefore published no name, and `sizeof...(args)` failed
when the retained body was analyzed. The call operator now owns the
declaration-level pack identity independently of its zero or more elements.
Second, body analysis changed the current class to the closure and lost the
enclosing member function's access privilege. Importing the enclosing `this`
would be incorrect for a captureless closure, so each call operator now retains
an explicit lexical parent-function edge. Access checks walk only that edge
chain and its member-owner/indexed friend facts; they do not supply an implicit
object. Explicit private-member access succeeds, while uncaptured implicit
`this` remains rejected.

The demanded template trace is source bytes -> one parsed retained template
body -> canonical function specialization -> closure key -> published closure,
parameter-pack, and lexical-access facts -> one four-state implicit-result body
analysis -> indexed empty-pack lookup -> canonical result `TypeId` -> retained
typed body -> demand worklist -> direct typed LowIR. The non-template access
trace uses the same closure fact, follows one lexical function edge to the
enclosing member owner, retains the selected field binding, and lowers without
lookup replay. The two positive regressions execute through LowIR/CY86 with
status zero; a negative regression confirms that the access edge does not
capture `this`.

For 16/64/256 sibling captureless closures performing private-member access,
tokens were 471/1,719/6,711 and semantic nodes 348/1,308/5,148. Closure requests
were 16/64/256, access path visits 32/128/512, access-grant probes
64/256/1,024, and demand pushes/emissions 18/66/258. Functions were 19/67/259,
instructions 223/847/3,343, and typed storage
66,195/256,323/1,017,459 bytes. Five-run median semantic time was
1.495/4.799/18.736 ms and lowering time 0.459/1.222/4.426 ms. Work, storage,
and time track closures and emitted IR without a translation-unit scan or
quadratic trend.

No relevant source/test shortcut, whole-program retry, lowering-time semantic
search, text transport, timeout behavior, incomplete checkpoint key, or
unresolved checkpoint-owned correctness, performance, or file-audit issue
remains. The original PA25 set remains 102/132; three audit regressions produce
105/135. Focused checkpoint and audit coverage is 8/8, PA1-24 pass
3,471/3,471, and file audit passes with 15 inherited nonfatal division
advisories. The remaining PA25 failures belong to invocation/pointer conversion,
capture layout and lookup, closure special members, broader pack substitution,
namespace identity, or the independent retained local-declaration parse path.

## Checkpoint Audit Ledger

| Checkpoint | Audit disposition |
|---|---|
| Ordinary placeholder results (`583b174a`) | Pass after cv-reference, runtime-demand, direct-ownership, and retained-body copy repairs; shipped baseline and all earlier stages preserved; linear scaling and file audit verified. |
| Range-for statements (`b985f854`) | Pass after single-parse dispatch, category-correct one-time range binding, and condition/iteration cleanup repairs; 67/128 PA25 and 3,471/3,471 earlier tests preserved; linear scaling and file audit verified. |
| Selected class conversions (`cec97359`) | Pass after single-parse canonical conversion targets, semantic-owned parameter ABI, and modifiable-reference filtering; 85/130 PA25 and 3,471/3,471 earlier tests preserved; linear scaling and file audit verified. |
| Function-template placeholder results (`2e7bf454`) | Pass after canonical dependent-result identity and semantic-owned class-result ABI repair; shipped PA25 is 88/130, audit regressions 2/2, PA1-24 3,471/3,471; linear scaling and file audit verified. |
| Ordinary captureless call operators (`60cd11b4`) | Pass after empty-pack identity and lexical access-edge repairs; original PA25 102/132 plus audit 3/3, PA1-24 3,471/3,471, runtime, linear scaling, and file audit verified. |
