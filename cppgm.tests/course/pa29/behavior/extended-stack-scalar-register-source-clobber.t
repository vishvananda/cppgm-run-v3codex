function @observe(%a : i64, %b : i64, %c : i64, %d : i64, %e : i64, %f : i64, %payload : obj<1x1>, %value : i64) -> i64 {
  block ^entry:
    return i64 %value
}

function @forward(%value : i64) -> i64 {
  slot $payload : obj<1x1>

  block ^entry:
    %result = call i64 @observe(0, 0, 0, 0, 0, 0, $payload, %value)
    return i64 %result
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %actual = call i64 @forward(42)
    %wrong = cmp ne i64 %actual, 42
    %exit = convert trunc i32 i64 %wrong
    return i32 %exit
}
