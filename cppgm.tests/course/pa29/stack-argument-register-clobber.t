function @take(%a : i64, %b : i64, %c : i64, %d : i64, %e : i64, %f : i64, %value : i64) -> i64 {
  block ^entry:
    return i64 %value
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  slot $value : i64

  block ^entry:
    store i64 1, $value
    %loaded = load i64 $value
    %result = call i64 @take(0, 0, 0, 0, 0, 0, %loaded)
    %wrong = cmp ne i64 %result, 1
    %exit = convert trunc i32 i64 %wrong
    return i32 %exit
}
