function @main() -> i64 {
  block ^entry:
    jump ^loop

  block ^loop:
    %left = phi i64 [^entry: 1, ^latch: %right]
    %right = phi i64 [^entry: 2, ^latch: %left]
    %iteration = phi i64 [^entry: 0, ^latch: %next]
    %done = cmp ge i64 %iteration, 1
    branch %done, ^exit, ^latch

  block ^latch:
    %next = binary add i64 %iteration, 1
    jump ^loop

  block ^exit:
    %tens = binary mul i64 %left, 10
    %result = binary add i64 %tens, %right
    return i64 %result
}
