An unwind-resume call between protected loop calls must remain outside the
coalesced LSDA range so a noexcept cleanup is run exactly once.
