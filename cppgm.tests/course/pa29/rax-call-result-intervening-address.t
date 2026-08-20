global @value : i64 = 11

function @produce() -> ptr {
  block ^entry:
    %result = addr @value
    return ptr %result
}

function @read(%address : ptr) -> i64 {
  block ^entry:
    %result = load i64 %address
    return i64 %result
}

function @exercise(%a : i64, %b : i64, %c : i64, %d : i64, %e : i64) -> i64 {
  slot $other : i64

  block ^entry:
    store i64 22, $other
    %saved = call ptr @produce()
    %other_address = addr $other
    %first = call i64 @read(%saved)
    %second = call i64 @read(%other_address)
    %third = call i64 @read(%saved)
    %fourth = call i64 @read(%other_address)
    %ab = binary add i64 %a, %b
    %abc = binary add i64 %ab, %c
    %abcd = binary add i64 %abc, %d
    %parameters = binary add i64 %abcd, %e
    %values1 = binary add i64 %first, %second
    %values2 = binary add i64 %values1, %third
    %values3 = binary add i64 %values2, %fourth
    %result = binary add i64 %values3, %parameters
    return i64 %result
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %actual = call i64 @exercise(1, 2, 3, 4, 5)
    %wrong = cmp ne i64 %actual, 81
    %exit = convert trunc i32 i64 %wrong
    return i32 %exit
}
