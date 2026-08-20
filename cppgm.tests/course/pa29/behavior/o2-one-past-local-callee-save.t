function @noop() -> i64 {
  block ^entry:
    return i64 0
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  slot $bytes : obj<16x1>

  block ^entry:
    %kept = copy i64 7
    %ignored = call i64 @noop()
    %first = addr $bytes
    %last = index i8 %first, 16
    %distance = binary sub ptr %last, %first
    %total = binary add i64 %distance, %kept
    %wrong = cmp ne i64 %total, 23
    %exit = convert trunc i32 i64 %wrong
    return i32 %exit
}
