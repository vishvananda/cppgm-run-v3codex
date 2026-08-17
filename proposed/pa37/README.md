# Proposed PA37 optimization-shape tests

These handwritten LowIR tests isolate useful optimizer layout properties whose
exact output differs from the pinned PA37 reference.  They remain proposed
unless a behavior-only or canonical comparison can express the contract
without imposing one optimizer layout.

`380-inline-growth-budget.t` proves that fifteen repeated calls share one
caller-wide 128-instruction budget: cppgm++ inlines twelve and retains three.
The reference inlines all fifteen after simplifying the callee cost, so the
case is useful as a bounded-work proof but cannot be an active exact fixture.
