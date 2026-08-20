global @left : f80 = 1.0L
global @right : f80 = 1.0L
global @observed : u8 = 0
global @g1 : i64 = 1
global @g2 : i64 = 2
global @g3 : i64 = 3
global @g4 : i64 = 4
global @g5 : i64 = 5
global @g6 : i64 = 6
global @g7 : i64 = 7

function @reduced(%first : ptr, %second : ptr) -> void [binding=strong] {
  block ^entry:
    %v1 = load i64 @g1
    %v2 = load i64 @g2
    %v3 = load i64 @g3
    %v4 = load i64 @g4
    %v5 = load i64 @g5
    %v6 = load i64 @g6
    %v7 = load i64 @g7
    branch 1, ^compare, ^use_live_values

  block ^compare:
    %left = load f80 @left
    %right = load f80 @right
    %equal = cmp eq f80 %left, %right
    store u8 %equal, @observed
    return void

  block ^use_live_values:
    %s1 = binary add i64 %v1, %v2
    %s2 = binary add i64 %s1, %v3
    %s3 = binary add i64 %s2, %v4
    %s4 = binary add i64 %s3, %v5
    %s5 = binary add i64 %s4, %v6
    %sum = binary add i64 %s5, %v7
    store i64 %sum, %first
    store i64 %sum, %second
    return void
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %storage = addr @g1
    call void @reduced(%storage, %storage)
    %value = load u8 @observed
    %wrong = cmp ne u8 %value, 1
    %exit = convert zext i32 u8 %wrong
    return i32 %exit
}
