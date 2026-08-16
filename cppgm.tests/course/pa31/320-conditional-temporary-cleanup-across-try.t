Conditional temporary cleanup keeps the enclosing catch active without making
the shared unwind-resume exit an ordinary protected-region merge.
