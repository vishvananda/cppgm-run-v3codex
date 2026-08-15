function @zero() -> i64 {
  block ^entry:
    return i64 0
}

function @clobber_caller_saved(%a : i64, %b : i64, %c : i64, %d : i64, %e : i64) -> i64 {
  block ^entry:
    return i64 0
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %value = call i64 @zero()
    %condition = cmp ne i64 %value, 0
    %copied = copy u8 %condition
    %ignored = call i64 @clobber_caller_saved(1, 2, 3, 4, 5)
    %exit = convert zext i32 u8 %copied
    return i32 %exit
}
