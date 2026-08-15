function @false_condition() -> u8 {
  block ^entry:
    return u8 0
}

function @clobber_return_register() -> i64 {
  block ^entry:
    return i64 1
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %condition = call u8 @false_condition()
    %ignored = call i64 @clobber_return_register()
    branch %condition, ^wrong, ^correct

  block ^wrong:
    return i32 1

  block ^correct:
    return i32 0
}
