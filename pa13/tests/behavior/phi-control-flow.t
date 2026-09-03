function @main() -> i64 [role=entry] {
  block ^entry:
    %choose_loop = const i64 0
    %from_entry = const i64 7
    branch %choose_loop, ^side, ^join

  block ^side:
    %from_side = const i64 9
    jump ^join

  block ^join:
    %start = phi i64 [^entry: %from_entry, ^side: %from_side]
    jump ^loop

  block ^loop:
    %current = phi i64 [^join: %start, ^latch: %next]
    %done = cmp ge i64 %current, 12
    branch %done, ^exit, ^latch

  block ^latch:
    %next = binary add i64 %current, 1
    jump ^loop

  block ^exit:
    %result = binary sub i64 %current, 12
    return i64 %result
}
