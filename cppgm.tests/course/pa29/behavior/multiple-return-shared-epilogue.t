global @void_result : i64 = 0

function @clobber() -> void {
  block ^entry:
    return void
}

function @choose(%value : i64) -> i64 {
  block ^entry:
    call void @clobber()
    %first = cmp eq i64 %value, 0
    branch %first, ^return_11, ^second_test

  block ^second_test:
    %second = cmp eq i64 %value, 1
    branch %second, ^return_22, ^third_test

  block ^third_test:
    %third = cmp eq i64 %value, 2
    branch %third, ^return_33, ^return_44

  block ^return_11:
    return i64 11

  block ^return_22:
    return i64 22

  block ^return_33:
    return i64 33

  block ^return_44:
    return i64 44
}

function @choose_float(%value : i64) -> f64 {
  block ^entry:
    call void @clobber()
    %first = cmp eq i64 %value, 0
    branch %first, ^return_first, ^second_test

  block ^second_test:
    %second = cmp eq i64 %value, 1
    branch %second, ^return_second, ^return_third

  block ^return_first:
    return f64 1.25

  block ^return_second:
    return f64 2.5

  block ^return_third:
    return f64 3.75
}

function @choose_extended(%value : i64) -> f80 {
  block ^entry:
    call void @clobber()
    %first = cmp eq i64 %value, 0
    branch %first, ^return_first, ^return_second

  block ^return_first:
    return f80 4.5L

  block ^return_second:
    return f80 5.5L
}

function @choose_void(%value : i64) -> void {
  block ^entry:
    call void @clobber()
    %first = cmp eq i64 %value, 0
    branch %first, ^return_first, ^second_test

  block ^second_test:
    %second = cmp eq i64 %value, 1
    branch %second, ^return_second, ^return_third

  block ^return_first:
    store i64 51, @void_result
    return void

  block ^return_second:
    store i64 52, @void_result
    return void

  block ^return_third:
    store i64 53, @void_result
    return void
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %a = call i64 @choose(0)
    %b = call i64 @choose(1)
    %c = call i64 @choose(2)
    %d = call i64 @choose(3)
    %f0 = call f64 @choose_float(0)
    %f1 = call f64 @choose_float(1)
    %f2 = call f64 @choose_float(2)
    %extended = call f80 @choose_extended(1)
    call void @choose_void(2)
    %void_value = load i64 @void_result
    %bad_a = cmp ne i64 %a, 11
    %bad_b = cmp ne i64 %b, 22
    %bad_c = cmp ne i64 %c, 33
    %bad_d = cmp ne i64 %d, 44
    %bad_f0 = cmp ne f64 %f0, 1.25
    %bad_f1 = cmp ne f64 %f1, 2.5
    %bad_f2 = cmp ne f64 %f2, 3.75
    %bad_extended = cmp ne f80 %extended, 5.5L
    %bad_void = cmp ne i64 %void_value, 53
    %bad_ab = binary or i64 %bad_a, %bad_b
    %bad_cd = binary or i64 %bad_c, %bad_d
    %bad_f01 = binary or i64 %bad_f0, %bad_f1
    %bad_fe = binary or i64 %bad_f2, %bad_extended
    %bad_fv = binary or i64 %bad_fe, %bad_void
    %bad_i = binary or i64 %bad_ab, %bad_cd
    %bad_f = binary or i64 %bad_f01, %bad_fv
    %bad = binary or i64 %bad_i, %bad_f
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}
