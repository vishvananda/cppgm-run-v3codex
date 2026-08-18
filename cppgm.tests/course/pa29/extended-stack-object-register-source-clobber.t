function @observe(%ignored : i64, %payload : obj<112x8>, %value : i64) -> i64 {
  block ^entry:
    return i64 %value
}

function @forward(%ignored : i64, %value : i64) -> i64 {
  slot $payload : obj<112x8>

  block ^entry:
    %result = call i64 @observe(%ignored, $payload, %value)
    return i64 %result
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %actual = call i64 @forward(0, 42)
    %wrong = cmp ne i64 %actual, 42
    %exit = convert trunc i32 i64 %wrong
    return i32 %exit
}
