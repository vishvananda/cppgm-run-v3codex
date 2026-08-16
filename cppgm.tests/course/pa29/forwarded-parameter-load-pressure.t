global @first_value : i64 = 11
global @second_value : i64 = 13
global @third_value : i64 = 17
global @observed : i64 = 0

function @touch() -> void {
  block ^entry:
    return void
}

function @finish(%first : ptr, %second : ptr, %third : ptr, %fourth : i32) -> void {
  block ^entry:
    %first_value = load i64 %first
    %second_value = load i64 %second
    %third_value = load i64 %third
    %first_two = binary add i64 %first_value, %second_value
    %all_three = binary add i64 %first_two, %third_value
    %fourth_wide = convert sext i64 i32 %fourth
    %sum = binary add i64 %all_three, %fourth_wide
    store i64 %sum, @observed
    return void
}

function @reduced(%first : ptr, %second : ptr, %third : ptr, %fourth : i32) -> void {
  slot $first : ptr
  slot $second : ptr
  slot $third : ptr
  slot $fourth : i32

  block ^entry:
    store ptr %first, $first
    store ptr %second, $second
    store ptr %third, $third
    store i32 %fourth, $fourth
    call void @touch()
    %first_loaded = load ptr $first
    %second_loaded = load ptr $second
    %third_loaded = load ptr $third
    %fourth_loaded = load i32 $fourth
    call void @finish(%first_loaded, %second_loaded, %third_loaded, %fourth_loaded)
    return void
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %first = addr @first_value
    %second = addr @second_value
    %third = addr @third_value
    call void @reduced(%first, %second, %third, 19)
    %actual = load i64 @observed
    %wrong = cmp ne i64 %actual, 60
    %exit = convert trunc i32 i64 %wrong
    return i32 %exit
}
