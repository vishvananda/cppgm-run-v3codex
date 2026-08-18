function @make_value(%value : i64) -> i64 {
  block ^entry:
    return i64 %value
}

function @clobber() -> i64 {
  block ^entry:
    return i64 5
}

function @probe(%take_old_path : i64) -> i64 {
  slot $joined : i64

  block ^entry:
    branch %take_old_path, ^old_value, ^constant_value

  block ^constant_value:
    store i64 7, $joined
    jump ^merge

  block ^old_value:
    %old = call i64 @make_value(91)
    %old_clobber = call i64 @clobber()
    %old_nonzero = cmp ne i64 %old, %old_clobber
    branch %old_nonzero, ^store_old, ^store_fallback

  block ^store_old:
    store i64 %old, $joined
    jump ^merge

  block ^store_fallback:
    store i64 3, $joined
    jump ^merge

  block ^merge:
    %current = load i64 $joined
    branch %current, ^use_current, ^return_zero

  block ^use_current:
    %increment = call i64 @clobber()
    %result = binary add i64 %current, %increment
    return i64 %result

  block ^return_zero:
    return i64 0
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %actual = call i64 @probe(0)
    %bad = cmp ne i64 %actual, 12
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}
