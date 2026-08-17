global @value : u32 = 4294967295

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %loaded = load u32 @value
    %wide = copy i64 %loaded
    %bad = cmp ne i64 %wide, 4294967295
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}
