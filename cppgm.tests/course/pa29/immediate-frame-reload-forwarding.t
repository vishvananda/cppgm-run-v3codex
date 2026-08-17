function @reload_to_return_register() -> i64 {
  slot $home : i64

  block ^entry:
    %value = copy i64 13
    store i64 %value, $home
    %reloaded = load i64 $home
    return i64 %reloaded
}

function @reload_to_same_register() -> i64 {
  slot $home : i64

  block ^entry:
    %value = copy i64 29
    store i64 %value, $home
    %reloaded = load i64 $home
    %adjusted = binary add i64 %reloaded, 1
    return i64 %adjusted
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %different = call i64 @reload_to_return_register()
    %same = call i64 @reload_to_same_register()
    %bad_different = cmp ne i64 %different, 13
    %bad_same = cmp ne i64 %same, 30
    %bad = binary or i64 %bad_different, %bad_same
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}
