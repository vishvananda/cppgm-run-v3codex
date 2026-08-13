# PA32 Checkpoint Audit

## Current Checkpoint Review

Scope: `45e35717` (`Preserve PA32 callable and member entity ABI facts`) and its canonical callable/member-entity increment. Verdict: pass after checkpoint-scoped audit fixes.

The audited declarations are the cv/ref-overloaded `bind(R (T::*)() const)` and the member NTTP `InvokeMemberTemplate<&Convert::get<int>>`; operator and conversion NTTPs exercise the other terminal forms. The ownership path is source syntax -> canonical `TypeRecord`, `BindingRecord`, template arguments, and `FunctionTemplateAbiRecipe` -> `AbiFactBuilder` -> interned ABI type/argument graph and one substitution table -> typed LowIR/MIR symbol metadata -> direct ELF. Lowering consumes recorded identities without lookup, mangled-name parsing, or an ELF-side C++ name reconstruction.

The landed structured member path preserved the owner and callable qualifiers but represented every terminal as an ordinary source name and omitted source-template arguments/result recipes. It therefore emitted literal `operator+`/`operator int` components and erased `get<int>`'s template identity. Pack specializations also repeated each concrete parameter instead of encoding the source pack once as `DpT_`. The fix adds a typed source/operator/conversion terminal, conversion target, canonical template arguments/prefix identity, dependent result, and source parameter types to the same member argument node; both the nested NTTP name and the referenced function consume the retained pack recipe. The encoder now emits those facts in the enclosing substitution sequence, and host `g++`/`cppgm++` raw symbols match exactly for all four regressions. This closes the complete producer/consumer violation under `spec.md` §§2 and 6 without adding a rendered-name fallback.

The performance audit also found `CPPGM_DRIVER_STATS` writing the observed token count into `.cppgm_object` only when telemetry was enabled. Removing that write makes stats-on/off objects byte-identical and restores `spec.md` §9's behavior-neutral telemetry rule. Process tracing of a representative compile observed only `cppgm++`'s own `execve`; the production path invokes no compiler, assembler, or reference tool.

Performance was checked with 16/32/64/128 independent operator, conversion, and member-template NTTP cases. Tokens were 3,672/7,336/14,664/29,320; semantic nodes 1,395/2,787/5,571/11,139; template requests 240/480/960/1,920; LowIR instructions 752/1,504/3,008/6,016; and object bytes 415,744/830,816/1,661,016/3,324,072. Semantic time was 13.87–111.04 ms and lowering 2.37–18.54 ms across the 8x range. Work and storage are linear in produced semantics; no global scan, retry loop, extra pass, per-node hot allocation, or name-keyed cache was introduced.

Validation: PA1–PA31 pass 4150/4150; PA32 is 91/138, preserving the 87/134 audit-turn baseline with the same 47 existing failures plus four passing audit regressions; all 117 PA14 tests pass; and the PA32 file audit passes with its 21 inherited warnings.

## Checkpoint Audit Ledger

| Checkpoint | Audit result |
| --- | --- |
| Structured dependent result/expression recipes (`f642998a`) | Pass after typed argument framing, canonical source types, and transactional ABI publication; prior and checkpoint baselines preserved. |
| Canonical callable/member-entity ABI facts (`45e35717`) | Pass after typed member terminals/template recipes and behavior-neutral telemetry; host symbols, linear scaling, prior tests, and checkpoint baseline verified. |
