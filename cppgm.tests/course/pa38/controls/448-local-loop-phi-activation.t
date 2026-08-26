function @observe(%value : i64) -> i64 [unwind=no] {
  block ^entry:
    %result = binary add i64 %value, 1
    return i64 %result
}

function @late_local_loop(%take_loop : i64, %count : i64,
                          %seed : i64) -> i64 [unwind=no] {
  block ^entry:
    %observed = call i64 @observe(%seed)
    branch %take_loop, ^loop_entry, ^bypass

  block ^loop_entry:
    jump ^loop

  block ^loop:
    %index = phi i64 [^loop_entry: 0, ^body: %next]
    %sum = phi i64 [^loop_entry: %seed, ^body: %next_sum]
    %done = cmp uge i64 %index, %count
    branch %done, ^loop_done, ^body

  block ^body:
    %next_sum = binary add i64 %sum, %index
    %next = binary add i64 %index, 1
    jump ^loop

  block ^loop_done:
    return i64 %sum

  block ^bypass:
    return i64 %seed
}

function @call_in_loop(%take_loop : i64, %count : i64,
                       %seed : i64) -> i64 [unwind=no] {
  block ^entry:
    branch %take_loop, ^loop_entry, ^bypass

  block ^loop_entry:
    jump ^loop

  block ^loop:
    %index = phi i64 [^loop_entry: 0, ^body: %next]
    %sum = phi i64 [^loop_entry: %seed, ^body: %next_sum]
    %done = cmp uge i64 %index, %count
    branch %done, ^loop_done, ^body

  block ^body:
    %next_sum = call i64 @observe(%sum)
    %next = binary add i64 %index, 1
    jump ^loop

  block ^loop_done:
    return i64 %sum

  block ^bypass:
    return i64 %seed
}

function @main() -> i64 [role=entry, unwind=no] {
  block ^entry:
    %local = call i64 @late_local_loop(1, 4, 5)
    %bypass = call i64 @late_local_loop(0, 9, 7)
    %guarded = call i64 @call_in_loop(1, 3, 11)
    %bad0 = cmp ne i64 %local, 11
    %bad1 = cmp ne i64 %bypass, 7
    %bad2 = cmp ne i64 %guarded, 14
    %bad01 = binary or i64 %bad0, %bad1
    %bad = binary or i64 %bad01, %bad2
    return i64 %bad
}
