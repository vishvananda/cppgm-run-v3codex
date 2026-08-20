global @cells = {
  i64 17
  i64 19
}
global @observed : i64 = 0

function @probe(%base : ptr, %a : i64, %b : i64, %c : i64, %d : i64) -> i64 {
  block ^entry:
    %first = copy i64 100
    %second = copy i64 200
    %indexed = index i8 %base, 0
    %single = const i64 11
    %second_index = index i64 %indexed, 1
    store i64 %single, @observed
    %loaded = load i64 %second_index
    %ab = binary add i64 %a, %b
    %cd = binary add i64 %c, %d
    %ac = binary add i64 %a, %c
    %bd = binary add i64 %b, %d
    %left = binary add i64 %ab, %cd
    %right = binary add i64 %ac, %bd
    %parameters = binary add i64 %left, %right
    %observed = load i64 @observed
    %fixed = binary add i64 %first, %second
    %values = binary add i64 %loaded, %observed
    %subtotal = binary add i64 %fixed, %values
    %result = binary add i64 %subtotal, %parameters
    return i64 %result
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %base = addr @cells
    %result = call i64 @probe(%base, 1, 2, 3, 4)
    %bad = cmp ne i64 %result, 350
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}
