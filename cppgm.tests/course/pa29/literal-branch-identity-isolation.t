global @zero : i64 = 0

function @false_condition() -> i64 {
  block ^entry:
    return i64 0
}

function @one() -> i64 {
  block ^entry:
    return i64 1
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %padding = copy i64 7
    %condition = call i64 @false_condition()
    branch %condition, ^wrong, ^continue

  block ^continue:
    %clobber = call i64 @one()
    branch 0, ^wrong, ^correct

  block ^wrong:
    return i32 1

  block ^correct:
    return i32 0
}
