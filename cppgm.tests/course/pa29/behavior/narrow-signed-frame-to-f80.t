function @noop() -> i64 {
  block ^entry:
    return i64 0
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %wide = copy i64 218
    %narrow = convert trunc i8 i64 %wide
    %condition = copy i64 0
    branch %condition, ^detour, ^convert

  block ^detour:
    %ignored = call i64 @noop()
    jump ^convert

  block ^convert:
    %floating = convert sitofp f80 i8 %narrow
    %roundtrip = convert fptosi i64 f80 %floating
    %wrong = cmp ne i64 %roundtrip, -38
    %exit = convert trunc i32 i64 %wrong
    return i32 %exit

}
