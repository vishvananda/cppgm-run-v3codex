declare function @observe(%value : i64) -> void

global @cell = {
  zero 8
}

function @repeated(%base : ptr) -> i64 {
  block ^entry:
    %first = load i64 @cell
    %second = load i64 @cell
    %sum = binary add i64 %first, %second
    call void @observe(%sum)
    %third = load i64 @cell
    %total = binary add i64 %sum, %third
    return i64 %total
}

function @keeps_store(%base : ptr) -> i64 {
  block ^entry:
    %first = load i64 %base
    store i64 9, %base
    %second = load i64 %base
    %sum = binary add i64 %first, %second
    return i64 %sum
}
