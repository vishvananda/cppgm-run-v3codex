function @choose(%value : i64) -> i64 {
  block ^entry:
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

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %a = call i64 @choose(0)
    %b = call i64 @choose(1)
    %c = call i64 @choose(2)
    %d = call i64 @choose(3)
    %bad_a = cmp ne i64 %a, 11
    %bad_b = cmp ne i64 %b, 22
    %bad_c = cmp ne i64 %c, 33
    %bad_d = cmp ne i64 %d, 44
    %bad_ab = binary or i64 %bad_a, %bad_b
    %bad_cd = binary or i64 %bad_c, %bad_d
    %bad = binary or i64 %bad_ab, %bad_cd
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}
