function @bump(%value : i64) -> i64 {
  block ^entry:
    %result = binary add i64 %value, 1
    return i64 %result
}

function @keep_across_edge(%seed : i64) -> i64 {
  block ^entry:
    %held = binary add i64 %seed, 7
    jump ^continuation

  block ^continuation:
    %called = call i64 @bump(30)
    %result = binary add i64 %held, %called
    return i64 %result
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %value = call i64 @keep_across_edge(5)
    %bad = cmp ne i64 %value, 43
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}
