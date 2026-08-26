global @one : i64 = 1
global @two : i64 = 2
global @three : i64 = 3
global @four : i64 = 4
global @five : i64 = 5

function @make_probe(%value : i32, %a : i64, %b : i64,
                     %c : i64, %d : i64, %e : i64) -> i32 {
  block ^entry:
    return i32 %value
}

function @pressured_compare(%value : i32) -> i64 {
  block ^entry:
    %one = load i64 @one
    %two = load i64 @two
    %three = load i64 @three
    %four = load i64 @four
    %five = load i64 @five
    %probe = call i32 @make_probe(%value, %one, %two, %three, %four, %five)
    %matches = cmp eq i32 %probe, 17
    branch %matches, ^matched, ^missed

  block ^matched:
    %sum12 = binary add i64 %one, %two
    %sum34 = binary add i64 %three, %four
    %sum1234 = binary add i64 %sum12, %sum34
    %result = binary add i64 %sum1234, %five
    return i64 %result

  block ^missed:
    return i64 99
}

function @multi_use_guard(%value : i32) -> i64 {
  block ^entry:
    %one = load i64 @one
    %two = load i64 @two
    %three = load i64 @three
    %four = load i64 @four
    %five = load i64 @five
    %probe = call i32 @make_probe(%value, %one, %two, %three, %four, %five)
    %matches = cmp eq i32 %probe, 23
    %wide = convert sext i64 i32 %probe
    branch %matches, ^matched, ^missed

  block ^matched:
    %sum12 = binary add i64 %one, %two
    %sum34 = binary add i64 %three, %four
    %sum1234 = binary add i64 %sum12, %sum34
    %sum = binary add i64 %sum1234, %five
    %result = binary add i64 %sum, %wide
    return i64 %result

  block ^missed:
    return i64 99
}

function @main() -> i64 [role=entry, unwind=no] {
  block ^entry:
    %positive = call i64 @pressured_compare(17)
    %guarded = call i64 @multi_use_guard(23)
    %positive_bad = cmp ne i64 %positive, 15
    %guarded_bad = cmp ne i64 %guarded, 38
    %bad = binary or i64 %positive_bad, %guarded_bad
    return i64 %bad
}
