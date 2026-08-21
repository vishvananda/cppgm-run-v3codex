function @identity(%value : i64) -> i64 [unwind=no] {
  block ^entry:
    return i64 %value
}

function @main() -> i64 [role=entry] {
  block ^entry:
    %observed = call i64 @identity(42)
    branch %observed, ^done, ^bad

  block ^done:
    %wrong = cmp ne i64 %observed, 42
    return i64 %wrong

  block ^bad:
    return i64 1
}
