function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %take_bad = copy i64 0
    branch %take_bad, ^bad, ^step_one

  block ^step_one:
    jump ^step_two

  block ^step_two:
    %still_good = copy i64 0
    branch %still_good, ^bad, ^step_three

  block ^step_three:
    jump ^ok

  block ^ok:
    return i32 0

  block ^bad:
    return i32 1
}
