function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  slot $dead_first : i64
  slot $dead_second : i64
  slot $dead_third : i64
  slot $retained : i64

  block ^entry:
    store i64 1, $dead_first
    store i64 2, $dead_second
    store i64 3, $dead_third
    store i64 42, $retained
    %value = load i64 $retained
    %wrong = cmp ne i64 %value, 42
    %exit = convert trunc i32 i64 %wrong
    return i32 %exit
}
