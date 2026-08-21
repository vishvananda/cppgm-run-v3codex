function @identity(%value : i64) -> i64 [unwind=no] {
  block ^entry:
    return i64 %value !dbg(test.cpp, 2, 3)
}

function @main() -> i64 [role=entry] {
  block ^entry:
    %observed = call i64 @identity(42) !dbg(test.cpp, 7, 5)
    branch %observed, ^done, ^bad !dbg(test.cpp, 8, 5)

  block ^done:
    %wrong = cmp ne i64 %observed, 42 !dbg(test.cpp, 11, 5)
    return i64 %wrong !dbg(test.cpp, 12, 5)

  block ^bad:
    return i64 1 !dbg(test.cpp, 15, 5)
}
