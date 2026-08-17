global @observed : i64 = 0

function @observe(%ignored : ptr, %value : i64) -> void {
  block ^entry:
    store i64 %value, @observed
    return void
}

function @copy_then_use(%source : ptr, %value : i64) -> void {
  slot $source : ptr
  slot $value : i64
  slot $copy : obj<16x8>

  block ^entry:
    store ptr %source, $source
    store i64 %value, $value
    %copy = addr $copy
    %loaded_source = load ptr $source
    copyobj 16x8 %loaded_source, %copy
    %loaded_value = load i64 $value
    call void @observe(%copy, %loaded_value)
    return void
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  slot $source : obj<16x8>

  block ^entry:
    %source = addr $source
    call void @copy_then_use(%source, 42)
    %actual = load i64 @observed
    %wrong = cmp ne i64 %actual, 42
    %exit = convert trunc i32 i64 %wrong
    return i32 %exit
}
