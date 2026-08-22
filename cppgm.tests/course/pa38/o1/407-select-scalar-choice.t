function @choose(%c : i64, %a : i64, %b : i64) -> i64 [unwind=no] {
  block ^entry:
    %test = cmp ne i64 %c, 0
    %picked = select i64 %test, %a, %b
    return i64 %picked
}

function @choose_narrow(%c : i64) -> i32 [unwind=no] {
  block ^entry:
    %test = cmp gt i64 %c, 9
    %picked = select i32 %test, 7, -1
    return i32 %picked
}

function @main() -> i64 [role=entry] {
  block ^entry:
    %taken = call i64 @choose(1, 40, 4)
    %skipped = call i64 @choose(0, 40, 4)
    %high = call i32 @choose_narrow(10)
    %low = call i32 @choose_narrow(9)
    %wide_high = convert sext i64 i32 %high
    %wide_low = convert sext i64 i32 %low
    %sum1 = binary add i64 %taken, %skipped
    %sum2 = binary add i64 %wide_high, %wide_low
    %total = binary add i64 %sum1, %sum2
    %bad = cmp ne i64 %total, 50
    %wide = convert zext i64 u8 %bad
    return i64 %wide
}
