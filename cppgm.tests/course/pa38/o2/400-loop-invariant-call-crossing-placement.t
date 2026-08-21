function @identity(%value : i64) -> i64 [unwind=no] {
  block ^entry:
    return i64 %value
}

function @float_loop() -> i64 [unwind=no] {
  block ^entry:
    %stable = const f64 3.5
    jump ^loop

  block ^loop:
    %same = cmp eq f64 %stable, 3.5
    branch %same, ^done, ^loop

  block ^done:
    return i64 0
}

function @main() -> i64 [role=entry] {
  block ^entry:
    %stable = binary add i64 40, 2
    jump ^loop

  block ^loop:
    %observed = call i64 @identity(%stable)
    branch %observed, ^done, ^loop

  block ^done:
    %bad = cmp ne i64 %observed, 42
    %float_bad = call i64 @float_loop()
    %result = binary or i64 %bad, %float_bad
    return i64 %result
}
