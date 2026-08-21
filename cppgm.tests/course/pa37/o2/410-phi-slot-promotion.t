global @choose_loop : i64 = 0

function @main() -> i64 {
  slot $value : i64

  block ^entry:
    store i64 7, $value
    %choose = load i64 @choose_loop
    branch %choose, ^side, ^join

  block ^side:
    store i64 9, $value
    jump ^join

  block ^join:
    %start = load i64 $value
    store i64 %start, $value
    jump ^loop

  block ^loop:
    %current = load i64 $value
    %done = cmp ge i64 %current, 12
    branch %done, ^exit, ^latch

  block ^latch:
    %previous = load i64 $value
    %next = binary add i64 %previous, 1
    store i64 %next, $value
    jump ^loop

  block ^exit:
    %result = load i64 $value
    return i64 %result
}
