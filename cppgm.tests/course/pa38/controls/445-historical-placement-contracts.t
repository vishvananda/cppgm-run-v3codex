function @clobber(%a : i64, %b : i64, %c : i64, %d : i64,
                  %e : i64, %f : i64) -> i64 [unwind=no] {
  block ^entry:
    %ab = binary add i64 %a, %b
    %cd = binary add i64 %c, %d
    %ef = binary add i64 %e, %f
    %abcd = binary add i64 %ab, %cd
    %all = binary add i64 %abcd, %ef
    return i64 %all
}

global @one : i64 = 1
global @two : i64 = 2
global @three : i64 = 3
global @four : i64 = 4
global @five : i64 = 5
global @six : i64 = 6
global @seven : i64 = 7

function @post_call_free_capacity(%condition : i64) -> i64 [unwind=no] {
  block ^entry:
    %one = load i64 @one
    %two = load i64 @two
    %three = load i64 @three
    %four = load i64 @four
    %five = load i64 @five
    %ignored = call i64 @clobber(1, 2, 3, 4, 5, 6)
    %tail_one = load i64 @six
    %tail_two = load i64 @seven
    branch %condition, ^left, ^right

  block ^left:
    %left01 = binary add i64 %one, %two
    %left23 = binary add i64 %three, %four
    %left45 = binary add i64 %five, %tail_one
    %left0123 = binary add i64 %left01, %left23
    %left456 = binary add i64 %left45, %tail_two
    %left_result = binary add i64 %left0123, %left456
    return i64 %left_result

  block ^right:
    %right01 = binary add i64 %one, %two
    %right23 = binary add i64 %three, %four
    %right45 = binary add i64 %five, %tail_one
    %right0123 = binary add i64 %right01, %right23
    %right456 = binary add i64 %right45, %tail_two
    %right_sum = binary add i64 %right0123, %right456
    %right_result = binary add i64 %right_sum, 1
    return i64 %right_result
}

function @call_free_branch(%a : i64, %b : i64,
                           %condition : i64) -> i64 [unwind=no] {
  block ^entry:
    %sum = binary add i64 %a, %b
    branch %condition, ^left, ^right

  block ^left:
    %left_value = binary mul i64 %sum, 2
    return i64 %left_value

  block ^right:
    %right_value = binary sub i64 %sum, 1
    return i64 %right_value
}

function @call_crossing_branch(%a : i64, %b : i64,
                               %condition : i64) -> i64 [unwind=no] {
  block ^entry:
    %sum = binary add i64 %a, %b
    %ignored = call i64 @clobber(1, 2, 3, 4, 5, 6)
    branch %condition, ^left, ^right

  block ^left:
    %left_value = binary mul i64 %sum, 2
    return i64 %left_value

  block ^right:
    %right_value = binary sub i64 %sum, 1
    return i64 %right_value
}

function @single_use_crossing(%take_call : i64) -> i64 [unwind=no] {
  slot $digit : i8

  block ^entry:
    %scaled = binary mul i64 2, 10
    branch %take_call, ^call, ^direct

  block ^call:
    %called = call i64 @clobber(0, 0, 0, 0, 0, 3)
    %narrow_called = convert trunc i8 i64 %called
    store i8 %narrow_called, $digit
    jump ^join

  block ^direct:
    store i8 3, $digit
    jump ^join

  block ^join:
    %narrow = load i8 $digit
    %wide = convert sext i64 i8 %narrow
    %result = binary add i64 %scaled, %wide
    return i64 %result
}

function @fallback_free_float_loop() -> i64 [unwind=no] {
  block ^entry:
    %stable = const f64 3.5
    jump ^loop

  block ^loop:
    %same = cmp eq f64 %stable, 3.5
    branch %same, ^done, ^loop

  block ^done:
    return i64 0
}

function @main() -> i64 [role=entry, unwind=no] {
  block ^entry:
    %free_left = call i64 @call_free_branch(20, 22, 1)
    %free_right = call i64 @call_free_branch(20, 22, 0)
    %cross_left = call i64 @call_crossing_branch(20, 22, 1)
    %cross_right = call i64 @call_crossing_branch(20, 22, 0)
    %single_direct = call i64 @single_use_crossing(0)
    %single_called = call i64 @single_use_crossing(1)
    %tail_left = call i64 @post_call_free_capacity(1)
    %tail_right = call i64 @post_call_free_capacity(0)
    %float_bad = call i64 @fallback_free_float_loop()
    %bad0 = cmp ne i64 %free_left, 84
    %bad1 = cmp ne i64 %free_right, 41
    %bad2 = cmp ne i64 %cross_left, 84
    %bad3 = cmp ne i64 %cross_right, 41
    %bad4 = cmp ne i64 %single_direct, 23
    %bad5 = cmp ne i64 %single_called, 23
    %bad6 = cmp ne i64 %tail_left, 28
    %bad7 = cmp ne i64 %tail_right, 29
    %bad01 = binary or i64 %bad0, %bad1
    %bad23 = binary or i64 %bad2, %bad3
    %bad45 = binary or i64 %bad4, %bad5
    %bad67 = binary or i64 %bad6, %bad7
    %bad0123 = binary or i64 %bad01, %bad23
    %bad012345 = binary or i64 %bad0123, %bad45
    %bad01234567 = binary or i64 %bad012345, %bad67
    %bad = binary or i64 %bad01234567, %float_bad
    return i64 %bad
}
