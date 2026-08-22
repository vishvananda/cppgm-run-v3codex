function @spin_flag() -> i32 [unwind=no] {
  slot $flag : i32

  block ^entry:
    store volatile i32 0, $flag
    store volatile i32 1, $flag
    %first = load volatile i32 $flag
    %second = load volatile i32 $flag
    %sum = binary add i32 %first, %second
    return i32 %sum
}

function @main() -> i64 [role=entry] {
  block ^entry:
    %sum = call i32 @spin_flag()
    %bad = cmp ne i32 %sum, 2
    %wide = convert zext i64 u8 %bad
    return i64 %wide
}
