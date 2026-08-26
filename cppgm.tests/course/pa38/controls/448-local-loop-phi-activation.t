global @pressure_a : i64 = 1
global @pressure_b : i64 = 2
global @pressure_c : i64 = 3
global @pressure_d : i64 = 4
global @pressure_e : i64 = 5

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

function @pressured_local_loop(%take_loop : i64) -> i64 [unwind=no] {
  block ^entry:
    %a = load i64 @pressure_a
    %b = load i64 @pressure_b
    %c = load i64 @pressure_c
    %d = load i64 @pressure_d
    %e = load i64 @pressure_e
    %observed = call i64 @observe(9)
    %left = binary add i64 %observed, 3
    %right = binary add i64 %observed, 5
    branch %take_loop, ^loop_entry, ^bypass

  block ^loop_entry:
    jump ^loop

  block ^loop:
    %index = phi i64 [^loop_entry: 0, ^body: %next]
    %sum = phi i64 [^loop_entry: 5, ^body: %next_sum]
    %done = cmp uge i64 %index, 3
    branch %done, ^loop_done, ^body

  block ^body:
    %next_sum = binary add i64 %sum, %index
    %next = binary add i64 %index, 1
    jump ^loop

  block ^loop_done:
    return i64 %sum

  block ^bypass:
    %ab = binary add i64 %a, %b
    %abc = binary add i64 %ab, %c
    %abcd = binary add i64 %abc, %d
    %abcde = binary add i64 %abcd, %e
    %partial = binary add i64 %left, %right
    %result = binary add i64 %partial, %abcde
    return i64 %result
}

function @pressured_capacity_baseline() -> i64 [unwind=no] {
  block ^entry:
    %a = load i64 @pressure_a
    %b = load i64 @pressure_b
    %c = load i64 @pressure_c
    %d = load i64 @pressure_d
    %e = load i64 @pressure_e
    %observed = call i64 @observe(9)
    %ab = binary add i64 %a, %b
    %abc = binary add i64 %ab, %c
    %abcd = binary add i64 %abc, %d
    %abcde = binary add i64 %abcd, %e
    %result = binary add i64 %abcde, %observed
    return i64 %result
}

function @main() -> i64 [role=entry, unwind=no] {
  block ^entry:
    %local = call i64 @late_local_loop(1, 4, 5)
    %bypass = call i64 @late_local_loop(0, 9, 7)
    %guarded = call i64 @call_in_loop(1, 3, 11)
    %pressured = call i64 @pressured_local_loop(1)
    %pressured_bypass = call i64 @pressured_local_loop(0)
    %capacity = call i64 @pressured_capacity_baseline()
    %bad0 = cmp ne i64 %local, 11
    %bad1 = cmp ne i64 %bypass, 7
    %bad2 = cmp ne i64 %guarded, 14
    %bad3 = cmp ne i64 %pressured, 8
    %bad4 = cmp ne i64 %pressured_bypass, 43
    %bad5 = cmp ne i64 %capacity, 25
    %bad01 = binary or i64 %bad0, %bad1
    %bad = binary or i64 %bad01, %bad2
    %bad34 = binary or i64 %bad3, %bad4
    %all_bad = binary or i64 %bad, %bad34
    %really_bad = binary or i64 %all_bad, %bad5
    return i64 %really_bad
}
