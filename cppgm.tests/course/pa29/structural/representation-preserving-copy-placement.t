global @value : i64 = 0

function @round_trip(%value : ptr) -> i64 {
  block ^entry:
    %bits = copy i64 %value
    %pointer = copy ptr %bits
    %bad = cmp ne ptr %value, %pointer
    return i64 %bad
}

function @main() -> i32 [role=entry] {
  block ^entry:
    %address = addr @value
    %bad = call i64 @round_trip(%address)
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}
