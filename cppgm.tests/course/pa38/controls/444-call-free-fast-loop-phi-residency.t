global @extra_one : i64 = 7
global @extra_two : i64 = 8
global @negative_sum : i64 = 0

function @touch_five(%a : i64, %b : i64, %c : i64, %d : i64,
                     %e : i64) -> i64 [unwind=no] {
  block ^entry:
    %ab = binary add i64 %a, %b
    %abc = binary add i64 %ab, %c
    %abcd = binary add i64 %abc, %d
    %all = binary add i64 %abcd, %e
    return i64 %all
}

function @call_free_fast_loop(%take_fast : i64, %count : i64,
                              %seed : i64) -> i64 [unwind=no] {
  block ^entry:
    branch %take_fast, ^fast_entry, ^slow

  block ^fast_entry:
    jump ^fast_loop

  block ^fast_loop:
    %index = phi i64 [^fast_entry: 0, ^fast_body: %next]
    %sum = phi i64 [^fast_entry: %seed, ^fast_body: %next_sum]
    %done = cmp uge i64 %index, %count
    branch %done, ^fast_done, ^fast_body

  block ^fast_body:
    %next_sum = binary add i64 %sum, %index
    %next = binary add i64 %index, 1
    jump ^fast_loop

  block ^fast_done:
    return i64 %sum

  block ^slow:
    %x = load i64 @extra_one
    %y = load i64 @extra_two
    %ignored = call i64 @touch_five(%take_fast, %count, %seed, %x, %y)
    %ab = binary add i64 %take_fast, %count
    %abc = binary add i64 %ab, %seed
    %abcd = binary add i64 %abc, %x
    %all = binary add i64 %abcd, %y
    return i64 %all
}

function @call_reaching_loop(%take_loop : i64, %count : i64,
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
    %next_sum = binary add i64 %sum, %index
    %next = binary add i64 %index, 1
    jump ^loop

  block ^loop_done:
    store i64 %sum, @negative_sum
    jump ^after

  block ^bypass:
    store i64 0, @negative_sum
    jump ^after

  block ^after:
    %x = load i64 @extra_one
    %y = load i64 @extra_two
    %ignored = call i64 @touch_five(%take_loop, %count, %seed, %x, %y)
    %ab = binary add i64 %take_loop, %count
    %abc = binary add i64 %ab, %seed
    %abcd = binary add i64 %abc, %x
    %all = binary add i64 %abcd, %y
    return i64 %all
}

function @main() -> i64 [role=entry] {
  block ^entry:
    %fast = call i64 @call_free_fast_loop(1, 4, 5)
    %guarded = call i64 @call_reaching_loop(1, 4, 5)
    %saved = load i64 @negative_sum
    %bad0 = cmp ne i64 %fast, 11
    %bad1 = cmp ne i64 %guarded, 25
    %bad2 = cmp ne i64 %saved, 11
    %bad3 = binary or i64 %bad0, %bad1
    %bad = binary or i64 %bad2, %bad3
    return i64 %bad
}
