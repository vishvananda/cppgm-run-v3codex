function @fallback() -> i8 [unwind=no] {
  block ^entry:
    return i8 3
}

function @accumulate(%take_call : i64) -> i64 [unwind=no] {
  slot $digit : i8

  block ^entry:
    %scaled = binary mul i64 2, 10
    branch %take_call, ^call, ^direct

  block ^call:
    %called = call i8 @fallback()
    store i8 %called, $digit
    jump ^join

  block ^direct:
    store i8 3, $digit
    jump ^join

  block ^join:
    %narrow = load i8 $digit
    %wide = convert sext i64 i8 %narrow
    %result = binary add i64 %scaled, %wide
    return i64 %result
}

function @main() -> i64 [role=entry] {
  block ^entry:
    %direct = call i64 @accumulate(0)
    %called = call i64 @accumulate(1)
    %direct_bad = cmp ne i64 %direct, 23
    %called_bad = cmp ne i64 %called, 23
    %bad = binary or i64 %direct_bad, %called_bad
    return i64 %bad
}
