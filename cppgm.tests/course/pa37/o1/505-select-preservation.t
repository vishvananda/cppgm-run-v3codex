declare function @observe(%value : i64) -> void [unwind=no]

function @keeps_select(%c : i64, %a : i64, %b : i64) -> i64 {
  block ^entry:
    %test = cmp ne i64 %c, 0
    %picked = select i64 %test, %a, %b
    %unused = select i64 %test, %b, %a
    return i64 %picked
}

function @main() -> i64 [role=entry] {
  block ^entry:
    %one = call i64 @keeps_select(1, 30, 5)
    %two = call i64 @keeps_select(0, 30, 5)
    %sum = binary add i64 %one, %two
    %bad = cmp ne i64 %sum, 35
    %wide = convert zext i64 u8 %bad
    return i64 %wide
}
